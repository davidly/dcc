/**
 * @file dcc_mir_machine_containers.c
 * @brief Emits exact container, array, stack, and comparison schedules.
 *
 * @par Role
 * Matches array reductions and fills, row and append operations, indexed,
 * pointer, and byte stacks, plus selected scalar and floating-point checks.
 * Emits their specialized Z80 schedules after complete MIR-shape proofs.
 *
 * @par Key entry point
 * mir_try_emit_container_kernels().
 */

#include "dcc_mir_machine_internal.h"


struct MirStateMember {
    struct Sym *root;
    int root_offset;
    int member_offset;
};

/* Plan struct moved verbatim from dcc_mir_machine_emit.c (also
 * duplicated in other sibling files that need it). */

struct MirMachineForm {
    int kind;
    long value;
    int storage;
    int offset;
    int pointer_terms;
    char name[64];
};

/* Private copy of mir_machine_state_member_address (small helper duplicated per
 * family file rather than shared, matching existing convention). */

static int mir_machine_state_member_address(
    int value, struct MirStateMember *member_out)
{
    const struct MirInsn *member = mir_definition(value);
    const struct MirInsn *root_load;
    struct Sym *root;
    int memory_type;
    int memory_storage;
    int memory_offset;

    if (member == NULL || member->opcode != MIR_MEMBER_ADDRESS ||
        member->bit_width != 0 ||
        (member->memory_flags & (1 | 8)) != 0)
        return 0;
    root_load = mir_definition(member->src1);
    if (root_load == NULL || root_load->opcode != MIR_LOAD ||
        type_ptr_depth(root_load->type) == 0 ||
        type_size(root_load->type) != 2 ||
        (root_load->memory_flags & (1 | 8)) != 0 ||
        !mir_scalar_memory_location(
            root_load, &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL)
        return 0;
    root = find_global(root_load->name);
    if (root == NULL || !root->is_defined || root->is_volatile)
        return 0;
    member_out->root = root;
    member_out->root_offset = memory_offset;
    member_out->member_offset = (int)member->immediate;
    return 1;
}

/* Private copy of mir_machine_emit_global_address_hl (small helper duplicated per
 * family file rather than shared, matching existing convention). */

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

/* Private copy of mir_machine_constant_value (small helper duplicated per
 * family file rather than shared, matching existing convention). */

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

/* Private copy of mir_machine_single_call_argument (small helper duplicated per
 * family file rather than shared, matching existing convention). */

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

/* Private copy of mir_machine_two_call_arguments (small helper duplicated per
 * family file rather than shared, matching existing convention). */

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

/* Private copy of mir_machine_three_call_arguments (small helper duplicated per
 * family file rather than shared, matching existing convention). */

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

/* Enum tags paired with the plan structs above (also moved
 * verbatim from dcc_mir_machine_emit.c). */

enum MirFixedMutationKind {
    MIR_FIXED_MUTATION_SET = 1,
    MIR_FIXED_MUTATION_ADD = 2
};

enum MirIndexedStackKind {
    MIR_INDEXED_STACK_PUSH = 1,
    MIR_INDEXED_STACK_POP = 2
};

enum MirPointerStackKind {
    MIR_POINTER_STACK_PUSH = 1,
    MIR_POINTER_STACK_POP = 2
};

enum MirByteMemoryStackKind {
    MIR_BYTE_MEMORY_STACK_PUSH = 1,
    MIR_BYTE_MEMORY_STACK_POP = 2,
    MIR_BYTE_MEMORY_STACK_PUSH_WORD = 3
};

enum MirConditionalFloatLongKind {
    MIR_CONDITIONAL_FLOAT_ADD = 1,
    MIR_CONDITIONAL_FLOAT_CALL = 2
};

enum MirFinalCallCheckKind {
    MIR_FINAL_CALL_NESTED_CONSTANT = 1,
    MIR_FINAL_CALL_NESTED_STRING,
    MIR_FINAL_CALL_DIRECT
};

enum MirMathConstantKind {
    MIR_MATH_FLOAT_CONSTANT = 1,
    MIR_MATH_WORD_CONSTANT
};

enum MirAtofScheduleCheckKind {
    MIR_ATOF_SCHEDULE_FLOAT = 1,
    MIR_ATOF_SCHEDULE_INT,
    MIR_ATOF_SCHEDULE_END,
    MIR_ATOF_SCHEDULE_INFINITY,
    MIR_ATOF_SCHEDULE_NAN
};

enum MirMachineFormKind {
    MIR_MACHINE_FORM_INTEGER = 1,
    MIR_MACHINE_FORM_POINTER = 2
};

/* The following struct/macro definitions are shared plan
 * types used by helper functions in this file; moved here
 * verbatim from dcc_mir_machine_emit.c during the family
 * split. */

struct MirFixedMutation {
    int kind;
    int offset;
    int width;
    unsigned long value;
};

struct MirGlobalAppendStore {
    int parameter_stack_offset;
    int field_offset;
    int width;
};

struct MirNestedAppendStore {
    int parameter_stack_offset;
    int array_member_offset;
    int element_stride;
    int width;
};

#define MIR_FINAL_CALL_CHECK_COUNT 43

struct MirFinalCallCheck {
    struct Sym *check_function;
    struct Sym *value_function;
    int kind;
    int width;
    int name_string_id;
    int value_string_id;
    unsigned long value;
    unsigned long expected;
    unsigned long direct_values[4];
};

#define MIR_MATH_CALL_CHECK_COUNT 78
#define MIR_MATH_ORDINARY_CHECK_COUNT 74
#define MIR_MATH_FREXP_CHECK_COUNT 4
#define MIR_MATH_MODEXP_CHECK_COUNT 4
#define MIR_MATH_MODF_CHECK_COUNT 2

struct MirMathConstant {
    int kind;
    unsigned long value;
};

struct MirMathCallCheck {
    struct Sym *value_function;
    struct Sym *check_function;
    struct MirMathConstant arguments[2];
    int argument_count;
    int string_id;
    unsigned long expected_bits;
};

struct MirMathPointerCheck {
    struct Sym *value_function;
    struct Sym *check_function;
    unsigned long input_bits;
    unsigned long result_bits;
    unsigned long output_bits;
    int result_string_id;
    int output_string_id;
};

#define MIR_MAX_CTYPE_CHECKS 32

struct MirCtypeCheck {
    struct Sym *value_function;
    unsigned long input;
    unsigned long expected;
    int comparison;
    int string_id;
};

#define MIR_CONTEXT_OP_CHECK_COUNT 33
#define MIR_CONTEXT_OP_MAX_ARGUMENTS 4

struct MirContextOpArgument {
    unsigned long value;
    int width;
};

struct MirContextOpCheck {
    struct Sym *value_function;
    struct Sym *check_function;
    struct MirContextOpArgument arguments[MIR_CONTEXT_OP_MAX_ARGUMENTS];
    unsigned long expected;
    int argument_count;
    int result_width;
    int result_unsigned;
    int check_width;
    int check_unsigned;
    int check_float;
    int string_id;
};

#define MIR_FORMAT_BUFFER_CHECK_COUNT 61

struct MirFormatBufferCheck {
    int format_string_id;
    int expected_string_id;
    int description_string_id;
    unsigned long value;
    int value_size;
    int runtime_flags;
    char call_name[64];
};

#define MIR_ATOF_SCHEDULE_CHECK_COUNT 46

struct MirAtofScheduleCheck {
    int kind;
    int name_string_id;
    int input_string_id;
    unsigned long expected;
    unsigned long tolerance;
    unsigned long scale;
    int has_scale;
};

struct MirRowMemberAddress {
    struct Sym *root;
    int root_pointer_offset;
    int row_stride;
    int member_offset;
    int index_value;
};
#include <stdint.h>

struct MirNestedRowStore {
    struct Sym *root;
    int root_pointer_offset;
    int row_stride;
    int value_member_offset;
    int count_member_offset;
    int element_stride;
    int index_stack_offset;
    int value_stack_offset;
};

struct MirFlatArrayChecks {
    struct Sym *check_function;
    int parameter_stack_offset;
    int width;
    int is_unsigned;
    int count;
    int offsets[16];
    int strings[16];
};

struct MirFixedParamMutations {
    int parameter_stack_offset;
    int count;
    struct MirFixedMutation mutations[8];
};

struct MirGlobalByteChecks {
    struct Sym *check_function;
    struct Sym *symbols[16];
    int offsets[16];
    int expected[16];
    int strings[16];
    int is_unsigned[16];
    int count;
};

struct MirGlobalAppend {
    struct Sym *root;
    int array_offset;
    int count_offset;
    int element_stride;
    int store_count;
    struct MirGlobalAppendStore stores[8];
};

struct MirNestedAppend {
    struct Sym *root;
    int root_pointer_offset;
    int row_stride;
    int row_index_value;
    int row_index_stack_offset;
    int count_member_offset;
    int store_count;
    struct MirNestedAppendStore stores[8];
};

struct MirIndexedStack {
    int kind;
    struct Sym *root;
    int root_offset;
    int base_member_offset;
    int index_member_offset;
    int element_width;
    int value_stack_offset;
};

struct MirPointerStack {
    int kind;
    struct Sym *root;
    int root_offset;
    int pointer_member_offset;
    int value_stack_offset;
};

struct MirByteMemoryStack {
    int kind;
    struct Sym *memory_root;
    int memory_offset;
    struct Sym *cursor_root;
    int cursor_offset;
    int value_stack_offset;
};

struct MirFixedArrayReduction {
    int parameter_stack_offset;
    int element_width;
    int element_is_unsigned;
};

struct MirFixedArrayAffineFill {
    struct Sym *function;
    int pointer_stack_offset;
    int base_stack_offset;
    int element_width;
};

struct MirWordRangeBool {
    int parameter_stack_offset;
    int lower;
    int upper;
};

struct MirAsciiUpper {
    int parameter_stack_offset;
    int width;
    int lower;
    int upper;
    int adjustment;
};

struct MirFixedWordArraySum {
    int parameter_stack_offset;
    int count;
    int pointer_is_volatile;
};

struct MirSliceWordSum {
    int parameter_stack_offset;
    int data_offset;
    int count_offset;
};

struct MirConditionalNullIdentity {
    int condition_stack_offset;
    int pointer_stack_offset;
};

struct MirWideConstantEqual {
    int parameter_stack_offset;
    unsigned long value;
};

struct MirFloatTruthOnce {
    int parameter_stack_offset;
};

struct MirNestedWordLongSelect {
    int first_condition_stack_offset;
    int second_condition_stack_offset;
    int third_stack_offset;
    int first_value;
    int second_value;
    int third_value;
    int third_is_parameter;
};

struct MirFloatIntTruth {
    int float_stack_offset;
    int int_stack_offset;
    int operation;
};

struct MirConditionalFloatLong {
    struct Sym *function;
    int condition_stack_offset;
    int argument_stack_offset;
    int true_value;
    unsigned long add_bits;
    int kind;
};

struct MirConditionalPointerFloatLong {
    struct Sym *true_root;
    struct Sym *false_pointer;
    int condition_stack_offset;
    int element_offset;
};

struct MirNestedMemberFloatLong {
    int first_condition_stack_offset;
    int second_condition_stack_offset;
    int pointer_stack_offset;
    int member_offset;
    int first_value;
    int second_value;
};

struct MirConditionalFloatCompareLong {
    int condition_stack_offset;
    int float_stack_offset;
    int true_value;
    int positive_value;
    int nonpositive_value;
};

struct MirConditionalBool {
    int condition_stack_offset;
    int true_value;
    int false_value;
    int result_type;
};

struct MirLogicalOrParameters {
    int first_stack_offset;
    int second_stack_offset;
};

struct MirClearedRecordAppend {
    struct Sym *root;
    struct Sym *clear_function;
    struct Sym *copy_function;
    int root_offset;
    int array_member_offset;
    int cursor_member_offset;
    int stride;
    int name_field_offset;
    int kind_field_offset;
    int value_field_offset;
    int name_stack_offset;
    int kind_stack_offset;
    int value_stack_offset;
};

struct MirRecordNameSearch {
    struct Sym *root;
    struct Sym *compare_function;
    int root_offset;
    int array_member_offset;
    int cursor_member_offset;
    int stride;
    int name_field_offset;
    int name_stack_offset;
};

struct MirSequentialUnaryReports {
    struct Sym *helper;
    struct Sym *print_function;
    int parameter_stack_offsets[5];
    int string_id;
};

struct MirNibbleAppend {
    int pointer_stack_offset;
    int value_stack_offset;
    int threshold;
    int low_adjustment;
    int high_adjustment;
};

struct MirVolatileFillWideConstant {
    int buffer_offset;
    int count;
    unsigned long result;
};

struct MirSingleSignedDivCheck {
    struct Sym *check_function;
    int numerator_stack_offset;
    int denominator_stack_offset;
    int expected_stack_offset;
    int label_stack_offset;
    int operation;
};

struct MirWideDivResultCheck {
    struct Sym *divide_function;
    struct Sym *failure_function;
    int name_stack_offset;
    int numerator_stack_offset;
    int denominator_stack_offset;
    int expected_quotient_stack_offset;
    int expected_remainder_stack_offset;
    int result_offset;
};

struct MirFinalCallCheckSchedule {
    struct MirFinalCallCheck checks[MIR_FINAL_CALL_CHECK_COUNT];
    struct Sym *failure_root;
    struct Sym *print_function;
    int failure_offset;
    int success_string_id;
};

struct MirMathVerificationSchedule {
    struct MirMathCallCheck calls[MIR_MATH_CALL_CHECK_COUNT];
    struct MirMathPointerCheck frexp[MIR_MATH_FREXP_CHECK_COUNT];
    struct MirMathPointerCheck modf[MIR_MATH_MODF_CHECK_COUNT];
    struct Sym *print_function;
    struct Sym *checks_root;
    struct Sym *failures_root;
    int checks_offset;
    int failures_offset;
    int intro_string_id;
    int separator_string_id;
    int summary_string_id;
    int success_string_id;
    int failure_string_id;
};

struct MirCtypeReallocSchedule {
    struct MirCtypeCheck checks[MIR_MAX_CTYPE_CHECKS];
    struct Sym *check_function;
    struct Sym *allocate_function;
    struct Sym *copy_function;
    struct Sym *resize_function;
    struct Sym *compare_function;
    struct Sym *free_function;
    struct Sym *print_function;
    struct Sym *failure_root;
    unsigned long allocation_size;
    unsigned long grow_size;
    unsigned long shrink_size;
    unsigned long stored_values[2];
    unsigned long byte_expected[2];
    int source_string_id;
    int allocation_failure_string_id;
    int grow_failure_string_id;
    int preserve_string_id;
    int shrink_failure_string_id;
    int byte_string_ids[2];
    int final_failure_string_id;
    int success_string_id;
    int store_indices[2];
    int byte_indices[2];
    int byte_unsigned[2];
    int failure_offset;
    int check_count;
};

struct MirContextOpSchedule {
    struct MirContextOpCheck checks[MIR_CONTEXT_OP_CHECK_COUNT];
    struct Sym *array_root;
    struct Sym *float_roots[2];
    struct Sym *failure_root;
    struct Sym *print_function;
    unsigned long float_values[2];
    int array_offset;
    int float_offsets[2];
    int failure_offset;
    int success_string_id;
    int failure_string_id;
};

struct MirFormatBufferSchedule {
    struct MirFormatBufferCheck checks[MIR_FORMAT_BUFFER_CHECK_COUNT];
    struct Sym *format_function;
    struct Sym *check_function;
    struct Sym *print_function;
    struct Sym *failure_root;
    int failure_offset;
    int success_string_id;
    int failure_string_id;
    char success_call_name[64];
    char failure_call_name[64];
};

struct MirAtofSchedule {
    struct MirAtofScheduleCheck checks[MIR_ATOF_SCHEDULE_CHECK_COUNT];
    struct Sym *value_function;
    struct Sym *float_check_function;
    struct Sym *int_check_function;
    struct Sym *end_check_function;
    struct Sym *infinity_check_function;
    struct Sym *nan_check_function;
    struct Sym *print_function;
    struct Sym *failure_root;
    struct Sym *checks_root;
    int failure_offset;
    int checks_offset;
    int failure_string_id;
    int success_string_id;
};

struct MirLocalIdentityArrayResult {
    struct Sym *escaped_pointer;
    int escaped_pointer_offset;
    int array_offset;
    int result;
};

static int mir_machine_row_member_address(
    int value, struct MirRowMemberAddress *address)
{
    const struct MirInsn *member = mir_definition(value);
    const struct MirInsn *row_index;
    const struct MirInsn *rows_load;
    const struct MirInsn *rows_member;
    const struct MirInsn *root;

    if (member == NULL || member->opcode != MIR_MEMBER_ADDRESS ||
        (member->memory_flags & (1 | 8)) != 0)
        return 0;
    row_index = mir_definition(member->src1);
    if (row_index == NULL || row_index->opcode != MIR_INDEX_ADDRESS ||
        row_index->immediate <= 0 ||
        (row_index->memory_flags & 1) != 0)
        return 0;
    rows_load = mir_definition(row_index->src1);
    if (rows_load == NULL || rows_load->opcode != MIR_LOAD_INDIRECT ||
        rows_load->memory_size != 2 ||
        (rows_load->memory_flags & (1 | 8)) != 0)
        return 0;
    rows_member = mir_definition(rows_load->src1);
    if (rows_member == NULL ||
        rows_member->opcode != MIR_MEMBER_ADDRESS ||
        (rows_member->memory_flags & (1 | 8)) != 0)
        return 0;
    root = mir_machine_resolve_local_alias(rows_member->src1);
    if (root == NULL || root->opcode != MIR_ADDRESS)
        return 0;
    address->root = find_global(root->name);
    if (address->root == NULL || !address->root->is_defined ||
        address->root->is_volatile)
        return 0;
    address->root_pointer_offset = (int)rows_member->immediate;
    address->row_stride = (int)row_index->immediate;
    address->member_offset = (int)member->immediate;
    address->index_value = row_index->src2;
    return 1;
}

static int mir_machine_same_row(
    const struct MirRowMemberAddress *left,
    const struct MirRowMemberAddress *right)
{
    return left->root == right->root &&
           left->root_pointer_offset == right->root_pointer_offset &&
           left->row_stride == right->row_stride &&
           left->index_value == right->index_value;
}

static int mir_match_wide_div_result_access(
    int address_index, int member_offset,
    const struct MirInsn *result_address)
{
    const struct MirInsn *address = &mir.insns[address_index];
    const struct MirInsn *member = &mir.insns[address_index + 1];
    const struct MirInsn *load = &mir.insns[address_index + 2];
    int memory_offset;
    int memory_storage;
    int memory_type;

    if (!mir_scalar_memory_location(
            address, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_LOCAL || memory_offset != -8 ||
        type_size(memory_type) != 8 ||
        (result_address != NULL &&
         !mir_machine_same_location(address, result_address)) ||
        member->src1 != address->dst ||
        member->immediate != member_offset ||
        load->src1 != member->dst ||
        load->memory_size != 4 ||
        load->memory_flags != 0 ||
        type_ptr_depth(load->type) != 0 ||
        (load->type & 15) != TYPE_LONG ||
        (load->type & TYPE_UNSIGNED) != 0 ||
        type_is_float(load->type))
        return 0;
    return 1;
}

static int mir_match_wide_parameter_offset(
    const struct MirInsn *parameter, int *stack_offset)
{
    int memory_offset;
    int memory_storage;
    int memory_type;

    if (parameter->opcode != MIR_PARAM ||
        !mir_scalar_memory_location(
            parameter, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM ||
        type_ptr_depth(memory_type) != 0 ||
        (memory_type & 15) != TYPE_LONG ||
        (memory_type & TYPE_UNSIGNED) != 0 ||
        type_is_float(memory_type) ||
        type_size(memory_type) != 4)
        return 0;
    *stack_offset = memory_offset - 2;
    return *stack_offset >= 0;
}

static int mir_match_final_call_function(
    const struct MirInsn *call, int argument_count,
    int argument_width)
{
    struct Sym *function = find_global(call->name);
    int argument;

    if (function == NULL || function->is_funcptr ||
        function->is_noreturn || !function->has_proto ||
        function->proto_nargs != argument_count ||
        function->proto_variadic ||
        (call->type & 15) != TYPE_VOID ||
        call->memory_flags != 0 ||
        type_ptr_depth(function->proto_types[0]) != 1 ||
        type_size(function->proto_types[0]) != 2)
        return 0;
    for (argument = 1; argument < argument_count; ++argument)
        if (!mir_match_final_call_integer_type(
                function->proto_types[argument],
                argument_width))
            return 0;
    return 1;
}

static int mir_match_final_value_function(
    const struct MirInsn *call, int argument_kind, int width)
{
    struct Sym *function = find_global(call->name);

    if (function == NULL || function->is_funcptr ||
        function->is_noreturn || !function->has_proto ||
        function->proto_nargs != 1 ||
        function->proto_variadic ||
        call->memory_flags != 0 ||
        !mir_match_final_call_integer_type(call->type, width))
        return 0;
    if (argument_kind == MIR_FINAL_CALL_NESTED_STRING)
        return type_ptr_depth(function->proto_types[0]) == 1 &&
            type_size(function->proto_types[0]) == 2;
    return mir_match_final_call_integer_type(
        function->proto_types[0], width);
}

static int mir_match_final_string_value(int value, int *string_id)
{
    const struct MirInsn *definition = mir_definition(value);

    if (definition == NULL ||
        definition->opcode != MIR_STRING_ADDRESS ||
        type_ptr_depth(definition->type) != 1 ||
        type_size(definition->type) != 2)
        return 0;
    *string_id = (int)definition->immediate;
    return 1;
}

static int mir_match_final_nested_call(
    struct MirFinalCallCheck *check,
    const struct MirInsn *value_call,
    const struct MirInsn *check_call,
    int argument_kind, int width,
    struct Sym **value_function,
    struct Sym **check_function)
{
    int check_arguments[3];
    int value_argument;
    long constant;

    memset(check, 0, sizeof(*check));
    if (!mir_machine_single_call_argument(
            value_call, &value_argument) ||
        !mir_machine_three_call_arguments(
            check_call, check_arguments) ||
        check_arguments[1] != value_call->dst ||
        !mir_match_final_value_function(
            value_call, argument_kind, width) ||
        !mir_match_final_call_function(check_call, 3, width) ||
        !mir_match_final_string_value(
            check_arguments[0], &check->name_string_id))
        return 0;
    check->value_function = find_global(value_call->name);
    check->check_function = find_global(check_call->name);
    if ((*value_function != NULL &&
         *value_function != check->value_function) ||
        (*check_function != NULL &&
         *check_function != check->check_function))
        return 0;
    *value_function = check->value_function;
    *check_function = check->check_function;
    if (argument_kind == MIR_FINAL_CALL_NESTED_STRING) {
        if (!mir_match_final_string_value(
                value_argument, &check->value_string_id))
            return 0;
    } else {
        if (!mir_machine_evaluate_constant(
                value_argument, &constant, 0))
            return 0;
        check->value = (unsigned long)constant;
    }
    if (!mir_machine_evaluate_constant(
            check_arguments[2], &constant, 0))
        return 0;
    check->kind = argument_kind;
    check->width = width;
    check->expected = (unsigned long)constant;
    return 1;
}

static int mir_match_final_direct_call(
    struct MirFinalCallCheck *check,
    const struct MirInsn *call, int width,
    struct Sym **check_function)
{
    int arguments[5];
    int argument;
    long constant;

    memset(check, 0, sizeof(*check));
    if (!mir_machine_five_call_arguments(call, arguments) ||
        !mir_match_final_call_function(call, 5, width) ||
        !mir_match_final_string_value(
            arguments[0], &check->name_string_id))
        return 0;
    check->check_function = find_global(call->name);
    if (*check_function != NULL &&
        *check_function != check->check_function)
        return 0;
    *check_function = check->check_function;
    for (argument = 0; argument < 4; ++argument) {
        if (!mir_machine_evaluate_constant(
                arguments[argument + 1], &constant, 0))
            return 0;
        check->direct_values[argument] =
            (unsigned long)constant;
    }
    check->kind = MIR_FINAL_CALL_DIRECT;
    check->width = width;
    return 1;
}

static int mir_match_math_float_type(int type)
{
    return type_ptr_depth(type) == 0 &&
        type_is_float(type) && type_size(type) == 4;
}

static int mir_match_math_float_constant(
    int value, unsigned long *bits)
{
    const struct MirInsn *definition = mir_definition(value);

    if (definition == NULL ||
        !mir_match_math_float_type(definition->type))
        return 0;
    if (definition->opcode == MIR_FLOAT_CONST) {
        *bits = (unsigned long)definition->immediate & 0xffffffffUL;
        return 1;
    }
    if (definition->opcode == MIR_UNARY &&
        definition->immediate == '-') {
        const struct MirInsn *source =
            mir_definition(definition->src1);

        if (source == NULL ||
            source->opcode != MIR_FLOAT_CONST ||
            !mir_match_math_float_type(source->type))
            return 0;
        *bits = ((unsigned long)source->immediate ^
                 0x80000000UL) & 0xffffffffUL;
        return 1;
    }
    return 0;
}

static int mir_match_math_word_constant(
    int value, unsigned long *word)
{
    const struct MirInsn *definition = mir_definition(value);
    long constant;

    if (definition == NULL ||
        type_ptr_depth(definition->type) != 0 ||
        type_is_float(definition->type) ||
        (definition->type & 15) != TYPE_INT ||
        (definition->type & TYPE_UNSIGNED) != 0 ||
        type_size(definition->type) != 2 ||
        !mir_machine_evaluate_constant(value, &constant, 0))
        return 0;
    *word = (unsigned long)constant & 0xffffUL;
    return 1;
}

static int mir_match_math_check_function(
    const struct MirInsn *call, struct Sym **expected)
{
    struct Sym *function = find_global(call->name);

    if (function == NULL || !function->is_defined ||
        function->is_funcptr || function->is_noreturn ||
        !function->has_proto || function->proto_nargs != 3 ||
        function->proto_variadic ||
        type_ptr_depth(function->proto_types[0]) != 1 ||
        type_size(function->proto_types[0]) != 2 ||
        !mir_match_math_float_type(function->proto_types[1]) ||
        !mir_match_math_float_type(function->proto_types[2]) ||
        (call->type & 15) != TYPE_VOID ||
        call->memory_flags != 0 ||
        !mir_match_math_symbol_target(call, function) ||
        (*expected != NULL && *expected != function))
        return 0;
    *expected = function;
    return 1;
}

static int mir_match_math_value_function(
    const struct MirInsn *call, struct Sym **function_out,
    int *argument_count)
{
    struct Sym *function = find_global(call->name);

    if (function == NULL || function->is_funcptr ||
        function->is_noreturn || !function->has_proto ||
        function->proto_variadic ||
        (function->proto_nargs != 1 &&
         function->proto_nargs != 2) ||
        !mir_match_math_float_type(call->type) ||
        call->memory_flags != 0 ||
        !mir_match_math_symbol_target(call, function))
        return 0;
    *function_out = function;
    *argument_count = function->proto_nargs;
    return 1;
}

static int mir_match_math_call_check(
    struct MirMathCallCheck *plan,
    const struct MirInsn *value_call,
    const struct MirInsn *check_call,
    struct Sym **check_function)
{
    int check_arguments[3];
    int value_arguments[2] = {-1, -1};
    int argument;

    memset(plan, 0, sizeof(*plan));
    if (value_call >= check_call ||
        !mir_match_math_value_function(
            value_call, &plan->value_function,
            &plan->argument_count) ||
        !mir_match_math_check_function(
            check_call, check_function) ||
        !mir_machine_three_call_arguments(
            check_call, check_arguments) ||
        check_arguments[1] != value_call->dst ||
        !mir_match_final_string_value(
            check_arguments[0], &plan->string_id) ||
        !mir_match_math_float_constant(
            check_arguments[2], &plan->expected_bits))
        return 0;
    plan->check_function = *check_function;
    if (plan->argument_count == 1) {
        if (!mir_machine_single_call_argument(
                value_call, &value_arguments[0]))
            return 0;
    } else if (!mir_machine_two_call_arguments(
                   value_call, value_arguments)) {
        return 0;
    }
    for (argument = 0;
         argument < plan->argument_count; ++argument) {
        int type =
            plan->value_function->proto_types[argument];
        const struct MirInsn *definition =
            mir_definition(value_arguments[argument]);

        if (definition == NULL || definition >= value_call)
            return 0;
        if (mir_match_math_float_type(type)) {
            plan->arguments[argument].kind =
                MIR_MATH_FLOAT_CONSTANT;
            if (!mir_match_math_float_constant(
                    value_arguments[argument],
                    &plan->arguments[argument].value))
                return 0;
        } else {
            plan->arguments[argument].kind =
                MIR_MATH_WORD_CONSTANT;
            if (type_ptr_depth(type) != 0 ||
                type_is_float(type) ||
                (type & 15) != TYPE_INT ||
                (type & TYPE_UNSIGNED) != 0 ||
                type_size(type) != 2 ||
                !mir_match_math_word_constant(
                    value_arguments[argument],
                    &plan->arguments[argument].value))
                return 0;
        }
    }
    return 1;
}

static const struct MirInsn *mir_match_math_result_store(
    const struct MirInsn *value_call,
    const struct MirInsn *first_check)
{
    const struct MirInsn *store = NULL;
    int memory_offset;
    int memory_storage;
    int memory_type;
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *candidate = &mir.insns[instruction];

        if (candidate->opcode != MIR_STORE ||
            candidate->src1 != value_call->dst)
            continue;
        if (store != NULL)
            return NULL;
        store = candidate;
    }
    if (store == NULL || store <= value_call ||
        store >= first_check ||
        !mir_machine_unobservable_local_store(store) ||
        !mir_scalar_memory_location(
            store, &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_LOCAL ||
        !mir_match_math_float_type(memory_type) ||
        store->memory_size != 4 ||
        store->memory_flags != 0)
        return NULL;
    return store;
}

static int mir_match_math_output_address(
    const struct MirInsn *address, int base_type,
    int *offset_out)
{
    int memory_offset;
    int memory_storage;
    int memory_type;

    if (address == NULL || address->opcode != MIR_ADDRESS ||
        type_ptr_depth(address->type) != 1 ||
        type_size(address->type) != 2 ||
        !mir_machine_named_nonvolatile(address) ||
        !mir_scalar_memory_location(
            address, &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_LOCAL ||
        type_ptr_depth(memory_type) != 0 ||
        (memory_type & 15) != base_type ||
        type_size(memory_type) !=
            (base_type == TYPE_FLOAT ? 4 : 2))
        return 0;
    *offset_out = memory_offset;
    return 1;
}

static int mir_match_math_same_location(
    const struct MirInsn *left, const struct MirInsn *right)
{
    int left_offset;
    int left_storage;
    int left_type;
    int right_offset;
    int right_storage;
    int right_type;

    return left != NULL && right != NULL &&
        mir_scalar_memory_location(
            left, &left_type, &left_storage, &left_offset) &&
        mir_scalar_memory_location(
            right, &right_type, &right_storage, &right_offset) &&
        left_storage == right_storage &&
        left_offset == right_offset &&
        !strcmp(left->name, right->name);
}

static int mir_match_math_pointer_function(
    const struct MirInsn *call, int pointer_base_type,
    struct Sym **function_out)
{
    struct Sym *function = find_global(call->name);

    if (function == NULL || function->is_funcptr ||
        function->is_noreturn || !function->has_proto ||
        function->proto_nargs != 2 ||
        function->proto_variadic ||
        !mir_match_math_float_type(function->proto_types[0]) ||
        type_ptr_depth(function->proto_types[1]) != 1 ||
        (function->proto_types[1] & 15) != pointer_base_type ||
        type_size(function->proto_types[1]) != 2 ||
        !mir_match_math_float_type(call->type) ||
        call->memory_flags != 0 ||
        !mir_match_math_symbol_target(call, function))
        return 0;
    *function_out = function;
    return 1;
}

static int mir_match_math_frexp_check(
    struct MirMathPointerCheck *plan,
    const struct MirInsn *value_call,
    const struct MirInsn *result_check,
    const struct MirInsn *output_check,
    struct Sym **check_function,
    const struct MirInsn **output_location,
    const struct MirInsn **result_location)
{
    const struct MirInsn *address;
    const struct MirInsn *conversion;
    const struct MirInsn *load;
    const struct MirInsn *store;
    int result_arguments[3];
    int output_arguments[3];
    int value_arguments[2];
    int output_offset;

    memset(plan, 0, sizeof(*plan));
    if (value_call >= result_check ||
        result_check >= output_check ||
        !mir_match_math_pointer_function(
            value_call, TYPE_INT, &plan->value_function) ||
        !mir_machine_two_call_arguments(
            value_call, value_arguments) ||
        !mir_match_math_float_constant(
            value_arguments[0], &plan->input_bits) ||
        (address = mir_definition(value_arguments[1])) == NULL ||
        !mir_match_math_output_address(
            address, TYPE_INT, &output_offset) ||
        output_offset >= 0)
        return mir_machine_reject(
            "math-frexp-check", "value-call");
    if (!mir_match_math_check_function(
            result_check, check_function) ||
        !mir_match_math_check_function(
            output_check, check_function) ||
        !mir_machine_three_call_arguments(
            result_check, result_arguments) ||
        result_arguments[1] != value_call->dst ||
        !mir_match_final_string_value(
            result_arguments[0], &plan->result_string_id) ||
        !mir_match_math_float_constant(
            result_arguments[2], &plan->result_bits) ||
        !mir_machine_three_call_arguments(
            output_check, output_arguments) ||
        !mir_match_final_string_value(
            output_arguments[0], &plan->output_string_id) ||
        !mir_match_math_float_constant(
            output_arguments[2], &plan->output_bits))
        return mir_machine_reject(
            "math-frexp-check", "check-calls");
    plan->check_function = *check_function;
    store = mir_match_math_result_store(
        value_call, result_check);
    conversion = mir_definition(output_arguments[1]);
    if (store == NULL)
        return mir_machine_reject(
            "math-frexp-check", "result-store");
    if (conversion == NULL ||
        conversion->opcode != MIR_UNARY ||
        conversion->immediate != 0 ||
        !mir_match_math_float_type(conversion->type))
        return mir_machine_reject(
            "math-frexp-check", "output-conversion");
    load = mir_definition(conversion->src1);
    if (load == NULL ||
        load->opcode != MIR_LOAD ||
        load <= result_check || load >= output_check ||
        type_ptr_depth(load->type) != 0 ||
        (load->type & 15) != TYPE_INT ||
        type_size(load->type) != 2 ||
        !mir_match_math_same_location(address, load) ||
        load->memory_flags != 0)
        return mir_machine_reject(
            "math-frexp-check", "output-load");
    if ((*output_location != NULL &&
         !mir_match_math_same_location(
             *output_location, address)) ||
        (*result_location != NULL &&
         !mir_match_math_same_location(
             *result_location, store)) ||
        !strcmp(address->name, store->name))
        return mir_machine_reject(
            "math-frexp-check", "location-identity");
    *output_location = address;
    *result_location = store;
    return 1;
}

static int mir_match_math_modf_check(
    struct MirMathPointerCheck *plan,
    const struct MirInsn *value_call,
    const struct MirInsn *result_check,
    const struct MirInsn *output_check,
    struct Sym **check_function,
    const struct MirInsn **output_location,
    const struct MirInsn **result_location)
{
    const struct MirInsn *address;
    const struct MirInsn *load;
    const struct MirInsn *store;
    int result_arguments[3];
    int output_arguments[3];
    int value_arguments[2];
    int output_offset;

    memset(plan, 0, sizeof(*plan));
    if (value_call >= result_check ||
        result_check >= output_check ||
        !mir_match_math_pointer_function(
            value_call, TYPE_FLOAT, &plan->value_function) ||
        !mir_machine_two_call_arguments(
            value_call, value_arguments) ||
        !mir_match_math_float_constant(
            value_arguments[0], &plan->input_bits) ||
        (address = mir_definition(value_arguments[1])) == NULL ||
        !mir_match_math_output_address(
            address, TYPE_FLOAT, &output_offset) ||
        output_offset >= 0 ||
        !mir_match_math_check_function(
            result_check, check_function) ||
        !mir_match_math_check_function(
            output_check, check_function) ||
        !mir_machine_three_call_arguments(
            result_check, result_arguments) ||
        result_arguments[1] != value_call->dst ||
        !mir_match_final_string_value(
            result_arguments[0], &plan->result_string_id) ||
        !mir_match_math_float_constant(
            result_arguments[2], &plan->result_bits) ||
        !mir_machine_three_call_arguments(
            output_check, output_arguments) ||
        !mir_match_final_string_value(
            output_arguments[0], &plan->output_string_id) ||
        !mir_match_math_float_constant(
            output_arguments[2], &plan->output_bits))
        return 0;
    plan->check_function = *check_function;
    store = mir_match_math_result_store(
        value_call, result_check);
    load = mir_definition(output_arguments[1]);
    if (store == NULL || load == NULL ||
        load->opcode != MIR_LOAD ||
        load <= result_check || load >= output_check ||
        !mir_match_math_float_type(load->type) ||
        !mir_match_math_same_location(address, load) ||
        load->memory_flags != 0 ||
        (*output_location != NULL &&
         !mir_match_math_same_location(
             *output_location, address)) ||
        (*result_location != NULL &&
         !mir_match_math_same_location(
             *result_location, store)) ||
        !strcmp(address->name, store->name))
        return 0;
    *output_location = address;
    *result_location = store;
    return 1;
}

static int mir_match_math_print_function(
    const struct MirInsn *call, struct Sym **expected)
{
    struct Sym *function = find_global(call->name);

    if (function == NULL || function->is_defined ||
        function->is_funcptr || function->is_noreturn ||
        !function->has_proto || function->proto_nargs != 1 ||
        !function->proto_variadic ||
        type_ptr_depth(function->proto_types[0]) != 1 ||
        type_size(function->proto_types[0]) != 2 ||
        (call->type & 15) != TYPE_INT ||
        call->memory_flags != MIR_CALL_FLAG_VARIADIC ||
        !mir_match_math_symbol_target(call, function) ||
        (*expected != NULL && *expected != function))
        return 0;
    *expected = function;
    return 1;
}

static int mir_match_math_global_word(
    const struct MirInsn *load, struct Sym **root_out,
    int *offset_out)
{
    int memory_offset;
    int memory_storage;
    int memory_type;
    struct Sym *root;

    if (load == NULL || load->opcode != MIR_LOAD ||
        !mir_machine_named_nonvolatile(load) ||
        !mir_scalar_memory_location(
            load, &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        type_ptr_depth(memory_type) != 0 ||
        (memory_type & 15) != TYPE_INT ||
        type_size(memory_type) != 2 ||
        (root = find_global(load->name)) == NULL)
        return 0;
    *root_out = root;
    *offset_out = memory_offset;
    return 1;
}

static int mir_match_ctype_word_type(int type)
{
    return type_ptr_depth(type) == 0 &&
        (type & 15) == TYPE_INT &&
        (type & TYPE_UNSIGNED) == 0 &&
        type_size(type) == 2;
}

static int mir_match_ctype_pointer_type(int type)
{
    return type_ptr_depth(type) == 1 &&
        type_size(type) == 2;
}

static int mir_match_ctype_call_target(
    const struct MirInsn *call, struct Sym *function)
{
    return function != NULL && !function->is_funcptr &&
        !function->is_noreturn && function->has_proto &&
        (call->base_name[0] == 0 ||
         !strcmp(call->base_name,
                 asm_name_for(sym_asm_name(function))));
}

static int mir_match_ctype_string(int value, int *string_id)
{
    const struct MirInsn *definition = mir_definition(value);

    if (definition == NULL ||
        definition->opcode != MIR_STRING_ADDRESS ||
        !mir_match_ctype_pointer_type(definition->type))
        return 0;
    *string_id = (int)definition->immediate;
    return 1;
}

static int mir_match_ctype_constant(
    int value, unsigned long *constant)
{
    long evaluated;

    if (!mir_machine_evaluate_constant(value, &evaluated, 0) ||
        evaluated < -32768 || evaluated > 65535)
        return 0;
    *constant = (unsigned long)evaluated & 0xffffUL;
    return 1;
}

static int mir_match_ctype_value_call(
    const struct MirInsn *call, struct Sym **function_out)
{
    struct Sym *function = find_global(call->name);

    if (!mir_match_ctype_call_target(call, function) ||
        function->proto_nargs != 1 ||
        function->proto_variadic ||
        !mir_match_ctype_word_type(function->proto_types[0]) ||
        !mir_match_ctype_word_type(call->type) ||
        call->memory_flags != 0)
        return 0;
    *function_out = function;
    return 1;
}

static int mir_match_ctype_check_call(
    const struct MirInsn *call, struct Sym **function_out)
{
    struct Sym *function = find_global(call->name);

    if (!mir_match_ctype_call_target(call, function) ||
        function->proto_nargs != 2 ||
        function->proto_variadic ||
        !mir_match_ctype_word_type(function->proto_types[0]) ||
        !mir_match_ctype_pointer_type(function->proto_types[1]) ||
        (call->type & 15) != TYPE_VOID ||
        call->memory_flags != 0)
        return 0;
    if (*function_out != NULL && *function_out != function)
        return 0;
    *function_out = function;
    return 1;
}

static int mir_match_ctype_print_call(
    const struct MirInsn *call, struct Sym **function_out)
{
    struct Sym *function = find_global(call->name);

    if (!mir_match_ctype_call_target(call, function) ||
        function->proto_nargs != 1 ||
        !function->proto_variadic ||
        !mir_match_ctype_pointer_type(function->proto_types[0]) ||
        !mir_match_ctype_word_type(call->type))
        return 0;
    if (*function_out != NULL && *function_out != function)
        return 0;
    *function_out = function;
    return 1;
}

static int mir_match_ctype_pointer_call(
    const struct MirInsn *call, int arguments,
    int pointer_arguments, int returns_pointer,
    struct Sym **function_out)
{
    struct Sym *function = find_global(call->name);
    int argument;

    if (!mir_match_ctype_call_target(call, function) ||
        function->proto_nargs != arguments ||
        function->proto_variadic)
        return 0;
    for (argument = 0; argument < arguments; ++argument) {
        int should_be_pointer = argument < pointer_arguments;

        if (should_be_pointer !=
                mir_match_ctype_pointer_type(
                    function->proto_types[argument]) ||
            (!should_be_pointer &&
             (type_ptr_depth(function->proto_types[argument]) != 0 ||
              type_size(function->proto_types[argument]) != 2)))
            return 0;
    }
    if (returns_pointer !=
            mir_match_ctype_pointer_type(call->type))
        return 0;
    if (!returns_pointer && (call->type & 15) != TYPE_VOID)
        return 0;
    if (*function_out != NULL && *function_out != function)
        return 0;
    *function_out = function;
    return 1;
}

static int mir_match_ctype_compare_call(
    const struct MirInsn *call, struct Sym **function_out)
{
    struct Sym *function = find_global(call->name);

    if (!mir_match_ctype_call_target(call, function) ||
        function->proto_nargs != 2 ||
        function->proto_variadic ||
        !mir_match_ctype_pointer_type(function->proto_types[0]) ||
        !mir_match_ctype_pointer_type(function->proto_types[1]) ||
        !mir_match_ctype_word_type(call->type))
        return 0;
    *function_out = function;
    return 1;
}

static int mir_match_ctype_prefix_check(
    struct MirCtypeReallocSchedule *plan, int *instruction)
{
    struct MirCtypeCheck *check;
    const struct MirInsn *input;
    const struct MirInsn *value_arg;
    const struct MirInsn *value_call;
    const struct MirInsn *expected;
    const struct MirInsn *comparison;
    const struct MirInsn *check_arg;
    const struct MirInsn *string;
    const struct MirInsn *string_arg;
    const struct MirInsn *check_call;
    int cursor = *instruction;
    int value_argument;
    int check_arguments[2];

    if (plan->check_count >= MIR_MAX_CTYPE_CHECKS)
        return 0;
    if (cursor < mir.count &&
        mir.insns[cursor].opcode == MIR_NOP)
        ++cursor;
    if (cursor + 3 > mir.count)
        return 0;
    input = &mir.insns[cursor++];
    value_arg = &mir.insns[cursor++];
    value_call = &mir.insns[cursor++];
    if (cursor < mir.count &&
        mir.insns[cursor].opcode == MIR_NOP)
        ++cursor;
    if (cursor + 6 > mir.count)
        return 0;
    expected = &mir.insns[cursor++];
    comparison = &mir.insns[cursor++];
    check_arg = &mir.insns[cursor++];
    string = &mir.insns[cursor++];
    string_arg = &mir.insns[cursor++];
    check_call = &mir.insns[cursor++];
    if (input->opcode != MIR_CONST ||
        value_arg->opcode != MIR_ARG ||
        value_call->opcode != MIR_CALL ||
        expected->opcode != MIR_CONST ||
        comparison->opcode != MIR_BINARY ||
        (comparison->immediate != TOK_EQ &&
         comparison->immediate != TOK_NE) ||
        check_arg->opcode != MIR_ARG ||
        string->opcode != MIR_STRING_ADDRESS ||
        string_arg->opcode != MIR_ARG ||
        check_call->opcode != MIR_CALL ||
        !mir_machine_single_call_argument(
            value_call, &value_argument) ||
        value_argument != input->dst ||
        comparison->src1 != value_call->dst ||
        comparison->src2 != expected->dst ||
        check_arg->src1 != comparison->dst ||
        !mir_machine_two_call_arguments(
            check_call, check_arguments) ||
        check_arguments[0] != comparison->dst ||
        check_arguments[1] != string->dst)
        return 0;
    check = &plan->checks[plan->check_count];
    if (!mir_match_ctype_constant(
            input->dst, &check->input) ||
        !mir_match_ctype_constant(
            expected->dst, &check->expected) ||
        !mir_match_ctype_string(
            string->dst, &check->string_id) ||
        !mir_match_ctype_value_call(
            value_call, &check->value_function) ||
        !mir_match_ctype_check_call(
            check_call, &plan->check_function))
        return 0;
    check->comparison = (int)comparison->immediate;
    ++plan->check_count;
    *instruction = cursor;
    return 1;
}

static int mir_match_context_call_arguments(
    const struct MirInsn *call, int count,
    int arguments[MIR_CONTEXT_OP_MAX_ARGUMENTS])
{
    int found = 0;
    int instruction;
    int argument;

    if (count < 1 || count > MIR_CONTEXT_OP_MAX_ARGUMENTS)
        return 0;
    for (argument = 0; argument < MIR_CONTEXT_OP_MAX_ARGUMENTS;
         ++argument)
        arguments[argument] = -1;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *item = &mir.insns[instruction];
        int index;

        if (item->opcode != MIR_ARG ||
            item->secondary_offset != call->secondary_offset)
            continue;
        index = (int)item->immediate;
        if (index < 0 || index >= count ||
            arguments[index] >= 0)
            return 0;
        arguments[index] = item->src1;
        ++found;
    }
    return found == count;
}

static int mir_match_context_scalar_type(int type, int width)
{
    return type_ptr_depth(type) == 0 &&
        (width == 2 || width == 4) &&
        type_size(type) == width;
}

static int mir_match_context_constant(
    int value, int type, unsigned long *constant)
{
    long evaluated;

    if (!mir_match_context_scalar_type(type, type_size(type)))
        return 0;
    if (type_is_float(type))
        return mir_match_math_float_constant(value, constant);
    if (!mir_machine_evaluate_constant(value, &evaluated, 0))
        return 0;
    if (type_size(type) == 2)
        *constant = (unsigned long)evaluated & 0xffffUL;
    else
        *constant = (unsigned long)evaluated & 0xffffffffUL;
    return 1;
}

static int mir_match_context_value_call(
    struct MirContextOpCheck *check,
    const struct MirInsn *call)
{
    struct Sym *function = find_global(call->name);
    int arguments[MIR_CONTEXT_OP_MAX_ARGUMENTS];
    int argument;

    if (!mir_match_ctype_call_target(call, function) ||
        function->proto_nargs < 1 ||
        function->proto_nargs > MIR_CONTEXT_OP_MAX_ARGUMENTS ||
        function->proto_variadic || call->memory_flags != 0 ||
        !mir_match_context_scalar_type(
            call->type, type_size(call->type)))
        return 0;
    if (!mir_match_context_call_arguments(
            call, function->proto_nargs, arguments))
        return 0;
    memset(check, 0, sizeof(*check));
    check->value_function = function;
    check->argument_count = function->proto_nargs;
    check->result_width = type_size(call->type);
    check->result_unsigned =
        !type_is_float(call->type) &&
        (call->type & TYPE_UNSIGNED) != 0;
    for (argument = 0; argument < check->argument_count;
         ++argument) {
        int type = function->proto_types[argument];

        if (!mir_match_context_scalar_type(type, type_size(type)) ||
            type_is_float(type) ||
            !mir_match_context_constant(
                arguments[argument], type,
                &check->arguments[argument].value))
            return 0;
        check->arguments[argument].width = type_size(type);
    }
    return 1;
}

static int mir_match_context_check_call(
    struct MirContextOpCheck *check,
    const struct MirInsn *value_call,
    const struct MirInsn *call)
{
    struct Sym *function = find_global(call->name);
    const struct MirInsn *converted;
    int arguments[3];
    int check_type;

    if (!mir_match_ctype_call_target(call, function) ||
        function->proto_nargs != 3 ||
        function->proto_variadic || call->memory_flags != 0 ||
        (call->type & 15) != TYPE_VOID ||
        !mir_machine_three_call_arguments(call, arguments))
        return 0;
    check_type = function->proto_types[0];
    if (!mir_match_context_scalar_type(check_type, 4) ||
        function->proto_types[1] != check_type ||
        !mir_match_ctype_pointer_type(function->proto_types[2]) ||
        !mir_match_context_constant(
            arguments[1], check_type, &check->expected) ||
        !mir_match_ctype_string(
            arguments[2], &check->string_id))
        return 0;
    if (arguments[0] != value_call->dst) {
        converted = mir_definition(arguments[0]);
        if (converted == NULL ||
            converted->opcode != MIR_UNARY ||
            converted->immediate != 0 ||
            converted->src1 != value_call->dst ||
            converted->type != check_type)
            return 0;
    } else if (value_call->type != check_type) {
        return 0;
    }
    check->check_function = function;
    check->check_width = 4;
    check->check_unsigned =
        !type_is_float(check_type) &&
        (check_type & TYPE_UNSIGNED) != 0;
    check->check_float = type_is_float(check_type);
    if (check->result_width == 4 &&
        (check->check_float != type_is_float(value_call->type) ||
         (!check->check_float &&
          check->check_unsigned != check->result_unsigned)))
        return 0;
    return 1;
}

static int mir_match_context_prefix(
    struct MirContextOpSchedule *plan)
{
    struct Sym *root;
    long offset;

    if (mir.insns[0].opcode != MIR_LABEL ||
        mir.insns[1].opcode != MIR_NOP ||
        mir.insns[2].opcode != MIR_CONST ||
        !mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        mir.insns[3].opcode != MIR_STORE ||
        mir.insns[3].src1 != mir.insns[2].dst ||
        mir.insns[3].memory_size != 1 ||
        !mir_machine_named_nonvolatile(&mir.insns[3]) ||
        mir.insns[4].opcode != MIR_LABEL ||
        mir.insns[5].opcode != MIR_PHI ||
        mir.insns[5].src1 != mir.insns[2].dst ||
        mir.insns[5].src2 != mir.insns[22].dst ||
        mir.insns[5].phi_pred1 != mir.insns[0].label ||
        mir.insns[5].phi_pred2 != mir.insns[19].label ||
        mir.insns[6].opcode != MIR_NOP ||
        mir.insns[7].opcode != MIR_CONST ||
        !mir_machine_constant_equals(mir.insns[7].dst, 8) ||
        mir.insns[8].opcode != MIR_UNARY ||
        mir.insns[8].immediate != 0 ||
        mir.insns[8].src1 != mir.insns[5].dst ||
        mir.insns[9].opcode != MIR_BINARY ||
        mir.insns[9].immediate != '<' ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].src2 != mir.insns[7].dst ||
        mir.insns[10].opcode != MIR_BRANCH_FALSE ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[10].label != mir.insns[25].label)
        return 0;
    if (mir.insns[11].opcode != MIR_ADDRESS ||
        !mir_machine_global_address_offset(
            mir.insns[11].dst, &root, &offset, 0) ||
        root == NULL || offset < -32768 || offset > 32767 ||
        mir.insns[12].opcode != MIR_NOP ||
        mir.insns[13].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[13].src1 != mir.insns[11].dst ||
        mir.insns[13].src2 != mir.insns[5].dst ||
        mir.insns[13].immediate != 4 ||
        mir.insns[13].memory_size != 4 ||
        mir.insns[14].opcode != MIR_NOP ||
        mir.insns[15].opcode != MIR_UNARY ||
        mir.insns[15].immediate != 0 ||
        mir.insns[15].src1 != mir.insns[5].dst ||
        mir.insns[16].opcode != MIR_CONST ||
        !mir_machine_constant_equals(
            mir.insns[16].dst, 100000) ||
        mir.insns[17].opcode != MIR_BINARY ||
        mir.insns[17].immediate != '*' ||
        mir.insns[17].src1 != mir.insns[15].dst ||
        mir.insns[17].src2 != mir.insns[16].dst ||
        type_size(mir.insns[17].type) != 4 ||
        mir.insns[18].opcode != MIR_STORE_INDIRECT ||
        mir.insns[18].src1 != mir.insns[13].dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        mir.insns[18].memory_size != 4 ||
        (mir.insns[18].memory_flags & (1 | 8)) != 0)
        return 0;
    if (mir.insns[19].opcode != MIR_LABEL ||
        mir.insns[20].opcode != MIR_NOP ||
        mir.insns[21].opcode != MIR_CONST ||
        !mir_machine_constant_equals(mir.insns[21].dst, 1) ||
        mir.insns[22].opcode != MIR_BINARY ||
        mir.insns[22].immediate != '+' ||
        mir.insns[22].src1 != mir.insns[5].dst ||
        mir.insns[22].src2 != mir.insns[21].dst ||
        mir.insns[23].opcode != MIR_STORE ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        !mir_machine_same_location(
            &mir.insns[3], &mir.insns[23]) ||
        mir.insns[24].opcode != MIR_JUMP ||
        mir.insns[24].label != mir.insns[4].label ||
        mir.insns[25].opcode != MIR_LABEL)
        return 0;
    plan->array_root = root;
    plan->array_offset = (int)offset;
    return 1;
}

static int mir_match_context_float_stores(
    struct MirContextOpSchedule *plan)
{
    int store;

    for (store = 0; store < 2; ++store) {
        const struct MirInsn *value = &mir.insns[214 + store * 3];
        const struct MirInsn *name = value + 1;
        const struct MirInsn *write = value + 2;
        int memory_type;
        int memory_storage;
        int memory_offset;

        if (value->opcode != MIR_FLOAT_CONST ||
            !mir_match_math_float_type(value->type) ||
            name->opcode != MIR_NOP ||
            write->opcode != MIR_STORE ||
            write->src1 != value->dst ||
            !mir_machine_named_nonvolatile(write) ||
            !mir_scalar_memory_location(
                write, &memory_type, &memory_storage,
                &memory_offset) ||
            memory_storage != SC_GLOBAL ||
            !mir_match_math_float_type(memory_type) ||
            (plan->float_roots[store] =
                 find_global(write->name)) == NULL)
            return 0;
        plan->float_values[store] =
            (unsigned long)value->immediate & 0xffffffffUL;
        plan->float_offsets[store] = memory_offset;
    }
    return plan->float_roots[0] != plan->float_roots[1] ||
        plan->float_offsets[0] != plan->float_offsets[1];
}

static int mir_match_context_tail(
    struct MirContextOpSchedule *plan,
    const int call_indices[68])
{
    struct Sym *print_function = NULL;
    int arguments[2];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int argument;

    if (mir.insns[413].opcode != MIR_LOAD ||
        !mir_machine_named_nonvolatile(&mir.insns[413]) ||
        !mir_scalar_memory_location(
            &mir.insns[413], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        type_ptr_depth(memory_type) != 0 ||
        type_size(memory_type) != 2 ||
        (plan->failure_root =
             find_global(mir.insns[413].name)) == NULL ||
        mir.insns[414].opcode != MIR_CONST ||
        !mir_machine_constant_equals(mir.insns[414].dst, 0) ||
        mir.insns[415].opcode != MIR_BINARY ||
        mir.insns[415].immediate != TOK_EQ ||
        mir.insns[415].src1 != mir.insns[413].dst ||
        mir.insns[415].src2 != mir.insns[414].dst ||
        mir.insns[416].opcode != MIR_BRANCH_FALSE ||
        mir.insns[416].src1 != mir.insns[415].dst ||
        mir.insns[416].label != mir.insns[422].label ||
        mir.insns[417].opcode != MIR_LABEL ||
        mir.insns[418].opcode != MIR_STRING_ADDRESS ||
        !mir_match_ctype_string(
            mir.insns[418].dst, &plan->success_string_id) ||
        mir.insns[419].opcode != MIR_ARG ||
        mir.insns[419].src1 != mir.insns[418].dst ||
        call_indices[66] != 420 ||
        mir.insns[420].opcode != MIR_CALL ||
        !mir_machine_single_call_argument(
            &mir.insns[420], &argument) ||
        argument != mir.insns[418].dst ||
        !mir_match_ctype_print_call(
            &mir.insns[420], &print_function) ||
        mir.insns[421].opcode != MIR_JUMP ||
        mir.insns[421].label != mir.insns[428].label ||
        mir.insns[422].opcode != MIR_LABEL ||
        mir.insns[423].opcode != MIR_STRING_ADDRESS ||
        !mir_match_ctype_string(
            mir.insns[423].dst, &plan->failure_string_id) ||
        mir.insns[424].opcode != MIR_ARG ||
        mir.insns[424].src1 != mir.insns[423].dst ||
        mir.insns[425].opcode != MIR_LOAD ||
        !mir_machine_same_location(
            &mir.insns[413], &mir.insns[425]) ||
        mir.insns[426].opcode != MIR_ARG ||
        mir.insns[426].src1 != mir.insns[425].dst ||
        call_indices[67] != 427 ||
        mir.insns[427].opcode != MIR_CALL ||
        !mir_machine_two_call_arguments(
            &mir.insns[427], arguments) ||
        arguments[0] != mir.insns[423].dst ||
        arguments[1] != mir.insns[425].dst ||
        !mir_match_ctype_print_call(
            &mir.insns[427], &print_function) ||
        mir.insns[428].opcode != MIR_LABEL ||
        mir.insns[429].opcode != MIR_LOAD ||
        !mir_machine_same_location(
            &mir.insns[413], &mir.insns[429]) ||
        mir.insns[430].opcode != MIR_CONST ||
        !mir_machine_constant_equals(mir.insns[430].dst, 0) ||
        mir.insns[431].opcode != MIR_BINARY ||
        mir.insns[431].immediate != TOK_NE ||
        mir.insns[431].src1 != mir.insns[429].dst ||
        mir.insns[431].src2 != mir.insns[430].dst ||
        mir.insns[432].opcode != MIR_RETURN ||
        mir.insns[432].src1 != mir.insns[431].dst)
        return 0;
    plan->failure_offset = memory_offset;
    plan->print_function = print_function;
    return 1;
}

static int mir_match_format_buffer_pointer_string(
    const struct MirInsn *insn)
{
    return insn->opcode == MIR_STRING_ADDRESS &&
           insn->immediate >= 0 &&
           mir_match_action_decode_pointer_type(insn->type) &&
           (insn->type & 15) == TYPE_CHAR;
}

static int mir_match_format_buffer_function(
    const struct MirInsn *call, int fixed_arguments, int variadic,
    int defined, struct Sym **function_out)
{
    struct Sym *function;
    int argument;

    if (call->opcode != MIR_CALL ||
        type_ptr_depth(call->type) != 0 ||
        (defined ? (call->type & 15) != TYPE_VOID
                 : !mir_match_action_decode_word_type(call->type)))
        return 0;
    function = find_global(call->name);
    if (function == NULL || function->is_funcptr ||
        function->is_noreturn || function->is_defined != defined ||
        !function->has_proto ||
        function->proto_nargs != fixed_arguments ||
        function->proto_variadic != variadic)
        return 0;
    for (argument = 0; argument < fixed_arguments; ++argument)
        if (!mir_match_action_decode_pointer_type(
                function->proto_types[argument]))
            return 0;
    *function_out = function;
    return 1;
}

static int mir_match_format_buffer_declaration(
    const struct MirInsn *address)
{
    int declared;

    for (declared = 0; declared < mir.declared_count; ++declared)
        if (!strcmp(mir.declared_names[declared], address->name))
            return mir.declared_storage[declared] == SC_LOCAL &&
                   mir.declared_offsets[declared] == -64 &&
                   mir.declared_sizes[declared] == 64 &&
                   mir.declared_is_array[declared] &&
                   !mir.declared_is_vla[declared] &&
                   !mir.declared_is_volatile[declared] &&
                   mir.declared_dim_counts[declared] == 1 &&
                   mir.declared_dims[declared][0] == 64 &&
                   mir.declared_elem_sizes[declared] == 1 &&
                   (mir.declared_types[declared] & 15) == TYPE_CHAR;
    return 0;
}

static int mir_match_atof_schedule_string(
    int value, int *string_id)
{
    const struct MirInsn *definition = mir_definition(value);

    return definition != NULL &&
           definition->opcode == MIR_STRING_ADDRESS &&
           type_ptr_depth(definition->type) == 1 &&
           (definition->type & 15) == TYPE_CHAR &&
           type_size(definition->type) == 2 &&
           mir_match_final_string_value(value, string_id);
}

static int mir_match_atof_schedule_argument(
    const struct MirInsn *argument, const struct MirInsn *call,
    int value, int index)
{
    return argument->opcode == MIR_ARG &&
           argument->src1 == value &&
           argument->secondary_offset == call->secondary_offset &&
           argument->immediate == index;
}

static int mir_match_atof_schedule_value_function(
    const struct MirInsn *call, struct Sym **expected)
{
    struct Sym *function = find_global(call->name);

    if (function == NULL || function->is_defined ||
        function->is_funcptr || function->is_noreturn ||
        !function->has_proto || function->proto_nargs != 1 ||
        function->proto_variadic ||
        type_ptr_depth(function->proto_types[0]) != 1 ||
        (function->proto_types[0] & 15) != TYPE_CHAR ||
        type_size(function->proto_types[0]) != 2 ||
        !mir_match_math_float_type(call->type) ||
        call->memory_flags != 0 ||
        !mir_match_math_symbol_target(call, function) ||
        (*expected != NULL && *expected != function))
        return 0;
    *expected = function;
    return 1;
}

static int mir_match_atof_schedule_check_function(
    const struct MirInsn *call, int kind, struct Sym **expected)
{
    struct Sym *function = find_global(call->name);
    int argument_count;

    if (function == NULL || !function->is_defined ||
        function->is_funcptr || function->is_noreturn ||
        !function->has_proto || function->proto_variadic ||
        (call->type & 15) != TYPE_VOID ||
        call->memory_flags != 0 ||
        !mir_match_math_symbol_target(call, function) ||
        (*expected != NULL && *expected != function))
        return 0;
    argument_count =
        kind == MIR_ATOF_SCHEDULE_FLOAT ? 4 :
        kind == MIR_ATOF_SCHEDULE_NAN ? 2 : 3;
    if (function->proto_nargs != argument_count ||
        type_ptr_depth(function->proto_types[0]) != 1 ||
        (function->proto_types[0] & 15) != TYPE_CHAR ||
        type_size(function->proto_types[0]) != 2)
        return 0;
    if (kind == MIR_ATOF_SCHEDULE_FLOAT) {
        if (!mir_match_math_float_type(function->proto_types[1]) ||
            !mir_match_math_float_type(function->proto_types[2]) ||
            !mir_match_math_float_type(function->proto_types[3]))
            return 0;
    } else if (kind == MIR_ATOF_SCHEDULE_INT) {
        if (!mir_match_final_call_integer_type(
                function->proto_types[1], 2) ||
            !mir_match_final_call_integer_type(
                function->proto_types[2], 2))
            return 0;
    } else if (kind == MIR_ATOF_SCHEDULE_END) {
        if (type_ptr_depth(function->proto_types[1]) != 1 ||
            (function->proto_types[1] & 15) != TYPE_CHAR ||
            type_size(function->proto_types[1]) != 2 ||
            !mir_match_final_call_integer_type(
                function->proto_types[2], 2))
            return 0;
    } else if (kind == MIR_ATOF_SCHEDULE_INFINITY) {
        if (!mir_match_math_float_type(function->proto_types[1]) ||
            !mir_match_final_call_integer_type(
                function->proto_types[2], 2))
            return 0;
    } else if (kind == MIR_ATOF_SCHEDULE_NAN) {
        if (!mir_match_math_float_type(function->proto_types[1]))
            return 0;
    } else {
        return 0;
    }
    *expected = function;
    return 1;
}

static int mir_match_atof_schedule_print_function(
    const struct MirInsn *call, struct Sym **expected)
{
    struct Sym *function = find_global(call->name);

    if (function == NULL || function->is_defined ||
        function->is_funcptr || function->is_noreturn ||
        !function->has_proto || function->proto_nargs != 1 ||
        !function->proto_variadic ||
        type_ptr_depth(function->proto_types[0]) != 1 ||
        (function->proto_types[0] & 15) != TYPE_CHAR ||
        type_size(function->proto_types[0]) != 2 ||
        !mir_match_final_call_integer_type(call->type, 2) ||
        call->memory_flags != MIR_CALL_FLAG_VARIADIC ||
        !mir_match_math_symbol_target(call, function) ||
        (*expected != NULL && *expected != function))
        return 0;
    *expected = function;
    return 1;
}

static int mir_match_atof_schedule_float_definition(
    int *cursor, int limit, int *value, unsigned long *bits)
{
    const struct MirInsn *constant;
    const struct MirInsn *definition;

    if (*cursor >= limit ||
        mir.insns[*cursor].opcode != MIR_FLOAT_CONST)
        return 0;
    constant = &mir.insns[(*cursor)++];
    definition = constant;
    if (*cursor < limit &&
        mir.insns[*cursor].opcode == MIR_UNARY &&
        mir.insns[*cursor].immediate == '-' &&
        mir.insns[*cursor].src1 == constant->dst) {
        definition = &mir.insns[(*cursor)++];
    }
    if (!mir_match_math_float_constant(definition->dst, bits))
        return 0;
    *value = definition->dst;
    return 1;
}

static int mir_match_atof_schedule_optional_scale(
    int *cursor, int limit, int source_value,
    struct MirAtofScheduleCheck *item, int *result_value)
{
    const struct MirInsn *constant;
    const struct MirInsn *multiply;

    *result_value = source_value;
    if (*cursor + 1 >= limit ||
        mir.insns[*cursor].opcode != MIR_FLOAT_CONST ||
        mir.insns[*cursor + 1].opcode != MIR_BINARY)
        return 1;
    constant = &mir.insns[*cursor];
    multiply = &mir.insns[*cursor + 1];
    if (multiply->immediate != '*' ||
        multiply->src1 != source_value ||
        multiply->src2 != constant->dst ||
        !mir_match_math_float_type(multiply->type) ||
        !mir_match_math_float_type(multiply->secondary_offset) ||
        !mir_match_math_float_constant(
            constant->dst, &item->scale))
        return 0;
    item->has_scale = 1;
    *result_value = multiply->dst;
    *cursor += 2;
    return 1;
}

static int mir_match_atof_schedule_prefix(
    struct MirAtofSchedule *plan,
    struct MirAtofScheduleCheck *item,
    int *cursor, int limit,
    const struct MirInsn **name_out,
    const struct MirInsn **name_argument_out,
    const struct MirInsn **value_call_out,
    int *result_value)
{
    const struct MirInsn *name;
    const struct MirInsn *name_argument;
    const struct MirInsn *input;
    const struct MirInsn *input_argument;
    const struct MirInsn *value_call;
    int value_argument;

    if (*cursor + 4 >= limit)
        return 0;
    name = &mir.insns[(*cursor)++];
    name_argument = &mir.insns[(*cursor)++];
    input = &mir.insns[(*cursor)++];
    input_argument = &mir.insns[(*cursor)++];
    value_call = &mir.insns[(*cursor)++];
    if (!mir_match_atof_schedule_string(
            name->dst, &item->name_string_id) ||
        !mir_match_atof_schedule_string(
            input->dst, &item->input_string_id) ||
        !mir_match_atof_schedule_value_function(
            value_call, &plan->value_function) ||
        !mir_machine_single_call_argument(
            value_call, &value_argument) ||
        value_argument != input->dst ||
        !mir_match_atof_schedule_argument(
            input_argument, value_call, input->dst, 0))
        return 0;
    *name_out = name;
    *name_argument_out = name_argument;
    *value_call_out = value_call;
    return mir_match_atof_schedule_optional_scale(
        cursor, limit, value_call->dst, item, result_value);
}

static int mir_match_atof_schedule_float_check(
    struct MirAtofSchedule *plan,
    struct MirAtofScheduleCheck *item,
    int *cursor, int limit)
{
    const struct MirInsn *name;
    const struct MirInsn *name_argument;
    const struct MirInsn *value_call;
    const struct MirInsn *value_argument;
    const struct MirInsn *expected_argument;
    const struct MirInsn *tolerance_argument;
    const struct MirInsn *check_call;
    int arguments[4];
    int result_value;
    int expected_value;
    int tolerance_value;

    if (!mir_match_atof_schedule_prefix(
            plan, item, cursor, limit,
            &name, &name_argument, &value_call, &result_value) ||
        *cursor >= limit)
        return 0;
    value_argument = &mir.insns[(*cursor)++];
    if (!mir_match_atof_schedule_float_definition(
            cursor, limit, &expected_value, &item->expected) ||
        *cursor >= limit)
        return 0;
    expected_argument = &mir.insns[(*cursor)++];
    if (!mir_match_atof_schedule_float_definition(
            cursor, limit, &tolerance_value, &item->tolerance) ||
        *cursor + 1 >= limit)
        return 0;
    tolerance_argument = &mir.insns[(*cursor)++];
    check_call = &mir.insns[(*cursor)++];
    if (!mir_match_atof_schedule_check_function(
            check_call, item->kind,
            &plan->float_check_function) ||
        !mir_machine_four_call_arguments(check_call, arguments) ||
        arguments[0] != name->dst ||
        arguments[1] != result_value ||
        arguments[2] != expected_value ||
        arguments[3] != tolerance_value ||
        !mir_match_atof_schedule_argument(
            name_argument, check_call, name->dst, 0) ||
        !mir_match_atof_schedule_argument(
            value_argument, check_call, result_value, 1) ||
        !mir_match_atof_schedule_argument(
            expected_argument, check_call, expected_value, 2) ||
        !mir_match_atof_schedule_argument(
            tolerance_argument, check_call, tolerance_value, 3))
        return 0;
    return 1;
}

static int mir_match_atof_schedule_int_check(
    struct MirAtofSchedule *plan,
    struct MirAtofScheduleCheck *item,
    int *cursor, int limit, int *nop_count)
{
    const struct MirInsn *name;
    const struct MirInsn *name_argument;
    const struct MirInsn *value_call;
    const struct MirInsn *conversion;
    const struct MirInsn *value_argument;
    const struct MirInsn *expected_argument;
    const struct MirInsn *check_call;
    int arguments[3];
    int result_value;
    int expected_value;

    if (!mir_match_atof_schedule_prefix(
            plan, item, cursor, limit,
            &name, &name_argument, &value_call, &result_value) ||
        *cursor + 3 >= limit)
        return 0;
    conversion = &mir.insns[(*cursor)++];
    value_argument = &mir.insns[(*cursor)++];
    if (conversion->opcode != MIR_UNARY ||
        conversion->immediate != 0 ||
        conversion->src1 != result_value ||
        !mir_match_final_call_integer_type(conversion->type, 2))
        return 0;
    while (*cursor < limit &&
           mir.insns[*cursor].opcode == MIR_NOP) {
        ++*nop_count;
        ++*cursor;
    }
    if (*cursor + 2 >= limit)
        return 0;
    expected_value = mir.insns[*cursor].dst;
    if (mir.insns[(*cursor)++].opcode != MIR_CONST ||
        !mir_match_math_word_constant(
            expected_value, &item->expected))
        return 0;
    expected_argument = &mir.insns[(*cursor)++];
    check_call = &mir.insns[(*cursor)++];
    if (!mir_match_atof_schedule_check_function(
            check_call, item->kind,
            &plan->int_check_function) ||
        !mir_machine_three_call_arguments(check_call, arguments) ||
        arguments[0] != name->dst ||
        arguments[1] != conversion->dst ||
        arguments[2] != expected_value ||
        !mir_match_atof_schedule_argument(
            name_argument, check_call, name->dst, 0) ||
        !mir_match_atof_schedule_argument(
            value_argument, check_call, conversion->dst, 1) ||
        !mir_match_atof_schedule_argument(
            expected_argument, check_call, expected_value, 2))
        return 0;
    return 1;
}

static int mir_match_atof_schedule_end_check(
    struct MirAtofSchedule *plan,
    struct MirAtofScheduleCheck *item,
    int *cursor, int limit)
{
    const struct MirInsn *name;
    const struct MirInsn *name_argument;
    const struct MirInsn *input;
    const struct MirInsn *input_argument;
    const struct MirInsn *expected;
    const struct MirInsn *expected_argument;
    const struct MirInsn *check_call;
    int arguments[3];

    if (*cursor + 6 >= limit)
        return 0;
    name = &mir.insns[(*cursor)++];
    name_argument = &mir.insns[(*cursor)++];
    input = &mir.insns[(*cursor)++];
    input_argument = &mir.insns[(*cursor)++];
    expected = &mir.insns[(*cursor)++];
    expected_argument = &mir.insns[(*cursor)++];
    check_call = &mir.insns[(*cursor)++];
    if (!mir_match_atof_schedule_string(
            name->dst, &item->name_string_id) ||
        !mir_match_atof_schedule_string(
            input->dst, &item->input_string_id) ||
        expected->opcode != MIR_CONST ||
        !mir_match_math_word_constant(
            expected->dst, &item->expected) ||
        !mir_match_atof_schedule_check_function(
            check_call, item->kind,
            &plan->end_check_function) ||
        !mir_machine_three_call_arguments(check_call, arguments) ||
        arguments[0] != name->dst ||
        arguments[1] != input->dst ||
        arguments[2] != expected->dst ||
        !mir_match_atof_schedule_argument(
            name_argument, check_call, name->dst, 0) ||
        !mir_match_atof_schedule_argument(
            input_argument, check_call, input->dst, 1) ||
        !mir_match_atof_schedule_argument(
            expected_argument, check_call, expected->dst, 2))
        return 0;
    return 1;
}

static int mir_match_atof_schedule_infinity_check(
    struct MirAtofSchedule *plan,
    struct MirAtofScheduleCheck *item,
    int *cursor, int limit)
{
    const struct MirInsn *name;
    const struct MirInsn *name_argument;
    const struct MirInsn *value_call;
    const struct MirInsn *value_argument;
    const struct MirInsn *expected;
    const struct MirInsn *expected_argument;
    const struct MirInsn *check_call;
    int arguments[3];
    int result_value;

    if (!mir_match_atof_schedule_prefix(
            plan, item, cursor, limit,
            &name, &name_argument, &value_call, &result_value) ||
        item->has_scale || *cursor + 3 >= limit)
        return 0;
    value_argument = &mir.insns[(*cursor)++];
    expected = &mir.insns[(*cursor)++];
    expected_argument = &mir.insns[(*cursor)++];
    check_call = &mir.insns[(*cursor)++];
    if (expected->opcode != MIR_CONST ||
        !mir_match_math_word_constant(
            expected->dst, &item->expected) ||
        !mir_match_atof_schedule_check_function(
            check_call, item->kind,
            &plan->infinity_check_function) ||
        !mir_machine_three_call_arguments(check_call, arguments) ||
        arguments[0] != name->dst ||
        arguments[1] != result_value ||
        arguments[2] != expected->dst ||
        !mir_match_atof_schedule_argument(
            name_argument, check_call, name->dst, 0) ||
        !mir_match_atof_schedule_argument(
            value_argument, check_call, result_value, 1) ||
        !mir_match_atof_schedule_argument(
            expected_argument, check_call, expected->dst, 2))
        return 0;
    return 1;
}

static int mir_match_atof_schedule_nan_check(
    struct MirAtofSchedule *plan,
    struct MirAtofScheduleCheck *item,
    int *cursor, int limit)
{
    const struct MirInsn *name;
    const struct MirInsn *name_argument;
    const struct MirInsn *value_call;
    const struct MirInsn *value_argument;
    const struct MirInsn *check_call;
    int arguments[2];
    int result_value;

    if (!mir_match_atof_schedule_prefix(
            plan, item, cursor, limit,
            &name, &name_argument, &value_call, &result_value) ||
        item->has_scale || *cursor + 1 >= limit)
        return 0;
    value_argument = &mir.insns[(*cursor)++];
    check_call = &mir.insns[(*cursor)++];
    if (!mir_match_atof_schedule_check_function(
            check_call, item->kind,
            &plan->nan_check_function) ||
        !mir_machine_two_call_arguments(check_call, arguments) ||
        arguments[0] != name->dst ||
        arguments[1] != result_value ||
        !mir_match_atof_schedule_argument(
            name_argument, check_call, name->dst, 0) ||
        !mir_match_atof_schedule_argument(
            value_argument, check_call, result_value, 1))
        return 0;
    return 1;
}

static int mir_match_atof_schedule_final(
    struct MirAtofSchedule *plan, int cursor)
{
    const struct MirInsn *failure_load = &mir.insns[cursor];
    const struct MirInsn *failure_string = &mir.insns[cursor + 3];
    const struct MirInsn *failure_print_load = &mir.insns[cursor + 5];
    const struct MirInsn *failure_print = &mir.insns[cursor + 7];
    const struct MirInsn *success_string = &mir.insns[cursor + 10];
    const struct MirInsn *checks_load = &mir.insns[cursor + 12];
    const struct MirInsn *success_print = &mir.insns[cursor + 14];
    const struct MirInsn *return_load = &mir.insns[cursor + 16];
    const struct MirInsn *phi = &mir.insns[cursor + 25];
    int failure_arguments[2];
    int success_arguments[2];
    int memory_type;
    int memory_storage;
    int memory_offset;

    if (cursor != 455 ||
        failure_load->opcode != MIR_LOAD ||
        !mir_machine_named_nonvolatile(failure_load) ||
        !mir_scalar_memory_location(
            failure_load, &memory_type,
            &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        !mir_match_final_call_integer_type(memory_type, 2) ||
        (plan->failure_root =
             find_global(failure_load->name)) == NULL ||
        plan->failure_root->is_volatile ||
        mir.insns[cursor + 1].opcode != MIR_BRANCH_FALSE ||
        mir.insns[cursor + 1].src1 != failure_load->dst ||
        mir.insns[cursor + 1].label !=
            mir.insns[cursor + 9].label ||
        mir.insns[cursor + 2].opcode != MIR_LABEL ||
        !mir_match_atof_schedule_string(
            failure_string->dst, &plan->failure_string_id) ||
        mir.insns[cursor + 4].opcode != MIR_ARG ||
        failure_print_load->opcode != MIR_LOAD ||
        !mir_machine_same_location(
            failure_load, failure_print_load) ||
        mir.insns[cursor + 6].opcode != MIR_ARG ||
        failure_print->opcode != MIR_CALL ||
        mir.insns[cursor + 8].opcode != MIR_JUMP ||
        mir.insns[cursor + 8].label !=
            mir.insns[cursor + 15].label ||
        mir.insns[cursor + 9].opcode != MIR_LABEL ||
        !mir_match_atof_schedule_string(
            success_string->dst, &plan->success_string_id) ||
        mir.insns[cursor + 11].opcode != MIR_ARG ||
        checks_load->opcode != MIR_LOAD ||
        !mir_machine_named_nonvolatile(checks_load) ||
        !mir_scalar_memory_location(
            checks_load, &memory_type,
            &memory_storage, &plan->checks_offset) ||
        memory_storage != SC_GLOBAL ||
        !mir_match_final_call_integer_type(memory_type, 2) ||
        (plan->checks_root =
             find_global(checks_load->name)) == NULL ||
        plan->checks_root->is_volatile ||
        mir_machine_same_location(failure_load, checks_load) ||
        mir.insns[cursor + 13].opcode != MIR_ARG ||
        success_print->opcode != MIR_CALL ||
        mir.insns[cursor + 15].opcode != MIR_LABEL ||
        return_load->opcode != MIR_LOAD ||
        !mir_machine_same_location(failure_load, return_load) ||
        mir.insns[cursor + 17].opcode != MIR_BRANCH_FALSE ||
        mir.insns[cursor + 17].src1 != return_load->dst ||
        mir.insns[cursor + 17].label !=
            mir.insns[cursor + 21].label ||
        mir.insns[cursor + 18].opcode != MIR_CONST ||
        !mir_machine_constant_equals(
            mir.insns[cursor + 18].dst, 1) ||
        mir.insns[cursor + 19].opcode != MIR_LABEL ||
        mir.insns[cursor + 20].opcode != MIR_JUMP ||
        mir.insns[cursor + 20].label !=
            mir.insns[cursor + 24].label ||
        mir.insns[cursor + 21].opcode != MIR_LABEL ||
        mir.insns[cursor + 22].opcode != MIR_CONST ||
        !mir_machine_constant_equals(
            mir.insns[cursor + 22].dst, 0) ||
        mir.insns[cursor + 23].opcode != MIR_LABEL ||
        mir.insns[cursor + 24].opcode != MIR_LABEL ||
        phi->opcode != MIR_PHI ||
        phi->src1 != mir.insns[cursor + 18].dst ||
        phi->src2 != mir.insns[cursor + 22].dst ||
        phi->phi_pred1 != mir.insns[cursor + 19].label ||
        phi->phi_pred2 != mir.insns[cursor + 23].label ||
        mir.insns[cursor + 26].opcode != MIR_RETURN ||
        mir.insns[cursor + 26].src1 != phi->dst)
        return 0;
    plan->failure_offset = memory_offset;
    if (!mir_machine_two_call_arguments(
            failure_print, failure_arguments) ||
        failure_arguments[0] != failure_string->dst ||
        failure_arguments[1] != failure_print_load->dst ||
        !mir_match_atof_schedule_argument(
            &mir.insns[cursor + 4], failure_print,
            failure_string->dst, 0) ||
        !mir_match_atof_schedule_argument(
            &mir.insns[cursor + 6], failure_print,
            failure_print_load->dst, 1) ||
        !mir_match_atof_schedule_print_function(
            failure_print, &plan->print_function) ||
        !mir_machine_two_call_arguments(
            success_print, success_arguments) ||
        success_arguments[0] != success_string->dst ||
        success_arguments[1] != checks_load->dst ||
        !mir_match_atof_schedule_argument(
            &mir.insns[cursor + 11], success_print,
            success_string->dst, 0) ||
        !mir_match_atof_schedule_argument(
            &mir.insns[cursor + 13], success_print,
            checks_load->dst, 1) ||
        !mir_match_atof_schedule_print_function(
            success_print, &plan->print_function))
        return 0;
    return 1;
}

static int mir_machine_flat_load(
    int value, int *stack_offset, long *offset,
    int *width, int *is_unsigned)
{
    const struct MirInsn *load = mir_definition(value);

    if (load != NULL && load->opcode == MIR_UNARY &&
        load->immediate == 0)
        load = mir_definition(load->src1);
    if (load == NULL || load->opcode != MIR_LOAD_INDIRECT ||
        (load->memory_size != 1 && load->memory_size != 2 &&
         load->memory_size != 4) ||
        (load->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_parameter_address(
            load->src1, stack_offset, offset, 0))
        return 0;
    *width = load->memory_size;
    *is_unsigned = (load->type & TYPE_UNSIGNED) != 0;
    return 1;
}

static int mir_machine_global_byte_value(
    int value, struct Sym **symbol_out, int *offset_out,
    int *is_unsigned_out)
{
    const struct MirInsn *definition = mir_definition(value);
    const struct MirInsn *address;
    const struct MirInsn *root;
    struct Sym *symbol;
    int memory_type;
    int memory_storage;
    int memory_offset;
    long index;

    if (definition != NULL && definition->opcode == MIR_UNARY &&
        definition->immediate == 0)
        definition = mir_definition(definition->src1);
    if (definition == NULL)
        return 0;
    if (definition->opcode == MIR_LOAD) {
        if (!mir_machine_named_nonvolatile(definition) ||
            !mir_scalar_memory_location(
                definition, &memory_type, &memory_storage,
                &memory_offset) ||
            memory_storage != SC_GLOBAL ||
            type_size(memory_type) != 1)
            return 0;
        symbol = find_global(definition->name);
        if (symbol == NULL || symbol->is_volatile)
            return 0;
        *symbol_out = symbol;
        *offset_out = memory_offset;
        *is_unsigned_out =
            type_is_bool(definition->type) ||
            (definition->type & TYPE_UNSIGNED) != 0;
        return 1;
    }
    if (definition->opcode != MIR_LOAD_INDIRECT ||
        definition->memory_size != 1 ||
        definition->bit_width != 0 ||
        (definition->memory_flags & (1 | 8)) != 0)
        return 0;
    address = mir_definition(definition->src1);
    if (address == NULL ||
        (address->memory_flags & (1 | 8)) != 0)
        return 0;
    if (address->opcode == MIR_MEMBER_ADDRESS) {
        root = mir_definition(address->src1);
        index = address->immediate;
    } else if (address->opcode == MIR_INDEX_ADDRESS &&
               address->immediate > 0 &&
               mir_machine_constant_value(address->src2, &index, 0)) {
        root = mir_definition(address->src1);
        index *= address->immediate;
    } else {
        return 0;
    }
    if (root == NULL || root->opcode != MIR_ADDRESS ||
        !mir_scalar_memory_location(
            root, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        index < -32768 || index > 32767)
        return 0;
    symbol = find_global(root->name);
    if (symbol == NULL || symbol->is_volatile)
        return 0;
    *symbol_out = symbol;
    *offset_out = memory_offset + (int)index;
    *is_unsigned_out =
        type_is_bool(definition->type) ||
        (definition->type & TYPE_UNSIGNED) != 0;
    return 1;
}

static int mir_machine_constant_bits(
    int value, int width, unsigned long *bits)
{
    const struct MirInsn *constant = mir_definition(value);

    if (constant == NULL ||
        (constant->opcode != MIR_CONST &&
         constant->opcode != MIR_FLOAT_CONST) ||
        type_size(constant->type) != width)
        return 0;
    *bits = (unsigned long)constant->immediate;
    if (width == 1)
        *bits &= 0xffUL;
    else if (width == 2)
        *bits &= 0xffffUL;
    return 1;
}

static int mir_machine_match_fixed_mutation(
    const struct MirInsn *store, struct MirFixedMutation *mutation,
    int *parameter_stack_offset)
{
    const struct MirInsn *value;
    int stack_offset;
    long offset;
    unsigned long bits;

    if (store == NULL || store->opcode != MIR_STORE_INDIRECT ||
        (store->memory_size != 1 && store->memory_size != 2 &&
         store->memory_size != 4) ||
        store->bit_width != 0 ||
        (store->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_parameter_address(
            store->src1, &stack_offset, &offset, 0) ||
        offset < -32768 || offset > 32767)
        return 0;
    if (*parameter_stack_offset < 0)
        *parameter_stack_offset = stack_offset;
    else if (*parameter_stack_offset != stack_offset)
        return 0;
    value = mir_definition(store->src2);
    if (mir_machine_constant_bits(
            store->src2, store->memory_size, &bits)) {
        mutation->kind = MIR_FIXED_MUTATION_SET;
    } else if (value != NULL && value->opcode == MIR_BINARY &&
               value->immediate == '+') {
        const struct MirInsn *load = mir_definition(value->src1);
        int load_stack;
        long load_offset;

        if (load == NULL || load->opcode != MIR_LOAD_INDIRECT ||
            load->memory_size != store->memory_size ||
            load->bit_width != 0 ||
            (load->memory_flags & (1 | 8)) != 0 ||
            !mir_machine_parameter_address(
                load->src1, &load_stack, &load_offset, 0) ||
            load_stack != stack_offset || load_offset != offset ||
            !mir_machine_constant_bits(
                value->src2, store->memory_size, &bits))
            return 0;
        mutation->kind = MIR_FIXED_MUTATION_ADD;
    } else {
        return 0;
    }
    mutation->offset = (int)offset;
    mutation->width = store->memory_size;
    mutation->value = bits;
    return 1;
}

static void mir_machine_emit_parameter_address(
    MirStream *out, int stack_offset, int offset)
{
    mir_stream_printf(out, "\tld hl,%d\n\tadd hl,sp\n", stack_offset);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n", out);
    mir_machine_emit_hl_offset(out, offset, 0);
}

static void mir_machine_emit_store_hl_to_bc(MirStream *out)
{
    mir_stream_puts("\tld a,l\n\tld (bc),a\n\tinc bc\n"
          "\tld a,h\n\tld (bc),a\n", out);
}

static void mir_machine_emit_fixed_mutation(
    MirStream *out, const struct MirFixedParamMutations *plan,
    const struct MirFixedMutation *mutation)
{
    mir_machine_emit_parameter_address(
        out, plan->parameter_stack_offset, mutation->offset);
    if (mutation->kind == MIR_FIXED_MUTATION_SET) {
        if (mutation->width == 1) {
            mir_stream_printf(out, "\tld (hl),%lu\n", mutation->value & 0xffUL);
        } else if (mutation->width == 2) {
            mir_stream_printf(out,
                    "\tld (hl),%lu\n\tinc hl\n\tld (hl),%lu\n",
                    mutation->value & 0xffUL,
                    (mutation->value >> 8) & 0xffUL);
        } else {
            int byte;
            for (byte = 0; byte < 4; ++byte) {
                mir_stream_printf(out, "\tld (hl),%lu\n",
                        (mutation->value >> (byte * 8)) & 0xffUL);
                if (byte != 3)
                    mir_stream_puts("\tinc hl\n", out);
            }
        }
        return;
    }
    mir_stream_puts("\tpush hl\n", out);
    if (mutation->width == 1) {
        mir_stream_puts("\tpop hl\n\tld a,(hl)\n", out);
        mir_stream_printf(out, "\tadd a,%lu\n\tld (hl),a\n",
                mutation->value & 0xffUL);
    } else if (mutation->width == 2) {
        mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n", out);
        mir_stream_printf(out, "\tld hl,%lu\n\tadd hl,de\n",
                mutation->value & 0xffffUL);
        mir_stream_puts("\tpop bc\n", out);
        mir_machine_emit_store_hl_to_bc(out);
    } else if (mutation->width == 4) {
        mir_stream_puts("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
              "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
              "\tld l,c\n\tld h,b\n", out);
        mir_stream_printf(out,
                "\tld bc,%lu\n\tadd hl,bc\n\tex de,hl\n"
                "\tld bc,%lu\n\tadc hl,bc\n\tex de,hl\n"
                "\tpop bc\n",
                mutation->value & 0xffffUL,
                (mutation->value >> 16) & 0xffffUL);
        mir_stream_puts("\tld a,l\n\tld (bc),a\n\tinc bc\n"
              "\tld a,h\n\tld (bc),a\n\tinc bc\n"
              "\tld a,e\n\tld (bc),a\n\tinc bc\n"
              "\tld a,d\n\tld (bc),a\n", out);
    }
}

static int mir_machine_global_member(
    int value, struct Sym **root_out, int *offset_out)
{
    const struct MirInsn *member = mir_definition(value);
    const struct MirInsn *root;
    struct Sym *symbol;

    if (member == NULL || member->opcode != MIR_MEMBER_ADDRESS ||
        (member->memory_flags & (1 | 8)) != 0)
        return 0;
    root = mir_machine_resolve_local_alias(member->src1);
    if (root == NULL || root->opcode != MIR_ADDRESS)
        return 0;
    symbol = find_global(root->name);
    if (symbol == NULL || !symbol->is_defined || symbol->is_volatile)
        return 0;
    *root_out = symbol;
    *offset_out = (int)member->immediate;
    return 1;
}

static int mir_machine_match_global_append_store(
    const struct MirInsn *store, struct MirGlobalAppend *plan,
    struct MirGlobalAppendStore *append_store)
{
    const struct MirInsn *destination;
    const struct MirInsn *index;
    const struct MirInsn *array_member;
    const struct MirInsn *count_load;
    struct Sym *array_root;
    struct Sym *count_root;
    int array_offset;
    int count_offset;
    int field_offset = 0;

    if (store == NULL || store->opcode != MIR_STORE_INDIRECT ||
        (store->memory_size != 1 && store->memory_size != 2) ||
        store->bit_width != 0 ||
        (store->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_parameter_value_offset(
            store->src2, &append_store->parameter_stack_offset))
        return 0;
    destination = mir_definition(store->src1);
    if (destination != NULL &&
        destination->opcode == MIR_MEMBER_ADDRESS) {
        if ((destination->memory_flags & (1 | 8)) != 0)
            return 0;
        field_offset = (int)destination->immediate;
        destination = mir_definition(destination->src1);
    }
    if (destination == NULL ||
        destination->opcode != MIR_INDEX_ADDRESS ||
        (destination->memory_flags & 1) != 0 ||
        (destination->immediate != 1 &&
         destination->immediate != 2 &&
         destination->immediate != 4 &&
         destination->immediate != 8))
        return 0;
    index = destination;
    array_member = mir_definition(index->src1);
    count_load = mir_definition(index->src2);
    if (array_member == NULL ||
        array_member->opcode != MIR_MEMBER_ADDRESS ||
        count_load == NULL ||
        count_load->opcode != MIR_LOAD_INDIRECT ||
        count_load->memory_size != 2 ||
        count_load->bit_width != 0 ||
        (count_load->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_global_member(
            array_member->dst, &array_root, &array_offset) ||
        !mir_machine_global_member(
            count_load->src1, &count_root, &count_offset) ||
        array_root != count_root)
        return 0;
    if (plan->root == NULL) {
        plan->root = array_root;
        plan->array_offset = array_offset;
        plan->count_offset = count_offset;
        plan->element_stride = (int)index->immediate;
    } else if (plan->root != array_root ||
               plan->array_offset != array_offset ||
               plan->count_offset != count_offset ||
               plan->element_stride != (int)index->immediate) {
        return 0;
    }
    append_store->field_offset = field_offset;
    append_store->width = store->memory_size;
    return 1;
}

static int mir_machine_match_global_increment(
    const struct MirInsn *store, struct Sym **root_out, int *offset_out)
{
    const struct MirInsn *add;
    const struct MirInsn *load;
    const struct MirInsn *one;
    struct Sym *store_root;
    struct Sym *load_root;
    int store_offset;
    int load_offset;

    if (store == NULL || store->opcode != MIR_STORE_INDIRECT ||
        store->memory_size != 2 ||
        store->bit_width != 0 ||
        (store->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_global_member(
            store->src1, &store_root, &store_offset))
        return 0;
    add = mir_definition(store->src2);
    if (add == NULL || add->opcode != MIR_BINARY ||
        add->immediate != '+')
        return 0;
    load = mir_definition(add->src1);
    one = mir_definition(add->src2);
    if (load == NULL || load->opcode != MIR_LOAD_INDIRECT ||
        load->memory_size != 2 ||
        load->bit_width != 0 ||
        (load->memory_flags & (1 | 8)) != 0 ||
        one == NULL || one->opcode != MIR_CONST ||
        one->immediate != 1 ||
        !mir_machine_global_member(
            load->src1, &load_root, &load_offset) ||
        load_root != store_root || load_offset != store_offset)
        return 0;
    *root_out = store_root;
    *offset_out = store_offset;
    return 1;
}

static int mir_machine_match_nested_append_store(
    const struct MirInsn *store, struct MirNestedAppend *plan,
    struct MirNestedAppendStore *append_store)
{
    const struct MirInsn *destination;
    const struct MirInsn *array_member;
    const struct MirInsn *count_load;
    struct MirRowMemberAddress array_address;
    struct MirRowMemberAddress count_address;

    if (store == NULL || store->opcode != MIR_STORE_INDIRECT ||
        (store->memory_size != 1 && store->memory_size != 2) ||
        store->bit_width != 0 ||
        (store->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_parameter_value_offset(
            store->src2, &append_store->parameter_stack_offset))
        return 0;
    destination = mir_definition(store->src1);
    if (destination == NULL ||
        destination->opcode != MIR_INDEX_ADDRESS ||
        (destination->memory_flags & 1) != 0 ||
        (destination->immediate != 1 &&
         destination->immediate != 2 &&
         destination->immediate != 4 &&
         destination->immediate != 8))
        return 0;
    array_member = mir_definition(destination->src1);
    count_load = mir_definition(destination->src2);
    if (array_member == NULL ||
        array_member->opcode != MIR_MEMBER_ADDRESS ||
        count_load == NULL ||
        count_load->opcode != MIR_LOAD_INDIRECT ||
        count_load->memory_size != 2 ||
        count_load->bit_width != 0 ||
        (count_load->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_row_member_address(
            array_member->dst, &array_address) ||
        !mir_machine_row_member_address(
            count_load->src1, &count_address) ||
        !mir_machine_same_row(&array_address, &count_address))
        return 0;
    if (plan->root == NULL) {
        plan->root = array_address.root;
        plan->root_pointer_offset =
            array_address.root_pointer_offset;
        plan->row_stride = array_address.row_stride;
        plan->row_index_value = array_address.index_value;
        plan->count_member_offset = count_address.member_offset;
    } else if (plan->root != array_address.root ||
               plan->root_pointer_offset !=
                   array_address.root_pointer_offset ||
               plan->row_stride != array_address.row_stride ||
               plan->row_index_value !=
                   array_address.index_value ||
               plan->count_member_offset !=
                   count_address.member_offset) {
        return 0;
    }
    append_store->array_member_offset =
        array_address.member_offset;
    append_store->element_stride = (int)destination->immediate;
    append_store->width = store->memory_size;
    return 1;
}

static int mir_machine_match_nested_increment(
    const struct MirInsn *store, struct MirRowMemberAddress *address)
{
    const struct MirInsn *add;
    const struct MirInsn *load;
    const struct MirInsn *one;

    if (store == NULL || store->opcode != MIR_STORE_INDIRECT ||
        store->memory_size != 2 ||
        store->bit_width != 0 ||
        (store->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_row_member_address(store->src1, address))
        return 0;
    add = mir_definition(store->src2);
    if (add == NULL || add->opcode != MIR_BINARY ||
        add->immediate != '+')
        return 0;
    load = mir_definition(add->src1);
    one = mir_definition(add->src2);
    return load != NULL && load->opcode == MIR_LOAD_INDIRECT &&
           load->src1 == store->src1 &&
           load->memory_size == 2 &&
           load->bit_width == 0 &&
           (load->memory_flags & (1 | 8)) == 0 &&
           one != NULL && one->opcode == MIR_CONST &&
           one->immediate == 1;
}

static int mir_machine_local_pointer_alias(
    const struct MirInsn *insn)
{
    int memory_type;
    int memory_storage;
    int memory_offset;
    int declared;

    if (!mir_scalar_memory_location(
            insn, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_LOCAL ||
        type_ptr_depth(memory_type) == 0 ||
        type_size(memory_type) != 2 ||
        (insn->memory_flags & (1 | 8)) != 0)
        return 0;
    for (declared = 0; declared < mir.declared_count; ++declared)
        if (!strcmp(mir.declared_names[declared], insn->name))
            break;
    if (declared == mir.declared_count ||
        mir.declared_is_volatile[declared])
        return 0;
    if (insn->opcode == MIR_LOAD)
        return 1;
    if (insn->opcode == MIR_STORE) {
        const struct MirInsn *source = mir_definition(insn->src1);
        return source != NULL && source->opcode == MIR_ADDRESS;
    }
    return 0;
}

static int mir_machine_same_state_member(
    const struct MirStateMember *left,
    const struct MirStateMember *right)
{
    return left->root == right->root &&
           left->root_offset == right->root_offset &&
           left->member_offset == right->member_offset;
}

static int mir_machine_index_update(
    const struct MirInsn *store, int operation,
    struct MirStateMember *member_out, int *old_value,
    int *new_value)
{
    const struct MirInsn *binary;
    const struct MirInsn *load;
    const struct MirInsn *one;
    struct MirStateMember loaded_member;

    if (store == NULL || store->opcode != MIR_STORE_INDIRECT ||
        store->memory_size != 2 || store->bit_width != 0 ||
        (store->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_state_member_address(
            store->src1, member_out))
        return 0;
    binary = mir_definition(store->src2);
    if (binary == NULL || binary->opcode != MIR_BINARY ||
        binary->immediate != operation)
        return 0;
    load = mir_definition(binary->src1);
    one = mir_definition(binary->src2);
    if (load == NULL || load->opcode != MIR_LOAD_INDIRECT ||
        load->memory_size != 2 || load->bit_width != 0 ||
        (load->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_state_member_address(
            load->src1, &loaded_member) ||
        !mir_machine_same_state_member(
            member_out, &loaded_member) ||
        one == NULL || one->opcode != MIR_CONST ||
        one->immediate != 1)
        return 0;
    *old_value = load->dst;
    *new_value = binary->dst;
    return 1;
}

static int mir_machine_indexed_stack_address(
    int value, struct MirStateMember *base_member,
    int *index_value, int *element_width)
{
    const struct MirInsn *index = mir_definition(value);
    const struct MirInsn *base_load;

    if (index == NULL || index->opcode != MIR_INDEX_ADDRESS ||
        index->immediate != 2 ||
        (index->memory_flags & 1) != 0)
        return 0;
    base_load = mir_definition(index->src1);
    if (base_load == NULL ||
        base_load->opcode != MIR_LOAD_INDIRECT ||
        base_load->memory_size != 2 ||
        base_load->bit_width != 0 ||
        type_ptr_depth(base_load->type) == 0 ||
        (base_load->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_state_member_address(
            base_load->src1, base_member))
        return 0;
    *index_value = index->src2;
    *element_width = (int)index->immediate;
    return 1;
}

static int mir_machine_pointer_update(
    const struct MirInsn *store, int operation,
    struct MirStateMember *member_out,
    const struct MirInsn **load_out)
{
    const struct MirInsn *binary;
    const struct MirInsn *load;
    struct MirStateMember loaded_member;
    long amount;

    if (store == NULL || store->opcode != MIR_STORE_INDIRECT ||
        store->memory_size != 2 || store->bit_width != 0 ||
        (store->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_state_member_address(
            store->src1, member_out))
        return 0;
    binary = mir_definition(store->src2);
    if (binary == NULL || binary->opcode != MIR_BINARY ||
        binary->immediate != operation)
        return 0;
    load = mir_definition(binary->src1);
    if (load == NULL || load->opcode != MIR_LOAD_INDIRECT ||
        load->memory_size != 2 || load->bit_width != 0 ||
        type_ptr_depth(load->type) == 0 ||
        (load->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_state_member_address(
            load->src1, &loaded_member) ||
        !mir_machine_same_state_member(
            member_out, &loaded_member) ||
        !mir_machine_constant_value(
            binary->src2, &amount, 0) ||
        amount != 2)
        return 0;
    *load_out = load;
    return 1;
}

static int mir_machine_pointer_member_load(
    int value, struct MirStateMember *member_out)
{
    const struct MirInsn *load = mir_definition(value);

    return load != NULL && load->opcode == MIR_LOAD_INDIRECT &&
           load->memory_size == 2 && load->bit_width == 0 &&
           type_ptr_depth(load->type) > 0 &&
           (load->memory_flags & (1 | 8)) == 0 &&
           mir_machine_state_member_address(
               load->src1, member_out);
}

static int mir_machine_only_root_loads(
    struct Sym *root, int root_offset)
{
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        int memory_type;
        int memory_storage;
        int memory_offset;

        if (insn->opcode != MIR_LOAD)
            continue;
        if (!mir_scalar_memory_location(
                insn, &memory_type, &memory_storage,
                &memory_offset) ||
            memory_storage != SC_GLOBAL ||
            memory_offset != root_offset ||
            find_global(insn->name) != root)
            return 0;
    }
    return 1;
}

static int mir_machine_byte_cursor_update(
    const struct MirInsn *store, int operation,
    struct Sym **root_out, int *offset_out,
    int *old_value, int *new_value,
    const struct MirInsn **load_out)
{
    const struct MirInsn *binary;
    const struct MirInsn *load;
    const struct MirInsn *one;
    struct Sym *store_root;
    struct Sym *load_root;
    int store_offset;
    int load_offset;

    if (store == NULL || store->opcode != MIR_STORE_INDIRECT ||
        store->memory_size != 1 || store->bit_width != 0 ||
        (store->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_global_member(
            store->src1, &store_root, &store_offset))
        return 0;
    binary = mir_definition(store->src2);
    if (binary == NULL || binary->opcode != MIR_BINARY ||
        binary->immediate != operation)
        return 0;
    load = mir_definition(binary->src1);
    one = mir_definition(binary->src2);
    if (load == NULL || load->opcode != MIR_LOAD_INDIRECT ||
        load->memory_size != 1 || load->bit_width != 0 ||
        (load->type & TYPE_UNSIGNED) == 0 ||
        (load->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_global_member(
            load->src1, &load_root, &load_offset) ||
        load_root != store_root || load_offset != store_offset ||
        one == NULL || one->opcode != MIR_CONST ||
        one->immediate != 1)
        return 0;
    *root_out = store_root;
    *offset_out = store_offset;
    *old_value = load->dst;
    *new_value = binary->dst;
    if (load_out != NULL)
        *load_out = load;
    return 1;
}

static int mir_machine_byte_stack_address(
    int value, int cursor_value, struct Sym **root_out,
    int *offset_out)
{
    const struct MirInsn *address = mir_definition(value);
    long offset;

    if (mir_machine_transparent_pointer_unary(address)) {
        value = address->src1;
        address = mir_definition(value);
    }
    if (address == NULL || address->opcode != MIR_BINARY ||
        address->immediate != '+')
        return 0;
    if (address->src1 == cursor_value &&
        mir_machine_global_address_offset(
            address->src2, root_out, &offset, 0)) {
        *offset_out = (int)offset;
        return 1;
    }
    if (address->src2 == cursor_value &&
        mir_machine_global_address_offset(
            address->src1, root_out, &offset, 0)) {
        *offset_out = (int)offset;
        return 1;
    }
    return 0;
}

static int mir_machine_value_object(int value)
{
    const struct MirInsn *definition = mir_definition(value);

    if (definition == NULL)
        return -1;
    if (definition->opcode == MIR_UNARY &&
        definition->immediate == 0) {
        const struct MirInsn *source =
            mir_definition(definition->src1);
        int source_width;
        int target_width;

        if (source == NULL ||
            type_ptr_depth(source->type) != 0 ||
            type_ptr_depth(definition->type) != 0 ||
            type_is_float(source->type) ||
            type_is_float(definition->type) ||
            (source->type & 15) == TYPE_BOOL ||
            (definition->type & 15) == TYPE_BOOL)
            return -1;
        source_width = type_size(source->type);
        target_width = type_size(definition->type);
        if (target_width != source_width &&
            !(source_width == 1 && target_width == 2))
            return -1;
        return mir_machine_value_object(definition->src1);
    }
    if (definition->opcode != MIR_LOAD &&
        definition->opcode != MIR_PHI &&
        definition->opcode != MIR_CONST)
        return -1;
    return definition->object;
}

static int mir_machine_reduction_operand(
    int value, const struct MirInsn *element_load,
    int result_width, int *is_unsigned)
{
    const struct MirInsn *conversions[8];
    int conversion_count = 0;
    int current_type;
    int current_width;
    int conversion;

    while (value != element_load->dst) {
        const struct MirInsn *definition = mir_definition(value);

        if (definition == NULL || definition->opcode != MIR_UNARY ||
            definition->immediate != 0 || conversion_count >= 8)
            return 0;
        conversions[conversion_count++] = definition;
        value = definition->src1;
    }
    current_type = element_load->type;
    current_width = type_size(current_type);
    if (type_ptr_depth(current_type) != 0 ||
        type_is_float(current_type) ||
        (current_type & 15) == TYPE_BOOL)
        return 0;
    *is_unsigned = (current_type & TYPE_UNSIGNED) != 0;
    for (conversion = conversion_count - 1;
         conversion >= 0; --conversion) {
        int target_type = conversions[conversion]->type;
        int target_width = type_size(target_type);

        if (type_ptr_depth(target_type) != 0 ||
            type_is_float(target_type) ||
            (target_type & 15) == TYPE_BOOL)
            return 0;
        if (target_width == current_width) {
            current_type = target_type;
            if (current_width == 1)
                *is_unsigned =
                    (current_type & TYPE_UNSIGNED) != 0;
        } else if (current_width == 1 &&
                   target_width == 2) {
            current_type = target_type;
            current_width = target_width;
        } else {
            return 0;
        }
    }
    return current_width == result_width;
}

static int mir_machine_find_branch_for_value(
    int value, int *branch_position)
{
    int instruction;
    int found = 0;

    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_BRANCH_FALSE &&
            mir.insns[instruction].src1 == value) {
            *branch_position = instruction;
            ++found;
        }
    return found == 1;
}

static int mir_machine_find_header_before(int position, int *label)
{
    int instruction;

    for (instruction = position - 1; instruction >= 0; --instruction)
        if (mir.insns[instruction].opcode == MIR_LABEL) {
            *label = mir.insns[instruction].label;
            return 1;
        }
    return 0;
}

static void mir_machine_emit_bc_offset(MirStream *out, int offset)
{
    if (offset == 0)
        return;
    if (offset >= -4 && offset <= 4) {
        const char *instruction = offset > 0 ? "\tinc bc\n" : "\tdec bc\n";
        int count = offset > 0 ? offset : -offset;

        while (count-- > 0)
            mir_stream_puts(instruction, out);
        return;
    }
    mir_stream_printf(out, "\tld hl,%d\n\tadd hl,bc\n"
                 "\tld b,h\n\tld c,l\n", offset & 0xffff);
}

static void mir_machine_emit_parameter_store_to_bc(
    MirStream *out, const struct MirGlobalAppendStore *store)
{
    mir_stream_printf(out, "\tld hl,%d\n\tadd hl,sp\n",
            store->parameter_stack_offset);
    if (store->width == 1) {
        mir_stream_puts("\tld a,(hl)\n\tld (bc),a\n", out);
    } else {
        mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
              "\tld a,e\n\tld (bc),a\n\tinc bc\n"
              "\tld a,d\n\tld (bc),a\n", out);
    }
}

static void mir_machine_emit_indexed_stack_base(
    MirStream *out, const struct MirIndexedStack *plan)
{
    mir_machine_emit_global_word(
        out, plan->root, plan->root_offset);
    mir_stream_puts("\tld c,l\n\tld b,h\n"
          "\tld l,c\n\tld h,b\n", out);
    mir_machine_emit_hl_offset(
        out, plan->base_member_offset, 1);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tpush de\n\tld l,c\n\tld h,b\n", out);
    mir_machine_emit_hl_offset(
        out, plan->index_member_offset, 0);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n", out);
}

static void mir_emit_word_as_long_return(MirStream *out, int value)
{
    mir_stream_printf(out,
            "\tld hl,%d\n\tld a,h\n\trlca\n\tsbc a,a\n"
            "\tld e,a\n\tld d,a\n\tret\n",
            value);
}

static void mir_emit_cleared_record_field(
    MirStream *out, int field_offset, int parameter_offset)
{
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n",
            parameter_offset);
    mir_stream_puts("\tpush iy\n\tpop hl\n", out);
    mir_machine_emit_hl_offset(out, field_offset, 0);
    mir_stream_puts("\tld (hl),e\n\tinc hl\n\tld (hl),d\n", out);
}

static void mir_emit_wide_ix_load(MirStream *out, int offset)
{
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
            offset, offset + 1, offset + 2, offset + 3);
}

static void mir_emit_wide_ix_store(MirStream *out, int offset)
{
    mir_stream_printf(out,
            "\tld (ix%+d),l\n\tld (ix%+d),h\n"
            "\tld (ix%+d),e\n\tld (ix%+d),d\n",
            offset, offset + 1, offset + 2, offset + 3);
}

static void mir_emit_wide_ix_equal_branch(
    MirStream *out, int expected_offset, int equal_label)
{
    mir_stream_printf(out,
            "\tld a,l\n\txor (ix%+d)\n\tld c,a\n"
            "\tld a,h\n\txor (ix%+d)\n\tor c\n\tld c,a\n"
            "\tld a,e\n\txor (ix%+d)\n\tor c\n\tld c,a\n"
            "\tld a,d\n\txor (ix%+d)\n\tor c\n"
            "\tjp z,L%d\n",
            expected_offset, expected_offset + 1,
            expected_offset + 2, expected_offset + 3,
            equal_label);
}

static void mir_emit_wide_div_failure(
    MirStream *out, const struct MirWideDivResultCheck *plan,
    int actual_offset, int expected_offset)
{
    mir_emit_local_wide_argument(out, expected_offset);
    mir_emit_wide_ix_load(out, actual_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n",
            plan->name_stack_offset + 2,
            plan->name_stack_offset + 3);
    mir_machine_emit_symbol_call(out, plan->failure_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
}

static void mir_emit_final_call_check(
    MirStream *out, const struct MirFinalCallCheck *check)
{
    int argument;
    int words = check->width / 2;

    if (check->kind == MIR_FINAL_CALL_DIRECT) {
        for (argument = 3; argument >= 0; --argument)
            mir_emit_final_call_constant(
                out, check->direct_values[argument],
                check->width);
    } else {
        mir_emit_final_call_constant(
            out, check->expected, check->width);
        if (check->kind == MIR_FINAL_CALL_NESTED_STRING)
            mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
                    check->value_string_id);
        else
            mir_emit_final_call_constant(
                out, check->value, check->width);
        mir_machine_emit_symbol_call(
            out, check->value_function);
        mir_emit_final_call_cleanup(
            out,
            check->kind == MIR_FINAL_CALL_NESTED_STRING ?
                1 : words);
        if (check->width == 4)
            mir_stream_puts("\tpush de\n\tpush hl\n", out);
        else
            mir_stream_puts("\tpush hl\n", out);
    }
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            check->name_string_id);
    mir_machine_emit_symbol_call(
        out, check->check_function);
    mir_emit_final_call_cleanup(
        out,
        check->kind == MIR_FINAL_CALL_DIRECT ?
            1 + 4 * words : 1 + 2 * words);
}

static int mir_emit_math_constant_argument(
    MirStream *out, const struct MirMathConstant *argument)
{
    if (argument->kind == MIR_MATH_FLOAT_CONSTANT) {
        mir_emit_final_call_constant(out, argument->value, 4);
        return 2;
    }
    mir_stream_printf(out, "\tld hl,%lu\n\tpush hl\n",
            argument->value & 0xffffUL);
    return 1;
}

static void mir_emit_math_call_check(
    MirStream *out, const struct MirMathCallCheck *check)
{
    int argument;
    int argument_words = 0;

    mir_emit_final_call_constant(
        out, check->expected_bits, 4);
    for (argument = check->argument_count - 1;
         argument >= 0; --argument)
        argument_words += mir_emit_math_constant_argument(
            out, &check->arguments[argument]);
    mir_machine_emit_symbol_call(
        out, check->value_function);
    mir_emit_final_call_cleanup(out, argument_words);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            check->string_id);
    mir_machine_emit_symbol_call(
        out, check->check_function);
    mir_emit_final_call_cleanup(out, 5);
}

static void mir_emit_math_frexp_check(
    MirStream *out, const struct MirMathPointerCheck *check)
{
    mir_emit_final_call_constant(
        out, check->result_bits, 4);
    mir_emit_local_address(out, -2);
    mir_stream_puts("\tpush hl\n", out);
    mir_emit_final_call_constant(
        out, check->input_bits, 4);
    mir_machine_emit_symbol_call(
        out, check->value_function);
    mir_emit_final_call_cleanup(out, 3);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            check->result_string_id);
    mir_machine_emit_symbol_call(
        out, check->check_function);
    mir_emit_final_call_cleanup(out, 5);

    mir_emit_final_call_constant(
        out, check->output_bits, 4);
    mir_stream_puts("\tld l,(ix-2)\n\tld h,(ix-1)\n", out);
    mir_emit_runtime_call(out, "__fif");
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            check->output_string_id);
    mir_machine_emit_symbol_call(
        out, check->check_function);
    mir_emit_final_call_cleanup(out, 5);
}

static void mir_emit_math_modf_check(
    MirStream *out, const struct MirMathPointerCheck *check)
{
    mir_emit_final_call_constant(
        out, check->result_bits, 4);
    mir_emit_local_address(out, -4);
    mir_stream_puts("\tpush hl\n", out);
    mir_emit_final_call_constant(
        out, check->input_bits, 4);
    mir_machine_emit_symbol_call(
        out, check->value_function);
    mir_emit_final_call_cleanup(out, 3);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            check->result_string_id);
    mir_machine_emit_symbol_call(
        out, check->check_function);
    mir_emit_final_call_cleanup(out, 5);

    mir_emit_final_call_constant(
        out, check->output_bits, 4);
    mir_stream_puts("\tld l,(ix-4)\n\tld h,(ix-3)\n"
          "\tld e,(ix-2)\n\tld d,(ix-1)\n"
          "\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            check->output_string_id);
    mir_machine_emit_symbol_call(
        out, check->check_function);
    mir_emit_final_call_cleanup(out, 5);
}

static void mir_emit_ctype_pointer_load(MirStream *out)
{
    mir_stream_puts("\tpop hl\n\tpush hl\n", out);
}

static void mir_emit_ctype_pointer_store(MirStream *out)
{
    mir_stream_puts("\tpop bc\n\tpush hl\n", out);
}

static void mir_emit_ctype_compare_bool(
    MirStream *out, unsigned long expected, int comparison)
{
    int done = new_label();

    if ((expected & 0xffffUL) == 0)
        mir_stream_puts("\tld a,h\n\tor l\n", out);
    else
        mir_stream_printf(out, "\tld de,%lu\n\tor a\n\tsbc hl,de\n",
                expected & 0xffffUL);
    mir_stream_printf(out,
            "\tld hl,0\n\tjp %s,L%d\n\tinc hl\nL%d:\n",
            comparison == TOK_EQ ? "nz" : "z",
            done, done);
}

static void mir_emit_ctype_check_result(
    MirStream *out, unsigned long expected, int comparison,
    int string_id, struct Sym *check_function)
{
    mir_emit_ctype_compare_bool(out, expected, comparison);
    mir_stream_puts("\tex de,hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_stream_puts("\tex de,hl\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, check_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
}

static void mir_emit_ctype_failure(
    MirStream *out, int string_id, struct Sym *print_function)
{
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_machine_emit_symbol_call(out, print_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld hl,1\n\tret\n", out);
}

static void mir_emit_ctype_resize(
    MirStream *out, const struct MirCtypeReallocSchedule *plan,
    unsigned long size)
{
    mir_emit_ctype_pointer_load(out);
    mir_stream_puts("\tex de,hl\n", out);
    mir_stream_printf(out, "\tld hl,%lu\n\tpush hl\n\tpush de\n",
            size & 0xffffUL);
    mir_machine_emit_symbol_call(out, plan->resize_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_emit_ctype_pointer_store(out);
}

static void mir_emit_context_wide_global_store(
    MirStream *out, struct Sym *root, int offset,
    unsigned long value)
{
    mir_machine_emit_float_bits(out, value);
    mir_machine_emit_global_word_store(out, root, offset);
    mir_stream_puts("\tex de,hl\n", out);
    mir_machine_emit_global_word_store(out, root, offset + 2);
}

static void mir_emit_context_array_init(
    MirStream *out, const struct MirContextOpSchedule *plan)
{
    int loop = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_address_de(
        out, plan->array_root, plan->array_offset);
    mir_stream_puts("\tld b,d\n\tld c,e\n"
          "\tld hl,0\n\tld de,0\n\tld a,8\n", out);
    mir_stream_printf(out,
            "L%d:\n\tpush af\n"
            "\tld a,l\n\tld (bc),a\n\tinc bc\n"
            "\tld a,h\n\tld (bc),a\n\tinc bc\n"
            "\tld a,e\n\tld (bc),a\n\tinc bc\n"
            "\tld a,d\n\tld (bc),a\n\tinc bc\n"
            "\tpush bc\n\tld bc,34464\n\tadd hl,bc\n"
            "\tex de,hl\n\tld bc,1\n\tadc hl,bc\n"
            "\tex de,hl\n\tpop bc\n"
            "\tpop af\n\tdec a\n\tjp nz,L%d\n",
            loop, loop);
}

static int mir_emit_context_argument(
    MirStream *out, const struct MirContextOpArgument *argument)
{
    mir_emit_final_call_constant(
        out, argument->value, argument->width);
    return argument->width / 2;
}

static void mir_emit_context_check(
    MirStream *out, const struct MirContextOpCheck *check)
{
    int argument;
    int argument_words = 0;

    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            check->string_id);
    mir_emit_final_call_constant(
        out, check->expected, check->check_width);
    for (argument = check->argument_count - 1;
         argument >= 0; --argument)
        argument_words += mir_emit_context_argument(
            out, &check->arguments[argument]);
    mir_machine_emit_symbol_call(out, check->value_function);
    mir_emit_final_call_cleanup(out, argument_words);
    if (check->result_width == 2) {
        if (check->result_unsigned)
            mir_stream_puts("\tld de,0\n", out);
        else
            mir_stream_puts("\tld a,h\n\trlca\n\tsbc a,a\n"
                  "\tld e,a\n\tld d,a\n", out);
    }
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, check->check_function);
    mir_emit_final_call_cleanup(out, 5);
}

static void mir_emit_atof_schedule_cleanup(
    MirStream *out, int words)
{
    if (words >= 5) {
        mir_stream_printf(out,
                "\tld hl,%d\n\tadd hl,sp\n\tld sp,hl\n",
                words * 2);
        return;
    }
    mir_emit_final_call_cleanup(out, words);
}

static void mir_emit_atof_schedule_value(
    MirStream *out, const struct MirAtofSchedule *plan,
    const struct MirAtofScheduleCheck *item)
{
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            item->input_string_id);
    mir_machine_emit_symbol_call(out, plan->value_function);
    mir_stream_puts("\tpop bc\n", out);
    if (item->has_scale) {
        mir_stream_puts("\tpush de\n\tpush hl\n", out);
        mir_machine_emit_float_bits(out, item->scale);
        mir_emit_runtime_call(out, "__fmf");
        mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    }
}

static void mir_emit_atof_schedule_check(
    MirStream *out, const struct MirAtofSchedule *plan,
    const struct MirAtofScheduleCheck *item)
{
    if (item->kind == MIR_ATOF_SCHEDULE_FLOAT) {
        mir_emit_final_call_constant(out, item->tolerance, 4);
        mir_emit_final_call_constant(out, item->expected, 4);
        mir_emit_atof_schedule_value(out, plan, item);
        mir_stream_puts("\tpush de\n\tpush hl\n", out);
        mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
                item->name_string_id);
        mir_machine_emit_symbol_call(
            out, plan->float_check_function);
        mir_emit_atof_schedule_cleanup(out, 7);
        return;
    }
    if (item->kind == MIR_ATOF_SCHEDULE_INT) {
        mir_stream_printf(out, "\tld hl,%lu\n\tpush hl\n",
                item->expected & 0xffffUL);
        mir_emit_atof_schedule_value(out, plan, item);
        mir_emit_runtime_call(out, "__ffi");
        mir_stream_puts("\tpush hl\n", out);
        mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
                item->name_string_id);
        mir_machine_emit_symbol_call(
            out, plan->int_check_function);
        mir_emit_atof_schedule_cleanup(out, 3);
        return;
    }
    if (item->kind == MIR_ATOF_SCHEDULE_END) {
        mir_stream_printf(out,
                "\tld hl,%lu\n\tpush hl\n"
                "\tld hl,S%d\n\tpush hl\n"
                "\tld hl,S%d\n\tpush hl\n",
                item->expected & 0xffffUL,
                item->input_string_id,
                item->name_string_id);
        mir_machine_emit_symbol_call(
            out, plan->end_check_function);
        mir_emit_atof_schedule_cleanup(out, 3);
        return;
    }
    if (item->kind == MIR_ATOF_SCHEDULE_INFINITY) {
        mir_stream_printf(out, "\tld hl,%lu\n\tpush hl\n",
                item->expected & 0xffffUL);
        mir_emit_atof_schedule_value(out, plan, item);
        mir_stream_puts("\tpush de\n\tpush hl\n", out);
        mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
                item->name_string_id);
        mir_machine_emit_symbol_call(
            out, plan->infinity_check_function);
        mir_emit_atof_schedule_cleanup(out, 4);
        return;
    }
    mir_emit_atof_schedule_value(out, plan, item);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            item->name_string_id);
    mir_machine_emit_symbol_call(
        out, plan->nan_check_function);
    mir_emit_atof_schedule_cleanup(out, 3);
}

static int mir_match_affine_pointer_constant_return(long *value_out)
{
    const struct MirInsn *return_insn = NULL;
    struct MirMachineForm form;
    int return_count = 0;
    int instruction;

    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        type_ptr_depth(mir.return_type) != 0 ||
        type_size(mir.return_type) != 2 ||
        type_is_float(mir.return_type))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
        case MIR_CONST:
        case MIR_ADDRESS:
        case MIR_UNARY:
        case MIR_BINARY:
            break;
        case MIR_LOAD:
            if (!mir_machine_named_nonvolatile(insn))
                return 0;
            break;
        case MIR_STORE:
            if (!mir_machine_unobservable_local_store(insn))
                return 0;
            break;
        case MIR_RETURN:
            return_insn = insn;
            ++return_count;
            break;
        default:
            return 0;
        }
    }
    if (return_count != 1 || return_insn == NULL ||
        !mir_machine_pointer_form(
            return_insn->src1, (int)(return_insn - mir.insns),
            &form, 0) ||
        form.kind != MIR_MACHINE_FORM_INTEGER ||
        form.pointer_terms < 2)
        return 0;
    *value_out = form.value & 0xffffL;
    return 1;
}

static int mir_match_local_constant_store_return(long *value_out)
{
    const struct MirInsn *store = NULL;
    const struct MirInsn *load = NULL;
    const struct MirInsn *return_insn = NULL;
    struct MirMachineForm store_address;
    struct MirMachineForm load_address;
    struct MirMachineForm value;
    int instruction;

    if (mir.has_vla || mir_cfg_block_count() > 2 ||
        type_ptr_depth(mir.return_type) != 0 ||
        type_size(mir.return_type) != 2 ||
        type_is_float(mir.return_type))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
        case MIR_CONST:
        case MIR_ADDRESS:
        case MIR_INDEX_ADDRESS:
        case MIR_UNARY:
        case MIR_BINARY:
            break;
        case MIR_JUMP:
            break;
        case MIR_STORE_INDIRECT:
            if (store != NULL || insn->memory_size != 2 ||
                insn->bit_width != 0 ||
                (insn->memory_flags & (1 | 8)) != 0)
                return 0;
            store = insn;
            break;
        case MIR_LOAD_INDIRECT:
            if (load != NULL || insn->memory_size != 2 ||
                insn->bit_width != 0 ||
                (insn->memory_flags & (1 | 8)) != 0)
                return 0;
            load = insn;
            break;
        case MIR_RETURN:
            if (return_insn != NULL)
                return 0;
            return_insn = insn;
            break;
        default:
            return 0;
        }
    }
    if (store == NULL || load == NULL || return_insn == NULL ||
        store >= load || load >= return_insn ||
        return_insn->src1 != load->dst ||
        !mir_machine_pointer_form(
            store->src1, (int)(store - mir.insns),
            &store_address, 0) ||
        !mir_machine_pointer_form(
            load->src1, (int)(load - mir.insns),
            &load_address, 0) ||
        store_address.kind != MIR_MACHINE_FORM_POINTER ||
        load_address.kind != MIR_MACHINE_FORM_POINTER ||
        store_address.storage != SC_LOCAL ||
        load_address.storage != SC_LOCAL ||
        store_address.offset != load_address.offset ||
        store_address.value != load_address.value ||
        strcmp(store_address.name, load_address.name) ||
        !mir_machine_name_nonvolatile(store_address.name) ||
        !mir_machine_pointer_form(
            store->src2, (int)(store - mir.insns),
            &value, 0) ||
        value.kind != MIR_MACHINE_FORM_INTEGER)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_JUMP) {
            int target;

            for (target = instruction + 1;
                 target < mir.count; ++target)
                if (mir.insns[target].opcode == MIR_LABEL &&
                    mir.insns[target].label ==
                        mir.insns[instruction].label)
                    break;
            if (target >= mir.count || &mir.insns[target] > store)
                return 0;
        }
    *value_out = value.value & 0xffffL;
    return 1;
}

static int mir_match_word_range_bool(struct MirWordRangeBool *plan)
{
    static const int expected_opcodes[18] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_RETURN
    };
    const struct MirInsn *parameter;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 18 || mir_cfg_block_count() != 4 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        ((mir.return_type & 15) != TYPE_INT &&
         (mir.return_type & 15) != TYPE_BOOL))
        return mir_machine_reject("word-range-bool", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject("word-range-bool", "opcode");
    parameter = &mir.insns[1];
    if (type_ptr_depth(parameter->type) != 0 ||
        (parameter->type & 15) != TYPE_INT ||
        (parameter->type & TYPE_UNSIGNED) != 0 ||
        type_size(parameter->type) != 2 ||
        !mir_machine_parameter_value_offset(
            parameter->dst, &plan->parameter_stack_offset) ||
        mir.insns[4].immediate != TOK_GE ||
        mir.insns[4].src1 != parameter->dst ||
        mir.insns[4].src2 != mir.insns[3].dst ||
        mir.insns[5].src1 != mir.insns[4].dst ||
        mir.insns[5].label != mir.insns[13].label ||
        mir.insns[8].immediate != '<' ||
        mir.insns[8].src1 != parameter->dst ||
        mir.insns[8].src2 != mir.insns[7].dst ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].label != mir.insns[13].label)
        return mir_machine_reject("word-range-bool", "comparisons");
    if (!mir_machine_constant_equals(mir.insns[11].dst, 1) ||
        mir.insns[12].label != mir.insns[15].label ||
        !mir_machine_constant_equals(mir.insns[14].dst, 0) ||
        mir.insns[16].src1 != mir.insns[11].dst ||
        mir.insns[16].src2 != mir.insns[14].dst ||
        mir.insns[16].phi_pred1 != mir.insns[10].label ||
        mir.insns[16].phi_pred2 != mir.insns[13].label ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        mir.insns[3].immediate != 0 ||
        mir.insns[7].immediate <= 0 ||
        mir.insns[7].immediate > 255)
        return mir_machine_reject("word-range-bool", "result");
    plan->lower = 0;
    plan->upper = (int)mir.insns[7].immediate;
    return 1;
}

static int mir_match_ascii_upper(struct MirAsciiUpper *plan)
{
    static const int expected_opcodes[31] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_CONST, MIR_BINARY,
        MIR_UNARY, MIR_RETURN, MIR_LABEL, MIR_NOP, MIR_RETURN
    };
    const struct MirInsn *parameter;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 31 || mir_cfg_block_count() != 5 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_CHAR ||
        type_size(mir.return_type) != 1)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    parameter = &mir.insns[1];
    if (type_ptr_depth(parameter->type) != 0 ||
        (parameter->type & 15) != TYPE_CHAR ||
        type_size(parameter->type) != 1 ||
        !mir_machine_parameter_value_offset(
            parameter->dst, &plan->parameter_stack_offset) ||
        mir.insns[4].immediate != 0 ||
        mir.insns[4].src1 != parameter->dst ||
        mir.insns[5].immediate != TOK_GE ||
        mir.insns[5].src1 != mir.insns[4].dst ||
        mir.insns[5].src2 != mir.insns[3].dst ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        mir.insns[6].label != mir.insns[15].label)
        return 0;
    if (mir.insns[9].immediate != 0 ||
        mir.insns[9].src1 != parameter->dst ||
        mir.insns[10].immediate != TOK_LE ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[10].src2 != mir.insns[8].dst ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[11].label != mir.insns[15].label ||
        !mir_machine_constant_equals(mir.insns[13].dst, 1) ||
        mir.insns[14].label != mir.insns[17].label ||
        !mir_machine_constant_equals(mir.insns[16].dst, 0) ||
        mir.insns[18].src1 != mir.insns[13].dst ||
        mir.insns[18].src2 != mir.insns[16].dst ||
        mir.insns[18].phi_pred1 != mir.insns[12].label ||
        mir.insns[18].phi_pred2 != mir.insns[15].label ||
        mir.insns[19].src1 != mir.insns[18].dst ||
        mir.insns[19].label != mir.insns[28].label)
        return 0;
    if (mir.insns[22].immediate != 0 ||
        mir.insns[22].src1 != parameter->dst ||
        mir.insns[23].immediate != '-' ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[23].src2 != mir.insns[21].dst ||
        mir.insns[25].immediate != '+' ||
        mir.insns[25].src1 != mir.insns[23].dst ||
        mir.insns[25].src2 != mir.insns[24].dst ||
        mir.insns[26].immediate != 0 ||
        mir.insns[26].src1 != mir.insns[25].dst ||
        mir.insns[27].src1 != mir.insns[26].dst ||
        mir.insns[30].src1 != parameter->dst)
        return 0;
    plan->lower = (int)mir.insns[3].immediate;
    plan->upper = (int)mir.insns[8].immediate;
    plan->adjustment =
        (int)mir.insns[24].immediate - (int)mir.insns[21].immediate;
    plan->width = 1;
    if (plan->lower < 0 || plan->lower > 255 ||
        plan->upper < plan->lower || plan->upper > 255 ||
        plan->adjustment < -255 || plan->adjustment > 255)
        return 0;
    return 1;
}

static int mir_match_ascii_word_case(struct MirAsciiUpper *plan)
{
    static const int expected_opcodes[27] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_RETURN, MIR_LABEL,
        MIR_NOP, MIR_RETURN
    };
    const struct MirInsn *parameter = &mir.insns[1];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 27 || mir_cfg_block_count() != 5 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        (mir.return_type & TYPE_UNSIGNED) != 0 ||
        type_size(mir.return_type) != 2)
        return mir_machine_reject("ascii-word-case", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject("ascii-word-case", "opcode");
    if (type_ptr_depth(parameter->type) != 0 ||
        (parameter->type & 15) != TYPE_INT ||
        (parameter->type & TYPE_UNSIGNED) != 0 ||
        type_size(parameter->type) != 2 ||
        !mir_machine_parameter_value_offset(
            parameter->dst, &plan->parameter_stack_offset) ||
        mir.insns[4].immediate != TOK_GE ||
        mir.insns[4].src1 != parameter->dst ||
        mir.insns[4].src2 != mir.insns[3].dst ||
        mir.insns[5].src1 != mir.insns[4].dst ||
        mir.insns[5].label != mir.insns[13].label ||
        mir.insns[8].immediate != TOK_LE ||
        mir.insns[8].src1 != parameter->dst ||
        mir.insns[8].src2 != mir.insns[7].dst ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].label != mir.insns[13].label ||
        !mir_machine_constant_equals(mir.insns[11].dst, 1) ||
        mir.insns[12].label != mir.insns[15].label ||
        !mir_machine_constant_equals(mir.insns[14].dst, 0) ||
        mir.insns[16].src1 != mir.insns[11].dst ||
        mir.insns[16].src2 != mir.insns[14].dst ||
        mir.insns[16].phi_pred1 != mir.insns[10].label ||
        mir.insns[16].phi_pred2 != mir.insns[13].label ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        mir.insns[17].label != mir.insns[24].label ||
        mir.insns[22].immediate != '+' ||
        mir.insns[22].src1 != parameter->dst ||
        mir.insns[22].src2 != mir.insns[21].dst ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[26].src1 != parameter->dst)
        return mir_machine_reject("ascii-word-case", "flow");
    plan->width = 2;
    plan->lower = (int)mir.insns[3].immediate;
    plan->upper = (int)mir.insns[7].immediate;
    plan->adjustment = (int)mir.insns[21].immediate;
    if (plan->lower < 0 || plan->lower > 255 ||
        plan->upper < plan->lower || plan->upper > 255 ||
        plan->adjustment < -255 || plan->adjustment > 255)
        return mir_machine_reject("ascii-word-case", "constants");
    return 1;
}

static int mir_match_fixed_word_array_sum(
    struct MirFixedWordArraySum *plan)
{
    static const int expected_opcodes[34] = {
        MIR_LABEL, MIR_PARAM, MIR_CONST, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_CONST, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_PHI, MIR_PHI,
        MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_RETURN
    };
    const struct MirInsn *parameter;
    const struct MirInsn *sum_phi;
    const struct MirInsn *index_phi;
    int declared;
    int found_declaration = 0;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 34 || mir_cfg_block_count() != 4 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    parameter = &mir.insns[1];
    sum_phi = &mir.insns[10];
    index_phi = &mir.insns[11];
    if (type_ptr_depth(parameter->type) != 1 ||
        (parameter->type & 15) != TYPE_INT ||
        mir_machine_pointee_is_volatile(parameter) ||
        !mir_machine_parameter_value_offset(
            parameter->dst, &plan->parameter_stack_offset) ||
        !mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[4]) ||
        mir.insns[4].memory_size != 2 ||
        !mir_machine_constant_equals(mir.insns[6].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[7]) ||
        mir.insns[7].memory_size != 1)
        return 0;
    for (declared = 0; declared < mir.declared_count; ++declared)
        if (!strcmp(mir.declared_names[declared], parameter->name)) {
            plan->pointer_is_volatile =
                mir.declared_is_volatile[declared];
            found_declaration = 1;
            break;
        }
    if (!found_declaration)
        return 0;
    if (sum_phi->src1 != mir.insns[2].dst ||
        sum_phi->src2 != mir.insns[22].dst ||
        sum_phi->phi_pred1 != mir.insns[0].label ||
        sum_phi->phi_pred2 != mir.insns[25].label ||
        type_ptr_depth(sum_phi->type) != 0 ||
        (sum_phi->type & 15) != TYPE_INT ||
        type_size(sum_phi->type) != 2 ||
        index_phi->src1 != mir.insns[6].dst ||
        index_phi->src2 != mir.insns[28].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[25].label ||
        (index_phi->type & TYPE_UNSIGNED) == 0 ||
        type_size(index_phi->type) != 1)
        return 0;
    if (mir.insns[13].immediate <= 0 ||
        mir.insns[13].immediate > 16 ||
        mir.insns[14].immediate != 0 ||
        mir.insns[14].src1 != index_phi->dst ||
        mir.insns[15].immediate != '<' ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        mir.insns[15].src2 != mir.insns[13].dst ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[16].label != mir.insns[31].label ||
        mir.insns[20].src1 != parameter->dst ||
        mir.insns[20].src2 != index_phi->dst ||
        mir.insns[20].immediate != 2 ||
        mir.insns[20].memory_size != 2 ||
        mir.insns[21].src1 != mir.insns[20].dst ||
        mir.insns[21].memory_size != 2 ||
        (mir.insns[21].memory_flags & (1 | 8)) != 0)
        return 0;
    if (mir.insns[22].immediate != '+' ||
        mir.insns[22].src1 != sum_phi->dst ||
        mir.insns[22].src2 != mir.insns[21].dst ||
        !mir_machine_same_location(&mir.insns[4], &mir.insns[24]) ||
        mir.insns[24].src1 != mir.insns[22].dst ||
        !mir_machine_constant_equals(mir.insns[27].dst, 1) ||
        mir.insns[28].immediate != '+' ||
        mir.insns[28].src1 != index_phi->dst ||
        mir.insns[28].src2 != mir.insns[27].dst ||
        !mir_machine_same_location(&mir.insns[7], &mir.insns[29]) ||
        mir.insns[29].src1 != mir.insns[28].dst ||
        mir.insns[30].label != mir.insns[8].label ||
        mir.insns[33].src1 != sum_phi->dst)
        return 0;
    plan->count = (int)mir.insns[13].immediate;
    return 1;
}

static int mir_match_slice_word_sum(struct MirSliceWordSum *plan)
{
    static const int expected_opcodes[36] = {
        MIR_LABEL, MIR_PARAM, MIR_CONST, MIR_NOP, MIR_STORE, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_LABEL, MIR_PHI, MIR_PHI, MIR_NOP,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_RETURN
    };
    const struct MirInsn *sum_phi;
    const struct MirInsn *index_phi;
    int base_offset;
    int base_storage;
    int base_type;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 36 || mir_cfg_block_count() != 4 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    sum_phi = &mir.insns[9];
    index_phi = &mir.insns[10];
    if (!mir_scalar_memory_location(
            &mir.insns[12], &base_type, &base_storage, &base_offset) ||
        base_storage != SC_PARAM ||
        !type_is_struct_object(base_type) ||
        base_offset < 2 ||
        !mir_machine_same_location(&mir.insns[12], &mir.insns[18]) ||
        !mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[4]) ||
        mir.insns[4].memory_size != 2 ||
        !mir_machine_constant_equals(mir.insns[5].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[7]) ||
        mir.insns[7].memory_size != 2)
        return 0;
    if (sum_phi->src1 != mir.insns[2].dst ||
        sum_phi->src2 != mir.insns[24].dst ||
        sum_phi->phi_pred1 != mir.insns[0].label ||
        sum_phi->phi_pred2 != mir.insns[27].label ||
        type_ptr_depth(sum_phi->type) != 0 ||
        (sum_phi->type & 15) != TYPE_INT ||
        type_size(sum_phi->type) != 2 ||
        index_phi->src1 != mir.insns[5].dst ||
        index_phi->src2 != mir.insns[30].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[27].label ||
        type_ptr_depth(index_phi->type) != 0 ||
        (index_phi->type & 15) != TYPE_INT ||
        type_size(index_phi->type) != 2)
        return 0;
    if (mir.insns[13].src1 != mir.insns[12].dst ||
        mir.insns[13].immediate < 0 ||
        mir.insns[13].memory_size != 2 ||
        (mir.insns[13].memory_flags & (1 | 8)) != 0 ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        mir.insns[14].memory_size != 2 ||
        (mir.insns[14].memory_flags & (1 | 8)) != 0 ||
        mir.insns[15].immediate != '<' ||
        mir.insns[15].src1 != index_phi->dst ||
        mir.insns[15].src2 != mir.insns[14].dst ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[16].label != mir.insns[33].label)
        return 0;
    if (mir.insns[19].src1 != mir.insns[18].dst ||
        mir.insns[19].immediate < 0 ||
        mir.insns[19].memory_size != 2 ||
        (mir.insns[19].memory_flags & (1 | 8)) != 0 ||
        mir.insns[20].src1 != mir.insns[19].dst ||
        type_ptr_depth(mir.insns[20].type) != 1 ||
        (mir.insns[20].type & 15) != TYPE_INT ||
        mir.insns[20].memory_size != 2 ||
        (mir.insns[20].memory_flags & (1 | 8)) != 0 ||
        mir.insns[22].src1 != mir.insns[20].dst ||
        mir.insns[22].src2 != index_phi->dst ||
        mir.insns[22].immediate != 2 ||
        mir.insns[22].memory_size != 2 ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[23].memory_size != 2 ||
        (mir.insns[23].memory_flags & (1 | 8)) != 0)
        return 0;
    if (mir.insns[24].immediate != '+' ||
        mir.insns[24].src1 != sum_phi->dst ||
        mir.insns[24].src2 != mir.insns[23].dst ||
        !mir_machine_same_location(&mir.insns[4], &mir.insns[26]) ||
        mir.insns[26].src1 != mir.insns[24].dst ||
        !mir_machine_constant_equals(mir.insns[29].dst, 1) ||
        mir.insns[30].immediate != '+' ||
        mir.insns[30].src1 != index_phi->dst ||
        mir.insns[30].src2 != mir.insns[29].dst ||
        !mir_machine_same_location(&mir.insns[7], &mir.insns[31]) ||
        mir.insns[31].src1 != mir.insns[30].dst ||
        mir.insns[32].label != mir.insns[8].label ||
        mir.insns[35].src1 != sum_phi->dst)
        return 0;
    plan->parameter_stack_offset = base_offset - 2;
    plan->count_offset = (int)mir.insns[13].immediate;
    plan->data_offset = (int)mir.insns[19].immediate;
    if (plan->parameter_stack_offset < 0 ||
        plan->count_offset > 127 || plan->data_offset > 127 ||
        plan->count_offset == plan->data_offset)
        return 0;
    return 1;
}

static int mir_match_conditional_null_identity(
    struct MirConditionalNullIdentity *plan)
{
    static const int expected_opcodes[20] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_LOAD,
        MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_NOP, MIR_STORE, MIR_LOAD,
        MIR_LOAD, MIR_BINARY, MIR_RETURN
    };
    int memory_offset;
    int memory_storage;
    int memory_type;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 20 || mir_cfg_block_count() != 5 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (type_ptr_depth(mir.insns[1].type) != 0 ||
        (mir.insns[1].type & 15) != TYPE_INT ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->condition_stack_offset) ||
        type_ptr_depth(mir.insns[2].type) != 1 ||
        !mir_scalar_memory_location(
            &mir.insns[2], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_PARAM ||
        type_ptr_depth(memory_type) != 1 ||
        memory_offset < 2 ||
        mir.insns[4].src1 != mir.insns[1].dst ||
        mir.insns[4].label != mir.insns[9].label ||
        !mir_machine_constant_equals(mir.insns[5].dst, 0))
        return 0;
    plan->pointer_stack_offset = memory_offset - 2;
    if (!mir_machine_same_location(&mir.insns[2], &mir.insns[10]) ||
        mir.insns[8].label != mir.insns[12].label ||
        mir.insns[13].src1 != mir.insns[5].dst ||
        mir.insns[13].src2 != mir.insns[10].dst ||
        mir.insns[13].phi_pred1 != mir.insns[7].label ||
        mir.insns[13].phi_pred2 != mir.insns[11].label ||
        mir.insns[15].src1 != mir.insns[13].dst ||
        !mir_machine_same_location(&mir.insns[15], &mir.insns[16]) ||
        !mir_machine_same_location(&mir.insns[2], &mir.insns[17]) ||
        mir.insns[18].immediate != TOK_EQ ||
        mir.insns[18].src1 != mir.insns[16].dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        mir.insns[19].src1 != mir.insns[18].dst)
        return 0;
    return 1;
}

static int mir_match_wide_constant_equal(
    struct MirWideConstantEqual *plan)
{
    static const int expected_opcodes[17] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_JUMP, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_RETURN, MIR_LABEL, MIR_CONST, MIR_RETURN,
        MIR_NOP, MIR_LABEL
    };
    int memory_offset;
    int memory_storage;
    int memory_type;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 17 || mir_cfg_block_count() != 5 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (!mir_scalar_memory_location(
            &mir.insns[1], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_PARAM ||
        type_ptr_depth(memory_type) != 0 ||
        (memory_type & 15) != TYPE_LONG ||
        type_size(memory_type) != 4 ||
        memory_offset < 2 ||
        mir.insns[3].type != memory_type ||
        mir.insns[4].immediate != TOK_EQ ||
        mir.insns[4].src1 != mir.insns[1].dst ||
        mir.insns[4].src2 != mir.insns[3].dst ||
        mir.insns[5].src1 != mir.insns[4].dst ||
        mir.insns[5].label != mir.insns[12].label ||
        mir.insns[6].label != mir.insns[9].label ||
        mir.insns[8].label != mir.insns[12].label ||
        !mir_machine_constant_equals(mir.insns[10].dst, 1) ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        !mir_machine_constant_equals(mir.insns[13].dst, 0) ||
        mir.insns[14].src1 != mir.insns[13].dst)
        return 0;
    plan->parameter_stack_offset = memory_offset - 2;
    plan->value = (unsigned long)mir.insns[3].immediate;
    return 1;
}

static int mir_match_float_truth_once(struct MirFloatTruthOnce *plan)
{
    static const int expected_opcodes[22] = {
        MIR_LABEL, MIR_PARAM, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_PHI,
        MIR_PHI, MIR_NOP, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_FLOAT_CONST, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_RETURN
    };
    int memory_offset;
    int memory_storage;
    int memory_type;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 22 || mir_cfg_block_count() != 4 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (!mir_scalar_memory_location(
            &mir.insns[1], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_PARAM ||
        !type_is_float(memory_type) ||
        type_size(memory_type) != 4 ||
        memory_offset < 2 ||
        !mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[3]) ||
        mir.insns[5].src1 != mir.insns[1].dst ||
        mir.insns[5].src2 != mir.insns[13].dst ||
        mir.insns[5].phi_pred1 != mir.insns[0].label ||
        mir.insns[5].phi_pred2 != mir.insns[17].label ||
        mir.insns[6].src1 != mir.insns[2].dst ||
        mir.insns[6].src2 != mir.insns[11].dst ||
        mir.insns[6].phi_pred1 != mir.insns[0].label ||
        mir.insns[6].phi_pred2 != mir.insns[17].label ||
        mir.insns[8].src1 != mir.insns[5].dst ||
        mir.insns[8].label != mir.insns[19].label)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[10].dst, 1) ||
        mir.insns[11].immediate != '+' ||
        mir.insns[11].src1 != mir.insns[6].dst ||
        mir.insns[11].src2 != mir.insns[10].dst ||
        !mir_machine_same_location(&mir.insns[3], &mir.insns[12]) ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[13].immediate != 0 ||
        !mir_machine_same_location(&mir.insns[1], &mir.insns[15]) ||
        mir.insns[15].src1 != mir.insns[13].dst ||
        mir.insns[18].label != mir.insns[4].label ||
        mir.insns[21].src1 != mir.insns[6].dst)
        return 0;
    plan->parameter_stack_offset = memory_offset - 2;
    return 1;
}

static int mir_match_nested_word_long_select(
    struct MirNestedWordLongSelect *plan)
{
    static const int constant_opcodes[26] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_UNARY, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_BRANCH_FALSE, MIR_CONST, MIR_UNARY, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_FLOAT_CONST, MIR_LABEL, MIR_LABEL, MIR_PHI,
        MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_UNARY, MIR_RETURN
    };
    static const int parameter_opcodes[28] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_NOP,
        MIR_BRANCH_FALSE, MIR_CONST, MIR_UNARY, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_BRANCH_FALSE, MIR_CONST, MIR_UNARY,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_UNARY, MIR_LABEL,
        MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_LABEL, MIR_PHI,
        MIR_UNARY, MIR_RETURN
    };
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if ((mir.count != 26 && mir.count != 28) ||
        mir_cfg_block_count() != 9 || mir.has_vla ||
        type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_LONG ||
        type_size(mir.return_type) != 4)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            (mir.count == 26
                ? constant_opcodes[instruction]
                : parameter_opcodes[instruction]))
            return 0;
    if (mir.count == 26) {
        if (!mir_machine_parameter_value_offset(
                mir.insns[1].dst,
                &plan->first_condition_stack_offset) ||
            !mir_machine_parameter_value_offset(
                mir.insns[2].dst,
                &plan->second_condition_stack_offset) ||
            mir.insns[4].src1 != mir.insns[1].dst ||
            mir.insns[4].label != mir.insns[9].label ||
            !mir_machine_constant_equals(mir.insns[5].dst, 1) ||
            mir.insns[6].immediate != 0 ||
            mir.insns[6].src1 != mir.insns[5].dst ||
            !type_is_float(mir.insns[6].type) ||
            mir.insns[8].label != mir.insns[22].label ||
            mir.insns[11].src1 != mir.insns[2].dst ||
            mir.insns[11].label != mir.insns[16].label ||
            !mir_machine_constant_equals(mir.insns[12].dst, 2) ||
            mir.insns[13].immediate != 0 ||
            mir.insns[13].src1 != mir.insns[12].dst ||
            !type_is_float(mir.insns[13].type) ||
            mir.insns[15].label != mir.insns[19].label ||
            mir.insns[17].immediate != 1080033280L)
            return 0;
        if (mir.insns[20].src1 != mir.insns[13].dst ||
            mir.insns[20].src2 != mir.insns[17].dst ||
            mir.insns[20].phi_pred1 != mir.insns[14].label ||
            mir.insns[20].phi_pred2 != mir.insns[18].label ||
            mir.insns[23].src1 != mir.insns[6].dst ||
            mir.insns[23].src2 != mir.insns[20].dst ||
            mir.insns[23].phi_pred1 != mir.insns[7].label ||
            mir.insns[23].phi_pred2 != mir.insns[21].label ||
            mir.insns[24].immediate != 0 ||
            mir.insns[24].src1 != mir.insns[23].dst ||
            (mir.insns[24].type & 15) != TYPE_LONG ||
            mir.insns[25].src1 != mir.insns[24].dst)
            return 0;
        plan->first_value = 1;
        plan->second_value = 2;
        plan->third_value = 3;
        return 1;
    }
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst,
            &plan->first_condition_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst,
            &plan->second_condition_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[3].dst, &plan->third_stack_offset) ||
        mir.insns[5].src1 != mir.insns[1].dst ||
        mir.insns[5].label != mir.insns[10].label ||
        !mir_machine_constant_equals(mir.insns[6].dst, 1) ||
        mir.insns[7].immediate != 0 ||
        mir.insns[7].src1 != mir.insns[6].dst ||
        !type_is_float(mir.insns[7].type) ||
        mir.insns[9].label != mir.insns[24].label ||
        mir.insns[12].src1 != mir.insns[2].dst ||
        mir.insns[12].label != mir.insns[17].label ||
        !mir_machine_constant_equals(mir.insns[13].dst, 2) ||
        mir.insns[14].immediate != 0 ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        !type_is_float(mir.insns[14].type) ||
        mir.insns[16].label != mir.insns[21].label ||
        mir.insns[19].immediate != 0 ||
        mir.insns[19].src1 != mir.insns[3].dst ||
        !type_is_float(mir.insns[19].type))
        return 0;
    if (mir.insns[22].src1 != mir.insns[14].dst ||
        mir.insns[22].src2 != mir.insns[19].dst ||
        mir.insns[22].phi_pred1 != mir.insns[15].label ||
        mir.insns[22].phi_pred2 != mir.insns[20].label ||
        mir.insns[25].src1 != mir.insns[7].dst ||
        mir.insns[25].src2 != mir.insns[22].dst ||
        mir.insns[25].phi_pred1 != mir.insns[8].label ||
        mir.insns[25].phi_pred2 != mir.insns[23].label ||
        mir.insns[26].immediate != 0 ||
        mir.insns[26].src1 != mir.insns[25].dst ||
        (mir.insns[26].type & 15) != TYPE_LONG ||
        mir.insns[27].src1 != mir.insns[26].dst)
        return 0;
    plan->first_value = 1;
    plan->second_value = 2;
    plan->third_is_parameter = 1;
    return 1;
}

static int mir_match_float_int_truth(struct MirFloatIntTruth *plan)
{
    static const int and_opcodes[24] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_BRANCH_FALSE,
        MIR_NOP, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_CONST,
        MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_RETURN
    };
    static const int or_opcodes[32] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_PHI, MIR_BRANCH_FALSE, MIR_CONST, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_RETURN
    };
    int memory_offset;
    int memory_storage;
    int memory_type;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if ((mir.count != 24 && mir.count != 32) ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2 ||
        mir_cfg_block_count() != (mir.count == 24 ? 8 : 12))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            (mir.count == 24
                ? and_opcodes[instruction]
                : or_opcodes[instruction]))
            return 0;
    if (!mir_scalar_memory_location(
            &mir.insns[1], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_PARAM ||
        !type_is_float(memory_type) ||
        type_size(memory_type) != 4 ||
        memory_offset < 2 ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &plan->int_stack_offset))
        return 0;
    plan->float_stack_offset = memory_offset - 2;
    if (mir.count == 24) {
        if (mir.insns[4].src1 != mir.insns[1].dst ||
            mir.insns[4].label != mir.insns[10].label ||
            mir.insns[6].src1 != mir.insns[2].dst ||
            mir.insns[6].label != mir.insns[10].label ||
            !mir_machine_constant_equals(mir.insns[8].dst, 1) ||
            !mir_machine_constant_equals(mir.insns[11].dst, 0) ||
            mir.insns[13].src1 != mir.insns[8].dst ||
            mir.insns[13].src2 != mir.insns[11].dst ||
            mir.insns[14].src1 != mir.insns[13].dst ||
            mir.insns[14].label != mir.insns[18].label ||
            !mir_machine_constant_equals(mir.insns[15].dst, 1) ||
            !mir_machine_constant_equals(mir.insns[19].dst, 0) ||
            mir.insns[22].src1 != mir.insns[15].dst ||
            mir.insns[22].src2 != mir.insns[19].dst ||
            mir.insns[23].src1 != mir.insns[22].dst)
            return 0;
        plan->operation = '&';
        return 1;
    }
    if (mir.insns[4].src1 != mir.insns[1].dst ||
        mir.insns[4].label != mir.insns[8].label ||
        !mir_machine_constant_equals(mir.insns[6].dst, 1) ||
        mir.insns[10].src1 != mir.insns[2].dst ||
        mir.insns[10].label != mir.insns[14].label ||
        !mir_machine_constant_equals(mir.insns[12].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[15].dst, 0) ||
        mir.insns[17].src1 != mir.insns[12].dst ||
        mir.insns[17].src2 != mir.insns[15].dst ||
        mir.insns[21].src1 != mir.insns[6].dst ||
        mir.insns[21].src2 != mir.insns[17].dst ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        mir.insns[22].label != mir.insns[26].label ||
        !mir_machine_constant_equals(mir.insns[23].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[27].dst, 0) ||
        mir.insns[30].src1 != mir.insns[23].dst ||
        mir.insns[30].src2 != mir.insns[27].dst ||
        mir.insns[31].src1 != mir.insns[30].dst)
        return 0;
    plan->operation = '|';
    return 1;
}

static int mir_match_conditional_float_long(
    struct MirConditionalFloatLong *plan)
{
    const struct MirInsn *false_value;
    int memory_offset;
    int memory_storage;
    int memory_type;
    int instruction;
    long true_value;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 18 || mir_cfg_block_count() != 5 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_LONG ||
        type_size(mir.return_type) != 4 ||
        mir.insns[0].opcode != MIR_LABEL ||
        mir.insns[1].opcode != MIR_PARAM ||
        mir.insns[2].opcode != MIR_PARAM ||
        mir.insns[3].opcode != MIR_NOP ||
        mir.insns[4].opcode != MIR_BRANCH_FALSE ||
        mir.insns[5].opcode != MIR_CONST ||
        mir.insns[6].opcode != MIR_UNARY ||
        mir.insns[7].opcode != MIR_LABEL ||
        mir.insns[8].opcode != MIR_JUMP ||
        mir.insns[9].opcode != MIR_LABEL ||
        mir.insns[10].opcode != MIR_NOP ||
        mir.insns[13].opcode != MIR_LABEL ||
        mir.insns[14].opcode != MIR_LABEL ||
        mir.insns[15].opcode != MIR_PHI ||
        mir.insns[16].opcode != MIR_UNARY ||
        mir.insns[17].opcode != MIR_RETURN)
        return 0;
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->condition_stack_offset) ||
        mir.insns[4].src1 != mir.insns[1].dst ||
        mir.insns[4].label != mir.insns[9].label ||
        !mir_machine_constant_value(
            mir.insns[5].dst, &true_value, 0) ||
        true_value < -32768 || true_value > 65535 ||
        mir.insns[6].immediate != 0 ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        !type_is_float(mir.insns[6].type) ||
        mir.insns[8].label != mir.insns[14].label ||
        mir.insns[15].src1 != mir.insns[6].dst ||
        mir.insns[15].phi_pred1 != mir.insns[7].label ||
        mir.insns[15].phi_pred2 != mir.insns[13].label ||
        mir.insns[16].immediate != 0 ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        (mir.insns[16].type & 15) != TYPE_LONG ||
        mir.insns[17].src1 != mir.insns[16].dst)
        return 0;
    plan->true_value = (int)true_value;
    if (mir.insns[11].opcode == MIR_FLOAT_CONST &&
        mir.insns[12].opcode == MIR_BINARY) {
        if (!mir_scalar_memory_location(
                &mir.insns[2], &memory_type, &memory_storage,
                &memory_offset) ||
            memory_storage != SC_PARAM ||
            !type_is_float(memory_type) ||
            type_size(memory_type) != 4 ||
            memory_offset < 2 ||
            mir.insns[12].immediate != '+' ||
            mir.insns[12].src1 != mir.insns[2].dst ||
            mir.insns[12].src2 != mir.insns[11].dst ||
            !type_is_float(mir.insns[12].type))
            return 0;
        plan->kind = MIR_CONDITIONAL_FLOAT_ADD;
        plan->argument_stack_offset = memory_offset - 2;
        plan->add_bits = (unsigned long)mir.insns[11].immediate;
        false_value = &mir.insns[12];
    } else if (mir.insns[11].opcode == MIR_ARG &&
               mir.insns[12].opcode == MIR_CALL) {
        if (!mir_machine_parameter_value_offset(
                mir.insns[2].dst, &plan->argument_stack_offset) ||
            mir.insns[11].src1 != mir.insns[2].dst ||
            !type_is_float(mir.insns[12].type))
            return 0;
        plan->function = find_global(mir.insns[12].name);
        if (plan->function == NULL || !plan->function->is_defined ||
            plan->function->storage != SC_FUNC ||
            plan->function->is_funcptr ||
            plan->function->is_noreturn ||
            !plan->function->has_proto ||
            plan->function->proto_nargs != 1 ||
            plan->function->proto_variadic ||
            plan->function->proto_types[0] != mir.insns[11].type ||
            mir.insns[12].memory_flags != 0)
            return 0;
        plan->kind = MIR_CONDITIONAL_FLOAT_CALL;
        false_value = &mir.insns[12];
    } else {
        return 0;
    }
    if (mir.insns[15].src2 != false_value->dst)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_OPAQUE)
            return 0;
    return 1;
}

static int mir_match_conditional_pointer_float_long(
    struct MirConditionalPointerFloatLong *plan)
{
    static const int expected_opcodes[20] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_BRANCH_FALSE, MIR_ADDRESS,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_LABEL, MIR_LABEL,
        MIR_PHI, MIR_NOP, MIR_STORE, MIR_LOAD, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_RETURN
    };
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 20 || mir_cfg_block_count() != 5 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_LONG ||
        type_size(mir.return_type) != 4)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->condition_stack_offset) ||
        mir.insns[3].src1 != mir.insns[1].dst ||
        mir.insns[3].label != mir.insns[7].label ||
        type_ptr_depth(mir.insns[4].type) != 1 ||
        type_ptr_depth(mir.insns[8].type) != 1 ||
        mir.insns[6].label != mir.insns[10].label ||
        mir.insns[11].src1 != mir.insns[4].dst ||
        mir.insns[11].src2 != mir.insns[8].dst ||
        mir.insns[11].phi_pred1 != mir.insns[5].label ||
        mir.insns[11].phi_pred2 != mir.insns[9].label ||
        mir.insns[13].src1 != mir.insns[11].dst ||
        !mir_machine_same_location(&mir.insns[13], &mir.insns[14]) ||
        mir.insns[16].src1 != mir.insns[14].dst ||
        mir.insns[16].src2 != mir.insns[15].dst ||
        mir.insns[16].immediate != 4 ||
        mir.insns[16].memory_size != 4 ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        !type_is_float(mir.insns[17].type) ||
        mir.insns[17].memory_size != 4 ||
        (mir.insns[17].memory_flags & (1 | 8)) != 0 ||
        mir.insns[18].immediate != 0 ||
        mir.insns[18].src1 != mir.insns[17].dst ||
        (mir.insns[18].type & 15) != TYPE_LONG ||
        mir.insns[19].src1 != mir.insns[18].dst ||
        mir.insns[15].immediate < 0 ||
        mir.insns[15].immediate > 8191)
        return 0;
    plan->true_root = find_global(mir.insns[4].name);
    plan->false_pointer = find_global(mir.insns[8].name);
    if (plan->true_root == NULL || !plan->true_root->is_defined ||
        plan->true_root->is_volatile ||
        plan->false_pointer == NULL ||
        !plan->false_pointer->is_defined ||
        plan->false_pointer->is_volatile)
        return 0;
    plan->element_offset = (int)mir.insns[15].immediate * 4;
    return 1;
}

static int mir_match_nested_member_float_long(
    struct MirNestedMemberFloatLong *plan)
{
    static const int expected_opcodes[29] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_NOP,
        MIR_BRANCH_FALSE, MIR_CONST, MIR_UNARY, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_BRANCH_FALSE, MIR_CONST, MIR_UNARY,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_LABEL,
        MIR_LABEL, MIR_PHI, MIR_UNARY, MIR_RETURN
    };
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 29 || mir_cfg_block_count() != 9 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_LONG ||
        type_size(mir.return_type) != 4)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst,
            &plan->first_condition_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst,
            &plan->second_condition_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[3].dst, &plan->pointer_stack_offset) ||
        mir.insns[5].src1 != mir.insns[1].dst ||
        mir.insns[5].label != mir.insns[10].label ||
        !mir_machine_constant_equals(mir.insns[6].dst, 1) ||
        mir.insns[7].immediate != 0 ||
        !type_is_float(mir.insns[7].type) ||
        mir.insns[9].label != mir.insns[25].label ||
        mir.insns[12].src1 != mir.insns[2].dst ||
        mir.insns[12].label != mir.insns[17].label ||
        !mir_machine_constant_equals(mir.insns[13].dst, 2) ||
        mir.insns[14].immediate != 0 ||
        !type_is_float(mir.insns[14].type) ||
        mir.insns[16].label != mir.insns[22].label)
        return 0;
    if (mir.insns[19].src1 != mir.insns[3].dst ||
        mir.insns[19].immediate < 0 ||
        mir.insns[19].immediate > 32767 ||
        mir.insns[19].memory_size != 4 ||
        mir.insns[20].src1 != mir.insns[19].dst ||
        !type_is_float(mir.insns[20].type) ||
        mir.insns[20].memory_size != 4 ||
        (mir.insns[20].memory_flags & (1 | 8)) != 0 ||
        mir.insns[23].src1 != mir.insns[14].dst ||
        mir.insns[23].src2 != mir.insns[20].dst ||
        mir.insns[23].phi_pred1 != mir.insns[15].label ||
        mir.insns[23].phi_pred2 != mir.insns[21].label ||
        mir.insns[26].src1 != mir.insns[7].dst ||
        mir.insns[26].src2 != mir.insns[23].dst ||
        mir.insns[26].phi_pred1 != mir.insns[8].label ||
        mir.insns[26].phi_pred2 != mir.insns[24].label ||
        mir.insns[27].immediate != 0 ||
        (mir.insns[27].type & 15) != TYPE_LONG ||
        mir.insns[28].src1 != mir.insns[27].dst)
        return 0;
    plan->first_value = 1;
    plan->second_value = 2;
    plan->member_offset = (int)mir.insns[19].immediate;
    return 1;
}

static int mir_match_conditional_float_compare_long(
    struct MirConditionalFloatCompareLong *plan)
{
    static const int expected_opcodes[26] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_LABEL,
        MIR_PHI, MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_UNARY, MIR_RETURN
    };
    int memory_offset;
    int memory_storage;
    int memory_type;
    int instruction;
    long nonpositive_value;
    long positive_value;
    long true_value;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 26 || mir_cfg_block_count() != 9 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_LONG ||
        type_size(mir.return_type) != 4)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->condition_stack_offset) ||
        !mir_scalar_memory_location(
            &mir.insns[2], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_PARAM ||
        !type_is_float(memory_type) ||
        type_size(memory_type) != 4 ||
        memory_offset < 2 ||
        mir.insns[4].src1 != mir.insns[1].dst ||
        mir.insns[4].label != mir.insns[8].label ||
        !mir_machine_constant_value(
            mir.insns[5].dst, &true_value, 0) ||
        true_value < -32768 || true_value > 65535 ||
        mir.insns[7].label != mir.insns[22].label ||
        mir.insns[10].immediate != 0 ||
        mir.insns[11].immediate != '>' ||
        mir.insns[11].src1 != mir.insns[2].dst ||
        mir.insns[11].src2 != mir.insns[10].dst ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[12].label != mir.insns[16].label)
        return 0;
    if (!mir_machine_constant_value(
            mir.insns[13].dst, &positive_value, 0) ||
        positive_value < -32768 || positive_value > 65535 ||
        !mir_machine_constant_value(
            mir.insns[17].dst, &nonpositive_value, 0) ||
        nonpositive_value < -32768 || nonpositive_value > 65535 ||
        mir.insns[20].src1 != mir.insns[13].dst ||
        mir.insns[20].src2 != mir.insns[17].dst ||
        mir.insns[23].src1 != mir.insns[5].dst ||
        mir.insns[23].src2 != mir.insns[20].dst ||
        mir.insns[24].immediate != 0 ||
        (mir.insns[24].type & 15) != TYPE_LONG ||
        mir.insns[25].src1 != mir.insns[24].dst)
        return 0;
    plan->float_stack_offset = memory_offset - 2;
    plan->true_value = (int)true_value;
    plan->positive_value = (int)positive_value;
    plan->nonpositive_value = (int)nonpositive_value;
    return 1;
}

static int mir_match_conditional_bool(struct MirConditionalBool *plan)
{
    static const int expected_opcodes[14] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_BRANCH_FALSE, MIR_CONST,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_LABEL,
        MIR_PHI, MIR_UNARY, MIR_RETURN
    };
    long false_value;
    int instruction;
    long true_value;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 14 || mir_cfg_block_count() != 5 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        type_is_float(mir.return_type) ||
        (type_size(mir.return_type) != 1 &&
         type_size(mir.return_type) != 2))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->condition_stack_offset) ||
        mir.insns[3].src1 != mir.insns[1].dst ||
        mir.insns[3].label != mir.insns[7].label ||
        !mir_machine_constant_value(
            mir.insns[4].dst, &true_value, 0) ||
        mir.insns[6].label != mir.insns[10].label ||
        !mir_machine_constant_value(
            mir.insns[8].dst, &false_value, 0) ||
        mir.insns[11].src1 != mir.insns[4].dst ||
        mir.insns[11].src2 != mir.insns[8].dst ||
        mir.insns[11].phi_pred1 != mir.insns[5].label ||
        mir.insns[11].phi_pred2 != mir.insns[9].label ||
        mir.insns[12].immediate != 0 ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[12].type != mir.return_type ||
        mir.insns[13].src1 != mir.insns[12].dst)
        return 0;
    plan->result_type = mir.return_type;
    if ((mir.return_type & 15) == TYPE_BOOL) {
        plan->true_value = true_value != 0;
        plan->false_value = false_value != 0;
    } else {
        if (!mir_machine_convert_integer(
                true_value, mir.return_type, &true_value) ||
            !mir_machine_convert_integer(
                false_value, mir.return_type, &false_value))
            return 0;
        plan->true_value = (int)true_value;
        plan->false_value = (int)false_value;
    }
    return 1;
}

static int mir_match_logical_or_parameters(
    struct MirLogicalOrParameters *plan)
{
    static const int expected_opcodes[23] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_PHI, MIR_RETURN
    };
    const struct MirInsn *first = &mir.insns[1];
    const struct MirInsn *second = &mir.insns[2];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 23 || mir_cfg_block_count() != 8 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return mir_machine_reject(
            "logical-or-parameters", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "logical-or-parameters", "opcode");
    if (type_is_float(first->type) || type_size(first->type) != 2 ||
        type_is_float(second->type) || type_size(second->type) != 2 ||
        !mir_machine_parameter_value_offset(
            first->dst, &plan->first_stack_offset) ||
        !mir_machine_parameter_value_offset(
            second->dst, &plan->second_stack_offset) ||
        mir.insns[4].src1 != first->dst ||
        mir.insns[4].label != mir.insns[8].label ||
        !mir_machine_constant_equals(mir.insns[6].dst, 1) ||
        mir.insns[7].label != mir.insns[20].label ||
        mir.insns[10].src1 != second->dst ||
        mir.insns[10].label != mir.insns[14].label ||
        !mir_machine_constant_equals(mir.insns[12].dst, 1) ||
        mir.insns[13].label != mir.insns[16].label ||
        !mir_machine_constant_equals(mir.insns[15].dst, 0) ||
        mir.insns[17].src1 != mir.insns[12].dst ||
        mir.insns[17].src2 != mir.insns[15].dst ||
        mir.insns[17].phi_pred1 != mir.insns[11].label ||
        mir.insns[17].phi_pred2 != mir.insns[14].label ||
        mir.insns[19].label != mir.insns[20].label ||
        mir.insns[21].src1 != mir.insns[6].dst ||
        mir.insns[21].src2 != mir.insns[17].dst ||
        mir.insns[21].phi_pred1 != mir.insns[5].label ||
        mir.insns[21].phi_pred2 != mir.insns[18].label ||
        mir.insns[22].src1 != mir.insns[21].dst)
        return mir_machine_reject(
            "logical-or-parameters", "flow");
    return 1;
}

static int mir_match_cleared_record_append(
    struct MirClearedRecordAppend *plan)
{
    static const int expected_opcodes[46] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY,
        MIR_STORE_INDIRECT, MIR_NOP, MIR_STORE, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LOAD, MIR_NOP,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_ARG, MIR_LOAD,
        MIR_ARG, MIR_CALL, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_NOP,
        MIR_STORE_INDIRECT, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_NOP,
        MIR_STORE_INDIRECT, MIR_NOP, MIR_RETURN
    };
    int clear_arguments[3];
    int copy_arguments[2];
    int memory_offset;
    int memory_storage;
    int memory_type;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 46 || mir_cfg_block_count() != 1 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (!mir_scalar_memory_location(
            &mir.insns[4], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        type_size(memory_type) != 2 ||
        !mir_machine_named_nonvolatile(&mir.insns[4]) ||
        !mir_machine_same_location(&mir.insns[4], &mir.insns[12]) ||
        mir.insns[5].src1 != mir.insns[4].dst ||
        mir.insns[5].memory_size != 2 ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        mir.insns[6].memory_size != 2 ||
        !mir_machine_constant_equals(mir.insns[7].dst, 1) ||
        mir.insns[8].immediate != '+' ||
        mir.insns[8].src1 != mir.insns[6].dst ||
        mir.insns[8].src2 != mir.insns[7].dst ||
        mir.insns[9].src1 != mir.insns[5].dst ||
        mir.insns[9].src2 != mir.insns[8].dst ||
        mir.insns[9].memory_size != 2 ||
        !mir_machine_unobservable_local_store(&mir.insns[11]) ||
        mir.insns[11].src1 != mir.insns[6].dst)
        return 0;
    plan->root = find_global(mir.insns[4].name);
    if (plan->root == NULL || !plan->root->is_defined ||
        plan->root->is_volatile)
        return 0;
    plan->root_offset = memory_offset;
    plan->cursor_member_offset = (int)mir.insns[5].immediate;
    if (mir.insns[13].src1 != mir.insns[12].dst ||
        mir.insns[13].memory_size != 2 ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        mir.insns[14].memory_size != 2 ||
        mir.insns[17].immediate != '*' ||
        mir.insns[17].src1 != mir.insns[6].dst ||
        mir.insns[17].src2 != mir.insns[16].dst ||
        mir.insns[16].immediate <= 0 ||
        mir.insns[16].immediate > 32767 ||
        mir.insns[18].immediate != '+' ||
        mir.insns[18].src1 != mir.insns[14].dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[20]) ||
        mir.insns[20].src1 != mir.insns[18].dst)
        return 0;
    plan->array_member_offset = (int)mir.insns[13].immediate;
    plan->stride = (int)mir.insns[16].immediate;
    if (!mir_machine_same_location(&mir.insns[20], &mir.insns[21]) ||
        mir.insns[23].src1 != mir.insns[21].dst ||
        !mir_machine_constant_equals(mir.insns[24].dst, 0) ||
        mir.insns[25].src1 != mir.insns[24].dst ||
        mir.insns[26].immediate != plan->stride ||
        mir.insns[28].src1 != mir.insns[26].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[29], clear_arguments) ||
        clear_arguments[0] != mir.insns[21].dst ||
        clear_arguments[1] != mir.insns[24].dst ||
        clear_arguments[2] != mir.insns[26].dst)
        return 0;
    plan->clear_function = find_global(mir.insns[29].name);
    if (plan->clear_function == NULL ||
        plan->clear_function->storage != SC_FUNC ||
        plan->clear_function->is_funcptr ||
        plan->clear_function->is_noreturn ||
        (mir.insns[29].memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME |
          MIR_CALL_FLAG_INLINE_SUBSTITUTABLE)) != 0)
        return 0;
    if (!mir_machine_same_location(&mir.insns[20], &mir.insns[30]) ||
        mir.insns[31].src1 != mir.insns[30].dst ||
        mir.insns[31].memory_size != 18 ||
        mir.insns[32].src1 != mir.insns[31].dst ||
        !mir_machine_same_location(&mir.insns[1], &mir.insns[33]) ||
        mir.insns[34].src1 != mir.insns[33].dst ||
        !mir_machine_two_call_arguments(
            &mir.insns[35], copy_arguments) ||
        copy_arguments[0] != mir.insns[31].dst ||
        copy_arguments[1] != mir.insns[33].dst)
        return 0;
    plan->copy_function = find_global(mir.insns[35].name);
    if (plan->copy_function == NULL ||
        plan->copy_function->storage != SC_FUNC ||
        plan->copy_function->is_funcptr ||
        plan->copy_function->is_noreturn ||
        (mir.insns[35].memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME |
          MIR_CALL_FLAG_INLINE_SUBSTITUTABLE)) != 0)
        return 0;
    if (!mir_machine_same_location(&mir.insns[20], &mir.insns[36]) ||
        mir.insns[37].src1 != mir.insns[36].dst ||
        mir.insns[37].memory_size != 2 ||
        mir.insns[39].src1 != mir.insns[37].dst ||
        mir.insns[39].src2 != mir.insns[2].dst ||
        mir.insns[39].memory_size != 2 ||
        !mir_machine_same_location(&mir.insns[20], &mir.insns[40]) ||
        mir.insns[41].src1 != mir.insns[40].dst ||
        mir.insns[41].memory_size != 2 ||
        mir.insns[43].src1 != mir.insns[41].dst ||
        mir.insns[43].src2 != mir.insns[3].dst ||
        mir.insns[43].memory_size != 2 ||
        mir.insns[45].src1 != mir.insns[6].dst)
        return 0;
    plan->name_field_offset = (int)mir.insns[31].immediate;
    plan->kind_field_offset = (int)mir.insns[37].immediate;
    plan->value_field_offset = (int)mir.insns[41].immediate;
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->name_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &plan->kind_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[3].dst, &plan->value_stack_offset) ||
        plan->name_field_offset != 0 ||
        plan->kind_field_offset < 0 ||
        plan->value_field_offset < 0)
        return 0;
    return 1;
}

static int mir_match_record_name_search(
    struct MirRecordNameSearch *plan)
{
    static const int expected_opcodes[43] = {
        MIR_LABEL, MIR_PARAM, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_LOAD, MIR_PHI, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BINARY, MIR_MEMBER_ADDRESS,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_UNARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_RETURN, MIR_LABEL, MIR_LABEL,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_RETURN
    };
    int arguments[2];
    int memory_offset;
    int memory_storage;
    int memory_type;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 43 || mir_cfg_block_count() != 5 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->name_stack_offset) ||
        !mir_scalar_memory_location(
            &mir.insns[2], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        type_size(memory_type) != 2 ||
        !mir_machine_named_nonvolatile(&mir.insns[2]) ||
        !mir_machine_same_location(&mir.insns[2], &mir.insns[16]) ||
        mir.insns[3].src1 != mir.insns[2].dst ||
        mir.insns[4].src1 != mir.insns[3].dst ||
        mir.insns[4].memory_size != 2 ||
        !mir_machine_constant_equals(mir.insns[5].dst, 1) ||
        mir.insns[6].immediate != '-' ||
        mir.insns[6].src1 != mir.insns[4].dst ||
        mir.insns[6].src2 != mir.insns[5].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[8]) ||
        mir.insns[8].src1 != mir.insns[6].dst)
        return 0;
    plan->root = find_global(mir.insns[2].name);
    if (plan->root == NULL || !plan->root->is_defined ||
        plan->root->is_volatile)
        return 0;
    plan->root_offset = memory_offset;
    plan->cursor_member_offset = (int)mir.insns[3].immediate;
    if (!mir_machine_same_location(&mir.insns[1], &mir.insns[10]) ||
        mir.insns[11].src1 != mir.insns[6].dst ||
        mir.insns[11].src2 != mir.insns[36].dst ||
        mir.insns[11].phi_pred1 != mir.insns[0].label ||
        mir.insns[11].phi_pred2 != mir.insns[33].label ||
        !mir_machine_constant_equals(mir.insns[13].dst, 0) ||
        mir.insns[14].immediate != TOK_GE ||
        mir.insns[14].src1 != mir.insns[11].dst ||
        mir.insns[14].src2 != mir.insns[13].dst ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        mir.insns[15].label != mir.insns[39].label)
        return 0;
    if (mir.insns[17].src1 != mir.insns[16].dst ||
        mir.insns[18].src1 != mir.insns[17].dst ||
        mir.insns[18].memory_size != 2 ||
        mir.insns[21].immediate != '*' ||
        mir.insns[21].src1 != mir.insns[11].dst ||
        mir.insns[21].src2 != mir.insns[20].dst ||
        mir.insns[20].immediate <= 0 ||
        mir.insns[20].immediate > 32767 ||
        mir.insns[22].immediate != '+' ||
        mir.insns[22].src1 != mir.insns[18].dst ||
        mir.insns[22].src2 != mir.insns[21].dst ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[23].memory_size <= 0 ||
        mir.insns[24].src1 != mir.insns[23].dst ||
        !mir_machine_same_location(&mir.insns[1], &mir.insns[25]) ||
        mir.insns[26].src1 != mir.insns[25].dst ||
        !mir_machine_two_call_arguments(
            &mir.insns[27], arguments) ||
        arguments[0] != mir.insns[23].dst ||
        arguments[1] != mir.insns[25].dst)
        return 0;
    plan->array_member_offset = (int)mir.insns[17].immediate;
    plan->stride = (int)mir.insns[20].immediate;
    plan->name_field_offset = (int)mir.insns[23].immediate;
    plan->compare_function = find_global(mir.insns[27].name);
    if (plan->compare_function == NULL ||
        plan->compare_function->storage != SC_FUNC ||
        plan->compare_function->is_funcptr ||
        plan->compare_function->is_noreturn ||
        (mir.insns[27].memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME |
          MIR_CALL_FLAG_INLINE_SUBSTITUTABLE)) != 0)
        return 0;
    if (mir.insns[28].immediate != '!' ||
        mir.insns[28].src1 != mir.insns[27].dst ||
        mir.insns[29].src1 != mir.insns[28].dst ||
        mir.insns[29].label != mir.insns[32].label ||
        mir.insns[31].src1 != mir.insns[11].dst ||
        !mir_machine_constant_equals(mir.insns[35].dst, 1) ||
        mir.insns[36].immediate != '-' ||
        mir.insns[36].src1 != mir.insns[11].dst ||
        mir.insns[36].src2 != mir.insns[35].dst ||
        !mir_machine_same_location(&mir.insns[8], &mir.insns[37]) ||
        mir.insns[37].src1 != mir.insns[36].dst ||
        mir.insns[38].label != mir.insns[9].label ||
        mir.insns[41].immediate != 65535 ||
        mir.insns[42].src1 != mir.insns[41].dst)
        return 0;
    return 1;
}

static int mir_match_sequential_unary_reports(
    struct MirSequentialUnaryReports *plan)
{
    int arguments[6];
    int parameter;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 29 || mir_cfg_block_count() != 1 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID ||
        mir.insns[0].opcode != MIR_LABEL ||
        mir.insns[6].opcode != MIR_STRING_ADDRESS ||
        mir.insns[7].opcode != MIR_ARG ||
        mir.insns[7].src1 != mir.insns[6].dst ||
        mir.insns[28].opcode != MIR_CALL)
        return 0;
    for (parameter = 0; parameter < 5; ++parameter) {
        int base = 1 + parameter;
        int call_base = 8 + parameter * 4;
        struct Sym *helper;

        if (mir.insns[base].opcode != MIR_PARAM ||
            (mir.insns[base].type & 15) != TYPE_BOOL ||
            !mir_machine_parameter_value_offset(
                mir.insns[base].dst,
                &plan->parameter_stack_offsets[parameter]) ||
            mir.insns[call_base].opcode != MIR_NOP ||
            mir.insns[call_base + 1].opcode != MIR_ARG ||
            mir.insns[call_base + 1].src1 != mir.insns[base].dst ||
            mir.insns[call_base + 2].opcode != MIR_CALL ||
            !mir_call_uses_value(
                &mir.insns[call_base + 2],
                mir.insns[base].dst) ||
            mir.insns[call_base + 3].opcode != MIR_ARG ||
            mir.insns[call_base + 3].src1 !=
                mir.insns[call_base + 2].dst)
            return 0;
        helper = find_global(mir.insns[call_base + 2].name);
        if (helper == NULL || !helper->is_defined ||
            helper->storage != SC_FUNC ||
            helper->is_funcptr || helper->is_noreturn ||
            !helper->has_proto || helper->proto_nargs != 1 ||
            helper->proto_variadic ||
            helper->proto_types[0] !=
                mir.insns[call_base + 1].type ||
            (helper->type & 15) != TYPE_CHAR ||
            mir.insns[call_base + 2].memory_flags != 0)
            return 0;
        if (parameter == 0)
            plan->helper = helper;
        else if (plan->helper != helper)
            return 0;
    }
    if (!mir_machine_six_call_arguments(
            &mir.insns[28], arguments) ||
        arguments[0] != mir.insns[6].dst)
        return 0;
    for (parameter = 0; parameter < 5; ++parameter)
        if (arguments[parameter + 1] !=
            mir.insns[10 + parameter * 4].dst)
            return 0;
    plan->print_function = find_global(mir.insns[28].name);
    if (plan->print_function == NULL ||
        strcmp(mir.insns[28].name, "printf") ||
        (mir.insns[28].memory_flags & MIR_CALL_FLAG_VARIADIC) == 0 ||
        (mir.insns[28].memory_flags &
         MIR_CALL_FLAG_FORMAT_RUNTIME) != 0)
        return 0;
    plan->string_id = (int)mir.insns[6].immediate;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_OPAQUE)
            return 0;
    return 1;
}

static int mir_match_nibble_append(struct MirNibbleAppend *plan)
{
    static const int expected_opcodes[32] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_CONST, MIR_BINARY, MIR_LABEL, MIR_LABEL, MIR_PHI,
        MIR_UNARY, MIR_STORE_INDIRECT, MIR_LOAD, MIR_RETURN
    };
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 32 || mir_cfg_block_count() != 5 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 1 ||
        (mir.return_type & 15) != TYPE_CHAR)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->pointer_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &plan->value_stack_offset) ||
        !mir_machine_same_location(&mir.insns[1], &mir.insns[3]) ||
        mir.insns[5].immediate != '+' ||
        mir.insns[5].src1 != mir.insns[3].dst ||
        mir.insns[5].src2 != mir.insns[4].dst ||
        !mir_machine_constant_equals(mir.insns[4].dst, 1) ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        !mir_machine_same_location(&mir.insns[3], &mir.insns[6]) ||
        mir.insns[9].immediate != 0 ||
        mir.insns[9].src1 != mir.insns[2].dst ||
        mir.insns[10].immediate != TOK_LE ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[10].src2 != mir.insns[8].dst ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[11].label != mir.insns[18].label)
        return 0;
    if (mir.insns[14].immediate != 0 ||
        mir.insns[14].src1 != mir.insns[2].dst ||
        mir.insns[15].immediate != '+' ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        mir.insns[15].src2 != mir.insns[13].dst ||
        mir.insns[17].label != mir.insns[26].label ||
        mir.insns[21].immediate != 0 ||
        mir.insns[21].src1 != mir.insns[2].dst ||
        mir.insns[22].immediate != '-' ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        mir.insns[22].src2 != mir.insns[20].dst ||
        mir.insns[24].immediate != '+' ||
        mir.insns[24].src1 != mir.insns[22].dst ||
        mir.insns[24].src2 != mir.insns[23].dst ||
        mir.insns[27].src1 != mir.insns[15].dst ||
        mir.insns[27].src2 != mir.insns[24].dst ||
        mir.insns[28].immediate != 0 ||
        mir.insns[28].src1 != mir.insns[27].dst ||
        type_size(mir.insns[28].type) != 1 ||
        mir.insns[29].src1 != mir.insns[3].dst ||
        mir.insns[29].src2 != mir.insns[28].dst ||
        mir.insns[29].memory_size != 1 ||
        (mir.insns[29].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_same_location(&mir.insns[6], &mir.insns[30]) ||
        mir.insns[31].src1 != mir.insns[30].dst)
        return 0;
    plan->threshold = (int)mir.insns[8].immediate + 1;
    plan->low_adjustment = (int)mir.insns[13].immediate;
    plan->high_adjustment =
        (int)mir.insns[23].immediate - (int)mir.insns[20].immediate;
    if (plan->threshold <= 0 || plan->threshold > 256 ||
        plan->low_adjustment < -255 || plan->low_adjustment > 255 ||
        plan->high_adjustment < -255 || plan->high_adjustment > 255)
        return 0;
    return 1;
}

static int mir_match_dead_constant_float_check(void)
{
    static const int expected_opcodes[24] = {
        MIR_LABEL, MIR_LABEL, MIR_FLOAT_CONST, MIR_FLOAT_CONST,
        MIR_BINARY, MIR_UNARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_NOP, MIR_LABEL, MIR_NOP,
        MIR_LABEL, MIR_CONST, MIR_BRANCH_FALSE, MIR_JUMP, MIR_LABEL
    };
    uint32_t left_bits;
    uint32_t right_bits;
    float left;
    float right;
    int comparison;
    int instruction;

    if (mir.count != 24 || mir_cfg_block_count() != 5 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (!type_is_float(mir.insns[2].type) ||
        mir.insns[3].type != mir.insns[2].type ||
        mir.insns[4].src1 != mir.insns[2].dst ||
        mir.insns[4].src2 != mir.insns[3].dst ||
        (mir.insns[4].immediate != TOK_EQ &&
         mir.insns[4].immediate != TOK_NE &&
         mir.insns[4].immediate != '<' &&
         mir.insns[4].immediate != '>' &&
         mir.insns[4].immediate != TOK_LE &&
         mir.insns[4].immediate != TOK_GE) ||
        mir.insns[5].immediate != '!' ||
        mir.insns[5].src1 != mir.insns[4].dst ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        mir_find_label(mir.insns[6].label) != 17 ||
        !mir_machine_constant_equals(mir.insns[20].dst, 0) ||
        mir.insns[21].src1 != mir.insns[20].dst ||
        mir_find_label(mir.insns[21].label) != 23 ||
        mir.insns[22].label != mir.insns[1].label)
        return 0;
    left_bits = (uint32_t)mir.insns[2].immediate;
    right_bits = (uint32_t)mir.insns[3].immediate;
    memcpy(&left, &left_bits, sizeof(left));
    memcpy(&right, &right_bits, sizeof(right));
    switch (mir.insns[4].immediate) {
    case TOK_EQ: comparison = left == right; break;
    case TOK_NE: comparison = left != right; break;
    case '<': comparison = left < right; break;
    case '>': comparison = left > right; break;
    case TOK_LE: comparison = left <= right; break;
    case TOK_GE: comparison = left >= right; break;
    default: return 0;
    }
    if (!comparison)
        return 0;
    return 1;
}

static int mir_match_volatile_fill_wide_constant(
    struct MirVolatileFillWideConstant *plan)
{
    static const int expected_opcodes[78] = {
        MIR_LABEL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_PHI,
        MIR_NOP, MIR_CONST, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP, MIR_UNARY,
        MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_NOP, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_CONST, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_NOP, MIR_RETURN, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST, MIR_NOP, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_RETURN, MIR_LABEL, MIR_CONST,
        MIR_NOP, MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST, MIR_NOP, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_RETURN, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP, MIR_RETURN
    };
    int declared;
    int found_volatile = 0;
    int memory_offset;
    int memory_storage;
    int memory_type;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 78 || mir_cfg_block_count() != 7 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_LONG ||
        (mir.return_type & TYPE_UNSIGNED) == 0 ||
        type_size(mir.return_type) != 4)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (!mir_scalar_memory_location(
            &mir.insns[11], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_LOCAL ||
        type_ptr_depth(mir.insns[11].type) != 1 ||
        (mir.insns[11].type & 15) != TYPE_CHAR ||
        memory_offset >= 0 ||
        !mir_machine_constant_equals(mir.insns[1].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[3]) ||
        mir.insns[5].src1 != mir.insns[1].dst ||
        mir.insns[5].src2 != mir.insns[20].dst ||
        mir.insns[5].phi_pred1 != mir.insns[0].label ||
        mir.insns[5].phi_pred2 != mir.insns[17].label ||
        mir.insns[7].immediate <= 0 ||
        mir.insns[7].immediate > 255 ||
        mir.insns[9].immediate != '<' ||
        mir.insns[9].src1 != mir.insns[5].dst ||
        mir.insns[9].src2 != mir.insns[7].dst ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[10].label != mir.insns[23].label)
        return 0;
    for (declared = 0; declared < mir.declared_count; ++declared)
        if (!strcmp(mir.declared_names[declared], mir.insns[11].name)) {
            found_volatile = mir.declared_is_volatile[declared];
            break;
        }
    if (!found_volatile ||
        mir.insns[13].src1 != mir.insns[11].dst ||
        mir.insns[13].src2 != mir.insns[5].dst ||
        mir.insns[13].immediate != 1 ||
        mir.insns[15].immediate != 0 ||
        mir.insns[15].src1 != mir.insns[5].dst ||
        mir.insns[16].src1 != mir.insns[13].dst ||
        mir.insns[16].src2 != mir.insns[15].dst ||
        mir.insns[16].memory_size != 1 ||
        !mir_machine_constant_equals(mir.insns[19].dst, 1) ||
        mir.insns[20].immediate != '+' ||
        mir.insns[20].src1 != mir.insns[5].dst ||
        mir.insns[20].src2 != mir.insns[19].dst ||
        !mir_machine_same_location(&mir.insns[3], &mir.insns[21]) ||
        mir.insns[22].label != mir.insns[4].label)
        return 0;
    if (mir.insns[24].immediate != 0x12345678L ||
        mir.insns[30].immediate != TOK_SHR ||
        mir.insns[30].src1 != mir.insns[24].dst ||
        !mir_machine_constant_equals(mir.insns[29].dst, 8) ||
        mir.insns[34].immediate != 0x00123456L ||
        mir.insns[36].immediate != TOK_NE ||
        mir.insns[36].src1 != mir.insns[30].dst ||
        mir.insns[36].src2 != mir.insns[34].dst ||
        mir.insns[37].label != mir.insns[40].label ||
        mir.insns[43].immediate != TOK_SHR ||
        mir.insns[43].src1 != mir.insns[30].dst ||
        !mir_machine_constant_equals(mir.insns[42].dst, 8) ||
        mir.insns[47].immediate != 0x1234L ||
        mir.insns[49].immediate != TOK_NE ||
        mir.insns[49].src1 != mir.insns[43].dst ||
        mir.insns[49].src2 != mir.insns[47].dst ||
        mir.insns[50].label != mir.insns[53].label)
        return 0;
    if (mir.insns[54].immediate != 49 ||
        mir.insns[60].immediate != TOK_SHL ||
        mir.insns[60].src1 != mir.insns[54].dst ||
        !mir_machine_constant_equals(mir.insns[59].dst, 6) ||
        mir.insns[64].immediate != 3136 ||
        mir.insns[66].immediate != TOK_NE ||
        mir.insns[66].src1 != mir.insns[60].dst ||
        mir.insns[66].src2 != mir.insns[64].dst ||
        mir.insns[67].label != mir.insns[70].label ||
        mir.insns[73].immediate != TOK_SHR ||
        mir.insns[73].src1 != mir.insns[60].dst ||
        !mir_machine_constant_equals(mir.insns[72].dst, 8) ||
        mir.insns[77].src1 != mir.insns[73].dst)
        return 0;
    plan->buffer_offset = memory_offset;
    plan->count = (int)mir.insns[7].immediate;
    plan->result = 12;
    if (-plan->buffer_offset < plan->count)
        return 0;
    return 1;
}

static int mir_match_single_signed_div_check(
    struct MirSingleSignedDivCheck *plan)
{
    const struct MirInsn *call;
    const struct MirInsn *label_load;
    const struct MirInsn *result;
    const struct MirInsn *other;
    int arguments[3];
    int expected_parameter;
    int instruction;
    int label_parameter;

    memset(plan, 0, sizeof(*plan));
    if ((mir.count != 23 && mir.count != 22) ||
        mir_cfg_block_count() != 1 || mir.has_vla ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    if (mir.count == 23) {
        result = &mir.insns[8];
        other = &mir.insns[13];
        expected_parameter = 3;
        label_parameter = 5;
        label_load = &mir.insns[20];
        call = &mir.insns[22];
    } else {
        result = &mir.insns[12];
        other = &mir.insns[7];
        expected_parameter = 3;
        label_parameter = 4;
        label_load = &mir.insns[19];
        call = &mir.insns[21];
    }
    if (mir.insns[0].opcode != MIR_LABEL ||
        mir.insns[1].opcode != MIR_PARAM ||
        mir.insns[2].opcode != MIR_PARAM ||
        result->opcode != MIR_BINARY ||
        other->opcode != MIR_BINARY ||
        (result->immediate != '%' && result->immediate != '/') ||
        (other->immediate != '%' && other->immediate != '/') ||
        result->immediate == other->immediate ||
        result->src1 != mir.insns[1].dst ||
        result->src2 != mir.insns[2].dst ||
        other->src1 != mir.insns[1].dst ||
        other->src2 != mir.insns[2].dst ||
        type_ptr_depth(result->type) != 0 ||
        (result->type & 15) != TYPE_INT ||
        (result->type & TYPE_UNSIGNED) != 0 ||
        label_load->opcode != MIR_LOAD ||
        !mir_machine_same_location(
            &mir.insns[label_parameter], label_load) ||
        call->opcode != MIR_CALL ||
        !mir_machine_three_call_arguments(call, arguments) ||
        arguments[0] != result->dst ||
        arguments[1] != mir.insns[expected_parameter].dst ||
        arguments[2] != label_load->dst)
        return 0;
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->numerator_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &plan->denominator_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[expected_parameter].dst,
            &plan->expected_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[label_parameter].dst,
            &plan->label_stack_offset))
        return 0;
    plan->check_function = find_global(call->name);
    if (plan->check_function == NULL ||
        !plan->check_function->is_defined ||
        plan->check_function->storage != SC_FUNC ||
        plan->check_function->is_funcptr ||
        plan->check_function->is_noreturn ||
        !plan->check_function->has_proto ||
        plan->check_function->proto_nargs != 3 ||
        plan->check_function->proto_variadic ||
        call->memory_flags != 0)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_OPAQUE)
            return 0;
    plan->operation = (int)result->immediate;
    return 1;
}

static int mir_match_wide_div_result_check(
    struct MirWideDivResultCheck *plan)
{
    static const int expected_opcodes[73] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_PARAM,
        MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL_AGGREGATE,
        MIR_NOP, MIR_NOP, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LOAD, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_LABEL, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_ARG,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG,
        MIR_NOP, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_BINARY,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY,
        MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_ARG,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP,
        MIR_BINARY, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_BINARY, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL, MIR_LABEL
    };
    const struct MirInsn *divide_call = &mir.insns[10];
    const struct MirInsn *first_failure = &mir.insns[27];
    const struct MirInsn *second_failure = &mir.insns[43];
    const struct MirInsn *third_failure = &mir.insns[71];
    int divide_arguments[2];
    int failure_arguments[3];
    int instruction;
    int parameter;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 73 || mir_cfg_block_count() != 4 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;

    if (type_ptr_depth(mir.insns[1].type) != 1 ||
        type_size(mir.insns[1].type) != 2)
        return mir_machine_reject(
            "wide-div-result-check", "name-parameter");
    for (parameter = 2; parameter <= 5; ++parameter)
        if (type_ptr_depth(mir.insns[parameter].type) != 0 ||
            (mir.insns[parameter].type & 15) != TYPE_LONG ||
            (mir.insns[parameter].type & TYPE_UNSIGNED) != 0 ||
            type_is_float(mir.insns[parameter].type) ||
            type_size(mir.insns[parameter].type) != 4)
            return mir_machine_reject(
                "wide-div-result-check", "wide-parameters");
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->name_stack_offset) ||
        !mir_match_wide_parameter_offset(
            &mir.insns[2], &plan->numerator_stack_offset) ||
        !mir_match_wide_parameter_offset(
            &mir.insns[3], &plan->denominator_stack_offset) ||
        !mir_match_wide_parameter_offset(
            &mir.insns[4],
            &plan->expected_quotient_stack_offset) ||
        !mir_match_wide_parameter_offset(
            &mir.insns[5],
            &plan->expected_remainder_stack_offset) ||
        plan->name_stack_offset != 2 ||
        plan->numerator_stack_offset != 4 ||
        plan->denominator_stack_offset != 8 ||
        plan->expected_quotient_stack_offset != 12 ||
        plan->expected_remainder_stack_offset != 16)
        return mir_machine_reject(
            "wide-div-result-check", "parameter-abi");

    if (!mir_machine_two_call_arguments(
            divide_call, divide_arguments) ||
        divide_arguments[0] != mir.insns[2].dst ||
        divide_arguments[1] != mir.insns[3].dst ||
        divide_call->memory_size != 8 ||
        divide_call->memory_flags != 0)
        return mir_machine_reject(
            "wide-div-result-check", "divide-call");
    plan->divide_function = find_global(divide_call->name);
    if (plan->divide_function == NULL ||
        plan->divide_function->is_funcptr ||
        plan->divide_function->is_noreturn ||
        !plan->divide_function->has_proto ||
        plan->divide_function->proto_nargs != 2 ||
        plan->divide_function->proto_variadic ||
        type_ptr_depth(plan->divide_function->proto_types[0]) != 0 ||
        (plan->divide_function->proto_types[0] & 15) != TYPE_LONG ||
        (plan->divide_function->proto_types[0] & TYPE_UNSIGNED) != 0 ||
        type_size(plan->divide_function->proto_types[0]) != 4 ||
        type_ptr_depth(plan->divide_function->proto_types[1]) != 0 ||
        (plan->divide_function->proto_types[1] & 15) != TYPE_LONG ||
        (plan->divide_function->proto_types[1] & TYPE_UNSIGNED) != 0 ||
        type_size(plan->divide_function->proto_types[1]) != 4)
        return mir_machine_reject(
            "wide-div-result-check", "divide-function");

    if (!mir_match_wide_div_result_access(13, 0, NULL) ||
        !mir_match_wide_div_result_access(21, 0, &mir.insns[13]) ||
        !mir_match_wide_div_result_access(29, 4, &mir.insns[13]) ||
        !mir_match_wide_div_result_access(37, 4, &mir.insns[13]) ||
        !mir_match_wide_div_result_access(45, 0, &mir.insns[13]) ||
        !mir_match_wide_div_result_access(50, 4, &mir.insns[13]) ||
        !mir_match_wide_div_result_access(59, 0, &mir.insns[13]) ||
        !mir_match_wide_div_result_access(64, 4, &mir.insns[13]))
        return mir_machine_reject(
            "wide-div-result-check", "result-layout");
    plan->result_offset = -8;

    if (mir.insns[17].immediate != TOK_NE ||
        mir.insns[17].src1 != mir.insns[15].dst ||
        mir.insns[17].src2 != mir.insns[4].dst ||
        mir.insns[17].secondary_offset != 4 ||
        mir.insns[18].src1 != mir.insns[17].dst ||
        mir.insns[18].label != mir.insns[28].label ||
        !mir_machine_same_location(&mir.insns[1], &mir.insns[19]) ||
        !mir_machine_three_call_arguments(
            first_failure, failure_arguments) ||
        failure_arguments[0] != mir.insns[19].dst ||
        failure_arguments[1] != mir.insns[23].dst ||
        failure_arguments[2] != mir.insns[4].dst)
        return mir_machine_reject(
            "wide-div-result-check", "quotient-check");

    plan->failure_function = find_global(first_failure->name);
    if (plan->failure_function == NULL ||
        !plan->failure_function->is_defined ||
        plan->failure_function->storage != SC_FUNC ||
        plan->failure_function->is_funcptr ||
        plan->failure_function->is_noreturn ||
        !plan->failure_function->has_proto ||
        plan->failure_function->proto_nargs != 3 ||
        plan->failure_function->proto_variadic ||
        type_ptr_depth(plan->failure_function->proto_types[0]) != 1 ||
        type_size(plan->failure_function->proto_types[0]) != 2 ||
        type_ptr_depth(plan->failure_function->proto_types[1]) != 0 ||
        (plan->failure_function->proto_types[1] & 15) != TYPE_LONG ||
        (plan->failure_function->proto_types[1] & TYPE_UNSIGNED) != 0 ||
        type_size(plan->failure_function->proto_types[1]) != 4 ||
        type_ptr_depth(plan->failure_function->proto_types[2]) != 0 ||
        (plan->failure_function->proto_types[2] & 15) != TYPE_LONG ||
        (plan->failure_function->proto_types[2] & TYPE_UNSIGNED) != 0 ||
        type_size(plan->failure_function->proto_types[2]) != 4 ||
        (first_failure->type & 15) != TYPE_VOID ||
        first_failure->memory_flags != 0)
        return mir_machine_reject(
            "wide-div-result-check", "failure-function");

    if (mir.insns[33].immediate != TOK_NE ||
        mir.insns[33].src1 != mir.insns[31].dst ||
        mir.insns[33].src2 != mir.insns[5].dst ||
        mir.insns[33].secondary_offset != 4 ||
        mir.insns[34].src1 != mir.insns[33].dst ||
        mir.insns[34].label != mir.insns[44].label ||
        !mir_machine_same_location(&mir.insns[1], &mir.insns[35]) ||
        !mir_machine_three_call_arguments(
            second_failure, failure_arguments) ||
        failure_arguments[0] != mir.insns[35].dst ||
        failure_arguments[1] != mir.insns[39].dst ||
        failure_arguments[2] != mir.insns[5].dst ||
        find_global(second_failure->name) != plan->failure_function ||
        (second_failure->type & 15) != TYPE_VOID ||
        second_failure->memory_flags != 0)
        return mir_machine_reject(
            "wide-div-result-check", "remainder-check");

    if (mir.insns[49].immediate != '*' ||
        mir.insns[49].src1 != mir.insns[47].dst ||
        mir.insns[49].src2 != mir.insns[3].dst ||
        mir.insns[53].immediate != '+' ||
        mir.insns[53].src1 != mir.insns[49].dst ||
        mir.insns[53].src2 != mir.insns[52].dst ||
        mir.insns[55].immediate != TOK_NE ||
        mir.insns[55].src1 != mir.insns[53].dst ||
        mir.insns[55].src2 != mir.insns[2].dst ||
        mir.insns[55].secondary_offset != 4 ||
        mir.insns[56].src1 != mir.insns[55].dst ||
        mir.insns[56].label != mir.insns[72].label ||
        !mir_machine_same_location(&mir.insns[1], &mir.insns[57]) ||
        mir.insns[63].immediate != '*' ||
        mir.insns[63].src1 != mir.insns[61].dst ||
        mir.insns[63].src2 != mir.insns[3].dst ||
        mir.insns[67].immediate != '+' ||
        mir.insns[67].src1 != mir.insns[63].dst ||
        mir.insns[67].src2 != mir.insns[66].dst ||
        !mir_machine_three_call_arguments(
            third_failure, failure_arguments) ||
        failure_arguments[0] != mir.insns[57].dst ||
        failure_arguments[1] != mir.insns[67].dst ||
        failure_arguments[2] != mir.insns[2].dst ||
        find_global(third_failure->name) != plan->failure_function ||
        (third_failure->type & 15) != TYPE_VOID ||
        third_failure->memory_flags != 0)
        return mir_machine_reject(
            "wide-div-result-check", "identity-check");

    return 1;
}

static int mir_match_final_call_check_schedule(
    struct MirFinalCallCheckSchedule *plan)
{
    static const int group_counts[6] = {4, 4, 5, 5, 10, 15};
    static const int group_widths[6] = {2, 4, 2, 4, 2, 4};
    static const int group_kinds[6] = {
        MIR_FINAL_CALL_NESTED_CONSTANT,
        MIR_FINAL_CALL_NESTED_CONSTANT,
        MIR_FINAL_CALL_DIRECT,
        MIR_FINAL_CALL_DIRECT,
        MIR_FINAL_CALL_NESTED_STRING,
        MIR_FINAL_CALL_NESTED_STRING
    };
    const struct MirInsn *final_load;
    const struct MirInsn *final_call;
    struct Sym *value_functions[4] = {NULL, NULL, NULL, NULL};
    struct Sym *check_functions[4] = {NULL, NULL, NULL, NULL};
    int call_indices[77];
    int opcode_counts[MIR_RETURN + 1];
    int call_count = 0;
    int check = 0;
    int group;
    int instruction;
    int call_position = 0;
    int memory_offset;
    int memory_storage;
    int memory_type;
    int print_argument;

    memset(plan, 0, sizeof(*plan));
    memset(opcode_counts, 0, sizeof(opcode_counts));
    if (mir.count != 448 || mir_cfg_block_count() != 2 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        int opcode = mir.insns[instruction].opcode;

        if (opcode < 0 || opcode > MIR_RETURN)
            return mir_machine_reject(
                "final-call-check-schedule", "opcode-range");
        ++opcode_counts[opcode];
        if (opcode == MIR_CALL) {
            if (call_count >= 77)
                return mir_machine_reject(
                    "final-call-check-schedule", "call-overflow");
            call_indices[call_count++] = instruction;
        }
        if (instruction < 436 &&
            opcode != MIR_LABEL &&
            opcode != MIR_STRING_ADDRESS &&
            opcode != MIR_ARG &&
            opcode != MIR_NOP &&
            opcode != MIR_CONST &&
            opcode != MIR_CALL &&
            opcode != MIR_BINARY)
            return mir_machine_reject(
                "final-call-check-schedule", "prefix-opcode");
    }
    if (call_count != 77 ||
        opcode_counts[MIR_ARG] != 183 ||
        opcode_counts[MIR_CONST] != 85 ||
        opcode_counts[MIR_STRING_ADDRESS] != 69 ||
        opcode_counts[MIR_NOP] != 26 ||
        opcode_counts[MIR_LABEL] != 2 ||
        opcode_counts[MIR_BINARY] != 2 ||
        opcode_counts[MIR_LOAD] != 1 ||
        opcode_counts[MIR_BRANCH_FALSE] != 1 ||
        opcode_counts[MIR_RETURN] != 2)
        return mir_machine_reject(
            "final-call-check-schedule", "opcode-counts");

    for (group = 0; group < 6; ++group) {
        int item;
        int check_slot =
            (group_kinds[group] == MIR_FINAL_CALL_DIRECT ? 2 : 0) +
            (group_widths[group] == 4);
        int value_slot = group < 2 ? group : group - 2;

        for (item = 0; item < group_counts[group]; ++item) {
            if (group_kinds[group] == MIR_FINAL_CALL_DIRECT) {
                if (!mir_match_final_direct_call(
                        &plan->checks[check],
                        &mir.insns[call_indices[call_position]],
                        group_widths[group],
                        &check_functions[check_slot]))
                    return mir_machine_reject(
                        "final-call-check-schedule", "direct-call");
                ++call_position;
            } else {
                if (!mir_match_final_nested_call(
                        &plan->checks[check],
                        &mir.insns[call_indices[call_position]],
                        &mir.insns[call_indices[call_position + 1]],
                        group_kinds[group], group_widths[group],
                        &value_functions[value_slot],
                        &check_functions[check_slot]))
                    return mir_machine_reject(
                        "final-call-check-schedule", "nested-call");
                call_position += 2;
            }
            ++check;
        }
    }
    if (check != MIR_FINAL_CALL_CHECK_COUNT ||
        call_position != 76 ||
        check_functions[0] == NULL ||
        check_functions[1] == NULL ||
        check_functions[2] == NULL ||
        check_functions[3] == NULL ||
        value_functions[0] == NULL ||
        value_functions[1] == NULL)
        return mir_machine_reject(
            "final-call-check-schedule", "group-counts");

    final_load = &mir.insns[436];
    final_call = &mir.insns[445];
    if (mir.insns[0].opcode != MIR_LABEL ||
        final_load->opcode != MIR_LOAD ||
        !mir_machine_named_nonvolatile(final_load) ||
        !mir_scalar_memory_location(
            final_load, &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        type_ptr_depth(final_load->type) != 0 ||
        type_size(final_load->type) != 2 ||
        (plan->failure_root =
             find_global(final_load->name)) == NULL ||
        mir.insns[437].opcode != MIR_CONST ||
        !mir_machine_constant_equals(mir.insns[437].dst, 0) ||
        mir.insns[438].opcode != MIR_BINARY ||
        mir.insns[438].immediate != TOK_NE ||
        mir.insns[438].src1 != final_load->dst ||
        mir.insns[438].src2 != mir.insns[437].dst ||
        mir.insns[439].opcode != MIR_BRANCH_FALSE ||
        mir.insns[439].src1 != mir.insns[438].dst ||
        mir.insns[439].label != mir.insns[442].label ||
        mir.insns[440].opcode != MIR_CONST ||
        !mir_machine_constant_equals(mir.insns[440].dst, 1) ||
        mir.insns[441].opcode != MIR_RETURN ||
        mir.insns[441].src1 != mir.insns[440].dst ||
        mir.insns[442].opcode != MIR_LABEL ||
        mir.insns[443].opcode != MIR_STRING_ADDRESS ||
        !mir_match_final_string_value(
            mir.insns[443].dst, &plan->success_string_id) ||
        mir.insns[444].opcode != MIR_ARG ||
        mir.insns[444].src1 != mir.insns[443].dst ||
        final_call->opcode != MIR_CALL ||
        call_indices[76] != 445 ||
        !mir_machine_single_call_argument(
            final_call, &print_argument) ||
        print_argument != mir.insns[443].dst ||
        (plan->print_function =
             find_global(final_call->name)) == NULL ||
        plan->print_function->is_funcptr ||
        plan->print_function->is_noreturn ||
        !plan->print_function->has_proto ||
        plan->print_function->proto_nargs != 1 ||
        !plan->print_function->proto_variadic ||
        (final_call->type & 15) != TYPE_INT ||
        mir.insns[446].opcode != MIR_CONST ||
        !mir_machine_constant_equals(mir.insns[446].dst, 0) ||
        mir.insns[447].opcode != MIR_RETURN ||
        mir.insns[447].src1 != mir.insns[446].dst)
        return mir_machine_reject(
            "final-call-check-schedule", "final");
    plan->failure_offset = memory_offset;
    return 1;
}

static int mir_match_math_verification_schedule(
    struct MirMathVerificationSchedule *plan)
{
    const struct MirInsn *frexp_output = NULL;
    const struct MirInsn *frexp_result = NULL;
    const struct MirInsn *modf_output = NULL;
    const struct MirInsn *modf_result = NULL;
    struct Sym *ordinary_check = NULL;
    struct Sym *exact_check = NULL;
    struct Sym *final_failures_root;
    int call_indices[179];
    int opcode_counts[MIR_RETURN + 1];
    int call_count = 0;
    int final_failures_offset;
    int instruction;
    int check;
    int arguments[3];
    int argument;
    int print_argument;

    memset(plan, 0, sizeof(*plan));
    memset(opcode_counts, 0, sizeof(opcode_counts));
    if (mir.count != 945 || mir_cfg_block_count() != 2 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        int opcode = mir.insns[instruction].opcode;

        if (opcode < 0 || opcode > MIR_RETURN)
            return mir_machine_reject(
                "math-verification-schedule", "opcode-range");
        ++opcode_counts[opcode];
        if (opcode == MIR_CALL) {
            if (call_count >= 179)
                return mir_machine_reject(
                    "math-verification-schedule", "call-overflow");
            call_indices[call_count++] = instruction;
        }
    }
    if (call_count != 179 ||
        opcode_counts[MIR_LABEL] != 2 ||
        opcode_counts[MIR_STRING_ADDRESS] != 95 ||
        opcode_counts[MIR_ARG] != 384 ||
        opcode_counts[MIR_CALL] != 179 ||
        opcode_counts[MIR_FLOAT_CONST] != 187 ||
        opcode_counts[MIR_UNARY] != 47 ||
        opcode_counts[MIR_ADDRESS] != 6 ||
        opcode_counts[MIR_NOP] != 19 ||
        opcode_counts[MIR_STORE] != 6 ||
        opcode_counts[MIR_LOAD] != 9 ||
        opcode_counts[MIR_CONST] != 7 ||
        opcode_counts[MIR_BINARY] != 1 ||
        opcode_counts[MIR_BRANCH_FALSE] != 1 ||
        opcode_counts[MIR_RETURN] != 2)
        return mir_machine_reject(
            "math-verification-schedule", "opcode-counts");
    for (instruction = 0; instruction <= MIR_RETURN; ++instruction) {
        int expected = 0;

        switch (instruction) {
        case MIR_LABEL: expected = 2; break;
        case MIR_STRING_ADDRESS: expected = 95; break;
        case MIR_ARG: expected = 384; break;
        case MIR_CALL: expected = 179; break;
        case MIR_FLOAT_CONST: expected = 187; break;
        case MIR_UNARY: expected = 47; break;
        case MIR_ADDRESS: expected = 6; break;
        case MIR_NOP: expected = 19; break;
        case MIR_STORE: expected = 6; break;
        case MIR_LOAD: expected = 9; break;
        case MIR_CONST: expected = 7; break;
        case MIR_BINARY: expected = 1; break;
        case MIR_BRANCH_FALSE: expected = 1; break;
        case MIR_RETURN: expected = 2; break;
        default: break;
        }
        if (opcode_counts[instruction] != expected)
            return mir_machine_reject(
                "math-verification-schedule", "unexpected-opcode");
    }

    if (mir.insns[0].opcode != MIR_LABEL ||
        call_indices[0] != 3 ||
        mir.insns[1].opcode != MIR_STRING_ADDRESS ||
        !mir_machine_single_call_argument(
            &mir.insns[call_indices[0]], &print_argument) ||
        print_argument != mir.insns[1].dst ||
        !mir_match_math_print_function(
            &mir.insns[call_indices[0]], &plan->print_function))
        return mir_machine_reject(
            "math-verification-schedule", "intro");
    plan->intro_string_id = (int)mir.insns[1].immediate;

    for (check = 0;
         check < MIR_MATH_ORDINARY_CHECK_COUNT; ++check) {
        struct MirMathCallCheck *item = &plan->calls[check];

        if (!mir_match_math_call_check(
                item,
                &mir.insns[call_indices[1 + check * 2]],
                &mir.insns[call_indices[2 + check * 2]],
                &ordinary_check))
            return mir_machine_reject(
                "math-verification-schedule", "ordinary-check");
        for (argument = 0;
             argument < item->argument_count; ++argument)
            if (item->arguments[argument].kind !=
                    MIR_MATH_FLOAT_CONSTANT)
                return mir_machine_reject(
                    "math-verification-schedule",
                    "ordinary-argument");
    }

    if (call_indices[149] != 737 ||
        call_indices[161] != 832 ||
        call_indices[169] != 877 ||
        call_indices[175] != 921)
        return mir_machine_reject(
            "math-verification-schedule", "section-boundaries");
    for (check = 0;
         check < MIR_MATH_FREXP_CHECK_COUNT; ++check)
        if (!mir_match_math_frexp_check(
                &plan->frexp[check],
                &mir.insns[call_indices[149 + check * 3]],
                &mir.insns[call_indices[150 + check * 3]],
                &mir.insns[call_indices[151 + check * 3]],
                &exact_check, &frexp_output, &frexp_result))
            return mir_machine_reject(
                "math-verification-schedule", "frexp-check");

    for (check = 0;
         check < MIR_MATH_MODEXP_CHECK_COUNT; ++check) {
        struct MirMathCallCheck *item =
            &plan->calls[MIR_MATH_ORDINARY_CHECK_COUNT + check];

        if (!mir_match_math_call_check(
                item,
                &mir.insns[call_indices[161 + check * 2]],
                &mir.insns[call_indices[162 + check * 2]],
                &exact_check) ||
            item->argument_count != 2 ||
            item->arguments[0].kind !=
                MIR_MATH_FLOAT_CONSTANT ||
            item->arguments[1].kind !=
                MIR_MATH_WORD_CONSTANT)
            return mir_machine_reject(
                "math-verification-schedule", "ldexp-check");
    }

    for (check = 0;
         check < MIR_MATH_MODF_CHECK_COUNT; ++check)
        if (!mir_match_math_modf_check(
                &plan->modf[check],
                &mir.insns[call_indices[169 + check * 3]],
                &mir.insns[call_indices[170 + check * 3]],
                &mir.insns[call_indices[171 + check * 3]],
                &exact_check, &modf_output, &modf_result))
            return mir_machine_reject(
                "math-verification-schedule", "modf-check");
    if (ordinary_check == NULL || exact_check == NULL ||
        ordinary_check == exact_check ||
        frexp_output == NULL || frexp_result == NULL ||
        modf_output == NULL || modf_result == NULL)
        return mir_machine_reject(
            "math-verification-schedule", "check-identities");

    if (mir.insns[919].opcode != MIR_STRING_ADDRESS ||
        !mir_machine_single_call_argument(
            &mir.insns[call_indices[175]], &print_argument) ||
        print_argument != mir.insns[919].dst ||
        !mir_match_math_print_function(
            &mir.insns[call_indices[175]], &plan->print_function) ||
        mir.insns[922].opcode != MIR_STRING_ADDRESS ||
        !mir_machine_three_call_arguments(
            &mir.insns[call_indices[176]], arguments) ||
        arguments[0] != mir.insns[922].dst ||
        arguments[1] != mir.insns[924].dst ||
        arguments[2] != mir.insns[926].dst ||
        !mir_match_math_global_word(
            &mir.insns[924], &plan->checks_root,
            &plan->checks_offset) ||
        !mir_match_math_global_word(
            &mir.insns[926], &plan->failures_root,
            &plan->failures_offset) ||
        plan->checks_root == plan->failures_root ||
        !mir_match_math_print_function(
            &mir.insns[call_indices[176]], &plan->print_function))
        return mir_machine_reject(
            "math-verification-schedule", "summary");
    plan->separator_string_id = (int)mir.insns[919].immediate;
    plan->summary_string_id = (int)mir.insns[922].immediate;
    if (call_indices[176] != 928 ||
        call_indices[177] != 935 ||
        call_indices[178] != 942 ||
        !mir_match_math_global_word(
            &mir.insns[929], &final_failures_root,
            &final_failures_offset) ||
        final_failures_root != plan->failures_root ||
        final_failures_offset != plan->failures_offset ||
        mir.insns[930].opcode != MIR_CONST ||
        !mir_machine_constant_equals(mir.insns[930].dst, 0) ||
        mir.insns[931].opcode != MIR_BINARY ||
        mir.insns[931].immediate != TOK_EQ ||
        mir.insns[931].src1 != mir.insns[929].dst ||
        mir.insns[931].src2 != mir.insns[930].dst ||
        mir.insns[932].opcode != MIR_BRANCH_FALSE ||
        mir.insns[932].src1 != mir.insns[931].dst ||
        mir.insns[932].label != mir.insns[939].label ||
        mir.insns[933].opcode != MIR_STRING_ADDRESS ||
        !mir_machine_single_call_argument(
            &mir.insns[935], &print_argument) ||
        print_argument != mir.insns[933].dst ||
        !mir_match_math_print_function(
            &mir.insns[935], &plan->print_function) ||
        mir.insns[936].opcode != MIR_CONST ||
        !mir_machine_constant_equals(mir.insns[936].dst, 0) ||
        mir.insns[937].opcode != MIR_RETURN ||
        mir.insns[937].src1 != mir.insns[936].dst ||
        mir.insns[939].opcode != MIR_LABEL ||
        mir.insns[940].opcode != MIR_STRING_ADDRESS ||
        !mir_machine_single_call_argument(
            &mir.insns[942], &print_argument) ||
        print_argument != mir.insns[940].dst ||
        !mir_match_math_print_function(
            &mir.insns[942], &plan->print_function) ||
        mir.insns[943].opcode != MIR_CONST ||
        !mir_machine_constant_equals(mir.insns[943].dst, 1) ||
        mir.insns[944].opcode != MIR_RETURN ||
        mir.insns[944].src1 != mir.insns[943].dst)
        return mir_machine_reject(
            "math-verification-schedule", "final");
    plan->success_string_id = (int)mir.insns[933].immediate;
    plan->failure_string_id = (int)mir.insns[940].immediate;
    return 1;
}

static int mir_match_ctype_realloc_schedule(
    struct MirCtypeReallocSchedule *plan)
{
    const struct MirInsn *tail;
    const struct MirInsn *pointer_store;
    struct Sym *first_print = NULL;
    int arguments[2];
    int argument;
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction = 1;
    int byte;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir.local_bytes != 2 ||
        mir.aggregate_temp_bytes != 0 ||
        mir_cfg_block_count() != 5 ||
        !mir_match_ctype_word_type(mir.return_type) ||
        mir.count < 1 + 8 * 9 + 127 ||
        mir.insns[0].opcode != MIR_LABEL)
        return 0;
    while (mir_match_ctype_prefix_check(plan, &instruction))
        ;
    if (plan->check_count < 8 ||
        mir.count - instruction != 127)
        return mir_machine_reject(
            "ctype-realloc-schedule", "prefix");
    tail = &mir.insns[instruction];

    if (tail[0].opcode != MIR_CONST ||
        tail[1].opcode != MIR_NOP ||
        tail[2].opcode != MIR_ARG ||
        tail[3].opcode != MIR_CALL ||
        tail[4].opcode != MIR_NOP ||
        tail[5].opcode != MIR_UNARY ||
        tail[5].immediate != 0 ||
        tail[5].src1 != tail[3].dst ||
        !mir_match_ctype_pointer_type(tail[5].type) ||
        tail[6].opcode != MIR_STORE ||
        tail[6].src1 != tail[5].dst ||
        !mir_machine_named_nonvolatile(&tail[6]) ||
        !mir_scalar_memory_location(
            &tail[6], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_LOCAL ||
        type_size(memory_type) != 2 ||
        tail[7].opcode != MIR_LOAD ||
        !mir_machine_same_location(&tail[6], &tail[7]) ||
        tail[8].opcode != MIR_UNARY ||
        tail[8].immediate != '!' ||
        tail[8].src1 != tail[7].dst ||
        tail[9].opcode != MIR_BRANCH_FALSE ||
        tail[9].src1 != tail[8].dst ||
        tail[9].label != tail[16].label ||
        !mir_machine_single_call_argument(
            &tail[3], &argument) ||
        argument != tail[0].dst ||
        !mir_match_ctype_constant(
            tail[0].dst, &plan->allocation_size) ||
        !mir_match_ctype_pointer_call(
            &tail[3], 1, 0, 1,
            &plan->allocate_function))
        return mir_machine_reject(
            "ctype-realloc-schedule", "allocation");
    pointer_store = &tail[6];

    if (tail[10].opcode != MIR_STRING_ADDRESS ||
        tail[11].opcode != MIR_ARG ||
        tail[12].opcode != MIR_CALL ||
        tail[13].opcode != MIR_CONST ||
        tail[14].opcode != MIR_RETURN ||
        tail[14].src1 != tail[13].dst ||
        !mir_machine_constant_equals(tail[13].dst, 1) ||
        tail[15].opcode != MIR_NOP ||
        tail[16].opcode != MIR_LABEL ||
        !mir_machine_single_call_argument(
            &tail[12], &argument) ||
        argument != tail[10].dst ||
        !mir_match_ctype_string(
            tail[10].dst,
            &plan->allocation_failure_string_id) ||
        !mir_match_ctype_print_call(
            &tail[12], &first_print))
        return mir_machine_reject(
            "ctype-realloc-schedule", "allocation-failure");
    plan->print_function = first_print;

    if (tail[17].opcode != MIR_LOAD ||
        !mir_machine_same_location(pointer_store, &tail[17]) ||
        tail[18].opcode != MIR_ARG ||
        tail[19].opcode != MIR_STRING_ADDRESS ||
        tail[20].opcode != MIR_ARG ||
        tail[21].opcode != MIR_CALL ||
        !mir_machine_two_call_arguments(&tail[21], arguments) ||
        arguments[0] != tail[17].dst ||
        arguments[1] != tail[19].dst ||
        !mir_match_ctype_string(
            tail[19].dst, &plan->source_string_id) ||
        !mir_match_ctype_pointer_call(
            &tail[21], 2, 2, 1,
            &plan->copy_function))
        return mir_machine_reject(
            "ctype-realloc-schedule", "copy");

    if (tail[22].opcode != MIR_LOAD ||
        !mir_machine_same_location(pointer_store, &tail[22]) ||
        tail[23].opcode != MIR_NOP ||
        tail[24].opcode != MIR_ARG ||
        tail[25].opcode != MIR_CONST ||
        tail[26].opcode != MIR_NOP ||
        tail[27].opcode != MIR_ARG ||
        tail[28].opcode != MIR_CALL ||
        tail[29].opcode != MIR_NOP ||
        tail[30].opcode != MIR_UNARY ||
        tail[30].immediate != 0 ||
        tail[30].src1 != tail[28].dst ||
        !mir_match_ctype_pointer_type(tail[30].type) ||
        tail[31].opcode != MIR_STORE ||
        tail[31].src1 != tail[30].dst ||
        !mir_machine_same_location(pointer_store, &tail[31]) ||
        tail[32].opcode != MIR_LOAD ||
        !mir_machine_same_location(pointer_store, &tail[32]) ||
        tail[33].opcode != MIR_UNARY ||
        tail[33].immediate != '!' ||
        tail[33].src1 != tail[32].dst ||
        tail[34].opcode != MIR_BRANCH_FALSE ||
        tail[34].src1 != tail[33].dst ||
        tail[34].label != tail[41].label ||
        !mir_machine_two_call_arguments(&tail[28], arguments) ||
        arguments[0] != tail[22].dst ||
        arguments[1] != tail[25].dst ||
        !mir_match_ctype_constant(
            tail[25].dst, &plan->grow_size) ||
        !mir_match_ctype_pointer_call(
            &tail[28], 2, 1, 1,
            &plan->resize_function))
        return mir_machine_reject(
            "ctype-realloc-schedule", "grow");

    if (tail[35].opcode != MIR_STRING_ADDRESS ||
        tail[36].opcode != MIR_ARG ||
        tail[37].opcode != MIR_CALL ||
        tail[38].opcode != MIR_CONST ||
        tail[39].opcode != MIR_RETURN ||
        tail[39].src1 != tail[38].dst ||
        !mir_machine_constant_equals(tail[38].dst, 1) ||
        tail[40].opcode != MIR_NOP ||
        tail[41].opcode != MIR_LABEL ||
        !mir_machine_single_call_argument(
            &tail[37], &argument) ||
        argument != tail[35].dst ||
        !mir_match_ctype_string(
            tail[35].dst, &plan->grow_failure_string_id) ||
        !mir_match_ctype_print_call(
            &tail[37], &plan->print_function))
        return mir_machine_reject(
            "ctype-realloc-schedule", "grow-failure");

    if (tail[42].opcode != MIR_LOAD ||
        !mir_machine_same_location(pointer_store, &tail[42]) ||
        tail[43].opcode != MIR_ARG ||
        tail[44].opcode != MIR_STRING_ADDRESS ||
        tail[45].opcode != MIR_ARG ||
        tail[46].opcode != MIR_CALL ||
        tail[47].opcode != MIR_CONST ||
        tail[48].opcode != MIR_BINARY ||
        tail[48].immediate != TOK_EQ ||
        tail[48].src1 != tail[46].dst ||
        tail[48].src2 != tail[47].dst ||
        tail[49].opcode != MIR_ARG ||
        tail[49].src1 != tail[48].dst ||
        tail[50].opcode != MIR_STRING_ADDRESS ||
        tail[51].opcode != MIR_ARG ||
        tail[52].opcode != MIR_CALL ||
        !mir_machine_two_call_arguments(&tail[46], arguments) ||
        arguments[0] != tail[42].dst ||
        arguments[1] != tail[44].dst ||
        !mir_match_ctype_string(
            tail[44].dst, &argument) ||
        argument != plan->source_string_id ||
        !mir_match_ctype_compare_call(
            &tail[46], &plan->compare_function) ||
        !mir_machine_constant_equals(tail[47].dst, 0) ||
        !mir_machine_two_call_arguments(&tail[52], arguments) ||
        arguments[0] != tail[48].dst ||
        arguments[1] != tail[50].dst ||
        !mir_match_ctype_string(
            tail[50].dst, &plan->preserve_string_id) ||
        !mir_match_ctype_check_call(
            &tail[52], &plan->check_function))
        return mir_machine_reject(
            "ctype-realloc-schedule", "preserve");

    for (byte = 0; byte < 2; ++byte) {
        const struct MirInsn *items = &tail[53 + byte * 6];
        unsigned long index;

        if (items[0].opcode != MIR_LOAD ||
            !mir_machine_same_location(pointer_store, &items[0]) ||
            items[1].opcode != MIR_CONST ||
            items[2].opcode != MIR_INDEX_ADDRESS ||
            items[2].src1 != items[0].dst ||
            items[2].src2 != items[1].dst ||
            items[2].immediate != 1 ||
            items[3].opcode != MIR_NOP ||
            items[4].opcode != MIR_CONST ||
            items[5].opcode != MIR_STORE_INDIRECT ||
            items[5].src1 != items[2].dst ||
            items[5].src2 != items[4].dst ||
            items[5].memory_size != 1 ||
            items[5].bit_width != 0 ||
            (items[5].memory_flags & (1 | 8)) != 0 ||
            !mir_match_ctype_constant(
                items[1].dst, &index) ||
            !mir_match_ctype_constant(
                items[4].dst,
                &plan->stored_values[byte]) ||
            index > 32767)
            return mir_machine_reject(
                "ctype-realloc-schedule", "byte-store");
        plan->store_indices[byte] =
            (int)(index & 0xffffUL);
    }

    if (tail[65].opcode != MIR_LOAD ||
        !mir_machine_same_location(pointer_store, &tail[65]) ||
        tail[66].opcode != MIR_NOP ||
        tail[67].opcode != MIR_ARG ||
        tail[68].opcode != MIR_CONST ||
        tail[69].opcode != MIR_NOP ||
        tail[70].opcode != MIR_ARG ||
        tail[71].opcode != MIR_CALL ||
        tail[72].opcode != MIR_NOP ||
        tail[73].opcode != MIR_UNARY ||
        tail[73].immediate != 0 ||
        tail[73].src1 != tail[71].dst ||
        !mir_match_ctype_pointer_type(tail[73].type) ||
        tail[74].opcode != MIR_STORE ||
        tail[74].src1 != tail[73].dst ||
        !mir_machine_same_location(pointer_store, &tail[74]) ||
        tail[75].opcode != MIR_LOAD ||
        !mir_machine_same_location(pointer_store, &tail[75]) ||
        tail[76].opcode != MIR_UNARY ||
        tail[76].immediate != '!' ||
        tail[76].src1 != tail[75].dst ||
        tail[77].opcode != MIR_BRANCH_FALSE ||
        tail[77].src1 != tail[76].dst ||
        tail[77].label != tail[84].label ||
        !mir_machine_two_call_arguments(&tail[71], arguments) ||
        arguments[0] != tail[65].dst ||
        arguments[1] != tail[68].dst ||
        !mir_match_ctype_constant(
            tail[68].dst, &plan->shrink_size) ||
        !mir_match_ctype_pointer_call(
            &tail[71], 2, 1, 1,
            &plan->resize_function))
        return mir_machine_reject(
            "ctype-realloc-schedule", "shrink");

    if (tail[78].opcode != MIR_STRING_ADDRESS ||
        tail[79].opcode != MIR_ARG ||
        tail[80].opcode != MIR_CALL ||
        tail[81].opcode != MIR_CONST ||
        tail[82].opcode != MIR_RETURN ||
        tail[82].src1 != tail[81].dst ||
        !mir_machine_constant_equals(tail[81].dst, 1) ||
        tail[83].opcode != MIR_NOP ||
        tail[84].opcode != MIR_LABEL ||
        !mir_machine_single_call_argument(
            &tail[80], &argument) ||
        argument != tail[78].dst ||
        !mir_match_ctype_string(
            tail[78].dst, &plan->shrink_failure_string_id) ||
        !mir_match_ctype_print_call(
            &tail[80], &plan->print_function))
        return mir_machine_reject(
            "ctype-realloc-schedule", "shrink-failure");

    for (byte = 0; byte < 2; ++byte) {
        const struct MirInsn *items = &tail[85 + byte * 11];
        unsigned long index;

        if (items[0].opcode != MIR_LOAD ||
            !mir_machine_same_location(pointer_store, &items[0]) ||
            items[1].opcode != MIR_CONST ||
            items[2].opcode != MIR_INDEX_ADDRESS ||
            items[2].src1 != items[0].dst ||
            items[2].src2 != items[1].dst ||
            items[2].immediate != 1 ||
            items[3].opcode != MIR_LOAD_INDIRECT ||
            items[3].src1 != items[2].dst ||
            items[3].memory_size != 1 ||
            items[3].bit_width != 0 ||
            (items[3].memory_flags & (1 | 8)) != 0 ||
            items[4].opcode != MIR_CONST ||
            items[5].opcode != MIR_UNARY ||
            items[5].immediate != 0 ||
            items[5].src1 != items[3].dst ||
            items[6].opcode != MIR_BINARY ||
            items[6].immediate != TOK_EQ ||
            items[6].src1 != items[5].dst ||
            items[6].src2 != items[4].dst ||
            items[7].opcode != MIR_ARG ||
            items[7].src1 != items[6].dst ||
            items[8].opcode != MIR_STRING_ADDRESS ||
            items[9].opcode != MIR_ARG ||
            items[10].opcode != MIR_CALL ||
            !mir_machine_two_call_arguments(
                &items[10], arguments) ||
            arguments[0] != items[6].dst ||
            arguments[1] != items[8].dst ||
            !mir_match_ctype_constant(items[1].dst, &index) ||
            !mir_match_ctype_constant(
                items[4].dst,
                &plan->byte_expected[byte]) ||
            index > 32767 ||
            !mir_match_ctype_string(
                items[8].dst,
                &plan->byte_string_ids[byte]) ||
            !mir_match_ctype_check_call(
                &items[10], &plan->check_function))
            return mir_machine_reject(
                "ctype-realloc-schedule", "byte-check");
        plan->byte_indices[byte] = (int)index;
        plan->byte_unsigned[byte] =
            (items[3].type & TYPE_UNSIGNED) != 0;
    }

    if (tail[107].opcode != MIR_LOAD ||
        !mir_machine_same_location(pointer_store, &tail[107]) ||
        tail[108].opcode != MIR_NOP ||
        tail[109].opcode != MIR_ARG ||
        tail[110].opcode != MIR_CALL ||
        !mir_machine_single_call_argument(
            &tail[110], &argument) ||
        argument != tail[107].dst ||
        !mir_match_ctype_pointer_call(
            &tail[110], 1, 1, 0,
            &plan->free_function))
        return mir_machine_reject(
            "ctype-realloc-schedule", "free");

    if (tail[111].opcode != MIR_LOAD ||
        !mir_machine_named_nonvolatile(&tail[111]) ||
        !mir_scalar_memory_location(
            &tail[111], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        !mir_match_ctype_word_type(tail[111].type) ||
        (plan->failure_root =
             find_global(tail[111].name)) == NULL ||
        tail[112].opcode != MIR_BRANCH_FALSE ||
        tail[112].src1 != tail[111].dst ||
        tail[112].label != tail[121].label ||
        tail[113].opcode != MIR_STRING_ADDRESS ||
        tail[114].opcode != MIR_ARG ||
        tail[115].opcode != MIR_LOAD ||
        !mir_machine_same_location(&tail[111], &tail[115]) ||
        tail[116].opcode != MIR_ARG ||
        tail[117].opcode != MIR_CALL ||
        !mir_machine_two_call_arguments(
            &tail[117], arguments) ||
        arguments[0] != tail[113].dst ||
        arguments[1] != tail[115].dst ||
        !mir_match_ctype_string(
            tail[113].dst,
            &plan->final_failure_string_id) ||
        !mir_match_ctype_print_call(
            &tail[117], &plan->print_function) ||
        tail[118].opcode != MIR_CONST ||
        tail[119].opcode != MIR_RETURN ||
        tail[119].src1 != tail[118].dst ||
        !mir_machine_constant_equals(tail[118].dst, 1) ||
        tail[120].opcode != MIR_NOP ||
        tail[121].opcode != MIR_LABEL ||
        tail[122].opcode != MIR_STRING_ADDRESS ||
        tail[123].opcode != MIR_ARG ||
        tail[124].opcode != MIR_CALL ||
        !mir_machine_single_call_argument(
            &tail[124], &argument) ||
        argument != tail[122].dst ||
        !mir_match_ctype_string(
            tail[122].dst, &plan->success_string_id) ||
        !mir_match_ctype_print_call(
            &tail[124], &plan->print_function) ||
        tail[125].opcode != MIR_CONST ||
        tail[126].opcode != MIR_RETURN ||
        tail[126].src1 != tail[125].dst ||
        !mir_machine_constant_equals(tail[125].dst, 0))
        return mir_machine_reject(
            "ctype-realloc-schedule", "final");
    plan->failure_offset = memory_offset;
    return 1;
}

static int mir_match_context_op_schedule(
    struct MirContextOpSchedule *plan)
{
    static const int expected_counts[MIR_RETURN + 1] = {
        [MIR_LABEL] = 7,
        [MIR_NOP] = 18,
        [MIR_CONST] = 99,
        [MIR_FLOAT_CONST] = 7,
        [MIR_STORE] = 4,
        [MIR_PHI] = 1,
        [MIR_UNARY] = 11,
        [MIR_BINARY] = 5,
        [MIR_BRANCH_FALSE] = 2,
        [MIR_ADDRESS] = 1,
        [MIR_INDEX_ADDRESS] = 1,
        [MIR_STORE_INDIRECT] = 1,
        [MIR_JUMP] = 2,
        [MIR_ARG] = 167,
        [MIR_CALL] = 68,
        [MIR_STRING_ADDRESS] = 35,
        [MIR_LOAD] = 3,
        [MIR_RETURN] = 1
    };
    struct Sym *check_functions[3] = {NULL, NULL, NULL};
    struct Sym *value_functions[MIR_CONTEXT_OP_CHECK_COUNT];
    int call_indices[68];
    int check_counts[3] = {0, 0, 0};
    int opcode_counts[MIR_RETURN + 1];
    int call_count = 0;
    int instruction;
    int check;

    memset(plan, 0, sizeof(*plan));
    memset(opcode_counts, 0, sizeof(opcode_counts));
    memset(value_functions, 0, sizeof(value_functions));
    if (mir.count != 433 || mir_cfg_block_count() != 7 ||
        mir.has_vla || mir.local_bytes != 1 ||
        mir.aggregate_temp_bytes != 0 ||
        type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        int opcode = mir.insns[instruction].opcode;

        if (opcode < 0 || opcode > MIR_RETURN)
            return mir_machine_reject(
                "context-op-schedule", "opcode-range");
        ++opcode_counts[opcode];
        if (opcode == MIR_CALL) {
            if (call_count >= 68)
                return mir_machine_reject(
                    "context-op-schedule", "call-overflow");
            call_indices[call_count++] = instruction;
        }
    }
    if (call_count != 68)
        return mir_machine_reject(
            "context-op-schedule", "call-count");
    for (instruction = 0; instruction <= MIR_RETURN; ++instruction)
        if (opcode_counts[instruction] != expected_counts[instruction])
            return mir_machine_reject(
                "context-op-schedule", "opcode-counts");
    if (!mir_match_context_prefix(plan) ||
        call_indices[0] != 32 ||
        call_indices[29] != 213 ||
        call_indices[30] != 222 ||
        call_indices[65] != 412 ||
        !mir_match_context_float_stores(plan))
        return mir_machine_reject(
            "context-op-schedule", "sections");
    for (check = 0; check < MIR_CONTEXT_OP_CHECK_COUNT; ++check) {
        struct MirContextOpCheck *item = &plan->checks[check];
        int category;
        int prior;

        if (!mir_match_context_value_call(
                item, &mir.insns[call_indices[check * 2]]) ||
            !mir_match_context_check_call(
                item, &mir.insns[call_indices[check * 2]],
                &mir.insns[call_indices[check * 2 + 1]]))
            return mir_machine_reject(
                "context-op-schedule", "check");
        category = item->check_float ? 2 :
            (item->check_unsigned ? 1 : 0);
        if (check_functions[category] != NULL &&
            check_functions[category] != item->check_function)
            return mir_machine_reject(
                "context-op-schedule", "check-identity");
        check_functions[category] = item->check_function;
        ++check_counts[category];
        value_functions[check] = item->value_function;
        for (prior = 0; prior < check; ++prior)
            if (plan->checks[prior].string_id == item->string_id)
                return mir_machine_reject(
                    "context-op-schedule", "duplicate-string");
    }
    if (check_counts[0] != 24 || check_counts[1] != 4 ||
        check_counts[2] != 5 ||
        check_functions[0] == NULL ||
        check_functions[1] == NULL ||
        check_functions[2] == NULL ||
        check_functions[0] == check_functions[1] ||
        check_functions[0] == check_functions[2] ||
        check_functions[1] == check_functions[2])
        return mir_machine_reject(
            "context-op-schedule", "check-groups");
    if (value_functions[5] != value_functions[6] ||
        value_functions[10] != value_functions[11] ||
        value_functions[10] != value_functions[12] ||
        value_functions[20] != value_functions[21] ||
        value_functions[23] != value_functions[24] ||
        value_functions[29] != value_functions[30])
        return mir_machine_reject(
            "context-op-schedule", "repeated-functions");
    if (!mir_match_context_tail(plan, call_indices))
        return mir_machine_reject(
            "context-op-schedule", "tail");
    return 1;
}

static int mir_match_format_buffer_schedule(
    struct MirFormatBufferSchedule *plan)
{
    static const int final_opcodes[27] = {
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_LOAD, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_LOAD,
        MIR_BRANCH_FALSE, MIR_CONST, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_LABEL, MIR_PHI,
        MIR_RETURN
    };
    const struct MirInsn *buffer = &mir.insns[1];
    struct Sym *function;
    int cursor = 1;
    int check;
    int nop_count = 0;
    int buffer_offset;
    int final_instruction;
    int memory_type;
    int memory_storage;
    int memory_offset;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 897 || mir_cfg_block_count() != 8)
        return 0;
    if (mir.has_vla || mir.aggregate_temp_bytes != 0 ||
        mir.local_bytes != 66 ||
        !mir_match_action_decode_word_type(mir.return_type) ||
        (mir.return_type & TYPE_UNSIGNED) != 0 ||
        mir.insns[0].opcode != MIR_LABEL)
        return mir_machine_reject(
            "format-buffer-schedule", "function-shape");
    if (!mir_match_buffered_declaration_buffer(
            1, buffer, &buffer_offset) ||
        buffer_offset != -64)
        return mir_machine_reject(
            "format-buffer-schedule", "buffer-location");
    if (!mir_match_format_buffer_declaration(buffer))
        return mir_machine_reject(
            "format-buffer-schedule", "buffer-declaration");

    for (check = 0;
         check < MIR_FORMAT_BUFFER_CHECK_COUNT;
         ++check) {
        struct MirFormatBufferCheck *item = &plan->checks[check];
        const struct MirInsn *format_address;
        const struct MirInsn *format_string;
        const struct MirInsn *value;
        const struct MirInsn *format_call;
        const struct MirInsn *check_address;
        const struct MirInsn *expected_string;
        const struct MirInsn *description_string;
        const struct MirInsn *check_call;
        int format_arguments[3];
        int check_arguments[3];
        int item_nops = 0;

        format_address = &mir.insns[cursor++];
        if (cursor + 5 >= mir.count ||
            mir.insns[cursor++].opcode != MIR_ARG)
            return mir_machine_reject(
                "format-buffer-schedule", "format-address");
        format_string = &mir.insns[cursor++];
        if (mir.insns[cursor++].opcode != MIR_ARG)
            return mir_machine_reject(
                "format-buffer-schedule", "format-string-argument");
        while (cursor < mir.count &&
               mir.insns[cursor].opcode == MIR_NOP) {
            ++cursor;
            ++item_nops;
            ++nop_count;
        }
        if (item_nops > 1)
            return mir_machine_reject(
                "format-buffer-schedule", "constant-nops");
        value = &mir.insns[cursor++];
        if (mir.insns[cursor++].opcode != MIR_ARG)
            return mir_machine_reject(
                "format-buffer-schedule", "value-argument");
        format_call = &mir.insns[cursor++];

        check_address = &mir.insns[cursor++];
        if (mir.insns[cursor++].opcode != MIR_ARG)
            return mir_machine_reject(
                "format-buffer-schedule", "check-address");
        expected_string = &mir.insns[cursor++];
        if (mir.insns[cursor++].opcode != MIR_ARG)
            return mir_machine_reject(
                "format-buffer-schedule", "expected-argument");
        description_string = &mir.insns[cursor++];
        if (mir.insns[cursor++].opcode != MIR_ARG)
            return mir_machine_reject(
                "format-buffer-schedule", "description-argument");
        check_call = &mir.insns[cursor++];

        if (!mir_match_buffered_declaration_buffer(
                (int)(format_address - mir.insns),
                buffer, &buffer_offset) ||
            !mir_match_buffered_declaration_buffer(
                (int)(check_address - mir.insns),
                buffer, &buffer_offset) ||
            !mir_match_format_buffer_pointer_string(format_string) ||
            !mir_match_format_buffer_pointer_string(expected_string) ||
            !mir_match_format_buffer_pointer_string(description_string) ||
            value->opcode != MIR_CONST ||
            type_ptr_depth(value->type) != 0 ||
            type_is_float(value->type) ||
            (type_size(value->type) != 2 &&
             type_size(value->type) != 4) ||
            !mir_machine_three_call_arguments(
                format_call, format_arguments) ||
            format_arguments[0] != format_address->dst ||
            format_arguments[1] != format_string->dst ||
            format_arguments[2] != value->dst ||
            !mir_machine_three_call_arguments(
                check_call, check_arguments) ||
            check_arguments[0] != check_address->dst ||
            check_arguments[1] != expected_string->dst ||
            check_arguments[2] != description_string->dst ||
            mir_value_use_count(format_call->dst) != 0 ||
            mir_value_use_count(check_call->dst) != 0)
            return mir_machine_reject(
                "format-buffer-schedule", "call-shape");

        if (!mir_match_format_buffer_function(
                format_call, 2, 1, 0, &function) ||
            format_call->memory_flags !=
                (MIR_CALL_FLAG_VARIADIC |
                 (format_call->memory_flags &
                  MIR_CALL_FLAG_FORMAT_RUNTIME)))
            return mir_machine_reject(
                "format-buffer-schedule", "format-function");
        if (plan->format_function == NULL)
            plan->format_function = function;
        else if (plan->format_function != function)
            return mir_machine_reject(
                "format-buffer-schedule", "mixed-format-functions");

        if (!mir_match_format_buffer_function(
                check_call, 3, 0, 1, &function) ||
            check_call->memory_flags != 0)
            return mir_machine_reject(
                "format-buffer-schedule", "check-function");
        if (plan->check_function == NULL)
            plan->check_function = function;
        else if (plan->check_function != function)
            return mir_machine_reject(
                "format-buffer-schedule", "mixed-check-functions");

        item->format_string_id = (int)format_string->immediate;
        item->expected_string_id = (int)expected_string->immediate;
        item->description_string_id =
            (int)description_string->immediate;
        item->value =
            (unsigned long)value->immediate &
            (type_size(value->type) == 4
                 ? 0xffffffffUL : 0xffffUL);
        item->value_size = type_size(value->type);
        item->runtime_flags =
            format_call->memory_flags &
            MIR_CALL_FLAG_FORMAT_RUNTIME;
        snprintf(item->call_name, sizeof(item->call_name), "%s",
                 format_call->base_name[0] != 0
                     ? format_call->base_name
                     : asm_name_for(
                           sym_asm_name(plan->format_function)));
    }
    if (cursor != 870 || nop_count != 15)
        return mir_machine_reject(
            "format-buffer-schedule", "sequence-length");

    for (final_instruction = 0;
         final_instruction <
             (int)(sizeof(final_opcodes) / sizeof(final_opcodes[0]));
         ++final_instruction)
        if (mir.insns[cursor + final_instruction].opcode !=
            final_opcodes[final_instruction])
            return mir_machine_reject(
                "format-buffer-schedule", "final-opcodes");

    if (!mir_machine_named_nonvolatile(&mir.insns[870]) ||
        !mir_scalar_memory_location(
            &mir.insns[870], &memory_type,
            &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        !mir_match_action_decode_word_type(memory_type) ||
        !mir_machine_constant_equals(mir.insns[871].dst, 0) ||
        mir.insns[872].immediate != TOK_EQ ||
        mir.insns[872].src1 != mir.insns[870].dst ||
        mir.insns[872].src2 != mir.insns[871].dst ||
        mir.insns[873].src1 != mir.insns[872].dst ||
        mir.insns[873].label != mir.insns[879].label ||
        !mir_match_format_buffer_pointer_string(&mir.insns[875]) ||
        !mir_match_format_buffer_pointer_string(&mir.insns[880]) ||
        mir.insns[878].label != mir.insns[885].label ||
        !mir_machine_same_location(
            &mir.insns[870], &mir.insns[882]) ||
        !mir_machine_same_location(
            &mir.insns[870], &mir.insns[886]) ||
        mir.insns[887].src1 != mir.insns[886].dst ||
        mir.insns[887].label != mir.insns[891].label ||
        !mir_machine_constant_equals(mir.insns[888].dst, 1) ||
        mir.insns[890].label != mir.insns[894].label ||
        !mir_machine_constant_equals(mir.insns[892].dst, 0) ||
        mir.insns[895].src1 != mir.insns[888].dst ||
        mir.insns[895].src2 != mir.insns[892].dst ||
        mir.insns[895].phi_pred1 != mir.insns[889].label ||
        mir.insns[895].phi_pred2 != mir.insns[893].label ||
        mir.insns[896].src1 != mir.insns[895].dst)
        return mir_machine_reject(
            "format-buffer-schedule", "final-control");
    plan->failure_root = find_global(mir.insns[870].name);
    if (plan->failure_root == NULL ||
        plan->failure_root->is_volatile)
        return mir_machine_reject(
            "format-buffer-schedule", "failure-global");
    plan->failure_offset = memory_offset;

    {
        int success_argument;
        int failure_arguments[2];

        if (!mir_machine_single_call_argument(
                &mir.insns[877], &success_argument) ||
            success_argument != mir.insns[875].dst ||
            !mir_machine_two_call_arguments(
                &mir.insns[884], failure_arguments) ||
            failure_arguments[0] != mir.insns[880].dst ||
            failure_arguments[1] != mir.insns[882].dst ||
            mir.insns[877].memory_flags != MIR_CALL_FLAG_VARIADIC ||
            mir.insns[884].memory_flags != MIR_CALL_FLAG_VARIADIC ||
            !mir_match_format_buffer_function(
                &mir.insns[877], 1, 1, 0, &function))
            return mir_machine_reject(
                "format-buffer-schedule", "summary-calls");
        plan->print_function = function;
        if (!mir_match_format_buffer_function(
                &mir.insns[884], 1, 1, 0, &function) ||
            function != plan->print_function)
            return mir_machine_reject(
                "format-buffer-schedule", "mixed-summary-functions");
    }
    plan->success_string_id = (int)mir.insns[875].immediate;
    plan->failure_string_id = (int)mir.insns[880].immediate;
    snprintf(plan->success_call_name,
             sizeof(plan->success_call_name), "%s",
             mir.insns[877].base_name[0] != 0
                 ? mir.insns[877].base_name
                 : asm_name_for(
                       sym_asm_name(plan->print_function)));
    snprintf(plan->failure_call_name,
             sizeof(plan->failure_call_name), "%s",
             mir.insns[884].base_name[0] != 0
                 ? mir.insns[884].base_name
                 : asm_name_for(
                       sym_asm_name(plan->print_function)));
    return 1;
}

static int mir_match_atof_schedule(
    struct MirAtofSchedule *plan)
{
    static const int group_counts[9] =
        {16, 3, 5, 5, 3, 3, 5, 4, 2};
    static const int group_kinds[9] = {
        MIR_ATOF_SCHEDULE_FLOAT,
        MIR_ATOF_SCHEDULE_INT,
        MIR_ATOF_SCHEDULE_END,
        MIR_ATOF_SCHEDULE_INFINITY,
        MIR_ATOF_SCHEDULE_FLOAT,
        MIR_ATOF_SCHEDULE_NAN,
        MIR_ATOF_SCHEDULE_FLOAT,
        MIR_ATOF_SCHEDULE_END,
        MIR_ATOF_SCHEDULE_FLOAT
    };
    int opcode_counts[MIR_RETURN + 1];
    int cursor = 1;
    int check = 0;
    int group;
    int instruction;
    int nop_count = 0;
    int scaled_float_count = 0;
    int scaled_int_count = 0;

    memset(plan, 0, sizeof(*plan));
    memset(opcode_counts, 0, sizeof(opcode_counts));
    if (mir.count != 482 || mir_cfg_block_count() != 8 ||
        mir.has_vla || mir.local_bytes != 0 ||
        mir.aggregate_temp_bytes != 0 ||
        type_ptr_depth(mir.return_type) != 0 ||
        !mir_match_final_call_integer_type(mir.return_type, 2) ||
        mir.insns[0].opcode != MIR_LABEL)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        int opcode = mir.insns[instruction].opcode;

        if (opcode < 0 || opcode > MIR_RETURN)
            return mir_machine_reject(
                "atof-schedule", "opcode-range");
        ++opcode_counts[opcode];
    }
    if (opcode_counts[MIR_ARG] != 202 ||
        opcode_counts[MIR_STRING_ADDRESS] != 94 ||
        opcode_counts[MIR_CALL] != 85 ||
        opcode_counts[MIR_FLOAT_CONST] != 55 ||
        opcode_counts[MIR_CONST] != 19 ||
        opcode_counts[MIR_LABEL] != 8 ||
        opcode_counts[MIR_UNARY] != 5 ||
        opcode_counts[MIR_LOAD] != 4 ||
        opcode_counts[MIR_BINARY] != 3 ||
        opcode_counts[MIR_BRANCH_FALSE] != 2 ||
        opcode_counts[MIR_JUMP] != 2 ||
        opcode_counts[MIR_NOP] != 1 ||
        opcode_counts[MIR_PHI] != 1 ||
        opcode_counts[MIR_RETURN] != 1)
        return mir_machine_reject(
            "atof-schedule", "opcode-counts");

    for (group = 0; group < 9; ++group) {
        int item_index;

        for (item_index = 0;
             item_index < group_counts[group];
             ++item_index) {
            struct MirAtofScheduleCheck *item =
                &plan->checks[check];
            int matched = 0;

            item->kind = group_kinds[group];
            if (item->kind == MIR_ATOF_SCHEDULE_FLOAT)
                matched = mir_match_atof_schedule_float_check(
                    plan, item, &cursor, 455);
            else if (item->kind == MIR_ATOF_SCHEDULE_INT)
                matched = mir_match_atof_schedule_int_check(
                    plan, item, &cursor, 455, &nop_count);
            else if (item->kind == MIR_ATOF_SCHEDULE_END)
                matched = mir_match_atof_schedule_end_check(
                    plan, item, &cursor, 455);
            else if (item->kind == MIR_ATOF_SCHEDULE_INFINITY)
                matched = mir_match_atof_schedule_infinity_check(
                    plan, item, &cursor, 455);
            else if (item->kind == MIR_ATOF_SCHEDULE_NAN)
                matched = mir_match_atof_schedule_nan_check(
                    plan, item, &cursor, 455);
            if (!matched)
                return mir_machine_reject(
                    "atof-schedule", "check-shape");
            if (item->has_scale) {
                if (item->kind == MIR_ATOF_SCHEDULE_FLOAT)
                    ++scaled_float_count;
                else if (item->kind == MIR_ATOF_SCHEDULE_INT)
                    ++scaled_int_count;
                else
                    return mir_machine_reject(
                        "atof-schedule", "scaled-kind");
            }
            ++check;
        }
    }
    if (check != MIR_ATOF_SCHEDULE_CHECK_COUNT ||
        cursor != 455 || nop_count != 1 ||
        scaled_float_count != 2 || scaled_int_count != 1 ||
        plan->value_function == NULL ||
        plan->float_check_function == NULL ||
        plan->int_check_function == NULL ||
        plan->end_check_function == NULL ||
        plan->infinity_check_function == NULL ||
        plan->nan_check_function == NULL ||
        !mir_match_atof_schedule_final(plan, cursor))
        return mir_machine_reject(
            "atof-schedule", "complete-shape");
    return 1;
}

static int mir_match_local_identity_array_result(
    struct MirLocalIdentityArrayResult *plan)
{
    static const int expected_opcodes[32] = {
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_PHI,
        MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP, MIR_UNARY,
        MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_ADDRESS, MIR_NOP, MIR_STORE,
        MIR_LOAD, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_RETURN
    };
    int instruction;
    int memory_offset;
    int memory_storage;
    int memory_type;
    long selected_index;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 32 || mir_cfg_block_count() != 4 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        type_size(mir.return_type) != 2)
        return mir_machine_reject("local-identity-array", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject("local-identity-array", "opcode");
    if (!mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[3]) ||
        mir.insns[5].src1 != mir.insns[2].dst ||
        mir.insns[5].src2 != mir.insns[20].dst ||
        mir.insns[5].phi_pred1 != mir.insns[0].label ||
        mir.insns[5].phi_pred2 != mir.insns[17].label ||
        mir.insns[7].immediate <= 0 ||
        mir.insns[7].immediate > 32767 ||
        mir.insns[8].immediate != 0 ||
        mir.insns[8].src1 != mir.insns[5].dst ||
        mir.insns[9].immediate != '<' ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].src2 != mir.insns[7].dst ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[10].label != mir.insns[23].label)
        return mir_machine_reject("local-identity-array", "loop");
    if (!mir_machine_named_nonvolatile(&mir.insns[11]) ||
        !mir_scalar_memory_location(
            &mir.insns[11], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_LOCAL || memory_offset >= 0 ||
        mir.insns[13].src1 != mir.insns[11].dst ||
        mir.insns[13].src2 != mir.insns[5].dst ||
        mir.insns[13].immediate != 2 ||
        mir.insns[13].memory_size != 2 ||
        mir.insns[15].immediate != 0 ||
        mir.insns[15].src1 != mir.insns[5].dst ||
        mir.insns[16].src1 != mir.insns[13].dst ||
        mir.insns[16].src2 != mir.insns[15].dst ||
        mir.insns[16].memory_size != 2 ||
        (mir.insns[16].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_constant_equals(mir.insns[19].dst, 1) ||
        mir.insns[20].immediate != '+' ||
        mir.insns[20].src1 != mir.insns[5].dst ||
        mir.insns[20].src2 != mir.insns[19].dst ||
        !mir_machine_same_location(&mir.insns[3], &mir.insns[21]) ||
        mir.insns[22].label != mir.insns[4].label)
        return mir_machine_reject("local-identity-array", "store");
    plan->array_offset = memory_offset;
    if (strcmp(mir.insns[11].name, mir.insns[24].name) ||
        !mir_machine_named_nonvolatile(&mir.insns[26]) ||
        !mir_scalar_memory_location(
            &mir.insns[26], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        type_ptr_depth(memory_type) != 1 ||
        mir.insns[26].src1 != mir.insns[24].dst ||
        !mir_machine_same_location(&mir.insns[26], &mir.insns[27]))
        return mir_machine_reject(
            "local-identity-array", "result-alias");
    plan->escaped_pointer = find_global(mir.insns[26].name);
    if (plan->escaped_pointer == NULL ||
        !plan->escaped_pointer->is_defined ||
        plan->escaped_pointer->is_volatile)
        return mir_machine_reject(
            "local-identity-array", "result-global");
    plan->escaped_pointer_offset = memory_offset;
    if (!mir_machine_constant_value(
            mir.insns[28].dst, &selected_index, 0) ||
        selected_index < 0 ||
        selected_index >= mir.insns[7].immediate ||
        mir.insns[29].src1 != mir.insns[27].dst ||
        mir.insns[29].src2 != mir.insns[28].dst ||
        mir.insns[29].immediate != 2)
        return mir_machine_reject(
            "local-identity-array", "result-index");
    if (
        mir.insns[30].src1 != mir.insns[29].dst ||
        mir.insns[30].memory_size != 2 ||
        (mir.insns[30].memory_flags & (1 | 8)) != 0 ||
        mir.insns[31].src1 != mir.insns[30].dst)
        return mir_machine_reject("local-identity-array", "result");
    plan->result = (int)selected_index;
    if (-plan->array_offset <
        (int)mir.insns[7].immediate * 2)
        return mir_machine_reject(
            "local-identity-array", "result-frame");
    return 1;
}

static int mir_match_flat_array_checks(struct MirFlatArrayChecks *plan)
{
    int instruction;
    int call_count = 0;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
        case MIR_PARAM:
        case MIR_CONST:
        case MIR_STRING_ADDRESS:
        case MIR_LOAD:
        case MIR_UNARY:
        case MIR_ARG:
        case MIR_INDEX_ADDRESS:
        case MIR_MEMBER_ADDRESS:
        case MIR_LOAD_INDIRECT:
            if (insn->opcode == MIR_LOAD &&
                (insn->memory_flags & 1) != 0)
                return 0;
            break;
        case MIR_STORE:
            {
                int memory_type, memory_storage, memory_offset;
                int parameter_stack_offset;
                long pointer_offset;

                if (!mir_scalar_memory_location(
                        insn, &memory_type, &memory_storage,
                        &memory_offset) ||
                    memory_storage != SC_LOCAL ||
                    type_ptr_depth(memory_type) == 0 ||
                    !mir_machine_parameter_address(
                        insn->src1, &parameter_stack_offset,
                        &pointer_offset, 0))
                    return 0;
                (void)parameter_stack_offset;
                (void)pointer_offset;
            }
            break;
        case MIR_CALL:
            {
                const struct MirInsn *string;
                int arguments[3];
                int actual_stack, expected_stack;
                int actual_width, expected_width;
                int actual_unsigned, expected_unsigned;
                long actual_offset, expected_offset;

                if (call_count >= 16 ||
                    !mir_machine_three_call_arguments(insn, arguments))
                    return 0;
                string = mir_definition(arguments[0]);
                if (string == NULL ||
                    string->opcode != MIR_STRING_ADDRESS ||
                    !mir_machine_flat_load(
                        arguments[1], &actual_stack, &actual_offset,
                        &actual_width, &actual_unsigned) ||
                    !mir_machine_flat_load(
                        arguments[2], &expected_stack, &expected_offset,
                        &expected_width, &expected_unsigned) ||
                    actual_stack != expected_stack ||
                    actual_offset != expected_offset ||
                    actual_width != expected_width ||
                    actual_unsigned != expected_unsigned ||
                    (actual_width < 4 &&
                     (actual_offset < -128 ||
                      actual_offset + actual_width - 1 > 127)))
                    return 0;
                if (call_count == 0) {
                    plan->check_function = find_global(insn->name);
                    plan->parameter_stack_offset = actual_stack;
                    plan->width = actual_width;
                    plan->is_unsigned = actual_unsigned;
                    if (plan->check_function == NULL ||
                        !plan->check_function->is_defined)
                        return 0;
                } else if (plan->check_function != find_global(insn->name) ||
                           plan->parameter_stack_offset != actual_stack ||
                           plan->width != actual_width ||
                           plan->is_unsigned != actual_unsigned) {
                    return 0;
                }
                plan->offsets[call_count] = (int)actual_offset;
                plan->strings[call_count] = (int)string->immediate;
                ++call_count;
            }
            break;
        default:
            return 0;
        }
    }
    plan->count = call_count;
    return call_count >= 2;
}

static int mir_match_global_byte_checks(
    struct MirGlobalByteChecks *plan)
{
    int call_count = 0;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        (mir.return_type & 15) != TYPE_VOID)
        return mir_machine_reject("global-byte-checks", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
        case MIR_CONST:
        case MIR_STRING_ADDRESS:
        case MIR_ADDRESS:
        case MIR_MEMBER_ADDRESS:
        case MIR_INDEX_ADDRESS:
        case MIR_LOAD:
        case MIR_LOAD_INDIRECT:
        case MIR_UNARY:
        case MIR_ARG:
            break;
        case MIR_CALL:
            {
                const struct MirInsn *string;
                long expected;
                int arguments[3];

                if (call_count >= 16 ||
                    !mir_machine_three_call_arguments(
                        insn, arguments) ||
                    !mir_machine_global_byte_value(
                        arguments[0],
                        &plan->symbols[call_count],
                        &plan->offsets[call_count],
                        &plan->is_unsigned[call_count]) ||
                    !mir_machine_constant_value(
                        arguments[1], &expected, 0))
                    return mir_machine_reject(
                        "global-byte-checks", "arguments");
                string = mir_definition(arguments[2]);
                if (string == NULL ||
                    string->opcode != MIR_STRING_ADDRESS ||
                    expected < -32768 || expected > 65535)
                    return mir_machine_reject(
                        "global-byte-checks", "expected");
                if (call_count == 0) {
                    plan->check_function = find_global(insn->name);
                    if (plan->check_function == NULL ||
                        !plan->check_function->is_defined ||
                        plan->check_function->is_funcptr ||
                        plan->check_function->is_noreturn)
                        return mir_machine_reject(
                            "global-byte-checks", "function");
                } else if (plan->check_function !=
                           find_global(insn->name)) {
                    return mir_machine_reject(
                        "global-byte-checks", "mixed-functions");
                }
                plan->expected[call_count] =
                    (int)((unsigned long)expected & 0xffffUL);
                plan->strings[call_count] = (int)string->immediate;
                ++call_count;
            }
            break;
        default:
            return mir_machine_reject(
                "global-byte-checks", "opcode");
        }
    }
    plan->count = call_count;
    return call_count >= 2;
}

static void mir_emit_flat_array_checks(
    MirStream *out, const struct MirFlatArrayChecks *plan)
{
    int check;

    mir_stream_printf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tpush de\n\tpop iy\n",
            plan->parameter_stack_offset + 2);
    for (check = 0; check < plan->count; ++check) {
        if (plan->width == 2) {
            mir_stream_printf(out,
                    "\tld l,(iy%+d)\n\tld h,(iy%+d)\n",
                    plan->offsets[check],
                    plan->offsets[check] + 1);
        } else if (plan->width == 4) {
            if (plan->offsets[check] >= -128 &&
                plan->offsets[check] + 3 <= 127) {
                mir_stream_printf(out,
                        "\tld l,(iy%+d)\n\tld h,(iy%+d)\n"
                        "\tld e,(iy%+d)\n\tld d,(iy%+d)\n",
                        plan->offsets[check],
                        plan->offsets[check] + 1,
                        plan->offsets[check] + 2,
                        plan->offsets[check] + 3);
            } else {
                mir_stream_puts("\tpush iy\n\tpop hl\n", out);
                mir_stream_printf(out,
                        "\tld de,%d\n\tadd hl,de\n"
                        "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
                        "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
                        "\tld l,c\n\tld h,b\n",
                        plan->offsets[check]);
            }
        } else if (plan->is_unsigned) {
            mir_stream_printf(out,
                    "\tld l,(iy%+d)\n\tld h,0\n",
                    plan->offsets[check]);
        } else {
            mir_stream_printf(out,
                    "\tld l,(iy%+d)\n"
                    "\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n",
                    plan->offsets[check]);
        }
        if (plan->width == 4)
            mir_stream_puts("\tpush de\n\tpush hl\n\tpush de\n\tpush hl\n", out);
        else
            mir_stream_puts("\tpush hl\n\tpush hl\n", out);
        mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[check]);
        mir_machine_emit_symbol_call(out, plan->check_function);
        mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n", out);
        if (plan->width == 4)
            mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    }
    mir_stream_puts("\tpop iy\n\tret\n", out);
}

static void mir_emit_global_byte_checks(
    MirStream *out, const struct MirGlobalByteChecks *plan)
{
    int check;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (check = 0; check < plan->count; ++check) {
        mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n"
                     "\tld hl,%d\n\tpush hl\n",
                plan->strings[check], plan->expected[check]);
        mir_machine_emit_global_byte_a(
            out, plan->symbols[check], plan->offsets[check], 0);
        mir_stream_puts("\tld l,a\n", out);
        if (plan->is_unsigned[check])
            mir_stream_puts("\tld h,0\n", out);
        else
            mir_stream_puts("\trlca\n\tsbc a,a\n\tld h,a\n", out);
        mir_stream_puts("\tpush hl\n", out);
        mir_machine_emit_symbol_call(out, plan->check_function);
        mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n", out);
    }
    mir_stream_puts("\tret\n", out);
}

static int mir_match_fixed_param_mutations(
    struct MirFixedParamMutations *plan)
{
    int instruction;

    memset(plan, 0, sizeof(*plan));
    plan->parameter_stack_offset = -1;
    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
        case MIR_PARAM:
        case MIR_CONST:
        case MIR_FLOAT_CONST:
        case MIR_INDEX_ADDRESS:
        case MIR_BINARY:
            break;
        case MIR_LOAD:
        case MIR_LOAD_INDIRECT:
            if ((insn->memory_flags & (1 | 8)) != 0)
                return 0;
            break;
        case MIR_STORE_INDIRECT:
            if (plan->count >= 8 ||
                !mir_machine_match_fixed_mutation(
                    insn, &plan->mutations[plan->count],
                    &plan->parameter_stack_offset))
                return 0;
            ++plan->count;
            break;
        default:
            return 0;
        }
    }
    return plan->count > 0 && plan->parameter_stack_offset >= 0;
}

static void mir_emit_fixed_param_mutations(
    MirStream *out, const struct MirFixedParamMutations *plan)
{
    int mutation;

    for (mutation = 0; mutation < plan->count; ++mutation)
        mir_machine_emit_fixed_mutation(
            out, plan, &plan->mutations[mutation]);
    mir_stream_puts("\tret\n", out);
}

static int mir_match_global_append(struct MirGlobalAppend *plan)
{
    struct Sym *increment_root = NULL;
    int increment_offset = 0;
    int increment_count = 0;
    int seen_increment = 0;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
        case MIR_PARAM:
        case MIR_CONST:
        case MIR_ADDRESS:
        case MIR_MEMBER_ADDRESS:
        case MIR_INDEX_ADDRESS:
        case MIR_UNARY:
        case MIR_BINARY:
            break;
        case MIR_LOAD_INDIRECT:
            if ((insn->memory_flags & (1 | 8)) != 0)
                return 0;
            break;
        case MIR_STORE_INDIRECT:
            if (mir_machine_match_global_increment(
                    insn, &increment_root, &increment_offset)) {
                ++increment_count;
                seen_increment = 1;
            } else {
                if (seen_increment || plan->store_count >= 8 ||
                    !mir_machine_match_global_append_store(
                        insn, plan, &plan->stores[plan->store_count]))
                    return 0;
                ++plan->store_count;
            }
            break;
        default:
            return 0;
        }
    }
    return plan->root != NULL && plan->store_count > 0 &&
           increment_count == 1 && increment_root == plan->root &&
           increment_offset == plan->count_offset;
}

static int mir_match_nested_append(struct MirNestedAppend *plan)
{
    struct MirRowMemberAddress increment_address;
    int increment_count = 0;
    int seen_increment = 0;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    memset(&increment_address, 0, sizeof(increment_address));
    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
        case MIR_PARAM:
        case MIR_CONST:
        case MIR_ADDRESS:
        case MIR_MEMBER_ADDRESS:
        case MIR_INDEX_ADDRESS:
        case MIR_UNARY:
        case MIR_BINARY:
            break;
        case MIR_LOAD:
        case MIR_STORE:
            if (!mir_machine_local_pointer_alias(insn))
                return 0;
            break;
        case MIR_LOAD_INDIRECT:
            if ((insn->memory_flags & (1 | 8)) != 0)
                return 0;
            break;
        case MIR_STORE_INDIRECT:
            if (mir_machine_match_nested_increment(
                    insn, &increment_address)) {
                ++increment_count;
                seen_increment = 1;
            } else {
                if (seen_increment || plan->store_count >= 8 ||
                    !mir_machine_match_nested_append_store(
                        insn, plan,
                        &plan->stores[plan->store_count]))
                    return 0;
                ++plan->store_count;
            }
            break;
        default:
            return 0;
        }
    }
    if (plan->root == NULL || plan->store_count == 0 ||
        increment_count != 1 ||
        increment_address.root != plan->root ||
        increment_address.root_pointer_offset !=
            plan->root_pointer_offset ||
        increment_address.row_stride != plan->row_stride ||
        increment_address.index_value != plan->row_index_value ||
        increment_address.member_offset !=
            plan->count_member_offset ||
        !mir_machine_parameter_offset(
            increment_address.index_value,
            &plan->row_index_stack_offset))
        return 0;
    return 1;
}

static int mir_match_indexed_stack(struct MirIndexedStack *plan)
{
    const struct MirInsn *index_store = NULL;
    const struct MirInsn *element_store = NULL;
    const struct MirInsn *return_insn = NULL;
    const struct MirInsn *element_load;
    struct MirStateMember index_member;
    struct MirStateMember base_member;
    int old_index = -1;
    int new_index = -1;
    int address_index = -1;
    int parameter_count = 0;
    int store_count = 0;
    int return_count = 0;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    memset(&index_member, 0, sizeof(index_member));
    memset(&base_member, 0, sizeof(base_member));
    if (mir.has_vla || mir_cfg_block_count() != 1)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
        case MIR_CONST:
        case MIR_MEMBER_ADDRESS:
        case MIR_INDEX_ADDRESS:
        case MIR_BINARY:
            break;
        case MIR_PARAM:
            ++parameter_count;
            break;
        case MIR_LOAD:
            if ((insn->memory_flags & (1 | 8)) != 0)
                return 0;
            break;
        case MIR_LOAD_INDIRECT:
            if ((insn->memory_flags & (1 | 8)) != 0 ||
                insn->bit_width != 0)
                return 0;
            break;
        case MIR_STORE_INDIRECT:
            if ((insn->memory_flags & (1 | 8)) != 0 ||
                insn->bit_width != 0)
                return 0;
            ++store_count;
            if (mir_definition(insn->src1) != NULL &&
                mir_definition(insn->src1)->opcode ==
                    MIR_INDEX_ADDRESS)
                element_store = insn;
            else
                index_store = insn;
            break;
        case MIR_RETURN:
            ++return_count;
            return_insn = insn;
            break;
        default:
            return 0;
        }
    }
    if (store_count == 2 && parameter_count == 1 &&
        return_count == 0 && element_store != NULL &&
        index_store != NULL &&
        (mir.return_type & 15) == TYPE_VOID) {
        plan->kind = MIR_INDEXED_STACK_PUSH;
        if (!mir_machine_index_update(
                index_store, '+', &index_member,
                &old_index, &new_index) ||
            !mir_machine_indexed_stack_address(
                element_store->src1, &base_member,
                &address_index, &plan->element_width) ||
            address_index != old_index ||
            index_store >= element_store ||
            element_store->memory_size != plan->element_width ||
            !mir_machine_parameter_value_offset(
                element_store->src2,
                &plan->value_stack_offset))
            return 0;
    } else if (store_count == 1 && parameter_count == 0 &&
               return_count == 1 && index_store != NULL &&
               return_insn != NULL &&
               type_size(mir.return_type) == 2) {
        plan->kind = MIR_INDEXED_STACK_POP;
        element_load = mir_definition(return_insn->src1);
        if (!mir_machine_index_update(
                index_store, '-', &index_member,
                &old_index, &new_index) ||
            element_load == NULL ||
            element_load->opcode != MIR_LOAD_INDIRECT ||
            element_load->bit_width != 0 ||
            (element_load->memory_flags & (1 | 8)) != 0 ||
            !mir_machine_indexed_stack_address(
                element_load->src1, &base_member,
                &address_index, &plan->element_width) ||
            address_index != new_index ||
            index_store >= element_load ||
            element_load->memory_size != plan->element_width ||
            type_size(mir.return_type) != plan->element_width)
            return 0;
    } else {
        return 0;
    }
    if (index_member.root != base_member.root ||
        index_member.root_offset != base_member.root_offset)
        return 0;
    plan->root = index_member.root;
    plan->root_offset = index_member.root_offset;
    plan->base_member_offset = base_member.member_offset;
    plan->index_member_offset = index_member.member_offset;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        int memory_type;
        int memory_storage;
        int memory_offset;

        if (insn->opcode != MIR_LOAD)
            continue;
        if (!mir_scalar_memory_location(
                insn, &memory_type, &memory_storage,
                &memory_offset) ||
            memory_storage != SC_GLOBAL ||
            memory_offset != plan->root_offset ||
            find_global(insn->name) != plan->root)
            return 0;
    }
    return 1;
}

static int mir_match_pointer_stack(struct MirPointerStack *plan)
{
    const struct MirInsn *pointer_store = NULL;
    const struct MirInsn *element_store = NULL;
    const struct MirInsn *return_insn = NULL;
    const struct MirInsn *element_load;
    const struct MirInsn *update_load = NULL;
    const struct MirInsn *data_pointer_load;
    struct MirStateMember update_member;
    struct MirStateMember data_member;
    int parameter_count = 0;
    int store_count = 0;
    int return_count = 0;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    memset(&update_member, 0, sizeof(update_member));
    memset(&data_member, 0, sizeof(data_member));
    if (mir.has_vla || mir_cfg_block_count() != 1)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
        case MIR_CONST:
        case MIR_MEMBER_ADDRESS:
        case MIR_UNARY:
        case MIR_BINARY:
            break;
        case MIR_PARAM:
            ++parameter_count;
            break;
        case MIR_LOAD:
            if ((insn->memory_flags & (1 | 8)) != 0)
                return 0;
            break;
        case MIR_LOAD_INDIRECT:
            if ((insn->memory_flags & (1 | 8)) != 0 ||
                insn->bit_width != 0)
                return 0;
            break;
        case MIR_STORE_INDIRECT:
            if ((insn->memory_flags & (1 | 8)) != 0 ||
                insn->bit_width != 0)
                return 0;
            ++store_count;
            if (mir_definition(insn->src1) != NULL &&
                mir_definition(insn->src1)->opcode ==
                    MIR_MEMBER_ADDRESS)
                pointer_store = insn;
            else
                element_store = insn;
            break;
        case MIR_RETURN:
            ++return_count;
            return_insn = insn;
            break;
        default:
            return 0;
        }
    }
    if (store_count == 2 && parameter_count == 1 &&
        return_count == 0 && pointer_store != NULL &&
        element_store != NULL &&
        (mir.return_type & 15) == TYPE_VOID) {
        plan->kind = MIR_POINTER_STACK_PUSH;
        if (!mir_machine_pointer_update(
                pointer_store, '+', &update_member,
                &update_load) ||
            !mir_machine_pointer_member_load(
                element_store->src1, &data_member) ||
            !mir_machine_same_state_member(
                &update_member, &data_member) ||
            element_store >= update_load ||
            update_load >= pointer_store ||
            element_store->memory_size != 2 ||
            !mir_machine_parameter_value_offset(
                element_store->src2,
                &plan->value_stack_offset))
            return 0;
    } else if (store_count == 1 && parameter_count == 0 &&
               return_count == 1 && pointer_store != NULL &&
               return_insn != NULL &&
               type_size(mir.return_type) == 2) {
        plan->kind = MIR_POINTER_STACK_POP;
        element_load = mir_definition(return_insn->src1);
        if (!mir_machine_pointer_update(
                pointer_store, '-', &update_member,
                &update_load) ||
            element_load == NULL ||
            element_load->opcode != MIR_LOAD_INDIRECT ||
            element_load->memory_size != 2 ||
            element_load->bit_width != 0 ||
            (element_load->memory_flags & (1 | 8)) != 0 ||
            !mir_machine_pointer_member_load(
                element_load->src1, &data_member) ||
            !mir_machine_same_state_member(
                &update_member, &data_member) ||
            (data_pointer_load =
                mir_definition(element_load->src1)) == NULL ||
            pointer_store >= data_pointer_load ||
            data_pointer_load >= element_load)
            return 0;
    } else {
        return 0;
    }
    plan->root = update_member.root;
    plan->root_offset = update_member.root_offset;
    plan->pointer_member_offset = update_member.member_offset;
    return mir_machine_only_root_loads(
        plan->root, plan->root_offset);
}

static int mir_match_byte_memory_stack(
    struct MirByteMemoryStack *plan)
{
    const struct MirInsn *cursor_store = NULL;
    const struct MirInsn *element_store = NULL;
    const struct MirInsn *return_insn = NULL;
    const struct MirInsn *element_load;
    int old_cursor = -1;
    int new_cursor = -1;
    int parameter_count = 0;
    int store_count = 0;
    int return_count = 0;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir_cfg_block_count() != 1)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
        case MIR_CONST:
        case MIR_ADDRESS:
        case MIR_MEMBER_ADDRESS:
        case MIR_UNARY:
        case MIR_BINARY:
            break;
        case MIR_PARAM:
            ++parameter_count;
            break;
        case MIR_LOAD_INDIRECT:
            if ((insn->memory_flags & (1 | 8)) != 0 ||
                insn->bit_width != 0)
                return 0;
            break;
        case MIR_STORE_INDIRECT:
            if ((insn->memory_flags & (1 | 8)) != 0 ||
                insn->bit_width != 0)
                return 0;
            ++store_count;
            if (mir_definition(insn->src1) != NULL &&
                mir_definition(insn->src1)->opcode ==
                    MIR_MEMBER_ADDRESS)
                cursor_store = insn;
            else
                element_store = insn;
            break;
        case MIR_RETURN:
            ++return_count;
            return_insn = insn;
            break;
        default:
            return 0;
        }
    }
    if (store_count == 2 && parameter_count == 1 &&
        return_count == 0 && cursor_store != NULL &&
        element_store != NULL &&
        (mir.return_type & 15) == TYPE_VOID) {
        plan->kind = MIR_BYTE_MEMORY_STACK_PUSH;
        if (!mir_machine_byte_cursor_update(
                cursor_store, '-', &plan->cursor_root,
                &plan->cursor_offset, &old_cursor,
                &new_cursor, NULL) ||
            !mir_machine_byte_stack_address(
                element_store->src1, old_cursor,
                &plan->memory_root, &plan->memory_offset) ||
            cursor_store >= element_store ||
            element_store->memory_size != 1 ||
            !mir_machine_parameter_value_offset(
                element_store->src2,
                &plan->value_stack_offset))
            return 0;
    } else if (store_count == 1 && parameter_count == 0 &&
               return_count == 1 && cursor_store != NULL &&
               return_insn != NULL &&
               type_size(mir.return_type) == 1 &&
               (mir.return_type & TYPE_UNSIGNED) != 0) {
        plan->kind = MIR_BYTE_MEMORY_STACK_POP;
        element_load = mir_definition(return_insn->src1);
        if (!mir_machine_byte_cursor_update(
                cursor_store, '+', &plan->cursor_root,
                &plan->cursor_offset, &old_cursor,
                &new_cursor, NULL) ||
            element_load == NULL ||
            element_load->opcode != MIR_LOAD_INDIRECT ||
            element_load->memory_size != 1 ||
            element_load->bit_width != 0 ||
            (element_load->memory_flags & (1 | 8)) != 0 ||
            !mir_machine_byte_stack_address(
                element_load->src1, new_cursor,
                &plan->memory_root, &plan->memory_offset) ||
            cursor_store >= element_load)
            return 0;
    } else {
        return 0;
    }
    return plan->memory_root != NULL &&
           plan->cursor_root != NULL;
}

static int mir_match_word_memory_stack_push(
    struct MirByteMemoryStack *plan)
{
    const struct MirInsn *cursor_stores[2] = { NULL, NULL };
    const struct MirInsn *element_store = NULL;
    const struct MirInsn *second_load = NULL;
    struct Sym *first_cursor_root;
    struct Sym *second_cursor_root;
    int first_cursor_offset;
    int second_cursor_offset;
    int first_old;
    int first_new;
    int second_old;
    int second_new;
    int cursor_store_count = 0;
    int parameter_count = 0;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
        case MIR_CONST:
        case MIR_ADDRESS:
        case MIR_MEMBER_ADDRESS:
        case MIR_UNARY:
        case MIR_BINARY:
            break;
        case MIR_PARAM:
            ++parameter_count;
            break;
        case MIR_LOAD_INDIRECT:
            if ((insn->memory_flags & (1 | 8)) != 0 ||
                insn->bit_width != 0)
                return 0;
            break;
        case MIR_STORE_INDIRECT:
            if ((insn->memory_flags & (1 | 8)) != 0 ||
                insn->bit_width != 0)
                return 0;
            if (mir_definition(insn->src1) != NULL &&
                mir_definition(insn->src1)->opcode ==
                    MIR_MEMBER_ADDRESS) {
                if (cursor_store_count >= 2)
                    return 0;
                cursor_stores[cursor_store_count++] = insn;
            } else {
                if (element_store != NULL)
                    return 0;
                element_store = insn;
            }
            break;
        default:
            return 0;
        }
    }
    if (parameter_count != 1 || cursor_store_count != 2 ||
        element_store == NULL ||
        !mir_machine_byte_cursor_update(
            cursor_stores[0], '-', &first_cursor_root,
            &first_cursor_offset, &first_old, &first_new, NULL) ||
        !mir_machine_byte_cursor_update(
            cursor_stores[1], '-', &second_cursor_root,
            &second_cursor_offset, &second_old, &second_new,
            &second_load) ||
        first_cursor_root != second_cursor_root ||
        first_cursor_offset != second_cursor_offset ||
        cursor_stores[0] >= element_store ||
        element_store >= second_load ||
        second_load >= cursor_stores[1] ||
        element_store->memory_size != 2 ||
        !mir_machine_byte_stack_address(
            element_store->src1, first_new,
            &plan->memory_root, &plan->memory_offset) ||
        !mir_machine_parameter_value_offset(
            element_store->src2, &plan->value_stack_offset))
        return 0;
    plan->kind = MIR_BYTE_MEMORY_STACK_PUSH_WORD;
    plan->cursor_root = first_cursor_root;
    plan->cursor_offset = first_cursor_offset;
    return 1;
}

static int mir_match_fixed_array_reduction(
    struct MirFixedArrayReduction *plan)
{
    const struct MirInsn *parameter = NULL;
    const struct MirInsn *element_load = NULL;
    const struct MirInsn *return_insn = NULL;
    const struct MirInsn *sum_update_store = NULL;
    const struct MirInsn *sum_init_store = NULL;
    const struct MirInsn *outer_phi = NULL;
    const struct MirInsn *previous_index = NULL;
    int index_objects[6];
    int init_positions[6];
    int compare_positions[6];
    int branch_positions[6];
    int increment_positions[6];
    int jump_positions[6];
    int exit_positions[6];
    int header_labels[6];
    int init_values[6];
    int increment_values[6];
    int sum_object = -1;
    int sum_init_position = -1;
    int sum_update_position = -1;
    int parameter_count = 0;
    int index_count = 0;
    int label_count = 0;
    int phi_count = 0;
    int binary_count = 0;
    int branch_count = 0;
    int jump_count = 0;
    int return_count = 0;
    int load_indirect_count = 0;
    int store_count = 0;
    int instruction;
    int loop;

    memset(plan, 0, sizeof(*plan));
    for (loop = 0; loop < 6; ++loop) {
        index_objects[loop] = -1;
        init_positions[loop] = -1;
        compare_positions[loop] = -1;
        branch_positions[loop] = -1;
        increment_positions[loop] = -1;
        jump_positions[loop] = -1;
        exit_positions[loop] = -1;
        header_labels[loop] = -1;
        init_values[loop] = -1;
        increment_values[loop] = -1;
    }
    if (mir.has_vla || mir_cfg_block_count() != 19 ||
        type_ptr_depth(mir.return_type) != 0 ||
        type_is_float(mir.return_type))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
            break;
        case MIR_LABEL:
            ++label_count;
            break;
        case MIR_PARAM:
            ++parameter_count;
            parameter = insn;
            break;
        case MIR_CONST:
            if (insn->immediate < 0 || insn->immediate > 2)
                return 0;
            break;
        case MIR_LOAD:
            if (!mir_machine_named_nonvolatile(insn))
                return 0;
            break;
        case MIR_STORE:
            if (!mir_machine_unobservable_local_store(insn))
                return 0;
            ++store_count;
            break;
        case MIR_PHI:
            ++phi_count;
            outer_phi = insn;
            break;
        case MIR_BINARY:
            ++binary_count;
            break;
        case MIR_BRANCH_FALSE:
            ++branch_count;
            break;
        case MIR_JUMP:
            ++jump_count;
            break;
        case MIR_INDEX_ADDRESS:
            if (index_count >= 6 ||
                (index_count == 0
                     ? parameter == NULL ||
                       insn->src1 != parameter->dst
                     : previous_index == NULL ||
                       insn->src1 != previous_index->dst)) {
                return 0;
            }
            index_objects[index_count] =
                mir_machine_value_object(insn->src2);
            if (index_objects[index_count] < 0)
                return 0;
            previous_index = insn;
            ++index_count;
            break;
        case MIR_LOAD_INDIRECT:
            if (element_load != NULL || insn->bit_width != 0 ||
                (insn->memory_flags & (1 | 8)) != 0)
                return 0;
            element_load = insn;
            ++load_indirect_count;
            break;
        case MIR_UNARY:
            if (insn->immediate != 0)
                return 0;
            break;
        case MIR_RETURN:
            ++return_count;
            return_insn = insn;
            break;
        default:
            return 0;
        }
    }
    if (parameter_count != 1 || parameter == NULL ||
        type_ptr_depth(parameter->type) == 0 ||
        !mir_machine_parameter_offset(
            parameter->dst, &plan->parameter_stack_offset) ||
        index_count != 6 || previous_index == NULL ||
        element_load == NULL || load_indirect_count != 1 ||
        element_load->src1 != previous_index->dst ||
        (element_load->memory_size != 1 &&
         element_load->memory_size != 2 &&
         element_load->memory_size != 4) ||
        return_count != 1 || return_insn == NULL ||
        label_count != 19 || phi_count != 1 ||
        binary_count != 13 || branch_count != 6 ||
        jump_count != 6 || store_count != 14)
        return 0;
    plan->element_width = element_load->memory_size;
    plan->element_is_unsigned = 0;
    if ((plan->element_width == 1 &&
         type_size(mir.return_type) != 2) ||
        (plan->element_width != 1 &&
         type_size(mir.return_type) != plan->element_width))
        return 0;
    previous_index = NULL;
    index_count = 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode != MIR_INDEX_ADDRESS)
            continue;
        if (insn->immediate !=
            plan->element_width * (1 << (5 - index_count)))
            return 0;
        previous_index = insn;
        ++index_count;
    }
    {
        const struct MirInsn *return_value =
            mir_definition(return_insn->src1);
        if (return_value == NULL ||
            return_value->opcode != MIR_LOAD ||
            !mir_machine_named_nonvolatile(return_value))
            return 0;
        sum_object = return_value->object;
    }
    if (sum_object < 0)
        return 0;
    for (loop = 0; loop < 6; ++loop) {
        int other;

        if (index_objects[loop] < 0 ||
            index_objects[loop] == sum_object)
            return 0;
        for (other = 0; other < loop; ++other)
            if (index_objects[other] == index_objects[loop])
                return 0;
    }
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode == MIR_STORE) {
            const struct MirInsn *value = mir_definition(insn->src1);
            int object = insn->object;
            int object_loop = -1;

            for (loop = 0; loop < 6; ++loop)
                if (index_objects[loop] == object)
                    object_loop = loop;
            if (object == sum_object) {
                if (mir_machine_constant_equals(insn->src1, 0)) {
                    if (sum_init_store != NULL)
                        return 0;
                    sum_init_store = insn;
                    sum_init_position = instruction;
                } else {
                    const struct MirInsn *left;
                    const struct MirInsn *right;

                    if (sum_update_store != NULL || value == NULL ||
                        value->opcode != MIR_BINARY ||
                        value->immediate != '+')
                        return 0;
                    left = mir_definition(value->src1);
                    right = mir_definition(value->src2);
                    if (left == NULL || left->opcode != MIR_LOAD ||
                        left->object != sum_object ||
                        !mir_machine_reduction_operand(
                            value->src2, element_load,
                            type_size(mir.return_type),
                            &plan->element_is_unsigned)) {
                        if (right == NULL ||
                            right->opcode != MIR_LOAD ||
                            right->object != sum_object ||
                            !mir_machine_reduction_operand(
                                value->src1, element_load,
                                type_size(mir.return_type),
                                &plan->element_is_unsigned))
                            return 0;
                    }
                    sum_update_store = insn;
                    sum_update_position = instruction;
                }
            } else if (object_loop >= 0) {
                if (mir_machine_constant_equals(insn->src1, 0)) {
                    if (init_positions[object_loop] >= 0)
                        return 0;
                    init_positions[object_loop] = instruction;
                    init_values[object_loop] = insn->src1;
                } else {
                    const struct MirInsn *add = value;

                    if (increment_positions[object_loop] >= 0 ||
                        add == NULL || add->opcode != MIR_BINARY ||
                        add->immediate != '+' ||
                        mir_machine_value_object(add->src1) != object ||
                        !mir_machine_constant_equals(add->src2, 1))
                        return 0;
                    increment_positions[object_loop] = instruction;
                    increment_values[object_loop] = add->dst;
                }
            } else {
                return 0;
            }
        } else if (insn->opcode == MIR_BINARY &&
                   insn->immediate == '<') {
            int object = mir_machine_value_object(insn->src1);

            for (loop = 0; loop < 6; ++loop)
                if (index_objects[loop] == object)
                    break;
            if (loop == 6 ||
                compare_positions[loop] >= 0 ||
                !mir_machine_constant_equals(insn->src2, 2) ||
                !mir_machine_find_branch_for_value(
                    insn->dst, &branch_positions[loop]) ||
                !mir_machine_find_header_before(
                    instruction, &header_labels[loop]))
                return 0;
            compare_positions[loop] = instruction;
        }
    }
    if (sum_init_store == NULL || sum_update_store == NULL ||
        sum_init_position < 0 || sum_update_position < 0 ||
        outer_phi == NULL ||
        outer_phi->object != index_objects[0] ||
        outer_phi->src1 != init_values[0] ||
        outer_phi->src2 != increment_values[0])
        return 0;
    for (loop = 0; loop < 6; ++loop) {
        int jump;

        if (init_positions[loop] < 0 ||
            compare_positions[loop] < 0 ||
            branch_positions[loop] != compare_positions[loop] + 1 ||
            increment_positions[loop] < 0)
            return 0;
        jump = increment_positions[loop] + 1;
        if (jump >= mir.count ||
            mir.insns[jump].opcode != MIR_JUMP ||
            mir.insns[jump].label != header_labels[loop])
            return 0;
        jump_positions[loop] = jump;
        exit_positions[loop] =
            mir_find_label(mir.insns[branch_positions[loop]].label);
        if (exit_positions[loop] <= jump_positions[loop])
            return 0;
        if (loop == 0) {
            if (init_positions[loop] >= compare_positions[loop])
                return 0;
        } else if (branch_positions[loop - 1] >=
                       init_positions[loop] ||
                   init_positions[loop] >=
                       compare_positions[loop]) {
            return 0;
        }
    }
    if (sum_init_position >= init_positions[0] ||
        sum_init_position >= compare_positions[0])
        return 0;
    if (branch_positions[5] >= sum_update_position ||
        sum_update_position >= increment_positions[5])
        return 0;
    for (loop = 5; loop > 0; --loop)
        if (exit_positions[loop] >= increment_positions[loop - 1])
            return 0;
    return exit_positions[0] <
           (int)(return_insn - mir.insns);
}

static int mir_match_fixed_array_affine_fill(
    struct MirFixedArrayAffineFill *plan)
{
    const struct MirInsn *pointer;
    const struct MirInsn *base;
    const struct MirInsn *call = NULL;
    const struct MirInsn *store_indirect = NULL;
    const struct MirInsn *outer_phi = NULL;
    const struct MirInsn *previous_index = NULL;
    int index_objects[6];
    int init_positions[6];
    int compare_positions[6];
    int branch_positions[6];
    int increment_positions[6];
    int exit_positions[6];
    int header_labels[6];
    int init_values[6];
    int increment_values[6];
    int arguments[6];
    int parameter_count = 0;
    int label_count = 0;
    int phi_count = 0;
    int binary_count = 0;
    int branch_count = 0;
    int jump_count = 0;
    int index_count = 0;
    int argument_count = 0;
    int call_count = 0;
    int store_count = 0;
    int store_indirect_count = 0;
    int instruction;
    int loop;

    memset(plan, 0, sizeof(*plan));
    for (loop = 0; loop < 6; ++loop) {
        index_objects[loop] = -1;
        init_positions[loop] = -1;
        compare_positions[loop] = -1;
        branch_positions[loop] = -1;
        increment_positions[loop] = -1;
        exit_positions[loop] = -1;
        header_labels[loop] = -1;
        init_values[loop] = -1;
        increment_values[loop] = -1;
    }
    if (mir.has_vla || mir_cfg_block_count() != 19 ||
        (mir.return_type & 15) != TYPE_VOID ||
        mir.count < 150 || mir.count > 160 ||
        mir.insns[0].opcode != MIR_LABEL ||
        mir.insns[1].opcode != MIR_PARAM ||
        mir.insns[2].opcode != MIR_PARAM)
        return 0;
    pointer = &mir.insns[1];
    base = &mir.insns[2];
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
            break;
        case MIR_LABEL:
            ++label_count;
            break;
        case MIR_PARAM:
            ++parameter_count;
            break;
        case MIR_CONST:
            if (insn->immediate < 0 || insn->immediate > 2)
                return 0;
            break;
        case MIR_LOAD:
            if (!mir_machine_named_nonvolatile(insn))
                return 0;
            break;
        case MIR_STORE:
            if (!mir_machine_unobservable_local_store(insn))
                return 0;
            ++store_count;
            break;
        case MIR_PHI:
            outer_phi = insn;
            ++phi_count;
            break;
        case MIR_UNARY:
            if (insn->immediate != 0)
                return 0;
            break;
        case MIR_BINARY:
            ++binary_count;
            break;
        case MIR_BRANCH_FALSE:
            ++branch_count;
            break;
        case MIR_JUMP:
            ++jump_count;
            break;
        case MIR_INDEX_ADDRESS:
            if (index_count >= 6 ||
                (index_count == 0
                     ? insn->src1 != pointer->dst
                     : previous_index == NULL ||
                       insn->src1 != previous_index->dst))
                return 0;
            index_objects[index_count] =
                mir_machine_value_object(insn->src2);
            if (index_objects[index_count] < 0)
                return 0;
            previous_index = insn;
            ++index_count;
            break;
        case MIR_ARG:
            ++argument_count;
            break;
        case MIR_CALL:
            call = insn;
            ++call_count;
            break;
        case MIR_STORE_INDIRECT:
            if (insn->bit_width != 0 ||
                (insn->memory_flags & (1 | 8)) != 0)
                return 0;
            store_indirect = insn;
            ++store_indirect_count;
            break;
        default:
            return 0;
        }
    }
    if (parameter_count != 2 || label_count != 19 ||
        phi_count != 1 || binary_count != 13 ||
        branch_count != 6 || jump_count != 6 ||
        index_count != 6 || argument_count != 6 ||
        call_count != 1 || store_count != 12 ||
        store_indirect_count != 1 ||
        call == NULL || store_indirect == NULL ||
        outer_phi == NULL || previous_index == NULL ||
        type_ptr_depth(pointer->type) != 1 ||
        mir_machine_pointee_is_volatile(pointer) ||
        type_ptr_depth(base->type) != 0 ||
        type_is_float(base->type) ||
        type_ptr_depth(call->type) != 0 ||
        type_size(call->type) != 2 ||
        type_is_float(call->type) ||
        (call->type & TYPE_UNSIGNED) != 0 ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0 ||
        store_indirect->src1 != previous_index->dst ||
        (store_indirect->memory_size != 1 &&
         store_indirect->memory_size != 2 &&
         store_indirect->memory_size != 4))
        return 0;
    plan->element_width = store_indirect->memory_size;
    if ((pointer->type & 15) !=
            (plan->element_width == 1
                 ? TYPE_CHAR
                 : plan->element_width == 2
                       ? TYPE_INT : TYPE_LONG) ||
        type_size(base->type) !=
            (plan->element_width == 4 ? 4 : 2) ||
        (base->type & TYPE_UNSIGNED) != 0)
        return 0;
    previous_index = NULL;
    index_count = 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode != MIR_INDEX_ADDRESS)
            continue;
        if (insn->src1 !=
                (previous_index == NULL
                     ? pointer->dst : previous_index->dst) ||
            insn->immediate !=
                plan->element_width * (1 << (5 - index_count)) ||
            insn->memory_size != plan->element_width)
            return 0;
        previous_index = insn;
        ++index_count;
    }
    if (!mir_machine_six_call_arguments(call, arguments))
        return 0;
    for (loop = 0; loop < 6; ++loop)
        if (mir_machine_value_object(arguments[loop]) !=
            index_objects[loop])
            return 0;
    {
        const struct MirInsn *stored =
            mir_definition(store_indirect->src2);
        const struct MirInsn *addition;
        const struct MirInsn *converted_call = NULL;

        if (plan->element_width == 1) {
            if (stored == NULL || stored->opcode != MIR_UNARY ||
                stored->immediate != 0 ||
                type_size(stored->type) != 1)
                return 0;
            addition = mir_definition(stored->src1);
        } else {
            addition = stored;
        }
        if (addition == NULL || addition->opcode != MIR_BINARY ||
            addition->immediate != '+' ||
            type_size(addition->type) !=
                (plan->element_width == 4 ? 4 : 2))
            return 0;
        if (plan->element_width == 4) {
            int call_value;

            if (addition->src1 == base->dst)
                call_value = addition->src2;
            else if (addition->src2 == base->dst)
                call_value = addition->src1;
            else
                return 0;
            converted_call = mir_definition(call_value);
            if (converted_call == NULL ||
                converted_call->opcode != MIR_UNARY ||
                converted_call->immediate != 0 ||
                converted_call->src1 != call->dst ||
                type_size(converted_call->type) != 4 ||
                type_is_float(converted_call->type) ||
                (converted_call->type & TYPE_UNSIGNED) != 0)
                return 0;
        } else if (!((addition->src1 == base->dst &&
                      addition->src2 == call->dst) ||
                     (addition->src2 == base->dst &&
                      addition->src1 == call->dst))) {
            return 0;
        }
    }
    plan->function = find_global(call->name);
    if (plan->function == NULL ||
        !plan->function->is_defined ||
        (call->base_name[0] != 0 &&
         strcmp(call->base_name,
                asm_name_for(
                    sym_asm_name(plan->function)))) ||
        !mir_machine_parameter_value_offset(
            pointer->dst, &plan->pointer_stack_offset))
        return 0;
    if (plan->element_width == 4) {
        if (!mir_machine_wide_parameter_offset(
                base->dst, &plan->base_stack_offset))
            return 0;
    } else if (!mir_machine_parameter_value_offset(
                   base->dst, &plan->base_stack_offset)) {
        return 0;
    }
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode == MIR_STORE) {
            const struct MirInsn *value = mir_definition(insn->src1);
            int object_loop = -1;

            for (loop = 0; loop < 6; ++loop)
                if (index_objects[loop] == insn->object)
                    object_loop = loop;
            if (object_loop < 0)
                return 0;
            if (mir_machine_constant_equals(insn->src1, 0)) {
                if (init_positions[object_loop] >= 0)
                    return 0;
                init_positions[object_loop] = instruction;
                init_values[object_loop] = insn->src1;
            } else {
                if (increment_positions[object_loop] >= 0 ||
                    value == NULL || value->opcode != MIR_BINARY ||
                    value->immediate != '+' ||
                    mir_machine_value_object(value->src1) !=
                        insn->object ||
                    !mir_machine_constant_equals(value->src2, 1))
                    return 0;
                increment_positions[object_loop] = instruction;
                increment_values[object_loop] = value->dst;
            }
        } else if (insn->opcode == MIR_BINARY &&
                   insn->immediate == '<') {
            int object = mir_machine_value_object(insn->src1);

            for (loop = 0; loop < 6; ++loop)
                if (index_objects[loop] == object)
                    break;
            if (loop == 6 || compare_positions[loop] >= 0 ||
                !mir_machine_constant_equals(insn->src2, 2) ||
                !mir_machine_find_branch_for_value(
                    insn->dst, &branch_positions[loop]) ||
                !mir_machine_find_header_before(
                    instruction, &header_labels[loop]))
                return 0;
            compare_positions[loop] = instruction;
        }
    }
    if (outer_phi->object != index_objects[0] ||
        outer_phi->src1 != init_values[0] ||
        outer_phi->src2 != increment_values[0] ||
        outer_phi->phi_pred1 != mir.insns[0].label)
        return 0;
    for (loop = 0; loop < 6; ++loop) {
        int jump;

        if (init_positions[loop] < 0 ||
            compare_positions[loop] < 0 ||
            branch_positions[loop] != compare_positions[loop] + 1 ||
            increment_positions[loop] < 0)
            return 0;
        jump = increment_positions[loop] + 1;
        if (jump >= mir.count ||
            mir.insns[jump].opcode != MIR_JUMP ||
            mir.insns[jump].label != header_labels[loop])
            return 0;
        exit_positions[loop] =
            mir_find_label(mir.insns[branch_positions[loop]].label);
        if (exit_positions[loop] <= jump)
            return 0;
        if (loop == 0) {
            if (init_positions[loop] >= compare_positions[loop])
                return 0;
        } else if (branch_positions[loop - 1] >=
                       init_positions[loop] ||
                   init_positions[loop] >=
                       compare_positions[loop]) {
            return 0;
        }
    }
    if (branch_positions[5] >=
            (int)(call - mir.insns) ||
        (int)(call - mir.insns) >=
            (int)(store_indirect - mir.insns) ||
        (int)(store_indirect - mir.insns) >=
            increment_positions[5])
        return 0;
    for (loop = 5; loop > 0; --loop)
        if (exit_positions[loop] >=
            increment_positions[loop - 1])
            return 0;
    return 1;
}

static int mir_match_nested_row_store(struct MirNestedRowStore *plan)
{
    const struct MirInsn *value_store = NULL;
    const struct MirInsn *count_store = NULL;
    const struct MirInsn *destination;
    const struct MirInsn *count_load;
    const struct MirInsn *increment;
    const struct MirInsn *one;
    struct MirRowMemberAddress value_member;
    struct MirRowMemberAddress loaded_count_member;
    struct MirRowMemberAddress stored_count_member;
    int store_count = 0;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        (mir.return_type & 15) != TYPE_VOID)
        return mir_machine_reject("nested-row-store", "preflight");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
        case MIR_PARAM:
        case MIR_CONST:
        case MIR_ADDRESS:
        case MIR_MEMBER_ADDRESS:
        case MIR_LOAD_INDIRECT:
        case MIR_INDEX_ADDRESS:
        case MIR_BINARY:
            break;
        case MIR_LOAD:
            if (!mir_machine_named_nonvolatile(insn))
                return mir_machine_reject(
                    "nested-row-store", "load");
            break;
        case MIR_STORE:
            if (!mir_machine_unobservable_local_store(insn))
                return mir_machine_reject(
                    "nested-row-store", "store");
            break;
        case MIR_STORE_INDIRECT:
            if (++store_count > 2)
                return mir_machine_reject(
                    "nested-row-store", "store-count");
            if (mir_definition(insn->src2) != NULL &&
                mir_definition(insn->src2)->opcode == MIR_BINARY)
                count_store = insn;
            else
                value_store = insn;
            break;
        default:
            return mir_machine_reject("nested-row-store", "opcode");
        }
    }
    if (store_count != 2 || value_store == NULL || count_store == NULL)
        return mir_machine_reject("nested-row-store", "stores");
    destination = mir_definition(value_store->src1);
    if (destination == NULL ||
        destination->opcode != MIR_INDEX_ADDRESS ||
        destination->immediate <= 0 ||
        destination->memory_size != 2 ||
        !mir_machine_row_member_address(
            destination->src1, &value_member))
        return mir_machine_reject("nested-row-store", "destination");
    count_load = mir_definition(destination->src2);
    if (count_load == NULL || count_load->opcode != MIR_LOAD_INDIRECT ||
        count_load->memory_size != 2)
        return mir_machine_reject("nested-row-store", "count-load");
    if (!mir_machine_row_member_address(
            count_load->src1, &loaded_count_member) ||
        !mir_machine_same_row(&value_member, &loaded_count_member))
        return mir_machine_reject(
            "nested-row-store", "loaded-count-row");
    if (!mir_machine_row_member_address(
            count_store->src1, &stored_count_member) ||
        !mir_machine_same_row(&value_member, &stored_count_member) ||
        stored_count_member.member_offset !=
            loaded_count_member.member_offset)
        return mir_machine_reject(
            "nested-row-store", "stored-count-row");
    increment = mir_definition(count_store->src2);
    if (increment == NULL || increment->opcode != MIR_BINARY ||
        increment->immediate != '+' ||
        increment->src1 < 0 || increment->src2 < 0)
        return mir_machine_reject("nested-row-store", "increment");
    one = mir_definition(increment->src2);
    if (one == NULL || one->opcode != MIR_CONST ||
        one->immediate != 1)
        return mir_machine_reject("nested-row-store", "increment-one");
    {
        const struct MirInsn *increment_source =
            mir_definition(increment->src1);
        if (increment_source == NULL ||
            increment_source->opcode != MIR_LOAD_INDIRECT ||
            increment_source->src1 != count_store->src1 ||
            increment_source->memory_size != 2)
            return mir_machine_reject(
                "nested-row-store", "increment-source");
    }
    if (!mir_machine_parameter_offset(
            value_member.index_value, &plan->index_stack_offset) ||
        !mir_machine_parameter_offset(
            value_store->src2, &plan->value_stack_offset))
        return mir_machine_reject("nested-row-store", "parameters");
    plan->root = value_member.root;
    plan->root_pointer_offset = value_member.root_pointer_offset;
    plan->row_stride = value_member.row_stride;
    plan->value_member_offset = value_member.member_offset;
    plan->count_member_offset = loaded_count_member.member_offset;
    plan->element_stride = (int)destination->immediate;
    return 1;
}

static void mir_emit_global_append(
    MirStream *out, const struct MirGlobalAppend *plan)
{
    int current_offset = 0;
    int store;

    mir_machine_emit_global_word(out, plan->root, plan->count_offset);
    for (store = 1; store < plan->element_stride; store <<= 1)
        mir_stream_puts("\tadd hl,hl\n", out);
    mir_machine_emit_global_address_de(
        out, plan->root, plan->array_offset);
    mir_stream_puts("\tadd hl,de\n\tld c,l\n\tld b,h\n", out);
    for (store = 0; store < plan->store_count; ++store) {
        const struct MirGlobalAppendStore *append_store =
            &plan->stores[store];

        mir_machine_emit_bc_offset(
            out, append_store->field_offset - current_offset);
        mir_machine_emit_parameter_store_to_bc(out, append_store);
        current_offset = append_store->field_offset +
            (append_store->width == 2 ? 1 : 0);
    }
    mir_machine_emit_global_word(out, plan->root, plan->count_offset);
    mir_stream_puts("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->root, plan->count_offset);
    mir_stream_puts("\tret\n", out);
}

static void mir_emit_nested_append(
    MirStream *out, const struct MirNestedAppend *plan)
{
    int done = new_label();
    int store;

    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld l,e\n\tld h,d\n",
            plan->row_index_stack_offset);
    mir_emit_mul_hl_const(out, (unsigned long)plan->row_stride);
    mir_stream_puts("\tld c,l\n\tld b,h\n", out);
    mir_machine_emit_global_word(
        out, plan->root, plan->root_pointer_offset);
    mir_stream_puts("\tadd hl,bc\n\tld c,l\n\tld b,h\n", out);
    for (store = 0; store < plan->store_count; ++store) {
        const struct MirNestedAppendStore *append_store =
            &plan->stores[store];
        struct MirGlobalAppendStore parameter_store;

        mir_stream_puts("\tld l,c\n\tld h,b\n", out);
        mir_machine_emit_hl_offset(
            out, plan->count_member_offset, 1);
        mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
              "\tld l,e\n\tld h,d\n", out);
        mir_emit_mul_hl_const(
            out, (unsigned long)append_store->element_stride);
        mir_machine_emit_hl_offset(
            out, append_store->array_member_offset, 1);
        mir_stream_puts("\tadd hl,bc\n\tpush bc\n"
              "\tld c,l\n\tld b,h\n", out);
        parameter_store.parameter_stack_offset =
            append_store->parameter_stack_offset + 2;
        parameter_store.field_offset = 0;
        parameter_store.width = append_store->width;
        mir_machine_emit_parameter_store_to_bc(
            out, &parameter_store);
        mir_stream_puts("\tpop bc\n", out);
    }
    mir_stream_puts("\tld l,c\n\tld h,b\n", out);
    mir_machine_emit_hl_offset(
        out, plan->count_member_offset, 0);
    mir_stream_puts("\tinc (hl)\n", out);
    mir_stream_printf(out, "\tjp nz, L%d\n\tinc hl\n\tinc (hl)\nL%d:\n\tret\n",
            done, done);
}

static void mir_emit_indexed_stack(
    MirStream *out, const struct MirIndexedStack *plan)
{
    if (plan->kind == MIR_INDEXED_STACK_PUSH) {
        mir_stream_printf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
                "\tpush de\n",
                plan->value_stack_offset);
    }
    mir_machine_emit_indexed_stack_base(out, plan);
    if (plan->kind == MIR_INDEXED_STACK_PUSH)
        mir_stream_puts("\tinc de\n", out);
    else
        mir_stream_puts("\tdec de\n", out);
    mir_stream_puts("\tld (hl),d\n\tdec hl\n\tld (hl),e\n", out);
    if (plan->kind == MIR_INDEXED_STACK_PUSH)
        mir_stream_puts("\tdec de\n", out);
    mir_stream_puts("\tld l,e\n\tld h,d\n\tadd hl,hl\n"
          "\tpop de\n\tadd hl,de\n", out);
    if (plan->kind == MIR_INDEXED_STACK_PUSH) {
        mir_stream_puts("\tpop de\n\tld (hl),e\n\tinc hl\n"
              "\tld (hl),d\n\tret\n", out);
    } else {
        mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
              "\tex de,hl\n\tret\n", out);
    }
}

static void mir_emit_pointer_stack(
    MirStream *out, const struct MirPointerStack *plan)
{
    mir_machine_emit_global_word(
        out, plan->root, plan->root_offset);
    mir_machine_emit_hl_offset(
        out, plan->pointer_member_offset, 0);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n", out);
    if (plan->kind == MIR_POINTER_STACK_PUSH) {
        mir_stream_printf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
                "\tld a,c\n\tld (de),a\n\tinc de\n"
                "\tld a,b\n\tld (de),a\n",
                plan->value_stack_offset);
        mir_machine_emit_global_word(
            out, plan->root, plan->root_offset);
        mir_machine_emit_hl_offset(
            out, plan->pointer_member_offset, 0);
        mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
              "\tinc de\n\tinc de\n"
              "\tld (hl),d\n\tdec hl\n"
              "\tld (hl),e\n\tret\n", out);
    } else {
        mir_stream_puts("\tdec de\n\tdec de\n"
              "\tld (hl),d\n\tdec hl\n\tld (hl),e\n", out);
        mir_machine_emit_global_word(
            out, plan->root, plan->root_offset);
        mir_machine_emit_hl_offset(
            out, plan->pointer_member_offset, 0);
        mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
              "\tex de,hl\n\tld e,(hl)\n\tinc hl\n"
              "\tld d,(hl)\n\tex de,hl\n\tret\n", out);
    }
}

static void mir_emit_byte_memory_stack(
    MirStream *out, const struct MirByteMemoryStack *plan)
{
    mir_machine_emit_global_byte_a(
        out, plan->cursor_root, plan->cursor_offset, 0);
    mir_stream_puts("\tld e,a\n", out);
    if (plan->kind == MIR_BYTE_MEMORY_STACK_PUSH ||
        plan->kind == MIR_BYTE_MEMORY_STACK_PUSH_WORD)
        mir_stream_puts("\tdec a\n", out);
    else
        mir_stream_puts("\tinc a\n", out);
    mir_machine_emit_global_byte_a(
        out, plan->cursor_root, plan->cursor_offset, 1);
    if (plan->kind == MIR_BYTE_MEMORY_STACK_POP ||
        plan->kind == MIR_BYTE_MEMORY_STACK_PUSH_WORD)
        mir_stream_puts("\tld e,a\n", out);
    mir_stream_puts("\tld d,0\n", out);
    mir_machine_emit_global_address_hl(
        out, plan->memory_root, plan->memory_offset);
    mir_stream_puts("\tadd hl,de\n", out);
    if (plan->kind == MIR_BYTE_MEMORY_STACK_PUSH) {
        mir_stream_puts("\tpush hl\n", out);
        mir_stream_printf(out, "\tld hl,%d\n\tadd hl,sp\n"
                     "\tld a,(hl)\n\tpop hl\n"
                     "\tld (hl),a\n\tret\n",
                plan->value_stack_offset + 2);
    } else if (plan->kind == MIR_BYTE_MEMORY_STACK_POP) {
        mir_stream_puts("\tld a,(hl)\n\tld l,a\n\tld h,0\n\tret\n", out);
    } else {
        mir_stream_puts("\tpush hl\n", out);
        mir_stream_printf(out, "\tld hl,%d\n\tadd hl,sp\n"
                     "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
                     "\tpop hl\n\tld (hl),e\n\tinc hl\n"
                     "\tld (hl),d\n",
                plan->value_stack_offset + 2);
        mir_machine_emit_global_byte_a(
            out, plan->cursor_root, plan->cursor_offset, 0);
        mir_stream_puts("\tdec a\n", out);
        mir_machine_emit_global_byte_a(
            out, plan->cursor_root, plan->cursor_offset, 1);
        mir_stream_puts("\tret\n", out);
    }
}

static void mir_emit_fixed_array_reduction(
    MirStream *out, const struct MirFixedArrayReduction *plan)
{
    int loop = new_label();

    if (plan->element_width == 4) {
        mir_stream_printf(out,
                ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
                "\tpush iy\n",
                mir.name);
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
        mir_stream_printf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
                "\tpush de\n\tpop iy\n"
                "\tld hl,0\n\tld de,0\n\tld b,64\n"
                "L%d:\n"
                "\tld c,(iy+0)\n\tld a,l\n\tadd a,c\n\tld l,a\n"
                "\tld c,(iy+1)\n\tld a,h\n\tadc a,c\n\tld h,a\n"
                "\tld c,(iy+2)\n\tld a,e\n\tadc a,c\n\tld e,a\n"
                "\tld c,(iy+3)\n\tld a,d\n\tadc a,c\n\tld d,a\n"
                "\tinc iy\n\tinc iy\n\tinc iy\n\tinc iy\n"
                "\tdjnz L%d\n\tpop iy\n\tret\n",
                plan->parameter_stack_offset + 2, loop, loop);
        return;
    }
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tex de,hl\n\tld de,0\n\tld b,64\n"
            "L%d:\n",
            plan->parameter_stack_offset, loop);
    if (plan->element_width == 2) {
        mir_stream_puts("\tld a,(hl)\n\tadd a,e\n\tld e,a\n\tinc hl\n"
              "\tld a,(hl)\n\tadc a,d\n\tld d,a\n\tinc hl\n",
              out);
    } else if (plan->element_is_unsigned) {
        mir_stream_puts("\tld a,(hl)\n\tinc hl\n"
              "\tadd a,e\n\tld e,a\n"
              "\tld a,0\n\tadc a,d\n\tld d,a\n", out);
    } else {
        int sign_done = new_label();

        mir_stream_printf(out,
                "\tld c,(hl)\n\tinc hl\n"
                "\tld a,c\n\tadd a,e\n\tld e,a\n"
                "\tld a,0\n\tadc a,d\n"
                "\tbit 7,c\n\tjp z,L%d\n\tdec a\n"
                "L%d:\n\tld d,a\n",
                sign_done, sign_done);
    }
    mir_stream_printf(out, "\tdjnz L%d\n\tex de,hl\n\tret\n", loop);
}

static void mir_emit_fixed_array_affine_fill(
    MirStream *out, const struct MirFixedArrayAffineFill *plan)
{
    int loop = new_label();
    int sign_done = new_label();
    int argument;

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-3\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld a,(ix+%d)\n\tld (ix-3),a\n"
            "\tld a,(ix+%d)\n\tld (ix-2),a\n"
            "\txor a\n\tld (ix-1),a\n"
            "L%d:\n\tld c,(ix-1)\n",
            plan->pointer_stack_offset + 2,
            plan->pointer_stack_offset + 3,
            loop);
    for (argument = 0; argument < 6; ++argument) {
        mir_stream_puts("\tld a,c\n\tand 1\n"
              "\tld l,a\n\tld h,0\n\tpush hl\n", out);
        if (argument != 5)
            mir_stream_puts("\tsrl c\n", out);
    }
    mir_machine_emit_symbol_call(out, plan->function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tpop bc\n\tpop bc\n\tpop bc\n", out);
    if (plan->element_width == 4) {
        mir_stream_printf(out,
                "\tld de,0\n\tbit 7,h\n\tjp z,L%d\n\tdec de\n"
                "L%d:\n"
                "\tld a,l\n\tadd a,(ix+%d)\n\tld l,a\n"
                "\tld a,h\n\tadc a,(ix+%d)\n\tld h,a\n"
                "\tld a,e\n\tadc a,(ix+%d)\n\tld e,a\n"
                "\tld a,d\n\tadc a,(ix+%d)\n\tld d,a\n"
                "\tld c,(ix-3)\n\tld b,(ix-2)\n"
                "\tld a,l\n\tld (bc),a\n\tinc bc\n"
                "\tld a,h\n\tld (bc),a\n\tinc bc\n"
                "\tld a,e\n\tld (bc),a\n\tinc bc\n"
                "\tld a,d\n\tld (bc),a\n\tinc bc\n",
                sign_done, sign_done,
                plan->base_stack_offset + 2,
                plan->base_stack_offset + 3,
                plan->base_stack_offset + 4,
                plan->base_stack_offset + 5);
    } else {
        mir_stream_printf(out,
                "\tld e,(ix+%d)\n\tld d,(ix+%d)\n"
                "\tadd hl,de\n"
                "\tld c,(ix-3)\n\tld b,(ix-2)\n"
                "\tld a,l\n\tld (bc),a\n\tinc bc\n",
                plan->base_stack_offset + 2,
                plan->base_stack_offset + 3);
        if (plan->element_width == 2)
            mir_stream_puts("\tld a,h\n\tld (bc),a\n\tinc bc\n", out);
    }
    mir_stream_printf(out,
            "\tld (ix-3),c\n\tld (ix-2),b\n"
            "\tld a,(ix-1)\n\tinc a\n\tld (ix-1),a\n"
            "\tcp 64\n\tjp nz,L%d\n"
            "\tld sp,ix\n\tpop ix\n\tret\n",
            loop);
}

static void mir_emit_nested_row_store(
    MirStream *out, const struct MirNestedRowStore *plan)
{
    int done = new_label();

    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tpush de\n"
            "\tld l,c\n\tld h,b\n",
            plan->index_stack_offset,
            plan->value_stack_offset);
    mir_emit_mul_hl_const(
        out, (unsigned long)plan->row_stride);
    mir_stream_puts("\tld c,l\n\tld b,h\n", out);
    mir_machine_emit_global_word(
        out, plan->root, plan->root_pointer_offset);
    mir_stream_puts("\tadd hl,bc\n\tld c,l\n\tld b,h\n", out);
    mir_machine_emit_hl_offset(
        out, plan->count_member_offset, 1);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld l,e\n\tld h,d\n", out);
    mir_emit_mul_hl_const(
        out, (unsigned long)plan->element_stride);
    mir_machine_emit_hl_offset(
        out, plan->value_member_offset, 1);
    mir_stream_puts("\tadd hl,bc\n\tpop de\n"
          "\tld (hl),e\n\tinc hl\n\tld (hl),d\n"
          "\tld l,c\n\tld h,b\n", out);
    mir_machine_emit_hl_offset(
        out, plan->count_member_offset, 1);
    mir_stream_puts("\tinc (hl)\n", out);
    mir_stream_printf(out, "\tjp nz, L%d\n\tinc hl\n\tinc (hl)\nL%d:\n\tret\n",
            done, done);
}

static void mir_emit_word_range_bool(
    MirStream *out, const struct MirWordRangeBool *plan)
{
    int outside = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld a,(hl)\n\tor a\n"
            "\tjp nz,L%d\n\tld a,e\n\tcp %d\n\tjp nc,L%d\n"
            "\tld hl,1\n\tret\nL%d:\n\tld hl,0\n\tret\n",
            plan->parameter_stack_offset,
            outside, plan->upper, outside, outside);
}

static void mir_emit_ascii_upper(
    MirStream *out, const struct MirAsciiUpper *plan)
{
    int result = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    if (plan->width == 1)
        mir_stream_printf(out,
                "\tld hl,%d\n\tadd hl,sp\n\tld a,(hl)\n",
                plan->parameter_stack_offset);
    else
        mir_stream_printf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n"
                "\tld a,h\n\tor a\n\tjp nz,L%d\n\tld a,l\n",
                plan->parameter_stack_offset, result);
    mir_stream_printf(out,
            "\tcp %d\n\tjp c,L%d\n\tcp %d\n\tjp nc,L%d\n",
            plan->lower, result, plan->upper + 1, result);
    if (plan->adjustment < 0)
        mir_stream_printf(out, "\tsub %d\n", -plan->adjustment);
    else if (plan->adjustment > 0)
        mir_stream_printf(out, "\tadd a,%d\n", plan->adjustment);
    if (plan->width == 1)
        mir_stream_printf(out,
                "L%d:\n\tld l,a\n\trlca\n\tsbc a,a\n\tld h,a\n\tret\n",
                result);
    else
        mir_stream_printf(out, "\tld l,a\nL%d:\n\tret\n", result);
}

static void mir_emit_fixed_word_array_sum(
    MirStream *out, const struct MirFixedWordArraySum *plan)
{
    int element;
    int offset;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    if (!plan->pointer_is_volatile) {
        mir_stream_printf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
                "\tld de,0\n",
                plan->parameter_stack_offset);
        for (element = 0; element < plan->count; ++element)
            mir_stream_puts("\tld a,(bc)\n\tinc bc\n\tld l,a\n"
                  "\tld a,(bc)\n\tinc bc\n\tld h,a\n"
                  "\tadd hl,de\n\tex de,hl\n", out);
        mir_stream_puts("\tex de,hl\n\tret\n", out);
        return;
    }
    mir_stream_puts("\tld bc,0\n", out);
    for (element = 0; element < plan->count; ++element) {
        mir_stream_printf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n",
                plan->parameter_stack_offset);
        for (offset = 0; offset < element * 2; ++offset)
            mir_stream_puts("\tinc hl\n", out);
        mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
              "\tld h,b\n\tld l,c\n\tadd hl,de\n"
              "\tld b,h\n\tld c,l\n", out);
    }
    mir_stream_puts("\tld h,b\n\tld l,c\n\tret\n", out);
}

static void mir_emit_slice_word_sum(
    MirStream *out, const struct MirSliceWordSum *plan)
{
    int done = new_label();
    int empty = new_label();
    int loop = new_label();

    mir_stream_printf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tbit 7,d\n\tjp nz,L%d\n"
            "\tld a,d\n\tor e\n\tjp z,L%d\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tpush bc\n\tpop iy\n\tadd iy,de\n\tadd iy,de\n"
            "\tld de,0\n"
            "L%d:\n\tld a,(bc)\n\tinc bc\n\tld l,a\n"
            "\tld a,(bc)\n\tinc bc\n\tld h,a\n"
            "\tadd hl,de\n\tex de,hl\n"
            "\tpush iy\n\tpop hl\n\tor a\n\tsbc hl,bc\n"
            "\tjp nz,L%d\n\tex de,hl\n\tjp L%d\n"
            "L%d:\n\tld hl,0\n"
            "L%d:\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            plan->parameter_stack_offset + plan->count_offset + 2,
            empty, empty,
            plan->parameter_stack_offset + plan->data_offset + 2,
            loop, loop, done, empty, done);
}

static void mir_emit_conditional_null_identity(
    MirStream *out, const struct MirConditionalNullIdentity *plan)
{
    int is_false = new_label();
    int is_true = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tjp z,L%d\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tjp nz,L%d\n"
            "L%d:\n\tld hl,1\n\tret\n"
            "L%d:\n\tld hl,0\n\tret\n",
            plan->condition_stack_offset, is_true,
            plan->pointer_stack_offset, is_false,
            is_true, is_false);
}

static void mir_emit_wide_constant_equal(
    MirStream *out, const struct MirWideConstantEqual *plan)
{
    int byte;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out, "\tld hl,%d\n\tadd hl,sp\n\tld c,0\n",
            plan->parameter_stack_offset);
    for (byte = 0; byte < 4; ++byte) {
        mir_stream_printf(out, "\tld a,(hl)\n\txor %lu\n\tor c\n\tld c,a\n",
                (plan->value >> (byte * 8)) & 0xffUL);
        if (byte != 3)
            mir_stream_puts("\tinc hl\n", out);
    }
    mir_stream_puts("\tld hl,0\n\tor a\n\tret nz\n\tinc hl\n\tret\n", out);
}

static void mir_emit_float_truth_once(
    MirStream *out, const struct MirFloatTruthOnce *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tinc hl\n"
            "\tor (hl)\n\tld c,a\n\tinc hl\n\tld a,(hl)\n"
            "\tand 127\n\tor c\n\tld hl,0\n\tret z\n\tinc hl\n\tret\n",
            plan->parameter_stack_offset);
}

static void mir_emit_nested_word_long_select(
    MirStream *out, const struct MirNestedWordLongSelect *plan)
{
    int second = new_label();
    int third = new_label();
    int selected = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tjp z,L%d\n"
            "\tld hl,%d\n\tjp L%d\n"
            "L%d:\n\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tjp z,L%d\n"
            "\tld hl,%d\n\tjp L%d\n"
            "L%d:\n",
            plan->first_condition_stack_offset, second,
            plan->first_value, selected,
            second, plan->second_condition_stack_offset, third,
            plan->second_value, selected, third);
    if (plan->third_is_parameter)
        mir_stream_printf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld e,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,e\n",
                plan->third_stack_offset);
    else
        mir_stream_printf(out, "\tld hl,%d\n", plan->third_value);
    mir_stream_printf(out,
            "L%d:\n\tld a,h\n\trlca\n\tsbc a,a\n"
            "\tld e,a\n\tld d,a\n\tret\n",
            selected);
}

static void mir_emit_float_int_truth(
    MirStream *out, const struct MirFloatIntTruth *plan)
{
    int false_result = new_label();
    int true_result = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tinc hl\n"
            "\tor (hl)\n\tld c,a\n\tinc hl\n\tld a,(hl)\n"
            "\tand 127\n\tor c\n",
            plan->float_stack_offset);
    if (plan->operation == '&')
        mir_stream_printf(out, "\tjp z,L%d\n", false_result);
    else
        mir_stream_printf(out, "\tjp nz,L%d\n", true_result);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n",
            plan->int_stack_offset);
    if (plan->operation == '&')
        mir_stream_printf(out, "\tjp z,L%d\n", false_result);
    else
        mir_stream_printf(out, "\tjp nz,L%d\n", true_result);
    if (plan->operation == '|')
        mir_stream_printf(out, "L%d:\n", false_result);
    mir_stream_printf(out, "\tld hl,%d\n\tret\n",
            plan->operation == '&' ? 1 : 0);
    if (plan->operation == '&')
        mir_stream_printf(out, "L%d:\n\tld hl,0\n\tret\n", false_result);
    else
        mir_stream_printf(out, "L%d:\n\tld hl,1\n\tret\n", true_result);
}

static void mir_emit_conditional_float_long(
    MirStream *out, const struct MirConditionalFloatLong *plan)
{
    int false_arm = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tjp z,L%d\n"
            "\tld hl,%d\n\tld a,h\n\trlca\n\tsbc a,a\n"
            "\tld e,a\n\tld d,a\n\tret\n"
            "L%d:\n",
            plan->condition_stack_offset, false_arm,
            plan->true_value, false_arm);
    if (plan->kind == MIR_CONDITIONAL_FLOAT_ADD) {
        mir_emit_wide_parameter(out, plan->argument_stack_offset);
        mir_stream_puts("\tpush de\n\tpush hl\n", out);
        mir_stream_printf(out, "\tld hl,%lu\n\tld de,%lu\n",
                plan->add_bits & 0xffffUL,
                (plan->add_bits >> 16) & 0xffffUL);
        mir_emit_runtime_call(out, "__faf");
        mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    } else {
        mir_stream_printf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n",
                plan->argument_stack_offset);
        mir_machine_emit_symbol_call(out, plan->function);
        mir_stream_puts("\tpop bc\n", out);
    }
    mir_emit_runtime_call(out, "__ffl");
    mir_stream_puts("\tret\n", out);
}

static void mir_emit_conditional_pointer_float_long(
    MirStream *out, const struct MirConditionalPointerFloatLong *plan)
{
    int false_arm = new_label();
    int selected = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tjp z,L%d\n",
            plan->condition_stack_offset, false_arm);
    mir_machine_emit_global_address_de(
        out, plan->true_root, plan->element_offset);
    mir_stream_printf(out, "\tex de,hl\n\tjp L%d\nL%d:\n",
            selected, false_arm);
    mir_machine_emit_global_word(out, plan->false_pointer, 0);
    if (plan->element_offset != 0)
        mir_stream_printf(out, "\tld de,%d\n\tadd hl,de\n",
                plan->element_offset);
    mir_stream_printf(out, "L%d:\n", selected);
    mir_stream_puts("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
          "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld l,c\n\tld h,b\n", out);
    mir_emit_runtime_call(out, "__ffl");
    mir_stream_puts("\tret\n", out);
}

static void mir_emit_nested_member_float_long(
    MirStream *out, const struct MirNestedMemberFloatLong *plan)
{
    int second = new_label();
    int third = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tjp z,L%d\n",
            plan->first_condition_stack_offset, second);
    mir_emit_word_as_long_return(out, plan->first_value);
    mir_stream_printf(out,
            "L%d:\n\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tjp z,L%d\n",
            second, plan->second_condition_stack_offset, third);
    mir_emit_word_as_long_return(out, plan->second_value);
    mir_stream_printf(out,
            "L%d:\n\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n",
            third, plan->pointer_stack_offset);
    if (plan->member_offset != 0)
        mir_stream_printf(out, "\tld de,%d\n\tadd hl,de\n",
                plan->member_offset);
    mir_stream_puts("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
          "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld l,c\n\tld h,b\n", out);
    mir_emit_runtime_call(out, "__ffl");
    mir_stream_puts("\tret\n", out);
}

static void mir_emit_conditional_float_compare_long(
    MirStream *out, const struct MirConditionalFloatCompareLong *plan)
{
    int false_arm = new_label();
    int nonpositive = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tjp z,L%d\n",
            plan->condition_stack_offset, false_arm);
    mir_emit_word_as_long_return(out, plan->true_value);
    mir_stream_printf(out, "L%d:\n", false_arm);
    mir_emit_wide_parameter(out, plan->float_stack_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n\tld hl,0\n\tld de,0\n", out);
    mir_emit_runtime_call(out, "__fltf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", nonpositive);
    mir_emit_word_as_long_return(out, plan->positive_value);
    mir_stream_printf(out, "L%d:\n", nonpositive);
    mir_emit_word_as_long_return(out, plan->nonpositive_value);
}

static void mir_emit_conditional_bool(
    MirStream *out, const struct MirConditionalBool *plan)
{
    int false_arm = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    if (plan->true_value == plan->false_value) {
        mir_stream_printf(out, "\tld hl,%d\n\tret\n", plan->true_value);
        return;
    }
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tjp z,L%d\n"
            "\tld hl,%d\n\tret\n"
            "L%d:\n\tld hl,%d\n\tret\n",
            plan->condition_stack_offset, false_arm,
            plan->true_value, false_arm, plan->false_value);
}

static void mir_emit_logical_or_parameters(
    MirStream *out, const struct MirLogicalOrParameters *plan)
{
    int nonzero = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tjp nz,L%d\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tjp nz,L%d\n"
            "\tld hl,0\n\tret\n"
            "L%d:\n\tld hl,1\n\tret\n",
            plan->first_stack_offset, nonzero,
            plan->second_stack_offset, nonzero, nonzero);
}

static void mir_emit_cleared_record_append(
    MirStream *out, const struct MirClearedRecordAppend *plan)
{
    mir_stream_printf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_word(out, plan->root, plan->root_offset);
    mir_machine_emit_hl_offset(out, plan->cursor_member_offset, 0);
    mir_stream_puts("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
          "\tinc bc\n\tld (hl),b\n\tdec hl\n\tld (hl),c\n", out);
    mir_stream_puts("\tdec bc\n", out);
    mir_machine_emit_global_word(out, plan->root, plan->root_offset);
    mir_machine_emit_hl_offset(out, plan->array_member_offset, 0);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tpush de\n\tld h,b\n\tld l,c\n", out);
    mir_emit_mul_hl_const(out, (unsigned long)plan->stride);
    mir_stream_puts("\tpop de\n\tadd hl,de\n\tpush hl\n\tpop iy\n", out);
    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n\tld hl,0\n\tpush hl\n",
            plan->stride);
    mir_stream_puts("\tpush iy\n", out);
    mir_machine_emit_symbol_call(out, plan->clear_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n",
            plan->name_stack_offset + 2);
    mir_stream_puts("\tpush iy\n", out);
    mir_machine_emit_symbol_call(out, plan->copy_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_emit_cleared_record_field(
        out, plan->kind_field_offset, plan->kind_stack_offset + 2);
    mir_emit_cleared_record_field(
        out, plan->value_field_offset, plan->value_stack_offset + 2);
    mir_machine_emit_global_word(out, plan->root, plan->root_offset);
    mir_machine_emit_hl_offset(out, plan->cursor_member_offset, 0);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n\tdec hl\n\tpop iy\n"
          ";@dcc.reg free=iy\n\tret\n", out);
}

static void mir_emit_record_name_search(
    MirStream *out, const struct MirRecordNameSearch *plan)
{
    int done = new_label();
    int found = new_label();
    int loop = new_label();
    int not_found = new_label();

    mir_stream_printf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_word(out, plan->root, plan->root_offset);
    mir_machine_emit_hl_offset(out, plan->cursor_member_offset, 0);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n\tdec hl\n\tpush hl\n\tpop iy\n", out);
    mir_stream_printf(out,
            "L%d:\n\tpush iy\n\tpop hl\n"
            "\tbit 7,h\n\tjp nz,L%d\n",
            loop, not_found);
    mir_machine_emit_global_word(out, plan->root, plan->root_offset);
    mir_machine_emit_hl_offset(out, plan->array_member_offset, 0);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n"
          "\tpush iy\n\tpop hl\n", out);
    mir_emit_mul_hl_const(out, (unsigned long)plan->stride);
    mir_stream_puts("\tpop de\n\tadd hl,de\n", out);
    mir_machine_emit_hl_offset(out, plan->name_field_offset, 0);
    mir_stream_puts("\tld c,l\n\tld b,h\n", out);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tpush de\n\tpush bc\n",
            plan->name_stack_offset + 2);
    mir_machine_emit_symbol_call(out, plan->compare_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out,
            "\tjp z,L%d\n\tdec iy\n\tjp L%d\n"
            "L%d:\n\tpush iy\n\tpop hl\n\tjp L%d\n"
            "L%d:\n\tld hl,65535\n"
            "L%d:\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            found, loop, found, done,
            not_found, done);
}

static void mir_emit_sequential_unary_reports(
    MirStream *out, const struct MirSequentialUnaryReports *plan)
{
    int parameter;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (parameter = 4; parameter >= 0; --parameter) {
        mir_stream_printf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n",
                plan->parameter_stack_offsets[parameter] +
                    (4 - parameter) * 2);
        mir_machine_emit_symbol_call(out, plan->helper);
        mir_stream_puts("\tpop bc\n\tpush hl\n", out);
    }
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", plan->string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    for (parameter = 0; parameter < 6; ++parameter)
        mir_stream_puts("\tpop bc\n", out);
    mir_stream_puts("\tret\n", out);
}

static void mir_emit_nibble_append(
    MirStream *out, const struct MirNibbleAppend *plan)
{
    int done = new_label();
    int ready = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld c,(hl)\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n"
            "\tld a,c\n\tcp %d\n",
            plan->value_stack_offset,
            plan->pointer_stack_offset, plan->threshold);
    if (plan->high_adjustment < 0)
        mir_stream_printf(out, "\tjp c,L%d\n\tsub %d\n\tjp L%d\n",
                ready, -plan->high_adjustment, done);
    else
        mir_stream_printf(out, "\tjp c,L%d\n\tadd a,%d\n\tjp L%d\n",
                ready, plan->high_adjustment, done);
    mir_stream_printf(out, "L%d:\n", ready);
    if (plan->low_adjustment < 0)
        mir_stream_printf(out, "\tsub %d\n", -plan->low_adjustment);
    else if (plan->low_adjustment > 0)
        mir_stream_printf(out, "\tadd a,%d\n", plan->low_adjustment);
    mir_stream_printf(out, "L%d:\n\tld (hl),a\n\tinc hl\n\tret\n", done);
}

static void mir_emit_volatile_fill_wide_constant(
    MirStream *out, const struct MirVolatileFillWideConstant *plan)
{
    int loop = new_label();

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    mir_stream_printf(out, "\tld hl,%d\n\tadd hl,sp\n\tld sp,hl\n",
            plan->buffer_offset);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\tpush ix\n\tpop hl\n", out);
    mir_stream_printf(out,
            "\tld de,%d\n\tadd hl,de\n\tld a,0\n\tld b,%d\n"
            "L%d:\n\tld (hl),a\n\tinc hl\n\tinc a\n\tdjnz L%d\n"
            "\tld hl,%lu\n\tld de,%lu\n"
            "\tld sp,ix\n\tpop ix\n\tret\n",
            plan->buffer_offset, plan->count, loop, loop,
            plan->result & 0xffffUL,
            (plan->result >> 16) & 0xffffUL);
}

static void mir_emit_single_signed_div_check(
    MirStream *out, const struct MirSingleSignedDivCheck *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld h,b\n\tld l,c\n",
            plan->numerator_stack_offset,
            plan->denominator_stack_offset);
    mir_emit_runtime_call(
        out, plan->operation == '%' ? "__mods" : "__divs");
    mir_stream_puts("\tld c,l\n\tld b,h\n", out);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n"
            "\tpush bc\n",
            plan->label_stack_offset,
            plan->expected_stack_offset + 2);
    mir_machine_emit_symbol_call(out, plan->check_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tret\n", out);
}

static void mir_emit_wide_div_result_check(
    MirStream *out, const struct MirWideDivResultCheck *plan)
{
    int quotient_ok = new_label();
    int remainder_ok = new_label();
    int identity_ok = new_label();
    int numerator_offset = plan->numerator_stack_offset + 2;
    int denominator_offset = plan->denominator_stack_offset + 2;
    int expected_quotient_offset =
        plan->expected_quotient_stack_offset + 2;
    int expected_remainder_offset =
        plan->expected_remainder_stack_offset + 2;

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-8\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    mir_emit_local_wide_argument(out, denominator_offset);
    mir_emit_local_wide_argument(out, numerator_offset);
    mir_emit_local_address(out, plan->result_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->divide_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);

    mir_emit_wide_ix_load(out, plan->result_offset);
    mir_emit_wide_ix_equal_branch(
        out, expected_quotient_offset, quotient_ok);
    mir_emit_wide_div_failure(
        out, plan, plan->result_offset, expected_quotient_offset);
    mir_stream_printf(out, "L%d:\n", quotient_ok);

    mir_emit_wide_ix_load(out, plan->result_offset + 4);
    mir_emit_wide_ix_equal_branch(
        out, expected_remainder_offset, remainder_ok);
    mir_emit_wide_div_failure(
        out, plan, plan->result_offset + 4,
        expected_remainder_offset);
    mir_stream_printf(out, "L%d:\n", remainder_ok);

    mir_emit_wide_ix_load(out, plan->result_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_emit_wide_ix_load(out, denominator_offset);
    mir_emit_runtime_call(out, "__lmul");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpush de\n\tpush hl\n", out);
    mir_emit_wide_ix_load(out, plan->result_offset + 4);
    mir_stream_puts("\tpop bc\n\tadd hl,bc\n\tex de,hl\n"
          "\tpop bc\n\tadc hl,bc\n\tex de,hl\n", out);
    mir_emit_wide_ix_store(out, plan->result_offset);
    mir_emit_wide_ix_equal_branch(out, numerator_offset, identity_ok);
    mir_emit_wide_div_failure(
        out, plan, plan->result_offset, numerator_offset);
    mir_stream_printf(out,
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            identity_ok);
}

static void mir_emit_final_call_check_schedule(
    MirStream *out, const struct MirFinalCallCheckSchedule *plan)
{
    int check;
    int success = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (check = 0;
         check < MIR_FINAL_CALL_CHECK_COUNT; ++check)
        mir_emit_final_call_check(out, &plan->checks[check]);
    mir_machine_emit_global_word(
        out, plan->failure_root, plan->failure_offset);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out,
            "\tjp z,L%d\n\tld hl,1\n\tret\n"
            "L%d:\n\tld hl,S%d\n\tpush hl\n",
            success, success, plan->success_string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_stream_puts("\tpop bc\n\tld hl,0\n\tret\n", out);
}

static void mir_emit_math_verification_schedule(
    MirStream *out, const struct MirMathVerificationSchedule *plan)
{
    int failure = new_label();
    int check;

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-4\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->intro_string_id);
    mir_machine_emit_symbol_call(
        out, plan->print_function);
    mir_emit_final_call_cleanup(out, 1);

    for (check = 0;
         check < MIR_MATH_ORDINARY_CHECK_COUNT; ++check)
        mir_emit_math_call_check(out, &plan->calls[check]);
    for (check = 0;
         check < MIR_MATH_FREXP_CHECK_COUNT; ++check)
        mir_emit_math_frexp_check(out, &plan->frexp[check]);
    for (check = 0;
         check < MIR_MATH_MODEXP_CHECK_COUNT; ++check)
        mir_emit_math_call_check(
            out,
            &plan->calls[
                MIR_MATH_ORDINARY_CHECK_COUNT + check]);
    for (check = 0;
         check < MIR_MATH_MODF_CHECK_COUNT; ++check)
        mir_emit_math_modf_check(out, &plan->modf[check]);

    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->separator_string_id);
    mir_machine_emit_symbol_call(
        out, plan->print_function);
    mir_emit_final_call_cleanup(out, 1);
    mir_machine_emit_global_word(
        out, plan->failures_root, plan->failures_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_global_word(
        out, plan->checks_root, plan->checks_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->summary_string_id);
    mir_machine_emit_symbol_call(
        out, plan->print_function);
    mir_emit_final_call_cleanup(out, 3);

    mir_machine_emit_global_word(
        out, plan->failures_root, plan->failures_offset);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n\tld hl,S%d\n\tpush hl\n",
            failure, plan->success_string_id);
    mir_machine_emit_symbol_call(
        out, plan->print_function);
    mir_stream_puts("\tpop bc\n\tld hl,0\n"
          "\tld sp,ix\n\tpop ix\n\tret\n", out);
    mir_stream_printf(out, "L%d:\n\tld hl,S%d\n\tpush hl\n",
            failure, plan->failure_string_id);
    mir_machine_emit_symbol_call(
        out, plan->print_function);
    mir_stream_puts("\tpop bc\n\tld hl,1\n"
          "\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static void mir_emit_ctype_realloc_schedule(
    MirStream *out, const struct MirCtypeReallocSchedule *plan)
{
    int allocation_ok = new_label();
    int grow_ok = new_label();
    int shrink_ok = new_label();
    int success = new_label();
    int check;
    int byte;

    mir_stream_puts("\tpush hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    for (check = 0; check < plan->check_count; ++check) {
        const struct MirCtypeCheck *item = &plan->checks[check];

        mir_stream_printf(out,
                "\tld hl,S%d\n\tpush hl\n"
                "\tld hl,%lu\n\tpush hl\n",
                item->string_id, item->input & 0xffffUL);
        mir_machine_emit_symbol_call(out, item->value_function);
        mir_stream_puts("\tpop bc\n", out);
        mir_emit_ctype_compare_bool(
            out, item->expected, item->comparison);
        mir_stream_puts("\tpush hl\n", out);
        mir_machine_emit_symbol_call(
            out, plan->check_function);
        mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    }

    mir_stream_printf(out, "\tld hl,%lu\n\tpush hl\n",
            plan->allocation_size & 0xffffUL);
    mir_machine_emit_symbol_call(out, plan->allocate_function);
    mir_stream_puts("\tpop bc\n", out);
    mir_emit_ctype_pointer_store(out);
    mir_stream_printf(out, "\tld a,h\n\tor l\n\tjp nz,L%d\n",
            allocation_ok);
    mir_emit_ctype_failure(
        out, plan->allocation_failure_string_id,
        plan->print_function);
    mir_stream_printf(out, "L%d:\n", allocation_ok);

    mir_emit_ctype_pointer_load(out);
    mir_stream_puts("\tex de,hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n\tpush de\n",
            plan->source_string_id);
    mir_machine_emit_symbol_call(out, plan->copy_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);

    mir_emit_ctype_resize(out, plan, plan->grow_size);
    mir_stream_printf(out, "\tld a,h\n\tor l\n\tjp nz,L%d\n",
            grow_ok);
    mir_emit_ctype_failure(
        out, plan->grow_failure_string_id,
        plan->print_function);
    mir_stream_printf(out, "L%d:\n", grow_ok);

    mir_emit_ctype_pointer_load(out);
    mir_stream_puts("\tex de,hl\n", out);
    mir_stream_printf(out,
            "\tld hl,S%d\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n\tpush de\n",
            plan->preserve_string_id,
            plan->source_string_id);
    mir_machine_emit_symbol_call(out, plan->compare_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_emit_ctype_compare_bool(out, 0, TOK_EQ);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->check_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);

    for (byte = 0; byte < 2; ++byte) {
        mir_emit_ctype_pointer_load(out);
        mir_machine_emit_hl_offset(
            out, plan->store_indices[byte], 0);
        mir_stream_printf(out, "\tld (hl),%lu\n",
                plan->stored_values[byte] & 255UL);
    }

    mir_emit_ctype_resize(out, plan, plan->shrink_size);
    mir_stream_printf(out, "\tld a,h\n\tor l\n\tjp nz,L%d\n",
            shrink_ok);
    mir_emit_ctype_failure(
        out, plan->shrink_failure_string_id,
        plan->print_function);
    mir_stream_printf(out, "L%d:\n", shrink_ok);

    for (byte = 0; byte < 2; ++byte) {
        mir_emit_ctype_pointer_load(out);
        mir_machine_emit_hl_offset(
            out, plan->byte_indices[byte], 0);
        mir_stream_puts("\tld l,(hl)\n", out);
        if (plan->byte_unsigned[byte])
            mir_stream_puts("\tld h,0\n", out);
        else
            mir_stream_puts("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n",
                  out);
        mir_emit_ctype_check_result(
            out, plan->byte_expected[byte], TOK_EQ,
            plan->byte_string_ids[byte],
            plan->check_function);
    }

    mir_emit_ctype_pointer_load(out);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->free_function);
    mir_stream_puts("\tpop bc\n", out);

    mir_machine_emit_global_word(
        out, plan->failure_root, plan->failure_offset);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            success, plan->final_failure_string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tld hl,1\n\tret\n", out);
    mir_stream_printf(out, "L%d:\n\tld hl,S%d\n\tpush hl\n",
            success, plan->success_string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld hl,0\n\tret\n", out);
}

static void mir_emit_context_op_schedule(
    MirStream *out, const struct MirContextOpSchedule *plan)
{
    int failure = new_label();
    int done = new_label();
    int check;

    mir_emit_context_array_init(out, plan);
    for (check = 0; check < 15; ++check)
        mir_emit_context_check(out, &plan->checks[check]);
    mir_emit_context_wide_global_store(
        out, plan->float_roots[0], plan->float_offsets[0],
        plan->float_values[0]);
    mir_emit_context_wide_global_store(
        out, plan->float_roots[1], plan->float_offsets[1],
        plan->float_values[1]);
    for (; check < MIR_CONTEXT_OP_CHECK_COUNT; ++check)
        mir_emit_context_check(out, &plan->checks[check]);

    mir_machine_emit_global_word(
        out, plan->failure_root, plan->failure_offset);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n\tld hl,S%d\n\tpush hl\n",
            failure, plan->success_string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_stream_puts("\tpop bc\n", out);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n",
            done, failure);
    mir_machine_emit_global_word(
        out, plan->failure_root, plan->failure_offset);
    mir_stream_printf(out, "\tpush hl\n\tld hl,S%d\n\tpush hl\n",
            plan->failure_string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_stream_printf(out, "L%d:\n", done);
    mir_machine_emit_global_word(
        out, plan->failure_root, plan->failure_offset);
    mir_stream_puts("\tld a,h\n\tor l\n\tld hl,0\n\tret z\n"
          "\tinc hl\n\tret\n", out);
}

static void mir_emit_local_identity_array_result(
    MirStream *out, const struct MirLocalIdentityArrayResult *plan)
{
    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    mir_stream_printf(out, "\tld hl,%d\n\tadd hl,sp\n\tld sp,hl\n",
            plan->array_offset);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\tpush ix\n\tpop hl\n", out);
    mir_machine_emit_hl_offset(out, plan->array_offset, 0);
    mir_machine_emit_global_word_store(
        out, plan->escaped_pointer, plan->escaped_pointer_offset);
    mir_stream_printf(out,
            "\tld hl,%d\n\tld sp,ix\n\tpop ix\n\tret\n",
            plan->result);
}

static void mir_emit_format_buffer_schedule(
    MirStream *out, const struct MirFormatBufferSchedule *plan)
{
    int check;
    int failure = new_label();
    int summary = new_label();
    int epilogue = new_label();

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-64\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_emit_local_address(out, -64);
    mir_stream_puts("\tex de,hl\n", out);

    for (check = 0;
         check < MIR_FORMAT_BUFFER_CHECK_COUNT;
         ++check) {
        const struct MirFormatBufferCheck *item =
            &plan->checks[check];

        if (item->value_size == 4)
            mir_emit_fixed_point_constant(out, item->value);
        else
            mir_stream_printf(out, "\tld hl,%lu\n\tpush hl\n",
                    item->value & 0xffffUL);
        mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
                item->format_string_id);
        mir_stream_puts("\tpush de\n", out);
        if ((item->runtime_flags &
             MIR_CALL_FLAG_FORMAT_HEX) != 0)
            mir_emit_runtime_call(out, "__pfehx");
        if ((item->runtime_flags &
             MIR_CALL_FLAG_FORMAT_OCTAL) != 0)
            mir_emit_runtime_call(out, "__pfeoc");
        mir_emit_runtime_call(out, item->call_name);
        mir_stream_puts("\tpop de\n\tpop bc\n\tpop bc\n", out);
        if (item->value_size == 4)
            mir_stream_puts("\tpop bc\n", out);

        mir_stream_printf(out,
                "\tld hl,S%d\n\tpush hl\n"
                "\tld hl,S%d\n\tpush hl\n",
                item->description_string_id,
                item->expected_string_id);
        mir_stream_puts("\tpush de\n", out);
        mir_machine_emit_symbol_call(
            out, plan->check_function);
        mir_stream_puts("\tpop de\n\tpop bc\n\tpop bc\n", out);
    }

    mir_machine_emit_global_word(
        out, plan->failure_root, plan->failure_offset);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", failure);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->success_string_id);
    mir_emit_runtime_call(out, plan->success_call_name);
    mir_stream_puts("\tpop bc\n", out);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n",
            summary, failure);
    mir_machine_emit_global_word(
        out, plan->failure_root, plan->failure_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->failure_string_id);
    mir_emit_runtime_call(out, plan->failure_call_name);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);

    mir_stream_printf(out, "L%d:\n", summary);
    mir_machine_emit_global_word(
        out, plan->failure_root, plan->failure_offset);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out,
            "\tld hl,0\n\tjr z,L%d\n\tinc l\n"
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            epilogue, epilogue);
}

static void mir_emit_atof_schedule(
    MirStream *out, const struct MirAtofSchedule *plan)
{
    int success = new_label();
    int summary = new_label();
    int check;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (check = 0;
         check < MIR_ATOF_SCHEDULE_CHECK_COUNT;
         ++check)
        mir_emit_atof_schedule_check(
            out, plan, &plan->checks[check]);

    mir_machine_emit_global_word(
        out, plan->failure_root, plan->failure_offset);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", success);
    mir_machine_emit_global_word(
        out, plan->failure_root, plan->failure_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->failure_string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_emit_atof_schedule_cleanup(out, 2);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n",
            summary, success);
    mir_machine_emit_global_word(
        out, plan->checks_root, plan->checks_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->success_string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_emit_atof_schedule_cleanup(out, 2);
    mir_stream_printf(out, "L%d:\n", summary);
    mir_machine_emit_global_word(
        out, plan->failure_root, plan->failure_offset);
    mir_stream_puts("\tld a,h\n\tor l\n\tld hl,0\n\tret z\n"
          "\tinc hl\n\tret\n", out);
}

int mir_try_emit_container_kernels(MirStream *out)
{
    struct MirLocalIdentityArrayResult local_identity_array_result;
    struct MirWordRangeBool word_range_bool;
    struct MirAsciiUpper ascii_upper;
    struct MirFixedWordArraySum fixed_word_array_sum;
    struct MirSliceWordSum slice_word_sum;
    struct MirConditionalNullIdentity conditional_null_identity;
    struct MirWideConstantEqual wide_constant_equal;
    struct MirFloatTruthOnce float_truth_once;
    struct MirNestedWordLongSelect nested_word_long_select;
    struct MirFloatIntTruth float_int_truth;
    struct MirConditionalFloatLong conditional_float_long;
    struct MirConditionalPointerFloatLong conditional_pointer_float_long;
    struct MirNestedMemberFloatLong nested_member_float_long;
    struct MirConditionalFloatCompareLong conditional_float_compare_long;
    struct MirConditionalBool conditional_bool;
    struct MirLogicalOrParameters logical_or_parameters;
    struct MirClearedRecordAppend cleared_record_append;
    struct MirRecordNameSearch record_name_search;
    struct MirSequentialUnaryReports sequential_unary_reports;
    struct MirNibbleAppend nibble_append;
    struct MirVolatileFillWideConstant volatile_fill_wide_constant;
    struct MirSingleSignedDivCheck single_signed_div_check;
    struct MirWideDivResultCheck wide_div_result_check;
    struct MirFinalCallCheckSchedule final_call_check_schedule;
    struct MirMathVerificationSchedule math_verification_schedule;
    struct MirCtypeReallocSchedule ctype_realloc_schedule;
    struct MirContextOpSchedule context_op_schedule;
    struct MirFormatBufferSchedule format_buffer_schedule;
    struct MirAtofSchedule atof_schedule;
    long constant;
    struct MirNestedRowStore nested_row_store;
    struct MirFlatArrayChecks flat_array_checks;
    struct MirGlobalByteChecks global_byte_checks;
    struct MirFixedParamMutations fixed_param_mutations;
    struct MirGlobalAppend global_append;
    struct MirNestedAppend nested_append;
    struct MirIndexedStack indexed_stack;
    struct MirPointerStack pointer_stack;
    struct MirByteMemoryStack byte_memory_stack;
    struct MirFixedArrayReduction fixed_array_reduction;
    struct MirFixedArrayAffineFill fixed_array_affine_fill;

    {
        int endgame_result = mir_try_emit_endgame_runners(out, 0);

        if (endgame_result >= 0)
            return endgame_result;
    }
    if (mir_match_dead_constant_float_check()) {
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
        mir_stream_puts("\tret\n", out);
        return 1;
    }
    if (mir_match_local_identity_array_result(
            &local_identity_array_result)) {
        mir_emit_local_identity_array_result(
            out, &local_identity_array_result);
        return 1;
    }
    if (mir_match_word_range_bool(&word_range_bool)) {
        mir_emit_word_range_bool(out, &word_range_bool);
        return 1;
    }
    if (mir_match_ascii_upper(&ascii_upper) ||
        mir_match_ascii_word_case(&ascii_upper)) {
        mir_emit_ascii_upper(out, &ascii_upper);
        return 1;
    }
    if (mir_match_fixed_word_array_sum(
            &fixed_word_array_sum)) {
        mir_emit_fixed_word_array_sum(
            out, &fixed_word_array_sum);
        return 1;
    }
    if (mir_match_slice_word_sum(&slice_word_sum)) {
        mir_emit_slice_word_sum(out, &slice_word_sum);
        return 1;
    }
    if (mir_match_conditional_null_identity(
            &conditional_null_identity)) {
        mir_emit_conditional_null_identity(
            out, &conditional_null_identity);
        return 1;
    }
    if (mir_match_wide_constant_equal(
            &wide_constant_equal)) {
        mir_emit_wide_constant_equal(
            out, &wide_constant_equal);
        return 1;
    }
    if (mir_match_float_truth_once(&float_truth_once)) {
        mir_emit_float_truth_once(out, &float_truth_once);
        return 1;
    }
    if (mir_match_nested_word_long_select(
            &nested_word_long_select)) {
        mir_emit_nested_word_long_select(
            out, &nested_word_long_select);
        return 1;
    }
    if (mir_match_float_int_truth(&float_int_truth)) {
        mir_emit_float_int_truth(out, &float_int_truth);
        return 1;
    }
    if (mir_match_conditional_float_long(
            &conditional_float_long)) {
        mir_emit_conditional_float_long(
            out, &conditional_float_long);
        return 1;
    }
    if (mir_match_conditional_pointer_float_long(
            &conditional_pointer_float_long)) {
        mir_emit_conditional_pointer_float_long(
            out, &conditional_pointer_float_long);
        return 1;
    }
    if (mir_match_nested_member_float_long(
            &nested_member_float_long)) {
        mir_emit_nested_member_float_long(
            out, &nested_member_float_long);
        return 1;
    }
    if (mir_match_conditional_float_compare_long(
            &conditional_float_compare_long)) {
        mir_emit_conditional_float_compare_long(
            out, &conditional_float_compare_long);
        return 1;
    }
    if (mir_match_conditional_bool(&conditional_bool)) {
        mir_emit_conditional_bool(out, &conditional_bool);
        return 1;
    }
    if (mir_match_logical_or_parameters(
            &logical_or_parameters)) {
        mir_emit_logical_or_parameters(
            out, &logical_or_parameters);
        return 1;
    }
    if (mir_match_cleared_record_append(
            &cleared_record_append)) {
        mir_emit_cleared_record_append(
            out, &cleared_record_append);
        return 1;
    }
    if (mir_match_record_name_search(&record_name_search)) {
        mir_emit_record_name_search(out, &record_name_search);
        return 1;
    }
    if (mir_match_sequential_unary_reports(
            &sequential_unary_reports)) {
        mir_emit_sequential_unary_reports(
            out, &sequential_unary_reports);
        return 1;
    }
    if (mir_match_nibble_append(&nibble_append)) {
        mir_emit_nibble_append(out, &nibble_append);
        return 1;
    }
    if (mir_match_volatile_fill_wide_constant(
            &volatile_fill_wide_constant)) {
        mir_emit_volatile_fill_wide_constant(
            out, &volatile_fill_wide_constant);
        return 1;
    }
    if (mir_match_single_signed_div_check(
            &single_signed_div_check)) {
        mir_emit_single_signed_div_check(
            out, &single_signed_div_check);
        return 1;
    }
    if (mir_match_wide_div_result_check(
            &wide_div_result_check)) {
        mir_emit_wide_div_result_check(
            out, &wide_div_result_check);
        return 1;
    }
    if (mir_match_final_call_check_schedule(
            &final_call_check_schedule)) {
        mir_emit_final_call_check_schedule(
            out, &final_call_check_schedule);
        return 1;
    }
    if (mir_match_math_verification_schedule(
            &math_verification_schedule)) {
        mir_emit_math_verification_schedule(
            out, &math_verification_schedule);
        return 1;
    }
    if (mir_match_ctype_realloc_schedule(
            &ctype_realloc_schedule)) {
        mir_emit_ctype_realloc_schedule(
            out, &ctype_realloc_schedule);
        return 1;
    }
    if (mir_match_context_op_schedule(&context_op_schedule)) {
        mir_emit_context_op_schedule(out, &context_op_schedule);
        return 1;
    }
    {
        int scanner_result =
            mir_try_emit_scanner_kernels(out, 0);

        if (scanner_result >= 0)
            return scanner_result;
    }
    {
        int attention_result =
            mir_try_emit_attention_kernels(out);

        if (attention_result >= 0)
            return attention_result;
    }
    {
        int numeric_result =
            mir_try_emit_numeric_kernels(out, 0);

        if (numeric_result >= 0)
            return numeric_result;
    }
    if (mir_match_format_buffer_schedule(
            &format_buffer_schedule)) {
        mir_emit_format_buffer_schedule(
            out, &format_buffer_schedule);
        return 1;
    }
    if (mir_match_atof_schedule(&atof_schedule)) {
        mir_emit_atof_schedule(out, &atof_schedule);
        return 1;
    }
    {
        int float_report_result =
            mir_try_emit_float_reports(out);

        if (float_report_result >= 0)
            return float_report_result;
    }
    {
        int scanner_result =
            mir_try_emit_scanner_kernels(out, 1);

        if (scanner_result >= 0)
            return scanner_result;
    }
    if (mir_match_affine_pointer_constant_return(&constant)) {
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
        mir_stream_printf(out, "\tld hl,%ld\n\tret\n", constant);
        return 1;
    }
    if (mir_match_local_constant_store_return(&constant)) {
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
        mir_stream_printf(out, "\tld hl,%ld\n\tret\n", constant);
        return 1;
    }
    if (mir_match_nested_row_store(&nested_row_store)) {
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
        mir_emit_nested_row_store(out, &nested_row_store);
        return 1;
    }
    if (mir_match_flat_array_checks(&flat_array_checks)) {
        mir_emit_flat_array_checks(out, &flat_array_checks);
        return 1;
    }
    if (mir_match_global_byte_checks(
            &global_byte_checks)) {
        mir_emit_global_byte_checks(
            out, &global_byte_checks);
        return 1;
    }
    if (mir_match_fixed_param_mutations(&fixed_param_mutations)) {
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
        mir_emit_fixed_param_mutations(out, &fixed_param_mutations);
        return 1;
    }
    if (mir_match_global_append(&global_append)) {
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
        mir_emit_global_append(out, &global_append);
        return 1;
    }
    if (mir_match_nested_append(&nested_append)) {
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
        mir_emit_nested_append(out, &nested_append);
        return 1;
    }
    if (mir_match_indexed_stack(&indexed_stack)) {
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
        mir_emit_indexed_stack(out, &indexed_stack);
        return 1;
    }
    if (mir_match_pointer_stack(&pointer_stack)) {
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
        mir_emit_pointer_stack(out, &pointer_stack);
        return 1;
    }
    if (mir_match_byte_memory_stack(&byte_memory_stack)) {
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
        mir_emit_byte_memory_stack(out, &byte_memory_stack);
        return 1;
    }
    if (mir_match_word_memory_stack_push(&byte_memory_stack)) {
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
        mir_emit_byte_memory_stack(out, &byte_memory_stack);
        return 1;
    }
    if (mir_match_fixed_array_reduction(&fixed_array_reduction)) {
        mir_emit_fixed_array_reduction(out, &fixed_array_reduction);
        return 1;
    }
    if (mir_match_fixed_array_affine_fill(
            &fixed_array_affine_fill)) {
        mir_emit_fixed_array_affine_fill(
            out, &fixed_array_affine_fill);
        return 1;
    }
    return -1;
}
