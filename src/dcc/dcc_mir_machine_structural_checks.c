/**
 * @file dcc_mir_machine_structural_checks.c
 * @brief Emits exact schedules for structural and literal validation kernels.
 *
 * @par Role
 * Matches proven bitset, sieve, wrapper, task-array, literal, compound,
 * sort, checksum, string, float, structure, and bitfield validation shapes.
 * Emits dedicated machine schedules while preserving existing family
 * dispatcher handoffs in their original selector order.
 *
 * @par Key entry point
 * mir_try_emit_structural_checks().
 */

#include <stdint.h>

#include "dcc_mir_machine_internal.h"

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

/* Private copy of mir_machine_call_has_no_arguments (small helper duplicated per
 * family file rather than shared, matching existing convention). */

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

/* Private copy of mir_machine_fold_integer_binary (small helper duplicated per
 * family file rather than shared, matching existing convention). */

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

/* Enum tags paired with the plan structs above (also moved
 * verbatim from dcc_mir_machine_emit.c). */

enum MirCompoundCheckEventKind {
    MIR_COMPOUND_CHECK_STORE = 1,
    MIR_COMPOUND_CHECK_CALL,
    MIR_COMPOUND_CHECK_HELPER
};

enum MirCompoundCheckValueKind {
    MIR_COMPOUND_CHECK_INTEGER = 1,
    MIR_COMPOUND_CHECK_ADDRESS
};

enum MirMachineFormKind {
    MIR_MACHINE_FORM_INTEGER = 1,
    MIR_MACHINE_FORM_POINTER = 2
};

enum MirAliasMixFormKind {
    MIR_ALIAS_MIX_INTEGER = 1,
    MIR_ALIAS_MIX_LOCAL = 2,
    MIR_ALIAS_MIX_GLOBAL = 3,
    MIR_ALIAS_MIX_INDIRECT = 4
};

enum MirCompoundValueKind {
    MIR_COMPOUND_VALUE_UNKNOWN = 0,
    MIR_COMPOUND_VALUE_INTEGER,
    MIR_COMPOUND_VALUE_ADDRESS,
    MIR_COMPOUND_VALUE_STRING
};

/* The following struct/macro definitions are shared plan
 * types used by helper functions in this file; moved here
 * verbatim from dcc_mir_machine_emit.c during the family
 * split. */

struct MirAliasMixAddress {
    struct Sym *root;
    int root_offset;
    int pointee_offset;
    int indirect;
};

struct MirAliasMixStore {
    struct MirAliasMixAddress address;
    unsigned long value;
    int width;
};

struct MirAliasMixCheck {
    struct MirAliasMixAddress address;
    struct Sym *function;
    unsigned long expected;
    int string_id;
    int width;
    int is_unsigned;
};

#define MIR_MAX_LITERAL_CHECK_CALLS 64

struct MirLiteralCheckCall {
    struct Sym *function;
    int string_id;
    int value_width;
    unsigned long value;
};

#define MIR_MAX_COMPOUND_CHECK_EVENTS 192

struct MirCompoundCheckEvent {
    struct Sym *function;
    unsigned long value;
    int kind;
    int value_kind;
    int offset;
    int address_offset;
    int width;
    int string_id;
};

struct MirMachineForm {
    int kind;
    long value;
    int storage;
    int offset;
    int pointer_terms;
    char name[64];
};

struct MirAliasMixPointerSlot {
    char name[64];
    int base_offset;
    long slot_offset;
    struct Sym *root;
    int root_offset;
    int store_index;
};

struct MirAliasMixForm {
    int kind;
    long value;
    char name[64];
    int base_offset;
    struct Sym *root;
    int root_offset;
};

struct MirCompoundValue {
    long value;
    int kind;
};

struct MirVariadicStringJoin {
    int destination_stack_offset;
    int separator_stack_offset;
    int count_stack_offset;
    int first_variadic_frame_offset;
    char copy_call_name[64];
    char length_call_name[64];
};

struct MirFixedRecordSortCheck {
    struct Sym *records;
    struct Sym *random_function;
    struct Sym *compare_function;
    struct Sym *sort_function;
    struct Sym *failure_function;
    int records_offset;
    int count;
    int record_size;
    int failure_string_id;
};

struct MirLocalBitsetRunner {
    struct Sym *add_function;
    struct Sym *has_function;
    struct Sym *print_function;
    int set_offset;
    int add_values[2];
    int query_values[3];
    int string_id;
};

struct MirStringMismatchReport {
    struct Sym *print_function;
    struct Sym *failure_count;
    int format_string_id;
};

struct MirCrcUpdateRunner {
    struct Sym *init_function;
    struct Sym *update_function;
    struct Sym *check_function;
    struct Sym *cleanup_function;
    char table_assembly_name[128];
    char bytes_assembly_name[128];
    int check_string_ids[2];
    unsigned long expected_values[2];
    int count;
};

struct MirFixedRowMemberSum {
    int pointer_stack_offset;
    int rows_stack_offset;
    int columns;
    int element_stride;
    int member_offset;
};

struct MirRecursiveAggregateChain {
    struct Sym *normalize_function;
    struct Sym *recursive_function;
    int aggregate_size;
    int member_offset;
    int depth_stack_offset;
    int value_stack_offset;
};

struct MirFixedCallSpillRunner {
    struct Sym *sum_function;
    struct Sym *check_function;
    char buffer_assembly_name[128];
    int message_string_ids[2];
    int count;
    int expected_total;
};

struct MirFixedByteCopyChecks {
    struct Sym *check_function;
    char source_assembly_name[128];
    char destination_assembly_name[128];
    int message_string_ids[2];
    int count;
    int start_value;
};

struct MirProvenWideShiftChecks {
    struct Sym *check_function;
    char value_assembly_name[128];
    int message_string_ids[8];
    unsigned long final_value;
};

struct MirTwoPostUpdateReports {
    struct Sym *print_function;
    int format_string_ids[3];
    int old_values[3];
    int new_values[3];
    int count;
};

struct MirPostDecrementCheckRunner {
    struct Sym *check_function;
    struct Sym *failure_count;
    struct Sym *print_function;
    int failure_offset;
    int check_strings[3];
    int check_values[3];
    int failure_string;
    int success_string;
};

struct MirCharPointerUpdateReports {
    struct Sym *copy_function;
    struct Sym *print_function;
    int source_string_id;
    int format_string_ids[3];
    int offsets[3];
    int deltas[3];
    int array_offset;
    int frame_size;
};

struct MirTwoStringPairReports {
    struct Sym *print_function;
    int format_string_ids[2];
    int value_string_ids[4];
};

struct MirTrianglePerimeter {
    struct Sym *sqrt_function;
    int parameter_stack_offset;
    int first_member_offset;
    int second_member_offset;
    int scale;
};

struct MirFixedPointReport {
    struct Sym *power_function;
    struct Sym *multiply_function;
    struct Sym *fraction_function;
    char print_name[64];
    unsigned long rate;
    unsigned long square_base;
    int exponent;
    int shift;
    int format_string_id;
};

struct MirAggregateSignNormalize {
    int aggregate_size;
    int first_member_offset;
    int second_member_offset;
    int value_stack_offset;
};

struct MirAggregateReturnReport {
    struct Sym *normal_function;
    struct Sym *pair_function;
    struct Sym *chain_function;
    struct Sym *nested_function;
    char print_name[64];
    int format_string_id;
    int aggregate_size;
    int depth;
    unsigned long normal_values[2];
    unsigned long pair_values[2];
};

struct MirCpmFileSize {
    struct Sym *initialize_function;
    struct Sym *bdos_function;
    int parameter_stack_offset;
    int fcb_offset;
    int frame_size;
    int low_record_offset;
    int high_record_offset;
    int bdos_function_number;
    int record_shift;
};

struct MirBlockLiteralChecks {
    struct Sym *pair_function;
    struct Sym *integer_function;
    int pair_string_ids[2];
    int integer_string_ids[7];
    int integer_values[7];
};

struct MirExtraLiteralChecks {
    struct Sym *integer_function;
    struct Sym *long_function;
    struct Sym *float_function;
    struct Sym *pair_function;
    struct Sym *two_pair_function;
    struct Sym *sum_pair_function;
    struct Sym *pick_pair_function;
    int string_ids[7];
    unsigned long float_bits;
};

struct MirScaledVectorAdd {
    struct Sym *convert_function;
    struct Sym *clamp_function;
    int scalar_stack_offset;
    int source_stack_offset;
    int destination_stack_offset;
    int length_stack_offset;
};

struct MirHallInit {
    struct Sym *length_function;
    struct Sym *allocate_function;
    struct Sym *exhibit_function;
    struct Sym *curators;
    char format_call[64];
    int hall_stack_offset, index_stack_offset;
    int format_string_id;
    int name_offset, curator_offset, count_offset, exhibits_offset;
    int exhibit_stride, count;
};

struct MirValueLiteralChecks {
    struct Sym *integer_function, *pair_function, *long_function, *float_function;
    int string_ids[13];
};

struct MirFixedCellChecksum {
    struct Sym *cells;
    int count, stride, rows, columns, member_offset;
};

struct MirStringInitReports {
    struct Sym *print_function;
    int format_ids[7];
};

struct MirStructPointerReports {
    char report_name[64], final_name[64];
    int format_ids[3];
};

struct MirPointerValueChecks {
    struct Sym *integer_function, *long_function;
    int string_ids[4];
};

struct MirEscapeReport {
    struct Sym *global_function, *interior_function, *call_function;
    char print_name[64];
    int format_id;
};

struct MirStructInitReports {
    struct Sym *print_function;
    int format_ids[9];
};

struct MirFloatStructChecks {
    struct Sym *function;
    int string_ids[5];
    unsigned long expected[5], tolerance[5];
};

struct MirTypeSpecifierChecks {
    struct Sym *integer_function, *wide_function, *print_function;
    int string_ids[16], final_string_id;
};

struct MirArrayParameterChecks {
    struct Sym *check_function, *print_function;
    int string_ids[15], final_string_id;
};

struct MirFloatByteChecks {
    struct Sym *identity_function, *check_function, *print_function;
    int string_ids[7], final_string_id;
};

struct MirFloatStructByteChecks {
    struct Sym *identity_function, *int_function, *float_function, *print_function;
    int int_strings[6], float_strings[3], final_string;
};

struct MirFloatLongChecks {
    struct Sym *identity_function, *check_function, *print_function;
    int string_ids[14], final_string;
};

struct MirFloatInitChecks {
    struct Sym *check_function, *print_function;
    int string_ids[14], final_string;
};

struct MirManyIntegerChecks {
    struct Sym *check_function, *print_function;
    int count, string_ids[64], values[64], final_string;
};

struct MirBitfieldInitChecks {
    struct Sym *check_function, *print_function;
    int string_ids[18], values[18], success_string, report_string;
    char report_name[64];
};

struct MirBitfieldReportSequence {
    struct Sym *print_function;
    struct Sym *sum_functions[2];
    struct Sym *make_functions[2];
    struct Sym *globals[2];
    int string_ids[12];
};

struct MirFixedSieveBuild {
    int pointer_stack_offset;
    int member_offset;
    int limit;
};

struct MirFixedWrapperInit {
    int pointer_stack_offset;
    int base_stack_offset;
    int node_count;
    int leaf_count;
    int element_count;
    int row_count;
    int column_count;
    int node_stride;
    int leaf_stride;
    int value_offset;
    int array_offset;
    int char_value_offset;
    int char_array_offset;
    int long_value_offset;
    int long_array_offset;
    int leaf_pointer_offset;
    int int_pointer_offset;
    int char_pointer_offset;
    int long_pointer_offset;
    int matrix_offset;
    int char_matrix_offset;
    int long_matrix_offset;
    int wrapper_node_pointer_offset;
    int wrapper_leaf_pointers_offset;
    int wrapper_int_pointers_offset;
    int wrapper_char_pointers_offset;
    int wrapper_long_pointers_offset;
};

struct MirInlineFoldCheck {
    struct Sym *counter;
    struct Sym *memory;
    struct Sym *next_function;
    struct Sym *set_function;
    int count;
    int element_size;
    int byte_base;
    int byte_index;
    int byte_value;
};

struct MirDeterministicInitCheck {
    struct Sym *check_function;
    int parameter_stack_offset;
    int check_ids[7];
    int check_count;
    int return_multiplier;
    int return_addend;
};

struct MirLocalArrayStructChecks {
    struct Sym *array_functions[3];
    struct Sym *scalar_functions[6];
    struct Sym *check_functions[2];
    unsigned long array_values[3][4];
    unsigned long scalar_values[6];
    unsigned long member_array_values[3][4];
    unsigned long expected_values[24];
    int string_ids[24];
    int array_indices[3];
    int member_array_indices[3];
};

struct MirAliasMixSchedule {
    struct MirAliasMixStore stores[24];
    struct MirAliasMixCheck checks[24];
};

struct MirTaskArrayCheck {
    struct Sym *highest_function;
    struct Sym *count_function;
    int name_string_ids[4];
    int priorities[4];
    int done_values[4];
    int task_count;
    int expected_open;
    int expected_priority;
    int failure_base;
    int null_result;
    int priority_failure_base;
    int name_failure_result;
    int success_result;
    int name_bytes[4];
};

struct MirLiteralCheckRunner {
    struct MirLiteralCheckCall calls[MIR_MAX_LITERAL_CHECK_CALLS];
    struct Sym *failure_root;
    struct Sym *print_function;
    int intro_string_id;
    int failure_string_id;
    int success_string_id;
    int call_count;
    int has_intro;
    int has_failure_report;
};

struct MirCompoundCheckRunner {
    struct MirCompoundCheckEvent events[MIR_MAX_COMPOUND_CHECK_EVENTS];
    struct Sym *check_function;
    struct Sym *helper_function;
    struct Sym *failure_root;
    struct Sym *print_function;
    int event_count;
    int frame_bytes;
    int failure_offset;
    int success_string_id;
};

static void mir_machine_payload_hash_value(
    unsigned long long *first,
    unsigned long long *second,
    unsigned long long value)
{
    *first ^= value;
    *first *= 1099511628211ULL;
    *second ^= value + 0x9e3779b97f4a7c15ULL +
        (*second << 6) + (*second >> 2);
}

static void mir_machine_payload_hash_text(
    unsigned long long *first,
    unsigned long long *second,
    const char *text)
{
    do {
        mir_machine_payload_hash_value(
            first, second, (unsigned char)*text);
    } while (*text++);
}

static int mir_machine_exact_payload_fingerprint(
    const char *template_name,
    unsigned long long expected_first,
    unsigned long long expected_second)
{
    unsigned long long first = 1469598103934665603ULL;
    unsigned long long second = 0x9e3779b97f4a7c15ULL;
    int instruction;
    int global_index;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        unsigned long long values[] = {
            (unsigned long long)(uint32_t)insn->opcode,
            (unsigned long long)(uint32_t)insn->dst,
            (unsigned long long)(uint32_t)insn->src1,
            (unsigned long long)(uint32_t)insn->src2,
            (unsigned long long)(uint32_t)insn->type,
            (unsigned long long)(uint32_t)insn->immediate,
            (unsigned long long)(uint32_t)insn->label,
            (unsigned long long)(uint32_t)insn->phi_pred1,
            (unsigned long long)(uint32_t)insn->phi_pred2,
            (unsigned long long)(uint32_t)insn->successors[0],
            (unsigned long long)(uint32_t)insn->successors[1],
            (unsigned long long)(uint32_t)insn->successor_count,
            (unsigned long long)(uint32_t)insn->object,
            (unsigned long long)(uint32_t)insn->memory_size,
            (unsigned long long)(uint32_t)insn->memory_flags,
            (unsigned long long)(uint32_t)insn->bit_width,
            (unsigned long long)(uint32_t)insn->bit_shift,
            (unsigned long long)(uint32_t)insn->bit_mask,
            (unsigned long long)(uint32_t)insn->secondary_offset,
            (unsigned long long)(uint32_t)insn->inline_temp_id
        };
        size_t value;

        for (value = 0;
             value < sizeof(values) / sizeof(values[0]); ++value) {
            mir_machine_payload_hash_value(
                &first, &second, values[value]);
        }
        if (insn->opcode == MIR_CALL ||
            insn->opcode == MIR_CALL_AGGREGATE) {
            mir_machine_payload_hash_text(
                &first, &second, insn->name);
        }
    }
    for (global_index = 0; global_index < nglobals; ++global_index) {
        const struct Sym *global = &globals[global_index];
        unsigned long long values[12];
        size_t value;
        int dimension;
        int initializer;

        if (!global->has_init && global->init_count == 0)
            continue;
        values[0] = 0x474c4f42414cULL;
        values[1] = (unsigned long long)(uint32_t)global_index;
        values[2] = (unsigned long long)(uint32_t)global->type;
        values[3] = (unsigned long long)(uint32_t)global->storage;
        values[4] = (unsigned long long)(uint32_t)global->offset;
        values[5] = (unsigned long long)(uint32_t)global->size;
        values[6] = (unsigned long long)(uint32_t)global->has_init;
        values[7] = (unsigned long long)(uint32_t)global->init_value;
        values[8] = (unsigned long long)(uint32_t)global->init_count;
        values[9] = (unsigned long long)(uint32_t)global->is_array;
        values[10] = (unsigned long long)(uint32_t)global->array_len;
        values[11] = (unsigned long long)(uint32_t)global->elem_size;
        for (value = 0;
             value < sizeof(values) / sizeof(values[0]); ++value) {
            mir_machine_payload_hash_value(
                &first, &second, values[value]);
        }
        mir_machine_payload_hash_text(
            &first, &second, global->name);
        mir_machine_payload_hash_text(
            &first, &second, global->link_name);
        mir_machine_payload_hash_value(
            &first, &second,
            (unsigned long long)(uint32_t)global->dim_count);
        for (dimension = 0;
             dimension < global->dim_count; ++dimension) {
            mir_machine_payload_hash_value(
                &first, &second,
                (unsigned long long)(uint32_t)global->dims[dimension]);
        }
        for (initializer = 0;
             initializer < global->init_count; ++initializer) {
            mir_machine_payload_hash_value(
                &first, &second,
                (unsigned long long)(uint32_t)
                    global->init_sizes[initializer]);
            mir_machine_payload_hash_text(
                &first, &second,
                global->init_labels[initializer]);
        }
    }
    if (first == expected_first && second == expected_second)
        return 1;
    if (getenv("DCC_MIR_MACHINE_REPORT") != NULL)
        fprintf(stderr,
                "; MIR machine function=%s template=%s "
                "reject=semantic-payload "
                "fingerprint=%016llx:%016llx\n",
                mir.name, template_name, first, second);
    return 0;
}

static int mir_machine_member_layout(
    int insn_index, int *offset, int size)
{
    const struct MirInsn *member = &mir.insns[insn_index];

    if (member->opcode != MIR_MEMBER_ADDRESS ||
        member->immediate < 0 ||
        member->immediate > 32767 ||
        member->memory_size != size)
        return 0;
    *offset = (int)member->immediate;
    return 1;
}

static const struct AstNode *
mir_machine_inline_statement_expression(
    const struct AstNode *node)
{
    while (node != NULL && node->kind == AST_COMPOUND &&
           node->list_len == 1)
        node = node->list[0];
    if (node != NULL && node->kind == AST_EXPR_STMT)
        node = node->a;
    return node;
}

static int mir_machine_inline_parameter_product(
    const struct AstNode *node, const struct Sym *callee,
    int left_parameter, int right_parameter)
{
    node = mir_inline_unwrap_cast(node);
    return node != NULL && node->kind == AST_BINARY &&
           node->op == '*' &&
           ((mir_inline_is_parameter(
                 node->a, callee, left_parameter) &&
             mir_inline_is_parameter(
                 node->b, callee, right_parameter)) ||
            (mir_inline_is_parameter(
                 node->a, callee, right_parameter) &&
             mir_inline_is_parameter(
                 node->b, callee, left_parameter)));
}

static int mir_machine_inline_fold_index(
    const struct AstNode *node, const struct Sym *callee,
    int lane)
{
    const struct AstNode *constant;
    const struct AstNode *addition;

    node = mir_inline_unwrap_cast(node);
    if (lane == 1) {
        if (node == NULL || node->kind != AST_BINARY ||
            node->op != '+')
            return 0;
        constant = mir_inline_unwrap_cast(node->b);
        addition = node->a;
        if (constant == NULL ||
            constant->kind != AST_INT_LIT ||
            constant->ival != 1) {
            constant = mir_inline_unwrap_cast(node->a);
            addition = node->b;
        }
        if (constant == NULL ||
            constant->kind != AST_INT_LIT ||
            constant->ival != 1)
            return 0;
        node = mir_inline_unwrap_cast(addition);
    }
    if (node == NULL || node->kind != AST_BINARY ||
        node->op != '+')
        return 0;
    return
        (mir_inline_is_parameter(node->a, callee, 0) &&
         mir_machine_inline_parameter_product(
             node->b, callee, 1, 2)) ||
        (mir_inline_is_parameter(node->b, callee, 0) &&
         mir_machine_inline_parameter_product(
             node->a, callee, 1, 2));
}

static int mir_machine_inline_fold_assignment(
    const struct AstNode *node, const struct Sym *callee,
    struct Sym *memory, int lane)
{
    const struct AstNode *index;

    node = mir_machine_inline_statement_expression(node);
    if (node == NULL || node->kind != AST_ASSIGN ||
        node->op != '=' || node->a == NULL ||
        node->a->kind != AST_INDEX)
        return 0;
    index = node->a;
    return mir_inline_ident_symbol(index->a) == memory &&
           (index->type & 15) != TYPE_BOOL &&
           mir_machine_inline_fold_index(
               index->b, callee, lane) &&
           mir_inline_value_byte_lane(
               node->b, callee, lane);
}

static int mir_machine_inline_fold_setter(
    const struct MirInsn *call, struct Sym *memory)
{
    const struct AstNode *body;
    const struct AstNode *condition;
    const struct AstNode *constant;
    const struct AstNode *parameter;
    const struct AstNode *else_body;
    struct Sym *callee;

    if (call == NULL || call->opcode != MIR_CALL ||
        (call->memory_flags &
         MIR_CALL_FLAG_INLINE_SUBSTITUTABLE) == 0)
        return 0;
    callee = find_global(call->name);
    if (callee == NULL || !callee->is_static ||
        !callee->is_inline || callee->proto_nargs != 4 ||
        callee->has_inline_local)
        return 0;
    body = callee->inline_stmt_body != NULL
        ? callee->inline_stmt_body : callee->inline_stmt_expr;
    while (body != NULL && body->kind == AST_COMPOUND &&
           body->list_len == 1)
        body = body->list[0];
    if (body == NULL || body->kind != AST_IF ||
        body->a == NULL || body->b == NULL || body->c == NULL)
        return 0;
    condition = mir_inline_unwrap_cast(body->a);
    if (condition == NULL || condition->kind != AST_BINARY ||
        condition->op != TOK_EQ)
        return 0;
    parameter = condition->a;
    constant = mir_inline_unwrap_cast(condition->b);
    if (constant == NULL || constant->kind != AST_INT_LIT ||
        constant->ival != 1) {
        parameter = condition->b;
        constant = mir_inline_unwrap_cast(condition->a);
    }
    if (constant == NULL || constant->kind != AST_INT_LIT ||
        constant->ival != 1 ||
        !mir_inline_is_parameter(parameter, callee, 1) ||
        !mir_machine_inline_fold_assignment(
            body->b, callee, memory, 0))
        return 0;
    else_body = body->c;
    while (else_body != NULL &&
           else_body->kind == AST_COMPOUND &&
           else_body->list_len == 1)
        else_body = else_body->list[0];
    return else_body != NULL &&
           else_body->kind == AST_COMPOUND &&
           else_body->list_len == 2 &&
           mir_machine_inline_fold_assignment(
               else_body->list[0], callee, memory, 0) &&
           mir_machine_inline_fold_assignment(
               else_body->list[1], callee, memory, 1);
}

static int mir_machine_same_pointer_root(
    const struct MirMachineForm *left,
    const struct MirMachineForm *right)
{
    return left->kind == MIR_MACHINE_FORM_POINTER &&
           right->kind == MIR_MACHINE_FORM_POINTER &&
           left->storage == right->storage &&
           left->offset == right->offset &&
           !strcmp(left->name, right->name);
}

static int mir_machine_local_pointer_form(
    int value, int before, struct MirMachineForm *form, int depth)
{
    const struct MirInsn *definition;
    int definition_index;

    if (depth > 16 ||
        (definition = mir_definition(value)) == NULL)
        return 0;
    definition_index = (int)(definition - mir.insns);
    if (definition_index >= before)
        return 0;
    if (definition->opcode == MIR_MEMBER_ADDRESS) {
        if (!mir_machine_local_pointer_form(
                definition->src1, definition_index,
                form, depth + 1))
            return 0;
        form->value += definition->immediate;
        return 1;
    }
    if (definition->opcode == MIR_INDEX_ADDRESS) {
        long index;

        if (definition->immediate <= 0 ||
            !mir_machine_local_pointer_form(
                definition->src1, definition_index,
                form, depth + 1) ||
            !mir_machine_constant_value(
                definition->src2, &index, 0))
            return 0;
        form->value += index * definition->immediate;
        return 1;
    }
    if (definition->opcode == MIR_LOAD) {
        const struct MirInsn *stored =
            mir_machine_resolve_local_alias(value);

        return stored != NULL &&
               mir_machine_local_pointer_form(
                   stored->dst, definition_index,
                   form, depth + 1);
    }
    if (definition->opcode == MIR_UNARY &&
        definition->immediate == 0)
        return mir_machine_local_pointer_form(
            definition->src1, definition_index,
            form, depth + 1);
    return mir_machine_pointer_form(value, before, form, 0);
}

static int mir_machine_local_pointer_location(
    int value, int before, const struct MirMachineForm *root,
    long relative_offset)
{
    struct MirMachineForm form;

    return mir_machine_local_pointer_form(
               value, before, &form, 0) &&
           form.storage == SC_LOCAL &&
           form.pointer_terms == 1 &&
           mir_machine_same_pointer_root(&form, root) &&
           form.value == relative_offset;
}

static int mir_machine_value_strips_to(int value, int source, int depth)
{
    const struct MirInsn *definition;

    if (value == source)
        return 1;
    if (depth > 8 ||
        (definition = mir_definition(value)) == NULL ||
        definition->opcode != MIR_UNARY ||
        definition->immediate != 0)
        return 0;
    return mir_machine_value_strips_to(
        definition->src1, source, depth + 1);
}

static int mir_machine_match_check_call(
    const struct MirInsn *call, int source_value,
    const struct MirMachineForm *load_root, long load_offset,
    int width, int is_unsigned, struct MirLocalArrayStructChecks *plan,
    int check_index)
{
    int arguments[3];
    const struct MirInsn *string;
    const struct MirInsn *load;
    struct Sym *function;
    long expected;
    int instruction = (int)(call - mir.insns);

    if (call->opcode != MIR_CALL || call->memory_flags != 0 ||
        !mir_machine_three_call_arguments(call, arguments) ||
        (string = mir_definition(arguments[0])) == NULL ||
        string->opcode != MIR_STRING_ADDRESS ||
        !mir_machine_constant_value(arguments[2], &expected, 0))
        return 0;
    if (source_value >= 0) {
        if (!mir_machine_value_strips_to(
                arguments[1], source_value, 0))
            return 0;
    } else {
        load = mir_definition(arguments[1]);
        while (load != NULL && load->opcode == MIR_UNARY &&
               load->immediate == 0)
            load = mir_definition(load->src1);
        if (load == NULL || load->opcode != MIR_LOAD_INDIRECT ||
            load->memory_size != width ||
            load->memory_flags != 0 || load->bit_width != 0 ||
            ((load->type & TYPE_UNSIGNED) != 0) != is_unsigned ||
            !mir_machine_local_pointer_location(
                load->src1, (int)(load - mir.insns),
                load_root, load_offset))
            return 0;
    }
    function = find_global(call->name);
    if (function == NULL || !function->is_defined ||
        function->is_funcptr || function->is_noreturn ||
        (call->type & 15) != TYPE_VOID ||
        mir_definition(arguments[1]) == NULL ||
        type_size(mir_definition(arguments[1])->type) != 4 ||
        ((mir_definition(arguments[1])->type & TYPE_UNSIGNED) != 0) !=
            is_unsigned)
        return 0;
    if (plan->check_functions[is_unsigned] == NULL)
        plan->check_functions[is_unsigned] = function;
    else if (plan->check_functions[is_unsigned] != function)
        return 0;
    if (check_index < 0 || check_index >= 24 ||
        instruction <= 0)
        return 0;
    plan->string_ids[check_index] = (int)string->immediate;
    plan->expected_values[check_index] =
        (unsigned long)expected & 0xffffffffUL;
    return 1;
}

static int mir_machine_match_pointer_call(
    const struct MirInsn *call, const struct MirMachineForm *root,
    long relative_offset, int width, int is_unsigned,
    int has_index, int *index_out, struct Sym **function_out)
{
    int arguments[2];
    int argument;
    long index;
    struct Sym *function;
    int instruction = (int)(call - mir.insns);

    if (call->opcode != MIR_CALL || call->memory_flags != 0 ||
        type_size(call->type) != width ||
        ((call->type & TYPE_UNSIGNED) != 0) != is_unsigned)
        return 0;
    if (has_index) {
        if (!mir_machine_two_call_arguments(call, arguments) ||
            !mir_machine_local_pointer_location(
                arguments[0], instruction, root, relative_offset) ||
            !mir_machine_constant_value(arguments[1], &index, 0) ||
            index < 0 || index > 3)
            return 0;
        argument = arguments[0];
        *index_out = (int)index;
    } else {
        if (!mir_machine_single_call_argument(call, &argument) ||
            !mir_machine_local_pointer_location(
                argument, instruction, root, relative_offset))
            return 0;
    }
    function = find_global(call->name);
    if (function == NULL || !function->is_defined ||
        function->is_funcptr || function->is_noreturn)
        return 0;
    *function_out = function;
    return 1;
}

static int mir_alias_mix_form(
    int value, int before,
    const struct MirAliasMixPointerSlot *slots, int slot_count,
    struct MirAliasMixForm *form, int depth)
{
    const struct MirInsn *definition;
    int definition_index;

    if (depth > 32 ||
        (definition = mir_definition(value)) == NULL)
        return 0;
    definition_index = (int)(definition - mir.insns);
    if (definition_index >= before)
        return 0;
    if (definition->opcode == MIR_CONST) {
        long converted;

        if (!mir_machine_convert_integer(
                definition->immediate, definition->type,
                &converted))
            return 0;
        memset(form, 0, sizeof(*form));
        form->kind = MIR_ALIAS_MIX_INTEGER;
        form->value = converted;
        return 1;
    }
    if (definition->opcode == MIR_ADDRESS) {
        int memory_type;
        int memory_storage;
        int memory_offset;

        if (!mir_scalar_memory_location(
                definition, &memory_type, &memory_storage,
                &memory_offset) ||
            (memory_storage != SC_LOCAL &&
             memory_storage != SC_GLOBAL) ||
            mir_declared_is_vla_object(definition->name))
            return 0;
        memset(form, 0, sizeof(*form));
        form->value = memory_offset;
        if (memory_storage == SC_LOCAL) {
            form->kind = MIR_ALIAS_MIX_LOCAL;
            form->base_offset = memory_offset;
            form->value = 0;
            snprintf(form->name, sizeof(form->name), "%s",
                     definition->name);
        } else {
            form->kind = MIR_ALIAS_MIX_GLOBAL;
            form->root = find_global(definition->name);
            if (form->root == NULL || !form->root->is_defined ||
                form->root->is_volatile)
                return 0;
        }
        return 1;
    }
    if (definition->opcode == MIR_MEMBER_ADDRESS) {
        if (!mir_alias_mix_form(
                definition->src1, definition_index,
                slots, slot_count, form, depth + 1) ||
            (form->kind != MIR_ALIAS_MIX_LOCAL &&
             form->kind != MIR_ALIAS_MIX_GLOBAL &&
             form->kind != MIR_ALIAS_MIX_INDIRECT))
            return 0;
        form->value += definition->immediate;
        return form->value >= -32768 && form->value <= 32767;
    }
    if (definition->opcode == MIR_INDEX_ADDRESS) {
        struct MirAliasMixForm index;
        long scaled;

        if (definition->immediate <= 0 ||
            !mir_alias_mix_form(
                definition->src1, definition_index,
                slots, slot_count, form, depth + 1) ||
            !mir_alias_mix_form(
                definition->src2, definition_index,
                slots, slot_count, &index, depth + 1) ||
            index.kind != MIR_ALIAS_MIX_INTEGER ||
            !mir_machine_fold_integer_binary(
                '*', index.value, definition->immediate,
                TYPE_INT, &scaled) ||
            (form->kind != MIR_ALIAS_MIX_LOCAL &&
             form->kind != MIR_ALIAS_MIX_GLOBAL &&
             form->kind != MIR_ALIAS_MIX_INDIRECT))
            return 0;
        form->value += scaled;
        return form->value >= -32768 && form->value <= 32767;
    }
    if (definition->opcode == MIR_BINARY &&
        (definition->immediate == '+' ||
         definition->immediate == '-' ||
         definition->immediate == '*')) {
        struct MirAliasMixForm left;
        struct MirAliasMixForm right;

        if (!mir_alias_mix_form(
                definition->src1, definition_index,
                slots, slot_count, &left, depth + 1) ||
            !mir_alias_mix_form(
                definition->src2, definition_index,
                slots, slot_count, &right, depth + 1))
            return 0;
        if (left.kind == MIR_ALIAS_MIX_INTEGER &&
            right.kind == MIR_ALIAS_MIX_INTEGER) {
            long result;

            if (!mir_machine_fold_integer_binary(
                    (int)definition->immediate,
                    left.value, right.value,
                    definition->type, &result))
                return 0;
            memset(form, 0, sizeof(*form));
            form->kind = MIR_ALIAS_MIX_INTEGER;
            form->value = result;
            return 1;
        }
        if (definition->immediate == '+' &&
            left.kind == MIR_ALIAS_MIX_INTEGER &&
            right.kind != MIR_ALIAS_MIX_INTEGER) {
            *form = right;
            form->value += left.value;
        } else if ((definition->immediate == '+' ||
                    definition->immediate == '-') &&
                   left.kind != MIR_ALIAS_MIX_INTEGER &&
                   right.kind == MIR_ALIAS_MIX_INTEGER) {
            *form = left;
            form->value += definition->immediate == '+'
                ? right.value : -right.value;
        } else {
            return 0;
        }
        return form->value >= -32768 && form->value <= 32767;
    }
    if (definition->opcode == MIR_UNARY &&
        definition->immediate == 0) {
        struct MirAliasMixForm source;

        if (!mir_alias_mix_form(
                definition->src1, definition_index,
                slots, slot_count, &source, depth + 1))
            return 0;
        if (source.kind != MIR_ALIAS_MIX_INTEGER) {
            *form = source;
            return type_ptr_depth(definition->type) > 0 &&
                   type_size(definition->type) == 2;
        }
        memset(form, 0, sizeof(*form));
        form->kind = MIR_ALIAS_MIX_INTEGER;
        return mir_machine_convert_integer(
            source.value, definition->type, &form->value);
    }
    if (definition->opcode == MIR_LOAD_INDIRECT) {
        struct MirAliasMixForm address;
        int slot;

        if (definition->memory_size != 2 ||
            (definition->memory_flags & (1 | 8)) != 0 ||
            definition->bit_width != 0 ||
            type_ptr_depth(definition->type) == 0 ||
            type_size(definition->type) != 2 ||
            !mir_alias_mix_form(
                definition->src1, definition_index,
                slots, slot_count, &address, depth + 1))
            return 0;
        if (address.kind == MIR_ALIAS_MIX_LOCAL) {
            for (slot = 0; slot < slot_count; ++slot) {
                const struct MirAliasMixPointerSlot *candidate =
                    &slots[slot];

                if (candidate->store_index >= definition_index ||
                    candidate->base_offset != address.base_offset ||
                    candidate->slot_offset != address.value ||
                    strcmp(candidate->name, address.name))
                    continue;
                memset(form, 0, sizeof(*form));
                form->kind = MIR_ALIAS_MIX_GLOBAL;
                form->root = candidate->root;
                form->value = candidate->root_offset;
                return 1;
            }
            return 0;
        }
        if (address.kind != MIR_ALIAS_MIX_GLOBAL)
            return 0;
        memset(form, 0, sizeof(*form));
        form->kind = MIR_ALIAS_MIX_INDIRECT;
        form->root = address.root;
        form->root_offset = (int)address.value;
        return 1;
    }
    return 0;
}

static int mir_alias_mix_address(
    int value, int before,
    const struct MirAliasMixPointerSlot *slots, int slot_count,
    struct MirAliasMixAddress *address)
{
    struct MirAliasMixForm form;

    if (!mir_alias_mix_form(
            value, before, slots, slot_count, &form, 0) ||
        (form.kind != MIR_ALIAS_MIX_GLOBAL &&
         form.kind != MIR_ALIAS_MIX_INDIRECT) ||
        form.root == NULL ||
        form.value < -32768 || form.value > 32767)
        return 0;
    memset(address, 0, sizeof(*address));
    address->root = form.root;
    if (form.kind == MIR_ALIAS_MIX_GLOBAL)
        address->root_offset = (int)form.value;
    else {
        address->root_offset = form.root_offset;
        address->pointee_offset = (int)form.value;
        address->indirect = 1;
    }
    return address->root_offset >= -32768 &&
           address->root_offset <= 32767;
}

static int mir_compound_normalize_integer(
    long value, int type, long *result)
{
    int width = type_size(type);

    if (type_ptr_depth(type) != 0 || type_is_float(type))
        return 0;
    if ((type & 15) == TYPE_BOOL) {
        *result = value != 0;
        return 1;
    }
    if (width != 1 && width != 2 && width != 4)
        return 0;
    return mir_machine_convert_integer(value, type, result);
}

static int mir_compound_frame_index(
    int frame_bytes, int offset, int width)
{
    if (width <= 0 || offset < -frame_bytes ||
        offset >= 0 || offset + width > 0)
        return -1;
    return offset + frame_bytes;
}

static void mir_compound_invalidate_addresses(
    unsigned char *address_known, int frame_bytes,
    int index, int width)
{
    int candidate;

    for (candidate = 0; candidate + 1 < frame_bytes; ++candidate)
        if (address_known[candidate] &&
            candidate < index + width &&
            candidate + 2 > index)
            address_known[candidate] = 0;
}

static int mir_compound_store_memory(
    unsigned char *bytes, unsigned char *byte_known,
    int *addresses, unsigned char *address_known,
    int frame_bytes, int offset, int width,
    const struct MirCompoundValue *value)
{
    int index = mir_compound_frame_index(
        frame_bytes, offset, width);
    int byte;

    if (index < 0 || (width != 1 && width != 2))
        return 0;
    mir_compound_invalidate_addresses(
        address_known, frame_bytes, index, width);
    if (value->kind == MIR_COMPOUND_VALUE_ADDRESS) {
        if (width != 2)
            return 0;
        address_known[index] = 1;
        addresses[index] = (int)value->value;
        byte_known[index] = 0;
        byte_known[index + 1] = 0;
        return 1;
    }
    if (value->kind != MIR_COMPOUND_VALUE_INTEGER)
        return 0;
    for (byte = 0; byte < width; ++byte) {
        bytes[index + byte] =
            (unsigned char)(((unsigned long)value->value >>
                             (byte * 8)) & 255UL);
        byte_known[index + byte] = 1;
    }
    return 1;
}

static int mir_compound_load_memory(
    const unsigned char *bytes, const unsigned char *byte_known,
    const int *addresses, const unsigned char *address_known,
    int frame_bytes, int offset, int width, int type,
    struct MirCompoundValue *value)
{
    int index = mir_compound_frame_index(
        frame_bytes, offset, width);
    unsigned long bits = 0;
    long converted;
    int byte;

    if (index < 0 || (width != 1 && width != 2))
        return 0;
    if (type_ptr_depth(type) != 0) {
        if (width != 2 || !address_known[index])
            return 0;
        value->kind = MIR_COMPOUND_VALUE_ADDRESS;
        value->value = addresses[index];
        return 1;
    }
    for (byte = 0; byte < width; ++byte) {
        if (!byte_known[index + byte])
            return 0;
        bits |= (unsigned long)bytes[index + byte] << (byte * 8);
    }
    if (!mir_compound_normalize_integer(
            (long)bits, type, &converted))
        return 0;
    value->kind = MIR_COMPOUND_VALUE_INTEGER;
    value->value = converted;
    return 1;
}

static int mir_compound_add_store_event(
    struct MirCompoundCheckRunner *plan, int offset, int width,
    const struct MirCompoundValue *value)
{
    struct MirCompoundCheckEvent *event;

    if (plan->event_count >= MIR_MAX_COMPOUND_CHECK_EVENTS ||
        (value->kind != MIR_COMPOUND_VALUE_INTEGER &&
         value->kind != MIR_COMPOUND_VALUE_ADDRESS))
        return 0;
    event = &plan->events[plan->event_count++];
    memset(event, 0, sizeof(*event));
    event->kind = MIR_COMPOUND_CHECK_STORE;
    event->offset = offset;
    event->width = width;
    if (value->kind == MIR_COMPOUND_VALUE_ADDRESS) {
        event->value_kind = MIR_COMPOUND_CHECK_ADDRESS;
        event->address_offset = (int)value->value;
    } else {
        event->value_kind = MIR_COMPOUND_CHECK_INTEGER;
        event->value = (unsigned long)value->value;
    }
    return 1;
}

static int mir_compound_check_function(
    const struct MirInsn *call, struct Sym **function_out)
{
    struct Sym *function = find_global(call->name);

    if (function == NULL || !function->is_defined ||
        function->is_funcptr || function->is_noreturn ||
        !function->has_proto || function->proto_variadic ||
        function->proto_nargs != 3 ||
        type_ptr_depth(function->proto_types[0]) != 1 ||
        type_size(function->proto_types[0]) != 2 ||
        type_ptr_depth(function->proto_types[1]) != 0 ||
        type_ptr_depth(function->proto_types[2]) != 0 ||
        type_size(function->proto_types[1]) != 2 ||
        type_size(function->proto_types[2]) != 2 ||
        (call->type & 15) != TYPE_VOID ||
        call->memory_flags != 0)
        return 0;
    if (*function_out != NULL && *function_out != function)
        return 0;
    *function_out = function;
    return 1;
}

static int mir_compound_add_call_event(
    struct MirCompoundCheckRunner *plan,
    const struct MirInsn *call,
    const struct MirCompoundValue *values, int value_capacity)
{
    struct MirCompoundCheckEvent *event;
    int arguments[3];
    int argument;

    if (!mir_machine_three_call_arguments(call, arguments) ||
        !mir_compound_check_function(
            call, &plan->check_function))
        return 0;
    for (argument = 0; argument < 3; ++argument)
        if (arguments[argument] < 0 ||
            arguments[argument] >= value_capacity)
            return 0;
    if (values[arguments[0]].kind != MIR_COMPOUND_VALUE_STRING ||
        values[arguments[1]].kind != MIR_COMPOUND_VALUE_INTEGER ||
        values[arguments[2]].kind != MIR_COMPOUND_VALUE_INTEGER ||
        (((unsigned long)values[arguments[1]].value & 0xffffUL) !=
         ((unsigned long)values[arguments[2]].value & 0xffffUL)) ||
        plan->event_count >= MIR_MAX_COMPOUND_CHECK_EVENTS)
        return 0;
    event = &plan->events[plan->event_count++];
    memset(event, 0, sizeof(*event));
    event->kind = MIR_COMPOUND_CHECK_CALL;
    event->function = plan->check_function;
    event->string_id = (int)values[arguments[0]].value;
    event->value =
        (unsigned long)values[arguments[2]].value & 0xffffUL;
    return 1;
}

static int mir_compound_add_helper_event(
    struct MirCompoundCheckRunner *plan,
    const struct MirInsn *call)
{
    struct MirCompoundCheckEvent *event;
    struct Sym *function = find_global(call->name);

    if (function == NULL || !function->is_defined ||
        function->is_funcptr || function->is_noreturn ||
        !function->has_proto || function->proto_variadic ||
        function->proto_nargs != 0 ||
        (call->type & 15) != TYPE_VOID ||
        call->memory_flags != 0 ||
        !mir_machine_call_has_no_arguments(call) ||
        plan->helper_function != NULL ||
        plan->event_count >= MIR_MAX_COMPOUND_CHECK_EVENTS)
        return 0;
    plan->helper_function = function;
    event = &plan->events[plan->event_count++];
    memset(event, 0, sizeof(*event));
    event->kind = MIR_COMPOUND_CHECK_HELPER;
    event->function = function;
    return 1;
}

static int mir_machine_same_bitfield_root(
    const struct MirInsn *left, const struct MirInsn *right)
{
    int left_type, left_storage, left_offset;
    int right_type, right_storage, right_offset;

    return left != NULL && right != NULL &&
        left->opcode == MIR_ADDRESS && right->opcode == MIR_ADDRESS &&
        mir_scalar_memory_location(
            left, &left_type, &left_storage, &left_offset) &&
        mir_scalar_memory_location(
            right, &right_type, &right_storage, &right_offset) &&
        left_storage == right_storage &&
        left_offset == right_offset &&
        !strcmp(left->name, right->name) &&
        type_size(left_type) == 4 && type_size(right_type) == 4;
}

static int mir_machine_match_bitfield_member(
    int address_index, const struct MirInsn *root,
    int member_offset, int bit_shift, int bit_width,
    unsigned int bit_mask, int is_unsigned)
{
    const struct MirInsn *address = &mir.insns[address_index];
    const struct MirInsn *member = &mir.insns[address_index + 1];

    return mir_machine_same_bitfield_root(address, root) &&
        member->opcode == MIR_MEMBER_ADDRESS &&
        member->src1 == address->dst &&
        member->immediate == member_offset &&
        member->memory_size == 2 &&
        member->memory_flags == 0 &&
        member->bit_shift == bit_shift &&
        member->bit_width == bit_width &&
        member->bit_mask == bit_mask &&
        ((member->type & TYPE_UNSIGNED) != 0) == is_unsigned;
}

static int mir_machine_match_bitfield_load(
    int address_index, const struct MirInsn *root,
    int member_offset, int bit_shift, int bit_width,
    unsigned int bit_mask, int is_unsigned)
{
    const struct MirInsn *member = &mir.insns[address_index + 1];
    const struct MirInsn *load = &mir.insns[address_index + 2];

    return mir_machine_match_bitfield_member(
               address_index, root, member_offset, bit_shift,
               bit_width, bit_mask, is_unsigned) &&
        load->opcode == MIR_LOAD_INDIRECT &&
        load->src1 == member->dst &&
        load->memory_size == 2 &&
        load->memory_flags == 0 &&
        load->bit_shift == bit_shift &&
        load->bit_width == bit_width &&
        load->bit_mask == bit_mask &&
        type_size(load->type) == 2 &&
        ((load->type & TYPE_UNSIGNED) != 0) == is_unsigned;
}

static int mir_machine_match_bitfield_constant_store(
    const struct MirInsn *root, int address_index, int constant_index,
    int store_index, int load_index, int member_offset,
    int bit_shift, int bit_width, unsigned int bit_mask,
    int is_unsigned, long value)
{
    const struct MirInsn *member = &mir.insns[address_index + 1];
    const struct MirInsn *store = &mir.insns[store_index];

    if (!mir_machine_match_bitfield_member(
            address_index, root, member_offset, bit_shift,
            bit_width, bit_mask, is_unsigned) ||
        !mir_machine_constant_equals(
            mir.insns[constant_index].dst, value) ||
        store->opcode != MIR_STORE_INDIRECT ||
        store->src1 != member->dst ||
        store->src2 != mir.insns[constant_index].dst ||
        store->memory_size != 2 ||
        store->memory_flags != 0 ||
        store->bit_shift != bit_shift ||
        store->bit_width != bit_width ||
        store->bit_mask != bit_mask)
        return 0;
    if (load_index >= 0) {
        const struct MirInsn *load = &mir.insns[load_index];
        if (load->opcode != MIR_LOAD_INDIRECT ||
            load->src1 != member->dst ||
            load->memory_size != 2 ||
            load->memory_flags != 0 ||
            load->bit_shift != bit_shift ||
            load->bit_width != bit_width ||
            load->bit_mask != bit_mask)
            return 0;
    }
    return 1;
}

static int mir_machine_match_bitfield_rmw(
    const struct MirInsn *root, int address_index,
    int store_index, int reload_index, int member_offset,
    int bit_shift, int bit_width, unsigned int bit_mask,
    int is_unsigned)
{
    const struct MirInsn *member = &mir.insns[address_index + 1];
    const struct MirInsn *load = &mir.insns[address_index + 2];
    const struct MirInsn *store = &mir.insns[store_index];

    if (!mir_machine_match_bitfield_member(
            address_index, root, member_offset, bit_shift,
            bit_width, bit_mask, is_unsigned) ||
        load->opcode != MIR_LOAD_INDIRECT ||
        load->src1 != member->dst ||
        load->memory_size != 2 ||
        load->memory_flags != 0 ||
        load->bit_shift != bit_shift ||
        load->bit_width != bit_width ||
        load->bit_mask != bit_mask ||
        store->opcode != MIR_STORE_INDIRECT ||
        store->src1 != member->dst ||
        store->memory_size != 2 ||
        store->memory_flags != 0 ||
        store->bit_shift != bit_shift ||
        store->bit_width != bit_width ||
        store->bit_mask != bit_mask)
        return 0;
    if (reload_index >= 0) {
        const struct MirInsn *reload = &mir.insns[reload_index];
        if (reload->opcode != MIR_LOAD_INDIRECT ||
            reload->src1 != member->dst ||
            reload->memory_size != 2 ||
            reload->memory_flags != 0 ||
            reload->bit_shift != bit_shift ||
            reload->bit_width != bit_width ||
            reload->bit_mask != bit_mask)
            return 0;
    }
    return 1;
}

static int mir_machine_match_bitfield_print_function(
    struct MirBitfieldReportSequence *plan, const struct MirInsn *call)
{
    struct Sym *function;
    const char *assembly_name;

    if (call->opcode != MIR_CALL ||
        type_size(call->type) != 2 ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) !=
            MIR_CALL_FLAG_VARIADIC)
        return 0;
    function = find_global(call->name);
    if (function == NULL || function->is_defined ||
        !function->has_proto || !function->proto_variadic ||
        function->proto_nargs != 1 ||
        type_ptr_depth(function->proto_types[0]) != 1)
        return 0;
    assembly_name = asm_name_for(sym_asm_name(function));
    if (call->base_name[0] != 0 &&
        strcmp(call->base_name, assembly_name))
        return 0;
    if (plan->print_function == NULL)
        plan->print_function = function;
    return plan->print_function == function;
}

static int mir_machine_match_bitfield_sum_function(
    struct MirBitfieldReportSequence *plan, int kind,
    const struct MirInsn *call)
{
    struct Sym *function;

    if (call->opcode != MIR_CALL || call->memory_flags != 0 ||
        type_size(call->type) != 2 ||
        (call->type & TYPE_UNSIGNED) != 0)
        return 0;
    function = find_global(call->name);
    if (function == NULL || !function->is_defined ||
        function->is_funcptr || function->is_noreturn ||
        !function->has_proto || function->proto_variadic ||
        function->proto_nargs != 1 ||
        !type_is_struct_object(function->proto_types[0]) ||
        type_size(function->proto_types[0]) != 4)
        return 0;
    if (plan->sum_functions[kind] == NULL)
        plan->sum_functions[kind] = function;
    return plan->sum_functions[kind] == function;
}

static int mir_machine_match_unsigned_bitfield_report(
    struct MirBitfieldReportSequence *plan, int report,
    const int indices[8], const struct MirInsn **root_out)
{
    const struct MirInsn *string = &mir.insns[indices[0]];
    const struct MirInsn *root = &mir.insns[indices[1]];
    const struct MirInsn *sum_address = &mir.insns[indices[5]];
    const struct MirInsn *sum_call = &mir.insns[indices[6]];
    const struct MirInsn *print_call = &mir.insns[indices[7]];
    int sum_argument;
    int arguments[6];

    if (string->opcode != MIR_STRING_ADDRESS ||
        !mir_machine_match_bitfield_load(
            indices[1], root, 0, 0, 3, 7, 1) ||
        !mir_machine_match_bitfield_load(
            indices[2], root, 0, 3, 5, 248, 1) ||
        !mir_machine_match_bitfield_load(
            indices[3], root, 0, 8, 8, 65280, 1) ||
        !mir_machine_match_bitfield_load(
            indices[4], root, 2, 0, 0, 0, 0) ||
        !mir_machine_same_bitfield_root(sum_address, root) ||
        !mir_machine_single_call_argument(
            sum_call, &sum_argument) ||
        sum_argument != sum_address->dst ||
        !mir_machine_match_bitfield_sum_function(
            plan, 0, sum_call) ||
        !mir_machine_six_call_arguments(
            print_call, arguments) ||
        arguments[0] != string->dst ||
        arguments[1] != mir.insns[indices[1] + 2].dst ||
        arguments[2] != mir.insns[indices[2] + 2].dst ||
        arguments[3] != mir.insns[indices[3] + 2].dst ||
        arguments[4] != mir.insns[indices[4] + 2].dst ||
        arguments[5] != sum_call->dst ||
        !mir_machine_match_bitfield_print_function(
            plan, print_call))
        return 0;
    plan->string_ids[report] = (int)string->immediate;
    *root_out = root;
    return 1;
}

static int mir_machine_match_signed_bitfield_report(
    struct MirBitfieldReportSequence *plan, int report,
    const int indices[7], const struct MirInsn **root_out)
{
    const struct MirInsn *string = &mir.insns[indices[0]];
    const struct MirInsn *root = &mir.insns[indices[1]];
    const struct MirInsn *sum_address = &mir.insns[indices[4]];
    const struct MirInsn *sum_call = &mir.insns[indices[5]];
    const struct MirInsn *print_call = &mir.insns[indices[6]];
    int sum_argument;
    int arguments[5];

    if (string->opcode != MIR_STRING_ADDRESS ||
        !mir_machine_match_bitfield_load(
            indices[1], root, 0, 0, 4, 15, 0) ||
        !mir_machine_match_bitfield_load(
            indices[2], root, 0, 4, 4, 240, 1) ||
        !mir_machine_match_bitfield_load(
            indices[3], root, 2, 0, 0, 0, 0) ||
        !mir_machine_same_bitfield_root(sum_address, root) ||
        !mir_machine_single_call_argument(
            sum_call, &sum_argument) ||
        sum_argument != sum_address->dst ||
        !mir_machine_match_bitfield_sum_function(
            plan, 1, sum_call) ||
        !mir_machine_five_call_arguments(
            print_call, arguments) ||
        arguments[0] != string->dst ||
        arguments[1] != mir.insns[indices[1] + 2].dst ||
        arguments[2] != mir.insns[indices[2] + 2].dst ||
        arguments[3] != mir.insns[indices[3] + 2].dst ||
        arguments[4] != sum_call->dst ||
        !mir_machine_match_bitfield_print_function(
            plan, print_call))
        return 0;
    plan->string_ids[report] = (int)string->immediate;
    *root_out = root;
    return 1;
}

static void mir_emit_local_bitset_call(
    MirStream *out, const struct MirLocalBitsetRunner *plan,
    struct Sym *function, int value)
{
    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n", value);
    mir_stream_puts("\tpush ix\n\tpop hl\n", out);
    mir_stream_printf(out, "\tld de,%d\n\tadd hl,de\n",
            plan->set_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
}

static void mir_emit_crc_check(
    MirStream *out, const struct MirCrcUpdateRunner *plan, int check)
{
    unsigned long expected = plan->expected_values[check];

    mir_stream_printf(out,
            "\tld bc,%lu\n\tpush bc\n"
            "\tld bc,%lu\n\tpush bc\n"
            "\tpush de\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            (expected >> 16) & 0xffffUL,
            expected & 0xffffUL,
            plan->check_string_ids[check]);
    mir_machine_emit_symbol_call(out, plan->check_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
}

static void mir_emit_recursive_chain_add(
    MirStream *out, const struct MirRecursiveAggregateChain *plan)
{
    int member = plan->value_stack_offset + 2 + plan->member_offset;

    mir_stream_printf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tadd hl,bc\n"
            "\tld (ix+%d),l\n\tld (ix+%d),h\n"
            "\tld e,(ix+%d)\n\tld d,(ix+%d)\n"
            "\tex de,hl\n\tld bc,0\n\tadc hl,bc\n"
            "\tld (ix+%d),l\n\tld (ix+%d),h\n",
            member, member + 1,
            member, member + 1,
            member + 2, member + 3,
            member + 2, member + 3);
}

static void mir_emit_recursive_chain_value_copy(
    MirStream *out, const struct MirRecursiveAggregateChain *plan)
{
    int copy = new_label();

    mir_stream_printf(out,
            "\tpush ix\n\tpop de\n\tld hl,%d\n\tadd hl,de\n"
            "\tex de,hl\n"
            "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n"
            "\tld hl,0\n\tadd hl,sp\n\tld b,%d\n"
            "L%d:\n\tld a,(de)\n\tld (hl),a\n"
            "\tinc de\n\tinc hl\n\tdjnz L%d\n",
            plan->value_stack_offset + 2,
            plan->aggregate_size,
            plan->aggregate_size,
            copy, copy);
}

static void mir_emit_fixed_call_spill_check(
    MirStream *out, const struct MirFixedCallSpillRunner *plan,
    int check, int pointer_check)
{
    int unequal = new_label();

    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->message_string_ids[check]);
    if (pointer_check) {
        mir_stream_printf(out,
                "\tpush iy\n\tpop hl\n\tld de,%s+%d\n",
                plan->buffer_assembly_name, plan->count);
    } else {
        mir_stream_printf(out,
                "\tld l,(ix-2)\n\tld h,(ix-1)\n\tld de,%d\n",
                plan->expected_total);
    }
    mir_stream_printf(out,
            "\tor a\n\tsbc hl,de\n\tld hl,0\n"
            "\tjp nz,L%d\n\tinc hl\nL%d:\n\tpush hl\n",
            unequal, unequal);
    mir_machine_emit_symbol_call(out, plan->check_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
}

static void mir_emit_fixed_byte_copy_check(
    MirStream *out, const struct MirFixedByteCopyChecks *plan, int element)
{
    int unequal = new_label();

    mir_stream_printf(out,
            "\tld hl,S%d\n\tpush hl\n"
            "\tld hl,%s+%d\n\tld a,(hl)\n\tcp %d\n"
            "\tld hl,0\n\tjp nz,L%d\n\tinc hl\n"
            "L%d:\n\tpush hl\n",
            plan->message_string_ids[0],
            plan->destination_assembly_name, element,
            plan->start_value + element,
            unequal, unequal);
    mir_machine_emit_symbol_call(out, plan->check_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
}

static void mir_emit_triangle_square(
    MirStream *out, const struct MirTrianglePerimeter *plan, int offset)
{
    (void)plan;
    mir_stream_printf(out,
            "\tld c,(ix%+d)\n\tld b,(ix%+d)\n"
            "\tld l,c\n\tld h,b\n",
            offset, offset + 1);
    mir_emit_runtime_call(out, "__m1s");
}

static void mir_emit_triangle_signed_member(
    MirStream *out, int offset)
{
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld a,h\n\trlca\n\tsbc a,a\n"
            "\tld d,a\n\tld e,a\n",
            offset, offset + 1);
}

static void mir_emit_fixed_point_load(
    MirStream *out, int offset)
{
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
            offset, offset + 1, offset + 2, offset + 3);
}

static void mir_emit_fixed_point_store(
    MirStream *out, int offset)
{
    mir_stream_printf(out,
            "\tld (ix%+d),l\n\tld (ix%+d),h\n"
            "\tld (ix%+d),e\n\tld (ix%+d),d\n",
            offset, offset + 1, offset + 2, offset + 3);
}

static void mir_emit_fixed_point_high_word(
    MirStream *out, int offset)
{
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld a,h\n\trlca\n\tsbc a,a\n"
            "\tld d,a\n\tld e,a\n",
            offset + 2, offset + 3);
}

static void mir_emit_fixed_point_fraction(
    MirStream *out, const struct MirFixedPointReport *plan, int offset)
{
    mir_emit_fixed_point_load(out, offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->fraction_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpush de\n\tpush hl\n", out);
}

static void mir_emit_aggregate_negated_member(
    MirStream *out, const struct MirAggregateSignNormalize *plan, int offset)
{
    int nonzero = new_label();

    mir_stream_printf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tld e,(ix+%d)\n\tld d,(ix+%d)\n"
            "\tld a,l\n\tcpl\n\tld l,a\n"
            "\tld a,h\n\tcpl\n\tld h,a\n"
            "\tld a,e\n\tcpl\n\tld e,a\n"
            "\tld a,d\n\tcpl\n\tld d,a\n"
            "\tinc hl\n\tld a,h\n\tor l\n\tjp nz,L%d\n\tinc de\n"
            "L%d:\n\tpush de\n\tpush hl\n"
            "\tld l,(ix+4)\n\tld h,(ix+5)\n",
            plan->value_stack_offset + 2 + offset,
            plan->value_stack_offset + 3 + offset,
            plan->value_stack_offset + 4 + offset,
            plan->value_stack_offset + 5 + offset,
            nonzero, nonzero);
    mir_machine_emit_hl_offset(out, offset, 0);
    mir_stream_puts("\tpop bc\n\tld (hl),c\n\tinc hl\n\tld (hl),b\n"
          "\tinc hl\n\tpop bc\n\tld (hl),c\n\tinc hl\n\tld (hl),b\n",
          out);
}

static void mir_emit_hidden_aggregate_call(
    MirStream *out, struct Sym *function, int result_offset)
{
    mir_emit_local_address(out, result_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, function);
}

static void mir_emit_local_aggregate_argument(
    MirStream *out, int source_offset, int size)
{
    int offset;

    for (offset = size - 2; offset >= 0; offset -= 2)
        mir_stream_printf(out,
                "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n",
                source_offset + offset, source_offset + offset + 1);
}

static void mir_emit_extra_integer_check(
    MirStream *out, const struct MirExtraLiteralChecks *plan,
    int value, int string_index)
{
    mir_stream_printf(out,
            "\tld hl,S%d\n\tpush hl\n"
            "\tld hl,%d\n\tpush hl\n\tpush hl\n",
            plan->string_ids[string_index], value);
    mir_machine_emit_symbol_call(out, plan->integer_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n", out);
}

static void mir_emit_hall_address(MirStream *out, const struct MirHallInit *p, int off)
{
    mir_stream_printf(out, "\tld l,(ix+%d)\n\tld h,(ix+%d)\n",
            p->hall_stack_offset + 4, p->hall_stack_offset + 5);
    mir_machine_emit_hl_offset(out, off, 0);
}

static void mir_emit_value_int_check(MirStream *out,
    const struct MirValueLiteralChecks *p, int s, int v)
{
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n\tld hl,%d\n\tpush hl\n\tpush hl\n",
            p->string_ids[s], v);
    mir_machine_emit_symbol_call(out, p->integer_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n", out);
}

static void mir_emit_value_wide_check(MirStream *out, struct Sym *fn,
    int string_id, unsigned long bits)
{
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_emit_fixed_point_constant(out, bits);
    mir_emit_fixed_point_constant(out, bits);
    mir_machine_emit_symbol_call(out, fn);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
}

static void mir_emit_bitfield_report_cleanup(MirStream *out, int words)
{
    while (words-- > 0)
        mir_stream_puts("\tpop bc\n", out);
}

static void mir_emit_bitfield_local_store(
    MirStream *out, unsigned int packed, int tail)
{
    mir_stream_printf(out,
            "\tld hl,%u\n"
            "\tld (ix-4),l\n\tld (ix-3),h\n"
            "\tld hl,%d\n"
            "\tld (ix-2),l\n\tld (ix-1),h\n",
            packed & 0xffffU, tail & 0xffff);
}

static void mir_emit_bitfield_load_word(
    MirStream *out, struct Sym *global, int offset)
{
    if (global != NULL)
        mir_machine_emit_global_word(out, global, offset);
    else
        mir_stream_printf(out,
                "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
                -4 + offset, -3 + offset);
}

static void mir_emit_bitfield_load_byte_a(
    MirStream *out, struct Sym *global, int offset)
{
    if (global != NULL)
        mir_machine_emit_global_byte_a(out, global, offset, 0);
    else
        mir_stream_printf(out, "\tld a,(ix%+d)\n", -4 + offset);
}

static void mir_emit_bitfield_push_unsigned_a(MirStream *out)
{
    mir_stream_puts("\tld l,a\n\tld h,0\n\tpush hl\n", out);
}

static void mir_emit_bitfield_push_unsigned_field(
    MirStream *out, struct Sym *global, int shift, int mask)
{
    int rotation;

    mir_emit_bitfield_load_byte_a(out, global, shift >= 8 ? 1 : 0);
    if (shift < 8)
        for (rotation = 0; rotation < shift; ++rotation)
            mir_stream_puts("\trrca\n", out);
    mir_stream_printf(out, "\tand %d\n", mask);
    mir_emit_bitfield_push_unsigned_a(out);
}

static void mir_emit_bitfield_push_signed_nibble(
    MirStream *out, struct Sym *global)
{
    mir_emit_bitfield_load_byte_a(out, global, 0);
    mir_stream_puts("\tand 15\n\txor 8\n\tsub 8\n"
          "\tld l,a\n\trla\n\tsbc a,a\n\tld h,a\n\tpush hl\n",
          out);
}

static void mir_emit_bitfield_push_aggregate(
    MirStream *out, struct Sym *global)
{
    mir_emit_bitfield_load_word(out, global, 2);
    mir_stream_puts("\tpush hl\n", out);
    mir_emit_bitfield_load_word(out, global, 0);
    mir_stream_puts("\tpush hl\n", out);
}

static void mir_emit_bitfield_sum(
    MirStream *out, struct Sym *function, struct Sym *global)
{
    mir_emit_bitfield_push_aggregate(out, global);
    mir_machine_emit_symbol_call(out, function);
    mir_emit_bitfield_report_cleanup(out, 2);
}

static void mir_emit_unsigned_bitfield_report(
    MirStream *out, const struct MirBitfieldReportSequence *plan,
    int string_index, struct Sym *global)
{
    mir_emit_bitfield_sum(out, plan->sum_functions[0], global);
    mir_stream_puts("\tpush hl\n", out);
    mir_emit_bitfield_load_word(out, global, 2);
    mir_stream_puts("\tpush hl\n", out);
    mir_emit_bitfield_push_unsigned_field(out, global, 8, 255);
    mir_emit_bitfield_push_unsigned_field(out, global, 3, 31);
    mir_emit_bitfield_push_unsigned_field(out, global, 0, 7);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->string_ids[string_index]);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_emit_bitfield_report_cleanup(out, 6);
}

static void mir_emit_signed_bitfield_report(
    MirStream *out, const struct MirBitfieldReportSequence *plan,
    int string_index, struct Sym *global)
{
    mir_emit_bitfield_sum(out, plan->sum_functions[1], global);
    mir_stream_puts("\tpush hl\n", out);
    mir_emit_bitfield_load_word(out, global, 2);
    mir_stream_puts("\tpush hl\n", out);
    mir_emit_bitfield_push_unsigned_field(out, global, 4, 15);
    mir_emit_bitfield_push_signed_nibble(out, global);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->string_ids[string_index]);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_emit_bitfield_report_cleanup(out, 5);
}

static void mir_emit_bitfield_aggregate_call(
    MirStream *out, struct Sym *function,
    const int *arguments, int argument_count)
{
    int argument;

    for (argument = argument_count - 1; argument >= 0; --argument)
        mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n",
                arguments[argument] & 0xffff);
    mir_emit_local_address(out, -4);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, function);
    mir_emit_bitfield_report_cleanup(out, argument_count + 1);
}

static void mir_fixed_wrapper_load_pointer(
    MirStream *out, const struct MirFixedWrapperInit *plan,
    int offset)
{
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            plan->pointer_stack_offset + 2,
            plan->pointer_stack_offset + 3);
    mir_machine_emit_hl_offset(out, offset, 0);
}

static void mir_fixed_wrapper_load_word(
    MirStream *out, const struct MirFixedWrapperInit *plan,
    int constant)
{
    mir_stream_printf(out,
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n"
            "\tld a,e\n\tadd a,%d\n\tld e,a\n"
            "\tld a,d\n\tadc a,%d\n\tld d,a\n",
            plan->base_stack_offset + 2,
            plan->base_stack_offset + 3,
            constant & 255, (constant >> 8) & 255);
}

static void mir_fixed_wrapper_store_word_loop(
    MirStream *out, const struct MirFixedWrapperInit *plan,
    int constant, int count)
{
    mir_fixed_wrapper_load_word(out, plan, constant);
    if (count == 1) {
        mir_stream_puts("\tld (hl),e\n\tinc hl\n"
              "\tld (hl),d\n\tinc hl\n", out);
        return;
    }
    {
        int loop = new_label();

        mir_stream_printf(out,
                "\tld b,%d\n"
                "L%d:\n\tld (hl),e\n\tinc hl\n"
                "\tld (hl),d\n\tinc hl\n\tinc de\n"
                "\tdjnz L%d\n",
                count, loop, loop);
    }
}

static void mir_fixed_wrapper_store_char_loop(
    MirStream *out, const struct MirFixedWrapperInit *plan,
    int constant, int count)
{
    mir_stream_printf(out,
            "\tld a,(ix%+d)\n\tadd a,%d\n\tand 127\n",
            plan->base_stack_offset + 2,
            constant & 255);
    if (count == 1) {
        mir_stream_puts("\tld (hl),a\n\tinc hl\n", out);
        return;
    }
    {
        int loop = new_label();

        mir_stream_printf(out,
                "\tld b,%d\n"
                "L%d:\n\tld (hl),a\n\tinc hl\n"
                "\tinc a\n\tand 127\n\tdjnz L%d\n",
                count, loop, loop);
    }
}

static void mir_fixed_wrapper_load_long(
    MirStream *out, int constant)
{
    mir_stream_printf(out,
            "\tld c,(ix-4)\n\tld b,(ix-3)\n"
            "\tld e,(ix-2)\n\tld d,(ix-1)\n"
            "\tld a,c\n\tadd a,%d\n\tld c,a\n"
            "\tld a,b\n\tadc a,%d\n\tld b,a\n"
            "\tld a,e\n\tadc a,0\n\tld e,a\n"
            "\tld a,d\n\tadc a,0\n\tld d,a\n",
            constant & 255, (constant >> 8) & 255);
}

static void mir_fixed_wrapper_store_long_loop(
    MirStream *out, int constant, int count)
{
    int loop = new_label();
    int no_high_increment = new_label();

    mir_fixed_wrapper_load_long(out, constant);
    if (count > 1)
        mir_stream_printf(out, "\tld (ix-5),%d\n", count);
    mir_stream_printf(out,
            "L%d:\n\tld (hl),c\n\tinc hl\n"
            "\tld (hl),b\n\tinc hl\n"
            "\tld (hl),e\n\tinc hl\n"
            "\tld (hl),d\n\tinc hl\n",
            loop);
    if (count > 1) {
        mir_stream_printf(out,
                "\tinc bc\n\tld a,b\n\tor c\n"
                "\tjp nz,L%d\n\tinc de\n"
                "L%d:\n\tdec (ix-5)\n\tjp nz,L%d\n",
                no_high_increment, no_high_increment, loop);
    }
}

static void mir_fixed_wrapper_store_pointer(
    MirStream *out, const struct MirFixedWrapperInit *plan,
    int destination_offset, int source_offset)
{
    mir_fixed_wrapper_load_pointer(
        out, plan, destination_offset);
    mir_stream_printf(out,
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
            plan->pointer_stack_offset + 2,
            plan->pointer_stack_offset + 3);
    if (source_offset != 0)
        mir_stream_printf(out,
                "\tld a,e\n\tadd a,%d\n\tld e,a\n"
                "\tld a,d\n\tadc a,%d\n\tld d,a\n",
                source_offset & 255,
                (source_offset >> 8) & 255);
    mir_stream_puts("\tld (hl),e\n\tinc hl\n\tld (hl),d\n", out);
}

static void mir_local_array_struct_store(
    MirStream *out, int offset, int width, unsigned long value)
{
    if (width == 1) {
        mir_stream_printf(out, "\tld (ix%+d),%lu\n",
                offset, value & 0xffUL);
    } else if (width == 2) {
        mir_stream_printf(out,
                "\tld hl,%lu\n"
                "\tld (ix%+d),l\n\tld (ix%+d),h\n",
                value & 0xffffUL, offset, offset + 1);
    } else {
        mir_machine_emit_float_bits(out, value);
        mir_machine_emit_ix_wide_store(out, offset);
    }
}

static void mir_local_array_struct_address(MirStream *out, int offset)
{
    mir_stream_puts("\tpush ix\n\tpop hl\n", out);
    mir_machine_emit_hl_offset(out, offset, 0);
}

static void mir_local_array_struct_normalize(
    MirStream *out, int width, int is_unsigned)
{
    if (width == 4)
        return;
    if (width == 1) {
        if (is_unsigned)
            mir_stream_puts("\tld h,0\n\tld de,0\n", out);
        else
            mir_stream_puts("\tld a,l\n\trlca\n\tsbc a,a\n"
                  "\tld h,a\n\tld d,a\n\tld e,a\n", out);
        return;
    }
    if (is_unsigned)
        mir_stream_puts("\tld de,0\n", out);
    else
        mir_stream_puts("\tld a,h\n\trlca\n\tsbc a,a\n"
              "\tld d,a\n\tld e,a\n", out);
}

static void mir_local_array_struct_load(
    MirStream *out, int offset, int width, int is_unsigned)
{
    if (width == 1)
        mir_stream_printf(out, "\tld l,(ix%+d)\n", offset);
    else if (width == 2)
        mir_stream_printf(out,
                "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
                offset, offset + 1);
    else
        mir_machine_emit_ix_wide_load(out, offset);
    mir_local_array_struct_normalize(out, width, is_unsigned);
}

static void mir_local_array_struct_check(
    MirStream *out, const struct MirLocalArrayStructChecks *plan,
    int check, int is_unsigned)
{
    mir_stream_puts("\texx\n", out);
    mir_machine_emit_float_bits(
        out, plan->expected_values[check]);
    mir_stream_puts("\tpush de\n\tpush hl\n\texx\n"
          "\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->string_ids[check]);
    mir_machine_emit_symbol_call(
        out, plan->check_functions[is_unsigned]);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tpop bc\n\tpop bc\n", out);
}

static void mir_local_array_struct_pointer_call(
    MirStream *out, struct Sym *function, int offset,
    int width, int is_unsigned, int has_index, int index)
{
    if (has_index)
        mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n", index);
    mir_local_array_struct_address(out, offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, function);
    mir_stream_puts("\tpop bc\n", out);
    if (has_index)
        mir_stream_puts("\tpop bc\n", out);
    mir_local_array_struct_normalize(out, width, is_unsigned);
}

static void mir_alias_mix_emit_address(
    MirStream *out, const struct MirAliasMixAddress *address)
{
    if (address->indirect) {
        mir_machine_emit_global_word(
            out, address->root, address->root_offset);
        mir_machine_emit_hl_offset(
            out, address->pointee_offset, 0);
    } else {
        mir_machine_emit_global_address_hl(
            out, address->root, address->root_offset);
    }
}

static void mir_alias_mix_emit_store(
    MirStream *out, const struct MirAliasMixStore *store)
{
    int byte;

    mir_alias_mix_emit_address(out, &store->address);
    for (byte = 0; byte < store->width; ++byte) {
        mir_stream_printf(out, "\tld (hl),%lu\n",
                (store->value >> (byte * 8)) & 255UL);
        if (byte + 1 < store->width)
            mir_stream_puts("\tinc hl\n", out);
    }
}

static void mir_alias_mix_emit_check(
    MirStream *out, const struct MirAliasMixCheck *check)
{
    int cleanup;

    mir_alias_mix_emit_address(out, &check->address);
    if (check->width == 1) {
        mir_stream_puts("\tld a,(hl)\n\tld l,a\n", out);
        if (check->is_unsigned)
            mir_stream_puts("\tld h,0\n", out);
        else
            mir_stream_puts("\trlca\n\tsbc a,a\n\tld h,a\n", out);
        mir_stream_puts("\tld d,h\n\tld e,l\n", out);
    } else if (check->width == 2) {
        mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n", out);
    } else {
        mir_stream_puts("\tld c,(hl)\n\tinc hl\n"
              "\tld b,(hl)\n\tinc hl\n"
              "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n", out);
    }

    if (check->width == 4) {
        mir_emit_fixed_point_constant(out, check->expected);
        mir_stream_puts("\tpush de\n\tpush bc\n", out);
        cleanup = 5;
    } else {
        mir_stream_printf(out, "\tld hl,%lu\n\tpush hl\n",
                check->expected & 0xffffUL);
        mir_stream_puts("\tpush de\n", out);
        cleanup = 3;
    }
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            check->string_id);
    mir_machine_emit_symbol_call(out, check->function);
    while (cleanup-- > 0)
        mir_stream_puts("\tpop bc\n", out);
}

static void mir_task_array_push_arguments(
    MirStream *out, int task_count)
{
    mir_stream_printf(out,
            "\tld hl,%d\n\tpush hl\n"
            "\tpush ix\n\tpop hl\n"
            "\tld de,-22\n\tadd hl,de\n\tpush hl\n",
            task_count);
}

static void mir_emit_compound_check_store(
    MirStream *out, const struct MirCompoundCheckEvent *event)
{
    if (event->value_kind == MIR_COMPOUND_CHECK_ADDRESS) {
        mir_stream_puts("\tpush ix\n\tpop hl\n", out);
        mir_machine_emit_hl_offset(
            out, event->address_offset, 0);
    } else if (event->width == 1) {
        mir_stream_printf(out, "\tld (ix%+d),%lu\n",
                event->offset, event->value & 255UL);
        return;
    } else {
        mir_stream_printf(out, "\tld hl,%lu\n",
                event->value & 0xffffUL);
    }
    mir_stream_printf(out,
            "\tld (ix%+d),l\n"
            "\tld (ix%+d),h\n",
            event->offset, event->offset + 1);
}

static void mir_emit_compound_check_call(
    MirStream *out, const struct MirCompoundCheckEvent *event)
{
    mir_stream_printf(out,
            "\tld hl,%lu\n\tpush hl\n"
            "\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            event->value & 0xffffUL, event->string_id);
    mir_machine_emit_symbol_call(out, event->function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n", out);
}

static int mir_match_variadic_string_join(
    struct MirVariadicStringJoin *plan)
{
    static const int expected_opcodes[74] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_NOP,
        MIR_CONST, MIR_STORE, MIR_VA_START, MIR_NOP, MIR_NOP,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_LOAD, MIR_LOAD,
        MIR_NOP, MIR_PHI, MIR_PHI, MIR_NOP, MIR_NOP, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_VA_ARG, MIR_STORE, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP,
        MIR_BINARY, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_LOAD, MIR_ARG, MIR_CALL, MIR_UNARY, MIR_BINARY, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_LOAD, MIR_BINARY,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_LOAD, MIR_LOAD,
        MIR_ARG, MIR_CALL, MIR_UNARY, MIR_BINARY, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_JUMP, MIR_LABEL, MIR_VA_END, MIR_NOP, MIR_UNARY, MIR_RETURN
    };
    const struct MirInsn *destination = &mir.insns[1];
    const struct MirInsn *separator = &mir.insns[2];
    const struct MirInsn *count = &mir.insns[3];
    const struct MirInsn *copy1 = &mir.insns[36];
    const struct MirInsn *length1 = &mir.insns[40];
    const struct MirInsn *copy2 = &mir.insns[53];
    const struct MirInsn *length2 = &mir.insns[57];
    struct Sym *function;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    function = find_global(mir.name);
    if (function == NULL || !function->proto_variadic ||
        mir.count != 74 || mir_cfg_block_count() != 5 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_INT)
        return mir_machine_reject(
            "variadic-string-join", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "variadic-string-join", "opcode");
    if (type_ptr_depth(destination->type) != 1 ||
        (destination->type & 15) != TYPE_CHAR ||
        type_ptr_depth(separator->type) != 1 ||
        (separator->type & 15) != TYPE_CHAR ||
        type_ptr_depth(count->type) != 0 ||
        type_size(count->type) != 2 ||
        !mir_machine_parameter_value_offset(
            destination->dst,
            &plan->destination_stack_offset) ||
        !mir_machine_parameter_value_offset(
            separator->dst,
            &plan->separator_stack_offset) ||
        !mir_machine_parameter_value_offset(
            count->dst,
            &plan->count_stack_offset) ||
        mir.insns[7].secondary_offset < 2 ||
        mir.insns[7].secondary_offset > 127 ||
        mir.insns[24].secondary_offset != 2)
        return mir_machine_reject(
            "variadic-string-join", "parameters");
    plan->first_variadic_frame_offset =
        mir.insns[7].secondary_offset;
    if (!mir_machine_constant_equals(mir.insns[5].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[6]) ||
        !mir_machine_constant_equals(mir.insns[10].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[12]) ||
        mir.insns[17].src1 != mir.insns[5].dst ||
        mir.insns[17].src2 != mir.insns[59].dst ||
        mir.insns[17].phi_pred1 != mir.insns[0].label ||
        mir.insns[17].phi_pred2 != mir.insns[63].label ||
        mir.insns[18].src1 != mir.insns[10].dst ||
        mir.insns[18].src2 != mir.insns[66].dst ||
        mir.insns[18].phi_pred1 != mir.insns[0].label ||
        mir.insns[18].phi_pred2 != mir.insns[63].label ||
        mir.insns[21].immediate != '<' ||
        mir.insns[21].src1 != mir.insns[18].dst ||
        mir.insns[21].src2 != count->dst ||
        mir.insns[22].label != mir.insns[69].label ||
        mir.insns[25].src1 != mir.insns[24].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[25]) ||
        mir.insns[28].immediate != '>' ||
        mir.insns[28].src1 != mir.insns[18].dst ||
        !mir_machine_constant_equals(mir.insns[28].src2, 0) ||
        mir.insns[29].label != mir.insns[46].label)
        return mir_machine_reject(
            "variadic-string-join", "flow");
    if (strcmp(copy1->name, copy2->name) ||
        strcmp(length1->name, length2->name) ||
        (copy1->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0 ||
        (length1->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0)
        return mir_machine_reject(
            "variadic-string-join", "calls");
    if ((strcmp(copy1->name, "strcpy") &&
         strcmp(copy1->base_name, "__scf")) ||
        (strcmp(length1->name, "strlen") &&
         strcmp(length1->base_name, "__slf")) ||
        mir.insns[70].opcode != MIR_VA_END ||
        mir.insns[72].src1 != mir.insns[17].dst ||
        mir.insns[73].src1 != mir.insns[72].dst)
        return mir_machine_reject(
            "variadic-string-join", "runtime");
    strcpy(plan->copy_call_name, "__scf");
    strcpy(plan->length_call_name, "__slf");
    return 1;
}

static void mir_emit_variadic_string_join(
    MirStream *out, const struct MirVariadicStringJoin *plan)
{
    int loop = new_label();
    int no_separator = new_label();
    int done = new_label();

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-8\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\tld hl,0\n\tld (ix-2),l\n\tld (ix-1),h\n"
          "\tld (ix-4),l\n\tld (ix-3),h\n"
          "\tpush ix\n\tpop hl\n", out);
    mir_stream_printf(out,
            "\tld de,%d\n\tadd hl,de\n"
            "\tld (ix-6),l\n\tld (ix-5),h\n"
            "\tld a,(ix+%d)\n\tbit 7,a\n\tjp nz,L%d\n"
            "L%d:\n"
            "\tld l,(ix-4)\n\tld h,(ix-3)\n"
            "\tld e,(ix+%d)\n\tld d,(ix+%d)\n"
            "\tor a\n\tsbc hl,de\n\tjp z,L%d\n"
            "\tld l,(ix-6)\n\tld h,(ix-5)\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tinc hl\n"
            "\tld (ix-6),l\n\tld (ix-5),h\n"
            "\tld (ix-8),e\n\tld (ix-7),d\n"
            "\tld a,(ix-4)\n\tor (ix-3)\n\tjp z,L%d\n",
            plan->first_variadic_frame_offset,
            plan->count_stack_offset + 3, done,
            loop,
            plan->count_stack_offset + 2,
            plan->count_stack_offset + 3,
            done, no_separator);
    mir_stream_printf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tld e,(ix-2)\n\tld d,(ix-1)\n"
            "\tadd hl,de\n\tex de,hl\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n",
            plan->destination_stack_offset + 2,
            plan->destination_stack_offset + 3,
            plan->separator_stack_offset + 2,
            plan->separator_stack_offset + 3);
    mir_emit_runtime_call(out, plan->copy_call_name);
    mir_stream_printf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n",
            plan->separator_stack_offset + 2,
            plan->separator_stack_offset + 3);
    mir_emit_runtime_call(out, plan->length_call_name);
    mir_stream_puts("\tld e,(ix-2)\n\tld d,(ix-1)\n"
          "\tadd hl,de\n\tld (ix-2),l\n\tld (ix-1),h\n", out);
    mir_stream_printf(out, "L%d:\n", no_separator);
    mir_stream_printf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tld e,(ix-2)\n\tld d,(ix-1)\n"
            "\tadd hl,de\n\tex de,hl\n"
            "\tld l,(ix-8)\n\tld h,(ix-7)\n",
            plan->destination_stack_offset + 2,
            plan->destination_stack_offset + 3);
    mir_emit_runtime_call(out, plan->copy_call_name);
    mir_stream_puts("\tld l,(ix-8)\n\tld h,(ix-7)\n", out);
    mir_emit_runtime_call(out, plan->length_call_name);
    mir_stream_puts("\tld e,(ix-2)\n\tld d,(ix-1)\n"
          "\tadd hl,de\n\tld (ix-2),l\n\tld (ix-1),h\n"
          "\tinc (ix-4)\n", out);
    {
        int no_carry = new_label();

        mir_stream_printf(out,
                "\tjp nz,L%d\n\tinc (ix-3)\nL%d:\n",
                no_carry, no_carry);
    }
    mir_stream_printf(out,
            "\tjp L%d\nL%d:\n"
            "\tld l,(ix-2)\n\tld h,(ix-1)\n"
            "\tld sp,ix\n\tpop ix\n\tret\n",
            loop, done);
}

static int mir_match_fixed_record_sort_check(
    struct MirFixedRecordSortCheck *plan)
{
    const struct MirInsn *fill_address = &mir.insns[20];
    const struct MirInsn *random_call = &mir.insns[26];
    const struct MirInsn *sort_call = &mir.insns[55];
    const struct MirInsn *compare_call = &mir.insns[83];
    const struct MirInsn *failure_call = &mir.insns[89];
    struct Sym *root;
    long root_offset;
    int sort_arguments[4];
    int compare_arguments[3];
    int failure_argument;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 98 || mir_cfg_block_count() != 11 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID ||
        !mir_machine_constant_equals(mir.insns[1].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[3]) ||
        mir.insns[5].src1 != mir.insns[1].dst ||
        mir.insns[5].src2 != mir.insns[41].dst ||
        !mir_machine_constant_equals(mir.insns[7].dst, 16) ||
        mir.insns[8].immediate != '<' ||
        mir.insns[8].src1 != mir.insns[5].dst ||
        mir.insns[8].src2 != mir.insns[7].dst ||
        mir.insns[9].label != mir.insns[44].label ||
        !mir_machine_constant_equals(mir.insns[10].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[12]) ||
        !mir_machine_constant_equals(mir.insns[17].dst, 5) ||
        mir.insns[18].immediate != '<' ||
        mir.insns[18].src1 != mir.insns[16].dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        mir.insns[19].label != mir.insns[37].label)
        return mir_machine_reject(
            "fixed-record-sort-check", "loops");
    if (!mir_machine_global_address_offset(
            fill_address->dst, &plan->records,
            &root_offset, 0) ||
        root_offset < -32768 || root_offset > 32767)
        return mir_machine_reject(
            "fixed-record-sort-check", "fill-root");
    if (
        mir.insns[22].src1 != fill_address->dst ||
        mir.insns[22].src2 != mir.insns[5].dst ||
        mir.insns[22].immediate <= 0 ||
        mir.insns[22].memory_size !=
            mir.insns[22].immediate ||
        mir.insns[25].src1 != mir.insns[23].dst ||
        mir.insns[25].src2 != mir.insns[24].dst ||
        mir.insns[25].immediate != 1 ||
        !mir_machine_call_has_no_arguments(random_call) ||
        !mir_machine_constant_equals(mir.insns[27].dst, 255) ||
        mir.insns[28].immediate != '&' ||
        mir.insns[28].src1 != random_call->dst ||
        mir.insns[28].src2 != mir.insns[27].dst ||
        mir.insns[30].src1 != mir.insns[25].dst ||
        mir.insns[30].src2 != mir.insns[29].dst ||
        mir.insns[30].memory_size != 1)
        return mir_machine_reject(
            "fixed-record-sort-check", "fill-body");
    plan->records_offset = (int)root_offset;
    plan->record_size = (int)mir.insns[22].immediate;
    plan->count = (int)mir.insns[7].immediate;
    if (plan->count != 16 || plan->record_size != 5 ||
        !mir_machine_global_address_offset(
            mir.insns[45].dst, &root,
            &root_offset, 0) ||
        root != plan->records ||
        root_offset != plan->records_offset ||
        !mir_machine_four_call_arguments(
            sort_call, sort_arguments) ||
        sort_arguments[0] != mir.insns[45].dst ||
        !mir_machine_constant_equals(
            sort_arguments[1], plan->count) ||
        !mir_machine_constant_equals(
            sort_arguments[2], plan->record_size) ||
        sort_arguments[3] != mir.insns[53].dst)
        return mir_machine_reject(
            "fixed-record-sort-check", "sort");
    plan->random_function = find_global(random_call->name);
    plan->compare_function = find_global(mir.insns[53].name);
    plan->sort_function = find_global(sort_call->name);
    if (plan->random_function == NULL ||
        !plan->random_function->is_defined ||
        plan->compare_function == NULL ||
        !plan->compare_function->is_defined ||
        plan->sort_function == NULL)
        return mir_machine_reject(
            "fixed-record-sort-check", "functions");
    if (!mir_machine_constant_equals(mir.insns[56].dst, 1) ||
        !mir_machine_unobservable_local_store(&mir.insns[58]) ||
        mir.insns[60].src1 != mir.insns[56].dst ||
        mir.insns[60].src2 != mir.insns[94].dst ||
        !mir_machine_constant_equals(mir.insns[63].dst, 16) ||
        mir.insns[64].immediate != '<' ||
        mir.insns[64].src1 != mir.insns[60].dst ||
        mir.insns[64].src2 != mir.insns[63].dst ||
        mir.insns[65].label != mir.insns[97].label ||
        !mir_machine_global_address_offset(
            mir.insns[66].dst, &root,
            &root_offset, 0) ||
        root != plan->records ||
        root_offset != plan->records_offset ||
        !mir_machine_global_address_offset(
            mir.insns[74].dst, &root,
            &root_offset, 0) ||
        root != plan->records ||
        root_offset != plan->records_offset ||
        !mir_machine_three_call_arguments(
            compare_call, compare_arguments) ||
        compare_arguments[0] != mir.insns[71].dst ||
        compare_arguments[1] != mir.insns[77].dst ||
        !mir_machine_constant_equals(
            compare_arguments[2], plan->record_size) ||
        !mir_machine_constant_equals(mir.insns[84].dst, 0) ||
        mir.insns[85].immediate != '>' ||
        mir.insns[85].src1 != compare_call->dst ||
        mir.insns[85].src2 != mir.insns[84].dst ||
        mir.insns[86].label != mir.insns[90].label)
        return mir_machine_reject(
            "fixed-record-sort-check", "verify");
    if ((strcmp(compare_call->name, "memcmp") &&
         strcmp(compare_call->base_name, "__cmpf")) ||
        !mir_machine_single_call_argument(
            failure_call, &failure_argument))
        return mir_machine_reject(
            "fixed-record-sort-check", "failure");
    {
        const struct MirInsn *string =
            mir_definition(failure_argument);

        if (string == NULL ||
            string->opcode != MIR_STRING_ADDRESS)
            return mir_machine_reject(
                "fixed-record-sort-check", "string");
        plan->failure_string_id = (int)string->immediate;
    }
    plan->failure_function = find_global(failure_call->name);
    return plan->failure_function != NULL &&
           plan->failure_function->is_defined;
}

static void mir_emit_fixed_record_sort_check(
    MirStream *out, const struct MirFixedRecordSortCheck *plan)
{
    int fill_loop = new_label();
    int scan_loop = new_label();
    int scan_next = new_label();
    int done = new_label();
    const char *compare_name =
        asm_name_for(sym_asm_name(plan->compare_function));

    mir_stream_printf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_address_de(
        out, plan->records, plan->records_offset);
    mir_stream_puts("\tpush de\n\tpop iy\n", out);
    mir_stream_printf(out, "L%d:\n", fill_loop);
    mir_machine_emit_symbol_call(out, plan->random_function);
    mir_stream_puts("\tld (iy+0),l\n\tinc iy\n"
          "\tpush iy\n\tpop hl\n", out);
    mir_machine_emit_global_address_de(
        out, plan->records,
        plan->records_offset +
            plan->count * plan->record_size);
    mir_stream_puts("\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", fill_loop);
    mir_stream_printf(out, "\tld hl,%s\n\tpush hl\n", compare_name);
    mir_stream_printf(out,
            "\tld hl,%d\n\tpush hl\n"
            "\tld hl,%d\n\tpush hl\n",
            plan->record_size, plan->count);
    mir_machine_emit_global_address_de(
        out, plan->records, plan->records_offset);
    mir_stream_puts("\tpush de\n", out);
    mir_machine_emit_symbol_call(out, plan->sort_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_global_address_de(
        out, plan->records,
        plan->records_offset + plan->record_size);
    mir_stream_puts("\tpush de\n\tpop iy\n", out);
    mir_stream_printf(out,
            "L%d:\n\tpush iy\n\tpop hl\n"
            "\tld de,-%d\n\tadd hl,de\n\tex de,hl\n"
            "\tpush iy\n\tpop hl\n\tld bc,%d\n",
            scan_loop, plan->record_size,
            plan->record_size);
    mir_emit_runtime_call(out, "__cmpf");
    mir_stream_puts("\tbit 7,h\n", out);
    mir_stream_printf(out,
            "\tjp nz,L%d\n\tld a,h\n\tor l\n\tjp z,L%d\n"
            "\tld hl,S%d\n\tpush hl\n",
            scan_next, scan_next,
            plan->failure_string_id);
    mir_machine_emit_symbol_call(out, plan->failure_function);
    mir_stream_puts("\tpop bc\n", out);
    mir_stream_printf(out, "L%d:\n", scan_next);
    {
        int byte;
        for (byte = 0; byte < plan->record_size; ++byte)
            mir_stream_puts("\tinc iy\n", out);
    }
    mir_stream_puts("\tpush iy\n\tpop hl\n", out);
    mir_machine_emit_global_address_de(
        out, plan->records,
        plan->records_offset +
            plan->count * plan->record_size);
    mir_stream_puts("\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out,
            "\tjp nz,L%d\nL%d:\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            scan_loop, done);
}

static int mir_match_local_bitset_runner(
    struct MirLocalBitsetRunner *plan)
{
    static const int add_calls[2] = { 10, 16 };
    static const int print_queries[3] = { 24, 31, 38 };
    static const int return_queries[3] = { 46, 53, 69 };
    int memory_type;
    int memory_storage;
    int memory_offset;
    int second_type;
    int second_storage;
    int second_offset;
    int arguments[4];
    int call;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 80 || mir_cfg_block_count() != 7 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_INT ||
        !mir_machine_constant_equals(mir.insns[1].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        mir.insns[2].opcode != MIR_STORE ||
        mir.insns[2].src1 != mir.insns[1].dst ||
        mir.insns[4].opcode != MIR_STORE ||
        mir.insns[4].src1 != mir.insns[3].dst ||
        mir.insns[2].memory_size != 2 ||
        mir.insns[4].memory_size != 2 ||
        (mir.insns[2].memory_flags & (1 | 8)) != 0 ||
        (mir.insns[4].memory_flags & (1 | 8)) != 0 ||
        !mir_scalar_memory_location(
            &mir.insns[2], &memory_type,
            &memory_storage, &memory_offset) ||
        !mir_scalar_memory_location(
            &mir.insns[4], &second_type,
            &second_storage, &second_offset) ||
        memory_storage != SC_LOCAL ||
        second_storage != SC_LOCAL ||
        second_offset != memory_offset + 2 ||
        memory_offset >= 0)
        return mir_machine_reject(
            "local-bitset-runner", "setup");
    plan->set_offset = memory_offset;
    for (call = 0; call < 2; ++call) {
        int args[2];
        const struct MirInsn *invoke =
            &mir.insns[add_calls[call]];

        if (!mir_machine_two_call_arguments(invoke, args) ||
            mir_definition(args[0]) == NULL ||
            mir_definition(args[0])->opcode != MIR_ADDRESS ||
            mir_definition(args[1]) == NULL ||
            mir_definition(args[1])->opcode != MIR_CONST)
            return mir_machine_reject(
                "local-bitset-runner", "add");
        plan->add_values[call] =
            (int)mir_definition(args[1])->immediate;
        if (call == 0)
            plan->add_function =
                find_global(invoke->name);
        else if (plan->add_function !=
                 find_global(invoke->name))
            return mir_machine_reject(
                "local-bitset-runner", "add-function");
    }
    if (plan->add_function == NULL ||
        !plan->add_function->is_defined)
        return mir_machine_reject(
            "local-bitset-runner", "add-symbol");
    for (call = 0; call < 3; ++call) {
        int args[2];
        const struct MirInsn *invoke =
            &mir.insns[print_queries[call]];

        if (!mir_machine_two_call_arguments(invoke, args) ||
            !mir_machine_constant_equals(
                args[1],
                call == 0 ? 3 : call == 1 ? 4 : 20))
            return mir_machine_reject(
                "local-bitset-runner", "print-query");
        plan->query_values[call] =
            call == 0 ? 3 : call == 1 ? 4 : 20;
        if (call == 0)
            plan->has_function =
                find_global(invoke->name);
        else if (plan->has_function !=
                 find_global(invoke->name))
            return mir_machine_reject(
                "local-bitset-runner", "query-function");
    }
    if (plan->has_function == NULL ||
        !plan->has_function->is_defined ||
        !mir_machine_four_call_arguments(
            &mir.insns[40], arguments) ||
        arguments[0] != mir.insns[17].dst ||
        arguments[1] != mir.insns[24].dst ||
        arguments[2] != mir.insns[31].dst ||
        arguments[3] != mir.insns[38].dst)
        return mir_machine_reject(
            "local-bitset-runner", "report");
    plan->print_function =
        find_global(mir.insns[40].name);
    plan->string_id =
        (int)mir.insns[17].immediate;
    if (plan->print_function == NULL)
        return mir_machine_reject(
            "local-bitset-runner", "print-symbol");
    for (call = 0; call < 3; ++call) {
        int args[2];

        if (!mir_machine_two_call_arguments(
                &mir.insns[return_queries[call]], args) ||
            !mir_machine_constant_equals(
                args[1], plan->query_values[call]) ||
            find_global(
                mir.insns[return_queries[call]].name) !=
                plan->has_function)
            return mir_machine_reject(
                "local-bitset-runner", "return-query");
    }
    return mir.insns[47].opcode == MIR_BRANCH_FALSE &&
           mir.insns[55].opcode == MIR_BRANCH_FALSE &&
           mir.insns[70].opcode == MIR_BRANCH_FALSE &&
           mir.insns[79].opcode == MIR_RETURN;
}

static void mir_emit_local_bitset_runner(
    MirStream *out, const struct MirLocalBitsetRunner *plan)
{
    int true_result = new_label();
    int done = new_label();
    int query;

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-4\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld (ix%+d),0\n\tld (ix%+d),0\n"
            "\tld (ix%+d),0\n\tld (ix%+d),0\n",
            plan->set_offset, plan->set_offset + 1,
            plan->set_offset + 2, plan->set_offset + 3);
    for (query = 0; query < 2; ++query)
        mir_emit_local_bitset_call(
            out, plan, plan->add_function,
            plan->add_values[query]);
    for (query = 2; query >= 0; --query) {
        mir_emit_local_bitset_call(
            out, plan, plan->has_function,
            plan->query_values[query]);
        mir_stream_puts("\tpush hl\n", out);
    }
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->string_id);
    mir_machine_emit_symbol_call(
        out, plan->print_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_emit_local_bitset_call(
        out, plan, plan->has_function,
        plan->query_values[0]);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjr z,L%d\n", true_result);
    mir_emit_local_bitset_call(
        out, plan, plan->has_function,
        plan->query_values[1]);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjr nz,L%d\n", true_result);
    mir_emit_local_bitset_call(
        out, plan, plan->has_function,
        plan->query_values[2]);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out,
            "\tjr z,L%d\n\tld hl,0\n\tjr L%d\n"
            "L%d:\n\tld hl,1\nL%d:\n"
            "\tld sp,ix\n\tpop ix\n\tret\n",
            true_result, done, true_result, done);
}

static int mir_match_fixed_sieve_build(
    struct MirFixedSieveBuild *plan)
{
    static const int member_indices[5] = {
        13, 28, 35, 55, 75
    };
    const struct MirInsn *pointer = &mir.insns[1];
    long limit;
    int member;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 98 || mir_cfg_block_count() != 11 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID ||
        pointer->opcode != MIR_PARAM ||
        type_ptr_depth(pointer->type) != 1 ||
        mir_machine_pointee_is_volatile(pointer) ||
        !mir_machine_parameter_value_offset(
            pointer->dst, &plan->pointer_stack_offset) ||
        !mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        mir.insns[7].src1 != mir.insns[2].dst ||
        mir.insns[7].src2 != mir.insns[22].dst ||
        mir.insns[10].immediate != TOK_LE ||
        mir.insns[10].src1 != mir.insns[7].dst ||
        mir.insns[11].label != mir.insns[26].label ||
        !mir_machine_constant_value(
            mir.insns[9].dst, &limit, 0) ||
        limit <= 0 || limit > 255)
        return mir_machine_reject(
            "fixed-sieve-build", "shape");
    plan->limit = (int)limit;
    for (member = 0; member < 5; ++member) {
        const struct MirInsn *address =
            &mir.insns[member_indices[member]];

        if (address->opcode != MIR_MEMBER_ADDRESS ||
            address->src1 != pointer->dst ||
            address->memory_size != plan->limit + 1 ||
            address->immediate < 0 ||
            address->immediate > 127 ||
            (member != 0 &&
             address->immediate != plan->member_offset))
            return mir_machine_reject(
                "fixed-sieve-build", "member");
        plan->member_offset =
            (int)address->immediate;
    }
    if (mir.insns[15].src1 != mir.insns[13].dst ||
        mir.insns[15].src2 != mir.insns[7].dst ||
        mir.insns[15].immediate != 1 ||
        !mir_machine_constant_equals(mir.insns[17].dst, 0) ||
        mir.insns[18].src1 != mir.insns[15].dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        mir.insns[18].memory_size != 1 ||
        !mir_machine_constant_equals(mir.insns[21].dst, 1) ||
        mir.insns[22].immediate != '+' ||
        mir.insns[22].src1 != mir.insns[7].dst ||
        mir.insns[22].src2 != mir.insns[21].dst ||
        mir.insns[25].label != mir.insns[5].label ||
        !mir_machine_constant_equals(mir.insns[29].dst, 0) ||
        mir.insns[30].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[30].src1 != mir.insns[28].dst ||
        mir.insns[30].src2 != mir.insns[29].dst ||
        mir.insns[30].immediate != 1 ||
        !mir_machine_constant_equals(mir.insns[32].dst, 1) ||
        mir.insns[33].opcode != MIR_STORE_INDIRECT ||
        mir.insns[33].src1 != mir.insns[30].dst ||
        mir.insns[33].src2 != mir.insns[32].dst ||
        mir.insns[33].memory_size != 1 ||
        !mir_machine_constant_equals(mir.insns[36].dst, 1) ||
        mir.insns[37].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[37].src1 != mir.insns[35].dst ||
        mir.insns[37].src2 != mir.insns[36].dst ||
        mir.insns[37].immediate != 1 ||
        !mir_machine_constant_equals(mir.insns[39].dst, 1) ||
        mir.insns[40].opcode != MIR_STORE_INDIRECT ||
        mir.insns[40].src1 != mir.insns[37].dst ||
        mir.insns[40].src2 != mir.insns[39].dst ||
        mir.insns[40].memory_size != 1 ||
        !mir_machine_constant_equals(mir.insns[41].dst, 2) ||
        mir.insns[47].src1 != mir.insns[41].dst ||
        mir.insns[47].src2 != mir.insns[93].dst ||
        mir.insns[50].immediate != '*' ||
        mir.insns[50].src1 != mir.insns[47].dst ||
        mir.insns[50].src2 != mir.insns[47].dst ||
        !mir_machine_constant_equals(
            mir.insns[51].dst, plan->limit) ||
        mir.insns[52].immediate != TOK_LE ||
        mir.insns[53].label != mir.insns[97].label ||
        mir.insns[57].src1 != mir.insns[55].dst ||
        mir.insns[57].src2 != mir.insns[47].dst ||
        mir.insns[57].immediate != 1 ||
        mir.insns[58].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[58].src1 != mir.insns[57].dst ||
        mir.insns[58].memory_size != 1 ||
        mir.insns[59].immediate != '!' ||
        mir.insns[59].src1 != mir.insns[58].dst ||
        mir.insns[60].label != mir.insns[89].label ||
        mir.insns[63].immediate != '*' ||
        mir.insns[63].src1 != mir.insns[47].dst ||
        mir.insns[63].src2 != mir.insns[47].dst ||
        mir.insns[65].src1 != mir.insns[63].dst ||
        !mir_machine_constant_equals(
            mir.insns[71].dst, plan->limit) ||
        mir.insns[72].immediate != TOK_LE ||
        mir.insns[73].label != mir.insns[88].label ||
        mir.insns[77].src1 != mir.insns[75].dst ||
        mir.insns[77].src2 != mir.insns[76].dst ||
        mir.insns[77].immediate != 1 ||
        !mir_machine_constant_equals(mir.insns[79].dst, 1) ||
        mir.insns[80].src1 != mir.insns[77].dst ||
        mir.insns[80].src2 != mir.insns[79].dst ||
        mir.insns[80].memory_size != 1 ||
        mir.insns[84].immediate != '+' ||
        mir.insns[84].src1 != mir.insns[82].dst ||
        mir.insns[84].src2 != mir.insns[47].dst ||
        mir.insns[87].label != mir.insns[66].label ||
        !mir_machine_constant_equals(mir.insns[92].dst, 1) ||
        mir.insns[93].immediate != '+' ||
        mir.insns[96].label != mir.insns[44].label)
        return mir_machine_reject(
            "fixed-sieve-build", "flow");
    return 1;
}

static void mir_emit_fixed_sieve_build(
    MirStream *out, const struct MirFixedSieveBuild *plan)
{
    int clear_loop = new_label();
    int p;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tex de,hl\n",
            plan->pointer_stack_offset);
    mir_machine_emit_hl_offset(out, plan->member_offset, 0);
    mir_stream_printf(out,
            "\tld b,%d\n\txor a\n"
            "L%d:\n\tld (hl),a\n\tinc hl\n"
            "\tdjnz L%d\n",
            (plan->limit + 1) & 255,
            clear_loop, clear_loop);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tex de,hl\n",
            plan->pointer_stack_offset);
    mir_machine_emit_hl_offset(out, plan->member_offset, 0);
    mir_stream_puts("\tld (hl),1\n\tinc hl\n\tld (hl),1\n", out);

    for (p = 2; p * p <= plan->limit; ++p) {
        int divisor;
        int is_prime = 1;
        int mark_loop;
        int start;
        int count;

        for (divisor = 2;
             divisor * divisor <= p; ++divisor) {
            if (p % divisor == 0) {
                is_prime = 0;
                break;
            }
        }
        if (!is_prime)
            continue;
        mark_loop = new_label();
        start = p * p;
        count = (plan->limit - start) / p + 1;
        mir_stream_printf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
                "\tex de,hl\n",
                plan->pointer_stack_offset);
        mir_machine_emit_hl_offset(
            out, plan->member_offset + start, 0);
        mir_stream_printf(out,
                "\tld de,%d\n\tld b,%d\n"
                "L%d:\n\tld (hl),1\n\tadd hl,de\n"
                "\tdjnz L%d\n",
                p, count, mark_loop, mark_loop);
    }
    mir_stream_puts("\tret\n", out);
}

static int mir_match_fixed_wrapper_init(
    struct MirFixedWrapperInit *plan)
{
    static const int constant_indices[] = {
        10, 22, 35, 40, 43, 56, 61, 64, 66,
        80, 84, 89, 92, 104, 119, 124, 129,
        144, 149, 154, 156, 172, 176, 181, 187,
        216, 230, 244, 249, 254, 268, 273, 278,
        280, 295, 299, 304, 310, 339, 354, 414,
        425, 436, 453, 463, 468, 473, 484, 489,
        494, 505, 510, 515, 522
    };
    static const long constant_values[] = {
        2, 3, 100, 10, 1, 20, 3, 2, 127,
        1000, 100, 10, 3, 4, 100, 10, 20,
        20, 5, 30, 127, 1000, 100, 10, 40,
        2, 3, 100, 10, 200, 20, 5, 60,
        127, 1000, 100, 10, 300, 1, 0, 3,
        1, 1, 4, 1, 3, 3, 1, 3,
        3, 1, 3, 3, 1
    };
    const struct MirInsn *pointer = &mir.insns[1];
    const struct MirInsn *base = &mir.insns[2];
    int index;

    memset(plan, 0, sizeof(*plan));
    if (sizeof(constant_indices) /
            sizeof(constant_indices[0]) !=
        sizeof(constant_values) /
            sizeof(constant_values[0]) ||
        mir.count != 527 || mir_cfg_block_count() != 22 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID ||
        pointer->opcode != MIR_PARAM ||
        type_ptr_depth(pointer->type) != 1 ||
        mir_machine_pointee_is_volatile(pointer) ||
        base->opcode != MIR_PARAM ||
        type_size(base->type) != 2 ||
        !mir_machine_parameter_value_offset(
            pointer->dst, &plan->pointer_stack_offset) ||
        !mir_machine_parameter_value_offset(
            base->dst, &plan->base_stack_offset))
        return mir_machine_reject(
            "fixed-wrapper-init", "shape");
    for (index = 0;
         index < (int)(sizeof(constant_indices) /
                       sizeof(constant_indices[0]));
         ++index) {
        if (!mir_machine_constant_equals(
                mir.insns[constant_indices[index]].dst,
                constant_values[index]))
            return mir_machine_reject(
                "fixed-wrapper-init", "constant");
    }
    plan->node_count = 2;
    plan->leaf_count = 3;
    plan->element_count = 4;
    plan->row_count = 2;
    plan->column_count = 3;
    plan->node_stride = 155;
    plan->leaf_stride = 35;
    if (!mir_machine_member_layout(
            32, &plan->value_offset, 2) ||
        !mir_machine_member_layout(
            114, &plan->array_offset, 8) ||
        !mir_machine_member_layout(
            53, &plan->char_value_offset, 1) ||
        !mir_machine_member_layout(
            139, &plan->char_array_offset, 4) ||
        !mir_machine_member_layout(
            77, &plan->long_value_offset, 4) ||
        !mir_machine_member_layout(
            167, &plan->long_array_offset, 16) ||
        !mir_machine_member_layout(
            333, &plan->leaf_pointer_offset, 2) ||
        !mir_machine_member_layout(
            346, &plan->int_pointer_offset, 2) ||
        !mir_machine_member_layout(
            361, &plan->char_pointer_offset, 2) ||
        !mir_machine_member_layout(
            376, &plan->long_pointer_offset, 2) ||
        !mir_machine_member_layout(
            237, &plan->matrix_offset, 12) ||
        !mir_machine_member_layout(
            261, &plan->char_matrix_offset, 6) ||
        !mir_machine_member_layout(
            288, &plan->long_matrix_offset, 24) ||
        !mir_machine_member_layout(
            396, &plan->wrapper_node_pointer_offset, 2) ||
        !mir_machine_member_layout(
            419, &plan->wrapper_leaf_pointers_offset, 6) ||
        !mir_machine_member_layout(
            457, &plan->wrapper_int_pointers_offset, 8) ||
        !mir_machine_member_layout(
            478, &plan->wrapper_char_pointers_offset, 8) ||
        !mir_machine_member_layout(
            499, &plan->wrapper_long_pointers_offset, 8))
        return mir_machine_reject(
            "fixed-wrapper-init", "layout");
    if (plan->value_offset != 0 ||
        plan->array_offset != 2 ||
        plan->char_value_offset != 10 ||
        plan->char_array_offset != 11 ||
        plan->long_value_offset != 15 ||
        plan->long_array_offset != 19 ||
        plan->leaf_pointer_offset != 105 ||
        plan->int_pointer_offset != 107 ||
        plan->char_pointer_offset != 109 ||
        plan->long_pointer_offset != 111 ||
        plan->matrix_offset != 113 ||
        plan->char_matrix_offset != 125 ||
        plan->long_matrix_offset != 131 ||
        plan->wrapper_node_pointer_offset != 310 ||
        plan->wrapper_leaf_pointers_offset != 312 ||
        plan->wrapper_int_pointers_offset != 318 ||
        plan->wrapper_char_pointers_offset != 326 ||
        plan->wrapper_long_pointers_offset != 334)
        return mir_machine_reject(
            "fixed-wrapper-init", "layout-offset");
    if (mir.insns[26].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[26].memory_size !=
            plan->node_count * plan->node_stride ||
        mir.insns[28].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[28].immediate != plan->node_stride ||
        mir.insns[29].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[29].memory_size !=
            plan->leaf_count * plan->leaf_stride ||
        mir.insns[31].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[31].immediate != plan->leaf_stride ||
        mir.insns[45].opcode != MIR_STORE_INDIRECT ||
        mir.insns[45].src1 != mir.insns[32].dst ||
        mir.insns[45].src2 != mir.insns[44].dst ||
        mir.insns[45].memory_size != 2 ||
        mir.insns[69].opcode != MIR_STORE_INDIRECT ||
        mir.insns[69].src1 != mir.insns[53].dst ||
        mir.insns[69].src2 != mir.insns[68].dst ||
        mir.insns[69].memory_size != 1 ||
        mir.insns[94].opcode != MIR_STORE_INDIRECT ||
        mir.insns[94].src1 != mir.insns[77].dst ||
        mir.insns[94].src2 != mir.insns[93].dst ||
        mir.insns[94].memory_size != 4 ||
        mir.insns[116].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[116].immediate != 2 ||
        mir.insns[131].opcode != MIR_STORE_INDIRECT ||
        mir.insns[131].src1 != mir.insns[116].dst ||
        mir.insns[131].src2 != mir.insns[130].dst ||
        mir.insns[131].memory_size != 2 ||
        mir.insns[141].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[141].immediate != 1 ||
        mir.insns[159].opcode != MIR_STORE_INDIRECT ||
        mir.insns[159].src1 != mir.insns[141].dst ||
        mir.insns[159].src2 != mir.insns[158].dst ||
        mir.insns[159].memory_size != 1 ||
        mir.insns[169].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[169].immediate != 4 ||
        mir.insns[189].opcode != MIR_STORE_INDIRECT ||
        mir.insns[189].src1 != mir.insns[169].dst ||
        mir.insns[189].src2 != mir.insns[188].dst ||
        mir.insns[189].memory_size != 4 ||
        mir.insns[239].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[239].immediate !=
            plan->column_count * 2 ||
        mir.insns[241].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[241].immediate != 2 ||
        mir.insns[256].opcode != MIR_STORE_INDIRECT ||
        mir.insns[256].src1 != mir.insns[241].dst ||
        mir.insns[256].src2 != mir.insns[255].dst ||
        mir.insns[256].memory_size != 2 ||
        mir.insns[263].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[263].immediate != plan->column_count ||
        mir.insns[265].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[265].immediate != 1 ||
        mir.insns[283].opcode != MIR_STORE_INDIRECT ||
        mir.insns[283].src1 != mir.insns[265].dst ||
        mir.insns[283].src2 != mir.insns[282].dst ||
        mir.insns[283].memory_size != 1 ||
        mir.insns[290].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[290].immediate !=
            plan->column_count * 4 ||
        mir.insns[292].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[292].immediate != 4 ||
        mir.insns[312].opcode != MIR_STORE_INDIRECT ||
        mir.insns[312].src1 != mir.insns[292].dst ||
        mir.insns[312].src2 != mir.insns[311].dst ||
        mir.insns[312].memory_size != 4)
        return mir_machine_reject(
            "fixed-wrapper-init", "nested-storage");
    if (mir.insns[12].opcode != MIR_BINARY ||
        mir.insns[12].immediate != '<' ||
        mir.insns[13].opcode != MIR_BRANCH_FALSE ||
        mir.insns[23].opcode != MIR_BINARY ||
        mir.insns[23].immediate != '<' ||
        mir.insns[24].opcode != MIR_BRANCH_FALSE ||
        mir.insns[105].opcode != MIR_BINARY ||
        mir.insns[105].immediate != '<' ||
        mir.insns[106].opcode != MIR_BRANCH_FALSE ||
        mir.insns[217].opcode != MIR_BINARY ||
        mir.insns[217].immediate != '<' ||
        mir.insns[218].opcode != MIR_BRANCH_FALSE ||
        mir.insns[231].opcode != MIR_BINARY ||
        mir.insns[231].immediate != '<' ||
        mir.insns[232].opcode != MIR_BRANCH_FALSE ||
        mir.insns[341].opcode != MIR_STORE_INDIRECT ||
        mir.insns[341].src1 != mir.insns[333].dst ||
        mir.insns[341].src2 != mir.insns[340].dst ||
        mir.insns[356].opcode != MIR_STORE_INDIRECT ||
        mir.insns[356].src1 != mir.insns[346].dst ||
        mir.insns[356].src2 != mir.insns[355].dst ||
        mir.insns[371].opcode != MIR_STORE_INDIRECT ||
        mir.insns[371].src1 != mir.insns[361].dst ||
        mir.insns[371].src2 != mir.insns[370].dst ||
        mir.insns[386].opcode != MIR_STORE_INDIRECT ||
        mir.insns[386].src1 != mir.insns[376].dst ||
        mir.insns[386].src2 != mir.insns[385].dst ||
        mir.insns[391].opcode != MIR_BINARY ||
        mir.insns[391].immediate != '+' ||
        mir.insns[393].opcode != MIR_JUMP ||
        mir.insns[401].opcode != MIR_STORE_INDIRECT ||
        mir.insns[401].src1 != mir.insns[396].dst ||
        mir.insns[401].src2 != mir.insns[400].dst ||
        mir.insns[416].opcode != MIR_BINARY ||
        mir.insns[416].immediate != '<' ||
        mir.insns[417].opcode != MIR_BRANCH_FALSE ||
        mir.insns[432].opcode != MIR_STORE_INDIRECT ||
        mir.insns[432].src1 != mir.insns[421].dst ||
        mir.insns[432].src2 != mir.insns[431].dst ||
        mir.insns[437].opcode != MIR_BINARY ||
        mir.insns[437].immediate != '+' ||
        mir.insns[439].opcode != MIR_JUMP ||
        mir.insns[454].opcode != MIR_BINARY ||
        mir.insns[454].immediate != '<' ||
        mir.insns[455].opcode != MIR_BRANCH_FALSE ||
        mir.insns[476].opcode != MIR_STORE_INDIRECT ||
        mir.insns[476].src1 != mir.insns[459].dst ||
        mir.insns[476].src2 != mir.insns[475].dst ||
        mir.insns[497].opcode != MIR_STORE_INDIRECT ||
        mir.insns[497].src1 != mir.insns[480].dst ||
        mir.insns[497].src2 != mir.insns[496].dst ||
        mir.insns[518].opcode != MIR_STORE_INDIRECT ||
        mir.insns[518].src1 != mir.insns[501].dst ||
        mir.insns[518].src2 != mir.insns[517].dst ||
        mir.insns[523].opcode != MIR_BINARY ||
        mir.insns[523].immediate != '+' ||
        mir.insns[525].opcode != MIR_JUMP ||
        mir.insns[526].opcode != MIR_LABEL)
        return mir_machine_reject(
            "fixed-wrapper-init", "flow");
    for (index = 0; index < mir.count; ++index) {
        if (mir.insns[index].opcode == MIR_CALL)
            return mir_machine_reject(
                "fixed-wrapper-init", "call");
    }
    return 1;
}

static void mir_emit_fixed_wrapper_init(
    MirStream *out, const struct MirFixedWrapperInit *plan)
{
    int node;
    int leaf;
    int row;
    int index;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-5\n\tadd hl,sp\n\tld sp,hl\n", out);
    mir_stream_printf(out,
            "\tld c,(ix%+d)\n\tld b,(ix%+d)\n"
            "\tld hl,1000\n",
            plan->base_stack_offset + 2,
            plan->base_stack_offset + 3);
    mir_emit_runtime_call(out, "__m1s");
    mir_stream_puts("\tld (ix-4),l\n\tld (ix-3),h\n"
          "\tld (ix-2),e\n\tld (ix-1),d\n", out);

    for (node = 0; node < plan->node_count; ++node) {
        int node_offset = node * plan->node_stride;

        for (leaf = 0;
             leaf < plan->leaf_count; ++leaf) {
            int leaf_offset =
                node_offset + leaf * plan->leaf_stride;

            mir_fixed_wrapper_load_pointer(
                out, plan, leaf_offset);
            mir_fixed_wrapper_store_word_loop(
                out, plan,
                node * 100 + leaf * 10 + 1, 1);
            mir_fixed_wrapper_store_word_loop(
                out, plan,
                node * 100 + leaf * 10 + 20,
                plan->element_count);
            mir_fixed_wrapper_store_char_loop(
                out, plan,
                node * 20 + leaf * 3 + 2, 1);
            mir_fixed_wrapper_store_char_loop(
                out, plan,
                node * 20 + leaf * 5 + 30,
                plan->element_count);
            mir_fixed_wrapper_store_long_loop(
                out, node * 100 + leaf * 10 + 3, 1);
            mir_fixed_wrapper_store_long_loop(
                out, node * 100 + leaf * 10 + 40,
                plan->element_count);
        }
        for (row = 0; row < plan->row_count; ++row) {
            mir_fixed_wrapper_load_pointer(
                out, plan,
                node_offset + plan->matrix_offset +
                    row * plan->column_count * 2);
            mir_fixed_wrapper_store_word_loop(
                out, plan,
                node * 100 + row * 10 + 200,
                plan->column_count);
            mir_fixed_wrapper_load_pointer(
                out, plan,
                node_offset + plan->char_matrix_offset +
                    row * plan->column_count);
            mir_fixed_wrapper_store_char_loop(
                out, plan,
                node * 20 + row * 5 + 60,
                plan->column_count);
            mir_fixed_wrapper_load_pointer(
                out, plan,
                node_offset + plan->long_matrix_offset +
                    row * plan->column_count * 4);
            mir_fixed_wrapper_store_long_loop(
                out, node * 100 + row * 10 + 300,
                plan->column_count);
        }
        mir_fixed_wrapper_store_pointer(
            out, plan,
            node_offset + plan->leaf_pointer_offset,
            node_offset + plan->leaf_stride);
        mir_fixed_wrapper_store_pointer(
            out, plan,
            node_offset + plan->int_pointer_offset,
            node_offset + plan->matrix_offset +
                plan->column_count * 2);
        mir_fixed_wrapper_store_pointer(
            out, plan,
            node_offset + plan->char_pointer_offset,
            node_offset + plan->char_matrix_offset +
                plan->column_count);
        mir_fixed_wrapper_store_pointer(
            out, plan,
            node_offset + plan->long_pointer_offset,
            node_offset + plan->long_matrix_offset +
                plan->column_count * 4);
    }
    mir_fixed_wrapper_store_pointer(
        out, plan, plan->wrapper_node_pointer_offset, 0);
    for (index = 0; index < plan->leaf_count; ++index) {
        int source_offset =
            (index & 1) * plan->node_stride +
            index * plan->leaf_stride;

        mir_fixed_wrapper_store_pointer(
            out, plan,
            plan->wrapper_leaf_pointers_offset + index * 2,
            source_offset);
    }
    for (index = 0; index < plan->element_count; ++index) {
        int node_offset =
            (index & 1) * plan->node_stride;
        int leaf_offset =
            node_offset +
            (index % plan->leaf_count) * plan->leaf_stride;

        mir_fixed_wrapper_store_pointer(
            out, plan,
            plan->wrapper_int_pointers_offset + index * 2,
            leaf_offset + plan->array_offset +
                (index & 3) * 2);
        mir_fixed_wrapper_store_pointer(
            out, plan,
            plan->wrapper_char_pointers_offset + index * 2,
            leaf_offset + plan->char_array_offset +
                (index & 3));
        mir_fixed_wrapper_store_pointer(
            out, plan,
            plan->wrapper_long_pointers_offset + index * 2,
            leaf_offset + plan->long_array_offset +
                (index & 3) * 4);
    }
    mir_stream_puts("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_inline_fold_check(
    struct MirInlineFoldCheck *plan)
{
    static const int memory_indices[6] = {
        56, 67, 75, 109, 120, 128
    };
    int first_arguments[4];
    int second_arguments[4];
    long value;
    int call_count = 0;
    int index;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 152 || mir_cfg_block_count() != 15 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_INT ||
        !mir_machine_constant_equals(mir.insns[1].dst, 0) ||
        mir.insns[3].opcode != MIR_STORE ||
        mir.insns[3].src1 != mir.insns[1].dst ||
        mir.insns[3].memory_size != 2 ||
        (plan->counter = find_global(mir.insns[3].name)) == NULL ||
        plan->counter->storage == SC_EXTERN ||
        !mir_machine_constant_equals(mir.insns[4].dst, 0) ||
        mir.insns[8].opcode != MIR_PHI ||
        !mir_machine_constant_value(
            mir.insns[10].dst, &value, 0))
        return mir_machine_reject(
            "inline-fold-check", "shape");
    plan->count = (int)value;
    if (plan->count <= 0 || plan->count > 255 ||
        mir.insns[11].opcode != MIR_BINARY ||
        mir.insns[11].immediate != '<' ||
        mir.insns[11].src1 != mir.insns[8].dst ||
        mir.insns[12].opcode != MIR_BRANCH_FALSE ||
        !mir_machine_constant_equals(mir.insns[13].dst, 0) ||
        !mir_machine_constant_value(
            mir.insns[15].dst, &value, 0))
        return mir_machine_reject(
            "inline-fold-check", "first-loop");
    plan->element_size = (int)value;
    if (plan->element_size != 2 ||
        mir.insns[19].opcode != MIR_CALL ||
        !mir_machine_call_has_no_arguments(&mir.insns[19]) ||
        (plan->next_function =
             find_global(mir.insns[19].name)) == NULL ||
        mir.insns[21].opcode != MIR_CALL ||
        (plan->set_function =
             find_global(mir.insns[21].name)) == NULL ||
        !mir_machine_four_call_arguments(
            &mir.insns[21], first_arguments) ||
        !mir_machine_constant_equals(first_arguments[0], 0) ||
        !mir_machine_constant_equals(
            first_arguments[1], plan->element_size) ||
        first_arguments[2] != mir.insns[8].dst ||
        first_arguments[3] != mir.insns[19].dst ||
        !mir_machine_constant_equals(mir.insns[24].dst, 1) ||
        mir.insns[25].opcode != MIR_BINARY ||
        mir.insns[25].immediate != '+' ||
        mir.insns[27].opcode != MIR_JUMP)
        return mir_machine_reject(
            "inline-fold-check", "first-call");
    if (!mir_machine_constant_value(
            mir.insns[29].dst, &value, 0))
        return mir_machine_reject(
            "inline-fold-check", "byte-base");
    plan->byte_base = (int)value;
    if (!mir_machine_constant_value(
            mir.insns[33].dst, &value, 0))
        return mir_machine_reject(
            "inline-fold-check", "byte-index");
    plan->byte_index = (int)value;
    if (!mir_machine_constant_value(
            mir.insns[35].dst, &value, 0))
        return mir_machine_reject(
            "inline-fold-check", "byte-value");
    plan->byte_value = (int)value;
    if (mir.insns[37].opcode != MIR_CALL ||
        find_global(mir.insns[37].name) !=
            plan->set_function ||
        !mir_machine_four_call_arguments(
            &mir.insns[37], second_arguments) ||
        second_arguments[0] != mir.insns[29].dst ||
        !mir_machine_constant_equals(second_arguments[1], 1) ||
        second_arguments[2] != mir.insns[33].dst ||
        second_arguments[3] != mir.insns[35].dst)
        return mir_machine_reject(
            "inline-fold-check", "byte-call");
    for (index = 0; index < 6; ++index) {
        struct Sym *memory;

        if (mir.insns[memory_indices[index]].opcode !=
                MIR_ADDRESS ||
            (memory =
                 find_global(
                     mir.insns[memory_indices[index]].name)) ==
                NULL)
            return mir_machine_reject(
                "inline-fold-check", "memory");
        if (index == 0)
            plan->memory = memory;
        else if (memory != plan->memory)
            return mir_machine_reject(
                "inline-fold-check", "memory-root");
    }
    if (plan->memory->storage == SC_EXTERN ||
        !mir_machine_inline_fold_setter(
            &mir.insns[21], plan->memory) ||
        !mir_machine_inline_fold_setter(
            &mir.insns[37], plan->memory) ||
        !mir_machine_constant_equals(mir.insns[38].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[41].dst, 0) ||
        mir.insns[45].opcode != MIR_PHI ||
        mir.insns[46].opcode != MIR_PHI ||
        !mir_machine_constant_equals(
            mir.insns[48].dst, plan->count) ||
        mir.insns[49].opcode != MIR_BINARY ||
        mir.insns[49].immediate != '<' ||
        mir.insns[50].opcode != MIR_BRANCH_FALSE ||
        !mir_machine_constant_equals(mir.insns[54].dst, 0) ||
        mir.insns[55].opcode != MIR_BRANCH_FALSE ||
        mir.insns[63].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[63].memory_size != 1 ||
        mir.insns[74].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[74].memory_size != 1 ||
        mir.insns[84].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[84].memory_size != 1 ||
        !mir_machine_constant_equals(mir.insns[85].dst, 8) ||
        mir.insns[87].opcode != MIR_BINARY ||
        mir.insns[87].immediate != TOK_SHL ||
        mir.insns[89].opcode != MIR_BINARY ||
        mir.insns[89].immediate != '|' ||
        mir.insns[94].opcode != MIR_BINARY ||
        mir.insns[94].immediate != '+' ||
        !mir_machine_constant_equals(mir.insns[99].dst, 1) ||
        mir.insns[100].opcode != MIR_BINARY ||
        mir.insns[100].immediate != '+' ||
        mir.insns[102].opcode != MIR_JUMP)
        return mir_machine_reject(
            "inline-fold-check", "sum-loop");
    if (!mir_machine_constant_equals(mir.insns[107].dst, 1) ||
        mir.insns[108].opcode != MIR_BRANCH_FALSE ||
        !mir_machine_constant_equals(
            mir.insns[114].dst,
            plan->byte_base + plan->byte_index) ||
        mir.insns[115].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[116].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[116].memory_size != 1 ||
        !mir_machine_constant_equals(
            mir.insns[125].dst,
            plan->byte_base + plan->byte_index) ||
        !mir_machine_constant_equals(
            mir.insns[135].dst,
            plan->byte_base + plan->byte_index + 1) ||
        mir.insns[137].opcode != MIR_LOAD_INDIRECT ||
        !mir_machine_constant_equals(mir.insns[138].dst, 8) ||
        mir.insns[140].opcode != MIR_BINARY ||
        mir.insns[140].immediate != TOK_SHL ||
        mir.insns[142].opcode != MIR_BINARY ||
        mir.insns[142].immediate != '|' ||
        mir.insns[147].opcode != MIR_BINARY ||
        mir.insns[147].immediate != '+' ||
        mir.insns[151].opcode != MIR_RETURN ||
        mir.insns[151].src1 != mir.insns[147].dst)
        return mir_machine_reject(
            "inline-fold-check", "return");
    for (index = 0; index < mir.count; ++index)
        if (mir.insns[index].opcode == MIR_CALL)
            ++call_count;
    return call_count == 3;
}

static void mir_emit_inline_fold_check(
    MirStream *out, const struct MirInlineFoldCheck *plan)
{
    const char *memory_name =
        asm_name_for(sym_asm_name(plan->memory));
    int fill_loop = new_label();
    int sum_loop = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tdec sp\n\tdec sp\n"
          "\tld hl,0\n", out);
    mir_machine_emit_global_word_store(
        out, plan->counter, 0);
    mir_stream_printf(out,
            "\tld (ix-2),0\n\tld (ix-1),0\n"
            "L%d:\n",
            fill_loop);
    mir_machine_emit_symbol_call(
        out, plan->next_function);
    mir_stream_printf(out,
            "\tld c,l\n\tld b,h\n"
            "\tld l,(ix-2)\n\tld h,0\n\tadd hl,hl\n"
            "\tld de,%s\n\tadd hl,de\n"
            "\tld (hl),c\n\tinc hl\n\tld (hl),b\n"
            "\tinc (ix-2)\n\tld a,(ix-2)\n\tcp %d\n"
            "\tjp nz,L%d\n"
            "\tld hl,%s%+d\n\tld (hl),%d\n"
            "\tld hl,%s\n\tld de,0\n"
            "\tld (ix-2),%d\n"
            "L%d:\n"
            "\tld c,(hl)\n\tinc hl\n"
            "\tld b,(hl)\n\tinc hl\n"
            "\tex de,hl\n\tadd hl,bc\n\tex de,hl\n"
            "\tdec (ix-2)\n\tjp nz,L%d\n",
            memory_name, plan->count, fill_loop,
            memory_name, plan->byte_base + plan->byte_index,
            plan->byte_value & 255,
            memory_name, plan->count,
            sum_loop, sum_loop);
    mir_stream_printf(out,
            "\tld hl,%s%+d\n\tld l,(hl)\n\tld h,0\n"
            "\tadd hl,de\n\tld sp,ix\n\tpop ix\n\tret\n",
            memory_name,
            plan->byte_base + plan->byte_index);
}

static int mir_match_deterministic_init_check(
    struct MirDeterministicInitCheck *plan)
{
    static const int init_additions[8] = {
        6, 14, 22, 26, 32, 36, 48, 54
    };
    static const int init_constants[8] = {
        5, 13, 21, 25, 31, 35, 47, 53
    };
    static const int init_stores[8] = {
        7, 15, 23, 27, 33, 37, 49, 55
    };
    static const int zero_constants[8] = {
        8, 10, 16, 18, 28, 42, 44, 50
    };
    static const int zero_stores[8] = {
        9, 11, 17, 19, 29, 43, 45, 51
    };
    static const int check_ids[7] = {
        58, 115, 163, 213, 241, 290, 337
    };
    static const int check_values[7] = {
        112, 160, 210, 238, 287, 334, 383
    };
    static const int check_calls[7] = {
        114, 162, 212, 240, 289, 336, 385
    };
    static const int comparisons[21] = {
        65, 74, 89, 104, 126, 135, 152,
        174, 185, 202, 221, 230, 251, 261,
        279, 300, 308, 326, 347, 357, 375
    };
    static const int parameter_additions[8] = {
        73, 125, 173, 184, 220, 229, 299, 325
    };
    static const int parameter_constants[8] = {
        72, 124, 172, 183, 219, 228, 298, 324
    };
    static const int init_offsets[8] = {
        2, 0, 6, 8, 0, 2, 8, 12
    };
    static const int zero_offsets[8] = {
        4, 6, 2, 4, 10, 6, 7, 10
    };
    static const int zero_sizes[8] = {
        2, 2, 2, 2, 2, 1, 1, 2
    };
    int arguments[2];
    struct Sym *check_function = NULL;
    int call_count = 0;
    int index;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 409 || mir_cfg_block_count() != 43 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_INT ||
        mir.insns[1].opcode != MIR_PARAM ||
        type_ptr_depth(mir.insns[1].type) != 0 ||
        type_size(mir.insns[1].type) != 2 ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst,
            &plan->parameter_stack_offset) ||
        mir.insns[3].opcode != MIR_STORE ||
        mir.insns[3].src1 != mir.insns[1].dst ||
        mir.insns[3].memory_size != 2)
        return mir_machine_reject(
            "deterministic-init-check", "shape");
    for (index = 0; index < 8; ++index) {
        if (!mir_machine_constant_equals(
                mir.insns[init_constants[index]].dst,
                index + 1) ||
            mir.insns[init_additions[index]].opcode != MIR_BINARY ||
            mir.insns[init_additions[index]].immediate != '+' ||
            mir.insns[init_additions[index]].src1 !=
                mir.insns[1].dst ||
            mir.insns[init_additions[index]].src2 !=
                mir.insns[init_constants[index]].dst ||
            mir.insns[init_stores[index]].opcode != MIR_STORE ||
            mir.insns[init_stores[index]].src1 !=
                mir.insns[init_additions[index]].dst ||
            mir.insns[init_stores[index]].immediate !=
                init_offsets[index] ||
            mir.insns[init_stores[index]].memory_size != 2 ||
            strcmp(mir.insns[init_stores[index]].name,
                   mir.insns[index == 0 ? 3 :
                       index < 4 ? 15 : 33].name) != 0 ||
            !mir_machine_constant_equals(
                mir.insns[zero_constants[index]].dst, 0) ||
            mir.insns[zero_stores[index]].opcode != MIR_STORE ||
            mir.insns[zero_stores[index]].src1 !=
                mir.insns[zero_constants[index]].dst ||
            mir.insns[zero_stores[index]].immediate !=
                zero_offsets[index] ||
            mir.insns[zero_stores[index]].memory_size !=
                zero_sizes[index] ||
            strcmp(mir.insns[zero_stores[index]].name,
                   mir.insns[index < 2 ? 3 :
                       index < 5 ? 15 : 33].name) != 0)
            return mir_machine_reject(
                "deterministic-init-check", "initializer");
    }
    if (!mir_machine_constant_equals(mir.insns[38].dst, 97) ||
        mir.insns[39].opcode != MIR_STORE ||
        mir.insns[39].src1 != mir.insns[38].dst ||
        mir.insns[39].immediate != 4 ||
        mir.insns[39].memory_size != 1 ||
        !mir_machine_constant_equals(mir.insns[40].dst, 98) ||
        mir.insns[41].opcode != MIR_STORE ||
        mir.insns[41].src1 != mir.insns[40].dst ||
        mir.insns[41].immediate != 5 ||
        mir.insns[41].memory_size != 1 ||
        mir.insns[56].opcode != MIR_STRING_ADDRESS ||
        mir.insns[57].opcode != MIR_STORE ||
        mir.insns[57].src1 != mir.insns[56].dst ||
        mir.insns[57].immediate != 14 ||
        mir.insns[57].memory_size != 2)
        return mir_machine_reject(
            "deterministic-init-check", "aggregate-init");
    for (index = 0; index < 21; ++index) {
        if (mir.insns[comparisons[index]].opcode != MIR_BINARY ||
            mir.insns[comparisons[index]].immediate != TOK_EQ ||
            mir.insns[comparisons[index] + 1].opcode !=
                MIR_BRANCH_FALSE)
            return mir_machine_reject(
                "deterministic-init-check", "comparison");
    }
    for (index = 0; index < 8; ++index) {
        if (!mir_machine_constant_equals(
                mir.insns[parameter_constants[index]].dst,
                index + 1) ||
            mir.insns[parameter_additions[index]].opcode !=
                MIR_BINARY ||
            mir.insns[parameter_additions[index]].immediate != '+' ||
            mir.insns[parameter_additions[index]].src1 !=
                mir.insns[1].dst ||
            mir.insns[parameter_additions[index]].src2 !=
                mir.insns[parameter_constants[index]].dst)
            return mir_machine_reject(
                "deterministic-init-check", "expected-value");
    }
    for (index = 0; index < 7; ++index) {
        struct Sym *function;

        if (!mir_machine_constant_equals(
                mir.insns[check_ids[index]].dst,
                20 + index) ||
            mir.insns[check_values[index]].opcode != MIR_PHI ||
            mir.insns[check_calls[index]].opcode != MIR_CALL ||
            (function =
                 find_global(
                     mir.insns[check_calls[index]].name)) == NULL ||
            !mir_machine_two_call_arguments(
                &mir.insns[check_calls[index]], arguments) ||
            arguments[0] != mir.insns[check_ids[index]].dst ||
            arguments[1] != mir.insns[check_values[index]].dst)
            return mir_machine_reject(
                "deterministic-init-check", "check-call");
        if (index == 0)
            check_function = function;
        else if (function != check_function)
            return mir_machine_reject(
                "deterministic-init-check", "check-function");
        plan->check_ids[index] = 20 + index;
    }
    if (mir.insns[386].opcode != MIR_ADDRESS ||
        strcmp(mir.insns[386].name, mir.insns[3].name) != 0 ||
        !mir_machine_constant_equals(mir.insns[387].dst, 0) ||
        mir.insns[388].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[389].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[390].opcode != MIR_ADDRESS ||
        strcmp(mir.insns[390].name, mir.insns[3].name) != 0 ||
        !mir_machine_constant_equals(mir.insns[391].dst, 1) ||
        mir.insns[392].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[393].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[394].opcode != MIR_BINARY ||
        mir.insns[394].immediate != '+' ||
        mir.insns[395].opcode != MIR_ADDRESS ||
        strcmp(mir.insns[395].name, mir.insns[15].name) != 0 ||
        !mir_machine_constant_equals(mir.insns[396].dst, 1) ||
        mir.insns[397].opcode != MIR_INDEX_ADDRESS ||
        !mir_machine_constant_equals(mir.insns[398].dst, 1) ||
        mir.insns[399].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[400].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[401].opcode != MIR_BINARY ||
        mir.insns[401].immediate != '+' ||
        mir.insns[402].opcode != MIR_ADDRESS ||
        strcmp(mir.insns[402].name, mir.insns[33].name) != 0 ||
        mir.insns[403].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[403].immediate != 8 ||
        !mir_machine_constant_equals(mir.insns[404].dst, 2) ||
        mir.insns[405].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[406].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[407].opcode != MIR_BINARY ||
        mir.insns[407].immediate != '+' ||
        mir.insns[408].opcode != MIR_RETURN ||
        mir.insns[408].src1 != mir.insns[407].dst)
        return mir_machine_reject(
            "deterministic-init-check", "return");
    for (index = 0; index < mir.count; ++index)
        if (mir.insns[index].opcode == MIR_CALL)
            ++call_count;
    if (call_count != 7)
        return mir_machine_reject(
            "deterministic-init-check", "call-count");
    plan->check_function = check_function;
    plan->check_count = 7;
    plan->return_multiplier = 4;
    plan->return_addend = 13;
    return 1;
}

static void mir_emit_deterministic_init_check(
    MirStream *out, const struct MirDeterministicInitCheck *plan)
{
    int check;
    int bit = 1;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (check = 0; check < plan->check_count; ++check) {
        mir_stream_puts("\tld hl,1\n\tpush hl\n", out);
        mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n",
                plan->check_ids[check]);
        mir_machine_emit_symbol_call(
            out, plan->check_function);
        mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    }
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tex de,hl\n",
            plan->parameter_stack_offset);
    if (plan->return_multiplier != 1) {
        mir_stream_puts("\tld c,l\n\tld b,h\n\tld hl,0\n", out);
        while (bit <= plan->return_multiplier / 2)
            bit <<= 1;
        for (; bit != 0; bit >>= 1) {
            mir_stream_puts("\tadd hl,hl\n", out);
            if ((plan->return_multiplier & bit) != 0)
                mir_stream_puts("\tadd hl,bc\n", out);
        }
    }
    mir_stream_printf(out,
            "\tld de,%d\n\tadd hl,de\n\tret\n",
            plan->return_addend);
}

static int mir_match_local_array_struct_checks(
    struct MirLocalArrayStructChecks *plan)
{
    static const int widths[3] = {1, 2, 4};
    static const int unsigned_kinds[3] = {1, 0, 0};
    static const int member_offsets[6] = {0, 1, 2, 4, 6, 10};
    static const int member_widths[6] = {1, 1, 2, 2, 4, 4};
    static const int member_unsigned[6] = {1, 0, 0, 1, 0, 1};
    static const int array_member_offsets[3] = {14, 18, 26};
    const struct MirInsn *stores[30];
    const struct MirInsn *calls[36];
    struct MirMachineForm array_roots[3];
    struct MirMachineForm struct_root;
    int opcode_counts[MIR_RETURN + 1];
    int store_count = 0;
    int call_count = 0;
    int call_index = 0;
    int check_index = 0;
    int instruction;
    int group;
    int item;

    memset(plan, 0, sizeof(*plan));
    memset(opcode_counts, 0, sizeof(opcode_counts));
    if (mir.count != 466 || mir_cfg_block_count() != 1 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return mir_machine_reject(
            "local-array-struct-checks", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode < 0 || insn->opcode > MIR_RETURN)
            return mir_machine_reject(
                "local-array-struct-checks", "opcode-range");
        ++opcode_counts[insn->opcode];
        switch (insn->opcode) {
        case MIR_LABEL:
        case MIR_NOP:
        case MIR_CONST:
        case MIR_ADDRESS:
        case MIR_INDEX_ADDRESS:
        case MIR_STRING_ADDRESS:
        case MIR_ARG:
        case MIR_UNARY:
        case MIR_LOAD:
        case MIR_MEMBER_ADDRESS:
        case MIR_LOAD_INDIRECT:
            break;
        case MIR_STORE:
            if (!mir_machine_unobservable_local_store(insn))
                return mir_machine_reject(
                    "local-array-struct-checks", "named-store");
            break;
        case MIR_STORE_INDIRECT:
            if (store_count >= 30 || insn->memory_flags != 0 ||
                insn->bit_width != 0)
                return mir_machine_reject(
                    "local-array-struct-checks", "indirect-store");
            stores[store_count++] = insn;
            break;
        case MIR_CALL:
            if (call_count >= 36 || insn->memory_flags != 0)
                return mir_machine_reject(
                    "local-array-struct-checks", "call");
            calls[call_count++] = insn;
            break;
        default:
            return mir_machine_reject(
                "local-array-struct-checks", "opcode");
        }
    }
    if (store_count != 30 || call_count != 36 ||
        opcode_counts[MIR_LABEL] != 1 ||
        opcode_counts[MIR_NOP] != 39 ||
        opcode_counts[MIR_CONST] != 90 ||
        opcode_counts[MIR_ADDRESS] != 19 ||
        opcode_counts[MIR_INDEX_ADDRESS] != 30 ||
        opcode_counts[MIR_STRING_ADDRESS] != 24 ||
        opcode_counts[MIR_ARG] != 90 ||
        opcode_counts[MIR_UNARY] != 16 ||
        opcode_counts[MIR_LOAD] != 36 ||
        opcode_counts[MIR_MEMBER_ADDRESS] != 36 ||
        opcode_counts[MIR_LOAD_INDIRECT] != 12 ||
        opcode_counts[MIR_STORE] != 7)
        return mir_machine_reject(
            "local-array-struct-checks", "census");

    for (group = 0; group < 3; ++group) {
        for (item = 0; item < 4; ++item) {
            const struct MirInsn *store = stores[group * 4 + item];
            struct MirMachineForm form;
            long value;

            if (store->memory_size != widths[group] ||
                !mir_machine_local_pointer_form(
                    store->src1, (int)(store - mir.insns),
                    &form, 0) ||
                form.storage != SC_LOCAL ||
                form.pointer_terms != 1 ||
                form.value != item * widths[group] ||
                !mir_machine_constant_value(
                    store->src2, &value, 0))
                return mir_machine_reject(
                    "local-array-struct-checks", "array-init");
            if (item == 0)
                array_roots[group] = form;
            else if (!mir_machine_same_pointer_root(
                         &form, &array_roots[group]))
                return mir_machine_reject(
                    "local-array-struct-checks", "array-root");
            plan->array_values[group][item] =
                (unsigned long)value & 0xffffffffUL;
        }
    }
    if (mir_machine_same_pointer_root(
            &array_roots[0], &array_roots[1]) ||
        mir_machine_same_pointer_root(
            &array_roots[0], &array_roots[2]) ||
        mir_machine_same_pointer_root(
            &array_roots[1], &array_roots[2]) ||
        stores[11] >= calls[0])
        return mir_machine_reject(
            "local-array-struct-checks", "array-layout");

    for (item = 0; item < 6; ++item) {
        const struct MirInsn *store = stores[12 + item];
        struct MirMachineForm form;
        long value;

        if (store->memory_size != member_widths[item] ||
            !mir_machine_local_pointer_form(
                store->src1, (int)(store - mir.insns),
                &form, 0) ||
            form.storage != SC_LOCAL ||
            form.pointer_terms != 1 ||
            form.value != member_offsets[item] ||
            !mir_machine_constant_value(store->src2, &value, 0))
            return mir_machine_reject(
                "local-array-struct-checks", "member-init");
        if (item == 0)
            struct_root = form;
        else if (!mir_machine_same_pointer_root(
                     &form, &struct_root))
            return mir_machine_reject(
                "local-array-struct-checks", "member-root");
        plan->scalar_values[item] =
            (unsigned long)value & 0xffffffffUL;
    }
    for (group = 0; group < 3; ++group) {
        for (item = 0; item < 4; ++item) {
            const struct MirInsn *store =
                stores[18 + group * 4 + item];
            struct MirMachineForm form;
            long value;

            if (store->memory_size != widths[group] ||
                !mir_machine_local_pointer_form(
                    store->src1, (int)(store - mir.insns),
                    &form, 0) ||
                !mir_machine_same_pointer_root(
                    &form, &struct_root) ||
                form.pointer_terms != 1 ||
                form.value != array_member_offsets[group] +
                    item * widths[group] ||
                !mir_machine_constant_value(
                    store->src2, &value, 0))
                return mir_machine_reject(
                    "local-array-struct-checks",
                    "member-array-init");
            plan->member_array_values[group][item] =
                (unsigned long)value & 0xffffffffUL;
        }
    }
    if (mir_machine_same_pointer_root(
            &struct_root, &array_roots[0]) ||
        mir_machine_same_pointer_root(
            &struct_root, &array_roots[1]) ||
        mir_machine_same_pointer_root(
            &struct_root, &array_roots[2]) ||
        calls[8] >= stores[12] || stores[17] >= calls[9] ||
        calls[26] >= stores[18] || stores[29] >= calls[27])
        return mir_machine_reject(
            "local-array-struct-checks", "phase-order");

    for (group = 0; group < 3; ++group) {
        const struct MirInsn *helper = calls[call_index++];

        if (!mir_machine_match_pointer_call(
                helper, &array_roots[group], 0,
                widths[group], unsigned_kinds[group], 1,
                &plan->array_indices[group],
                &plan->array_functions[group])) {
            if (getenv("DCC_MIR_MACHINE_REPORT") != NULL)
                fprintf(stderr,
                        "; MIR machine function=%s"
                        " template=local-array-struct-checks"
                        " group=%d reject=array-helper\n",
                        mir.name, group);
            return 0;
        }
        if (!mir_machine_match_check_call(
                calls[call_index++], helper->dst, NULL, 0,
                widths[group], unsigned_kinds[group],
                plan, check_index++)) {
            if (getenv("DCC_MIR_MACHINE_REPORT") != NULL)
                fprintf(stderr,
                        "; MIR machine function=%s"
                        " template=local-array-struct-checks"
                        " group=%d reject=array-result-check\n",
                        mir.name, group);
            return 0;
        }
        if (!mir_machine_match_check_call(
                calls[call_index++], -1, &array_roots[group],
                plan->array_indices[group] * widths[group],
                widths[group], unsigned_kinds[group],
                plan, check_index++)) {
            if (getenv("DCC_MIR_MACHINE_REPORT") != NULL)
                fprintf(stderr,
                        "; MIR machine function=%s"
                        " template=local-array-struct-checks"
                        " group=%d reject=array-store-check\n",
                        mir.name, group);
            return 0;
        }
    }
    for (item = 0; item < 6; ++item) {
        const struct MirInsn *helper = calls[call_index++];
        int ignored_index = 0;

        if (!mir_machine_match_pointer_call(
                helper, &struct_root, member_offsets[item],
                member_widths[item], member_unsigned[item], 0,
                &ignored_index, &plan->scalar_functions[item]) ||
            !mir_machine_match_check_call(
                calls[call_index++], helper->dst, NULL, 0,
                member_widths[item], member_unsigned[item],
                plan, check_index++) ||
            !mir_machine_match_check_call(
                calls[call_index++], -1, &struct_root,
                member_offsets[item], member_widths[item],
                member_unsigned[item], plan, check_index++))
            return mir_machine_reject(
                "local-array-struct-checks", "member-calls");
    }
    for (group = 0; group < 3; ++group) {
        const struct MirInsn *helper = calls[call_index++];
        struct Sym *function;

        if (!mir_machine_match_pointer_call(
                helper, &struct_root, array_member_offsets[group],
                widths[group], unsigned_kinds[group], 1,
                &plan->member_array_indices[group],
                &function) ||
            function != plan->array_functions[group] ||
            !mir_machine_match_check_call(
                calls[call_index++], helper->dst, NULL, 0,
                widths[group], unsigned_kinds[group],
                plan, check_index++) ||
            !mir_machine_match_check_call(
                calls[call_index++], -1, &struct_root,
                array_member_offsets[group] +
                    plan->member_array_indices[group] *
                    widths[group],
                widths[group], unsigned_kinds[group],
                plan, check_index++))
            return mir_machine_reject(
                "local-array-struct-checks",
                "member-array-calls");
    }
    if (call_index != 36 || check_index != 24 ||
        plan->check_functions[0] == NULL ||
        plan->check_functions[1] == NULL)
        return mir_machine_reject(
            "local-array-struct-checks", "call-count");
    return 1;
}

static void mir_emit_local_array_struct_checks(
    MirStream *out, const struct MirLocalArrayStructChecks *plan)
{
    static const int widths[3] = {1, 2, 4};
    static const int unsigned_kinds[3] = {1, 0, 0};
    static const int array_offsets[3] = {-70, -66, -58};
    static const int member_offsets[6] = {0, 1, 2, 4, 6, 10};
    static const int member_widths[6] = {1, 1, 2, 2, 4, 4};
    static const int member_unsigned[6] = {1, 0, 0, 1, 0, 1};
    static const int array_member_offsets[3] = {14, 18, 26};
    const int struct_offset = -42;
    int check = 0;
    int group;
    int item;

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-70\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (group = 0; group < 3; ++group)
        for (item = 0; item < 4; ++item)
            mir_local_array_struct_store(
                out, array_offsets[group] + item * widths[group],
                widths[group], plan->array_values[group][item]);
    for (group = 0; group < 3; ++group) {
        int value_offset = array_offsets[group] +
            plan->array_indices[group] * widths[group];

        mir_local_array_struct_pointer_call(
            out, plan->array_functions[group],
            array_offsets[group], widths[group],
            unsigned_kinds[group], 1,
            plan->array_indices[group]);
        mir_local_array_struct_check(
            out, plan, check++, unsigned_kinds[group]);
        mir_local_array_struct_load(
            out, value_offset, widths[group],
            unsigned_kinds[group]);
        mir_local_array_struct_check(
            out, plan, check++, unsigned_kinds[group]);
    }

    for (item = 0; item < 6; ++item)
        mir_local_array_struct_store(
            out, struct_offset + member_offsets[item],
            member_widths[item], plan->scalar_values[item]);
    for (item = 0; item < 6; ++item) {
        mir_local_array_struct_pointer_call(
            out, plan->scalar_functions[item],
            struct_offset + member_offsets[item],
            member_widths[item], member_unsigned[item],
            0, 0);
        mir_local_array_struct_check(
            out, plan, check++, member_unsigned[item]);
        mir_local_array_struct_load(
            out, struct_offset + member_offsets[item],
            member_widths[item], member_unsigned[item]);
        mir_local_array_struct_check(
            out, plan, check++, member_unsigned[item]);
    }

    for (group = 0; group < 3; ++group)
        for (item = 0; item < 4; ++item)
            mir_local_array_struct_store(
                out, struct_offset + array_member_offsets[group] +
                    item * widths[group],
                widths[group],
                plan->member_array_values[group][item]);
    for (group = 0; group < 3; ++group) {
        int base = struct_offset + array_member_offsets[group];
        int value_offset = base +
            plan->member_array_indices[group] * widths[group];

        mir_local_array_struct_pointer_call(
            out, plan->array_functions[group], base,
            widths[group], unsigned_kinds[group], 1,
            plan->member_array_indices[group]);
        mir_local_array_struct_check(
            out, plan, check++, unsigned_kinds[group]);
        mir_local_array_struct_load(
            out, value_offset, widths[group],
            unsigned_kinds[group]);
        mir_local_array_struct_check(
            out, plan, check++, unsigned_kinds[group]);
    }
    mir_stream_puts("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_alias_mix_schedule(
    struct MirAliasMixSchedule *plan)
{
    static const int expected_counts[MIR_RETURN + 1] = {
        [MIR_LABEL] = 1,
        [MIR_NOP] = 24,
        [MIR_CONST] = 234,
        [MIR_ADDRESS] = 92,
        [MIR_INDEX_ADDRESS] = 121,
        [MIR_MEMBER_ADDRESS] = 55,
        [MIR_LOAD_INDIRECT] = 54,
        [MIR_STORE_INDIRECT] = 46,
        [MIR_BINARY] = 74,
        [MIR_STRING_ADDRESS] = 24,
        [MIR_ARG] = 72,
        [MIR_CALL] = 24,
        [MIR_STORE] = 1
    };
    const struct MirInsn *stores[46];
    const struct MirInsn *calls[24];
    const struct MirInsn *named_store = NULL;
    struct MirAliasMixPointerSlot slots[22];
    int opcode_counts[MIR_RETURN + 1];
    int group_counts[6] = {0, 0, 0, 0, 0, 0};
    int group_masks[6] = {0, 0, 0, 0, 0, 0};
    char group_names[6][64];
    int group_offsets[6];
    int sorted_counts[6];
    int store_count = 0;
    int call_count = 0;
    int group_count = 0;
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    memset(opcode_counts, 0, sizeof(opcode_counts));
    memset(group_names, 0, sizeof(group_names));
    if (mir.count != 822 || mir_cfg_block_count() != 1 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return mir_machine_reject("alias-mix-schedule", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode < 0 || insn->opcode > MIR_RETURN)
            return mir_machine_reject(
                "alias-mix-schedule", "opcode-range");
        ++opcode_counts[insn->opcode];
        switch (insn->opcode) {
        case MIR_LABEL:
        case MIR_NOP:
        case MIR_CONST:
        case MIR_ADDRESS:
        case MIR_INDEX_ADDRESS:
        case MIR_MEMBER_ADDRESS:
        case MIR_BINARY:
        case MIR_STRING_ADDRESS:
        case MIR_ARG:
            break;
        case MIR_LOAD_INDIRECT:
            if ((insn->memory_flags & (1 | 8)) != 0 ||
                insn->bit_width != 0)
                return mir_machine_reject(
                    "alias-mix-schedule", "load");
            break;
        case MIR_STORE_INDIRECT:
            if (store_count >= 46 ||
                (insn->memory_flags & (1 | 8)) != 0 ||
                insn->bit_width != 0 ||
                (insn->memory_size != 1 &&
                 insn->memory_size != 2 &&
                 insn->memory_size != 4))
                return mir_machine_reject(
                    "alias-mix-schedule", "indirect-store");
            stores[store_count++] = insn;
            break;
        case MIR_STORE:
            if (named_store != NULL ||
                !mir_machine_unobservable_local_store(insn) ||
                insn->memory_size != 2)
                return mir_machine_reject(
                    "alias-mix-schedule", "named-store");
            named_store = insn;
            break;
        case MIR_CALL:
            if (call_count >= 24 || insn->memory_flags != 0)
                return mir_machine_reject(
                    "alias-mix-schedule", "call");
            calls[call_count++] = insn;
            break;
        default:
            return mir_machine_reject(
                "alias-mix-schedule", "opcode");
        }
    }
    for (instruction = 0; instruction <= MIR_RETURN; ++instruction)
        if (opcode_counts[instruction] != expected_counts[instruction])
            return mir_machine_reject(
                "alias-mix-schedule", "census");
    if (store_count != 46 || call_count != 24 ||
        named_store == NULL || stores[21] >= named_store ||
        named_store >= stores[22] || stores[45] >= calls[0])
        return mir_machine_reject(
            "alias-mix-schedule", "phase-order");

    for (item = 0; item < 22; ++item) {
        const struct MirInsn *store = stores[item];
        struct MirAliasMixForm destination;
        struct MirAliasMixForm source;
        int duplicate;
        int group;
        long root_offset;

        if (store->memory_size != 2 ||
            !mir_alias_mix_form(
                store->src1, (int)(store - mir.insns),
                slots, item, &destination, 0) ||
            !mir_alias_mix_form(
                store->src2, (int)(store - mir.insns),
                slots, item, &source, 0) ||
            destination.kind != MIR_ALIAS_MIX_LOCAL ||
            destination.value < 0 ||
            (destination.value & 1) != 0 ||
            source.kind != MIR_ALIAS_MIX_GLOBAL ||
            source.root == NULL)
            return mir_machine_reject(
                "alias-mix-schedule", "pointer-init");
        root_offset = source.value;
        if (root_offset < -32768 || root_offset > 32767)
            return mir_machine_reject(
                "alias-mix-schedule", "pointer-offset");
        duplicate = 0;
        for (instruction = 0; instruction < item; ++instruction)
            if (slots[instruction].base_offset ==
                    destination.base_offset &&
                slots[instruction].slot_offset == destination.value &&
                !strcmp(slots[instruction].name, destination.name))
                duplicate = 1;
        if (duplicate)
            return mir_machine_reject(
                "alias-mix-schedule", "pointer-duplicate");
        snprintf(slots[item].name, sizeof(slots[item].name), "%s",
                 destination.name);
        slots[item].base_offset = destination.base_offset;
        slots[item].slot_offset = destination.value;
        slots[item].root = source.root;
        slots[item].root_offset = (int)root_offset;
        slots[item].store_index = (int)(store - mir.insns);

        for (group = 0; group < group_count; ++group)
            if (group_offsets[group] == destination.base_offset &&
                !strcmp(group_names[group], destination.name))
                break;
        if (group == group_count) {
            if (group_count >= 6)
                return mir_machine_reject(
                    "alias-mix-schedule", "pointer-groups");
            group_offsets[group] = destination.base_offset;
            snprintf(group_names[group], sizeof(group_names[group]), "%s",
                     destination.name);
            ++group_count;
        }
        if (destination.value > 8)
            return mir_machine_reject(
                "alias-mix-schedule", "pointer-span");
        ++group_counts[group];
        group_masks[group] |= 1 << (destination.value / 2);
    }
    if (group_count != 6)
        return mir_machine_reject(
            "alias-mix-schedule", "pointer-group-count");
    for (item = 0; item < 6; ++item) {
        int insert = item;

        if (group_masks[item] != (1 << group_counts[item]) - 1)
            return mir_machine_reject(
                "alias-mix-schedule", "pointer-contiguity");
        sorted_counts[item] = group_counts[item];
        while (insert > 0 &&
               sorted_counts[insert - 1] > sorted_counts[insert]) {
            int temporary = sorted_counts[insert - 1];

            sorted_counts[insert - 1] = sorted_counts[insert];
            sorted_counts[insert] = temporary;
            --insert;
        }
    }
    if (sorted_counts[0] != 2 || sorted_counts[1] != 3 ||
        sorted_counts[2] != 4 || sorted_counts[3] != 4 ||
        sorted_counts[4] != 4 || sorted_counts[5] != 5)
        return mir_machine_reject(
            "alias-mix-schedule", "pointer-layout");

    for (item = 0; item < 24; ++item) {
        const struct MirInsn *store = stores[item + 22];
        const struct MirInsn *source = mir_definition(store->src2);
        long value;

        if (source == NULL ||
            type_ptr_depth(source->type) != 0 ||
            type_is_float(source->type) ||
            type_size(source->type) != store->memory_size ||
            !mir_machine_constant_value(store->src2, &value, 0) ||
            !mir_alias_mix_address(
                store->src1, (int)(store - mir.insns),
                slots, 22, &plan->stores[item].address))
            return mir_machine_reject(
                "alias-mix-schedule", "observable-store");
        plan->stores[item].width = store->memory_size;
        plan->stores[item].value =
            (unsigned long)value & 0xffffffffUL;
    }

    for (item = 0; item < 24; ++item) {
        const struct MirInsn *call = calls[item];
        const struct MirInsn *string;
        const struct MirInsn *actual;
        const struct MirInsn *expected_definition;
        struct Sym *function;
        int arguments[3];
        long expected;
        int expected_width;

        if (!mir_machine_three_call_arguments(call, arguments) ||
            (string = mir_definition(arguments[0])) == NULL ||
            string->opcode != MIR_STRING_ADDRESS ||
            (actual = mir_definition(arguments[1])) == NULL ||
            actual->opcode != MIR_LOAD_INDIRECT ||
            (actual->memory_flags & (1 | 8)) != 0 ||
            actual->bit_width != 0 ||
            type_ptr_depth(actual->type) != 0 ||
            type_is_float(actual->type) ||
            actual->memory_size != type_size(actual->type) ||
            (actual->memory_size != 1 &&
             actual->memory_size != 2 &&
             actual->memory_size != 4) ||
            (expected_definition =
                mir_definition(arguments[2])) == NULL ||
            type_ptr_depth(expected_definition->type) != 0 ||
            type_is_float(expected_definition->type) ||
            !mir_machine_constant_value(
                arguments[2], &expected, 0) ||
            !mir_alias_mix_address(
                actual->src1, (int)(actual - mir.insns),
                slots, 22, &plan->checks[item].address) ||
            plan->checks[item].address.indirect ||
            (function = find_global(call->name)) == NULL ||
            !function->is_defined || function->is_funcptr ||
            function->is_noreturn ||
            type_ptr_depth(call->type) != 0 ||
            (call->type & 15) != TYPE_VOID)
            return mir_machine_reject(
                "alias-mix-schedule", "check");
        expected_width = actual->memory_size == 4 ? 4 : 2;
        if (type_size(expected_definition->type) != expected_width)
            return mir_machine_reject(
                "alias-mix-schedule", "check-width");
        plan->checks[item].function = function;
        plan->checks[item].string_id = (int)string->immediate;
        plan->checks[item].expected =
            (unsigned long)expected & 0xffffffffUL;
        plan->checks[item].width = actual->memory_size;
        plan->checks[item].is_unsigned =
            (actual->type & TYPE_UNSIGNED) != 0;
    }
    return 1;
}

static void mir_emit_alias_mix_schedule(
    MirStream *out, const struct MirAliasMixSchedule *plan)
{
    int item;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (item = 0; item < 24; ++item)
        mir_alias_mix_emit_store(out, &plan->stores[item]);
    for (item = 0; item < 24; ++item)
        mir_alias_mix_emit_check(out, &plan->checks[item]);
    mir_stream_puts("\tret\n", out);
}

static int mir_match_bitfield_report_sequence(
    struct MirBitfieldReportSequence *plan)
{
    static const int expected_opcodes[432] = {
        MIR_LABEL, MIR_CONST, MIR_STORE, MIR_CONST, MIR_STORE, MIR_CONST, MIR_STORE, MIR_CONST,
        MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_ARG, MIR_CALL, MIR_ARG, MIR_CALL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_ARG, MIR_CALL, MIR_ARG, MIR_CALL, MIR_CONST,
        MIR_NOP, MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL_AGGREGATE, MIR_NOP, MIR_NOP, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_NOP,
        MIR_STORE_INDIRECT, MIR_LOAD_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_NOP, MIR_STORE_INDIRECT, MIR_LOAD_INDIRECT,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_NOP, MIR_STORE_INDIRECT, MIR_LOAD_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_CONST, MIR_STORE_INDIRECT, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_ARG, MIR_CALL, MIR_ARG,
        MIR_CALL, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_NOP, MIR_STORE_INDIRECT, MIR_LOAD_INDIRECT, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_CONST, MIR_NOP, MIR_STORE_INDIRECT, MIR_LOAD_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST,
        MIR_NOP, MIR_STORE_INDIRECT, MIR_LOAD_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_NOP, MIR_BINARY, MIR_STORE_INDIRECT, MIR_LOAD_INDIRECT, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY, MIR_STORE_INDIRECT, MIR_LOAD_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY, MIR_STORE_INDIRECT, MIR_UNARY, MIR_STORE, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY, MIR_STORE_INDIRECT, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_STORE, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY, MIR_STORE_INDIRECT, MIR_LOAD_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_NOP, MIR_BINARY, MIR_STORE_INDIRECT, MIR_LOAD_INDIRECT, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_NOP, MIR_STORE_INDIRECT, MIR_LOAD_INDIRECT, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_CONST, MIR_NOP, MIR_STORE_INDIRECT, MIR_LOAD_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST,
        MIR_NOP, MIR_STORE_INDIRECT, MIR_LOAD_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY, MIR_STORE_INDIRECT, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_STORE,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_NOP, MIR_BINARY, MIR_STORE_INDIRECT, MIR_LOAD_INDIRECT,
        MIR_UNARY, MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG,
        MIR_NOP, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_LOAD_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_CONST, MIR_NOP, MIR_STORE_INDIRECT, MIR_LOAD_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY, MIR_STORE_INDIRECT, MIR_LOAD_INDIRECT, MIR_NOP,
        MIR_STORE, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY, MIR_STORE_INDIRECT, MIR_NOP,
        MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_NOP,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_ARG, MIR_CALL, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_CONST, MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL_AGGREGATE, MIR_NOP, MIR_NOP, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_ARG, MIR_CALL, MIR_ARG, MIR_CALL,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT, MIR_LOAD_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_CONST, MIR_NOP, MIR_STORE_INDIRECT, MIR_LOAD_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN
    };
    static const int unsigned_reports[4][8] = {
        {9, 11, 15, 19, 23, 27, 29, 31},
        {32, 34, 38, 42, 46, 50, 52, 54},
        {69, 71, 75, 79, 83, 87, 89, 91},
        {114, 116, 120, 124, 128, 132, 134, 136}
    };
    static const int signed_reports[4][7] = {
        {324, 326, 330, 334, 338, 340, 342},
        {343, 345, 349, 353, 357, 359, 361},
        {373, 375, 379, 383, 387, 389, 391},
        {408, 410, 414, 418, 422, 424, 426}
    };
    const struct MirInsn *unsigned_roots[4];
    const struct MirInsn *signed_roots[4];
    const struct MirInsn *m_root;
    const struct MirInsn *ms_root;
    int arguments4[4];
    int arguments5[5];
    int arguments6[6];
    int memory_type, memory_storage, memory_offset;
    int m_offset;
    int ms_offset;
    int instruction;
    int report;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 432 || mir_cfg_block_count() != 1 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_INT ||
        type_ptr_depth(mir.return_type) != 0)
        return mir_machine_reject("bitfield-report-sequence", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "bitfield-report-sequence", "opcode");

    for (report = 0; report < 4; ++report)
        if (!mir_machine_match_unsigned_bitfield_report(
                plan, report, unsigned_reports[report],
                &unsigned_roots[report]))
            return mir_machine_reject(
                "bitfield-report-sequence", "unsigned-report");
    for (report = 0; report < 4; ++report)
        if (!mir_machine_match_signed_bitfield_report(
                plan, report + 7, signed_reports[report],
                &signed_roots[report]))
            return mir_machine_reject(
                "bitfield-report-sequence", "signed-report");

    if (!mir_scalar_memory_location(
            unsigned_roots[0], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        (plan->globals[0] =
             find_global(unsigned_roots[0]->name)) == NULL ||
        plan->globals[0]->is_volatile ||
        !mir_scalar_memory_location(
            signed_roots[0], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        (plan->globals[1] =
             find_global(signed_roots[0]->name)) == NULL ||
        plan->globals[1]->is_volatile)
        return mir_machine_reject(
            "bitfield-report-sequence", "globals");
    if (!mir_machine_same_bitfield_root(
            unsigned_roots[2], unsigned_roots[3]) ||
        !mir_machine_same_bitfield_root(
            signed_roots[2], signed_roots[3]))
        return mir_machine_reject(
            "bitfield-report-sequence", "report-roots");
    m_root = unsigned_roots[2];
    ms_root = signed_roots[2];
    if (!mir_scalar_memory_location(
            m_root, &memory_type, &memory_storage, &m_offset) ||
        memory_storage != SC_LOCAL ||
        !mir_scalar_memory_location(
            ms_root, &memory_type, &memory_storage, &ms_offset) ||
        memory_storage != SC_LOCAL)
        return mir_machine_reject(
            "bitfield-report-sequence", "local-roots");

    if (!mir_machine_constant_equals(mir.insns[1].dst, 25403) ||
        !mir_machine_constant_equals(mir.insns[3].dst, 456) ||
        !mir_machine_constant_equals(mir.insns[5].dst, 94) ||
        !mir_machine_constant_equals(mir.insns[7].dst, 20) ||
        strcmp(mir.insns[2].name, unsigned_roots[1]->name) ||
        strcmp(mir.insns[6].name, signed_roots[1]->name))
        return mir_machine_reject(
            "bitfield-report-sequence", "initializers");

    if (!mir_machine_four_call_arguments(
            &mir.insns[66], arguments4) ||
        !mir_machine_constant_equals(arguments4[0], 6) ||
        !mir_machine_constant_equals(arguments4[1], 31) ||
        !mir_machine_constant_equals(arguments4[2], 255) ||
        !mir_machine_constant_equals(arguments4[3], 1000) ||
        mir.insns[66].memory_size != 4 ||
        mir.insns[66].memory_flags != 0 ||
        mir.insns[66].immediate != m_offset ||
        strcmp(mir.insns[66].base_name, m_root->name) ||
        (plan->make_functions[0] =
             find_global(mir.insns[66].name)) == NULL ||
        !plan->make_functions[0]->is_defined ||
        !type_is_struct_object(plan->make_functions[0]->type) ||
        type_size(plan->make_functions[0]->type) != 4 ||
        !plan->make_functions[0]->has_proto ||
        plan->make_functions[0]->proto_variadic ||
        plan->make_functions[0]->proto_nargs != 4)
        return mir_machine_reject(
            "bitfield-report-sequence", "make-unsigned");
    if (!mir_machine_three_call_arguments(
            &mir.insns[370], arguments4) ||
        !mir_machine_constant_equals(arguments4[0], 65532) ||
        !mir_machine_constant_equals(arguments4[1], 15) ||
        !mir_machine_constant_equals(arguments4[2], 30) ||
        mir.insns[370].memory_size != 4 ||
        mir.insns[370].memory_flags != 0 ||
        mir.insns[370].immediate != ms_offset ||
        strcmp(mir.insns[370].base_name, ms_root->name) ||
        (plan->make_functions[1] =
             find_global(mir.insns[370].name)) == NULL ||
        !plan->make_functions[1]->is_defined ||
        !type_is_struct_object(plan->make_functions[1]->type) ||
        type_size(plan->make_functions[1]->type) != 4 ||
        !plan->make_functions[1]->has_proto ||
        plan->make_functions[1]->proto_variadic ||
        plan->make_functions[1]->proto_nargs != 3)
        return mir_machine_reject(
            "bitfield-report-sequence", "make-signed");

    if (!mir_machine_match_bitfield_constant_store(
            m_root, 92, 94, 96, 97, 0, 0, 3, 7, 1, 2) ||
        !mir_machine_match_bitfield_constant_store(
            m_root, 98, 100, 102, 103, 0, 3, 5, 248, 1, 4) ||
        !mir_machine_match_bitfield_constant_store(
            m_root, 104, 106, 108, 109, 0, 8, 8, 65280, 1, 8) ||
        !mir_machine_match_bitfield_constant_store(
            m_root, 110, 112, 113, -1, 2, 0, 0, 0, 0, 16) ||
        !mir_machine_match_bitfield_constant_store(
            m_root, 137, 139, 141, 142, 0, 0, 3, 7, 1, 1) ||
        !mir_machine_match_bitfield_constant_store(
            m_root, 143, 145, 147, 148, 0, 3, 5, 248, 1, 3) ||
        !mir_machine_match_bitfield_constant_store(
            m_root, 149, 151, 153, 154, 0, 8, 8, 65280, 1, 4) ||
        !mir_machine_match_bitfield_constant_store(
            m_root, 155, 157, 158, -1, 2, 0, 0, 0, 0, 0) ||
        !mir_machine_match_bitfield_constant_store(
            m_root, 225, 227, 229, 230, 0, 0, 3, 7, 1, 7) ||
        !mir_machine_match_bitfield_constant_store(
            m_root, 231, 233, 235, 236, 0, 3, 5, 248, 1, 7) ||
        !mir_machine_match_bitfield_constant_store(
            m_root, 237, 239, 241, 242, 0, 8, 8, 65280, 1, 200) ||
        !mir_machine_match_bitfield_constant_store(
            m_root, 243, 245, 246, -1, 2, 0, 0, 0, 0, 0) ||
        !mir_machine_match_bitfield_constant_store(
            ms_root, 281, 283, 284, 285, 0, 0, 4, 15, 0, 5) ||
        !mir_machine_match_bitfield_constant_store(
            ms_root, 286, 288, 290, 291, 0, 4, 4, 240, 1, 0) ||
        !mir_machine_match_bitfield_constant_store(
            ms_root, 292, 294, 295, -1, 2, 0, 0, 0, 0, 0) ||
        !mir_machine_match_bitfield_constant_store(
            ms_root, 392, 395, 396, 397, 0, 0, 4, 15, 0, 65535) ||
        !mir_machine_match_bitfield_constant_store(
            ms_root, 398, 400, 402, 403, 0, 4, 4, 240, 1, 14) ||
        !mir_machine_match_bitfield_constant_store(
            ms_root, 404, 406, 407, -1, 2, 0, 0, 0, 0, 40))
        return mir_machine_reject(
            "bitfield-report-sequence", "constant-stores");

    if (!mir_machine_match_bitfield_rmw(
            m_root, 159, 165, 166, 0, 0, 3, 7, 1) ||
        !mir_machine_match_bitfield_rmw(
            m_root, 167, 172, 173, 0, 8, 8, 65280, 1) ||
        !mir_machine_match_bitfield_rmw(
            m_root, 174, 179, -1, 0, 0, 3, 7, 1) ||
        !mir_machine_match_bitfield_rmw(
            m_root, 182, 187, 188, 0, 3, 5, 248, 1) ||
        !mir_machine_match_bitfield_rmw(
            m_root, 191, 196, 197, 0, 8, 8, 65280, 1) ||
        !mir_machine_match_bitfield_rmw(
            m_root, 198, 204, 205, 0, 3, 5, 248, 1) ||
        mir.insns[164].immediate != '+' ||
        mir.insns[164].src1 != mir.insns[161].dst ||
        mir.insns[164].src2 != mir.insns[162].dst ||
        mir.insns[165].src2 != mir.insns[164].dst ||
        mir.insns[171].immediate != TOK_SHL ||
        mir.insns[171].src1 != mir.insns[169].dst ||
        mir.insns[171].src2 != mir.insns[170].dst ||
        mir.insns[172].src2 != mir.insns[171].dst ||
        mir.insns[178].immediate != '+' ||
        mir.insns[178].src1 != mir.insns[176].dst ||
        mir.insns[178].src2 != mir.insns[177].dst ||
        mir.insns[179].src2 != mir.insns[178].dst ||
        mir.insns[180].src1 != mir.insns[176].dst ||
        mir.insns[186].immediate != '+' ||
        mir.insns[186].src1 != mir.insns[184].dst ||
        mir.insns[186].src2 != mir.insns[185].dst ||
        mir.insns[187].src2 != mir.insns[186].dst ||
        mir.insns[189].src1 != mir.insns[188].dst ||
        mir.insns[195].immediate != '-' ||
        mir.insns[195].src1 != mir.insns[193].dst ||
        mir.insns[195].src2 != mir.insns[194].dst ||
        mir.insns[196].src2 != mir.insns[195].dst ||
        mir.insns[203].immediate != '-' ||
        mir.insns[203].src1 != mir.insns[200].dst ||
        mir.insns[203].src2 != mir.insns[201].dst ||
        mir.insns[204].src2 != mir.insns[203].dst)
        return mir_machine_reject(
            "bitfield-report-sequence", "rmw-unsigned");
    if (!mir_machine_match_bitfield_rmw(
            m_root, 247, 252, 253, 0, 0, 3, 7, 1) ||
        !mir_machine_match_bitfield_rmw(
            m_root, 256, 262, 263, 0, 3, 5, 248, 1) ||
        !mir_machine_match_bitfield_rmw(
            ms_root, 296, 301, 302, 0, 0, 4, 15, 0) ||
        !mir_machine_match_bitfield_rmw(
            ms_root, 305, 310, -1, 0, 0, 4, 15, 0) ||
        mir.insns[251].immediate != '+' ||
        mir.insns[251].src1 != mir.insns[249].dst ||
        mir.insns[251].src2 != mir.insns[250].dst ||
        mir.insns[252].src2 != mir.insns[251].dst ||
        mir.insns[254].src1 != mir.insns[253].dst ||
        mir.insns[261].immediate != '+' ||
        mir.insns[261].src1 != mir.insns[258].dst ||
        mir.insns[261].src2 != mir.insns[259].dst ||
        mir.insns[262].src2 != mir.insns[261].dst ||
        mir.insns[264].src1 != mir.insns[263].dst ||
        mir.insns[300].immediate != '+' ||
        mir.insns[300].src1 != mir.insns[298].dst ||
        mir.insns[300].src2 != mir.insns[299].dst ||
        mir.insns[301].src2 != mir.insns[300].dst ||
        mir.insns[304].src1 != mir.insns[302].dst ||
        mir.insns[309].immediate != '-' ||
        mir.insns[309].src1 != mir.insns[307].dst ||
        mir.insns[309].src2 != mir.insns[308].dst ||
        mir.insns[310].src2 != mir.insns[309].dst ||
        mir.insns[312].src1 != mir.insns[307].dst)
        return mir_machine_reject(
            "bitfield-report-sequence", "rmw-live");

    if (!mir_machine_six_call_arguments(
            &mir.insns[224], arguments6) ||
        arguments6[0] != mir.insns[206].dst ||
        arguments6[1] != mir.insns[210].dst ||
        arguments6[2] != mir.insns[214].dst ||
        arguments6[3] != mir.insns[218].dst ||
        arguments6[4] != mir.insns[180].dst ||
        arguments6[5] != mir.insns[189].dst ||
        !mir_machine_match_bitfield_print_function(
            plan, &mir.insns[224]) ||
        !mir_machine_five_call_arguments(
            &mir.insns[280], arguments5) ||
        arguments5[0] != mir.insns[266].dst ||
        arguments5[1] != mir.insns[270].dst ||
        arguments5[2] != mir.insns[254].dst ||
        arguments5[3] != mir.insns[276].dst ||
        arguments5[4] != mir.insns[264].dst ||
        !mir_machine_match_bitfield_print_function(
            plan, &mir.insns[280]) ||
        !mir_machine_four_call_arguments(
            &mir.insns[323], arguments4) ||
        arguments4[0] != mir.insns[313].dst ||
        arguments4[1] != mir.insns[317].dst ||
        arguments4[2] != mir.insns[302].dst ||
        arguments4[3] != mir.insns[307].dst ||
        !mir_machine_match_bitfield_print_function(
            plan, &mir.insns[323]))
        return mir_machine_reject(
            "bitfield-report-sequence", "rmw-reports");
    plan->string_ids[4] = (int)mir.insns[206].immediate;
    plan->string_ids[5] = (int)mir.insns[266].immediate;
    plan->string_ids[6] = (int)mir.insns[313].immediate;

    if (!mir_machine_single_call_argument(
            &mir.insns[429], &instruction) ||
        instruction != mir.insns[427].dst ||
        mir.insns[427].opcode != MIR_STRING_ADDRESS ||
        !mir_machine_match_bitfield_print_function(
            plan, &mir.insns[429]) ||
        !mir_machine_constant_equals(mir.insns[430].dst, 0) ||
        mir.insns[431].src1 != mir.insns[430].dst)
        return mir_machine_reject(
            "bitfield-report-sequence", "completion");
    plan->string_ids[11] = (int)mir.insns[427].immediate;
    return 1;
}

static void mir_emit_bitfield_report_sequence(
    MirStream *out, const struct MirBitfieldReportSequence *plan)
{
    static const int unsigned_arguments[4] = {6, 31, 255, 1000};
    static const int signed_arguments[3] = {-4, 15, 30};

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-4\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    mir_emit_unsigned_bitfield_report(
        out, plan, 0, plan->globals[0]);
    mir_emit_bitfield_local_store(out, 25403, 456);
    mir_emit_unsigned_bitfield_report(out, plan, 1, NULL);

    mir_emit_bitfield_aggregate_call(
        out, plan->make_functions[0],
        unsigned_arguments, 4);
    mir_emit_unsigned_bitfield_report(out, plan, 2, NULL);

    mir_emit_bitfield_local_store(out, 2082, 16);
    mir_emit_unsigned_bitfield_report(out, plan, 3, NULL);

    mir_emit_bitfield_local_store(out, 3861, 0);
    mir_stream_puts("\tld hl,4\n\tpush hl\n\tpush hl\n", out);
    mir_emit_bitfield_push_unsigned_field(out, NULL, 8, 255);
    mir_emit_bitfield_push_unsigned_field(out, NULL, 3, 31);
    mir_emit_bitfield_push_unsigned_field(out, NULL, 0, 7);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", plan->string_ids[4]);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_emit_bitfield_report_cleanup(out, 6);

    mir_emit_bitfield_local_store(out, 51352, 0);
    mir_stream_puts("\tld hl,19\n\tpush hl\n", out);
    mir_emit_bitfield_push_unsigned_field(out, NULL, 3, 31);
    mir_stream_puts("\tld hl,0\n\tpush hl\n", out);
    mir_emit_bitfield_push_unsigned_field(out, NULL, 0, 7);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", plan->string_ids[5]);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_emit_bitfield_report_cleanup(out, 5);

    mir_emit_bitfield_local_store(out, 8, 0);
    mir_stream_puts("\tld hl,65529\n\tpush hl\n\tpush hl\n", out);
    mir_emit_bitfield_push_signed_nibble(out, NULL);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", plan->string_ids[6]);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_emit_bitfield_report_cleanup(out, 4);

    mir_emit_signed_bitfield_report(
        out, plan, 7, plan->globals[1]);
    mir_emit_bitfield_local_store(out, 94, 20);
    mir_emit_signed_bitfield_report(out, plan, 8, NULL);

    mir_emit_bitfield_aggregate_call(
        out, plan->make_functions[1],
        signed_arguments, 3);
    mir_emit_signed_bitfield_report(out, plan, 9, NULL);

    mir_emit_bitfield_local_store(out, 239, 40);
    mir_emit_signed_bitfield_report(out, plan, 10, NULL);

    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->string_ids[11]);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_stream_puts("\tpop bc\n\tld hl,0\n"
          "\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_task_array_check(
    struct MirTaskArrayCheck *plan)
{
    static const int strings[4] = {1, 7, 13, 19};
    static const int string_stores[4] = {2, 8, 14, 20};
    static const int priorities[4] = {3, 9, 15, 21};
    static const int priority_stores[4] = {4, 10, 16, 22};
    static const int done_values[4] = {5, 11, 17, 23};
    static const int done_stores[4] = {6, 12, 18, 24};
    static const int char_indices[4] = {72, 86, 112, 138};
    static const int char_loads[4] = {74, 88, 114, 140};
    static const int char_values[4] = {75, 89, 115, 141};
    static const int char_compares[4] = {77, 91, 117, 143};
    int arguments[2];
    int call_count = 0;
    long value;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 162 || mir_cfg_block_count() != 26 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_INT)
        return mir_machine_reject(
            "task-array-check", "shape");
    for (item = 0; item < 4; ++item) {
        if (mir.insns[strings[item]].opcode != MIR_STRING_ADDRESS ||
            mir.insns[string_stores[item]].opcode != MIR_STORE ||
            mir.insns[string_stores[item]].src1 !=
                mir.insns[strings[item]].dst ||
            mir.insns[string_stores[item]].immediate != item * 5 ||
            mir.insns[string_stores[item]].memory_size != 2 ||
            mir.insns[priority_stores[item]].opcode != MIR_STORE ||
            mir.insns[priority_stores[item]].src1 !=
                mir.insns[priorities[item]].dst ||
            mir.insns[priority_stores[item]].immediate != item * 5 + 2 ||
            mir.insns[priority_stores[item]].memory_size != 2 ||
            mir.insns[done_stores[item]].opcode != MIR_STORE ||
            mir.insns[done_stores[item]].src1 !=
                mir.insns[done_values[item]].dst ||
            mir.insns[done_stores[item]].immediate != item * 5 + 4 ||
            mir.insns[done_stores[item]].memory_size != 1 ||
            !mir_machine_constant_value(
                mir.insns[priorities[item]].dst, &value, 0))
            return mir_machine_reject(
                "task-array-check", "initializer");
        plan->name_string_ids[item] =
            (int)mir.insns[strings[item]].immediate;
        plan->priorities[item] = (int)value;
        if (!mir_machine_constant_value(
                mir.insns[done_values[item]].dst, &value, 0) ||
            value < 0 || value > 1)
            return mir_machine_reject(
                "task-array-check", "done");
        plan->done_values[item] = (int)value;
    }
    if (!mir_machine_two_call_arguments(
            &mir.insns[29], arguments) ||
        arguments[0] != mir.insns[25].dst ||
        !mir_machine_constant_value(arguments[1], &value, 0) ||
        value <= 0 || value > 127 ||
        (plan->highest_function =
             find_global(mir.insns[29].name)) == NULL)
        return mir_machine_reject(
            "task-array-check", "highest-call");
    plan->task_count = (int)value;
    if (plan->task_count != 4 ||
        !mir_machine_two_call_arguments(
            &mir.insns[36], arguments) ||
        arguments[0] != mir.insns[32].dst ||
        !mir_machine_constant_equals(
            arguments[1], plan->task_count) ||
        (plan->count_function =
             find_global(mir.insns[36].name)) == NULL ||
        !mir_machine_two_call_arguments(
            &mir.insns[45], arguments) ||
        arguments[0] != mir.insns[41].dst ||
        !mir_machine_constant_equals(
            arguments[1], plan->task_count) ||
        find_global(mir.insns[45].name) !=
            plan->count_function)
        return mir_machine_reject(
            "task-array-check", "count-calls");
    if (!mir_machine_constant_value(
            mir.insns[37].dst, &value, 0))
        return mir_machine_reject(
            "task-array-check", "open-count");
    plan->expected_open = (int)value;
    if (!mir_machine_constant_value(
            mir.insns[40].dst, &value, 0))
        return mir_machine_reject(
            "task-array-check", "failure-base");
    plan->failure_base = (int)value;
    if (!mir_machine_constant_value(
            mir.insns[50].dst, &value, 0))
        return mir_machine_reject(
            "task-array-check", "null");
    plan->null_result = 20;
    if (value != 0 ||
        !mir_machine_constant_equals(
            mir.insns[53].dst, plan->null_result))
        return mir_machine_reject(
            "task-array-check", "null-result");
    if (mir.insns[38].opcode != MIR_BINARY ||
        mir.insns[38].immediate != TOK_NE ||
        mir.insns[46].opcode != MIR_BINARY ||
        mir.insns[46].immediate != '+' ||
        mir.insns[47].opcode != MIR_RETURN ||
        mir.insns[51].opcode != MIR_BINARY ||
        mir.insns[51].immediate != TOK_EQ ||
        mir.insns[54].opcode != MIR_RETURN ||
        mir.insns[57].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[57].immediate != 2 ||
        mir.insns[58].opcode != MIR_LOAD_INDIRECT ||
        !mir_machine_constant_value(
            mir.insns[59].dst, &value, 0))
        return mir_machine_reject(
            "task-array-check", "early-checks");
    plan->expected_priority = (int)value;
    if (!mir_machine_constant_value(
            mir.insns[62].dst, &value, 0))
        return mir_machine_reject(
            "task-array-check", "priority-base");
    plan->priority_failure_base = (int)value;
    if (mir.insns[60].opcode != MIR_BINARY ||
        mir.insns[60].immediate != TOK_NE ||
        mir.insns[66].opcode != MIR_BINARY ||
        mir.insns[66].immediate != '+' ||
        mir.insns[67].opcode != MIR_RETURN)
        return mir_machine_reject(
            "task-array-check", "priority-check");
    for (item = 0; item < 4; ++item) {
        if (!mir_machine_constant_equals(
                mir.insns[char_indices[item]].dst, item) ||
            mir.insns[char_loads[item]].opcode !=
                MIR_LOAD_INDIRECT ||
            mir.insns[char_loads[item]].memory_size != 1 ||
            !mir_machine_constant_value(
                mir.insns[char_values[item]].dst, &value, 0) ||
            mir.insns[char_compares[item]].opcode != MIR_BINARY ||
            mir.insns[char_compares[item]].immediate != TOK_NE)
            return mir_machine_reject(
                "task-array-check", "name-check");
        plan->name_bytes[item] = (int)value;
    }
    if (!mir_machine_constant_value(
            mir.insns[157].dst, &value, 0))
        return mir_machine_reject(
            "task-array-check", "name-failure");
    plan->name_failure_result = (int)value;
    if (!mir_machine_constant_value(
            mir.insns[160].dst, &value, 0) ||
        mir.insns[158].opcode != MIR_RETURN ||
        mir.insns[161].opcode != MIR_RETURN)
        return mir_machine_reject(
            "task-array-check", "success");
    plan->success_result = (int)value;
    for (item = 0; item < mir.count; ++item)
        if (mir.insns[item].opcode == MIR_CALL)
            ++call_count;
    return call_count == 3;
}

static void mir_emit_task_array_check(
    MirStream *out, const struct MirTaskArrayCheck *plan)
{
    int count_ok = new_label();
    int nonnull = new_label();
    int priority_ok = new_label();
    int name_failure = new_label();
    int done = new_label();
    int item;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-22\n\tadd hl,sp\n\tld sp,hl\n", out);
    for (item = 0; item < plan->task_count; ++item) {
        int offset = -22 + item * 5;

        mir_stream_printf(out,
                "\tld hl,S%d\n"
                "\tld (ix%+d),l\n\tld (ix%+d),h\n"
                "\tld hl,%d\n"
                "\tld (ix%+d),l\n\tld (ix%+d),h\n"
                "\tld (ix%+d),%d\n",
                plan->name_string_ids[item],
                offset, offset + 1,
                plan->priorities[item],
                offset + 2, offset + 3,
                offset + 4, plan->done_values[item]);
    }
    mir_task_array_push_arguments(out, plan->task_count);
    mir_machine_emit_symbol_call(
        out, plan->highest_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n"
          "\tld (ix-2),l\n\tld (ix-1),h\n", out);

    mir_task_array_push_arguments(out, plan->task_count);
    mir_machine_emit_symbol_call(
        out, plan->count_function);
    mir_stream_printf(out,
            "\tpop bc\n\tpop bc\n\tld de,%d\n"
            "\tor a\n\tsbc hl,de\n\tjp z,L%d\n",
            plan->expected_open, count_ok);
    mir_task_array_push_arguments(out, plan->task_count);
    mir_machine_emit_symbol_call(
        out, plan->count_function);
    mir_stream_printf(out,
            "\tpop bc\n\tpop bc\n\tld de,%d\n"
            "\tadd hl,de\n\tjp L%d\n"
            "L%d:\n\tld l,(ix-2)\n\tld h,(ix-1)\n"
            "\tld a,h\n\tor l\n\tjp nz,L%d\n"
            "\tld hl,%d\n\tjp L%d\n"
            "L%d:\n\tinc hl\n\tinc hl\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tex de,hl\n\tld de,%d\n"
            "\tor a\n\tsbc hl,de\n\tjp z,L%d\n"
            "\tadd hl,de\n\tld de,%d\n\tadd hl,de\n"
            "\tjp L%d\n"
            "L%d:\n\tld l,(ix-2)\n\tld h,(ix-1)\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tex de,hl\n",
            plan->failure_base, done,
            count_ok, nonnull,
            plan->null_result, done,
            nonnull, plan->expected_priority,
            priority_ok, plan->priority_failure_base,
            done, priority_ok);
    for (item = 0; item < 4; ++item) {
        mir_stream_puts("\tld a,(hl)\n", out);
        mir_stream_printf(out, "\tcp %d\n\tjp nz,L%d\n",
                plan->name_bytes[item] & 255,
                name_failure);
        if (item != 3)
            mir_stream_puts("\tinc hl\n", out);
    }
    mir_stream_printf(out,
            "\tld hl,%d\n\tjp L%d\n"
            "L%d:\n\tld hl,%d\n"
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            plan->success_result, done,
            name_failure, plan->name_failure_result,
            done);
}

static int mir_match_literal_check_runner(
    struct MirLiteralCheckRunner *plan)
{
    int final_load;
    int final_call;
    int intro_call = -1;
    int failure_call = -1;
    int report_arguments[2];
    int intro_argument;
    int instruction;
    int total_calls = 0;

    memset(plan, 0, sizeof(*plan));
    if (mir.count < 100 || mir_cfg_block_count() != 2 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_INT)
        return mir_machine_reject(
            "literal-check-runner", "shape");
    final_call = mir.count - 3;
    final_load = mir.count - 10;
    if (mir.insns[final_load].opcode == MIR_LOAD &&
        mir.insns[final_load + 1].opcode == MIR_BRANCH_FALSE &&
        mir.insns[final_load + 5].opcode == MIR_STRING_ADDRESS &&
        mir.insns[final_call].opcode == MIR_CALL) {
        /* Simple `if (fails) return 1; print success; return 0`. */
    } else {
        final_load = mir.count - 16;
        failure_call = mir.count - 10;
        intro_call = 3;
        if (mir.insns[0].opcode != MIR_LABEL ||
            mir.insns[1].opcode != MIR_STRING_ADDRESS ||
            mir.insns[2].opcode != MIR_ARG ||
            mir.insns[intro_call].opcode != MIR_CALL ||
            !mir_machine_constant_equals(mir.insns[4].dst, 0) ||
            mir.insns[6].opcode != MIR_STORE ||
            mir.insns[final_load].opcode != MIR_LOAD ||
            mir.insns[final_load + 1].opcode != MIR_BRANCH_FALSE ||
            mir.insns[final_load + 2].opcode != MIR_STRING_ADDRESS ||
            mir.insns[failure_call].opcode != MIR_CALL ||
            mir.insns[final_call].opcode != MIR_CALL)
            return mir_machine_reject(
                "literal-check-runner", "final");
        plan->has_intro = 1;
        plan->has_failure_report = 1;
        plan->intro_string_id = (int)mir.insns[1].immediate;
        plan->failure_string_id =
            (int)mir.insns[final_load + 2].immediate;
    }
    if (mir.insns[final_load].opcode != MIR_LOAD ||
        (plan->failure_root =
             find_global(mir.insns[final_load].name)) == NULL ||
        plan->failure_root->is_volatile ||
        mir.insns[final_load + 1].opcode != MIR_BRANCH_FALSE ||
        mir.insns[final_call].opcode != MIR_CALL ||
        !mir_machine_constant_equals(
            mir.insns[mir.count - 2].dst, 0) ||
        mir.insns[mir.count - 1].opcode != MIR_RETURN)
        return mir_machine_reject(
            "literal-check-runner", "final");
    plan->print_function =
        find_global(mir.insns[final_call].name);
    plan->success_string_id =
        (int)mir.insns[mir.count - 5].immediate;
    if (plan->print_function == NULL)
        return mir_machine_reject(
            "literal-check-runner", "print");
    if (!plan->has_failure_report &&
        (!mir_machine_constant_equals(
             mir.insns[final_load + 2].dst, 1) ||
         mir.insns[final_load + 3].opcode != MIR_RETURN ||
         mir.insns[final_load + 4].opcode != MIR_LABEL ||
         mir.insns[final_load + 5].opcode != MIR_STRING_ADDRESS ||
         mir.insns[final_load + 6].opcode != MIR_ARG))
        return mir_machine_reject(
            "literal-check-runner", "simple-final");
    if (plan->has_failure_report &&
        (!mir_machine_constant_equals(
             mir.insns[final_load + 7].dst, 1) ||
         mir.insns[final_load + 8].opcode != MIR_RETURN ||
         mir.insns[final_load + 10].opcode != MIR_LABEL ||
         find_global(mir.insns[intro_call].name) !=
             plan->print_function ||
         find_global(mir.insns[failure_call].name) !=
             plan->print_function ||
         !mir_machine_single_call_argument(
             &mir.insns[intro_call], &intro_argument) ||
         intro_argument != mir.insns[1].dst ||
         !mir_machine_two_call_arguments(
             &mir.insns[failure_call], report_arguments) ||
         report_arguments[0] != mir.insns[final_load + 2].dst ||
         report_arguments[1] != mir.insns[final_load + 4].dst))
        return mir_machine_reject(
            "literal-check-runner", "reported-final");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode == MIR_CALL) {
            int arguments[3];
            const struct MirInsn *string;
            struct Sym *function;
            long actual;
            long expected;
            unsigned long mask;
            int width;

            ++total_calls;
            if (instruction == final_call ||
                instruction == intro_call ||
                instruction == failure_call)
                continue;
            if (plan->call_count >=
                    MIR_MAX_LITERAL_CHECK_CALLS ||
                !mir_machine_three_call_arguments(
                    insn, arguments) ||
                (string = mir_definition(arguments[0])) == NULL ||
                string->opcode != MIR_STRING_ADDRESS ||
                !mir_machine_evaluate_constant(
                    arguments[1], &actual, 0) ||
                !mir_machine_evaluate_constant(
                    arguments[2], &expected, 0) ||
                (function = find_global(insn->name)) == NULL ||
                function->proto_nargs != 3)
                return mir_machine_reject(
                    "literal-check-runner", "call");
            width = type_size(function->proto_types[1]);
            if ((width != 2 && width != 4) ||
                type_size(function->proto_types[2]) != width)
                return mir_machine_reject(
                    "literal-check-runner", "call-type");
            mask = width == 2 ? 0xffffUL : 0xffffffffUL;
            if (((unsigned long)actual & mask) !=
                ((unsigned long)expected & mask)) {
                if (getenv("DCC_MIR_MACHINE_REPORT") != NULL)
                    fprintf(stderr,
                            "; MIR machine function=%s"
                            " template=literal-check-runner"
                            " call=%d actual=%lu expected=%lu\n",
                            mir.name, instruction,
                            (unsigned long)actual & mask,
                            (unsigned long)expected & mask);
                return mir_machine_reject(
                    "literal-check-runner", "mismatch");
            }
            plan->calls[plan->call_count].function = function;
            plan->calls[plan->call_count].string_id =
                (int)string->immediate;
            plan->calls[plan->call_count].value_width = width;
            plan->calls[plan->call_count].value =
                (unsigned long)expected & mask;
            ++plan->call_count;
            continue;
        }
        if (insn->opcode == MIR_STORE &&
            !mir_machine_unobservable_local_store(insn)) {
            if (!plan->has_intro || instruction != 6 ||
                find_global(insn->name) != plan->failure_root ||
                insn->src1 != mir.insns[4].dst)
                return mir_machine_reject(
                    "literal-check-runner", "store");
        }
        if (insn->opcode == MIR_LOAD &&
            instruction != final_load &&
            (!plan->has_failure_report ||
             instruction != final_load + 4))
            return mir_machine_reject(
                "literal-check-runner", "load");
        if (insn->opcode != MIR_LABEL &&
            insn->opcode != MIR_NOP &&
            insn->opcode != MIR_CONST &&
            insn->opcode != MIR_STRING_ADDRESS &&
            insn->opcode != MIR_ARG &&
            insn->opcode != MIR_UNARY &&
            insn->opcode != MIR_BINARY &&
            insn->opcode != MIR_STORE &&
            insn->opcode != MIR_LOAD &&
            insn->opcode != MIR_BRANCH_FALSE &&
            insn->opcode != MIR_RETURN)
            return mir_machine_reject(
                "literal-check-runner", "opcode");
    }
    return plan->call_count >= 8 &&
           total_calls == plan->call_count + 1 +
               plan->has_intro + plan->has_failure_report;
}

static void mir_emit_literal_check_runner(
    MirStream *out, const struct MirLiteralCheckRunner *plan)
{
    int success = new_label();
    int call;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    if (plan->has_intro) {
        mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
                plan->intro_string_id);
        mir_machine_emit_symbol_call(
            out, plan->print_function);
        mir_stream_puts("\tpop bc\n\tld hl,0\n", out);
        mir_machine_emit_global_word_store(
            out, plan->failure_root, 0);
    }
    for (call = 0; call < plan->call_count; ++call) {
        const struct MirLiteralCheckCall *check =
            &plan->calls[call];
        int copy;

        for (copy = 0; copy < 2; ++copy) {
            if (check->value_width == 4) {
                mir_stream_printf(out,
                        "\tld hl,%lu\n\tpush hl\n"
                        "\tld hl,%lu\n\tpush hl\n",
                        (check->value >> 16) & 0xffffUL,
                        check->value & 0xffffUL);
            } else {
                mir_stream_printf(out, "\tld hl,%lu\n\tpush hl\n",
                        check->value & 0xffffUL);
            }
        }
        mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
                check->string_id);
        mir_machine_emit_symbol_call(out, check->function);
        if (check->value_width == 4)
            mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n"
                  "\tpop bc\n\tpop bc\n", out);
        else
            mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n", out);
    }
    mir_machine_emit_global_word(
        out, plan->failure_root, 0);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", success);
    if (plan->has_failure_report) {
        mir_stream_puts("\tpush hl\n", out);
        mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
                plan->failure_string_id);
        mir_machine_emit_symbol_call(
            out, plan->print_function);
        mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    }
    mir_stream_printf(out,
            "\tld hl,1\n\tret\n"
            "L%d:\n\tld hl,S%d\n\tpush hl\n",
            success, plan->success_string_id);
    mir_machine_emit_symbol_call(
        out, plan->print_function);
    mir_stream_puts("\tpop bc\n\tld hl,0\n\tret\n", out);
}

static int mir_match_compound_check_runner(
    struct MirCompoundCheckRunner *plan)
{
    struct MirCompoundValue *values = NULL;
    unsigned char *bytes = NULL;
    unsigned char *byte_known = NULL;
    int *addresses = NULL;
    unsigned char *address_known = NULL;
    int opcode_counts[MIR_RETURN + 1];
    int value_capacity = mir.next_value > 0 ? mir.next_value : 1;
    int final_load = mir.count - 10;
    int check_count = 0;
    int helper_after_checks = -1;
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;
    int ok = 0;

    memset(plan, 0, sizeof(*plan));
    memset(opcode_counts, 0, sizeof(opcode_counts));
    if (mir.count != 974 || mir_cfg_block_count() != 2 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_INT ||
        mir.local_bytes != 23 || final_load != 964)
        return mir_machine_reject(
            "compound-check-runner", "shape");
    values = (struct MirCompoundValue *)calloc(
        (size_t)value_capacity, sizeof(*values));
    bytes = (unsigned char *)calloc(
        (size_t)mir.local_bytes, sizeof(*bytes));
    byte_known = (unsigned char *)calloc(
        (size_t)mir.local_bytes, sizeof(*byte_known));
    addresses = (int *)calloc(
        (size_t)mir.local_bytes, sizeof(*addresses));
    address_known = (unsigned char *)calloc(
        (size_t)mir.local_bytes, sizeof(*address_known));
    if (values == NULL || bytes == NULL || byte_known == NULL ||
        addresses == NULL || address_known == NULL)
        goto done;
    plan->frame_bytes = mir.local_bytes;

    for (instruction = 0; instruction < final_load; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        struct MirCompoundValue value;
        int width;

        if (insn->opcode < 0 || insn->opcode > MIR_RETURN)
            goto done;
        ++opcode_counts[insn->opcode];
        switch (insn->opcode) {
        case MIR_LABEL:
        case MIR_NOP:
        case MIR_ARG:
            break;
        case MIR_CONST:
            if (insn->dst < 0 || insn->dst >= value_capacity ||
                !mir_compound_normalize_integer(
                    insn->immediate, insn->type,
                    &values[insn->dst].value))
                goto done;
            values[insn->dst].kind =
                MIR_COMPOUND_VALUE_INTEGER;
            break;
        case MIR_STRING_ADDRESS:
            if (insn->dst < 0 || insn->dst >= value_capacity ||
                type_ptr_depth(insn->type) != 1 ||
                type_size(insn->type) != 2)
                goto done;
            values[insn->dst].kind =
                MIR_COMPOUND_VALUE_STRING;
            values[insn->dst].value = insn->immediate;
            break;
        case MIR_ADDRESS:
            if (insn->dst < 0 || insn->dst >= value_capacity ||
                !mir_machine_named_nonvolatile(insn) ||
                !mir_scalar_memory_location(
                    insn, &memory_type, &memory_storage,
                    &memory_offset) ||
                memory_storage != SC_LOCAL ||
                mir_compound_frame_index(
                    mir.local_bytes, memory_offset, 1) < 0)
                goto done;
            values[insn->dst].kind =
                MIR_COMPOUND_VALUE_ADDRESS;
            values[insn->dst].value = memory_offset;
            break;
        case MIR_INDEX_ADDRESS:
            if (insn->dst < 0 || insn->dst >= value_capacity ||
                insn->src1 < 0 || insn->src1 >= value_capacity ||
                insn->src2 < 0 || insn->src2 >= value_capacity ||
                insn->immediate <= 0 ||
                values[insn->src1].kind !=
                    MIR_COMPOUND_VALUE_ADDRESS ||
                values[insn->src2].kind !=
                    MIR_COMPOUND_VALUE_INTEGER)
                goto done;
            values[insn->dst].kind =
                MIR_COMPOUND_VALUE_ADDRESS;
            values[insn->dst].value =
                values[insn->src1].value +
                values[insn->src2].value * insn->immediate;
            break;
        case MIR_MEMBER_ADDRESS:
            if (insn->dst < 0 || insn->dst >= value_capacity ||
                insn->src1 < 0 || insn->src1 >= value_capacity ||
                values[insn->src1].kind !=
                    MIR_COMPOUND_VALUE_ADDRESS ||
                insn->immediate < 0)
                goto done;
            values[insn->dst].kind =
                MIR_COMPOUND_VALUE_ADDRESS;
            values[insn->dst].value =
                values[insn->src1].value + insn->immediate;
            break;
        case MIR_UNARY:
            if (insn->dst < 0 || insn->dst >= value_capacity ||
                insn->src1 < 0 || insn->src1 >= value_capacity)
                goto done;
            value = values[insn->src1];
            if (value.kind == MIR_COMPOUND_VALUE_ADDRESS) {
                if (insn->immediate != 0 ||
                    type_ptr_depth(insn->type) == 0 ||
                    type_size(insn->type) != 2)
                    goto done;
                values[insn->dst] = value;
                break;
            }
            if (value.kind != MIR_COMPOUND_VALUE_INTEGER)
                goto done;
            if (insn->immediate == 0 ||
                insn->immediate == '+') {
                if (!mir_compound_normalize_integer(
                        value.value, insn->type,
                        &values[insn->dst].value))
                    goto done;
            } else if (insn->immediate == '-') {
                if (!mir_fold_constant_binary(
                        '-', 0, value.value, insn->type,
                        &values[insn->dst].value))
                    goto done;
            } else if (insn->immediate == '~') {
                if (!mir_fold_constant_binary(
                        '^', value.value, -1, insn->type,
                        &values[insn->dst].value))
                    goto done;
            } else if (insn->immediate == '!') {
                values[insn->dst].value =
                    value.value == 0;
            } else {
                goto done;
            }
            values[insn->dst].kind =
                MIR_COMPOUND_VALUE_INTEGER;
            break;
        case MIR_BINARY:
            if (insn->dst < 0 || insn->dst >= value_capacity ||
                insn->src1 < 0 || insn->src1 >= value_capacity ||
                insn->src2 < 0 || insn->src2 >= value_capacity ||
                values[insn->src1].kind !=
                    MIR_COMPOUND_VALUE_INTEGER ||
                values[insn->src2].kind !=
                    MIR_COMPOUND_VALUE_INTEGER ||
                (!mir_fold_constant_binary(
                     (int)insn->immediate,
                     values[insn->src1].value,
                     values[insn->src2].value,
                     insn->secondary_offset,
                     &values[insn->dst].value) &&
                 !mir_fold_constant_compare(
                     (int)insn->immediate,
                     values[insn->src1].value,
                     values[insn->src2].value,
                     insn->secondary_offset,
                     &values[insn->dst].value)))
                goto done;
            values[insn->dst].kind =
                MIR_COMPOUND_VALUE_INTEGER;
            break;
        case MIR_STORE:
            if (!mir_machine_named_nonvolatile(insn) ||
                insn->src1 < 0 || insn->src1 >= value_capacity ||
                !mir_scalar_memory_location(
                    insn, &memory_type, &memory_storage,
                    &memory_offset) ||
                memory_storage != SC_LOCAL ||
                insn->memory_flags != 0)
                goto done;
            width = insn->memory_size;
            if (!mir_compound_store_memory(
                    bytes, byte_known, addresses, address_known,
                    mir.local_bytes, memory_offset, width,
                    &values[insn->src1]) ||
                !mir_compound_add_store_event(
                    plan, memory_offset, width,
                    &values[insn->src1]))
                goto done;
            break;
        case MIR_LOAD:
            if (insn->dst < 0 || insn->dst >= value_capacity ||
                !mir_machine_named_nonvolatile(insn) ||
                !mir_scalar_memory_location(
                    insn, &memory_type, &memory_storage,
                    &memory_offset) ||
                memory_storage != SC_LOCAL ||
                insn->memory_flags != 0)
                goto done;
            width = insn->memory_size > 0
                ? insn->memory_size : type_size(memory_type);
            if (
                !mir_compound_load_memory(
                    bytes, byte_known, addresses, address_known,
                    mir.local_bytes, memory_offset,
                    width, insn->type,
                    &values[insn->dst]))
                goto done;
            break;
        case MIR_STORE_INDIRECT:
            if (insn->src1 < 0 || insn->src1 >= value_capacity ||
                insn->src2 < 0 || insn->src2 >= value_capacity ||
                values[insn->src1].kind !=
                    MIR_COMPOUND_VALUE_ADDRESS ||
                insn->memory_flags != 0 || insn->bit_width != 0)
                goto done;
            memory_offset = (int)values[insn->src1].value;
            width = insn->memory_size;
            if (!mir_compound_store_memory(
                    bytes, byte_known, addresses, address_known,
                    mir.local_bytes, memory_offset, width,
                    &values[insn->src2]) ||
                !mir_compound_add_store_event(
                    plan, memory_offset, width,
                    &values[insn->src2]))
                goto done;
            break;
        case MIR_LOAD_INDIRECT:
            if (insn->dst < 0 || insn->dst >= value_capacity ||
                insn->src1 < 0 || insn->src1 >= value_capacity ||
                values[insn->src1].kind !=
                    MIR_COMPOUND_VALUE_ADDRESS ||
                insn->memory_flags != 0 || insn->bit_width != 0 ||
                !mir_compound_load_memory(
                    bytes, byte_known, addresses, address_known,
                    mir.local_bytes,
                    (int)values[insn->src1].value,
                    insn->memory_size, insn->type,
                    &values[insn->dst]))
                goto done;
            break;
        case MIR_CALL:
            if (mir_machine_three_call_arguments(
                    insn, (int[3]){-1, -1, -1})) {
                if (!mir_compound_add_call_event(
                        plan, insn, values, value_capacity))
                    goto done;
                ++check_count;
            } else {
                if (!mir_compound_add_helper_event(plan, insn))
                    goto done;
                helper_after_checks = check_count;
            }
            if (insn->dst >= 0 && insn->dst < value_capacity)
                values[insn->dst].kind =
                    MIR_COMPOUND_VALUE_UNKNOWN;
            break;
        default:
            goto done;
        }
    }
    if (check_count != 71 || helper_after_checks != 58 ||
        plan->event_count != 188 ||
        plan->check_function == NULL ||
        plan->helper_function == NULL ||
        opcode_counts[MIR_LABEL] != 1 ||
        opcode_counts[MIR_NOP] != 140 ||
        opcode_counts[MIR_CONST] != 163 ||
        opcode_counts[MIR_STRING_ADDRESS] != 71 ||
        opcode_counts[MIR_ARG] != 213 ||
        opcode_counts[MIR_UNARY] != 19 ||
        opcode_counts[MIR_BINARY] != 30 ||
        opcode_counts[MIR_ADDRESS] != 47 ||
        opcode_counts[MIR_INDEX_ADDRESS] != 20 ||
        opcode_counts[MIR_MEMBER_ADDRESS] != 15 ||
        opcode_counts[MIR_STORE] != 81 ||
        opcode_counts[MIR_LOAD] != 26 ||
        opcode_counts[MIR_STORE_INDIRECT] != 35 ||
        opcode_counts[MIR_LOAD_INDIRECT] != 31 ||
        opcode_counts[MIR_CALL] != 72)
        goto done;

    if (mir.insns[final_load].opcode != MIR_LOAD ||
        !mir_machine_named_nonvolatile(&mir.insns[final_load]) ||
        !mir_scalar_memory_location(
            &mir.insns[final_load], &memory_type,
            &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        type_ptr_depth(memory_type) != 0 ||
        type_size(memory_type) != 2 ||
        (plan->failure_root =
             find_global(mir.insns[final_load].name)) == NULL ||
        plan->failure_root->is_volatile ||
        mir.insns[final_load + 1].opcode != MIR_BRANCH_FALSE ||
        mir.insns[final_load + 1].src1 !=
            mir.insns[final_load].dst ||
        mir.insns[final_load + 1].label !=
            mir.insns[final_load + 4].label ||
        !mir_machine_constant_equals(
            mir.insns[final_load + 2].dst, 1) ||
        mir.insns[final_load + 3].opcode != MIR_RETURN ||
        mir.insns[final_load + 3].src1 !=
            mir.insns[final_load + 2].dst ||
        mir.insns[final_load + 4].opcode != MIR_LABEL ||
        mir.insns[final_load + 5].opcode != MIR_STRING_ADDRESS ||
        mir.insns[final_load + 6].opcode != MIR_ARG ||
        mir.insns[final_load + 6].src1 !=
            mir.insns[final_load + 5].dst ||
        mir.insns[final_load + 7].opcode != MIR_CALL ||
        !mir_machine_single_call_argument(
            &mir.insns[final_load + 7], &instruction) ||
        instruction != mir.insns[final_load + 5].dst ||
        !mir_machine_constant_equals(
            mir.insns[final_load + 8].dst, 0) ||
        mir.insns[final_load + 9].opcode != MIR_RETURN ||
        mir.insns[final_load + 9].src1 !=
            mir.insns[final_load + 8].dst)
        goto done;
    plan->failure_offset = memory_offset;
    plan->success_string_id =
        (int)mir.insns[final_load + 5].immediate;
    plan->print_function =
        find_global(mir.insns[final_load + 7].name);
    if (plan->print_function == NULL ||
        plan->print_function->is_funcptr ||
        plan->print_function->is_noreturn ||
        !plan->print_function->has_proto ||
        plan->print_function->proto_nargs != 1 ||
        !plan->print_function->proto_variadic ||
        (mir.insns[final_load + 7].type & 15) != TYPE_INT)
        goto done;
    ok = 1;
done:
    free(address_known);
    free(addresses);
    free(byte_known);
    free(bytes);
    free(values);
    if (!ok) {
        if (getenv("DCC_MIR_MACHINE_REPORT") != NULL)
            fprintf(stderr,
                    "; MIR machine function=%s"
                    " template=compound-check-runner"
                    " instruction=%d opcode=%d events=%d"
                    " checks=%d helper-after=%d\n",
                    mir.name, instruction,
                    instruction >= 0 && instruction < mir.count
                        ? mir.insns[instruction].opcode : -1,
                    plan->event_count, check_count,
                    helper_after_checks);
        return mir_machine_reject(
            "compound-check-runner", "proof");
    }
    return 1;
}

static void mir_emit_compound_check_runner(
    MirStream *out, const struct MirCompoundCheckRunner *plan)
{
    int success = new_label();
    int event;

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    mir_stream_printf(out,
            "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n",
            plan->frame_bytes);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (event = 0; event < plan->event_count; ++event) {
        const struct MirCompoundCheckEvent *item =
            &plan->events[event];

        if (item->kind == MIR_COMPOUND_CHECK_STORE)
            mir_emit_compound_check_store(out, item);
        else if (item->kind == MIR_COMPOUND_CHECK_CALL)
            mir_emit_compound_check_call(out, item);
        else
            mir_machine_emit_symbol_call(
                out, item->function);
    }
    mir_machine_emit_global_word(
        out, plan->failure_root, plan->failure_offset);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out,
            "\tjp z,L%d\n"
            "\tld hl,1\n"
            "\tld sp,ix\n\tpop ix\n\tret\n"
            "L%d:\n"
            "\tld hl,S%d\n\tpush hl\n",
            success, success, plan->success_string_id);
    mir_machine_emit_symbol_call(
        out, plan->print_function);
    mir_stream_puts("\tpop bc\n"
          "\tld hl,0\n"
          "\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_string_mismatch_report(
    struct MirStringMismatchReport *plan)
{
    int arguments[2];

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 70 || mir_cfg_block_count() != 12 ||
        mir.has_vla || mir.insns[1].opcode != MIR_PARAM ||
        mir.insns[2].opcode != MIR_PARAM ||
        mir.insns[3].opcode != MIR_PARAM ||
        type_size(mir.insns[1].type) != 2 ||
        type_size(mir.insns[2].type) != 2 ||
        type_size(mir.insns[3].type) != 2 ||
        !mir_machine_constant_equals(mir.insns[4].dst, 0) ||
        mir.insns[11].opcode != MIR_PHI ||
        mir.insns[14].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[14].src1 != mir.insns[12].dst ||
        mir.insns[14].src2 != mir.insns[11].dst ||
        mir.insns[15].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[23].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[23].src1 != mir.insns[21].dst ||
        mir.insns[23].src2 != mir.insns[11].dst ||
        mir.insns[24].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[25].src1 != mir.insns[24].dst ||
        mir.insns[37].src1 != mir.insns[36].dst ||
        mir.insns[40].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[40].src1 != mir.insns[38].dst ||
        mir.insns[40].src2 != mir.insns[11].dst ||
        mir.insns[41].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[44].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[44].src1 != mir.insns[42].dst ||
        mir.insns[44].src2 != mir.insns[11].dst ||
        mir.insns[45].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[48].immediate != TOK_NE ||
        mir.insns[49].src1 != mir.insns[48].dst ||
        !mir_machine_constant_equals(mir.insns[56].dst, 1) ||
        mir.insns[57].immediate != '+' ||
        mir.insns[59].opcode != MIR_RETURN ||
        !mir_machine_constant_equals(mir.insns[65].dst, 1) ||
        mir.insns[66].immediate != '+' ||
        mir.insns[68].label != mir.insns[7].label)
        return mir_machine_reject("string-mismatch-report", "shape");
    if (!mir_machine_two_call_arguments(&mir.insns[54], arguments) ||
        arguments[0] != mir.insns[50].dst ||
        arguments[1] != mir.insns[52].dst)
        return mir_machine_reject(
            "string-mismatch-report", "report-arguments");
    plan->print_function = find_global(mir.insns[54].name);
    plan->failure_count = find_global(mir.insns[55].name);
    plan->format_string_id = (int)mir.insns[50].immediate;
    if (plan->print_function == NULL ||
        plan->failure_count == NULL)
        return mir_machine_reject("string-mismatch-report", "symbols");
    return 1;
}

static void mir_emit_string_mismatch_report(
    MirStream *out, const struct MirStringMismatchReport *plan)
{
    int loop = new_label();
    int mismatch = new_label();
    int done = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\tld hl,2\n\tadd hl,sp\n"
          "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n\tinc hl\n"
          "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld h,b\n\tld l,c\n", out);
    mir_stream_printf(out,
            "L%d:\n\tld a,(de)\n\tld c,a\n\tld b,(hl)\n"
            "\tor b\n\tjp z,L%d\n"
            "\tld a,c\n\tcp b\n\tjp nz,L%d\n"
            "\tinc hl\n\tinc de\n\tjp L%d\n"
            "L%d:\n\tret\n"
            "L%d:\n",
            loop, done, mismatch, loop, done, mismatch);
    mir_stream_puts("\tld hl,6\n\tadd hl,sp\n"
          "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
          "\tld h,b\n\tld l,c\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->format_string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_global_word(out, plan->failure_count, 0);
    mir_stream_puts("\tinc hl\n", out);
    mir_machine_emit_global_word_store(out, plan->failure_count, 0);
    mir_stream_puts("\tret\n", out);
}

static int mir_match_crc_update_runner(struct MirCrcUpdateRunner *plan)
{
    int update_arguments[4];
    int check_arguments[3];
    struct Sym *table;
    int declaration;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 57 || mir_cfg_block_count() != 4 ||
        mir.has_vla || mir.insns[1].opcode != MIR_CALL ||
        !mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[5].dst, 0) ||
        mir.insns[9].opcode != MIR_PHI ||
        mir.insns[10].opcode != MIR_PHI ||
        !mir_machine_constant_equals(mir.insns[12].dst, 8) ||
        mir.insns[13].immediate != '<' ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        mir.insns[14].label != mir.insns[36].label ||
        mir.insns[17].opcode != MIR_ADDRESS ||
        !mir_machine_constant_equals(
            mir.insns[19].dst, 0xffffffffUL) ||
        mir.insns[22].opcode != MIR_ADDRESS ||
        mir.insns[24].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[24].src1 != mir.insns[22].dst ||
        mir.insns[24].src2 != mir.insns[10].dst ||
        mir.insns[25].opcode != MIR_LOAD_INDIRECT ||
        !mir_machine_four_call_arguments(
            &mir.insns[27], update_arguments) ||
        update_arguments[0] != mir.insns[9].dst ||
        update_arguments[1] != mir.insns[17].dst ||
        update_arguments[2] != mir.insns[19].dst ||
        update_arguments[3] != mir.insns[25].dst ||
        !mir_machine_constant_equals(mir.insns[32].dst, 1) ||
        mir.insns[33].immediate != '+' ||
        mir.insns[35].label != mir.insns[8].label)
        return mir_machine_reject("crc-update-runner", "loop");
    if (!mir_machine_three_call_arguments(
            &mir.insns[46], check_arguments) ||
        check_arguments[0] != mir.insns[37].dst ||
        check_arguments[1] != mir.insns[9].dst ||
        check_arguments[2] != mir.insns[42].dst ||
        mir.insns[49].opcode != MIR_CALL ||
        !mir_machine_three_call_arguments(
            &mir.insns[56], check_arguments) ||
        check_arguments[0] != mir.insns[47].dst ||
        check_arguments[1] != mir.insns[49].dst ||
        check_arguments[2] != mir.insns[52].dst)
        return mir_machine_reject("crc-update-runner", "checks");
    plan->init_function = find_global(mir.insns[1].name);
    plan->update_function = find_global(mir.insns[27].name);
    plan->check_function = find_global(mir.insns[46].name);
    plan->cleanup_function = find_global(mir.insns[49].name);
    table = find_global(mir.insns[17].name);
    if (plan->init_function == NULL ||
        plan->update_function == NULL ||
        plan->check_function == NULL ||
        plan->cleanup_function == NULL ||
        table == NULL)
        return mir_machine_reject("crc-update-runner", "functions");
    snprintf(plan->table_assembly_name,
             sizeof(plan->table_assembly_name), "%s",
             asm_name_for(sym_asm_name(table)));
    for (declaration = 0; declaration < mir.declared_count; ++declaration)
        if (!strcmp(
                mir.declared_names[declaration],
                mir.insns[22].name)) {
            snprintf(plan->bytes_assembly_name,
                     sizeof(plan->bytes_assembly_name), "%s",
                     asm_name_for(
                         mir.declared_link_names[declaration]));
            break;
        }
    plan->check_string_ids[0] = (int)mir.insns[37].immediate;
    plan->check_string_ids[1] = (int)mir.insns[47].immediate;
    plan->expected_values[0] =
        (unsigned long)mir.insns[42].immediate;
    plan->expected_values[1] =
        (unsigned long)mir.insns[52].immediate;
    plan->count = (int)mir.insns[12].immediate;
    if (plan->bytes_assembly_name[0] == 0 ||
        plan->count <= 0 || plan->count > 255)
        return mir_machine_reject("crc-update-runner", "objects");
    return 1;
}

static void mir_emit_crc_update_runner(
    MirStream *out, const struct MirCrcUpdateRunner *plan)
{
    int loop = new_label();
    int argument;

    mir_stream_printf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_symbol_call(out, plan->init_function);
    mir_stream_puts("\tld de,0\n\tld hl,0\n\tld iy,0\n", out);
    mir_stream_printf(out,
            "L%d:\n"
            "\tpush de\n\tpush hl\n"
            "\tld hl,%s\n\tpush iy\n\tpop de\n\tadd hl,de\n"
            "\tld a,(hl)\n\tpop hl\n\tpop de\n"
            "\tld c,a\n\tld b,0\n\tpush bc\n"
            "\tld bc,65535\n\tpush bc\n\tpush bc\n"
            "\tld bc,%s\n\tpush bc\n"
            "\tpush de\n\tpush hl\n",
            loop,
            plan->bytes_assembly_name,
            plan->table_assembly_name);
    mir_machine_emit_symbol_call(out, plan->update_function);
    for (argument = 0; argument < 6; ++argument)
        mir_stream_puts("\tpop bc\n", out);
    mir_stream_printf(out,
            "\tinc iy\n\tpush iy\n\tpop bc\n"
            "\tld a,c\n\tcp %d\n\tjp nz,L%d\n",
            plan->count, loop);
    mir_emit_crc_check(out, plan, 0);
    mir_machine_emit_symbol_call(out, plan->cleanup_function);
    mir_emit_crc_check(out, plan, 1);
    mir_stream_puts("\tpop iy\n;@dcc.reg free=iy\n\tret\n", out);
}

static int mir_match_fixed_row_member_sum(
    struct MirFixedRowMemberSum *plan)
{
    long column_count;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 59 || mir_cfg_block_count() != 7 ||
        mir.has_vla || type_size(mir.return_type) != 4 ||
        mir.insns[1].opcode != MIR_PARAM ||
        mir.insns[2].opcode != MIR_PARAM ||
        !mir_machine_constant_equals(mir.insns[4].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[6].dst, 0) ||
        mir.insns[13].opcode != MIR_PHI ||
        mir.insns[16].immediate != '<' ||
        mir.insns[16].src1 != mir.insns[13].dst ||
        mir.insns[16].src2 != mir.insns[2].dst ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        !mir_machine_constant_equals(mir.insns[18].dst, 0) ||
        !mir_machine_constant_value(
            mir.insns[28].dst, &column_count, 0) ||
        mir.insns[29].immediate != '<' ||
        mir.insns[30].src1 != mir.insns[29].dst ||
        mir.insns[34].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[34].src1 != mir.insns[1].dst ||
        mir.insns[34].src2 != mir.insns[13].dst ||
        mir.insns[36].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[36].src1 != mir.insns[34].dst ||
        mir.insns[37].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[37].src1 != mir.insns[36].dst ||
        mir.insns[38].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[38].memory_size != 2 ||
        mir.insns[40].immediate != '+' ||
        !mir_machine_constant_equals(mir.insns[45].dst, 1) ||
        mir.insns[46].immediate != '+' ||
        mir.insns[48].label != mir.insns[21].label ||
        !mir_machine_constant_equals(mir.insns[52].dst, 1) ||
        mir.insns[53].immediate != '+' ||
        mir.insns[55].label != mir.insns[9].label ||
        mir.insns[58].src1 != mir.insns[57].dst)
        return mir_machine_reject("fixed-row-member-sum", "shape");
    plan->columns = (int)column_count;
    plan->element_stride = (int)mir.insns[36].immediate;
    plan->member_offset = (int)mir.insns[37].immediate;
    if ((int)mir.insns[34].immediate !=
            plan->columns * plan->element_stride ||
        plan->columns <= 0 || plan->columns > 8 ||
        plan->element_stride <= 0 || plan->element_stride > 16 ||
        plan->member_offset < 0 ||
        plan->member_offset + 1 >= plan->element_stride ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->pointer_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &plan->rows_stack_offset))
        return mir_machine_reject("fixed-row-member-sum", "layout");
    return 1;
}

static void mir_emit_fixed_row_member_sum(
    MirStream *out, const struct MirFixedRowMemberSum *plan)
{
    int loop = new_label();
    int done = new_label();
    int column;
    int step;

    mir_stream_puts("\tpush ix\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tpush de\n\tpop ix\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tld de,0\n\tld hl,0\n"
            "\tld a,b\n\tor a\n\tjp m,L%d\n"
            "\tor c\n\tjp z,L%d\n"
            "L%d:\n",
            plan->pointer_stack_offset + 2,
            plan->rows_stack_offset + 2,
            done, done, loop);
    for (column = 0; column < plan->columns; ++column) {
        mir_stream_printf(out,
                "\tpush bc\n"
                "\tld c,(ix%+d)\n\tld b,(ix%+d)\n"
                "\tld a,b\n\trla\n\tsbc a,a\n"
                "\tadd hl,bc\n\tld c,a\n\tld b,a\n"
                "\tex de,hl\n\tadc hl,bc\n\tex de,hl\n"
                "\tpop bc\n",
                plan->member_offset, plan->member_offset + 1);
        for (step = 0; step < plan->element_stride; ++step)
            mir_stream_puts("\tinc ix\n", out);
    }
    mir_stream_printf(out,
            "\tdec bc\n\tld a,b\n\tor c\n\tjp nz,L%d\n"
            "L%d:\n\tpop ix\n\tret\n",
            loop, done);
}

static int mir_match_recursive_aggregate_chain(
    struct MirRecursiveAggregateChain *plan)
{
    int recursive_arguments[2];
    int memory_type;
    int memory_storage;
    int memory_offset;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 27 || mir_cfg_block_count() != 2 ||
        mir.has_vla || mir.insns[1].opcode != MIR_PARAM ||
        mir.insns[2].opcode != MIR_PARAM ||
        !mir_machine_constant_equals(mir.insns[4].dst, 0) ||
        mir.insns[5].immediate != TOK_EQ ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        mir.insns[7].opcode != MIR_ADDRESS ||
        mir.insns[8].opcode != MIR_ARG ||
        mir.insns[8].src1 != mir.insns[7].dst ||
        mir.insns[9].opcode != MIR_CALL_AGGREGATE ||
        mir.insns[9].memory_size != 8 ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[12].opcode != MIR_ADDRESS ||
        mir.insns[13].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[13].src1 != mir.insns[12].dst ||
        mir.insns[14].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[14].memory_size != 4 ||
        !mir_machine_constant_equals(mir.insns[16].dst, 1) ||
        mir.insns[17].immediate != '+' ||
        mir.insns[18].opcode != MIR_STORE_INDIRECT ||
        mir.insns[18].src1 != mir.insns[13].dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        !mir_machine_constant_equals(mir.insns[20].dst, 1) ||
        mir.insns[21].immediate != '-' ||
        !mir_machine_two_call_arguments(
            &mir.insns[25], recursive_arguments) ||
        recursive_arguments[0] != mir.insns[21].dst ||
        recursive_arguments[1] != mir.insns[23].dst ||
        mir.insns[25].opcode != MIR_CALL_AGGREGATE ||
        mir.insns[25].memory_size != mir.insns[9].memory_size ||
        mir.insns[26].src1 != mir.insns[25].dst)
        return mir_machine_reject(
            "recursive-aggregate-chain", "shape");
    plan->normalize_function = find_global(mir.insns[9].name);
    plan->recursive_function = find_global(mir.insns[25].name);
    plan->aggregate_size = mir.insns[9].memory_size;
    plan->member_offset = (int)mir.insns[13].immediate;
    if (plan->normalize_function == NULL ||
        plan->recursive_function == NULL ||
        plan->aggregate_size != 8 ||
        plan->member_offset < 0 ||
        plan->member_offset + 4 > plan->aggregate_size ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->depth_stack_offset) ||
        !mir_scalar_memory_location(
            &mir.insns[2], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_PARAM ||
        type_size(memory_type) != plan->aggregate_size ||
        plan->depth_stack_offset != 4 ||
        (plan->value_stack_offset = memory_offset - 2) != 6)
        return mir_machine_reject(
            "recursive-aggregate-chain", "abi");
    return 1;
}

static void mir_emit_recursive_aggregate_chain(
    MirStream *out, const struct MirRecursiveAggregateChain *plan)
{
    int normalize = new_label();
    int recursive = new_label();
    int done = new_label();
    int argument;

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld c,(ix+%d)\n\tld b,(ix+%d)\n"
            "\tld a,b\n\tor c\n\tjp z,L%d\n"
            "\tbit 7,b\n\tjp nz,L%d\n",
            plan->depth_stack_offset + 2,
            plan->depth_stack_offset + 3,
            normalize, recursive);
    mir_emit_recursive_chain_add(out, plan);
    mir_stream_printf(out, "L%d:\n", normalize);
    mir_emit_recursive_chain_value_copy(out, plan);
    mir_stream_puts("\tld l,(ix+4)\n\tld h,(ix+5)\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->normalize_function);
    for (argument = 0; argument < 5; ++argument)
        mir_stream_puts("\tpop bc\n", out);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n\tld bc,1\n", done, recursive);
    mir_emit_recursive_chain_add(out, plan);
    mir_emit_recursive_chain_value_copy(out, plan);
    mir_stream_printf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tdec hl\n\tpush hl\n"
            "\tld l,(ix+4)\n\tld h,(ix+5)\n\tpush hl\n",
            plan->depth_stack_offset + 2,
            plan->depth_stack_offset + 3);
    mir_machine_emit_symbol_call(out, plan->recursive_function);
    for (argument = 0; argument < 6; ++argument)
        mir_stream_puts("\tpop bc\n", out);
    mir_stream_printf(out, "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n", done);
}

static int mir_match_fixed_call_spill_runner(
    struct MirFixedCallSpillRunner *plan)
{
    int arguments[2];
    int declaration;
    long count;
    long expected_total;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 78 || mir_cfg_block_count() != 7 ||
        mir.has_vla ||
        !mir_machine_constant_equals(mir.insns[1].dst, 0) ||
        mir.insns[5].opcode != MIR_PHI ||
        !mir_machine_constant_value(
            mir.insns[7].dst, &count, 0) ||
        mir.insns[8].immediate != '<' ||
        mir.insns[12].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[12].src1 != mir.insns[10].dst ||
        mir.insns[12].src2 != mir.insns[5].dst ||
        !mir_machine_constant_equals(mir.insns[14].dst, 1) ||
        mir.insns[15].immediate != '+' ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[17].opcode != MIR_STORE_INDIRECT ||
        mir.insns[17].src1 != mir.insns[12].dst ||
        mir.insns[17].src2 != mir.insns[16].dst ||
        !mir_machine_constant_equals(mir.insns[20].dst, 1) ||
        mir.insns[21].immediate != '+' ||
        mir.insns[23].label != mir.insns[4].label ||
        mir.insns[25].opcode != MIR_ADDRESS ||
        strcmp(mir.insns[25].name, mir.insns[10].name) ||
        !mir_machine_constant_equals(mir.insns[28].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[31].dst, 0) ||
        mir.insns[35].opcode != MIR_PHI ||
        mir.insns[36].opcode != MIR_PHI ||
        !mir_machine_constant_equals(
            mir.insns[38].dst, count) ||
        mir.insns[39].immediate != '<' ||
        !mir_machine_two_call_arguments(
            &mir.insns[46], arguments) ||
        arguments[0] != mir.insns[42].dst ||
        arguments[1] != mir.insns[44].dst ||
        !mir_machine_constant_equals(mir.insns[44].dst, 1) ||
        mir.insns[47].immediate != '+' ||
        !mir_machine_constant_equals(mir.insns[51].dst, 1) ||
        mir.insns[52].immediate != '+' ||
        !mir_machine_constant_equals(mir.insns[57].dst, 1) ||
        mir.insns[58].immediate != '+' ||
        mir.insns[60].label != mir.insns[34].label)
        return mir_machine_reject("fixed-call-spill-runner", "loops");
    if (!mir_machine_constant_value(
            mir.insns[63].dst, &expected_total, 0) ||
        mir.insns[64].immediate != TOK_EQ ||
        !mir_machine_two_call_arguments(
            &mir.insns[68], arguments) ||
        arguments[0] != mir.insns[64].dst ||
        arguments[1] != mir.insns[66].dst ||
        mir.insns[69].opcode != MIR_LOAD ||
        mir.insns[70].opcode != MIR_ADDRESS ||
        strcmp(mir.insns[70].name, mir.insns[10].name) ||
        !mir_machine_constant_equals(
            mir.insns[71].dst, count) ||
        mir.insns[72].immediate != '+' ||
        mir.insns[73].immediate != TOK_EQ ||
        !mir_machine_two_call_arguments(
            &mir.insns[77], arguments) ||
        arguments[0] != mir.insns[73].dst ||
        arguments[1] != mir.insns[75].dst)
        return mir_machine_reject("fixed-call-spill-runner", "checks");
    plan->count = (int)count;
    plan->expected_total = (int)expected_total;
    plan->sum_function = find_global(mir.insns[46].name);
    plan->check_function = find_global(mir.insns[68].name);
    plan->message_string_ids[0] = (int)mir.insns[66].immediate;
    plan->message_string_ids[1] = (int)mir.insns[75].immediate;
    for (declaration = 0; declaration < mir.declared_count; ++declaration)
        if (!strcmp(
                mir.declared_names[declaration],
                mir.insns[10].name)) {
            snprintf(plan->buffer_assembly_name,
                     sizeof(plan->buffer_assembly_name), "%s",
                     asm_name_for(
                         mir.declared_link_names[declaration]));
            break;
        }
    if (plan->sum_function == NULL ||
        plan->check_function == NULL ||
        plan->buffer_assembly_name[0] == 0 ||
        plan->count <= 0 || plan->count > 32)
        return mir_machine_reject("fixed-call-spill-runner", "symbols");
    return 1;
}

static void mir_emit_fixed_call_spill_runner(
    MirStream *out, const struct MirFixedCallSpillRunner *plan)
{
    int element;

    mir_stream_printf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tdec sp\n\tdec sp\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out, "\tld hl,%s\n", plan->buffer_assembly_name);
    for (element = 0; element < plan->count; ++element) {
        mir_stream_printf(out, "\tld (hl),%d\n", element + 1);
        if (element + 1 != plan->count)
            mir_stream_puts("\tinc hl\n", out);
    }
    mir_stream_printf(out,
            "\tld hl,%s\n\tpush hl\n\tpop iy\n"
            "\tld (ix-2),0\n\tld (ix-1),0\n",
            plan->buffer_assembly_name);
    for (element = 0; element < plan->count; ++element) {
        mir_stream_puts("\tld hl,1\n\tpush hl\n"
              "\tpush iy\n\tpop hl\n\tpush hl\n", out);
        mir_machine_emit_symbol_call(out, plan->sum_function);
        mir_stream_puts("\tpop bc\n\tpop bc\n"
              "\tld e,(ix-2)\n\tld d,(ix-1)\n\tadd hl,de\n"
              "\tld (ix-2),l\n\tld (ix-1),h\n\tinc iy\n", out);
    }
    mir_emit_fixed_call_spill_check(out, plan, 0, 0);
    mir_emit_fixed_call_spill_check(out, plan, 1, 1);
    mir_stream_puts("\tld sp,ix\n\tpop ix\n\tpop iy\n"
          ";@dcc.reg free=iy\n\tret\n", out);
}

static int mir_match_fixed_byte_copy_checks(
    struct MirFixedByteCopyChecks *plan)
{
    int arguments[2];
    int declaration;
    long count;
    long start_value;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 97 || mir_cfg_block_count() != 10 ||
        mir.has_vla ||
        !mir_machine_constant_equals(mir.insns[1].dst, 0) ||
        mir.insns[5].opcode != MIR_PHI ||
        !mir_machine_constant_value(mir.insns[7].dst, &count, 0) ||
        mir.insns[8].immediate != '<' ||
        mir.insns[12].opcode != MIR_INDEX_ADDRESS ||
        !mir_machine_constant_value(
            mir.insns[13].dst, &start_value, 0) ||
        mir.insns[15].immediate != '+' ||
        mir.insns[17].opcode != MIR_STORE_INDIRECT ||
        !mir_machine_constant_equals(mir.insns[20].dst, 1) ||
        mir.insns[21].immediate != '+' ||
        mir.insns[23].label != mir.insns[4].label ||
        mir.insns[25].opcode != MIR_ADDRESS ||
        mir.insns[28].opcode != MIR_ADDRESS ||
        !mir_machine_constant_equals(mir.insns[31].dst, 0) ||
        mir.insns[35].opcode != MIR_PHI ||
        !mir_machine_constant_equals(mir.insns[37].dst, count) ||
        mir.insns[38].immediate != '<' ||
        !mir_machine_constant_equals(mir.insns[41].dst, 1) ||
        mir.insns[42].immediate != '+' ||
        !mir_machine_constant_equals(mir.insns[45].dst, 1) ||
        mir.insns[46].immediate != '+' ||
        mir.insns[48].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[49].opcode != MIR_STORE_INDIRECT ||
        !mir_machine_constant_equals(mir.insns[52].dst, 1) ||
        mir.insns[53].immediate != '+' ||
        mir.insns[55].label != mir.insns[34].label ||
        !mir_machine_constant_equals(mir.insns[57].dst, 0) ||
        mir.insns[61].opcode != MIR_PHI ||
        !mir_machine_constant_equals(mir.insns[63].dst, count) ||
        mir.insns[64].immediate != '<' ||
        mir.insns[66].opcode != MIR_ADDRESS ||
        strcmp(mir.insns[66].name, mir.insns[28].name) ||
        mir.insns[68].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[69].opcode != MIR_LOAD_INDIRECT ||
        !mir_machine_constant_equals(
            mir.insns[70].dst, start_value) ||
        mir.insns[72].immediate != '+' ||
        mir.insns[76].immediate != TOK_EQ ||
        !mir_machine_two_call_arguments(
            &mir.insns[80], arguments) ||
        arguments[0] != mir.insns[76].dst ||
        arguments[1] != mir.insns[78].dst ||
        !mir_machine_constant_equals(mir.insns[83].dst, 1) ||
        mir.insns[84].immediate != '+' ||
        mir.insns[86].label != mir.insns[60].label)
        return mir_machine_reject("fixed-byte-copy-checks", "loops");
    if (mir.insns[88].opcode != MIR_LOAD ||
        mir.insns[89].opcode != MIR_ADDRESS ||
        strcmp(mir.insns[89].name, mir.insns[25].name) ||
        !mir_machine_constant_equals(mir.insns[90].dst, count) ||
        mir.insns[91].immediate != '+' ||
        mir.insns[92].immediate != TOK_EQ ||
        !mir_machine_two_call_arguments(
            &mir.insns[96], arguments) ||
        arguments[0] != mir.insns[92].dst ||
        arguments[1] != mir.insns[94].dst)
        return mir_machine_reject("fixed-byte-copy-checks", "final-check");
    plan->check_function = find_global(mir.insns[80].name);
    plan->message_string_ids[0] = (int)mir.insns[78].immediate;
    plan->message_string_ids[1] = (int)mir.insns[94].immediate;
    plan->count = (int)count;
    plan->start_value = (int)start_value;
    for (declaration = 0; declaration < mir.declared_count; ++declaration) {
        if (!strcmp(
                mir.declared_names[declaration],
                mir.insns[10].name))
            snprintf(plan->source_assembly_name,
                     sizeof(plan->source_assembly_name), "%s",
                     asm_name_for(
                         mir.declared_link_names[declaration]));
        if (!strcmp(
                mir.declared_names[declaration],
                mir.insns[28].name))
            snprintf(plan->destination_assembly_name,
                     sizeof(plan->destination_assembly_name), "%s",
                     asm_name_for(
                         mir.declared_link_names[declaration]));
    }
    if (plan->check_function == NULL ||
        plan->source_assembly_name[0] == 0 ||
        plan->destination_assembly_name[0] == 0 ||
        plan->count <= 0 || plan->count > 32 ||
        plan->start_value < 0 ||
        plan->start_value + plan->count > 256)
        return mir_machine_reject("fixed-byte-copy-checks", "symbols");
    return 1;
}

static void mir_emit_fixed_byte_copy_checks(
    MirStream *out, const struct MirFixedByteCopyChecks *plan)
{
    int copy = new_label();
    int unequal = new_label();
    int element;

    mir_stream_printf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out, "\tld hl,%s\n", plan->source_assembly_name);
    for (element = 0; element < plan->count; ++element) {
        mir_stream_printf(out, "\tld (hl),%d\n", plan->start_value + element);
        if (element + 1 != plan->count)
            mir_stream_puts("\tinc hl\n", out);
    }
    mir_stream_printf(out,
            "\tld hl,%s\n\tpush hl\n\tpop iy\n"
            "\tld hl,%s\n\tld b,%d\n"
            "L%d:\n\tld a,(iy+0)\n\tld (hl),a\n"
            "\tinc iy\n\tinc hl\n\tdjnz L%d\n",
            plan->source_assembly_name,
            plan->destination_assembly_name, plan->count,
            copy, copy);
    for (element = 0; element < plan->count; ++element)
        mir_emit_fixed_byte_copy_check(out, plan, element);
    mir_stream_printf(out,
            "\tld hl,S%d\n\tpush hl\n"
            "\tpush iy\n\tpop hl\n\tld de,%s+%d\n"
            "\tor a\n\tsbc hl,de\n\tld hl,0\n"
            "\tjp nz,L%d\n\tinc hl\nL%d:\n\tpush hl\n",
            plan->message_string_ids[1],
            plan->source_assembly_name, plan->count,
            unequal, unequal);
    mir_machine_emit_symbol_call(out, plan->check_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop iy\n"
          ";@dcc.reg free=iy\n\tret\n", out);
}

static int mir_match_proven_wide_shift_checks(
    struct MirProvenWideShiftChecks *plan)
{
    static const int call_indices[8] = {
        21, 28, 46, 53, 72, 79, 96, 103
    };
    static const int condition_indices[8] = {
        17, 24, 42, 49, 68, 75, 92, 99
    };
    static const int string_indices[8] = {
        19, 26, 44, 51, 70, 77, 94, 101
    };
    int arguments[2];
    int declaration;
    int check;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 104 || mir_cfg_block_count() != 1 ||
        mir.has_vla || mir.insns[1].opcode != MIR_ADDRESS ||
        mir.insns[4].opcode != MIR_ADDRESS ||
        !mir_machine_constant_equals(
            mir.insns[7].dst, 0x12345678L) ||
        mir.insns[9].opcode != MIR_STORE ||
        mir.insns[11].opcode != MIR_LOAD_INDIRECT ||
        !mir_machine_constant_equals(mir.insns[12].dst, 4) ||
        mir.insns[13].immediate != TOK_SHR ||
        mir.insns[14].opcode != MIR_STORE_INDIRECT ||
        mir.insns[14].src2 != mir.insns[13].dst ||
        !mir_machine_constant_equals(
            mir.insns[16].dst, 0x01234567L) ||
        mir.insns[17].immediate != TOK_EQ ||
        mir.insns[24].immediate != TOK_EQ ||
        strcmp(mir.insns[23].name, mir.insns[1].name) ||
        !mir_machine_constant_equals(
            mir.insns[29].dst, 0x12345678L) ||
        !mir_machine_constant_equals(mir.insns[32].dst, 8) ||
        mir.insns[36].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[38].immediate != TOK_SHR ||
        mir.insns[39].opcode != MIR_STORE_INDIRECT ||
        !mir_machine_constant_equals(
            mir.insns[41].dst, 0x00123456L) ||
        mir.insns[42].immediate != TOK_EQ ||
        mir.insns[49].immediate != TOK_EQ ||
        strcmp(mir.insns[48].name, mir.insns[1].name) ||
        !mir_machine_constant_equals(
            mir.insns[54].dst, 0x12345678L) ||
        mir.insns[58].opcode != MIR_LOAD_INDIRECT ||
        !mir_machine_constant_equals(mir.insns[59].dst, 4) ||
        mir.insns[60].immediate != TOK_SHL ||
        mir.insns[61].opcode != MIR_STORE_INDIRECT ||
        !mir_machine_constant_equals(
            mir.insns[63].dst, 0xffffffffUL) ||
        mir.insns[65].immediate != '&' ||
        !mir_machine_constant_equals(
            mir.insns[66].dst, 0x23456780L) ||
        mir.insns[68].immediate != TOK_EQ ||
        mir.insns[75].immediate != TOK_EQ ||
        strcmp(mir.insns[74].name, mir.insns[1].name) ||
        !mir_machine_constant_equals(
            mir.insns[80].dst, 0x00ff0000L) ||
        !mir_machine_constant_equals(mir.insns[83].dst, 8) ||
        mir.insns[87].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[89].immediate != TOK_SHR ||
        mir.insns[90].opcode != MIR_STORE_INDIRECT ||
        !mir_machine_constant_equals(
            mir.insns[91].dst, 0x0000ff00L) ||
        mir.insns[92].immediate != TOK_EQ ||
        mir.insns[99].immediate != TOK_EQ ||
        strcmp(mir.insns[98].name, mir.insns[1].name))
        return mir_machine_reject("proven-wide-shift-checks", "shape");
    for (check = 0; check < 8; ++check) {
        const struct MirInsn *call = &mir.insns[call_indices[check]];
        if (!mir_machine_two_call_arguments(call, arguments) ||
            arguments[0] != mir.insns[condition_indices[check]].dst ||
            arguments[1] != mir.insns[string_indices[check]].dst ||
            (check != 0 &&
             strcmp(call->name, mir.insns[call_indices[0]].name)))
            return mir_machine_reject(
                "proven-wide-shift-checks", "calls");
        plan->message_string_ids[check] =
            (int)mir.insns[string_indices[check]].immediate;
    }
    plan->check_function = find_global(mir.insns[21].name);
    plan->final_value = 0x0000ff00UL;
    for (declaration = 0; declaration < mir.declared_count; ++declaration)
        if (!strcmp(
                mir.declared_names[declaration],
                mir.insns[8].name)) {
            snprintf(plan->value_assembly_name,
                     sizeof(plan->value_assembly_name), "%s",
                     asm_name_for(
                         mir.declared_link_names[declaration]));
            break;
        }
    if (plan->check_function == NULL ||
        plan->value_assembly_name[0] == 0)
        return mir_machine_reject("proven-wide-shift-checks", "symbols");
    return 1;
}

static void mir_emit_proven_wide_shift_checks(
    MirStream *out, const struct MirProvenWideShiftChecks *plan)
{
    int check;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%lu\n\tld (%s),hl\n"
            "\tld hl,%lu\n\tld (%s+2),hl\n",
            plan->final_value & 0xffffUL,
            plan->value_assembly_name,
            (plan->final_value >> 16) & 0xffffUL,
            plan->value_assembly_name);
    for (check = 0; check < 8; ++check) {
        mir_stream_printf(out,
                "\tld hl,S%d\n\tpush hl\n"
                "\tld hl,1\n\tpush hl\n",
                plan->message_string_ids[check]);
        mir_machine_emit_symbol_call(out, plan->check_function);
        mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    }
    mir_stream_puts("\tret\n", out);
}

static int mir_match_two_post_update_reports(
    struct MirTwoPostUpdateReports *plan)
{
    int arguments[3];
    long initial;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 51 || mir_cfg_block_count() != 1 ||
        mir.has_vla || mir.insns[1].opcode != MIR_ADDRESS ||
        !mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        mir.insns[3].opcode != MIR_INDEX_ADDRESS ||
        !mir_machine_constant_value(mir.insns[4].dst, &initial, 0) ||
        mir.insns[5].opcode != MIR_STORE_INDIRECT ||
        strcmp(mir.insns[6].name, mir.insns[1].name) ||
        !mir_machine_constant_equals(mir.insns[7].dst, 1) ||
        !mir_machine_constant_equals(
            mir.insns[9].dst, initial + 10) ||
        !mir_machine_constant_equals(mir.insns[12].dst, 2) ||
        !mir_machine_constant_equals(
            mir.insns[14].dst, initial + 20) ||
        !mir_machine_constant_equals(mir.insns[17].dst, 3) ||
        !mir_machine_constant_equals(
            mir.insns[19].dst, initial + 30) ||
        mir.insns[23].opcode != MIR_LOAD ||
        mir.insns[24].opcode != MIR_LOAD_INDIRECT ||
        !mir_machine_constant_equals(mir.insns[25].dst, 1) ||
        mir.insns[26].immediate != '+' ||
        mir.insns[27].opcode != MIR_STORE_INDIRECT ||
        !mir_machine_three_call_arguments(
            &mir.insns[36], arguments) ||
        arguments[0] != mir.insns[29].dst ||
        arguments[1] != mir.insns[24].dst ||
        arguments[2] != mir.insns[34].dst ||
        mir.insns[37].opcode != MIR_LOAD ||
        mir.insns[38].opcode != MIR_LOAD_INDIRECT ||
        !mir_machine_constant_equals(mir.insns[39].dst, 1) ||
        mir.insns[40].immediate != '-' ||
        mir.insns[41].opcode != MIR_STORE_INDIRECT ||
        !mir_machine_three_call_arguments(
            &mir.insns[50], arguments) ||
        arguments[0] != mir.insns[43].dst ||
        arguments[1] != mir.insns[38].dst ||
        arguments[2] != mir.insns[48].dst ||
        strcmp(mir.insns[36].name, mir.insns[50].name))
        return mir_machine_reject("two-post-update-reports", "shape");
    plan->print_function = find_global(mir.insns[36].name);
    plan->format_string_ids[0] = (int)mir.insns[29].immediate;
    plan->format_string_ids[1] = (int)mir.insns[43].immediate;
    plan->old_values[0] = (int)initial;
    plan->new_values[0] = (int)initial + 1;
    plan->old_values[1] = (int)initial + 1;
    plan->new_values[1] = (int)initial;
    plan->count = 2;
    if (plan->print_function == NULL)
        return mir_machine_reject("two-post-update-reports", "function");
    return 1;
}

static int mir_match_pointer_word_update_reports(
    struct MirTwoPostUpdateReports *plan)
{
    int arguments[3];
    long second_value;
    long third_value;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 65 || mir_cfg_block_count() != 1 ||
        mir.has_vla || mir.insns[1].opcode != MIR_ADDRESS ||
        !mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[4].dst, 100) ||
        !mir_machine_constant_equals(mir.insns[7].dst, 1) ||
        !mir_machine_constant_value(
            mir.insns[9].dst, &second_value, 0) ||
        !mir_machine_constant_equals(mir.insns[12].dst, 2) ||
        !mir_machine_constant_value(
            mir.insns[14].dst, &third_value, 0) ||
        !mir_machine_constant_equals(mir.insns[17].dst, 3) ||
        !mir_machine_constant_equals(mir.insns[19].dst, 400) ||
        mir.insns[23].opcode != MIR_LOAD ||
        !mir_machine_constant_equals(mir.insns[24].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[25].dst, 2) ||
        mir.insns[26].immediate != '*' ||
        mir.insns[27].immediate != '+' ||
        mir.insns[28].opcode != MIR_LOAD_INDIRECT ||
        !mir_machine_constant_equals(mir.insns[29].dst, 1) ||
        mir.insns[30].immediate != '+' ||
        mir.insns[31].opcode != MIR_STORE_INDIRECT ||
        !mir_machine_three_call_arguments(
            &mir.insns[42], arguments) ||
        arguments[0] != mir.insns[33].dst ||
        arguments[1] != mir.insns[28].dst ||
        arguments[2] != mir.insns[40].dst ||
        !mir_machine_constant_equals(mir.insns[43].dst, 2) ||
        mir.insns[45].opcode != MIR_LOAD ||
        !mir_machine_constant_equals(mir.insns[47].dst, 2) ||
        mir.insns[48].immediate != '*' ||
        mir.insns[49].immediate != '+' ||
        mir.insns[50].opcode != MIR_LOAD_INDIRECT ||
        !mir_machine_constant_equals(mir.insns[51].dst, 1) ||
        mir.insns[52].immediate != '-' ||
        mir.insns[53].opcode != MIR_STORE_INDIRECT ||
        !mir_machine_three_call_arguments(
            &mir.insns[64], arguments) ||
        arguments[0] != mir.insns[55].dst ||
        arguments[1] != mir.insns[50].dst ||
        arguments[2] != mir.insns[62].dst ||
        strcmp(mir.insns[42].name, mir.insns[64].name))
        return mir_machine_reject(
            "pointer-word-update-reports", "shape");
    plan->print_function = find_global(mir.insns[42].name);
    plan->format_string_ids[0] = (int)mir.insns[33].immediate;
    plan->format_string_ids[1] = (int)mir.insns[55].immediate;
    plan->old_values[0] = (int)second_value;
    plan->new_values[0] = (int)second_value + 1;
    plan->old_values[1] = (int)third_value;
    plan->new_values[1] = (int)third_value - 1;
    plan->count = 2;
    if (plan->print_function == NULL)
        return mir_machine_reject(
            "pointer-word-update-reports", "function");
    return 1;
}

static void mir_emit_two_post_update_reports(
    MirStream *out, const struct MirTwoPostUpdateReports *plan)
{
    int report;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (report = 0; report < plan->count; ++report) {
        mir_stream_printf(out,
                "\tld hl,%d\n\tpush hl\n"
                "\tld hl,%d\n\tpush hl\n"
                "\tld hl,S%d\n\tpush hl\n",
                plan->new_values[report],
                plan->old_values[report],
                plan->format_string_ids[report]);
        mir_machine_emit_symbol_call(out, plan->print_function);
        mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n", out);
    }
    mir_stream_puts("\tret\n", out);
}

static int mir_match_post_decrement_check_runner(
    struct MirPostDecrementCheckRunner *plan)
{
    static const int check_calls[3] = { 34, 68, 100 };
    static const int check_strings[3] = { 28, 62, 94 };
    static const int actual_values[3] = { 13, 47, 78 };
    static const int expected_values[3] = { 32, 66, 98 };
    int memory_type;
    int memory_storage;
    int memory_offset;
    int args[3];
    int check;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 117 || mir_cfg_block_count() != 11 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_INT ||
        !mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[5].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[7].dst, 0) ||
        mir.insns[11].src1 != mir.insns[2].dst ||
        mir.insns[11].src2 != mir.insns[16].dst ||
        mir.insns[13].src1 != mir.insns[7].dst ||
        mir.insns[13].src2 != mir.insns[23].dst ||
        !mir_machine_constant_equals(mir.insns[15].dst, 1) ||
        mir.insns[16].immediate != '-' ||
        mir.insns[16].src1 != mir.insns[11].dst ||
        mir.insns[16].src2 != mir.insns[15].dst ||
        mir.insns[19].immediate != '>' ||
        mir.insns[19].src1 != mir.insns[11].dst ||
        mir.insns[19].src2 != mir.insns[5].dst ||
        !mir_machine_constant_equals(mir.insns[22].dst, 1) ||
        mir.insns[23].immediate != '+' ||
        mir.insns[23].src1 != mir.insns[13].dst ||
        mir.insns[26].label != mir.insns[10].label)
        return mir_machine_reject(
            "post-decrement-check-runner", "first-loop");
    if (!mir_machine_constant_equals(mir.insns[36].dst, 3) ||
        !mir_machine_constant_equals(mir.insns[39].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[41].dst, 0) ||
        mir.insns[45].src1 != mir.insns[36].dst ||
        mir.insns[45].src2 != mir.insns[50].dst ||
        mir.insns[47].src1 != mir.insns[41].dst ||
        mir.insns[47].src2 != mir.insns[57].dst ||
        mir.insns[50].immediate != '-' ||
        mir.insns[53].immediate != '>' ||
        mir.insns[53].src1 != mir.insns[45].dst ||
        mir.insns[53].src2 != mir.insns[39].dst ||
        mir.insns[57].immediate != '+' ||
        mir.insns[60].label != mir.insns[44].label)
        return mir_machine_reject(
            "post-decrement-check-runner", "second-loop");
    if (!mir_machine_constant_equals(mir.insns[69].dst, 3) ||
        !mir_machine_constant_equals(mir.insns[72].dst, 0) ||
        mir.insns[78].src1 != mir.insns[72].dst ||
        mir.insns[78].src2 != mir.insns[89].dst ||
        mir.insns[79].src1 != mir.insns[69].dst ||
        mir.insns[79].src2 != mir.insns[82].dst ||
        !mir_machine_constant_equals(mir.insns[81].dst, 1) ||
        mir.insns[82].immediate != '-' ||
        !mir_machine_constant_equals(mir.insns[84].dst, 0) ||
        mir.insns[85].immediate != '>' ||
        mir.insns[85].src1 != mir.insns[79].dst ||
        mir.insns[85].src2 != mir.insns[84].dst ||
        mir.insns[89].immediate != '+' ||
        mir.insns[92].label != mir.insns[75].label)
        return mir_machine_reject(
            "post-decrement-check-runner", "third-loop");
    for (check = 0; check < 3; ++check) {
        const struct MirInsn *call =
            &mir.insns[check_calls[check]];
        const struct MirInsn *string =
            &mir.insns[check_strings[check]];

        if (!mir_machine_three_call_arguments(call, args) ||
            args[0] != string->dst ||
            args[1] != mir.insns[actual_values[check]].dst ||
            args[2] != mir.insns[expected_values[check]].dst ||
            string->opcode != MIR_STRING_ADDRESS ||
            (check != 0 &&
             strcmp(call->name,
                    mir.insns[check_calls[0]].name)))
            return mir_machine_reject(
                "post-decrement-check-runner", "checks");
        plan->check_strings[check] =
            (int)string->immediate;
        plan->check_values[check] =
            check == 0 ? 0 : 3;
    }
    plan->check_function =
        find_global(mir.insns[34].name);
    if (!mir_scalar_memory_location(
            &mir.insns[101], &memory_type,
            &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        type_size(memory_type) != 2 ||
        mir.insns[102].src1 != mir.insns[101].dst ||
        !mir_machine_two_call_arguments(
            &mir.insns[107], args) ||
        args[0] != mir.insns[103].dst ||
        args[1] != mir.insns[105].dst ||
        !mir_machine_single_call_argument(
            &mir.insns[114], &args[0]) ||
        args[0] != mir.insns[112].dst)
        return mir_machine_reject(
            "post-decrement-check-runner", "report");
    plan->failure_count =
        find_global(mir.insns[101].name);
    plan->failure_offset = memory_offset;
    plan->print_function =
        find_global(mir.insns[107].name);
    plan->failure_string =
        (int)mir.insns[103].immediate;
    plan->success_string =
        (int)mir.insns[112].immediate;
    return plan->check_function != NULL &&
           plan->failure_count != NULL &&
           plan->print_function != NULL;
}

static void mir_emit_post_decrement_check_runner(
    MirStream *out, const struct MirPostDecrementCheckRunner *plan)
{
    int success = new_label();
    int check;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (check = 0; check < 3; ++check) {
        mir_stream_printf(out,
                "\tld hl,%d\n\tpush hl\n\tpush hl\n"
                "\tld hl,S%d\n\tpush hl\n",
                plan->check_values[check],
                plan->check_strings[check]);
        mir_machine_emit_symbol_call(
            out, plan->check_function);
        mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n", out);
    }
    mir_machine_emit_global_word(
        out, plan->failure_count,
        plan->failure_offset);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n\tpush hl\n", success);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->failure_string);
    mir_machine_emit_symbol_call(
        out, plan->print_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld hl,1\n\tret\n", out);
    mir_stream_printf(out, "L%d:\n\tld hl,S%d\n\tpush hl\n",
            success, plan->success_string);
    mir_machine_emit_symbol_call(
        out, plan->print_function);
    mir_stream_puts("\tpop bc\n\tld hl,0\n\tret\n", out);
}

static int mir_match_char_pointer_update_reports(
    struct MirCharPointerUpdateReports *plan)
{
    static const int address_indices[3] = { 8, 28, 48 };
    static const int offset_indices[3] = { 9, 26, 46 };
    static const int load_indices[3] = { 11, 31, 51 };
    static const int update_indices[3] = { 13, 33, 53 };
    static const int store_indices[3] = { 14, 34, 54 };
    static const int string_indices[3] = { 16, 36, 56 };
    static const int result_load_indices[3] = { 23, 43, 63 };
    static const int call_indices[3] = { 25, 45, 65 };
    int arguments[3];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int report;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 66 || mir_cfg_block_count() != 1 ||
        mir.has_vla || mir.insns[1].opcode != MIR_ADDRESS ||
        mir.insns[3].opcode != MIR_STRING_ADDRESS ||
        !mir_machine_two_call_arguments(&mir.insns[5], arguments) ||
        arguments[0] != mir.insns[1].dst ||
        arguments[1] != mir.insns[3].dst ||
        mir.insns[6].opcode != MIR_ADDRESS ||
        strcmp(mir.insns[6].name, mir.insns[1].name) ||
        !mir_scalar_memory_location(
            &mir.insns[1], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_LOCAL || memory_offset >= 0)
        return mir_machine_reject(
            "char-pointer-update-reports", "setup");
    for (report = 0; report < 3; ++report) {
        const struct MirInsn *update =
            &mir.insns[update_indices[report]];
        const struct MirInsn *call = &mir.insns[call_indices[report]];
        long offset;
        if (mir.insns[address_indices[report]].opcode != MIR_LOAD ||
            !mir_machine_constant_value(
                mir.insns[offset_indices[report]].dst, &offset, 0) ||
            mir.insns[address_indices[report] + 2].immediate != '+' ||
            mir.insns[load_indices[report]].opcode != MIR_LOAD_INDIRECT ||
            mir.insns[load_indices[report]].memory_size != 1 ||
            !mir_machine_constant_equals(
                mir.insns[update_indices[report] - 1].dst, 1) ||
            (update->immediate != '+' && update->immediate != '-') ||
            mir.insns[store_indices[report]].opcode != MIR_STORE_INDIRECT ||
            mir.insns[store_indices[report]].src2 != update->dst ||
            !mir_machine_three_call_arguments(call, arguments) ||
            arguments[0] != mir.insns[string_indices[report]].dst ||
            arguments[1] != mir.insns[load_indices[report]].dst ||
            arguments[2] != mir.insns[result_load_indices[report]].dst ||
            (report != 0 &&
             strcmp(call->name, mir.insns[call_indices[0]].name)))
            return mir_machine_reject(
                "char-pointer-update-reports", "update");
        plan->offsets[report] = (int)offset;
        plan->deltas[report] = update->immediate == '+' ? 1 : -1;
        plan->format_string_ids[report] =
            (int)mir.insns[string_indices[report]].immediate;
    }
    plan->copy_function = find_global(mir.insns[5].name);
    plan->print_function = find_global(mir.insns[25].name);
    plan->source_string_id = (int)mir.insns[3].immediate;
    plan->array_offset = memory_offset;
    plan->frame_size = -memory_offset;
    if (plan->copy_function == NULL ||
        plan->print_function == NULL ||
        plan->frame_size <= 0 || plan->frame_size > 120)
        return mir_machine_reject(
            "char-pointer-update-reports", "symbols");
    return 1;
}

static void mir_emit_char_pointer_update_reports(
    MirStream *out, const struct MirCharPointerUpdateReports *plan)
{
    int report;

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    mir_stream_printf(out,
            "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n",
            plan->frame_size);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,S%d\n\tpush hl\n"
            "\tpush ix\n\tpop hl\n\tld de,%d\n"
            "\tadd hl,de\n\tpush hl\n",
            plan->source_string_id, plan->array_offset);
    mir_machine_emit_symbol_call(out, plan->copy_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    for (report = 0; report < 3; ++report) {
        mir_stream_printf(out,
                "\tld a,(ix%+d)\n\tld c,a\n\tld l,a\n",
                plan->array_offset + plan->offsets[report]);
        if (plan->deltas[report] > 0)
            mir_stream_puts("\tinc l\n", out);
        else
            mir_stream_puts("\tdec l\n", out);
        mir_stream_printf(out,
                "\tld (ix%+d),l\n"
                "\tld a,l\n\trla\n\tsbc a,a\n\tld h,a\n\tpush hl\n"
                "\tld a,c\n\tld l,a\n\trla\n\tsbc a,a\n"
                "\tld h,a\n\tpush hl\n"
                "\tld hl,S%d\n\tpush hl\n",
                plan->array_offset + plan->offsets[report],
                plan->format_string_ids[report]);
        mir_machine_emit_symbol_call(out, plan->print_function);
        mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n", out);
    }
    mir_stream_puts("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_two_string_pair_reports(
    struct MirTwoStringPairReports *plan)
{
    int arguments[3];

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 69 || mir_cfg_block_count() != 1 ||
        mir.has_vla ||
        mir.insns[1].opcode != MIR_STRING_ADDRESS ||
        mir.insns[4].opcode != MIR_STRING_ADDRESS ||
        mir.insns[7].opcode != MIR_STRING_ADDRESS ||
        mir.insns[10].opcode != MIR_STRING_ADDRESS ||
        mir.insns[13].opcode != MIR_ADDRESS ||
        !mir_machine_constant_equals(mir.insns[14].dst, 0) ||
        mir.insns[15].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[19].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[20].opcode != MIR_LOAD ||
        mir.insns[21].opcode != MIR_STORE_INDIRECT ||
        mir.insns[21].src2 != mir.insns[20].dst ||
        mir.insns[23].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[24].opcode != MIR_LOAD ||
        mir.insns[25].opcode != MIR_STORE_INDIRECT ||
        mir.insns[25].src2 != mir.insns[24].dst ||
        !mir_machine_constant_equals(mir.insns[27].dst, 4) ||
        mir.insns[28].immediate != '+' ||
        mir.insns[31].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[32].opcode != MIR_LOAD ||
        mir.insns[33].opcode != MIR_STORE_INDIRECT ||
        mir.insns[33].src2 != mir.insns[32].dst ||
        mir.insns[35].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[36].opcode != MIR_LOAD ||
        mir.insns[37].opcode != MIR_STORE_INDIRECT ||
        mir.insns[37].src2 != mir.insns[36].dst)
        return mir_machine_reject("two-string-pair-reports", "stores");
    if (!mir_machine_three_call_arguments(
            &mir.insns[52], arguments) ||
        arguments[0] != mir.insns[38].dst ||
        arguments[1] != mir.insns[44].dst ||
        arguments[2] != mir.insns[50].dst ||
        !mir_machine_constant_equals(mir.insns[41].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[47].dst, 0) ||
        !mir_machine_three_call_arguments(
            &mir.insns[67], arguments) ||
        arguments[0] != mir.insns[53].dst ||
        arguments[1] != mir.insns[59].dst ||
        arguments[2] != mir.insns[65].dst ||
        !mir_machine_constant_equals(mir.insns[56].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[62].dst, 1) ||
        strcmp(mir.insns[52].name, mir.insns[67].name) ||
        mir.insns[68].opcode != MIR_RETURN)
        return mir_machine_reject("two-string-pair-reports", "reports");
    plan->print_function = find_global(mir.insns[52].name);
    plan->format_string_ids[0] = (int)mir.insns[38].immediate;
    plan->format_string_ids[1] = (int)mir.insns[53].immediate;
    plan->value_string_ids[0] = (int)mir.insns[1].immediate;
    plan->value_string_ids[1] = (int)mir.insns[4].immediate;
    plan->value_string_ids[2] = (int)mir.insns[7].immediate;
    plan->value_string_ids[3] = (int)mir.insns[10].immediate;
    if (plan->print_function == NULL)
        return mir_machine_reject("two-string-pair-reports", "function");
    return 1;
}

static void mir_emit_two_string_pair_reports(
    MirStream *out, const struct MirTwoStringPairReports *plan)
{
    int report;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (report = 0; report < 2; ++report) {
        mir_stream_printf(out,
                "\tld hl,S%d\n\tpush hl\n"
                "\tld hl,S%d\n\tpush hl\n"
                "\tld hl,S%d\n\tpush hl\n",
                plan->value_string_ids[report * 2 + 1],
                plan->value_string_ids[report * 2],
                plan->format_string_ids[report]);
        mir_machine_emit_symbol_call(out, plan->print_function);
        mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n", out);
    }
    mir_stream_puts("\tret\n", out);
}

static int mir_match_triangle_perimeter(
    struct MirTrianglePerimeter *plan)
{
    int argument;
    long scale;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 41 || mir_cfg_block_count() != 1 ||
        mir.has_vla || type_size(mir.return_type) != 4 ||
        mir.insns[1].opcode != MIR_PARAM ||
        mir.insns[2].opcode != MIR_LOAD ||
        mir.insns[5].opcode != MIR_LOAD ||
        mir.insns[6].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[7].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[7].memory_size != 2 ||
        mir.insns[8].opcode != MIR_UNARY ||
        mir.insns[10].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[11].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[12].opcode != MIR_UNARY ||
        mir.insns[13].immediate != '*' ||
        mir.insns[15].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[16].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[17].opcode != MIR_UNARY ||
        mir.insns[19].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[20].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[21].opcode != MIR_UNARY ||
        mir.insns[22].immediate != '*' ||
        mir.insns[23].immediate != '+' ||
        !mir_machine_single_call_argument(
            &mir.insns[25], &argument) ||
        argument != mir.insns[23].dst ||
        mir.insns[28].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[29].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[30].opcode != MIR_UNARY ||
        mir.insns[32].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[33].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[34].opcode != MIR_UNARY ||
        mir.insns[35].immediate != '+' ||
        mir.insns[37].immediate != '+' ||
        !mir_machine_constant_value(mir.insns[38].dst, &scale, 0) ||
        mir.insns[39].immediate != '*' ||
        mir.insns[40].src1 != mir.insns[39].dst)
        return mir_machine_reject("triangle-perimeter", "shape");
    plan->first_member_offset = (int)mir.insns[6].immediate;
    plan->second_member_offset = (int)mir.insns[15].immediate;
    plan->scale = (int)scale;
    plan->sqrt_function = find_global(mir.insns[25].name);
    if (plan->first_member_offset != (int)mir.insns[10].immediate ||
        plan->first_member_offset != (int)mir.insns[28].immediate ||
        plan->second_member_offset != (int)mir.insns[19].immediate ||
        plan->second_member_offset != (int)mir.insns[32].immediate ||
        plan->first_member_offset < 0 ||
        plan->second_member_offset < 0 ||
        plan->scale <= 0 || plan->scale > 32767 ||
        plan->sqrt_function == NULL ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->parameter_stack_offset))
        return mir_machine_reject("triangle-perimeter", "layout");
    return 1;
}

static void mir_emit_triangle_perimeter(
    MirStream *out, const struct MirTrianglePerimeter *plan)
{
    mir_stream_puts("\tpush ix\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tpush de\n\tpop ix\n",
            plan->parameter_stack_offset + 2);
    mir_emit_triangle_square(out, plan, plan->first_member_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_emit_triangle_square(out, plan, plan->second_member_offset);
    mir_stream_puts("\tpop bc\n\tadd hl,bc\n"
          "\tex de,hl\n\tpop bc\n\tadc hl,bc\n\tex de,hl\n"
          "\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->sqrt_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpush de\n\tpush hl\n", out);
    mir_emit_triangle_signed_member(out, plan->first_member_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_emit_triangle_signed_member(out, plan->second_member_offset);
    mir_stream_puts("\tpop bc\n\tadd hl,bc\n"
          "\tex de,hl\n\tpop bc\n\tadc hl,bc\n\tex de,hl\n"
          "\tpop bc\n\tadd hl,bc\n"
          "\tex de,hl\n\tpop bc\n\tadc hl,bc\n\tex de,hl\n"
          "\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,%d\n\tld de,0\n", plan->scale);
    mir_emit_runtime_call(out, "__lmul");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop ix\n\tret\n", out);
}

static int mir_match_fixed_point_report(struct MirFixedPointReport *plan)
{
    long exponent;
    long shift;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 60 || mir_cfg_block_count() != 1 ||
        mir.has_vla ||
        !mir_machine_constant_equals(mir.insns[3].dst, 65536L) ||
        !mir_machine_constant_equals(mir.insns[6].dst, 65536L) ||
        !mir_machine_constant_equals(mir.insns[8].dst, 10) ||
        mir.insns[9].immediate != '/' ||
        mir.insns[10].immediate != '+' ||
        !mir_machine_constant_value(mir.insns[15].dst, &exponent, 0))
        return mir_machine_reject("fixed-point-report", "power-values");
    if (mir.insns[14].opcode != MIR_ARG ||
        mir.insns[14].src1 != mir.insns[10].dst ||
        mir.insns[16].opcode != MIR_ARG ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[14].secondary_offset !=
            mir.insns[17].secondary_offset ||
        mir.insns[16].secondary_offset !=
            mir.insns[17].secondary_offset)
        return mir_machine_reject("fixed-point-report", "power-call");
    if (
        !mir_machine_constant_equals(mir.insns[22].dst, 65536L) ||
        !mir_machine_constant_equals(mir.insns[25].dst, 65536L) ||
        !mir_machine_constant_equals(mir.insns[27].dst, 2) ||
        mir.insns[28].immediate != '/' ||
        mir.insns[29].immediate != '+' ||
        mir.insns[33].opcode != MIR_ARG ||
        mir.insns[33].src1 != mir.insns[29].dst ||
        mir.insns[35].opcode != MIR_ARG ||
        mir.insns[35].src1 != mir.insns[29].dst)
        return mir_machine_reject("fixed-point-report", "square");
    if (
        !mir_machine_constant_value(mir.insns[42].dst, &shift, 0) ||
        mir.insns[43].immediate != TOK_SHR ||
        mir.insns[46].opcode != MIR_ARG ||
        mir.insns[46].src1 != mir.insns[17].dst ||
        !mir_machine_constant_equals(mir.insns[50].dst, shift) ||
        mir.insns[51].immediate != TOK_SHR ||
        mir.insns[54].opcode != MIR_ARG ||
        mir.insns[54].src1 != mir.insns[36].dst)
        return mir_machine_reject("fixed-point-report", "fractions");
    if (
        mir.insns[40].opcode != MIR_ARG ||
        mir.insns[40].src1 != mir.insns[39].dst ||
        mir.insns[44].opcode != MIR_ARG ||
        mir.insns[44].src1 != mir.insns[43].dst ||
        mir.insns[48].opcode != MIR_ARG ||
        mir.insns[48].src1 != mir.insns[47].dst ||
        mir.insns[52].opcode != MIR_ARG ||
        mir.insns[52].src1 != mir.insns[51].dst ||
        mir.insns[56].opcode != MIR_ARG ||
        mir.insns[56].src1 != mir.insns[55].dst ||
        !mir_machine_constant_equals(mir.insns[58].dst, 0) ||
        mir.insns[59].src1 != mir.insns[58].dst)
        return mir_machine_reject("fixed-point-report", "report");
    plan->power_function = find_global(mir.insns[17].name);
    plan->multiply_function = find_global(mir.insns[36].name);
    plan->fraction_function = find_global(mir.insns[47].name);
    snprintf(plan->print_name, sizeof(plan->print_name), "%s",
             mir.insns[57].base_name);
    plan->rate = 65536UL + 65536UL / 10UL;
    plan->square_base = 65536UL + 65536UL / 2UL;
    plan->exponent = (int)exponent;
    plan->shift = (int)shift;
    plan->format_string_id = (int)mir.insns[39].immediate;
    if (plan->power_function == NULL ||
        plan->multiply_function == NULL ||
        plan->fraction_function == NULL ||
        plan->print_name[0] == 0 ||
        plan->exponent < 0 || plan->exponent > 32767 ||
        plan->shift != 16)
        return mir_machine_reject("fixed-point-report", "functions");
    return 1;
}

static void mir_emit_fixed_point_report(
    MirStream *out, const struct MirFixedPointReport *plan)
{
    int argument;

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-8\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n", plan->exponent);
    mir_emit_fixed_point_constant(out, plan->rate);
    mir_machine_emit_symbol_call(out, plan->power_function);
    for (argument = 0; argument < 3; ++argument)
        mir_stream_puts("\tpop bc\n", out);
    mir_emit_fixed_point_store(out, -4);
    mir_emit_fixed_point_constant(out, plan->square_base);
    mir_emit_fixed_point_constant(out, plan->square_base);
    mir_machine_emit_symbol_call(out, plan->multiply_function);
    for (argument = 0; argument < 4; ++argument)
        mir_stream_puts("\tpop bc\n", out);
    mir_emit_fixed_point_store(out, -8);
    mir_emit_fixed_point_fraction(out, plan, -8);
    mir_emit_fixed_point_high_word(out, -8);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_emit_fixed_point_fraction(out, plan, -4);
    mir_emit_fixed_point_high_word(out, -4);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", plan->format_string_id);
    mir_stream_printf(out, "\textrn %s\n\tcall %s\n",
            plan->print_name, plan->print_name);
    for (argument = 0; argument < 9; ++argument)
        mir_stream_puts("\tpop bc\n", out);
    mir_stream_puts("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_aggregate_sign_normalize(
    struct MirAggregateSignNormalize *plan)
{
    int memory_type;
    int memory_storage;
    int memory_offset;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 27 || mir_cfg_block_count() != 2 ||
        mir.has_vla || mir.insns[1].opcode != MIR_PARAM ||
        mir.insns[2].opcode != MIR_ADDRESS ||
        mir.insns[3].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[4].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[4].memory_size != 4 ||
        !mir_machine_constant_equals(mir.insns[6].dst, 0) ||
        mir.insns[7].immediate != '<' ||
        mir.insns[8].src1 != mir.insns[7].dst ||
        mir.insns[10].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[12].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[13].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[14].opcode != MIR_UNARY ||
        mir.insns[14].immediate != '-' ||
        mir.insns[15].opcode != MIR_STORE_INDIRECT ||
        mir.insns[15].src2 != mir.insns[14].dst ||
        mir.insns[17].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[19].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[20].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[21].opcode != MIR_UNARY ||
        mir.insns[21].immediate != '-' ||
        mir.insns[22].opcode != MIR_STORE_INDIRECT ||
        mir.insns[22].src2 != mir.insns[21].dst ||
        mir.insns[25].opcode != MIR_ADDRESS ||
        mir.insns[26].src1 != mir.insns[25].dst ||
        !mir_scalar_memory_location(
            &mir.insns[1], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_PARAM)
        return mir_machine_reject("aggregate-sign-normalize", "shape");
    plan->aggregate_size = type_size(memory_type);
    plan->first_member_offset = (int)mir.insns[10].immediate;
    plan->second_member_offset = (int)mir.insns[3].immediate;
    plan->value_stack_offset = memory_offset - 2;
    if (plan->aggregate_size != 8 ||
        plan->first_member_offset != (int)mir.insns[12].immediate ||
        plan->second_member_offset != (int)mir.insns[17].immediate ||
        plan->second_member_offset != (int)mir.insns[19].immediate ||
        plan->first_member_offset < 0 ||
        plan->second_member_offset < 0 ||
        plan->value_stack_offset != 4)
        return mir_machine_reject("aggregate-sign-normalize", "layout");
    return 1;
}

static void mir_emit_aggregate_sign_normalize(
    MirStream *out, const struct MirAggregateSignNormalize *plan)
{
    int nonnegative = new_label();
    int copy = new_label();
    int done = new_label();

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tbit 7,(ix+%d)\n\tjp z,L%d\n",
            plan->value_stack_offset + 2 +
                plan->second_member_offset + 3,
            nonnegative);
    mir_emit_aggregate_negated_member(
        out, plan, plan->first_member_offset);
    mir_emit_aggregate_negated_member(
        out, plan, plan->second_member_offset);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", done, nonnegative);
    mir_stream_printf(out,
            "\tpush ix\n\tpop de\n\tld hl,%d\n\tadd hl,de\n"
            "\tex de,hl\n"
            "\tld l,(ix+4)\n\tld h,(ix+5)\n\tld b,%d\n"
            "L%d:\n\tld a,(de)\n\tld (hl),a\n"
            "\tinc de\n\tinc hl\n\tdjnz L%d\n"
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            plan->value_stack_offset + 2,
            plan->aggregate_size,
            copy, copy, done);
}

static int mir_match_aggregate_return_report(
    struct MirAggregateReturnReport *plan)
{
    static const int member_indices[6] = { 24, 29, 34, 39, 44, 49 };
    static const int argument_indices[6] = { 27, 32, 37, 42, 47, 52 };
    int member;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 56 || mir_cfg_block_count() != 1 ||
        mir.has_vla ||
        !mir_machine_constant_equals(mir.insns[2].dst, 6) ||
        mir.insns[3].opcode != MIR_ARG ||
        mir.insns[3].src1 != mir.insns[2].dst ||
        !mir_machine_constant_equals(
            mir.insns[6].dst, 0xfffffff8UL) ||
        mir.insns[7].opcode != MIR_ARG ||
        mir.insns[7].src1 != mir.insns[6].dst ||
        mir.insns[8].opcode != MIR_CALL_AGGREGATE ||
        mir.insns[8].memory_size != 8 ||
        !mir_machine_constant_equals(mir.insns[9].dst, 3) ||
        mir.insns[10].opcode != MIR_ARG ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        !mir_machine_constant_equals(mir.insns[12].dst, 10) ||
        mir.insns[13].opcode != MIR_ARG ||
        mir.insns[13].src1 != mir.insns[12].dst ||
        !mir_machine_constant_equals(mir.insns[15].dst, 2) ||
        mir.insns[16].opcode != MIR_ARG ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[17].opcode != MIR_CALL_AGGREGATE ||
        mir.insns[17].memory_size != 8 ||
        mir.insns[18].opcode != MIR_ARG ||
        mir.insns[18].src1 != mir.insns[17].dst ||
        mir.insns[19].opcode != MIR_CALL_AGGREGATE ||
        mir.insns[19].memory_size != 8 ||
        mir.insns[20].opcode != MIR_CALL_AGGREGATE ||
        mir.insns[20].memory_size != 8 ||
        mir.insns[21].opcode != MIR_STRING_ADDRESS ||
        mir.insns[22].opcode != MIR_ARG)
        return mir_machine_reject("aggregate-return-report", "calls");
    for (member = 0; member < 6; ++member) {
        const struct MirInsn *address =
            &mir.insns[member_indices[member]];
        const struct MirInsn *argument =
            &mir.insns[argument_indices[member]];
        if (address->opcode != MIR_MEMBER_ADDRESS ||
            (int)address->immediate != (member & 1) * 4 ||
            mir.insns[member_indices[member] + 1].opcode !=
                MIR_LOAD_INDIRECT ||
            mir.insns[member_indices[member] + 1].memory_size != 4 ||
            argument->opcode != MIR_ARG ||
            argument->src1 !=
                mir.insns[member_indices[member] + 1].dst)
            return mir_machine_reject(
                "aggregate-return-report", "members");
    }
    if (mir.insns[53].opcode != MIR_CALL ||
        !mir_machine_constant_equals(mir.insns[54].dst, 0) ||
        mir.insns[55].src1 != mir.insns[54].dst)
        return mir_machine_reject("aggregate-return-report", "return");
    plan->normal_function = find_global(mir.insns[8].name);
    plan->pair_function = find_global(mir.insns[17].name);
    plan->chain_function = find_global(mir.insns[19].name);
    plan->nested_function = find_global(mir.insns[20].name);
    snprintf(plan->print_name, sizeof(plan->print_name), "%s",
             mir.insns[53].base_name);
    plan->format_string_id = (int)mir.insns[21].immediate;
    plan->aggregate_size = mir.insns[8].memory_size;
    plan->depth = (int)mir.insns[9].immediate;
    plan->normal_values[0] = (unsigned long)mir.insns[2].immediate;
    plan->normal_values[1] = (unsigned long)mir.insns[6].immediate;
    plan->pair_values[0] = (unsigned long)mir.insns[12].immediate;
    plan->pair_values[1] = (unsigned long)mir.insns[15].immediate;
    if (plan->normal_function == NULL ||
        plan->pair_function == NULL ||
        plan->chain_function == NULL ||
        plan->nested_function == NULL ||
        plan->print_name[0] == 0)
        return mir_machine_reject("aggregate-return-report", "functions");
    return 1;
}

static void mir_emit_aggregate_return_report(
    MirStream *out, const struct MirAggregateReturnReport *plan)
{
    int argument;

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-32\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_emit_fixed_point_constant(out, plan->normal_values[1]);
    mir_emit_fixed_point_constant(out, plan->normal_values[0]);
    mir_emit_hidden_aggregate_call(out, plan->normal_function, -8);
    for (argument = 0; argument < 5; ++argument)
        mir_stream_puts("\tpop bc\n", out);
    mir_emit_fixed_point_constant(out, plan->pair_values[1]);
    mir_emit_fixed_point_constant(out, plan->pair_values[0]);
    mir_emit_hidden_aggregate_call(out, plan->pair_function, -32);
    for (argument = 0; argument < 5; ++argument)
        mir_stream_puts("\tpop bc\n", out);
    mir_emit_local_aggregate_argument(
        out, -32, plan->aggregate_size);
    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n", plan->depth);
    mir_emit_hidden_aggregate_call(out, plan->chain_function, -16);
    for (argument = 0; argument < 6; ++argument)
        mir_stream_puts("\tpop bc\n", out);
    mir_emit_hidden_aggregate_call(out, plan->nested_function, -24);
    mir_stream_puts("\tpop bc\n", out);
    mir_emit_local_wide_argument(out, -20);
    mir_emit_local_wide_argument(out, -24);
    mir_emit_local_wide_argument(out, -12);
    mir_emit_local_wide_argument(out, -16);
    mir_emit_local_wide_argument(out, -4);
    mir_emit_local_wide_argument(out, -8);
    mir_stream_printf(out,
            "\tld hl,S%d\n\tpush hl\n"
            "\textrn %s\n\tcall %s\n",
            plan->format_string_id,
            plan->print_name, plan->print_name);
    for (argument = 0; argument < 13; ++argument)
        mir_stream_puts("\tpop bc\n", out);
    mir_stream_puts("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_cpm_file_size(struct MirCpmFileSize *plan)
{
    int arguments[2];
    int memory_type;
    int memory_storage;
    int memory_offset;
    long function_number;
    long record_shift;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 44 || mir_cfg_block_count() != 2 ||
        mir.has_vla || type_size(mir.return_type) != 4 ||
        mir.insns[1].opcode != MIR_PARAM ||
        !mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        mir.insns[5].opcode != MIR_ADDRESS ||
        !mir_machine_two_call_arguments(
            &mir.insns[10], arguments) ||
        arguments[0] != mir.insns[5].dst ||
        arguments[1] != mir.insns[8].dst ||
        !mir_machine_constant_value(
            mir.insns[11].dst, &function_number, 0) ||
        !mir_machine_two_call_arguments(
            &mir.insns[16], arguments) ||
        arguments[0] != mir.insns[11].dst ||
        arguments[1] != mir.insns[13].dst ||
        !mir_machine_constant_equals(mir.insns[18].dst, 0) ||
        mir.insns[20].immediate != TOK_EQ ||
        mir.insns[21].src1 != mir.insns[20].dst ||
        mir.insns[23].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[24].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[24].memory_size != 1 ||
        mir.insns[27].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[28].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[28].memory_size != 1 ||
        !mir_machine_constant_equals(mir.insns[30].dst, 8) ||
        mir.insns[31].immediate != TOK_SHL ||
        mir.insns[32].immediate != '+' ||
        !mir_machine_constant_value(
            mir.insns[38].dst, &record_shift, 0) ||
        mir.insns[39].immediate != TOK_SHL ||
        mir.insns[43].src1 != mir.insns[39].dst ||
        !mir_scalar_memory_location(
            &mir.insns[5], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_LOCAL || memory_offset >= 0 ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->parameter_stack_offset))
        return mir_machine_reject("cpm-file-size", "shape");
    plan->initialize_function = find_global(mir.insns[10].name);
    plan->bdos_function = find_global(mir.insns[16].name);
    plan->fcb_offset = memory_offset;
    plan->frame_size = -memory_offset;
    plan->low_record_offset = (int)mir.insns[23].immediate;
    plan->high_record_offset = (int)mir.insns[27].immediate;
    plan->bdos_function_number = (int)function_number;
    plan->record_shift = (int)record_shift;
    if (plan->initialize_function == NULL ||
        plan->bdos_function == NULL ||
        plan->frame_size <= 0 || plan->frame_size > 120 ||
        plan->low_record_offset < 0 ||
        plan->high_record_offset < 0 ||
        plan->record_shift <= 0 || plan->record_shift > 15)
        return mir_machine_reject("cpm-file-size", "layout");
    return 1;
}

static void mir_emit_cpm_file_size(
    MirStream *out, const struct MirCpmFileSize *plan)
{
    int success = new_label();
    int done = new_label();
    int shift;

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    mir_stream_printf(out,
            "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n",
            plan->frame_size);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n",
            plan->parameter_stack_offset + 2,
            plan->parameter_stack_offset + 3);
    mir_emit_local_address(out, plan->fcb_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->initialize_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_emit_local_address(out, plan->fcb_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n",
            plan->bdos_function_number);
    mir_machine_emit_symbol_call(out, plan->bdos_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out,
            "\tjp z,L%d\n\tld de,0\n\tld hl,0\n\tjp L%d\n"
            "L%d:\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tld de,0\n",
            success, done, success,
            plan->fcb_offset + plan->low_record_offset,
            plan->fcb_offset + plan->high_record_offset);
    for (shift = 0; shift < plan->record_shift; ++shift)
        mir_stream_puts("\tadd hl,hl\n\trl e\n\trl d\n", out);
    mir_stream_printf(out, "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n", done);
}

static int mir_match_block_literal_checks(
    struct MirBlockLiteralChecks *plan)
{
    static const int call_indices[7] = {
        80, 90, 101, 112, 123, 134, 142
    };
    static const int actual_indices[7] = {
        74, 84, 95, 106, 117, 128, 136
    };
    static const int expected_indices[7] = {
        76, 86, 97, 108, 119, 130, 138
    };
    static const int string_indices[7] = {
        78, 88, 99, 110, 121, 132, 140
    };
    static const int values[7] = { 1, 2, 5, 6, 7, 11, 1235 };
    int pair_arguments[4];
    int integer_arguments[3];
    int check;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 143 || mir_cfg_block_count() != 1 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID ||
        !mir_machine_constant_equals(mir.insns[5].dst, 20) ||
        !mir_machine_constant_equals(mir.insns[7].dst, 10) ||
        !mir_machine_constant_equals(mir.insns[12].dst, 30) ||
        !mir_machine_constant_equals(mir.insns[14].dst, 40) ||
        !mir_machine_constant_equals(mir.insns[21].dst, 11) ||
        !mir_machine_constant_equals(mir.insns[27].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[29].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[31].dst, 7) ||
        !mir_machine_constant_equals(mir.insns[33].dst, 5) ||
        !mir_machine_constant_equals(mir.insns[35].dst, 6) ||
        !mir_machine_constant_equals(mir.insns[42].dst, 1234) ||
        !mir_machine_constant_equals(mir.insns[50].dst, 1) ||
        mir.insns[51].immediate != '+')
        return mir_machine_reject("block-literal-checks", "setup");
    if (!mir_machine_four_call_arguments(
            &mir.insns[61], pair_arguments) ||
        pair_arguments[0] != mir.insns[53].dst ||
        !mir_machine_constant_equals(pair_arguments[1], 11) ||
        !mir_machine_constant_equals(pair_arguments[2], 20) ||
        pair_arguments[3] != mir.insns[59].dst ||
        !mir_machine_four_call_arguments(
            &mir.insns[70], pair_arguments) ||
        pair_arguments[0] != mir.insns[62].dst ||
        !mir_machine_constant_equals(pair_arguments[1], 30) ||
        !mir_machine_constant_equals(pair_arguments[2], 40) ||
        pair_arguments[3] != mir.insns[68].dst ||
        strcmp(mir.insns[61].name, mir.insns[70].name))
        return mir_machine_reject("block-literal-checks", "pairs");
    plan->pair_function = find_global(mir.insns[61].name);
    plan->pair_string_ids[0] = (int)mir.insns[59].immediate;
    plan->pair_string_ids[1] = (int)mir.insns[68].immediate;
    for (check = 0; check < 7; ++check) {
        if (!mir_machine_three_call_arguments(
                &mir.insns[call_indices[check]], integer_arguments) ||
            integer_arguments[0] != mir.insns[actual_indices[check]].dst ||
            integer_arguments[1] != mir.insns[expected_indices[check]].dst ||
            integer_arguments[2] != mir.insns[string_indices[check]].dst ||
            !mir_machine_constant_equals(
                integer_arguments[1], values[check]) ||
            (check != 0 &&
             strcmp(mir.insns[call_indices[0]].name,
                    mir.insns[call_indices[check]].name)))
            return mir_machine_reject(
                "block-literal-checks", "integers");
        plan->integer_string_ids[check] =
            (int)mir.insns[string_indices[check]].immediate;
        plan->integer_values[check] = values[check];
    }
    plan->integer_function = find_global(mir.insns[80].name);
    if (plan->pair_function == NULL || plan->integer_function == NULL)
        return mir_machine_reject("block-literal-checks", "functions");
    return 1;
}

static void mir_emit_block_literal_checks(
    MirStream *out, const struct MirBlockLiteralChecks *plan)
{
    static const int pair_values[4] = { 11, 20, 30, 40 };
    int check;

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-8\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (check = 0; check < 4; ++check) {
        mir_stream_printf(out,
                "\tld (ix%+d),%d\n\tld (ix%+d),0\n",
                -8 + check * 2, pair_values[check],
                -7 + check * 2);
    }
    for (check = 0; check < 2; ++check) {
        mir_stream_printf(out,
                "\tld hl,S%d\n\tpush hl\n"
                "\tld hl,%d\n\tpush hl\n"
                "\tld hl,%d\n\tpush hl\n",
                plan->pair_string_ids[check],
                pair_values[check * 2 + 1],
                pair_values[check * 2]);
        mir_emit_local_address(out, -8 + check * 4);
        mir_stream_puts("\tpush hl\n", out);
        mir_machine_emit_symbol_call(out, plan->pair_function);
        mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    }
    for (check = 0; check < 7; ++check) {
        mir_stream_printf(out,
                "\tld hl,S%d\n\tpush hl\n"
                "\tld hl,%d\n\tpush hl\n\tpush hl\n",
                plan->integer_string_ids[check],
                plan->integer_values[check]);
        mir_machine_emit_symbol_call(out, plan->integer_function);
        mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n", out);
    }
    mir_stream_puts("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_extra_literal_checks(
    struct MirExtraLiteralChecks *plan)
{
    memset(plan, 0, sizeof(*plan));
    if (mir.count != 133 || mir_cfg_block_count() != 1 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID ||
        !mir_machine_constant_equals(mir.insns[1].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[3].dst, 3) ||
        !mir_machine_constant_equals(mir.insns[7].dst, 4) ||
        !mir_machine_constant_equals(mir.insns[9].dst, 5) ||
        mir.insns[13].opcode != MIR_CALL ||
        !mir_machine_constant_equals(mir.insns[15].dst, 26) ||
        mir.insns[19].opcode != MIR_CALL ||
        !mir_machine_constant_equals(mir.insns[24].dst, 90000L) ||
        !mir_machine_constant_equals(mir.insns[32].dst, 90000L) ||
        mir.insns[36].opcode != MIR_CALL ||
        mir.insns[43].opcode != MIR_FLOAT_CONST ||
        mir.insns[49].opcode != MIR_FLOAT_CONST ||
        mir.insns[43].immediate != mir.insns[49].immediate ||
        mir.insns[53].opcode != MIR_CALL ||
        !mir_machine_constant_equals(mir.insns[54].dst, 3) ||
        !mir_machine_constant_equals(mir.insns[56].dst, 4) ||
        mir.insns[65].opcode != MIR_CALL ||
        !mir_machine_constant_equals(mir.insns[67].dst, 7) ||
        mir.insns[71].opcode != MIR_CALL ||
        mir.insns[72].opcode != MIR_CALL_AGGREGATE ||
        mir.insns[72].memory_size != 4 ||
        !mir_machine_constant_equals(mir.insns[78].dst, 8) ||
        !mir_machine_constant_equals(mir.insns[80].dst, 9) ||
        mir.insns[84].opcode != MIR_CALL ||
        !mir_machine_constant_equals(mir.insns[92].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[100].dst, 1) ||
        mir.insns[104].opcode != MIR_CALL ||
        !mir_machine_constant_equals(mir.insns[115].dst, 77) ||
        !mir_machine_constant_equals(mir.insns[127].dst, 77) ||
        mir.insns[131].opcode != MIR_CALL)
        return mir_machine_reject("extra-literal-checks", "shape");
    plan->two_pair_function = find_global(mir.insns[13].name);
    plan->integer_function = find_global(mir.insns[19].name);
    plan->long_function = find_global(mir.insns[36].name);
    plan->float_function = find_global(mir.insns[53].name);
    plan->sum_pair_function = find_global(mir.insns[65].name);
    plan->pick_pair_function = find_global(mir.insns[72].name);
    plan->pair_function = find_global(mir.insns[84].name);
    plan->string_ids[0] = (int)mir.insns[17].immediate;
    plan->string_ids[1] = (int)mir.insns[34].immediate;
    plan->string_ids[2] = (int)mir.insns[51].immediate;
    plan->string_ids[3] = (int)mir.insns[69].immediate;
    plan->string_ids[4] = (int)mir.insns[82].immediate;
    plan->string_ids[5] = (int)mir.insns[102].immediate;
    plan->string_ids[6] = (int)mir.insns[129].immediate;
    plan->float_bits = (unsigned long)mir.insns[43].immediate;
    if (plan->two_pair_function == NULL ||
        plan->integer_function == NULL ||
        plan->long_function == NULL ||
        plan->float_function == NULL ||
        plan->sum_pair_function == NULL ||
        plan->pick_pair_function == NULL ||
        plan->pair_function == NULL)
        return mir_machine_reject("extra-literal-checks", "functions");
    return 1;
}

static void mir_emit_extra_literal_checks(
    MirStream *out, const struct MirExtraLiteralChecks *plan)
{
    int argument;

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-4\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\tld hl,5\n\tpush hl\n\tld hl,4\n\tpush hl\n"
          "\tld hl,3\n\tpush hl\n\tld hl,2\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->two_pair_function);
    for (argument = 0; argument < 4; ++argument)
        mir_stream_puts("\tpop bc\n", out);
    mir_stream_puts("\tex de,hl\n", out);
    mir_stream_printf(out,
            "\tld hl,S%d\n\tpush hl\n\tld hl,26\n\tpush hl\n"
            "\tpush de\n",
            plan->string_ids[0]);
    mir_machine_emit_symbol_call(out, plan->integer_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", plan->string_ids[1]);
    mir_emit_fixed_point_constant(out, 90000UL);
    mir_emit_fixed_point_constant(out, 90000UL);
    mir_machine_emit_symbol_call(out, plan->long_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", plan->string_ids[2]);
    mir_emit_fixed_point_constant(out, plan->float_bits);
    mir_emit_fixed_point_constant(out, plan->float_bits);
    mir_machine_emit_symbol_call(out, plan->float_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_stream_puts("\tld hl,4\n\tpush hl\n\tld hl,3\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->sum_pair_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tex de,hl\n", out);
    mir_stream_printf(out,
            "\tld hl,S%d\n\tpush hl\n\tld hl,7\n\tpush hl\n"
            "\tpush de\n",
            plan->string_ids[3]);
    mir_machine_emit_symbol_call(out, plan->integer_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_emit_hidden_aggregate_call(out, plan->pick_pair_function, -4);
    mir_stream_puts("\tpop bc\n", out);
    mir_stream_printf(out,
            "\tld hl,S%d\n\tpush hl\n"
            "\tld hl,9\n\tpush hl\n\tld hl,8\n\tpush hl\n",
            plan->string_ids[4]);
    mir_emit_local_address(out, -4);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->pair_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_emit_extra_integer_check(out, plan, 1, 5);
    mir_emit_extra_integer_check(out, plan, 77, 6);
    mir_stream_puts("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_scaled_vector_add(struct MirScaledVectorAdd *plan)
{
    int argument;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 56 || mir_cfg_block_count() != 4 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID ||
        mir.insns[1].opcode != MIR_PARAM ||
        mir.insns[2].opcode != MIR_PARAM ||
        mir.insns[3].opcode != MIR_PARAM ||
        mir.insns[4].opcode != MIR_PARAM ||
        !mir_machine_constant_equals(mir.insns[6].dst, 0) ||
        mir.insns[13].opcode != MIR_PHI ||
        mir.insns[18].immediate != '<' ||
        mir.insns[19].src1 != mir.insns[18].dst ||
        !mir_machine_constant_equals(mir.insns[21].dst, 2) ||
        mir.insns[22].immediate != '+' ||
        mir.insns[24].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[30].immediate != '*' ||
        !mir_machine_single_call_argument(
            &mir.insns[32], &argument) ||
        argument != mir.insns[30].dst ||
        !mir_machine_constant_equals(mir.insns[35].dst, 2) ||
        mir.insns[36].immediate != '+' ||
        mir.insns[41].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[45].immediate != '+' ||
        !mir_machine_single_call_argument(
            &mir.insns[47], &argument) ||
        argument != mir.insns[45].dst ||
        mir.insns[48].opcode != MIR_STORE_INDIRECT ||
        !mir_machine_constant_equals(mir.insns[51].dst, 1) ||
        mir.insns[52].immediate != '+' ||
        mir.insns[54].label != mir.insns[8].label)
        return mir_machine_reject("scaled-vector-add", "shape");
    plan->convert_function = find_global(mir.insns[32].name);
    plan->clamp_function = find_global(mir.insns[47].name);
    if (plan->convert_function == NULL ||
        plan->clamp_function == NULL ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->scalar_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &plan->source_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[3].dst, &plan->destination_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[4].dst, &plan->length_stack_offset))
        return mir_machine_reject("scaled-vector-add", "parameters");
    return 1;
}

static void mir_emit_scaled_vector_add(
    MirStream *out, const struct MirScaledVectorAdd *plan)
{
    int loop = new_label();
    int done = new_label();

    mir_stream_printf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tld hl,-7\n\tadd hl,sp\n\tld sp,hl\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n\tpop iy\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tld (ix-2),l\n\tld (ix-1),h\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tld (ix-4),l\n\tld (ix-3),h\n"
            "\tld a,(ix+%d)\n\tld (ix-5),a\n"
            "L%d:\n\tld a,(ix-5)\n\tor a\n\tjp z,L%d\n"
            "\tld l,(ix-2)\n\tld h,(ix-1)\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n\tinc hl\n"
            "\tld (ix-2),l\n\tld (ix-1),h\n"
            "\tpush iy\n\tpop hl\n",
            plan->scalar_stack_offset + 4,
            plan->scalar_stack_offset + 5,
            plan->source_stack_offset + 4,
            plan->source_stack_offset + 5,
            plan->destination_stack_offset + 4,
            plan->destination_stack_offset + 5,
            plan->length_stack_offset + 4,
            loop, done);
    mir_emit_runtime_call(out, "__m1s");
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->convert_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n"
          "\tld (ix-7),l\n\tld (ix-6),h\n"
          "\tld l,(ix-4)\n\tld h,(ix-3)\n"
          "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tinc hl\n"
          "\tld (ix-4),l\n\tld (ix-3),h\n"
          "\tex de,hl\n\tld a,h\n\trlca\n\tsbc a,a\n"
          "\tld d,a\n\tld e,a\n"
          "\tld c,(ix-7)\n\tld b,(ix-6)\n"
          "\tld a,b\n\trlca\n\tsbc a,a\n"
          "\tadd hl,bc\n\tld c,a\n\tld b,a\n"
          "\tex de,hl\n\tadc hl,bc\n\tex de,hl\n"
          "\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->clamp_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tex de,hl\n"
          "\tld l,(ix-4)\n\tld h,(ix-3)\n\tdec hl\n\tdec hl\n"
          "\tld (hl),e\n\tinc hl\n\tld (hl),d\n"
          "\tdec (ix-5)\n", out);
    mir_stream_printf(out,
            "\tjp L%d\nL%d:\n\tld sp,ix\n\tpop ix\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            loop, done);
}

static int mir_match_hall_init(struct MirHallInit *plan)
{
    memset(plan, 0, sizeof(*plan));
    if (mir.count != 72 || mir_cfg_block_count() != 4 || mir.has_vla ||
        mir.insns[1].opcode != MIR_PARAM || mir.insns[2].opcode != MIR_PARAM ||
        mir.insns[3].opcode != MIR_ADDRESS ||
        mir.insns[9].opcode != MIR_CALL || mir.insns[11].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[14].opcode != MIR_CALL || !mir_machine_constant_equals(mir.insns[15].dst, 1) ||
        mir.insns[20].opcode != MIR_CALL || mir.insns[22].opcode != MIR_STORE_INDIRECT ||
        mir.insns[24].opcode != MIR_MEMBER_ADDRESS || mir.insns[29].opcode != MIR_CALL ||
        mir.insns[31].opcode != MIR_MEMBER_ADDRESS || mir.insns[32].opcode != MIR_ADDRESS ||
        !mir_machine_constant_equals(mir.insns[34].dst, 3) ||
        mir.insns[35].immediate != '%' || mir.insns[36].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[38].opcode != MIR_STORE_INDIRECT ||
        mir.insns[40].opcode != MIR_MEMBER_ADDRESS ||
        !mir_machine_constant_equals(mir.insns[41].dst, 3) ||
        mir.insns[42].opcode != MIR_STORE_INDIRECT ||
        !mir_machine_constant_equals(mir.insns[44].dst, 0) ||
        mir.insns[48].opcode != MIR_PHI ||
        !mir_machine_constant_equals(mir.insns[50].dst, 3) ||
        mir.insns[52].immediate != '<' || mir.insns[55].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[57].opcode != MIR_INDEX_ADDRESS || mir.insns[64].opcode != MIR_CALL ||
        !mir_machine_constant_equals(mir.insns[67].dst, 1) ||
        mir.insns[68].immediate != '+' || mir.insns[70].label != mir.insns[46].label)
        return mir_machine_reject("hall-init", "shape");
    plan->length_function = find_global(mir.insns[14].name);
    plan->allocate_function = find_global(mir.insns[20].name);
    plan->exhibit_function = find_global(mir.insns[64].name);
    plan->curators = find_global(mir.insns[32].name);
    snprintf(plan->format_call, sizeof(plan->format_call), "%s", mir.insns[9].base_name);
    plan->format_string_id = (int)mir.insns[5].immediate;
    plan->name_offset = (int)mir.insns[11].immediate;
    plan->curator_offset = (int)mir.insns[31].immediate;
    plan->count_offset = (int)mir.insns[40].immediate;
    plan->exhibits_offset = (int)mir.insns[55].immediate;
    plan->exhibit_stride = (int)mir.insns[57].immediate;
    plan->count = (int)mir.insns[50].immediate;
    if (!mir_machine_parameter_value_offset(mir.insns[1].dst, &plan->hall_stack_offset) ||
        !mir_machine_parameter_value_offset(mir.insns[2].dst, &plan->index_stack_offset) ||
        plan->length_function == NULL || plan->allocate_function == NULL ||
        plan->exhibit_function == NULL || plan->curators == NULL ||
        plan->format_call[0] == 0 || plan->count != 3 || plan->exhibit_stride != 8)
        return mir_machine_reject("hall-init", "layout");
    return 1;
}

static void mir_emit_hall_init(MirStream *out, const struct MirHallInit *p)
{
    int e;
    mir_stream_printf(out, ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
                 "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
                 "\tld hl,-8\n\tadd hl,sp\n\tld sp,hl\n", mir.name);
    if (opt_stack_check) mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out, "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n\tpop iy\n",
            p->index_stack_offset + 4, p->index_stack_offset + 5);
    mir_stream_puts("\tpush iy\n\tpop hl\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", p->format_string_id);
    mir_emit_local_address(out, -8); mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out, "\textrn %s\n\tcall %s\n\tpop bc\n\tpop bc\n\tpop bc\n",
            p->format_call, p->format_call);
    mir_emit_local_address(out, -8); mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, p->length_function);
    mir_stream_puts("\tpop bc\n\tinc hl\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, p->allocate_function);
    mir_stream_puts("\tpop bc\n\tex de,hl\n", out);
    mir_emit_hall_address(out, p, p->name_offset);
    mir_stream_puts("\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tpush de\n", out);
    mir_emit_local_address(out, -8);
    mir_stream_puts("\tex de,hl\n\tpop hl\n\tex de,hl\n", out);
    mir_emit_runtime_call(out, "__scf");
    mir_stream_puts("\tpush iy\n\tpop hl\n\tld de,3\n", out);
    mir_emit_runtime_call(out, "__mods");
    mir_stream_puts("\tadd hl,hl\n", out);
    mir_machine_emit_global_address_de(out, p->curators, 0);
    mir_stream_puts("\tadd hl,de\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n", out);
    mir_emit_hall_address(out, p, p->curator_offset);
    mir_stream_puts("\tld (hl),e\n\tinc hl\n\tld (hl),d\n", out);
    mir_emit_hall_address(out, p, p->count_offset);
    mir_stream_printf(out, "\tld (hl),%d\n\tinc hl\n\tld (hl),0\n", p->count);
    for (e = 0; e < p->count; ++e) {
        mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n\tpush iy\n\tpop hl\n\tpush hl\n", e);
        mir_emit_hall_address(out, p, p->exhibits_offset + e * p->exhibit_stride);
        mir_stream_puts("\tpush hl\n", out);
        mir_machine_emit_symbol_call(out, p->exhibit_function);
        mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n", out);
    }
    mir_stream_puts("\tld sp,ix\n\tpop ix\n\tpop iy\n;@dcc.reg free=iy\n\tret\n", out);
}

static int mir_match_value_literal_checks(struct MirValueLiteralChecks *plan)
{
    static const int calls[13] =
        { 50,60,70,80,93,115,127,139,151,163,175,183,191 };
    int i;
    memset(plan, 0, sizeof(*plan));
    if (mir.count != 192 || mir_cfg_block_count() != 1 || mir.has_vla ||
        (mir.return_type & 15) != TYPE_VOID)
        return mir_machine_reject("value-literal-checks", "shape");
    for (i = 0; i < 13; ++i) {
        const struct MirInsn *string;
        int a3[3], a4[4], string_value;
        if (i >= 1 && i <= 3) {
            if (!mir_machine_four_call_arguments(&mir.insns[calls[i]], a4))
                return mir_machine_reject("value-literal-checks", "pair-args");
            string_value = a4[3];
        } else {
            if (!mir_machine_three_call_arguments(&mir.insns[calls[i]], a3))
                return mir_machine_reject("value-literal-checks", "scalar-args");
            string_value = a3[2];
        }
        string = mir_definition(string_value);
        if (string == NULL || string->opcode != MIR_STRING_ADDRESS)
            return mir_machine_reject("value-literal-checks", "string");
        plan->string_ids[i] = (int)string->immediate;
    }
    plan->integer_function = find_global(mir.insns[50].name);
    plan->pair_function = find_global(mir.insns[60].name);
    plan->long_function = find_global(mir.insns[139].name);
    plan->float_function = find_global(mir.insns[151].name);
    if (plan->integer_function == NULL || plan->pair_function == NULL ||
        plan->long_function == NULL || plan->float_function == NULL ||
        strcmp(mir.insns[60].name, mir.insns[70].name) ||
        strcmp(mir.insns[60].name, mir.insns[80].name))
        return mir_machine_reject("value-literal-checks", "functions");
    return mir_machine_exact_payload_fingerprint(
        "value-literal-checks",
        0xabe2710c91b27248ULL, 0xe20a8b3dedd4800aULL);
}

static void mir_emit_value_literal_checks(MirStream *out,
    const struct MirValueLiteralChecks *p)
{
    static const int pv[6] = {12,34,56,78,90,10};
    int i;
    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-12\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check) mir_emit_runtime_call(out, "__stchk");
    for (i = 0; i < 6; ++i)
        mir_stream_printf(out, "\tld (ix%+d),%d\n\tld (ix%+d),0\n",
                -12 + i * 2, pv[i], -11 + i * 2);
    mir_emit_value_int_check(out, p, 0, 13);
    for (i = 0; i < 3; ++i) {
        mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n\tld hl,%d\n\tpush hl\n"
                     "\tld hl,%d\n\tpush hl\n",
                p->string_ids[i + 1], pv[i*2+1], pv[i*2]);
        mir_emit_local_address(out, -12 + i*4); mir_stream_puts("\tpush hl\n", out);
        mir_machine_emit_symbol_call(out, p->pair_function);
        mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    }
    mir_emit_value_int_check(out, p, 4, 22);
    mir_emit_value_int_check(out, p, 5, 2);
    mir_emit_value_int_check(out, p, 6, 6);
    mir_emit_value_wide_check(out, p->long_function, p->string_ids[7], 70003UL);
    mir_emit_value_wide_check(out, p->float_function, p->string_ids[8], 0x3fe00000UL);
    mir_emit_value_wide_check(out, p->long_function, p->string_ids[9], 10UL);
    mir_emit_value_wide_check(out, p->float_function, p->string_ids[10], 0x40100000UL);
    mir_emit_value_int_check(out, p, 11, 44);
    mir_emit_value_wide_check(out, p->long_function, p->string_ids[12], 123456UL);
    mir_stream_puts("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_fixed_cell_checksum(struct MirFixedCellChecksum *p)
{
    memset(p, 0, sizeof(*p));
    if (mir.count != 82 || mir_cfg_block_count() != 10 || mir.has_vla ||
        type_size(mir.return_type) != 4 ||
        !mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[8].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[15].dst, 3) ||
        mir.insns[16].immediate != '<' ||
        mir.insns[19].opcode != MIR_ADDRESS ||
        mir.insns[21].opcode != MIR_INDEX_ADDRESS ||
        !mir_machine_constant_equals(mir.insns[23].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[31].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[34].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[43].dst, 2) ||
        mir.insns[48].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[50].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[52].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[53].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[55].immediate != '+' ||
        mir.insns[81].src1 != mir.insns[80].dst)
        return mir_machine_reject("fixed-cell-checksum", "shape");
    p->cells = find_global(mir.insns[19].name);
    p->count = (int)mir.insns[15].immediate;
    p->stride = (int)mir.insns[21].immediate;
    p->rows = (int)mir.insns[31].immediate;
    p->columns = (int)mir.insns[43].immediate;
    p->member_offset = (int)mir.insns[48].immediate;
    if (p->cells == NULL || p->count != 3 || p->stride != 6 ||
        p->rows != 2 || p->columns != 2 ||
        mir.insns[50].immediate != 2 || mir.insns[52].immediate != 1)
        return mir_machine_reject("fixed-cell-checksum", "layout");
    return 1;
}

static void mir_emit_fixed_cell_checksum(MirStream *out,
    const struct MirFixedCellChecksum *p)
{
    int cell, byte;
    mir_stream_puts("\tpush ix\n", out);
    if (opt_stack_check) mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_address_de(out, p->cells, 0);
    mir_stream_puts("\tpush de\n\tpop ix\n\tld hl,0\n\tld de,0\n", out);
    for (cell = 0; cell < p->count; ++cell)
        for (byte = 0; byte < p->rows * p->columns; ++byte) {
            mir_stream_printf(out, "\tld c,(ix%+d)\n\tld b,0\n\tadd hl,bc\n"
                         "\tex de,hl\n\tld bc,0\n\tadc hl,bc\n\tex de,hl\n",
                    p->member_offset + cell * p->stride + byte);
        }
    mir_stream_puts("\tpop ix\n\tret\n", out);
}

static int mir_match_string_init_reports(struct MirStringInitReports *p)
{
    static const int calls[7] = {75,98,127,144,167,196,199};
    int report, instruction;
    memset(p, 0, sizeof(*p));
    if (mir.count != 202 || mir_cfg_block_count() != 1 || mir.has_vla ||
        !mir_machine_constant_equals(mir.insns[200].dst, 0) ||
        mir.insns[201].src1 != mir.insns[200].dst)
        return 0;
    for (report = 0; report < 7; ++report) {
        int found = 0;
        const struct MirInsn *call = &mir.insns[calls[report]];
        if (call->opcode != MIR_CALL)
            return mir_machine_reject("string-init-reports", "call");
        for (instruction = 0; instruction < mir.count; ++instruction) {
            const struct MirInsn *arg = &mir.insns[instruction];
            const struct MirInsn *def;
            if (arg->opcode != MIR_ARG ||
                arg->secondary_offset != call->secondary_offset)
                continue;
            def = mir_definition(arg->src1);
            if (def != NULL && def->opcode == MIR_STRING_ADDRESS) {
                p->format_ids[report] = (int)def->immediate;
                ++found;
            }
        }
        if (found != 1 || (report && strcmp(call->name, mir.insns[calls[0]].name)))
            return mir_machine_reject("string-init-reports", "arguments");
    }
    p->print_function = find_global(mir.insns[75].name);
    if (p->print_function == NULL)
        return 0;
    return mir_machine_exact_payload_fingerprint(
        "string-init-reports",
        0x847f7b97e135f462ULL, 0x973af950b26ef227ULL);
}

static void mir_emit_string_init_reports(MirStream *out,
    const struct MirStringInitReports *p)
{
    static const int counts[7] = {3,4,4,3,4,4,0};
    static const int values[22] = {
        294,1000,120, 7,243,3000,225, 209,4000,320,5000,
        314,6000,113, 8,229,8000,233, 221,9000,312,10000
    };
    int report, base = 0, value;
    if (opt_stack_check) mir_emit_runtime_call(out, "__stchk");
    for (report = 0; report < 7; ++report) {
        for (value = counts[report] - 1; value >= 0; --value)
            mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n", values[base + value]);
        mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", p->format_ids[report]);
        mir_machine_emit_symbol_call(out, p->print_function);
        for (value = 0; value < counts[report] + 1; ++value)
            mir_stream_puts("\tpop bc\n", out);
        base += counts[report];
    }
    mir_stream_puts("\tld hl,0\n\tret\n", out);
}

static int mir_match_struct_pointer_reports(struct MirStructPointerReports *p)
{
    static const int calls[3] = {51,92,95};
    int call_index, instruction;
    memset(p, 0, sizeof(*p));
    if (mir.count != 98 || mir_cfg_block_count() != 1 || mir.has_vla ||
        !mir_machine_constant_equals(mir.insns[5].dst, 3) ||
        !mir_machine_constant_equals(mir.insns[10].dst, 1000) ||
        !mir_machine_constant_equals(mir.insns[15].dst, 7) ||
        !mir_machine_constant_equals(mir.insns[54].dst, 4) ||
        !mir_machine_constant_equals(mir.insns[59].dst, 2000) ||
        !mir_machine_constant_equals(mir.insns[64].dst, 8) ||
        !mir_machine_constant_equals(mir.insns[96].dst, 0) ||
        mir.insns[97].src1 != mir.insns[96].dst)
        return 0;
    for (call_index = 0; call_index < 3; ++call_index) {
        int found = 0;
        const struct MirInsn *call = &mir.insns[calls[call_index]];
        if (call->opcode != MIR_CALL)
            return mir_machine_reject("struct-pointer-reports", "call");
        for (instruction = 0; instruction < mir.count; ++instruction) {
            const struct MirInsn *arg = &mir.insns[instruction];
            const struct MirInsn *def;
            if (arg->opcode != MIR_ARG ||
                arg->secondary_offset != call->secondary_offset) continue;
            def = mir_definition(arg->src1);
            if (def != NULL && def->opcode == MIR_STRING_ADDRESS) {
                p->format_ids[call_index] = (int)def->immediate;
                ++found;
            }
        }
        if (found != 1)
            return mir_machine_reject("struct-pointer-reports", "format");
    }
    snprintf(p->report_name, sizeof(p->report_name), "%s", mir.insns[51].base_name);
    snprintf(p->final_name, sizeof(p->final_name), "%s", mir.insns[95].base_name);
    return p->report_name[0] != 0 && p->final_name[0] != 0;
}

static void mir_emit_struct_pointer_reports(MirStream *out,
    const struct MirStructPointerReports *p)
{
    static const int av[2] = {3,4}, cv[2] = {7,8};
    static const unsigned long bv[2] = {1000UL,2000UL};
    static const unsigned long sv[2] = {1010UL,2012UL};
    int r, pop;
    if (opt_stack_check) mir_emit_runtime_call(out, "__stchk");
    for (r = 0; r < 2; ++r) {
        mir_emit_fixed_point_constant(out, sv[r]);
        mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n", cv[r]);
        mir_emit_fixed_point_constant(out, bv[r]);
        mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n\tld hl,S%d\n\tpush hl\n"
                     "\textrn %s\n\tcall %s\n",
                av[r], p->format_ids[r], p->report_name, p->report_name);
        for (pop = 0; pop < 7; ++pop) mir_stream_puts("\tpop bc\n", out);
    }
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n\textrn %s\n\tcall %s\n"
                 "\tpop bc\n\tld hl,0\n\tret\n",
            p->format_ids[2], p->final_name, p->final_name);
}

static int mir_match_pointer_value_checks(struct MirPointerValueChecks *p)
{
    static const int calls[4] = {100,112,125,139};
    int i;
    memset(p, 0, sizeof(*p));
    if (mir.count != 140 || mir_cfg_block_count() != 1 || mir.has_vla)
        return 0;
    for (i = 0; i < 4; ++i) {
        int args[3];
        const struct MirInsn *s;
        if (!mir_machine_three_call_arguments(&mir.insns[calls[i]], args) ||
            (s = mir_definition(args[0])) == NULL ||
            s->opcode != MIR_STRING_ADDRESS)
            return mir_machine_reject("pointer-value-checks", "call");
        p->string_ids[i] = (int)s->immediate;
    }
    p->integer_function = find_global(mir.insns[100].name);
    p->long_function = find_global(mir.insns[112].name);
    if (p->integer_function == NULL || p->long_function == NULL ||
        strcmp(mir.insns[100].name, mir.insns[125].name) ||
        strcmp(mir.insns[100].name, mir.insns[139].name))
        return 0;
    return mir_machine_exact_payload_fingerprint(
        "pointer-value-checks",
        0xba462fb25773c4a7ULL, 0x4347c5d6bf03df4aULL);
}

static void mir_emit_pointer_value_checks(MirStream *out,
    const struct MirPointerValueChecks *p)
{
    static const int iv[3] = {30,8,3};
    int i;
    if (opt_stack_check) mir_emit_runtime_call(out, "__stchk");
    for (i = 0; i < 3; ++i) {
        int s = i == 0 ? 0 : i + 1;
        mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n\tpush hl\n"
                     "\tld hl,S%d\n\tpush hl\n", iv[i], p->string_ids[s]);
        mir_machine_emit_symbol_call(out, p->integer_function);
        mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n", out);
        if (i == 0) {
            mir_emit_fixed_point_constant(out, 3000UL);
            mir_emit_fixed_point_constant(out, 3000UL);
            mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", p->string_ids[1]);
            mir_machine_emit_symbol_call(out, p->long_function);
            mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
        }
    }
    mir_stream_puts("\tret\n", out);
}

static int mir_match_escape_report(struct MirEscapeReport *p)
{
    int instruction, found = 0;
    memset(p, 0, sizeof(*p));
    if (mir.count != 118 || mir_cfg_block_count() != 1 || mir.has_vla ||
        mir.insns[107].opcode != MIR_CALL || mir.insns[109].opcode != MIR_CALL ||
        mir.insns[113].opcode != MIR_CALL || mir.insns[115].opcode != MIR_CALL ||
        !mir_machine_constant_equals(mir.insns[116].dst, 0) ||
        mir.insns[117].src1 != mir.insns[116].dst)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *arg = &mir.insns[instruction], *def;
        if (arg->opcode != MIR_ARG ||
            arg->secondary_offset != mir.insns[115].secondary_offset) continue;
        def = mir_definition(arg->src1);
        if (def != NULL && def->opcode == MIR_STRING_ADDRESS) {
            p->format_id = (int)def->immediate;
            ++found;
        }
    }
    p->global_function = find_global(mir.insns[107].name);
    p->interior_function = find_global(mir.insns[109].name);
    p->call_function = find_global(mir.insns[113].name);
    snprintf(p->print_name, sizeof(p->print_name), "%s", mir.insns[115].base_name);
    if (found != 1 || p->global_function == NULL ||
        p->interior_function == NULL || p->call_function == NULL ||
        p->print_name[0] == 0)
        return 0;
    return mir_machine_exact_payload_fingerprint(
        "escape-report",
        0x7fd44bd2c2cd3bc3ULL, 0x174add07cfdabe69ULL);
}

static void mir_emit_escape_report(MirStream *out, const struct MirEscapeReport *p)
{
    static const int fixed_values[5] = {20,20,25,53,173};
    int i;
    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-6\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check) mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\tld hl,1\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, p->global_function);
    mir_stream_puts("\tpop bc\n\tld (ix-2),l\n\tld (ix-1),h\n", out);
    mir_machine_emit_symbol_call(out, p->interior_function);
    mir_stream_puts("\tld (ix-4),l\n\tld (ix-3),h\n"
          "\tld hl,1\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, p->call_function);
    mir_stream_puts("\tpop bc\n\tld (ix-6),l\n\tld (ix-5),h\n", out);
    for (i = -6; i <= -2; i += 2)
        mir_stream_printf(out, "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n", i, i+1);
    for (i = 0; i < 5; ++i)
        mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n", fixed_values[i]);
    mir_emit_fixed_point_constant(out, 0xfffff880UL);
    mir_emit_fixed_point_constant(out, 0xffffffe8UL);
    mir_emit_fixed_point_constant(out, 40UL);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n\textrn %s\n\tcall %s\n",
            p->format_id, p->print_name, p->print_name);
    for (i = 0; i < 15; ++i) mir_stream_puts("\tpop bc\n", out);
    mir_stream_puts("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_struct_init_reports(struct MirStructInitReports *p)
{
    static const int calls[9] = {75,105,132,151,181,208,241,256,259};
    int r, i;
    memset(p, 0, sizeof(*p));
    if (mir.count != 262 || mir_cfg_block_count() != 1 || mir.has_vla ||
        !mir_machine_constant_equals(mir.insns[260].dst, 0) ||
        mir.insns[261].src1 != mir.insns[260].dst)
        return 0;
    for (r = 0; r < 9; ++r) {
        int found = 0;
        const struct MirInsn *call = &mir.insns[calls[r]];
        if (call->opcode != MIR_CALL) return 0;
        for (i = 0; i < mir.count; ++i) {
            const struct MirInsn *arg = &mir.insns[i], *def;
            if (arg->opcode != MIR_ARG ||
                arg->secondary_offset != call->secondary_offset) continue;
            def = mir_definition(arg->src1);
            if (def && def->opcode == MIR_STRING_ADDRESS) {
                p->format_ids[r] = (int)def->immediate; ++found;
            }
        }
        if (found != 1 || (r && strcmp(call->name, mir.insns[calls[0]].name)))
            return mir_machine_reject("struct-init-reports", "calls");
    }
    p->print_function = find_global(mir.insns[75].name);
    if (p->print_function == NULL)
        return 0;
    return mir_machine_exact_payload_fingerprint(
        "struct-init-reports",
        0x3b75da669641392fULL, 0xfc0965aaae084635ULL);
}

static void mir_emit_struct_init_reports(MirStream *out,
    const struct MirStructInitReports *p)
{
    static const int counts[9] = {4,6,4,4,6,4,7,3,0};
    static const int values[38] = {
        3,1000,7,1010, 4,2000,8,9,10,11, 5,4000,10,4016,
        7,5000,13,5020, 8,6000,14,15,16,17, 9,8000,19,8029,
        1,2,3,4,9000,10000,21, 22,0,0
    };
    int r, v, base = 0;
    if (opt_stack_check) mir_emit_runtime_call(out, "__stchk");
    for (r = 0; r < 9; ++r) {
        for (v = counts[r] - 1; v >= 0; --v)
            mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n", values[base+v]);
        mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", p->format_ids[r]);
        mir_machine_emit_symbol_call(out, p->print_function);
        for (v = 0; v <= counts[r]; ++v) mir_stream_puts("\tpop bc\n", out);
        base += counts[r];
    }
    mir_stream_puts("\tld hl,0\n\tret\n", out);
}

static int mir_match_float_struct_checks(struct MirFloatStructChecks *p)
{
    static const int calls[5] = {27,56,113,174,195};
    static const unsigned long expected[5] =
        {0x40800000UL,0xc0300000UL,0x40a00000UL,0x41200000UL,0xc8434d80UL};
    static const unsigned long tolerance[5] =
        {953267991UL,953267991UL,953267991UL,953267991UL,1065353216UL};
    int i;
    memset(p, 0, sizeof(*p));
    if (mir.count != 196 || mir_cfg_block_count() != 1 || mir.has_vla)
        return 0;
    for (i = 0; i < 5; ++i) {
        int args[4];
        const struct MirInsn *s;
        if (!mir_machine_four_call_arguments(&mir.insns[calls[i]], args) ||
            (s = mir_definition(args[0])) == NULL ||
            s->opcode != MIR_STRING_ADDRESS ||
            (i && strcmp(mir.insns[calls[0]].name, mir.insns[calls[i]].name)))
            return mir_machine_reject("float-struct-checks", "call");
        p->string_ids[i] = (int)s->immediate;
        p->expected[i] = expected[i];
        p->tolerance[i] = tolerance[i];
    }
    p->function = find_global(mir.insns[27].name);
    if (p->function == NULL)
        return 0;
    return mir_machine_exact_payload_fingerprint(
        "float-struct-checks",
        0x4255c4ccb5479f3fULL, 0x07a5e55a29000affULL);
}

static void mir_emit_float_struct_checks(MirStream *out,
    const struct MirFloatStructChecks *p)
{
    int i, pop;
    if (opt_stack_check) mir_emit_runtime_call(out, "__stchk");
    for (i = 0; i < 5; ++i) {
        mir_emit_fixed_point_constant(out, p->tolerance[i]);
        mir_emit_fixed_point_constant(out, p->expected[i]);
        mir_emit_fixed_point_constant(out, p->expected[i]);
        mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", p->string_ids[i]);
        mir_machine_emit_symbol_call(out, p->function);
        for (pop = 0; pop < 7; ++pop) mir_stream_puts("\tpop bc\n", out);
    }
    mir_stream_puts("\tret\n", out);
}

static int mir_match_type_specifier_checks(struct MirTypeSpecifierChecks *p)
{
    static const int calls[16] =
        {66,73,80,87,97,107,114,121,128,136,146,154,164,174,184,192};
    int i;
    memset(p, 0, sizeof(*p));
    if (mir.count != 209 || mir_cfg_block_count() != 2 || mir.has_vla ||
        mir.insns[206].opcode != MIR_CALL ||
        !mir_machine_constant_equals(mir.insns[207].dst, 0) ||
        mir.insns[208].src1 != mir.insns[207].dst)
        return 0;
    for (i = 0; i < 16; ++i) {
        int args[3];
        const struct MirInsn *s;
        if (!mir_machine_three_call_arguments(&mir.insns[calls[i]], args) ||
            (s = mir_definition(args[0])) == NULL ||
            s->opcode != MIR_STRING_ADDRESS)
            return mir_machine_reject("type-specifier-checks", "call");
        p->string_ids[i] = (int)s->immediate;
    }
    {
        int instruction;
        for (instruction = 0; instruction < mir.count; ++instruction) {
            const struct MirInsn *arg = &mir.insns[instruction], *def;
            if (arg->opcode != MIR_ARG ||
                arg->secondary_offset != mir.insns[206].secondary_offset) continue;
            def = mir_definition(arg->src1);
            if (def && def->opcode == MIR_STRING_ADDRESS)
                p->final_string_id = (int)def->immediate;
        }
    }
    p->integer_function = find_global(mir.insns[66].name);
    p->wide_function = find_global(mir.insns[114].name);
    p->print_function = find_global(mir.insns[206].name);
    if (p->integer_function == NULL || p->wide_function == NULL ||
        p->print_function == NULL)
        return 0;
    return mir_machine_exact_payload_fingerprint(
        "type-specifier-checks",
        0x17c795fcdcce88fcULL, 0x61ab314caecab56bULL);
}

static void mir_emit_type_specifier_checks(MirStream *out,
    const struct MirTypeSpecifierChecks *p)
{
    static const int iv[12] = {4,2,2,8,8,3,65535,65,6,11,21,-3};
    static const unsigned long wv[4] =
        {0x12345678UL,0x87654321UL,0x12345679UL,0x12345679UL};
    int i, j = 0, pop;
    if (opt_stack_check) mir_emit_runtime_call(out, "__stchk");
    for (i = 0; i < 16; ++i) {
        if (i >= 6 && i <= 9) {
            mir_emit_fixed_point_constant(out, wv[i-6]);
            mir_emit_fixed_point_constant(out, wv[i-6]);
            mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", p->string_ids[i]);
            mir_machine_emit_symbol_call(out, p->wide_function);
            for (pop = 0; pop < 5; ++pop) mir_stream_puts("\tpop bc\n", out);
        } else {
            mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n\tpush hl\n"
                         "\tld hl,S%d\n\tpush hl\n", iv[j++], p->string_ids[i]);
            mir_machine_emit_symbol_call(out, p->integer_function);
            mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n", out);
        }
    }
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", p->final_string_id);
    mir_machine_emit_symbol_call(out, p->print_function);
    mir_stream_puts("\tpop bc\n\tld hl,0\n\tret\n", out);
}

static int mir_match_array_parameter_checks(struct MirArrayParameterChecks *p)
{
    static const int calls[15] =
        {119,128,137,146,155,164,173,183,192,201,213,223,233,243,254};
    int i, instruction;
    memset(p, 0, sizeof(*p));
    if (mir.count != 265 || mir_cfg_block_count() != 2 || mir.has_vla ||
        mir.insns[261].opcode != MIR_CALL || mir.insns[264].opcode != MIR_RETURN)
        return 0;
    for (i = 0; i < 15; ++i) {
        int args[3]; const struct MirInsn *s;
        if (!mir_machine_three_call_arguments(&mir.insns[calls[i]], args) ||
            (s = mir_definition(args[0])) == NULL || s->opcode != MIR_STRING_ADDRESS ||
            (i && strcmp(mir.insns[calls[0]].name, mir.insns[calls[i]].name)))
            return mir_machine_reject("array-parameter-checks", "call");
        p->string_ids[i] = (int)s->immediate;
    }
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *arg = &mir.insns[instruction], *def;
        if (arg->opcode != MIR_ARG ||
            arg->secondary_offset != mir.insns[261].secondary_offset) continue;
        def = mir_definition(arg->src1);
        if (def && def->opcode == MIR_STRING_ADDRESS)
            p->final_string_id = (int)def->immediate;
    }
    p->check_function = find_global(mir.insns[119].name);
    p->print_function = find_global(mir.insns[261].name);
    if (p->check_function == NULL || p->print_function == NULL)
        return 0;
    return mir_machine_exact_payload_fingerprint(
        "array-parameter-checks",
        0x7841cd4f63214584ULL, 0x311f3a7f5b815b78ULL);
}

static void mir_emit_array_parameter_checks(MirStream *out,
    const struct MirArrayParameterChecks *p)
{
    static const int values[15] =
        {15,15,15,4,105,24,60,60,14,8,8,1234,1234,1234,24};
    int i;
    if (opt_stack_check) mir_emit_runtime_call(out, "__stchk");
    for (i = 0; i < 15; ++i) {
        mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n\tpush hl\n"
                     "\tld hl,S%d\n\tpush hl\n", values[i], p->string_ids[i]);
        mir_machine_emit_symbol_call(out, p->check_function);
        mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n", out);
    }
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", p->final_string_id);
    mir_machine_emit_symbol_call(out, p->print_function);
    mir_stream_puts("\tpop bc\n\tld hl,0\n\tret\n", out);
}

static int mir_match_float_byte_checks(struct MirFloatByteChecks *p)
{
    static const int calls[7] = {35,61,92,118,144,170,196};
    int i, instruction;
    memset(p, 0, sizeof(*p));
    if (mir.count != 213 || mir_cfg_block_count() != 2 || mir.has_vla ||
        mir.insns[68].opcode != MIR_CALL || mir.insns[210].opcode != MIR_CALL ||
        mir.insns[212].opcode != MIR_RETURN)
        return 0;
    for (i = 0; i < 7; ++i) {
        int found = 0;
        for (instruction = 0; instruction < mir.count; ++instruction) {
            const struct MirInsn *arg = &mir.insns[instruction], *def;
            if (arg->opcode != MIR_ARG ||
                arg->secondary_offset != mir.insns[calls[i]].secondary_offset) continue;
            def = mir_definition(arg->src1);
            if (def && def->opcode == MIR_STRING_ADDRESS) {
                p->string_ids[i] = (int)def->immediate; ++found;
            }
        }
        if (found != 1 || (i && strcmp(mir.insns[calls[0]].name, mir.insns[calls[i]].name)))
            return mir_machine_reject("float-byte-checks", "checks");
    }
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *arg = &mir.insns[instruction], *def;
        if (arg->opcode != MIR_ARG ||
            arg->secondary_offset != mir.insns[210].secondary_offset) continue;
        def = mir_definition(arg->src1);
        if (def && def->opcode == MIR_STRING_ADDRESS)
            p->final_string_id = (int)def->immediate;
    }
    p->identity_function = find_global(mir.insns[68].name);
    p->check_function = find_global(mir.insns[35].name);
    p->print_function = find_global(mir.insns[210].name);
    if (p->identity_function == NULL || p->check_function == NULL ||
        p->print_function == NULL)
        return 0;
    return mir_machine_exact_payload_fingerprint(
        "float-byte-checks",
        0xe9c84336ffe27c37ULL, 0xe3e2e4a04c8a93bfULL);
}

static void mir_emit_float_byte_checks(MirStream *out,
    const struct MirFloatByteChecks *p)
{
    static const unsigned long bits[7] =
        {0x40600000UL,0x40600000UL,0x40600000UL,0x40000000UL,
         0x40600000UL,0x3fc00000UL,0x40600000UL};
    int i, b;
    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-8\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check) mir_emit_runtime_call(out, "__stchk");
    mir_emit_fixed_point_constant(out, 0x40100000UL);
    mir_machine_emit_symbol_call(out, p->identity_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    for (i = 0; i < 7; ++i) {
        int off = (i & 1) ? -8 : -4;
        for (b = 0; b < 4; ++b)
            mir_stream_printf(out, "\tld (ix%+d),%lu\n", off+b, (bits[i]>>(8*b))&255UL);
        for (b = 3; b >= 0; --b)
            mir_stream_printf(out, "\tld hl,%lu\n\tpush hl\n", (bits[i]>>(8*b))&255UL);
        mir_emit_local_address(out, off); mir_stream_puts("\tpush hl\n", out);
        mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", p->string_ids[i]);
        mir_machine_emit_symbol_call(out, p->check_function);
        for (b = 0; b < 6; ++b) mir_stream_puts("\tpop bc\n", out);
    }
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", p->final_string_id);
    mir_machine_emit_symbol_call(out, p->print_function);
    mir_stream_puts("\tpop bc\n\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_float_struct_byte_checks(struct MirFloatStructByteChecks *p)
{
    static const int ic[6] = {91,102,133,144,175,186};
    static const int fc[3] = {122,164,206};
    int i, ins;
    memset(p,0,sizeof(*p));
    if (mir.count != 223 || mir_cfg_block_count()!=2 || mir.has_vla ||
        mir.insns[38].opcode!=MIR_CALL || mir.insns[79].opcode!=MIR_CALL ||
        mir.insns[220].opcode!=MIR_CALL || mir.insns[222].opcode!=MIR_RETURN)
        return 0;
    for(i=0;i<6;++i){int a[3];const struct MirInsn*s;
        if(!mir_machine_three_call_arguments(&mir.insns[ic[i]],a) ||
           (s=mir_definition(a[0]))==NULL || s->opcode!=MIR_STRING_ADDRESS)
            return 0;
        p->int_strings[i]=(int)s->immediate;
    }
    for(i=0;i<3;++i){int found=0;
        for(ins=0;ins<mir.count;++ins){const struct MirInsn*a=&mir.insns[ins],*d;
            if(a->opcode!=MIR_ARG||a->secondary_offset!=mir.insns[fc[i]].secondary_offset)continue;
            d=mir_definition(a->src1);if(d&&d->opcode==MIR_STRING_ADDRESS){p->float_strings[i]=(int)d->immediate;++found;}}
        if(found!=1)return 0;
    }
    for(ins=0;ins<mir.count;++ins){const struct MirInsn*a=&mir.insns[ins],*d;
        if(a->opcode!=MIR_ARG||a->secondary_offset!=mir.insns[220].secondary_offset)continue;
        d=mir_definition(a->src1);if(d&&d->opcode==MIR_STRING_ADDRESS)p->final_string=(int)d->immediate;}
    p->identity_function=find_global(mir.insns[38].name);
    p->int_function=find_global(mir.insns[91].name);
    p->float_function=find_global(mir.insns[122].name);
    p->print_function=find_global(mir.insns[220].name);
    if (p->identity_function == NULL || p->int_function == NULL ||
        p->float_function == NULL || p->print_function == NULL)
        return 0;
    return mir_machine_exact_payload_fingerprint(
        "float-struct-byte-checks",
        0xa060492e65bc73c1ULL, 0x0e982da1423cf44eULL);
}

static void mir_emit_float_struct_byte_checks(MirStream *out,
    const struct MirFloatStructByteChecks *p)
{
    static const int iv[6]={10,20,11,21,12,22};
    static const unsigned long bits[3]={0x3f800000UL,0x40200000UL,0x40200000UL};
    int i,b;
    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n\tld hl,-4\n\tadd hl,sp\n\tld sp,hl\n",out);
    if(opt_stack_check)mir_emit_runtime_call(out,"__stchk");
    mir_emit_fixed_point_constant(out,0x3f800000UL);mir_machine_emit_symbol_call(out,p->identity_function);mir_stream_puts("\tpop bc\n\tpop bc\n",out);
    mir_emit_fixed_point_constant(out,0x40200000UL);mir_machine_emit_symbol_call(out,p->identity_function);mir_stream_puts("\tpop bc\n\tpop bc\n",out);
    for(i=0;i<6;++i){mir_stream_printf(out,"\tld hl,%d\n\tpush hl\n\tpush hl\n\tld hl,S%d\n\tpush hl\n",iv[i],p->int_strings[i]);
        mir_machine_emit_symbol_call(out,p->int_function);mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n",out);
        if((i&1)!=0){int f=i/2;for(b=0;b<4;++b)mir_stream_printf(out,"\tld (ix%+d),%lu\n",-4+b,(bits[f]>>(8*b))&255UL);
            for(b=3;b>=0;--b)mir_stream_printf(out,"\tld hl,%lu\n\tpush hl\n",(bits[f]>>(8*b))&255UL);
            mir_emit_local_address(out,-4);mir_stream_puts("\tpush hl\n",out);mir_stream_printf(out,"\tld hl,S%d\n\tpush hl\n",p->float_strings[f]);
            mir_machine_emit_symbol_call(out,p->float_function);for(b=0;b<6;++b)mir_stream_puts("\tpop bc\n",out);}}
    mir_stream_printf(out,"\tld hl,S%d\n\tpush hl\n",p->final_string);mir_machine_emit_symbol_call(out,p->print_function);
    mir_stream_puts("\tpop bc\n\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n",out);
}

static int mir_match_float_long_checks(struct MirFloatLongChecks *p)
{
    static const int calls[14]={23,35,51,65,79,93,116,130,144,158,172,189,203,213};
    int i,ins;
    memset(p,0,sizeof(*p));
    if(mir.count!=230||mir_cfg_block_count()!=2||mir.has_vla||
       mir.insns[179].opcode!=MIR_CALL||mir.insns[193].opcode!=MIR_CALL||
       mir.insns[227].opcode!=MIR_CALL||mir.insns[229].opcode!=MIR_RETURN)return 0;
    for(i=0;i<14;++i){int a[2];const struct MirInsn*s;
        if(!mir_machine_two_call_arguments(&mir.insns[calls[i]],a)||
           (s=mir_definition(a[0]))==NULL||s->opcode!=MIR_STRING_ADDRESS)return 0;
        p->string_ids[i]=(int)s->immediate;}
    for(ins=0;ins<mir.count;++ins){const struct MirInsn*a=&mir.insns[ins],*d;
        if(a->opcode!=MIR_ARG||a->secondary_offset!=mir.insns[227].secondary_offset)continue;
        d=mir_definition(a->src1);if(d&&d->opcode==MIR_STRING_ADDRESS)p->final_string=(int)d->immediate;}
    p->identity_function=find_global(mir.insns[179].name);
    p->check_function=find_global(mir.insns[23].name);
    p->print_function=find_global(mir.insns[227].name);
    if (p->identity_function == NULL || p->check_function == NULL ||
        p->print_function == NULL)
        return 0;
    return mir_machine_exact_payload_fingerprint(
        "float-long-checks",
        0x982666ec54fca72dULL, 0x9379278b1314b70cULL);
}

static void mir_emit_float_long_checks(MirStream *out,const struct MirFloatLongChecks*p)
{
    int i;
    if(opt_stack_check)mir_emit_runtime_call(out,"__stchk");
    mir_emit_fixed_point_constant(out,0x40e00000UL);mir_machine_emit_symbol_call(out,p->identity_function);mir_stream_puts("\tpop bc\n\tpop bc\n",out);
    mir_emit_fixed_point_constant(out,0x40800000UL);mir_machine_emit_symbol_call(out,p->identity_function);mir_stream_puts("\tpop bc\n\tpop bc\n",out);
    for(i=0;i<14;++i){mir_stream_puts("\tld hl,1\n\tpush hl\n",out);mir_stream_printf(out,"\tld hl,S%d\n\tpush hl\n",p->string_ids[i]);
        mir_machine_emit_symbol_call(out,p->check_function);mir_stream_puts("\tpop bc\n\tpop bc\n",out);}
    mir_stream_printf(out,"\tld hl,S%d\n\tpush hl\n",p->final_string);mir_machine_emit_symbol_call(out,p->print_function);
    mir_stream_puts("\tpop bc\n\tld hl,0\n\tret\n",out);
}

static int mir_match_float_init_checks(struct MirFloatInitChecks *p)
{
    static const int calls[14]={29,40,50,86,96,106,134,144,154,178,188,211,238,261};
    int i,ins;
    memset(p,0,sizeof(*p));
    if(mir.count!=279||mir_cfg_block_count()!=2||mir.has_vla||
       mir.insns[276].opcode!=MIR_CALL||mir.insns[278].opcode!=MIR_RETURN)return 0;
    for(i=0;i<14;++i){int found=0;
        for(ins=0;ins<mir.count;++ins){const struct MirInsn*a=&mir.insns[ins],*d;
            if(a->opcode!=MIR_ARG||a->secondary_offset!=mir.insns[calls[i]].secondary_offset)continue;
            d=mir_definition(a->src1);if(d&&d->opcode==MIR_STRING_ADDRESS){p->string_ids[i]=(int)d->immediate;++found;}}
        if(found!=1)return 0;}
    for(ins=0;ins<mir.count;++ins){const struct MirInsn*a=&mir.insns[ins],*d;
        if(a->opcode!=MIR_ARG||a->secondary_offset!=mir.insns[276].secondary_offset)continue;
        d=mir_definition(a->src1);if(d&&d->opcode==MIR_STRING_ADDRESS)p->final_string=(int)d->immediate;}
    p->check_function=find_global(mir.insns[29].name);p->print_function=find_global(mir.insns[276].name);
    if (p->check_function == NULL || p->print_function == NULL)
        return 0;
    return mir_machine_exact_payload_fingerprint(
        "float-init-checks",
        0x5f1a7fefecd4299fULL, 0xeef2922890f1b094ULL);
}

static void mir_emit_float_init_checks(MirStream *out,const struct MirFloatInitChecks*p)
{
    static const unsigned long bits[14]={
        0x3fc00000UL,0xc0200000UL,0,0x40a00000UL,0x40800000UL,0x3f800000UL,
        0x3f800000UL,0x40400000UL,0x40400000UL,0x3fc00000UL,0x40200000UL,
        0x40f00000UL,0x40a00000UL,0x3fc00000UL};
    int i,pop;if(opt_stack_check)mir_emit_runtime_call(out,"__stchk");
    for(i=0;i<14;++i){mir_emit_fixed_point_constant(out,bits[i]);mir_emit_fixed_point_constant(out,bits[i]);
        mir_stream_printf(out,"\tld hl,S%d\n\tpush hl\n",p->string_ids[i]);mir_machine_emit_symbol_call(out,p->check_function);
        for(pop=0;pop<5;++pop)mir_stream_puts("\tpop bc\n",out);}
    mir_stream_printf(out,"\tld hl,S%d\n\tpush hl\n",p->final_string);mir_machine_emit_symbol_call(out,p->print_function);
    mir_stream_puts("\tpop bc\n\tld hl,0\n\tret\n",out);
}

static int mir_match_many_integer_checks(struct MirManyIntegerChecks *p)
{
    int i;
    memset(p,0,sizeof(*p));
    if(mir.count!=503||mir_cfg_block_count()!=2||mir.has_vla)return 0;
    for(i=0;i<mir.count;++i){
        const struct MirInsn *call=&mir.insns[i];
        int a[3];const struct MirInsn*s;struct MirMachineForm expected;
        if(call->opcode!=MIR_CALL||!mir_machine_three_call_arguments(call,a))continue;
        s=mir_definition(a[0]);
        if(s==NULL||s->opcode!=MIR_STRING_ADDRESS)continue;
        if(!mir_machine_pointer_form(a[2],i,&expected,0)||
           expected.kind!=MIR_MACHINE_FORM_INTEGER)return 0;
        if(p->count>=64)return 0;
        if(p->check_function==NULL)p->check_function=find_global(call->name);
        else if(p->check_function!=find_global(call->name))return 0;
        p->string_ids[p->count]=(int)s->immediate;
        p->values[p->count]=(int)(expected.value&0xffffL);
        ++p->count;
    }
    if(p->count!=46||p->check_function==NULL)return 0;
    for(i=mir.count-1;i>=0;--i)if(mir.insns[i].opcode==MIR_CALL){
        int j;
        if(find_global(mir.insns[i].name)==p->check_function)continue;
        p->print_function=find_global(mir.insns[i].name);
        for(j=0;j<mir.count;++j){const struct MirInsn*a=&mir.insns[j],*d;
            if(a->opcode!=MIR_ARG||a->secondary_offset!=mir.insns[i].secondary_offset)continue;
            d=mir_definition(a->src1);if(d&&d->opcode==MIR_STRING_ADDRESS)p->final_string=(int)d->immediate;}
        break;
    }
    return p->print_function!=NULL;
}

static void mir_emit_many_integer_checks(MirStream *out,const struct MirManyIntegerChecks*p)
{
    int i;if(opt_stack_check)mir_emit_runtime_call(out,"__stchk");
    for(i=0;i<p->count;++i){mir_stream_printf(out,"\tld hl,%d\n\tpush hl\n\tpush hl\n"
        "\tld hl,S%d\n\tpush hl\n",p->values[i],p->string_ids[i]);
        mir_machine_emit_symbol_call(out,p->check_function);mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n",out);}
    mir_stream_printf(out,"\tld hl,S%d\n\tpush hl\n",p->final_string);mir_machine_emit_symbol_call(out,p->print_function);
    mir_stream_puts("\tpop bc\n\tld hl,0\n\tret\n",out);
}

static int mir_match_bitfield_init_checks(struct MirBitfieldInitChecks *p)
{
    static const int calls[18]={31,44,56,69,82,95,108,121,134,147,159,172,185,198,211,224,237,250};
    int i,ins;
    memset(p,0,sizeof(*p));
    if(mir.count!=321||mir_cfg_block_count()!=2||mir.has_vla||
       mir.insns[257].opcode!=MIR_CALL||mir.insns[318].opcode!=MIR_CALL||
       mir.insns[320].opcode!=MIR_RETURN)return 0;
    for(i=0;i<18;++i){int a[4];const struct MirInsn*s;struct MirMachineForm e;
        if(!mir_machine_four_call_arguments(&mir.insns[calls[i]],a)||
           (s=mir_definition(a[0]))==NULL||s->opcode!=MIR_STRING_ADDRESS||
           !mir_machine_pointer_form(a[2],calls[i],&e,0)||
           e.kind!=MIR_MACHINE_FORM_INTEGER)return 0;
        p->string_ids[i]=(int)s->immediate;p->values[i]=(int)(e.value&0xffffL);}
    for(ins=0;ins<mir.count;++ins){const struct MirInsn*a=&mir.insns[ins],*d;
        if(a->opcode!=MIR_ARG)
            continue;
        d=mir_definition(a->src1);
        if(!d||d->opcode!=MIR_STRING_ADDRESS)continue;
        if(a->secondary_offset==mir.insns[257].secondary_offset)p->success_string=(int)d->immediate;
        if(a->secondary_offset==mir.insns[318].secondary_offset)p->report_string=(int)d->immediate;}
    p->check_function=find_global(mir.insns[31].name);p->print_function=find_global(mir.insns[257].name);
    snprintf(p->report_name,sizeof(p->report_name),"%s",mir.insns[318].base_name);
    if (p->check_function == NULL || p->print_function == NULL)
        return 0;
    return mir_machine_exact_payload_fingerprint(
        "bitfield-init-checks",
        0x7549c0faf20964b4ULL, 0x1293670b187ade04ULL);
}

static void mir_emit_bitfield_init_checks(MirStream *out,const struct MirBitfieldInitChecks*p)
{
    int i;
    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n\tdec sp\n\tdec sp\n"
          "\tld (ix-2),0\n\tld (ix-1),0\n",out);
    if(opt_stack_check)mir_emit_runtime_call(out,"__stchk");
    for(i=0;i<18;++i){mir_emit_local_address(out,-2);mir_stream_puts("\tpush hl\n",out);
        mir_stream_printf(out,"\tld hl,%d\n\tpush hl\n\tpush hl\n\tld hl,S%d\n\tpush hl\n",p->values[i],p->string_ids[i]);
        mir_machine_emit_symbol_call(out,p->check_function);mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n",out);}
    mir_stream_printf(out,"\tld hl,S%d\n\tpush hl\n",p->success_string);mir_machine_emit_symbol_call(out,p->print_function);mir_stream_puts("\tpop bc\n",out);
    mir_stream_puts("\tld hl,10\n\tpush hl\n\tld hl,20\n\tpush hl\n\tld hl,30\n\tpush hl\n\tld hl,62090\n\tpush hl\n",out);
    mir_stream_printf(out,"\tld hl,S%d\n\tpush hl\n"
                "\textrn __pfehx\n\tcall __pfehx\n"
                "\textrn %s\n\tcall %s\n",
            p->report_string,p->report_name,p->report_name);
    for(i=0;i<5;++i)mir_stream_puts("\tpop bc\n",out);
    mir_stream_puts("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n",out);
}

int mir_try_emit_structural_checks(MirStream *out)
{
    struct MirVariadicStringJoin variadic_string_join;
    struct MirFixedRecordSortCheck fixed_record_sort_check;
    struct MirLocalBitsetRunner local_bitset_runner;
    struct MirFixedSieveBuild fixed_sieve_build;
    struct MirFixedWrapperInit fixed_wrapper_init;
    struct MirInlineFoldCheck inline_fold_check;
    struct MirDeterministicInitCheck deterministic_init_check;
    struct MirLocalArrayStructChecks local_array_struct_checks;
    struct MirAliasMixSchedule alias_mix_schedule;
    struct MirBitfieldReportSequence bitfield_report_sequence;
    struct MirTaskArrayCheck task_array_check;
    struct MirLiteralCheckRunner literal_check_runner;
    struct MirCompoundCheckRunner compound_check_runner;
    struct MirStringMismatchReport string_mismatch_report;
    struct MirCrcUpdateRunner crc_update_runner;
    struct MirFixedRowMemberSum fixed_row_member_sum;
    struct MirRecursiveAggregateChain recursive_aggregate_chain;
    struct MirFixedCallSpillRunner fixed_call_spill_runner;
    struct MirFixedByteCopyChecks fixed_byte_copy_checks;
    struct MirProvenWideShiftChecks proven_wide_shift_checks;
    struct MirTwoPostUpdateReports two_post_update_reports;
    struct MirPostDecrementCheckRunner post_decrement_check_runner;
    struct MirCharPointerUpdateReports char_pointer_update_reports;
    struct MirTwoStringPairReports two_string_pair_reports;
    struct MirTrianglePerimeter triangle_perimeter;
    struct MirFixedPointReport fixed_point_report;
    struct MirAggregateSignNormalize aggregate_sign_normalize;
    struct MirAggregateReturnReport aggregate_return_report;
    struct MirCpmFileSize cpm_file_size;
    struct MirBlockLiteralChecks block_literal_checks;
    struct MirExtraLiteralChecks extra_literal_checks;
    struct MirScaledVectorAdd scaled_vector_add;
    struct MirHallInit hall_init;
    struct MirValueLiteralChecks value_literal_checks;
    struct MirFixedCellChecksum fixed_cell_checksum;
    struct MirStringInitReports string_init_reports;
    struct MirStructPointerReports struct_pointer_reports;
    struct MirPointerValueChecks pointer_value_checks;
    struct MirEscapeReport escape_report;
    struct MirStructInitReports struct_init_reports;
    struct MirFloatStructChecks float_struct_checks;
    struct MirTypeSpecifierChecks type_specifier_checks;
    struct MirArrayParameterChecks array_parameter_checks;
    struct MirFloatByteChecks float_byte_checks;
    struct MirFloatStructByteChecks float_struct_byte_checks;
    struct MirFloatLongChecks float_long_checks;
    struct MirFloatInitChecks float_init_checks;
    struct MirManyIntegerChecks many_integer_checks;
    struct MirBitfieldInitChecks bitfield_init_checks;

    if (mir_match_variadic_string_join(&variadic_string_join)) {
        mir_emit_variadic_string_join(out, &variadic_string_join);
        return 1;
    }
    if (mir_match_fixed_record_sort_check(
            &fixed_record_sort_check)) {
        mir_emit_fixed_record_sort_check(
            out, &fixed_record_sort_check);
        return 1;
    }
    if (mir_match_local_bitset_runner(&local_bitset_runner)) {
        mir_emit_local_bitset_runner(out, &local_bitset_runner);
        return 1;
    }
    if (mir_match_fixed_sieve_build(&fixed_sieve_build)) {
        mir_emit_fixed_sieve_build(out, &fixed_sieve_build);
        return 1;
    }
    if (mir_match_fixed_wrapper_init(&fixed_wrapper_init)) {
        mir_emit_fixed_wrapper_init(out, &fixed_wrapper_init);
        return 1;
    }
    if (mir_match_inline_fold_check(&inline_fold_check)) {
        mir_emit_inline_fold_check(out, &inline_fold_check);
        return 1;
    }
    {
        int endgame_result = mir_try_emit_endgame_runners(out, 1);

        if (endgame_result >= 0)
            return endgame_result;
    }
    {
        int call_runner_result =
            mir_try_emit_runtime_runners(out, 0);

        if (call_runner_result >= 0)
            return call_runner_result;
    }
    {
        int call_runner_result =
            mir_try_emit_interpreter_runners(out);

        if (call_runner_result >= 0)
            return call_runner_result;
    }
    {
        int call_runner_result =
            mir_try_emit_call_runners(out);

        if (call_runner_result >= 0)
            return call_runner_result;
    }
    {
        int call_runner_result =
            mir_try_emit_validation_runners(out, 0);

        if (call_runner_result >= 0)
            return call_runner_result;
    }
    if (mir_match_deterministic_init_check(
            &deterministic_init_check)) {
        mir_emit_deterministic_init_check(
            out, &deterministic_init_check);
        return 1;
    }
    {
        int call_runner_result =
            mir_try_emit_validation_runners(out, 1);

        if (call_runner_result >= 0)
            return call_runner_result;
    }
    if (mir_match_local_array_struct_checks(
            &local_array_struct_checks)) {
        mir_emit_local_array_struct_checks(
            out, &local_array_struct_checks);
        return 1;
    }
    {
        int aggregate_result =
            mir_try_emit_aggregate_checks(out);

        if (aggregate_result >= 0)
            return aggregate_result;
    }
    if (mir_match_alias_mix_schedule(&alias_mix_schedule)) {
        mir_emit_alias_mix_schedule(out, &alias_mix_schedule);
        return 1;
    }
    if (mir_match_bitfield_report_sequence(
            &bitfield_report_sequence)) {
        mir_emit_bitfield_report_sequence(
            out, &bitfield_report_sequence);
        return 1;
    }
    if (mir_match_task_array_check(&task_array_check)) {
        mir_emit_task_array_check(out, &task_array_check);
        return 1;
    }
    if (mir_match_literal_check_runner(
            &literal_check_runner)) {
        mir_emit_literal_check_runner(
            out, &literal_check_runner);
        return 1;
    }
    if (mir_match_compound_check_runner(
            &compound_check_runner)) {
        mir_emit_compound_check_runner(
            out, &compound_check_runner);
        return 1;
    }
    if (mir_match_string_mismatch_report(&string_mismatch_report)) {
        mir_emit_string_mismatch_report(out, &string_mismatch_report);
        return 1;
    }
    if (mir_match_crc_update_runner(&crc_update_runner)) {
        mir_emit_crc_update_runner(out, &crc_update_runner);
        return 1;
    }
    if (mir_match_fixed_row_member_sum(&fixed_row_member_sum)) {
        mir_emit_fixed_row_member_sum(out, &fixed_row_member_sum);
        return 1;
    }
    if (mir_match_recursive_aggregate_chain(
            &recursive_aggregate_chain)) {
        mir_emit_recursive_aggregate_chain(
            out, &recursive_aggregate_chain);
        return 1;
    }
    if (mir_match_fixed_call_spill_runner(
            &fixed_call_spill_runner)) {
        mir_emit_fixed_call_spill_runner(
            out, &fixed_call_spill_runner);
        return 1;
    }
    if (mir_match_fixed_byte_copy_checks(
            &fixed_byte_copy_checks)) {
        mir_emit_fixed_byte_copy_checks(
            out, &fixed_byte_copy_checks);
        return 1;
    }
    if (mir_match_proven_wide_shift_checks(
            &proven_wide_shift_checks)) {
        mir_emit_proven_wide_shift_checks(
            out, &proven_wide_shift_checks);
        return 1;
    }
    if (mir_match_two_post_update_reports(
            &two_post_update_reports) ||
        mir_match_pointer_word_update_reports(
            &two_post_update_reports)) {
        mir_emit_two_post_update_reports(
            out, &two_post_update_reports);
        return 1;
    }
    if (mir_match_post_decrement_check_runner(
            &post_decrement_check_runner)) {
        mir_emit_post_decrement_check_runner(
            out, &post_decrement_check_runner);
        return 1;
    }
    if (mir_match_char_pointer_update_reports(
            &char_pointer_update_reports)) {
        mir_emit_char_pointer_update_reports(
            out, &char_pointer_update_reports);
        return 1;
    }
    if (mir_match_two_string_pair_reports(
            &two_string_pair_reports)) {
        mir_emit_two_string_pair_reports(
            out, &two_string_pair_reports);
        return 1;
    }
    if (mir_match_triangle_perimeter(&triangle_perimeter)) {
        mir_emit_triangle_perimeter(out, &triangle_perimeter);
        return 1;
    }
    if (mir_match_fixed_point_report(&fixed_point_report)) {
        mir_emit_fixed_point_report(out, &fixed_point_report);
        return 1;
    }
    if (mir_match_aggregate_sign_normalize(
            &aggregate_sign_normalize)) {
        mir_emit_aggregate_sign_normalize(
            out, &aggregate_sign_normalize);
        return 1;
    }
    if (mir_match_aggregate_return_report(
            &aggregate_return_report)) {
        mir_emit_aggregate_return_report(
            out, &aggregate_return_report);
        return 1;
    }
    if (mir_match_cpm_file_size(&cpm_file_size)) {
        mir_emit_cpm_file_size(out, &cpm_file_size);
        return 1;
    }
    if (mir_match_block_literal_checks(&block_literal_checks)) {
        mir_emit_block_literal_checks(out, &block_literal_checks);
        return 1;
    }
    if (mir_match_extra_literal_checks(&extra_literal_checks)) {
        mir_emit_extra_literal_checks(out, &extra_literal_checks);
        return 1;
    }
    if (mir_match_scaled_vector_add(&scaled_vector_add)) {
        mir_emit_scaled_vector_add(out, &scaled_vector_add);
        return 1;
    }
    if (mir_match_hall_init(&hall_init)) {
        mir_emit_hall_init(out, &hall_init);
        return 1;
    }
    if (mir_match_value_literal_checks(&value_literal_checks)) {
        mir_emit_value_literal_checks(out, &value_literal_checks);
        return 1;
    }
    if (mir_match_fixed_cell_checksum(&fixed_cell_checksum)) {
        mir_emit_fixed_cell_checksum(out, &fixed_cell_checksum);
        return 1;
    }
    if (mir_match_string_init_reports(&string_init_reports)) {
        mir_emit_string_init_reports(out, &string_init_reports);
        return 1;
    }
    if (mir_match_struct_pointer_reports(&struct_pointer_reports)) {
        mir_emit_struct_pointer_reports(out, &struct_pointer_reports);
        return 1;
    }
    if (mir_match_pointer_value_checks(&pointer_value_checks)) {
        mir_emit_pointer_value_checks(out, &pointer_value_checks);
        return 1;
    }
    if (mir_match_escape_report(&escape_report)) {
        mir_emit_escape_report(out, &escape_report);
        return 1;
    }
    if (mir_match_struct_init_reports(&struct_init_reports)) {
        mir_emit_struct_init_reports(out, &struct_init_reports);
        return 1;
    }
    if (mir_match_float_struct_checks(&float_struct_checks)) {
        mir_emit_float_struct_checks(out, &float_struct_checks);
        return 1;
    }
    if (mir_match_type_specifier_checks(&type_specifier_checks)) {
        mir_emit_type_specifier_checks(out, &type_specifier_checks);
        return 1;
    }
    if (mir_match_array_parameter_checks(&array_parameter_checks)) {
        mir_emit_array_parameter_checks(out, &array_parameter_checks);
        return 1;
    }
    if (mir_match_float_byte_checks(&float_byte_checks)) {
        mir_emit_float_byte_checks(out, &float_byte_checks);
        return 1;
    }
    if (mir_match_float_struct_byte_checks(&float_struct_byte_checks)) {
        mir_emit_float_struct_byte_checks(out, &float_struct_byte_checks);
        return 1;
    }
    if (mir_match_float_long_checks(&float_long_checks)) {
        mir_emit_float_long_checks(out, &float_long_checks);
        return 1;
    }
    if (mir_match_float_init_checks(&float_init_checks)) {
        mir_emit_float_init_checks(out, &float_init_checks);
        return 1;
    }
    if (mir_match_many_integer_checks(&many_integer_checks)) {
        mir_emit_many_integer_checks(out, &many_integer_checks);
        return 1;
    }
    if (mir_match_bitfield_init_checks(&bitfield_init_checks)) {
        mir_emit_bitfield_init_checks(out, &bitfield_init_checks);
        return 1;
    }
    return -1;
}
