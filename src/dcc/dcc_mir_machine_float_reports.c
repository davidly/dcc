/**
 * @file dcc_mir_machine_float_reports.c
 * @brief Emits exact floating-point kernels and report schedules.
 *
 * @par Role
 * Matches float sweeps, transcendental wrappers, normalization and log
 * series, pi computations, tolerance/comparison reports, and raw conversion
 * checks while preserving the target's 32-bit float ABI.
 *
 * @par Key entry point
 * mir_try_emit_float_reports().
 */

#include "dcc_mir_machine_internal.h"

#define MIR_FLOAT_REPORT_MAX_CHECKS 80
#define MIR_FLOAT_REPORT_MAX_LOCALS 4
#define MIR_FLOAT_REPORT_MAX_CALL_ARGS 6

struct MirFloatReportLocal {
    int source_value;
    int memory_storage;
    int memory_offset;
    int ix_offset;
    struct Sym *root;
};

struct MirFloatReportCheck {
    int call_index;
    int name_string_id;
};

struct MirFloatReportSetup {
    int call_index;
    int before_check;
    int result_value;
    int output_index;
};

enum MirFloatReportVariant {
    MIR_FLOAT_REPORT_SHARED = 0,
    MIR_FLOAT_REPORT_ASIN_DOMAIN,
    MIR_FLOAT_REPORT_MOD_SPECIAL,
    MIR_FLOAT_REPORT_FMA_BITS
};

struct MirFloatReportSchedule {
    struct MirFloatReportCheck checks[MIR_FLOAT_REPORT_MAX_CHECKS];
    struct MirFloatReportSetup setups[MIR_FLOAT_REPORT_MAX_LOCALS];
    struct MirFloatReportLocal snapshots[MIR_FLOAT_REPORT_MAX_LOCALS];
    struct MirFloatReportLocal outputs[MIR_FLOAT_REPORT_MAX_LOCALS];
    struct Sym *check_functions[3];
    int check_function_uses[3];
    struct Sym *print_function;
    struct Sym *checks_root;
    struct Sym *failures_root;
    int check_count;
    int setup_count;
    int snapshot_count;
    int output_count;
    int checks_offset;
    int failures_offset;
    int summary_string_id;
    int result_string_id;
    int success_string_id;
    int failure_string_id;
    int frame_bytes;
    int variant;
};

#define MIR_RAW_CONVERSION_BOOL_CHECKS 26
#define MIR_RAW_CONVERSION_WIDE_CHECKS 14

struct MirRawConversionCheck {
    struct Sym *value_function;
    unsigned long input;
    unsigned long conversion_input;
    unsigned long expected;
    int input_width;
    int conversion_width;
    int name_string_id;
};

struct MirRawConversionCheckSchedule {
    struct MirRawConversionCheck
        boolean_checks[MIR_RAW_CONVERSION_BOOL_CHECKS];
    struct MirRawConversionCheck
        wide_checks[MIR_RAW_CONVERSION_WIDE_CHECKS];
    struct Sym *boolean_check_function;
    struct Sym *wide_check_function;
    struct Sym *print_function;
    struct Sym *checks_root;
    struct Sym *failures_root;
    int checks_offset;
    int failures_offset;
    int summary_string_id;
    int result_string_id;
    int success_string_id;
    int failure_string_id;
};

struct MirFloatNormalizationSchedule {
    struct Sym *function;
    int value_frame_offset;
    int exponent_frame_offset;
};

struct MirFloatLogSeriesSchedule {
    struct Sym *normalization_function;
    int value_frame_offset;
};

struct MirFloatToleranceSchedule {
    struct Sym *checks;
    struct Sym *failures;
    int name_offset;
    int got_offset;
    int want_offset;
    int tolerance_offset;
    int format_string_id;
    int report_diff;
    char print_name[64];
};

struct MirFloatComparisonReportSchedule {
    struct Sym *absolute_function;
    int left_offset;
    int right_offset;
    unsigned long epsilon_bits;
    int format_string_id;
    char print_name[64];
};

struct MirPiDigitSchedule {
    struct Sym *series_function;
    struct Sym *fraction_function;
    struct Sym *assert_function;
    int parameter_offset;
    int assert_string;
};

struct MirPiSeriesSchedule {
    struct Sym *power_function;
    struct Sym *normalize_function;
    struct Sym *epsilon_function;
    int n_offset;
    int j_offset;
};

struct MirFloatSubtractCallSchedule {
    struct Sym *function;
    int parameter_offset;
    unsigned long left_bits;
};

struct MirFloatAtan2Schedule {
    struct Sym *atan_function;
    int y_offset;
    int x_offset;
    unsigned long zero_bits;
    unsigned long half_pi_bits;
    unsigned long pi_bits;
};

struct MirFloatPowerSchedule {
    struct Sym *exp_function;
    struct Sym *log_function;
    int base_offset;
    int exponent_offset;
    unsigned long zero_bits;
    unsigned long one_bits;
    unsigned long two_long;
};

struct MirFloatAsinSchedule {
    struct Sym *sqrt_function;
    struct Sym *self_function;
    int parameter_offset;
    unsigned long zero_bits;
    unsigned long one_bits;
    unsigned long half_bits;
    unsigned long half_pi_bits;
    unsigned long two_bits;
    unsigned long coefficients[4];
};

#define MIR_FLOAT_SWEEP_GROUPS 4
#define MIR_FLOAT_SWEEP_MAX_VALUES 11

struct MirFloatSweepUnary {
    struct Sym *function;
    int format_string_id;
    char print_name[64];
};

struct MirFloatSweepSchedule {
    unsigned long values[MIR_FLOAT_SWEEP_GROUPS]
                        [MIR_FLOAT_SWEEP_MAX_VALUES];
    int value_counts[MIR_FLOAT_SWEEP_GROUPS];
    struct MirFloatSweepUnary first[4];
    struct MirFloatSweepUnary inverse[2];
    struct MirFloatSweepUnary last[3];
    struct Sym *binary_function;
    struct Sym *print_function;
    struct Sym *check_function;
    struct Sym *slow_sine_function;
    int binary_format_string_id;
    int compare_format_string_id;
    int compare_name_string_id;
    int done_string_id;
    int variant;
    char binary_print_name[64];
    char compare_print_name[64];
    char done_print_name[64];
};

static void mir_float_kernel_load(MirStream *out, int offset);

static int mir_float_report_call_arguments(
    const struct MirInsn *call, int count,
    int arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS])
{
    int found = 0;
    int instruction;
    int argument;

    if (count < 0 || count > MIR_FLOAT_REPORT_MAX_CALL_ARGS)
        return 0;
    for (argument = 0;
         argument < MIR_FLOAT_REPORT_MAX_CALL_ARGS; ++argument)
        arguments[argument] = -1;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *arg = &mir.insns[instruction];
        int index;

        if (arg->opcode != MIR_ARG ||
            arg->secondary_offset != call->secondary_offset)
            continue;
        index = (int)arg->immediate;
        if (index < 0 || index >= count ||
            arguments[index] >= 0)
            return 0;
        arguments[index] = arg->src1;
        ++found;
    }
    return found == count;
}

static int mir_float_report_value_width(int value)
{
    const struct MirInsn *definition = mir_definition(value);
    int width;

    if (definition == NULL)
        return 0;
    if (definition->opcode == MIR_ADDRESS)
        return 2;
    width = type_size(definition->type);
    return width == 2 || width == 4 ? width : 0;
}

static int mir_float_report_find_local(
    const struct MirFloatReportLocal *locals, int count,
    int storage, int offset)
{
    int local;

    for (local = 0; local < count; ++local)
        if (locals[local].memory_storage == storage &&
            locals[local].memory_offset == offset)
            return local;
    return -1;
}

static int mir_float_report_add_output(
    struct MirFloatReportSchedule *plan,
    const struct MirInsn *insn)
{
    int memory_type;
    int memory_storage;
    int memory_offset;
    int local;

    if (!mir_scalar_memory_location(
            insn, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_LOCAL ||
        type_ptr_depth(memory_type) != 0 ||
        !type_is_float(memory_type) ||
        type_size(memory_type) != 4)
        return -1;
    local = mir_float_report_find_local(
        plan->outputs, plan->output_count,
        memory_storage, memory_offset);
    if (local >= 0)
        return local;
    if (plan->output_count >= MIR_FLOAT_REPORT_MAX_LOCALS)
        return -1;
    local = plan->output_count++;
    plan->outputs[local].memory_storage = memory_storage;
    plan->outputs[local].memory_offset = memory_offset;
    plan->outputs[local].root = find_global(insn->name);
    return local;
}

static int mir_float_report_snapshot_index(
    const struct MirFloatReportSchedule *plan, int value)
{
    int snapshot;

    for (snapshot = 0;
         snapshot < plan->snapshot_count; ++snapshot)
        if (plan->snapshots[snapshot].source_value == value)
            return snapshot;
    return -1;
}

static int mir_float_report_setup_index(
    const struct MirFloatReportSchedule *plan, int value)
{
    int setup;

    for (setup = 0; setup < plan->setup_count; ++setup)
        if (plan->setups[setup].result_value == value)
            return setup;
    return -1;
}

static int mir_float_report_validate_expression(
    struct MirFloatReportSchedule *plan, int value, int limit,
    unsigned char *covered, unsigned char *visiting)
{
    const struct MirInsn *definition = mir_definition(value);
    int instruction;

    if (definition == NULL)
        return 0;
    instruction = (int)(definition - mir.insns);
    if (instruction < 0 || instruction >= limit)
        return 0;
    if (covered[instruction] ||
        mir_float_report_snapshot_index(plan, value) >= 0 ||
        mir_float_report_setup_index(plan, value) >= 0)
        return 1;
    if (visiting[instruction])
        return 0;
    visiting[instruction] = 1;
    switch (definition->opcode) {
    case MIR_CONST:
    case MIR_FLOAT_CONST:
    case MIR_STRING_ADDRESS:
        if (mir_float_report_value_width(value) == 0)
            goto reject;
        break;
    case MIR_ADDRESS:
        if (mir_float_report_add_output(plan, definition) < 0)
            goto reject;
        break;
    case MIR_LOAD:
        {
            int memory_type;
            int memory_storage;
            int memory_offset;

            if (!mir_machine_named_nonvolatile(definition) ||
                !mir_scalar_memory_location(
                    definition, &memory_type,
                    &memory_storage, &memory_offset) ||
                mir_float_report_add_output(plan, definition) < 0)
                goto reject;
        }
        break;
    case MIR_UNARY:
        if (definition->immediate != '-' ||
            !type_is_float(definition->type) ||
            type_size(definition->type) != 4 ||
            !mir_float_report_validate_expression(
                plan, definition->src1, instruction,
                covered, visiting))
            goto reject;
        break;
    case MIR_BINARY:
        if (!type_is_float(definition->secondary_offset) ||
            type_size(definition->secondary_offset) != 4 ||
            (!((definition->immediate == '+' ||
                definition->immediate == '-' ||
                definition->immediate == '*' ||
                definition->immediate == '/') &&
               type_is_float(definition->type) &&
               type_size(definition->type) == 4) &&
             !(definition->immediate == TOK_EQ ||
               definition->immediate == TOK_NE ||
               definition->immediate == '<' ||
               definition->immediate == '>' ||
               definition->immediate == TOK_LE ||
               definition->immediate == TOK_GE)))
            goto reject;
        if ((definition->immediate == TOK_EQ ||
             definition->immediate == TOK_NE ||
             definition->immediate == '<' ||
             definition->immediate == '>' ||
             definition->immediate == TOK_LE ||
             definition->immediate == TOK_GE) &&
            !mir_match_final_call_integer_type(
                definition->type, 2))
            goto reject;
        if (!mir_float_report_validate_expression(
                plan, definition->src1, instruction,
                covered, visiting) ||
            !mir_float_report_validate_expression(
                plan, definition->src2, instruction,
                covered, visiting))
            goto reject;
        break;
    case MIR_CALL:
        {
            struct Sym *function = find_global(definition->name);
            int arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];
            int argument;

            if (function == NULL || function->is_funcptr ||
                function->is_noreturn || !function->has_proto ||
                function->proto_variadic ||
                function->proto_nargs < 0 ||
                function->proto_nargs >
                    MIR_FLOAT_REPORT_MAX_CALL_ARGS ||
                definition->memory_flags != 0 ||
                !mir_match_math_symbol_target(
                    definition, function) ||
                mir_float_report_value_width(value) == 0 ||
                !mir_float_report_call_arguments(
                    definition, function->proto_nargs,
                    arguments))
                goto reject;
            for (argument = 0;
                 argument < function->proto_nargs; ++argument) {
                int argument_width =
                    mir_float_report_value_width(
                        arguments[argument]);
                int previous;

                if (argument_width == 0 ||
                    argument_width !=
                        type_size(
                            function->proto_types[argument]) ||
                    !mir_float_report_validate_expression(
                        plan, arguments[argument], instruction,
                        covered, visiting))
                    goto reject;
                for (previous = 0;
                     previous < argument; ++previous)
                    if (arguments[previous] ==
                            arguments[argument] &&
                        mir_definition(arguments[argument])->opcode !=
                            MIR_CONST &&
                        mir_definition(arguments[argument])->opcode !=
                            MIR_FLOAT_CONST &&
                        mir_definition(arguments[argument])->opcode !=
                            MIR_STRING_ADDRESS &&
                        mir_float_report_snapshot_index(
                            plan, arguments[argument]) < 0)
                        goto reject;
            }
            for (argument = 0;
                 argument < mir.count; ++argument)
                if (mir.insns[argument].opcode == MIR_ARG &&
                    mir.insns[argument].secondary_offset ==
                        definition->secondary_offset)
                    covered[argument] = 1;
        }
        break;
    default:
        goto reject;
    }
    covered[instruction] = 1;
    visiting[instruction] = 0;
    return 1;

reject:
    visiting[instruction] = 0;
    return 0;
}

static int mir_float_report_match_global_word(
    const struct MirInsn *load, struct Sym **root_out,
    int *offset_out)
{
    int memory_type;
    int memory_storage;
    int memory_offset;
    struct Sym *root;

    if (load->opcode != MIR_LOAD ||
        !mir_machine_named_nonvolatile(load) ||
        !mir_scalar_memory_location(
            load, &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        !mir_match_final_call_integer_type(memory_type, 2) ||
        (root = find_global(load->name)) == NULL ||
        root->is_volatile)
        return 0;
    *root_out = root;
    *offset_out = memory_offset;
    return 1;
}

static int mir_float_report_match_print(
    const struct MirInsn *call, int argument_count,
    struct Sym **expected)
{
    struct Sym *function = find_global(call->name);

    if (function == NULL || function->is_defined ||
        function->is_funcptr || function->is_noreturn ||
        !function->has_proto ||
        function->proto_nargs != 1 ||
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
    return argument_count == 2 || argument_count == 3;
}

static int mir_match_float_report_tail(
    struct MirFloatReportSchedule *plan, int cursor)
{
    const struct MirInsn *summary_call;
    const struct MirInsn *result_call;
    struct Sym *root;
    int arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];
    int offset;
    int end = mir.count;

    while (end > cursor && mir.insns[end - 1].opcode == MIR_NOP)
        --end;
    if (cursor + 32 > end ||
        mir.insns[cursor].opcode != MIR_STRING_ADDRESS ||
        mir.insns[cursor + 1].opcode != MIR_ARG ||
        mir.insns[cursor + 1].src1 != mir.insns[cursor].dst ||
        !mir_float_report_match_global_word(
            &mir.insns[cursor + 2],
            &plan->checks_root, &plan->checks_offset) ||
        mir.insns[cursor + 3].opcode != MIR_ARG ||
        mir.insns[cursor + 3].src1 !=
            mir.insns[cursor + 2].dst ||
        !mir_float_report_match_global_word(
            &mir.insns[cursor + 4],
            &plan->failures_root, &plan->failures_offset) ||
        plan->checks_root == plan->failures_root ||
        mir.insns[cursor + 5].opcode != MIR_ARG ||
        mir.insns[cursor + 5].src1 !=
            mir.insns[cursor + 4].dst)
        return 0;
    summary_call = &mir.insns[cursor + 6];
    if (summary_call->opcode != MIR_CALL ||
        !mir_float_report_call_arguments(
            summary_call, 3, arguments) ||
        arguments[0] != mir.insns[cursor].dst ||
        arguments[1] != mir.insns[cursor + 2].dst ||
        arguments[2] != mir.insns[cursor + 4].dst ||
        !mir_float_report_match_print(
            summary_call, 3, &plan->print_function))
        return 0;

    if (mir.insns[cursor + 7].opcode != MIR_STRING_ADDRESS ||
        mir.insns[cursor + 8].opcode != MIR_ARG ||
        mir.insns[cursor + 9].opcode != MIR_LOAD ||
        !mir_float_report_match_global_word(
            &mir.insns[cursor + 9], &root, &offset) ||
        root != plan->failures_root ||
        offset != plan->failures_offset ||
        !mir_machine_constant_equals(
            mir.insns[cursor + 10].dst, 0) ||
        mir.insns[cursor + 11].opcode != MIR_BINARY ||
        mir.insns[cursor + 11].immediate != TOK_EQ ||
        mir.insns[cursor + 11].src1 !=
            mir.insns[cursor + 9].dst ||
        mir.insns[cursor + 11].src2 !=
            mir.insns[cursor + 10].dst ||
        mir.insns[cursor + 12].opcode != MIR_BRANCH_FALSE ||
        mir.insns[cursor + 12].src1 !=
            mir.insns[cursor + 11].dst ||
        mir.insns[cursor + 13].opcode != MIR_STRING_ADDRESS ||
        mir.insns[cursor + 14].opcode != MIR_LABEL ||
        mir.insns[cursor + 15].opcode != MIR_JUMP ||
        mir.insns[cursor + 16].opcode != MIR_LABEL ||
        mir.insns[cursor + 17].opcode != MIR_STRING_ADDRESS ||
        mir.insns[cursor + 18].opcode != MIR_LABEL ||
        mir.insns[cursor + 19].opcode != MIR_LABEL ||
        mir.insns[cursor + 20].opcode != MIR_PHI ||
        mir.insns[cursor + 20].src1 !=
            mir.insns[cursor + 13].dst ||
        mir.insns[cursor + 20].src2 !=
            mir.insns[cursor + 17].dst ||
        mir.insns[cursor + 21].opcode != MIR_ARG ||
        mir.insns[cursor + 21].src1 !=
            mir.insns[cursor + 20].dst)
        return 0;
    result_call = &mir.insns[cursor + 22];
    if (result_call->opcode != MIR_CALL ||
        !mir_float_report_call_arguments(
            result_call, 2, arguments) ||
        arguments[0] != mir.insns[cursor + 7].dst ||
        arguments[1] != mir.insns[cursor + 20].dst ||
        !mir_float_report_match_print(
            result_call, 2, &plan->print_function) ||
        mir.insns[cursor + 23].opcode != MIR_LOAD ||
        !mir_float_report_match_global_word(
            &mir.insns[cursor + 23], &root, &offset) ||
        root != plan->failures_root ||
        offset != plan->failures_offset ||
        mir.insns[cursor + 24].opcode != MIR_BRANCH_FALSE ||
        mir.insns[cursor + 24].src1 !=
            mir.insns[cursor + 23].dst)
        return 0;
    if (cursor + 34 == end) {
        if (!mir_machine_constant_equals(
                mir.insns[cursor + 25].dst, 1) ||
            mir.insns[cursor + 26].opcode != MIR_LABEL ||
            mir.insns[cursor + 27].opcode != MIR_JUMP ||
            mir.insns[cursor + 28].opcode != MIR_LABEL ||
            !mir_machine_constant_equals(
                mir.insns[cursor + 29].dst, 0) ||
            mir.insns[cursor + 30].opcode != MIR_LABEL ||
            mir.insns[cursor + 31].opcode != MIR_LABEL ||
            mir.insns[cursor + 32].opcode != MIR_PHI ||
            mir.insns[cursor + 32].src1 !=
                mir.insns[cursor + 25].dst ||
            mir.insns[cursor + 32].src2 !=
                mir.insns[cursor + 29].dst ||
            mir.insns[cursor + 33].opcode != MIR_RETURN ||
            mir.insns[cursor + 33].src1 !=
                mir.insns[cursor + 32].dst)
            return 0;
    } else if (cursor + 32 <= end) {
        int trailing;

        if (!mir_machine_constant_equals(
                mir.insns[cursor + 25].dst, 1) ||
            mir.insns[cursor + 26].opcode != MIR_LABEL ||
            mir.insns[cursor + 27].opcode != MIR_RETURN ||
            mir.insns[cursor + 27].src1 !=
                mir.insns[cursor + 25].dst ||
            mir.insns[cursor + 28].opcode != MIR_LABEL ||
            !mir_machine_constant_equals(
                mir.insns[cursor + 29].dst, 0) ||
            mir.insns[cursor + 30].opcode != MIR_LABEL ||
            mir.insns[cursor + 31].opcode != MIR_RETURN ||
            mir.insns[cursor + 31].src1 !=
                mir.insns[cursor + 29].dst)
            return 0;
        for (trailing = cursor + 32;
             trailing < end; ++trailing)
            if (mir.insns[trailing].opcode != MIR_LABEL)
                return 0;
    } else {
        return 0;
    }
    plan->summary_string_id =
        (int)mir.insns[cursor].immediate;
    plan->result_string_id =
        (int)mir.insns[cursor + 7].immediate;
    plan->success_string_id =
        (int)mir.insns[cursor + 13].immediate;
    plan->failure_string_id =
        (int)mir.insns[cursor + 17].immediate;
    return 1;
}

static int mir_float_report_match_checker(
    const struct MirInsn *call, struct Sym **function_out,
    int arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS])
{
    struct Sym *function = find_global(call->name);
    int width;

    if (function == NULL || !function->is_defined ||
        function->is_funcptr || function->is_noreturn ||
        !function->has_proto || function->proto_variadic ||
        function->proto_nargs != 3 ||
        (call->type & 15) != TYPE_VOID ||
        call->memory_flags != 0 ||
        !mir_match_math_symbol_target(call, function) ||
        type_ptr_depth(function->proto_types[0]) != 1 ||
        (function->proto_types[0] & 15) != TYPE_CHAR ||
        type_size(function->proto_types[0]) != 2 ||
        !mir_float_report_call_arguments(call, 3, arguments))
        return 0;
    width = type_size(function->proto_types[1]);
    if ((width != 2 && width != 4) ||
        type_ptr_depth(function->proto_types[1]) != 0 ||
        type_ptr_depth(function->proto_types[2]) != 0 ||
        type_size(function->proto_types[2]) != width ||
        mir_float_report_value_width(arguments[1]) != width ||
        mir_float_report_value_width(arguments[2]) != width)
        return 0;
    *function_out = function;
    return 1;
}

static int mir_float_report_match_setup(
    struct MirFloatReportSchedule *plan, int *cursor,
    unsigned char *covered, unsigned char *visiting)
{
    int start = *cursor;
    int call_index = -1;
    int store_index = -1;
    int instruction;
    int memory_type;
    int memory_storage;
    int memory_offset;
    int output;

    for (instruction = start;
         instruction < mir.count; ++instruction) {
        if (mir.insns[instruction].opcode == MIR_STRING_ADDRESS ||
            mir.insns[instruction].opcode == MIR_LABEL)
            break;
        if (mir.insns[instruction].opcode == MIR_CALL) {
            if (call_index >= 0)
                return 0;
            call_index = instruction;
        }
    }
    if (call_index < 0 ||
        !type_is_float(mir.insns[call_index].type) ||
        type_size(mir.insns[call_index].type) != 4 ||
        !mir_float_report_validate_expression(
            plan, mir.insns[call_index].dst, call_index + 1,
            covered, visiting))
        return 0;
    for (instruction = call_index + 1;
         instruction < mir.count; ++instruction) {
        if (mir.insns[instruction].opcode == MIR_STRING_ADDRESS ||
            mir.insns[instruction].opcode == MIR_LABEL)
            break;
        if (mir.insns[instruction].opcode == MIR_STORE &&
            mir.insns[instruction].src1 ==
                mir.insns[call_index].dst) {
            store_index = instruction;
            break;
        }
    }
    if (store_index < 0 ||
        plan->setup_count >= MIR_FLOAT_REPORT_MAX_LOCALS ||
        !mir_machine_unobservable_local_store(
            &mir.insns[store_index]) ||
        mir.insns[store_index].memory_size != 4 ||
        !mir_scalar_memory_location(
            &mir.insns[store_index], &memory_type,
            &memory_storage, &memory_offset) ||
        memory_storage != SC_LOCAL ||
        !type_is_float(memory_type) ||
        type_size(memory_type) != 4 ||
        (output = mir_float_report_add_output(
            plan, &mir.insns[store_index])) < 0)
        return 0;
    for (instruction = start;
         instruction <= store_index; ++instruction) {
        if (covered[instruction] ||
            mir.insns[instruction].opcode == MIR_NOP ||
            instruction == store_index) {
            covered[instruction] = 1;
            continue;
        }
        return 0;
    }
    plan->setups[plan->setup_count].call_index = call_index;
    plan->setups[plan->setup_count].before_check =
        plan->check_count;
    plan->setups[plan->setup_count].result_value =
        mir.insns[call_index].dst;
    plan->setups[plan->setup_count].output_index = output;
    ++plan->setup_count;
    *cursor = store_index + 1;
    return 1;
}

static int mir_match_float_report_schedule(
    struct MirFloatReportSchedule *plan)
{
    unsigned char *covered;
    unsigned char *visiting;
    int cursor = 1;
    int check_function_count = 0;
    int call_count = 0;
    int instruction;
    int accepted = 0;

    memset(plan, 0, sizeof(*plan));
    if (mir.count < 140 || mir.count > 800 ||
        mir_cfg_block_count() != 9 || mir.has_vla ||
        type_ptr_depth(mir.return_type) != 0 ||
        !mir_match_final_call_integer_type(mir.return_type, 2) ||
        mir.insns[0].opcode != MIR_LABEL)
        return 0;
    covered = (unsigned char *)calloc((size_t)mir.count, 1);
    visiting = (unsigned char *)calloc((size_t)mir.count, 1);
    if (covered == NULL || visiting == NULL)
        fatal("out of memory matching MIR float reports");
    covered[0] = 1;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_CALL)
            ++call_count;

    while (cursor + 1 < mir.count &&
           (mir.insns[cursor].opcode == MIR_CONST ||
            mir.insns[cursor].opcode == MIR_FLOAT_CONST ||
            mir.insns[cursor].opcode == MIR_LOAD) &&
           mir.insns[cursor + 1].opcode == MIR_STORE) {
        const struct MirInsn *source = &mir.insns[cursor];
        const struct MirInsn *store = &mir.insns[cursor + 1];
        int memory_type;
        int memory_storage;
        int memory_offset;

        if (!mir_machine_unobservable_local_store(store) ||
            store->src1 != source->dst ||
            store->memory_size != 4 ||
            !mir_scalar_memory_location(
                store, &memory_type, &memory_storage,
                &memory_offset) ||
            memory_storage != SC_LOCAL ||
            !type_is_float(memory_type) ||
            type_size(memory_type) != 4)
            goto done;
        if (source->opcode == MIR_LOAD) {
            struct MirFloatReportLocal *snapshot;
            struct Sym *root;

            if (plan->snapshot_count >=
                    MIR_FLOAT_REPORT_MAX_LOCALS ||
                !mir_machine_named_nonvolatile(source) ||
                !mir_scalar_memory_location(
                    source, &memory_type, &memory_storage,
                    &memory_offset) ||
                (memory_storage != SC_GLOBAL &&
                 memory_storage != SC_EXTERN) ||
                !type_is_float(memory_type) ||
                type_size(memory_type) != 4 ||
                (root = find_global(source->name)) == NULL ||
                root->is_volatile)
                goto done;
            snapshot =
                &plan->snapshots[plan->snapshot_count++];
            snapshot->source_value = source->dst;
            snapshot->memory_storage = memory_storage;
            snapshot->memory_offset = memory_offset;
            snapshot->root = root;
        }
        covered[cursor] = covered[cursor + 1] = 1;
        cursor += 2;
    }
    while (cursor < mir.count) {
        const struct MirInsn *name = &mir.insns[cursor];
        struct Sym *function = NULL;
        int arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];
        int call_index = -1;
        int candidate;
        int slot = -1;

        while (cursor < mir.count &&
               mir.insns[cursor].opcode != MIR_STRING_ADDRESS) {
            if (!mir_float_report_match_setup(
                    plan, &cursor, covered, visiting))
                goto done;
        }
        if (cursor >= mir.count)
            break;
        name = &mir.insns[cursor];
        for (candidate = cursor + 1;
             candidate < mir.count; ++candidate) {
            if (mir.insns[candidate].opcode == MIR_LABEL)
                break;
            if (mir.insns[candidate].opcode == MIR_CALL &&
                mir_float_report_match_checker(
                    &mir.insns[candidate],
                    &function, arguments) &&
                arguments[0] == name->dst) {
                call_index = candidate;
                break;
            }
        }
        if (call_index < 0)
            break;
        if (plan->check_count >= MIR_FLOAT_REPORT_MAX_CHECKS)
            goto done;
        for (candidate = 0;
             candidate < check_function_count; ++candidate)
            if (plan->check_functions[candidate] == function)
                slot = candidate;
        if (slot < 0) {
            if (check_function_count >= 3)
                goto done;
            slot = check_function_count;
            plan->check_functions[check_function_count++] = function;
        }
        ++plan->check_function_uses[slot];
        plan->checks[plan->check_count].call_index = call_index;
        plan->checks[plan->check_count].name_string_id =
            (int)name->immediate;
        ++plan->check_count;
        covered[cursor] = covered[call_index] = 1;
        for (instruction = 0; instruction < mir.count; ++instruction)
            if (mir.insns[instruction].opcode == MIR_ARG &&
                mir.insns[instruction].secondary_offset ==
                    mir.insns[call_index].secondary_offset)
                covered[instruction] = 1;
        if (!mir_float_report_validate_expression(
                plan, arguments[1], call_index,
                covered, visiting) ||
            !mir_float_report_validate_expression(
                plan, arguments[2], call_index,
                covered, visiting))
            goto done;
        for (instruction = cursor;
             instruction <= call_index; ++instruction) {
            const struct MirInsn *insn = &mir.insns[instruction];

            if (covered[instruction])
                continue;
            if (insn->opcode == MIR_NOP) {
                covered[instruction] = 1;
                continue;
            }
            if (insn->opcode == MIR_STORE &&
                insn->memory_size == 4 &&
                mir_machine_unobservable_local_store(insn)) {
                covered[instruction] = 1;
                continue;
            }
            goto done;
        }
        cursor = call_index + 1;
    }
    if (!mir_match_float_report_tail(plan, cursor))
        goto done;
    if (plan->check_count >= 14 &&
        call_count >= 38 &&
        plan->snapshot_count > 0 &&
        check_function_count == 2) {
        plan->variant = MIR_FLOAT_REPORT_SHARED;
    } else if (mir.count == 177 &&
               plan->check_count == 12 &&
               call_count == 36 &&
               plan->snapshot_count == 1 &&
               plan->setup_count == 0 &&
               plan->output_count == 0 &&
               check_function_count == 2 &&
               plan->check_function_uses[0] == 10 &&
               plan->check_function_uses[1] == 2) {
        plan->variant = MIR_FLOAT_REPORT_ASIN_DOMAIN;
    } else if (mir.count == 396 &&
               plan->check_count == 27 &&
               call_count == 86 &&
               plan->snapshot_count == 1 &&
               plan->setup_count == 0 &&
               plan->output_count == 0 &&
               check_function_count == 3 &&
               plan->check_function_uses[0] == 8 &&
               plan->check_function_uses[1] == 4 &&
               plan->check_function_uses[2] == 15) {
        plan->variant = MIR_FLOAT_REPORT_MOD_SPECIAL;
    } else if (mir.count == 289 &&
               plan->check_count == 15 &&
               call_count == 61 &&
               plan->snapshot_count == 0 &&
               plan->setup_count == 0 &&
               plan->output_count == 0 &&
               check_function_count == 2 &&
               plan->check_function_uses[0] == 14 &&
               plan->check_function_uses[1] == 1) {
        plan->variant = MIR_FLOAT_REPORT_FMA_BITS;
    } else if (mir.count == 195 &&
               plan->check_count == 9 &&
               call_count == 41 &&
               plan->snapshot_count == 0 &&
               plan->setup_count == 0 &&
               plan->output_count == 0 &&
               check_function_count == 1 &&
               plan->check_function_uses[0] == 9) {
        plan->variant = MIR_FLOAT_REPORT_FMA_BITS;
    } else {
        goto done;
    }
    for (instruction = 0; instruction < cursor; ++instruction)
        if (!covered[instruction])
            goto done;
    for (instruction = 0;
         instruction < plan->snapshot_count; ++instruction)
        plan->snapshots[instruction].ix_offset =
            -4 * (instruction + 1);
    for (instruction = 0;
         instruction < plan->output_count; ++instruction)
        plan->outputs[instruction].ix_offset =
            -4 * (plan->snapshot_count + instruction + 1);
    plan->frame_bytes =
        4 * (plan->snapshot_count + plan->output_count);
    accepted = 1;

done:
    free(covered);
    free(visiting);
    if (!accepted)
        return mir_machine_reject(
            "float-report-schedule", "shape");
    return 1;
}

static int mir_raw_conversion_check_function(
    const struct MirInsn *call, int width, struct Sym **expected,
    int arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS])
{
    struct Sym *function = find_global(call->name);

    if (function == NULL || !function->is_defined ||
        function->is_funcptr || function->is_noreturn ||
        !function->has_proto || function->proto_variadic ||
        function->proto_nargs != 3 ||
        type_ptr_depth(function->proto_types[0]) != 1 ||
        (function->proto_types[0] & 15) != TYPE_CHAR ||
        type_size(function->proto_types[0]) != 2 ||
        type_ptr_depth(function->proto_types[1]) != 0 ||
        type_ptr_depth(function->proto_types[2]) != 0 ||
        type_is_float(function->proto_types[1]) ||
        type_is_float(function->proto_types[2]) ||
        type_size(function->proto_types[1]) != width ||
        type_size(function->proto_types[2]) != width ||
        (call->type & 15) != TYPE_VOID ||
        call->memory_flags != 0 ||
        !mir_match_math_symbol_target(call, function) ||
        !mir_float_report_call_arguments(call, 3, arguments) ||
        (*expected != NULL && *expected != function))
        return 0;
    *expected = function;
    return 1;
}

static int mir_raw_conversion_value_function(
    const struct MirInsn *call, int input_width, int result_width,
    int *argument_value, struct Sym **function_out)
{
    struct Sym *function = find_global(call->name);
    int arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];

    if (function == NULL || !function->is_defined ||
        function->is_funcptr || function->is_noreturn ||
        !function->has_proto || function->proto_variadic ||
        function->proto_nargs != 1 ||
        type_ptr_depth(function->proto_types[0]) != 0 ||
        type_is_float(function->proto_types[0]) ||
        type_size(function->proto_types[0]) != input_width ||
        type_ptr_depth(call->type) != 0 ||
        type_is_float(call->type) ||
        type_size(call->type) != result_width ||
        type_ptr_depth(function->type) != 0 ||
        type_is_float(function->type) ||
        type_size(function->type) != result_width ||
        call->memory_flags != 0 ||
        !mir_match_math_symbol_target(call, function) ||
        !mir_float_report_call_arguments(
            call, 1, arguments))
        return 0;
    *argument_value = arguments[0];
    *function_out = function;
    return 1;
}

static int mir_raw_conversion_segment_calls(
    int start, int *first_call, int *second_call)
{
    int instruction;

    *first_call = -1;
    *second_call = -1;
    for (instruction = start; instruction < mir.count; ++instruction) {
        if (mir.insns[instruction].opcode != MIR_CALL)
            continue;
        if (*first_call < 0)
            *first_call = instruction;
        else {
            *second_call = instruction;
            return 1;
        }
    }
    return 0;
}

static int mir_raw_conversion_allowed_boolean_prefix(
    int start, int end)
{
    int instruction;

    for (instruction = start; instruction <= end; ++instruction)
        switch (mir.insns[instruction].opcode) {
        case MIR_STRING_ADDRESS:
        case MIR_ARG:
        case MIR_CONST:
        case MIR_NOP:
        case MIR_CALL:
            break;
        default:
            return 0;
        }
    return 1;
}

static int mir_raw_conversion_allowed_wide_prefix(
    int start, int end)
{
    int instruction;

    for (instruction = start; instruction <= end; ++instruction)
        switch (mir.insns[instruction].opcode) {
        case MIR_ADDRESS:
        case MIR_MEMBER_ADDRESS:
        case MIR_STRING_ADDRESS:
        case MIR_ARG:
        case MIR_CONST:
        case MIR_NOP:
        case MIR_UNARY:
        case MIR_BINARY:
        case MIR_STORE_INDIRECT:
        case MIR_LOAD_INDIRECT:
        case MIR_CALL:
            break;
        default:
            return 0;
        }
    return 1;
}

static int mir_match_raw_conversion_boolean_check(
    struct MirRawConversionCheckSchedule *plan,
    struct MirRawConversionCheck *check, int start, int *next)
{
    const struct MirInsn *value_call;
    const struct MirInsn *check_call;
    const struct MirInsn *name;
    const struct MirInsn *input;
    const struct MirInsn *expected;
    struct Sym *value_function;
    int check_arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];
    int value_argument;
    int first_call;
    int second_call;
    long input_value;
    long expected_value;

    if (!mir_raw_conversion_segment_calls(
            start, &first_call, &second_call) ||
        !mir_raw_conversion_allowed_boolean_prefix(
            start, second_call))
        return 0;
    value_call = &mir.insns[first_call];
    check_call = &mir.insns[second_call];
    if (!mir_raw_conversion_check_function(
            check_call, 2, &plan->boolean_check_function,
            check_arguments) ||
        !mir_raw_conversion_value_function(
            value_call, 4, 2, &value_argument, &value_function) ||
        check_arguments[1] != value_call->dst)
        return 0;
    name = mir_definition(check_arguments[0]);
    input = mir_definition(value_argument);
    expected = mir_definition(check_arguments[2]);
    if (name == NULL || name->opcode != MIR_STRING_ADDRESS ||
        input == NULL ||
        type_is_float(input->type) ||
        !mir_machine_evaluate_constant(
            input->dst, &input_value, 0) ||
        type_ptr_depth(input->type) != 0 ||
        type_size(input->type) != 4 ||
        expected == NULL ||
        type_is_float(expected->type) ||
        !mir_machine_evaluate_constant(
            expected->dst, &expected_value, 0) ||
        type_ptr_depth(expected->type) != 0 ||
        type_size(expected->type) != 2)
        return 0;
    check->value_function = value_function;
    check->input = (unsigned long)input_value & 0xffffffffUL;
    check->expected = (unsigned long)expected_value & 0xffffUL;
    check->input_width = 4;
    check->name_string_id = (int)name->immediate;
    *next = second_call + 1;
    return 1;
}

static int mir_match_raw_conversion_wide_check(
    struct MirRawConversionCheckSchedule *plan,
    struct MirRawConversionCheck *check, int start, int *next)
{
    const struct MirInsn *value_call;
    const struct MirInsn *check_call;
    const struct MirInsn *name;
    const struct MirInsn *input;
    const struct MirInsn *expected_load;
    const struct MirInsn *expected_member;
    const struct MirInsn *expected_base;
    const struct MirInsn *store = NULL;
    const struct MirInsn *stored_member;
    const struct MirInsn *stored_base;
    const struct MirInsn *conversion;
    const struct MirInsn *conversion_input;
    struct Sym *value_function;
    int check_arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];
    int value_argument;
    int first_call;
    int second_call;
    int instruction;
    int input_width;
    int memory_type;
    int memory_storage;
    int memory_offset;
    long input_value;
    long conversion_value;

    if (!mir_raw_conversion_segment_calls(
            start, &first_call, &second_call) ||
        !mir_raw_conversion_allowed_wide_prefix(
            start, second_call) ||
        !mir_raw_conversion_check_function(
            &mir.insns[second_call], 4,
            &plan->wide_check_function, check_arguments))
        return 0;
    value_call = &mir.insns[first_call];
    check_call = &mir.insns[second_call];
    input = NULL;
    input_width = 0;
    if (mir_raw_conversion_value_function(
            value_call, 2, 4, &value_argument, &value_function))
        input_width = 2;
    else if (mir_raw_conversion_value_function(
                 value_call, 4, 4, &value_argument, &value_function))
        input_width = 4;
    if (input_width == 0 ||
        check_arguments[1] != value_call->dst)
        return 0;
    name = mir_definition(check_arguments[0]);
    input = mir_definition(value_argument);
    expected_load = mir_definition(check_arguments[2]);
    if (name == NULL || name->opcode != MIR_STRING_ADDRESS ||
        input == NULL ||
        !mir_machine_evaluate_constant(
            input->dst, &input_value, 0) ||
        type_size(input->type) != input_width ||
        expected_load == NULL ||
        expected_load->opcode != MIR_LOAD_INDIRECT ||
        expected_load->memory_size != 4 ||
        type_is_float(expected_load->type) ||
        type_size(expected_load->type) != 4)
        return 0;
    expected_member = mir_definition(expected_load->src1);
    expected_base = expected_member != NULL
        ? mir_definition(expected_member->src1) : NULL;
    if (expected_member == NULL ||
        expected_member->opcode != MIR_MEMBER_ADDRESS ||
        expected_member->immediate != 0 ||
        expected_base == NULL ||
        expected_base->opcode != MIR_ADDRESS ||
        !mir_scalar_memory_location(
            expected_base, &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_LOCAL)
        return 0;
    for (instruction = start; instruction < first_call; ++instruction)
        if (mir.insns[instruction].opcode == MIR_STORE_INDIRECT) {
            if (store != NULL)
                return 0;
            store = &mir.insns[instruction];
        }
    if (store == NULL || store->memory_size != 4)
        return 0;
    stored_member = mir_definition(store->src1);
    stored_base = stored_member != NULL
        ? mir_definition(stored_member->src1) : NULL;
    conversion = mir_definition(store->src2);
    conversion_input = conversion != NULL
        ? mir_definition(conversion->src1) : NULL;
    if (stored_member == NULL ||
        stored_member->opcode != MIR_MEMBER_ADDRESS ||
        stored_member->immediate != 0 ||
        stored_base == NULL ||
        stored_base->opcode != MIR_ADDRESS ||
        !mir_machine_same_location(expected_base, stored_base) ||
        conversion == NULL || conversion->opcode != MIR_UNARY ||
        conversion->immediate != 0 ||
        !type_is_float(conversion->type) ||
        type_size(conversion->type) != 4 ||
        conversion_input == NULL ||
        type_is_float(conversion_input->type) ||
        (type_size(conversion_input->type) != 2 &&
         type_size(conversion_input->type) != 4) ||
        !mir_machine_evaluate_constant(
            conversion_input->dst, &conversion_value, 0) ||
        (((unsigned long)input_value) &
         (input_width == 2 ? 0xffffUL : 0xffffffffUL)) !=
        (((unsigned long)conversion_value) &
         (input_width == 2 ? 0xffffUL : 0xffffffffUL)))
        return 0;
    check->value_function = value_function;
    check->input = (unsigned long)input_value &
        (input_width == 2 ? 0xffffUL : 0xffffffffUL);
    check->conversion_input =
        (unsigned long)conversion_value &
        (type_size(conversion_input->type) == 2
             ? 0xffffUL : 0xffffffffUL);
    check->input_width = input_width;
    check->conversion_width = type_size(conversion_input->type);
    check->name_string_id = (int)name->immediate;
    *next = (int)(check_call - mir.insns) + 1;
    return 1;
}

static int mir_match_raw_conversion_check_schedule(
    struct MirRawConversionCheckSchedule *plan)
{
    struct MirFloatReportSchedule tail;
    struct Sym *boolean_functions[4] = {NULL, NULL, NULL, NULL};
    struct Sym *wide_functions[2] = {NULL, NULL};
    static const int boolean_uses[4] = {6, 7, 6, 7};
    int cursor = 1;
    int check;
    int group;
    int use;
    int call_count = 0;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    memset(&tail, 0, sizeof(tail));
    if (mir.count != 519 || mir_cfg_block_count() != 9 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        !mir_match_final_call_integer_type(mir.return_type, 2) ||
        mir.insns[0].opcode != MIR_LABEL)
        return mir_machine_reject(
            "raw-conversion-check-schedule", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_CALL)
            ++call_count;
    if (call_count != 82)
        return mir_machine_reject(
            "raw-conversion-check-schedule", "call-count");
    check = 0;
    for (group = 0; group < 4; ++group)
        for (use = 0; use < boolean_uses[group]; ++use) {
            if (!mir_match_raw_conversion_boolean_check(
                    plan, &plan->boolean_checks[check],
                    cursor, &cursor))
                return mir_machine_reject(
                    "raw-conversion-check-schedule",
                    "boolean-check");
            if (use == 0)
                boolean_functions[group] =
                    plan->boolean_checks[check].value_function;
            else if (boolean_functions[group] !=
                     plan->boolean_checks[check].value_function)
                return mir_machine_reject(
                    "raw-conversion-check-schedule",
                    "boolean-group");
            ++check;
        }
    for (group = 0; group < 4; ++group) {
        int previous;

        for (previous = 0; previous < group; ++previous)
            if (boolean_functions[group] ==
                boolean_functions[previous])
                return mir_machine_reject(
                    "raw-conversion-check-schedule",
                    "boolean-functions");
    }
    for (check = 0;
         check < MIR_RAW_CONVERSION_WIDE_CHECKS; ++check) {
        group = check / 7;
        if (!mir_match_raw_conversion_wide_check(
                plan, &plan->wide_checks[check],
                cursor, &cursor))
            return mir_machine_reject(
                "raw-conversion-check-schedule", "wide-check");
        if (check % 7 == 0)
            wide_functions[group] =
                plan->wide_checks[check].value_function;
        else if (wide_functions[group] !=
                 plan->wide_checks[check].value_function)
            return mir_machine_reject(
                "raw-conversion-check-schedule", "wide-group");
        if (plan->wide_checks[check].input_width !=
            (group == 0 ? 2 : 4))
            return mir_machine_reject(
                "raw-conversion-check-schedule", "wide-width");
    }
    if (wide_functions[0] == wide_functions[1] ||
        plan->boolean_check_function == NULL ||
        plan->wide_check_function == NULL ||
        plan->boolean_check_function == plan->wide_check_function)
        return mir_machine_reject(
            "raw-conversion-check-schedule", "functions");
    while (cursor < mir.count &&
           mir.insns[cursor].opcode == MIR_NOP)
        ++cursor;
    if (!mir_match_float_report_tail(&tail, cursor))
        return mir_machine_reject(
            "raw-conversion-check-schedule", "tail");
    plan->print_function = tail.print_function;
    plan->checks_root = tail.checks_root;
    plan->failures_root = tail.failures_root;
    plan->checks_offset = tail.checks_offset;
    plan->failures_offset = tail.failures_offset;
    plan->summary_string_id = tail.summary_string_id;
    plan->result_string_id = tail.result_string_id;
    plan->success_string_id = tail.success_string_id;
    plan->failure_string_id = tail.failure_string_id;
    return 1;
}

static int mir_float_report_expression_rematerializable(
    const struct MirFloatReportSchedule *plan,
    int value, int depth)
{
    const struct MirInsn *definition;

    if (depth > 32)
        return 0;
    if (mir_float_report_snapshot_index(plan, value) >= 0 ||
        mir_float_report_setup_index(plan, value) >= 0)
        return 1;
    definition = mir_definition(value);
    if (definition == NULL)
        return 0;
    if (definition->opcode == MIR_CONST ||
        definition->opcode == MIR_FLOAT_CONST ||
        definition->opcode == MIR_STRING_ADDRESS ||
        definition->opcode == MIR_ADDRESS)
        return 1;
    return definition->opcode == MIR_UNARY &&
        definition->immediate == '-' &&
        mir_float_report_expression_rematerializable(
            plan, definition->src1, depth + 1);
}

static void mir_float_report_emit_global_wide(
    MirStream *out, struct Sym *symbol, int offset)
{
    const char *name = asm_name_for(sym_asm_name(symbol));

    if ((symbol->storage == SC_EXTERN || symbol->needs_extrn) &&
        mir_extrn_should_emit(symbol))
        mir_stream_printf(out, "\textrn %s\n", name);
    mir_stream_printf(out, "\tld hl,%s\n", name);
    if (offset != 0)
        mir_stream_printf(out, "\tld de,%d\n\tadd hl,de\n", offset);
    mir_stream_puts("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
          "\tinc hl\n\tld e,(hl)\n\tinc hl\n"
          "\tld d,(hl)\n\tld h,b\n\tld l,c\n", out);
}

static void mir_float_report_emit_stack_load(
    MirStream *out, int offset, int width)
{
    if (width == 2) {
        mir_stream_printf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld a,(hl)\n\tinc hl\n"
                "\tld h,(hl)\n\tld l,a\n",
                offset);
        return;
    }
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n"
            "\tld b,(hl)\n\tinc hl\n"
            "\tld e,(hl)\n\tinc hl\n"
            "\tld d,(hl)\n\tld h,b\n\tld l,c\n",
            offset);
}

static void mir_float_report_push_value(MirStream *out, int width)
{
    if (width == 4)
        mir_stream_puts("\tpush de\n\tpush hl\n", out);
    else
        mir_stream_puts("\tpush hl\n", out);
}

static const char *mir_float_report_binary_helper(
    const struct MirInsn *binary)
{
    switch ((int)binary->immediate) {
    case '+': return "__faf";
    case '-': return "__fsf";
    case '*': return "__fmf";
    case '/': return "__fdf";
    case TOK_EQ: return "__feqf";
    case TOK_NE: return "__fnef";
    case '<': return "__fgtf";
    case '>': return "__fltf";
    case TOK_LE: return "__fgef";
    case TOK_GE: return "__flef";
    default: return NULL;
    }
}

static void mir_emit_float_report_expression(
    MirStream *out, const struct MirFloatReportSchedule *plan,
    int value);

static void mir_emit_float_report_call(
    MirStream *out, const struct MirFloatReportSchedule *plan,
    const struct MirInsn *call)
{
    struct Sym *function = find_global(call->name);
    int arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];
    int order[MIR_FLOAT_REPORT_MAX_CALL_ARGS];
    int temp_before[MIR_FLOAT_REPORT_MAX_CALL_ARGS];
    int saved[MIR_FLOAT_REPORT_MAX_CALL_ARGS] = {0, 0, 0, 0};
    int order_count = 0;
    int temp_words = 0;
    int actual_words = 0;
    int direct_argument = -1;
    int argument;

    if (function == NULL ||
        !mir_float_report_call_arguments(
            call, function->proto_nargs, arguments))
        fatal("invalid scheduled float-report call");
    for (argument = 0;
         argument < function->proto_nargs; ++argument) {
        int position;

        temp_before[argument] = 0;
        if (mir_float_report_expression_rematerializable(
                plan, arguments[argument], 0))
            continue;
        position = order_count++;
        while (position > 0 &&
               mir_definition(arguments[order[position - 1]]) >
                   mir_definition(arguments[argument])) {
            order[position] = order[position - 1];
            --position;
        }
        order[position] = argument;
    }
    if (order_count > 0) {
        int last = order[order_count - 1];
        int scheduled;

        direct_argument = last;
        for (scheduled = 0;
             scheduled < order_count - 1; ++scheduled)
            if (order[scheduled] > direct_argument)
                direct_argument = -1;
    }
    for (argument = 0;
         argument < order_count -
             (direct_argument >= 0 ? 1 : 0);
         ++argument) {
        int index = order[argument];
        int width =
            mir_float_report_value_width(arguments[index]);

        temp_before[index] = temp_words;
        mir_emit_float_report_expression(
            out, plan, arguments[index]);
        mir_float_report_push_value(out, width);
        temp_words += width / 2;
        saved[index] = 1;
    }
    for (argument = function->proto_nargs - 1;
         argument >= 0; --argument) {
        int width =
            mir_float_report_value_width(arguments[argument]);
        int words = width / 2;

        if (direct_argument >= 0 &&
            argument == direct_argument) {
            mir_emit_float_report_expression(
                out, plan, arguments[argument]);
            mir_float_report_push_value(out, width);
            actual_words += words;
            continue;
        }
        if (saved[argument]) {
            int base_words =
                temp_words -
                (temp_before[argument] + words);

            mir_float_report_emit_stack_load(
                out, 2 * (base_words + actual_words),
                width);
        } else {
            mir_emit_float_report_expression(
                out, plan, arguments[argument]);
        }
        mir_float_report_push_value(out, width);
        actual_words += words;
    }
    mir_machine_emit_symbol_call(out, function);
    mir_emit_final_call_cleanup(
        out, actual_words + temp_words);
}

static void mir_emit_float_report_expression(
    MirStream *out, const struct MirFloatReportSchedule *plan,
    int value)
{
    const struct MirInsn *definition = mir_definition(value);
    int snapshot =
        mir_float_report_snapshot_index(plan, value);
    int setup =
        mir_float_report_setup_index(plan, value);
    int width = mir_float_report_value_width(value);

    if (snapshot >= 0) {
        mir_machine_emit_ix_wide_load(
            out, plan->snapshots[snapshot].ix_offset);
        return;
    }
    if (setup >= 0) {
        mir_machine_emit_ix_wide_load(
            out,
            plan->outputs[
                plan->setups[setup].output_index].ix_offset);
        return;
    }
    if (definition == NULL)
        fatal("missing scheduled float-report value");
    switch (definition->opcode) {
    case MIR_CONST:
        if (width == 4)
            mir_machine_emit_float_bits(
                out, (unsigned long)definition->immediate);
        else
            mir_stream_printf(out, "\tld hl,%lu\n",
                    (unsigned long)definition->immediate &
                        0xffffUL);
        return;
    case MIR_FLOAT_CONST:
        mir_machine_emit_float_bits(
            out, (unsigned long)definition->immediate);
        return;
    case MIR_STRING_ADDRESS:
        mir_stream_printf(out, "\tld hl,S%ld\n",
                definition->immediate);
        return;
    case MIR_ADDRESS:
        {
            int memory_type;
            int memory_storage;
            int memory_offset;
            int local;

            if (!mir_scalar_memory_location(
                    definition, &memory_type,
                    &memory_storage, &memory_offset))
                fatal("invalid scheduled float-report address");
            local = mir_float_report_find_local(
                plan->outputs, plan->output_count,
                memory_storage, memory_offset);
            if (local < 0)
                fatal("missing scheduled float-report address");
            mir_stream_printf(out,
                    "\tpush ix\n\tpop hl\n"
                    "\tld de,%d\n\tadd hl,de\n",
                    plan->outputs[local].ix_offset);
        }
        return;
    case MIR_LOAD:
        {
            int memory_type;
            int memory_storage;
            int memory_offset;
            int local;

            if (!mir_scalar_memory_location(
                    definition, &memory_type,
                    &memory_storage, &memory_offset))
                fatal("invalid scheduled float-report load");
            local = mir_float_report_find_local(
                plan->outputs, plan->output_count,
                memory_storage, memory_offset);
            if (local < 0)
                fatal("missing scheduled float-report load");
            mir_machine_emit_ix_wide_load(
                out, plan->outputs[local].ix_offset);
        }
        return;
    case MIR_UNARY:
        mir_emit_float_report_expression(
            out, plan, definition->src1);
        mir_stream_puts("\tld a,d\n\txor 128\n\tld d,a\n", out);
        return;
    case MIR_BINARY:
        mir_emit_float_report_expression(
            out, plan, definition->src1);
        mir_stream_puts("\tpush de\n\tpush hl\n", out);
        mir_emit_float_report_expression(
            out, plan, definition->src2);
        mir_emit_runtime_call(
            out, mir_float_report_binary_helper(definition));
        mir_stream_puts("\tpop bc\n\tpop bc\n", out);
        return;
    case MIR_CALL:
        mir_emit_float_report_call(out, plan, definition);
        return;
    default:
        fatal("unsupported scheduled float-report expression");
    }
}

static void mir_emit_float_report_variant_expression(
    MirStream *out, const struct MirFloatReportSchedule *plan,
    int value);

static void mir_emit_float_report_variant_call(
    MirStream *out, const struct MirFloatReportSchedule *plan,
    const struct MirInsn *call)
{
    struct Sym *function = find_global(call->name);
    int arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];
    int actual_words = 0;
    int argument;

    if (function == NULL ||
        !mir_float_report_call_arguments(
            call, function->proto_nargs, arguments))
        fatal("invalid scheduled float-report variant call");
    /* These exact profiles reproduce the legacy ABI argument evaluation
     * order, avoiding temporary saves between nested float calls. */
    for (argument = function->proto_nargs - 1;
         argument >= 0; --argument) {
        int width =
            mir_float_report_value_width(arguments[argument]);

        mir_emit_float_report_variant_expression(
            out, plan, arguments[argument]);
        mir_float_report_push_value(out, width);
        actual_words += width / 2;
    }
    mir_machine_emit_symbol_call(out, function);
    mir_emit_final_call_cleanup(out, actual_words);
}

static void mir_emit_float_report_variant_expression(
    MirStream *out, const struct MirFloatReportSchedule *plan,
    int value)
{
    const struct MirInsn *definition = mir_definition(value);
    int snapshot =
        mir_float_report_snapshot_index(plan, value);
    int setup =
        mir_float_report_setup_index(plan, value);
    int width = mir_float_report_value_width(value);

    if (snapshot >= 0) {
        mir_machine_emit_ix_wide_load(
            out, plan->snapshots[snapshot].ix_offset);
        return;
    }
    if (setup >= 0) {
        mir_machine_emit_ix_wide_load(
            out,
            plan->outputs[
                plan->setups[setup].output_index].ix_offset);
        return;
    }
    if (definition == NULL)
        fatal("missing scheduled float-report variant value");
    switch (definition->opcode) {
    case MIR_CONST:
        if (width == 4)
            mir_machine_emit_float_bits(
                out, (unsigned long)definition->immediate);
        else
            mir_stream_printf(out, "\tld hl,%lu\n",
                    (unsigned long)definition->immediate &
                        0xffffUL);
        return;
    case MIR_FLOAT_CONST:
        mir_machine_emit_float_bits(
            out, (unsigned long)definition->immediate);
        return;
    case MIR_STRING_ADDRESS:
        mir_stream_printf(out, "\tld hl,S%ld\n",
                definition->immediate);
        return;
    case MIR_ADDRESS:
        {
            int memory_type;
            int memory_storage;
            int memory_offset;
            int local;

            if (!mir_scalar_memory_location(
                    definition, &memory_type,
                    &memory_storage, &memory_offset))
                fatal("invalid scheduled float-report variant address");
            local = mir_float_report_find_local(
                plan->outputs, plan->output_count,
                memory_storage, memory_offset);
            if (local < 0)
                fatal("missing scheduled float-report variant address");
            mir_stream_printf(out,
                    "\tpush ix\n\tpop hl\n"
                    "\tld de,%d\n\tadd hl,de\n",
                    plan->outputs[local].ix_offset);
        }
        return;
    case MIR_LOAD:
        {
            int memory_type;
            int memory_storage;
            int memory_offset;
            int local;

            if (!mir_scalar_memory_location(
                    definition, &memory_type,
                    &memory_storage, &memory_offset))
                fatal("invalid scheduled float-report variant load");
            local = mir_float_report_find_local(
                plan->outputs, plan->output_count,
                memory_storage, memory_offset);
            if (local < 0)
                fatal("missing scheduled float-report variant load");
            mir_machine_emit_ix_wide_load(
                out, plan->outputs[local].ix_offset);
        }
        return;
    case MIR_UNARY:
        {
            const struct MirInsn *operand =
                mir_definition(definition->src1);

            if (operand != NULL &&
                (operand->opcode == MIR_CONST ||
                 operand->opcode == MIR_FLOAT_CONST)) {
                mir_machine_emit_float_bits(
                    out,
                    ((unsigned long)operand->immediate) ^
                        0x80000000UL);
                return;
            }
        }
        mir_emit_float_report_variant_expression(
            out, plan, definition->src1);
        mir_stream_puts("\tld a,d\n\txor 128\n\tld d,a\n", out);
        return;
    case MIR_BINARY:
        mir_emit_float_report_variant_expression(
            out, plan, definition->src1);
        mir_stream_puts("\tpush de\n\tpush hl\n", out);
        mir_emit_float_report_variant_expression(
            out, plan, definition->src2);
        mir_emit_runtime_call(
            out, mir_float_report_binary_helper(definition));
        mir_stream_puts("\tpop bc\n\tpop bc\n", out);
        return;
    case MIR_CALL:
        mir_emit_float_report_variant_call(
            out, plan, definition);
        return;
    default:
        fatal("unsupported scheduled float-report variant expression");
    }
}

static void mir_emit_float_report_epilogue(
    MirStream *out, const struct MirFloatReportSchedule *plan)
{
    if (plan->frame_bytes > 0)
        mir_stream_puts("\tld sp,ix\n\tpop ix\n", out);
    mir_stream_puts("\tret\n", out);
}

static void mir_emit_float_report_schedule(
    MirStream *out, const struct MirFloatReportSchedule *plan)
{
    int failure_string = new_label();
    int result_ready = new_label();
    int return_failure = new_label();
    int snapshot;
    int setup = 0;
    int check;

    if (plan->frame_bytes > 0) {
        mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
        mir_stream_printf(out,
                "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n",
                plan->frame_bytes);
    }
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (snapshot = 0;
         snapshot < plan->snapshot_count; ++snapshot) {
        mir_float_report_emit_global_wide(
            out, plan->snapshots[snapshot].root,
            plan->snapshots[snapshot].memory_offset);
        mir_machine_emit_ix_wide_store(
            out, plan->snapshots[snapshot].ix_offset);
    }
    for (check = 0; check < plan->check_count; ++check) {
        while (setup < plan->setup_count &&
               plan->setups[setup].before_check == check) {
            if (plan->variant == MIR_FLOAT_REPORT_SHARED)
                mir_emit_float_report_call(
                    out, plan,
                    &mir.insns[plan->setups[setup].call_index]);
            else
                mir_emit_float_report_variant_call(
                    out, plan,
                    &mir.insns[plan->setups[setup].call_index]);
            mir_machine_emit_ix_wide_store(
                out,
                plan->outputs[
                    plan->setups[setup].output_index].ix_offset);
            ++setup;
        }
        if (plan->variant == MIR_FLOAT_REPORT_SHARED)
            mir_emit_float_report_call(
                out, plan,
                &mir.insns[plan->checks[check].call_index]);
        else
            mir_emit_float_report_variant_call(
                out, plan,
                &mir.insns[plan->checks[check].call_index]);
    }

    mir_machine_emit_global_word(
        out, plan->failures_root, plan->failures_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_global_word(
        out, plan->checks_root, plan->checks_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->summary_string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_emit_final_call_cleanup(out, 3);

    mir_machine_emit_global_word(
        out, plan->failures_root, plan->failures_offset);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    if (plan->variant == MIR_FLOAT_REPORT_SHARED)
        mir_stream_printf(out,
                "\tjp nz,L%d\n\tld hl,S%d\n\tpush hl\n"
                "\tjp L%d\nL%d:\n"
                "\tld hl,S%d\n\tpush hl\nL%d:\n"
                "\tld hl,S%d\n\tpush hl\n",
                failure_string, plan->success_string_id,
                result_ready, failure_string,
                plan->failure_string_id, result_ready,
                plan->result_string_id);
    else
        mir_stream_printf(out,
                "\tjr nz,L%d\n\tld hl,S%d\n\tpush hl\n"
                "\tjr L%d\nL%d:\n"
                "\tld hl,S%d\n\tpush hl\nL%d:\n"
                "\tld hl,S%d\n\tpush hl\n",
                failure_string, plan->success_string_id,
                result_ready, failure_string,
                plan->failure_string_id, result_ready,
                plan->result_string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_emit_final_call_cleanup(out, 2);

    mir_machine_emit_global_word(
        out, plan->failures_root, plan->failures_offset);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    if (plan->variant == MIR_FLOAT_REPORT_SHARED) {
        mir_stream_printf(out, "\tjp nz,L%d\n\tld hl,0\n",
                return_failure);
        mir_emit_float_report_epilogue(out, plan);
        mir_stream_printf(out, "L%d:\n\tld hl,1\n", return_failure);
        mir_emit_float_report_epilogue(out, plan);
    } else {
        mir_stream_printf(out, "\tjr z,L%d\n\tld hl,1\n",
                return_failure);
        mir_emit_float_report_epilogue(out, plan);
        mir_stream_printf(out, "L%d:\n\tld hl,0\n", return_failure);
        mir_emit_float_report_epilogue(out, plan);
    }
}

static void mir_emit_raw_conversion_check_schedule(
    MirStream *out, const struct MirRawConversionCheckSchedule *plan)
{
    int failure_string = new_label();
    int result_ready = new_label();
    int return_success = new_label();
    int check;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (check = 0;
         check < MIR_RAW_CONVERSION_BOOL_CHECKS; ++check) {
        const struct MirRawConversionCheck *item =
            &plan->boolean_checks[check];

        mir_emit_final_call_constant(out, item->expected, 2);
        mir_emit_final_call_constant(out, item->input, 4);
        mir_machine_emit_symbol_call(out, item->value_function);
        mir_emit_final_call_cleanup(out, 2);
        mir_stream_puts("\tpush hl\n", out);
        mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
                item->name_string_id);
        mir_machine_emit_symbol_call(
            out, plan->boolean_check_function);
        mir_emit_final_call_cleanup(out, 3);
    }
    for (check = 0;
         check < MIR_RAW_CONVERSION_WIDE_CHECKS; ++check) {
        const struct MirRawConversionCheck *item =
            &plan->wide_checks[check];

        if (item->conversion_width == 2) {
            mir_stream_printf(out, "\tld hl,%lu\n",
                    item->conversion_input & 0xffffUL);
            mir_emit_runtime_call(out, "__fif");
        } else {
            mir_machine_emit_float_bits(
                out, item->conversion_input);
            mir_emit_runtime_call(out, "__flf");
        }
        mir_stream_puts("\tpush de\n\tpush hl\n", out);
        mir_emit_final_call_constant(
            out, item->input, item->input_width);
        mir_machine_emit_symbol_call(out, item->value_function);
        mir_emit_final_call_cleanup(
            out, item->input_width / 2);
        mir_stream_puts("\tpush de\n\tpush hl\n", out);
        mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
                item->name_string_id);
        mir_machine_emit_symbol_call(
            out, plan->wide_check_function);
        mir_emit_final_call_cleanup(out, 5);
    }

    mir_machine_emit_global_word(
        out, plan->failures_root, plan->failures_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_global_word(
        out, plan->checks_root, plan->checks_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->summary_string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_emit_final_call_cleanup(out, 3);

    mir_machine_emit_global_word(
        out, plan->failures_root, plan->failures_offset);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out,
            "\tjr nz,L%d\n\tld hl,S%d\n\tpush hl\n"
            "\tjr L%d\nL%d:\n\tld hl,S%d\n\tpush hl\n"
            "L%d:\n\tld hl,S%d\n\tpush hl\n",
            failure_string, plan->success_string_id,
            result_ready, failure_string,
            plan->failure_string_id, result_ready,
            plan->result_string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_emit_final_call_cleanup(out, 2);

    mir_machine_emit_global_word(
        out, plan->failures_root, plan->failures_offset);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out,
            "\tjr z,L%d\n\tld hl,1\n\tret\n"
            "L%d:\n\tld hl,0\n\tret\n",
            return_success, return_success);
}

static int mir_float_normalization_type(int type)
{
    return type_ptr_depth(type) == 0 &&
        type_is_float(type) && type_size(type) == 4;
}

static int mir_float_normalization_parameter_load(
    const struct MirInsn *load, const struct MirInsn *parameter)
{
    return load->opcode == MIR_LOAD &&
        mir_machine_named_nonvolatile(load) &&
        mir_machine_same_location(load, parameter);
}

static int mir_float_log_float_constant(int instruction,
                                        unsigned long bits)
{
    return mir.insns[instruction].opcode == MIR_FLOAT_CONST &&
        mir_float_normalization_type(mir.insns[instruction].type) &&
        ((unsigned long)mir.insns[instruction].immediate &
         0xffffffffUL) == bits;
}

static int mir_float_log_binary(int instruction, int operation,
                                int left_instruction,
                                int right_instruction,
                                int result_is_float)
{
    const struct MirInsn *binary = &mir.insns[instruction];

    return binary->opcode == MIR_BINARY &&
        binary->immediate == operation &&
        binary->secondary_offset == TYPE_FLOAT &&
        binary->src1 == mir.insns[left_instruction].dst &&
        binary->src2 == mir.insns[right_instruction].dst &&
        (result_is_float
             ? mir_float_normalization_type(binary->type)
             : mir_match_final_call_integer_type(binary->type, 2));
}

static int mir_float_log_local(const struct MirInsn *insn, int offset,
                               int width, int is_float)
{
    int type;
    int storage;
    int actual_offset;

    return mir_scalar_memory_location(
               insn, &type, &storage, &actual_offset) &&
        storage == SC_LOCAL && actual_offset == offset &&
        (insn->opcode == MIR_ADDRESS ||
         (insn->memory_flags == 0 && insn->memory_size == width)) &&
        type_ptr_depth(type) == 0 && type_size(type) == width &&
        (is_float ? type_is_float(type) : (type & 15) == TYPE_INT);
}

static int mir_match_float_log_series_schedule(
    struct MirFloatLogSeriesSchedule *plan)
{
    static const int expected_opcodes[139] = {
        MIR_LABEL, MIR_PARAM, MIR_FLOAT_CONST, MIR_STORE, MIR_NOP,
        MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP,
        MIR_BINARY, MIR_RETURN, MIR_LABEL, MIR_NOP, MIR_FLOAT_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_FLOAT_CONST, MIR_UNARY, MIR_NOP,
        MIR_BINARY, MIR_RETURN, MIR_LABEL, MIR_NOP, MIR_ARG, MIR_ADDRESS,
        MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_FLOAT_CONST, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP, MIR_LABEL, MIR_LOAD,
        MIR_FLOAT_CONST, MIR_BINARY, MIR_LOAD, MIR_FLOAT_CONST, MIR_BINARY,
        MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP, MIR_NOP, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_NOP, MIR_STORE, MIR_NOP, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_NOP, MIR_FLOAT_CONST, MIR_BINARY, MIR_BINARY, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_NOP, MIR_FLOAT_CONST, MIR_BINARY, MIR_BINARY, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_NOP, MIR_FLOAT_CONST, MIR_BINARY, MIR_BINARY, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_NOP, MIR_FLOAT_CONST, MIR_BINARY, MIR_BINARY, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_NOP, MIR_FLOAT_CONST, MIR_BINARY, MIR_BINARY, MIR_NOP,
        MIR_STORE, MIR_FLOAT_CONST, MIR_NOP, MIR_BINARY, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_LOAD, MIR_UNARY, MIR_FLOAT_CONST, MIR_BINARY, MIR_BINARY,
        MIR_RETURN
    };
    static const int labels[4] = {0, 12, 22, 46};
    static const int sum_stores[6] = {63, 78, 90, 102, 114, 126};
    static const int term_stores[6] = {66, 71, 83, 95, 107, 119};
    static const int term_products[5] = {69, 81, 93, 105, 117};
    static const int divisors[5] = {74, 86, 98, 110, 122};
    static const unsigned long divisor_bits[5] = {
        1077936128UL, 1084227584UL, 1088421888UL,
        1091567616UL, 1093664768UL
    };
    static const int quotients[5] = {75, 87, 99, 111, 123};
    static const int sums[5] = {76, 88, 100, 112, 124};
    const struct MirInsn *value = &mir.insns[1];
    const struct MirInsn *call = &mir.insns[28];
    struct Sym *function;
    int arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];
    int value_type;
    int value_storage;
    int value_offset;
    int instruction;
    int index;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 139 || mir_cfg_block_count() != 4 ||
        mir.has_vla || mir.local_bytes != 30 ||
        mir.aggregate_temp_bytes != 0 ||
        !mir_float_normalization_type(mir.return_type))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    for (index = 0; index < 4; ++index) {
        int other;

        for (other = index + 1; other < 4; ++other)
            if (mir.insns[labels[index]].label ==
                mir.insns[labels[other]].label)
                return 0;
    }
    if (!mir_scalar_memory_location(
            value, &value_type, &value_storage, &value_offset) ||
        value_storage != SC_PARAM || value_offset != 4 ||
        !mir_float_normalization_type(value_type) ||
        !mir_float_normalization_type(value->type) ||
        !mir_machine_same_location(value, &mir.insns[4]) ||
        !mir_machine_same_location(value, &mir.insns[13]) ||
        !mir_machine_same_location(value, &mir.insns[23]))
        return 0;

    if (!mir_float_log_local(&mir.insns[3], -30, 4, 1) ||
        !mir_float_log_local(&mir.insns[30], -4, 4, 1) ||
        !mir_float_log_local(&mir.insns[25], -26, 2, 0) ||
        type_ptr_depth(mir.insns[25].type) != 1 ||
        (mir.insns[25].type & 15) != TYPE_INT ||
        !mir_float_log_local(&mir.insns[55], -8, 4, 1) ||
        !mir_float_log_local(&mir.insns[60], -12, 4, 1) ||
        !mir_float_log_local(&mir.insns[63], -16, 4, 1) ||
        !mir_float_log_local(&mir.insns[66], -20, 4, 1) ||
        !mir_float_log_local(&mir.insns[131], -24, 4, 1))
        return 0;
    if (!mir_machine_same_location(&mir.insns[30], &mir.insns[39]) ||
        !mir_machine_same_location(&mir.insns[30], &mir.insns[47]) ||
        !mir_machine_same_location(&mir.insns[30], &mir.insns[50]) ||
        !mir_machine_same_location(&mir.insns[25], &mir.insns[40]) ||
        !mir_machine_same_location(&mir.insns[25], &mir.insns[44]) ||
        !mir_machine_same_location(&mir.insns[25], &mir.insns[133]))
        return 0;
    for (index = 1; index < 6; ++index)
        if (!mir_machine_same_location(
                &mir.insns[sum_stores[0]],
                &mir.insns[sum_stores[index]]) ||
            !mir_machine_same_location(
                &mir.insns[term_stores[0]],
                &mir.insns[term_stores[index]]))
            return 0;

    if (!mir_float_log_float_constant(2, 0) ||
        mir.insns[3].src1 != mir.insns[2].dst ||
        !mir_float_log_float_constant(5, 0) ||
        !mir_float_log_binary(6, '<', 1, 5, 0) ||
        mir.insns[7].src1 != mir.insns[6].dst ||
        mir.insns[7].label != mir.insns[12].label ||
        !mir_float_log_binary(10, '/', 2, 2, 1) ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        !mir_float_log_float_constant(14, 0) ||
        !mir_float_log_binary(15, TOK_EQ, 1, 14, 0) ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[16].label != mir.insns[22].label ||
        !mir_float_log_float_constant(17, 1065353216UL) ||
        mir.insns[18].immediate != '-' ||
        mir.insns[18].src1 != mir.insns[17].dst ||
        !mir_float_normalization_type(mir.insns[18].type) ||
        !mir_float_log_binary(20, '/', 18, 2, 1) ||
        mir.insns[21].src1 != mir.insns[20].dst)
        return 0;

    if (!mir_float_report_call_arguments(call, 2, arguments) ||
        arguments[0] != value->dst ||
        arguments[1] != mir.insns[25].dst ||
        mir.insns[24].src1 != value->dst ||
        mir.insns[24].immediate != 0 ||
        mir.insns[24].secondary_offset != call->secondary_offset ||
        mir.insns[27].src1 != mir.insns[25].dst ||
        mir.insns[27].immediate != 1 ||
        mir.insns[27].secondary_offset != call->secondary_offset ||
        mir.insns[30].src1 != call->dst)
        return 0;
    function = find_global(call->name);
    if (function == NULL || !function->is_defined ||
        function->is_funcptr || function->is_noreturn ||
        !function->has_proto || function->proto_variadic ||
        function->proto_nargs != 2 ||
        !mir_float_normalization_type(function->type) ||
        !mir_float_normalization_type(function->proto_types[0]) ||
        type_ptr_depth(function->proto_types[1]) != 1 ||
        (function->proto_types[1] & 15) != TYPE_INT ||
        !strcmp(call->name, mir.name) ||
        call->memory_flags != 0 ||
        !mir_float_normalization_type(call->type) ||
        !mir_match_math_symbol_target(call, function))
        return 0;

    if (!mir_float_log_float_constant(32, 1060439283UL) ||
        !mir_float_log_binary(33, '<', 28, 32, 0) ||
        mir.insns[34].src1 != mir.insns[33].dst ||
        mir.insns[34].label != mir.insns[46].label ||
        !mir_float_log_float_constant(36, 1073741824UL) ||
        !mir_float_log_binary(37, '*', 28, 36, 1) ||
        mir.insns[39].src1 != mir.insns[37].dst ||
        !mir_machine_constant_equals(mir.insns[41].dst, 1) ||
        mir.insns[42].immediate != '-' ||
        mir.insns[42].src1 != mir.insns[40].dst ||
        mir.insns[42].src2 != mir.insns[41].dst ||
        !mir_match_final_call_integer_type(mir.insns[42].type, 2) ||
        mir.insns[44].src1 != mir.insns[42].dst)
        return 0;

    if (!mir_float_log_float_constant(48, 1065353216UL) ||
        !mir_float_log_binary(49, '-', 47, 48, 1) ||
        !mir_float_log_float_constant(51, 1065353216UL) ||
        !mir_float_log_binary(52, '+', 50, 51, 1) ||
        !mir_float_log_binary(53, '/', 49, 52, 1) ||
        mir.insns[55].src1 != mir.insns[53].dst ||
        !mir_float_log_binary(58, '*', 53, 53, 1) ||
        mir.insns[60].src1 != mir.insns[58].dst ||
        mir.insns[63].src1 != mir.insns[53].dst ||
        mir.insns[66].src1 != mir.insns[53].dst)
        return 0;

    for (index = 0; index < 5; ++index) {
        int previous_term =
            index == 0 ? 53 : term_products[index - 1];
        int previous_sum =
            index == 0 ? 53 : sums[index - 1];

        if (!mir_float_log_binary(
                term_products[index], '*', previous_term, 58, 1) ||
            mir.insns[term_stores[index + 1]].src1 !=
                mir.insns[term_products[index]].dst ||
            !mir_float_log_float_constant(
                divisors[index], divisor_bits[index]) ||
            !mir_float_log_binary(
                quotients[index], '/',
                term_products[index], divisors[index], 1) ||
            !mir_float_log_binary(
                sums[index], '+',
                previous_sum, quotients[index], 1) ||
            mir.insns[sum_stores[index + 1]].src1 !=
                mir.insns[sums[index]].dst)
            return 0;
    }

    if (!mir_float_log_float_constant(127, 1073741824UL) ||
        !mir_float_log_binary(129, '*', 127, 124, 1) ||
        mir.insns[131].src1 != mir.insns[129].dst ||
        mir.insns[133].opcode != MIR_LOAD ||
        mir.insns[134].immediate != 0 ||
        mir.insns[134].src1 != mir.insns[133].dst ||
        !mir_float_normalization_type(mir.insns[134].type) ||
        !mir_float_log_float_constant(135, 1060205080UL) ||
        !mir_float_log_binary(136, '*', 134, 135, 1) ||
        !mir_float_log_binary(137, '+', 129, 136, 1) ||
        mir.insns[138].src1 != mir.insns[137].dst)
        return 0;

    plan->normalization_function = function;
    plan->value_frame_offset = value_offset;
    return 1;
}

static int mir_match_float_normalization_schedule(
    struct MirFloatNormalizationSchedule *plan)
{
    static const int expected_opcodes[72] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_LOAD, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_NOP, MIR_FLOAT_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_FLOAT_CONST, MIR_RETURN, MIR_NOP, MIR_LABEL,
        MIR_LOAD, MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD,
        MIR_UNARY, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_UNARY,
        MIR_RETURN, MIR_NOP, MIR_LABEL, MIR_LABEL, MIR_NOP, MIR_LOAD,
        MIR_LOAD, MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD,
        MIR_FLOAT_CONST, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LOAD,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY, MIR_STORE_INDIRECT,
        MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_LABEL, MIR_NOP,
        MIR_LOAD, MIR_LOAD, MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LOAD, MIR_FLOAT_CONST, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LOAD,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY, MIR_STORE_INDIRECT,
        MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_RETURN
    };
    static const int label_indices[9] = {
        0, 13, 27, 28, 46, 48, 49, 67, 69
    };
    static const int value_load_indices[7] = {
        14, 18, 31, 35, 52, 56, 70
    };
    static const int exponent_load_indices[6] = {
        3, 21, 30, 40, 51, 61
    };
    const struct MirInsn *value;
    const struct MirInsn *exponent;
    const struct MirInsn *call;
    int arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];
    int value_type, value_storage, value_offset;
    int exponent_type, exponent_storage, exponent_offset;
    int instruction;
    int left;
    int right;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 72 || mir_cfg_block_count() != 9 ||
        mir.has_vla || mir.local_bytes != 0 ||
        mir.aggregate_temp_bytes != 0 ||
        !mir_float_normalization_type(mir.return_type))
        return 0;
    value = &mir.insns[1];
    exponent = &mir.insns[2];
    call = &mir.insns[23];
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    for (left = 0; left < 9; ++left)
        for (right = left + 1; right < 9; ++right)
            if (mir.insns[label_indices[left]].label ==
                mir.insns[label_indices[right]].label)
                return 0;
    if (!mir_float_normalization_type(value->type) ||
        type_ptr_depth(exponent->type) != 1 ||
        (exponent->type & 15) != TYPE_INT ||
        mir_machine_pointee_is_volatile(exponent) ||
        !mir_scalar_memory_location(
            value, &value_type, &value_storage, &value_offset) ||
        value_storage != SC_PARAM ||
        !mir_float_normalization_type(value_type) ||
        !mir_scalar_memory_location(
            exponent, &exponent_type, &exponent_storage, &exponent_offset) ||
        exponent_storage != SC_PARAM ||
        type_ptr_depth(exponent_type) != 1 ||
        (exponent_type & 15) != TYPE_INT ||
        value_offset != 4 || exponent_offset != 8)
        return 0;
    for (instruction = 0; instruction < 7; ++instruction)
        if (!mir_float_normalization_parameter_load(
                &mir.insns[value_load_indices[instruction]], value))
            return 0;
    for (instruction = 0; instruction < 6; ++instruction)
        if (!mir_float_normalization_parameter_load(
                &mir.insns[exponent_load_indices[instruction]], exponent))
            return 0;

    if (!mir_machine_constant_equals(mir.insns[4].dst, 0) ||
        mir.insns[5].src1 != mir.insns[3].dst ||
        mir.insns[5].src2 != mir.insns[4].dst ||
        mir.insns[5].memory_size != 2 ||
        ((unsigned long)mir.insns[7].immediate & 0xffffffffUL) != 0 ||
        !mir_float_normalization_type(mir.insns[7].type) ||
        mir.insns[8].immediate != TOK_EQ ||
        mir.insns[8].secondary_offset != TYPE_FLOAT ||
        mir.insns[8].src1 != value->dst ||
        mir.insns[8].src2 != mir.insns[7].dst ||
        !mir_match_final_call_integer_type(mir.insns[8].type, 2) ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].label != mir.insns[13].label ||
        ((unsigned long)mir.insns[10].immediate & 0xffffffffUL) != 0 ||
        !mir_float_normalization_type(mir.insns[10].type) ||
        mir.insns[11].src1 != mir.insns[10].dst)
        return 0;

    if (((unsigned long)mir.insns[15].immediate & 0xffffffffUL) != 0 ||
        !mir_float_normalization_type(mir.insns[15].type) ||
        mir.insns[16].immediate != '<' ||
        mir.insns[16].secondary_offset != TYPE_FLOAT ||
        mir.insns[16].src1 != mir.insns[14].dst ||
        mir.insns[16].src2 != mir.insns[15].dst ||
        !mir_match_final_call_integer_type(mir.insns[16].type, 2) ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        mir.insns[17].label != mir.insns[27].label ||
        mir.insns[19].immediate != '-' ||
        mir.insns[19].src1 != mir.insns[18].dst ||
        !mir_float_normalization_type(mir.insns[19].type) ||
        !mir_float_report_call_arguments(call, 2, arguments) ||
        arguments[0] != mir.insns[19].dst ||
        arguments[1] != mir.insns[21].dst ||
        mir.insns[24].immediate != '-' ||
        mir.insns[24].src1 != call->dst ||
        !mir_float_normalization_type(mir.insns[24].type) ||
        mir.insns[25].src1 != mir.insns[24].dst)
        return 0;

    if (((unsigned long)mir.insns[32].immediate & 0xffffffffUL) !=
            1065353216UL ||
        !mir_float_normalization_type(mir.insns[32].type) ||
        mir.insns[33].immediate != TOK_GE ||
        mir.insns[33].secondary_offset != TYPE_FLOAT ||
        mir.insns[33].src1 != mir.insns[31].dst ||
        mir.insns[33].src2 != mir.insns[32].dst ||
        !mir_match_final_call_integer_type(mir.insns[33].type, 2) ||
        mir.insns[34].src1 != mir.insns[33].dst ||
        mir.insns[34].label != mir.insns[48].label ||
        ((unsigned long)mir.insns[36].immediate & 0xffffffffUL) !=
            1056964608UL ||
        !mir_float_normalization_type(mir.insns[36].type) ||
        mir.insns[37].immediate != '*' ||
        mir.insns[37].secondary_offset != TYPE_FLOAT ||
        mir.insns[37].src1 != mir.insns[35].dst ||
        mir.insns[37].src2 != mir.insns[36].dst ||
        !mir_float_normalization_type(mir.insns[37].type) ||
        !mir_machine_same_location(&mir.insns[39], value) ||
        mir.insns[39].src1 != mir.insns[37].dst ||
        mir.insns[39].memory_size != 4 ||
        mir.insns[41].src1 != mir.insns[40].dst ||
        mir.insns[41].memory_size != 2 ||
        !mir_match_final_call_integer_type(mir.insns[41].type, 2) ||
        !mir_machine_constant_equals(mir.insns[42].dst, 1) ||
        mir.insns[43].immediate != '+' ||
        mir.insns[43].src1 != mir.insns[41].dst ||
        mir.insns[43].src2 != mir.insns[42].dst ||
        !mir_match_final_call_integer_type(mir.insns[43].type, 2) ||
        mir.insns[44].src1 != mir.insns[40].dst ||
        mir.insns[44].src2 != mir.insns[43].dst ||
        mir.insns[44].memory_size != 2 ||
        mir.insns[47].label != mir.insns[28].label)
        return 0;

    if (((unsigned long)mir.insns[53].immediate & 0xffffffffUL) !=
            1056964608UL ||
        !mir_float_normalization_type(mir.insns[53].type) ||
        mir.insns[54].immediate != '<' ||
        mir.insns[54].secondary_offset != TYPE_FLOAT ||
        mir.insns[54].src1 != mir.insns[52].dst ||
        mir.insns[54].src2 != mir.insns[53].dst ||
        !mir_match_final_call_integer_type(mir.insns[54].type, 2) ||
        mir.insns[55].src1 != mir.insns[54].dst ||
        mir.insns[55].label != mir.insns[69].label ||
        ((unsigned long)mir.insns[57].immediate & 0xffffffffUL) !=
            1073741824UL ||
        !mir_float_normalization_type(mir.insns[57].type) ||
        mir.insns[58].immediate != '*' ||
        mir.insns[58].secondary_offset != TYPE_FLOAT ||
        mir.insns[58].src1 != mir.insns[56].dst ||
        mir.insns[58].src2 != mir.insns[57].dst ||
        !mir_float_normalization_type(mir.insns[58].type) ||
        !mir_machine_same_location(&mir.insns[60], value) ||
        mir.insns[60].src1 != mir.insns[58].dst ||
        mir.insns[60].memory_size != 4 ||
        mir.insns[62].src1 != mir.insns[61].dst ||
        mir.insns[62].memory_size != 2 ||
        !mir_match_final_call_integer_type(mir.insns[62].type, 2) ||
        !mir_machine_constant_equals(mir.insns[63].dst, 1) ||
        mir.insns[64].immediate != '-' ||
        mir.insns[64].src1 != mir.insns[62].dst ||
        mir.insns[64].src2 != mir.insns[63].dst ||
        !mir_match_final_call_integer_type(mir.insns[64].type, 2) ||
        mir.insns[65].src1 != mir.insns[61].dst ||
        mir.insns[65].src2 != mir.insns[64].dst ||
        mir.insns[65].memory_size != 2 ||
        mir.insns[68].label != mir.insns[49].label ||
        mir.insns[71].src1 != mir.insns[70].dst)
        return 0;

    plan->function = find_global(call->name);
    if (plan->function == NULL || !plan->function->is_defined ||
        plan->function->is_funcptr || plan->function->is_noreturn ||
        !plan->function->has_proto ||
        plan->function->proto_nargs != 2 ||
        plan->function->proto_variadic ||
        !mir_float_normalization_type(plan->function->proto_types[0]) ||
        type_ptr_depth(plan->function->proto_types[1]) != 1 ||
        (plan->function->proto_types[1] & 15) != TYPE_INT ||
        strcmp(call->name, mir.name) ||
        call->memory_flags != 0 ||
        !mir_float_normalization_type(call->type) ||
        !mir_match_math_symbol_target(call, plan->function))
        return 0;
    plan->value_frame_offset = value_offset;
    plan->exponent_frame_offset = exponent_offset;
    return 1;
}

static void mir_emit_float_normalization_frame_word(
    MirStream *out, int offset)
{
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            offset, offset + 1);
}

static void mir_emit_float_normalization_frame_float(
    MirStream *out, int offset)
{
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
            offset, offset + 1, offset + 2, offset + 3);
}

static void mir_emit_float_normalization_store_frame_float(
    MirStream *out, int offset)
{
    mir_stream_printf(out,
            "\tld (ix%+d),l\n\tld (ix%+d),h\n"
            "\tld (ix%+d),e\n\tld (ix%+d),d\n",
            offset, offset + 1, offset + 2, offset + 3);
}

static void mir_emit_float_log_iteration(MirStream *out,
                                         unsigned long divisor_bits)
{
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, -4);
    mir_emit_runtime_call(out, "__fmf");
    mir_emit_final_call_cleanup(out, 2);

    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, divisor_bits);
    mir_emit_runtime_call(out, "__fdf");
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, -8);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_float_report_emit_stack_load(out, 4, 4);
    mir_emit_runtime_call(out, "__faf");
    mir_emit_final_call_cleanup(out, 4);
    mir_machine_emit_ix_wide_store(out, -8);
    mir_stream_puts("\tpop hl\n\tpop de\n", out);
}

static void mir_emit_float_log_series_schedule(
    MirStream *out, const struct MirFloatLogSeriesSchedule *plan)
{
    static const unsigned long divisor_bits[5] = {
        1077936128UL, 1084227584UL, 1088421888UL,
        1091567616UL, 1093664768UL
    };
    int nonnegative = new_label();
    int nonzero = new_label();
    int reduced = new_label();
    int finish = new_label();
    int index;

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-10\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    mir_machine_emit_ix_wide_load(out, plan->value_frame_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, 0);
    mir_emit_runtime_call(out, "__fgtf");
    mir_emit_final_call_cleanup(out, 2);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjr z,L%d\n", nonnegative);
    mir_machine_emit_float_bits(out, 0);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, 0);
    mir_emit_runtime_call(out, "__fdf");
    mir_emit_final_call_cleanup(out, 2);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", finish, nonnegative);

    mir_machine_emit_ix_wide_load(out, plan->value_frame_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, 0);
    mir_emit_runtime_call(out, "__feqf");
    mir_emit_final_call_cleanup(out, 2);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjr z,L%d\n", nonzero);
    mir_machine_emit_float_bits(out, 1065353216UL ^ 0x80000000UL);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, 0);
    mir_emit_runtime_call(out, "__fdf");
    mir_emit_final_call_cleanup(out, 2);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", finish, nonzero);

    mir_stream_puts("\tpush ix\n\tpop hl\n\tld de,-10\n\tadd hl,de\n"
          "\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, plan->value_frame_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->normalization_function);
    mir_emit_final_call_cleanup(out, 3);
    mir_machine_emit_ix_wide_store(out, -4);

    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, 1060439283UL);
    mir_emit_runtime_call(out, "__fgtf");
    mir_emit_final_call_cleanup(out, 2);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjr z,L%d\n", reduced);
    mir_machine_emit_ix_wide_load(out, -4);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, 1073741824UL);
    mir_emit_runtime_call(out, "__fmf");
    mir_emit_final_call_cleanup(out, 2);
    mir_machine_emit_ix_wide_store(out, -4);
    mir_stream_puts("\tld a,(ix-10)\n\tdec (ix-10)\n\tor a\n", out);
    mir_stream_printf(out, "\tjr nz,L%d\n\tdec (ix-9)\nL%d:\n",
            reduced, reduced);

    mir_machine_emit_ix_wide_load(out, -4);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, 1065353216UL);
    mir_emit_runtime_call(out, "__fsf");
    mir_emit_final_call_cleanup(out, 2);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, -4);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, 1065353216UL);
    mir_emit_runtime_call(out, "__faf");
    mir_emit_final_call_cleanup(out, 2);
    mir_emit_runtime_call(out, "__fdf");
    mir_emit_final_call_cleanup(out, 2);

    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_emit_runtime_call(out, "__fmf");
    mir_machine_emit_ix_wide_store(out, -4);
    mir_stream_puts("\tpop hl\n\tpop de\n", out);
    mir_machine_emit_ix_wide_store(out, -8);

    for (index = 0; index < 5; ++index)
        mir_emit_float_log_iteration(out, divisor_bits[index]);

    mir_machine_emit_ix_wide_load(out, -8);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, 1073741824UL);
    mir_emit_runtime_call(out, "__fmf");
    mir_emit_final_call_cleanup(out, 2);
    mir_stream_puts("\tpush de\n\tpush hl\n"
          "\tld l,(ix-10)\n\tld h,(ix-9)\n", out);
    mir_emit_runtime_call(out, "__fif");
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, 1060205080UL);
    mir_emit_runtime_call(out, "__fmaf");
    mir_emit_final_call_cleanup(out, 4);

    mir_stream_printf(out,
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            finish);
}

static void mir_emit_float_normalization_schedule(
    MirStream *out, const struct MirFloatNormalizationSchedule *plan)
{
    int nonzero = new_label();
    int nonnegative = new_label();
    int upper_loop = new_label();
    int upper_done = new_label();
    int lower_loop = new_label();
    int lower_done = new_label();
    int finish = new_label();

    /* The two direct backedges replace the source CFG's carry/borrow joins.
     * Reserve their label IDs so later functions keep legacy numbering. */
    (void)new_label();
    (void)new_label();
    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_emit_float_normalization_frame_word(
        out, plan->exponent_frame_offset);
    mir_stream_puts("\txor a\n\tld (hl),a\n\tinc hl\n\tld (hl),a\n", out);

    mir_emit_float_normalization_frame_float(
        out, plan->value_frame_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n\tld hl,0\n\tld de,0\n", out);
    mir_emit_runtime_call(out, "__feqf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out,
            "\tjr z,L%d\n\tld hl,0\n\tld de,0\n\tjp L%d\nL%d:\n",
            nonzero, finish, nonzero);

    mir_emit_float_normalization_frame_float(
        out, plan->value_frame_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n\tld hl,0\n\tld de,0\n", out);
    mir_emit_runtime_call(out, "__fgtf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjr z,L%d\n", nonnegative);
    mir_emit_float_normalization_frame_word(
        out, plan->exponent_frame_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_emit_float_normalization_frame_float(
        out, plan->value_frame_offset);
    mir_stream_puts("\tld a,d\n\txor 128\n\tld d,a\n"
          "\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tld a,d\n\txor 128\n\tld d,a\n", out);
    mir_stream_printf(out, "\tjp L%d\n", finish);

    mir_stream_printf(out, "L%d:\nL%d:\n", nonnegative, upper_loop);
    mir_emit_float_normalization_frame_float(
        out, plan->value_frame_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n\tld hl,0\n\tld de,16256\n", out);
    mir_emit_runtime_call(out, "__flef");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjr z,L%d\n", upper_done);
    mir_emit_float_normalization_frame_float(
        out, plan->value_frame_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n\tld hl,0\n\tld de,16128\n", out);
    mir_emit_runtime_call(out, "__fmf");
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_emit_float_normalization_store_frame_float(
        out, plan->value_frame_offset);
    mir_emit_float_normalization_frame_word(
        out, plan->exponent_frame_offset);
    mir_stream_puts("\tinc (hl)\n", out);
    mir_stream_printf(out,
            "\tjr nz,L%d\n\tinc hl\n\tinc (hl)\n\tjr L%d\n"
            "L%d:\nL%d:\n",
            upper_loop, upper_loop,
            upper_done, lower_loop);

    mir_emit_float_normalization_frame_float(
        out, plan->value_frame_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n\tld hl,0\n\tld de,16128\n", out);
    mir_emit_runtime_call(out, "__fgtf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjr z,L%d\n", lower_done);
    mir_emit_float_normalization_frame_float(
        out, plan->value_frame_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n\tld hl,0\n\tld de,16384\n", out);
    mir_emit_runtime_call(out, "__fmf");
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_emit_float_normalization_store_frame_float(
        out, plan->value_frame_offset);
    mir_emit_float_normalization_frame_word(
        out, plan->exponent_frame_offset);
    mir_stream_puts("\tld a,(hl)\n\tdec (hl)\n\tor a\n", out);
    mir_stream_printf(out,
            "\tjr nz,L%d\n\tinc hl\n\tdec (hl)\n\tjr L%d\nL%d:\n",
            lower_loop, lower_loop, lower_done);
    mir_emit_float_normalization_frame_float(
        out, plan->value_frame_offset);
    mir_stream_printf(out,
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            finish);
}

static int mir_float_tolerance_parameter(
    const struct MirInsn *parameter, int pointer, int *offset_out)
{
    int memory_type;
    int memory_storage;
    int memory_offset;

    if (parameter->opcode != MIR_PARAM ||
        !mir_scalar_memory_location(
            parameter, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM)
        return 0;
    if (pointer) {
        if (type_ptr_depth(memory_type) == 0 ||
            type_size(memory_type) != 2)
            return 0;
    } else if (type_ptr_depth(memory_type) != 0 ||
               !type_is_float(memory_type) ||
               type_size(memory_type) != 4) {
        return 0;
    }
    if (memory_offset < 4 ||
        memory_offset + type_size(memory_type) - 1 > 127)
        return 0;
    *offset_out = memory_offset;
    return 1;
}

static int mir_match_float_subtract_call_schedule(
    struct MirFloatSubtractCallSchedule *plan)
{
    int arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];
    const struct MirInsn *call = &mir.insns[5];

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 8 || mir_cfg_block_count() != 1 ||
        mir.has_vla || mir.local_bytes != 0 ||
        mir.aggregate_temp_bytes != 0 ||
        !type_is_float(mir.return_type) ||
        type_size(mir.return_type) != 4 ||
        mir.insns[0].opcode != MIR_LABEL ||
        mir.insns[1].opcode != MIR_PARAM ||
        mir.insns[2].opcode != MIR_FLOAT_CONST ||
        mir.insns[3].opcode != MIR_NOP ||
        mir.insns[4].opcode != MIR_ARG ||
        call->opcode != MIR_CALL ||
        mir.insns[6].opcode != MIR_BINARY ||
        mir.insns[7].opcode != MIR_RETURN)
        return 0;
    if (!mir_float_tolerance_parameter(
            &mir.insns[1], 0, &plan->parameter_offset) ||
        !mir_float_report_call_arguments(call, 1, arguments) ||
        arguments[0] != mir.insns[1].dst ||
        !type_is_float(call->type) || type_size(call->type) != 4 ||
        mir.insns[6].src1 != mir.insns[2].dst ||
        mir.insns[6].src2 != call->dst ||
        mir.insns[6].immediate != '-' ||
        !type_is_float(mir.insns[6].secondary_offset) ||
        mir.insns[7].src1 != mir.insns[6].dst)
        return mir_machine_reject(
            "float-subtract-call-schedule", "shape");
    plan->function = find_global(call->name);
    if (plan->function == NULL || !plan->function->is_defined ||
        plan->function->is_funcptr ||
        !plan->function->has_proto ||
        plan->function->proto_variadic ||
        plan->function->proto_nargs != 1 ||
        !type_is_float(plan->function->proto_types[0]) ||
        !type_is_float(plan->function->type))
        return mir_machine_reject(
            "float-subtract-call-schedule", "function");
    plan->left_bits =
        (unsigned long)mir.insns[2].immediate & 0xffffffffUL;
    return 1;
}

static int mir_match_float_atan2_schedule(
    struct MirFloatAtan2Schedule *plan)
{
    int expected_opcodes[66] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_FLOAT_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_FLOAT_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_FLOAT_CONST, MIR_RETURN,
        MIR_LABEL, MIR_NOP, MIR_FLOAT_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_FLOAT_CONST, MIR_UNARY, MIR_RETURN,
        MIR_LABEL, MIR_FLOAT_CONST, MIR_RETURN, MIR_NOP, MIR_LABEL,
        MIR_LOAD, MIR_LOAD, MIR_BINARY, MIR_STORE, MIR_LOAD,
        MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_NOP, MIR_ARG, MIR_CALL, MIR_RETURN, MIR_NOP, MIR_JUMP,
        MIR_LABEL, MIR_LOAD, MIR_FLOAT_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_FLOAT_CONST, MIR_BINARY, MIR_RETURN, MIR_NOP, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_ARG, MIR_CALL, MIR_FLOAT_CONST,
        MIR_BINARY, MIR_RETURN, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_LABEL
    };
    struct Sym *atan_function = NULL;
    int call_indices[3] = {37, 49, 58};
    int argument_indices[3] = {36, 48, 57};
    int arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];
    int instruction;
    int call;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 66 || mir_cfg_block_count() != 10 ||
        mir.has_vla || mir.local_bytes != 4 ||
        mir.aggregate_temp_bytes != 0 ||
        !type_is_float(mir.return_type) ||
        type_size(mir.return_type) != 4)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "float-atan2-schedule", "opcodes");
    if (!mir_float_tolerance_parameter(
            &mir.insns[1], 0, &plan->y_offset) ||
        !mir_float_tolerance_parameter(
            &mir.insns[2], 0, &plan->x_offset) ||
        plan->x_offset != plan->y_offset + 4)
        return mir_machine_reject(
            "float-atan2-schedule", "parameters");
    plan->zero_bits =
        (unsigned long)mir.insns[4].immediate & 0xffffffffUL;
    plan->half_pi_bits =
        (unsigned long)mir.insns[11].immediate & 0xffffffffUL;
    plan->pi_bits =
        (unsigned long)mir.insns[50].immediate & 0xffffffffUL;
    if (plan->zero_bits != 0 ||
        (unsigned long)mir.insns[8].immediate != plan->zero_bits ||
        (unsigned long)mir.insns[15].immediate != plan->zero_bits ||
        (unsigned long)mir.insns[18].immediate != plan->half_pi_bits ||
        (unsigned long)mir.insns[22].immediate != plan->zero_bits ||
        (unsigned long)mir.insns[31].immediate != plan->zero_bits ||
        (unsigned long)mir.insns[43].immediate != plan->zero_bits ||
        (unsigned long)mir.insns[59].immediate != plan->pi_bits ||
        mir.insns[5].immediate != TOK_EQ ||
        mir.insns[9].immediate != '>' ||
        mir.insns[16].immediate != '<' ||
        mir.insns[28].immediate != '/' ||
        mir.insns[32].immediate != '>' ||
        mir.insns[44].immediate != TOK_GE ||
        mir.insns[51].immediate != '+' ||
        mir.insns[60].immediate != '-')
        return mir_machine_reject(
            "float-atan2-schedule", "constants");
    if (mir.insns[5].src1 != mir.insns[2].dst ||
        mir.insns[5].src2 != mir.insns[4].dst ||
        mir.insns[9].src1 != mir.insns[1].dst ||
        mir.insns[9].src2 != mir.insns[8].dst ||
        mir.insns[16].src1 != mir.insns[1].dst ||
        mir.insns[16].src2 != mir.insns[15].dst ||
        mir.insns[19].src1 != mir.insns[18].dst ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[28].src1 != mir.insns[26].dst ||
        mir.insns[28].src2 != mir.insns[27].dst ||
        mir.insns[29].src1 != mir.insns[28].dst ||
        mir.insns[32].src1 != mir.insns[30].dst ||
        mir.insns[32].src2 != mir.insns[31].dst ||
        mir.insns[44].src1 != mir.insns[42].dst ||
        mir.insns[44].src2 != mir.insns[43].dst)
        return mir_machine_reject(
            "float-atan2-schedule", "flow");
    for (call = 0; call < 3; ++call) {
        const struct MirInsn *call_insn =
            &mir.insns[call_indices[call]];
        struct Sym *function;

        if (!mir_float_report_call_arguments(
                call_insn, 1, arguments) ||
            arguments[0] != mir.insns[28].dst ||
            mir.insns[argument_indices[call]].src1 !=
                mir.insns[28].dst ||
            !type_is_float(call_insn->type) ||
            type_size(call_insn->type) != 4)
            return mir_machine_reject(
                "float-atan2-schedule", "call");
        function = find_global(call_insn->name);
        if (function == NULL || !function->is_defined ||
            !function->has_proto || function->proto_variadic ||
            function->proto_nargs != 1 ||
            !type_is_float(function->proto_types[0]) ||
            !type_is_float(function->type))
            return mir_machine_reject(
                "float-atan2-schedule", "call-symbol");
        if (atan_function == NULL)
            atan_function = function;
        else if (atan_function != function)
            return mir_machine_reject(
                "float-atan2-schedule", "call-identity");
    }
    if (mir.insns[51].src1 != mir.insns[49].dst ||
        mir.insns[51].src2 != mir.insns[50].dst ||
        mir.insns[52].src1 != mir.insns[51].dst ||
        mir.insns[60].src1 != mir.insns[58].dst ||
        mir.insns[60].src2 != mir.insns[59].dst ||
        mir.insns[61].src1 != mir.insns[60].dst)
        return mir_machine_reject(
            "float-atan2-schedule", "results");
    plan->atan_function = atan_function;
    return 1;
}

static int mir_match_float_power_schedule(
    struct MirFloatPowerSchedule *plan)
{
    int expected_opcodes[91] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_FLOAT_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_FLOAT_CONST, MIR_RETURN,
        MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_FLOAT_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_FLOAT_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_FLOAT_CONST, MIR_RETURN,
        MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_FLOAT_CONST, MIR_RETURN,
        MIR_NOP, MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_LOAD,
        MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD,
        MIR_LOAD, MIR_UNARY, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_LOAD, MIR_UNARY, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_LOAD, MIR_UNARY, MIR_ARG, MIR_CALL, MIR_UNARY, MIR_BINARY,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_UNARY, MIR_RETURN, MIR_NOP,
        MIR_LABEL, MIR_LOAD, MIR_RETURN, MIR_NOP, MIR_JUMP, MIR_LABEL,
        MIR_FLOAT_CONST, MIR_RETURN, MIR_NOP, MIR_LABEL, MIR_NOP,
        MIR_LABEL, MIR_LOAD, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_BINARY,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_RETURN
    };
    int log_calls[2] = {50, 85};
    int exp_calls[2] = {54, 88};
    struct Sym *log_function = NULL;
    struct Sym *exp_function = NULL;
    int arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];
    int instruction;
    int call;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 91 || mir_cfg_block_count() != 11 ||
        mir.has_vla || mir.local_bytes != 8 ||
        mir.aggregate_temp_bytes != 0 ||
        !type_is_float(mir.return_type) ||
        type_size(mir.return_type) != 4)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "float-power-schedule", "opcodes");
    if (!mir_float_tolerance_parameter(
            &mir.insns[1], 0, &plan->base_offset) ||
        !mir_float_tolerance_parameter(
            &mir.insns[2], 0, &plan->exponent_offset) ||
        plan->exponent_offset != plan->base_offset + 4)
        return mir_machine_reject(
            "float-power-schedule", "parameters");
    plan->zero_bits =
        (unsigned long)mir.insns[4].immediate & 0xffffffffUL;
    plan->one_bits =
        (unsigned long)mir.insns[7].immediate & 0xffffffffUL;
    plan->two_long =
        (unsigned long)mir.insns[60].immediate & 0xffffffffUL;
    if (plan->zero_bits != 0 ||
        (unsigned long)mir.insns[12].immediate != plan->zero_bits ||
        (unsigned long)mir.insns[16].immediate != plan->zero_bits ||
        (unsigned long)mir.insns[20].immediate != plan->zero_bits ||
        (unsigned long)mir.insns[25].immediate != plan->zero_bits ||
        (unsigned long)mir.insns[32].immediate != plan->zero_bits ||
        (unsigned long)mir.insns[63].immediate != 0 ||
        (unsigned long)mir.insns[76].immediate != plan->zero_bits ||
        mir.insns[5].immediate != TOK_EQ ||
        mir.insns[13].immediate != TOK_EQ ||
        mir.insns[17].immediate != '>' ||
        mir.insns[33].immediate != '<' ||
        mir.insns[39].immediate != TOK_EQ ||
        mir.insns[61].immediate != '%' ||
        mir.insns[64].immediate != TOK_NE)
        return mir_machine_reject(
            "float-power-schedule", "constants");
    for (call = 0; call < 2; ++call) {
        const struct MirInsn *log_call =
            &mir.insns[log_calls[call]];
        const struct MirInsn *exp_call =
            &mir.insns[exp_calls[call]];
        struct Sym *function;

        if (!mir_float_report_call_arguments(
                log_call, 1, arguments) ||
            !type_is_float(log_call->type) ||
            type_size(log_call->type) != 4)
            return mir_machine_reject(
                "float-power-schedule", "log-call");
        function = find_global(log_call->name);
        if (function == NULL || !function->is_defined ||
            !function->has_proto || function->proto_variadic ||
            function->proto_nargs != 1 ||
            !type_is_float(function->type))
            return mir_machine_reject(
                "float-power-schedule", "log-symbol");
        if (log_function == NULL)
            log_function = function;
        else if (log_function != function)
            return mir_machine_reject(
                "float-power-schedule", "log-identity");
        if (!mir_float_report_call_arguments(
                exp_call, 1, arguments) ||
            !type_is_float(exp_call->type) ||
            type_size(exp_call->type) != 4)
            return mir_machine_reject(
                "float-power-schedule", "exp-call");
        function = find_global(exp_call->name);
        if (function == NULL || !function->is_defined ||
            !function->has_proto || function->proto_variadic ||
            function->proto_nargs != 1 ||
            !type_is_float(function->type))
            return mir_machine_reject(
                "float-power-schedule", "exp-symbol");
        if (exp_function == NULL)
            exp_function = function;
        else if (exp_function != function)
            return mir_machine_reject(
                "float-power-schedule", "exp-identity");
    }
    if (mir.insns[48].src1 != mir.insns[47].dst ||
        mir.insns[52].src1 != mir.insns[51].dst ||
        mir.insns[52].src2 != mir.insns[50].dst ||
        mir.insns[67].src1 != mir.insns[54].dst ||
        !mir_machine_same_location(
            &mir.insns[57], &mir.insns[71]) ||
        mir.insns[84].src1 != mir.insns[83].dst ||
        mir.insns[86].src1 != mir.insns[82].dst ||
        mir.insns[86].src2 != mir.insns[85].dst ||
        mir.insns[90].src1 != mir.insns[88].dst)
        return mir_machine_reject(
            "float-power-schedule", "flow");
    plan->log_function = log_function;
    plan->exp_function = exp_function;
    return 1;
}

static int mir_match_float_asin_schedule(
    struct MirFloatAsinSchedule *plan)
{
    int expected_opcodes[95] = {
        MIR_LABEL, MIR_PARAM, MIR_FLOAT_CONST, MIR_STORE, MIR_NOP,
        MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_FLOAT_CONST,
        MIR_UNARY, MIR_NOP, MIR_STORE, MIR_NOP, MIR_UNARY, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_FLOAT_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_FLOAT_CONST, MIR_RETURN,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_LOAD, MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_FLOAT_CONST, MIR_FLOAT_CONST, MIR_LOAD, MIR_BINARY,
        MIR_BINARY, MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_NOP, MIR_ARG, MIR_CALL, MIR_STORE, MIR_LOAD,
        MIR_FLOAT_CONST, MIR_FLOAT_CONST, MIR_NOP, MIR_BINARY,
        MIR_BINARY, MIR_BINARY, MIR_RETURN, MIR_NOP, MIR_LABEL, MIR_LOAD,
        MIR_LOAD, MIR_BINARY, MIR_STORE, MIR_LOAD, MIR_LOAD, MIR_NOP,
        MIR_BINARY, MIR_FLOAT_CONST, MIR_NOP, MIR_FLOAT_CONST, MIR_NOP,
        MIR_FLOAT_CONST, MIR_NOP, MIR_FLOAT_CONST, MIR_BINARY,
        MIR_BINARY, MIR_BINARY, MIR_BINARY, MIR_BINARY, MIR_BINARY,
        MIR_BINARY, MIR_BINARY, MIR_STORE, MIR_LOAD, MIR_NOP, MIR_BINARY,
        MIR_RETURN
    };
    int sqrt_arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];
    int self_arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 95 || mir_cfg_block_count() != 4 ||
        mir.has_vla || mir.local_bytes != 20 ||
        mir.aggregate_temp_bytes != 0 ||
        !type_is_float(mir.return_type) ||
        type_size(mir.return_type) != 4)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "float-asin-schedule", "opcodes");
    if (!mir_float_tolerance_parameter(
            &mir.insns[1], 0, &plan->parameter_offset))
        return mir_machine_reject(
            "float-asin-schedule", "parameter");
    plan->one_bits =
        (unsigned long)mir.insns[2].immediate & 0xffffffffUL;
    plan->zero_bits =
        (unsigned long)mir.insns[5].immediate & 0xffffffffUL;
    plan->half_bits =
        (unsigned long)mir.insns[39].immediate & 0xffffffffUL;
    plan->half_pi_bits =
        (unsigned long)mir.insns[58].immediate & 0xffffffffUL;
    plan->two_bits =
        (unsigned long)mir.insns[59].immediate & 0xffffffffUL;
    plan->coefficients[0] =
        (unsigned long)mir.insns[75].immediate & 0xffffffffUL;
    plan->coefficients[1] =
        (unsigned long)mir.insns[77].immediate & 0xffffffffUL;
    plan->coefficients[2] =
        (unsigned long)mir.insns[79].immediate & 0xffffffffUL;
    plan->coefficients[3] =
        (unsigned long)mir.insns[81].immediate & 0xffffffffUL;
    if (plan->zero_bits != 0 ||
        (unsigned long)mir.insns[8].immediate != plan->one_bits ||
        (unsigned long)mir.insns[19].immediate != plan->one_bits ||
        (unsigned long)mir.insns[22].immediate != plan->zero_bits ||
        (unsigned long)mir.insns[43].immediate != plan->half_bits ||
        (unsigned long)mir.insns[44].immediate != plan->one_bits ||
        mir.insns[6].immediate != '<' ||
        mir.insns[20].immediate != '>' ||
        mir.insns[40].immediate != '>' ||
        mir.insns[46].immediate != '-' ||
        mir.insns[47].immediate != '*' ||
        mir.insns[61].immediate != '*' ||
        mir.insns[62].immediate != '-' ||
        mir.insns[63].immediate != '*')
        return mir_machine_reject(
            "float-asin-schedule", "constants");
    if (!mir_float_report_call_arguments(
            &mir.insns[49], 1, sqrt_arguments) ||
        sqrt_arguments[0] != mir.insns[47].dst ||
        !mir_float_report_call_arguments(
            &mir.insns[55], 1, self_arguments) ||
        self_arguments[0] != mir.insns[49].dst)
        return mir_machine_reject(
            "float-asin-schedule", "calls");
    plan->sqrt_function = find_global(mir.insns[49].name);
    plan->self_function = find_global(mir.insns[55].name);
    if (plan->sqrt_function == NULL ||
        plan->sqrt_function->is_funcptr ||
        !plan->sqrt_function->has_proto ||
        plan->sqrt_function->proto_variadic ||
        plan->sqrt_function->proto_nargs != 1 ||
        !type_is_float(plan->sqrt_function->type) ||
        plan->self_function == NULL ||
        plan->self_function != find_global(mir.name) ||
        !plan->self_function->is_defined ||
        !plan->self_function->has_proto ||
        plan->self_function->proto_variadic ||
        plan->self_function->proto_nargs != 1)
        return mir_machine_reject(
            "float-asin-schedule", "call-symbols");
    if (mir.insns[69].src1 != mir.insns[67].dst ||
        mir.insns[69].src2 != mir.insns[68].dst ||
        mir.insns[69].immediate != '*' ||
        mir.insns[74].src1 != mir.insns[72].dst ||
        mir.insns[74].src2 != mir.insns[69].dst ||
        mir.insns[74].immediate != '*' ||
        mir.insns[82].src1 != mir.insns[69].dst ||
        mir.insns[82].src2 != mir.insns[81].dst ||
        mir.insns[82].immediate != '*' ||
        mir.insns[83].src1 != mir.insns[79].dst ||
        mir.insns[83].src2 != mir.insns[82].dst ||
        mir.insns[83].immediate != '+' ||
        mir.insns[84].src1 != mir.insns[69].dst ||
        mir.insns[84].src2 != mir.insns[83].dst ||
        mir.insns[84].immediate != '*' ||
        mir.insns[85].src1 != mir.insns[77].dst ||
        mir.insns[85].src2 != mir.insns[84].dst ||
        mir.insns[85].immediate != '+' ||
        mir.insns[86].src1 != mir.insns[69].dst ||
        mir.insns[86].src2 != mir.insns[85].dst ||
        mir.insns[86].immediate != '*' ||
        mir.insns[87].src1 != mir.insns[75].dst ||
        mir.insns[87].src2 != mir.insns[86].dst ||
        mir.insns[87].immediate != '+' ||
        mir.insns[88].src1 != mir.insns[74].dst ||
        mir.insns[88].src2 != mir.insns[87].dst ||
        mir.insns[88].immediate != '*' ||
        mir.insns[89].src1 != mir.insns[71].dst ||
        mir.insns[89].src2 != mir.insns[88].dst ||
        mir.insns[89].immediate != '+' ||
        mir.insns[93].src1 != mir.insns[91].dst ||
        mir.insns[93].src2 != mir.insns[89].dst ||
        mir.insns[93].immediate != '*' ||
        mir.insns[94].src1 != mir.insns[93].dst)
        return mir_machine_reject(
            "float-asin-schedule", "polynomial");
    return 1;
}

static int mir_float_sweep_constant_bits_depth(
    int value, unsigned long *bits_out, int depth)
{
    const struct MirInsn *definition = mir_definition(value);
    unsigned long bits;

    if (definition == NULL || depth > 4)
        return 0;
    if (definition->opcode == MIR_FLOAT_CONST &&
        type_is_float(definition->type) &&
        type_size(definition->type) == 4) {
        *bits_out =
            (unsigned long)definition->immediate & 0xffffffffUL;
        return 1;
    }
    if (definition->opcode != MIR_UNARY ||
        definition->immediate != '-' ||
        !type_is_float(definition->type) ||
        !mir_float_sweep_constant_bits_depth(
            definition->src1, &bits, depth + 1))
        return 0;
    *bits_out = bits ^ 0x80000000UL;
    return 1;
}

static int mir_float_sweep_store(
    const struct MirInsn *store, char group_names[MIR_FLOAT_SWEEP_GROUPS][64],
    int *group_count, struct MirFloatSweepSchedule *plan)
{
    const struct MirInsn *index;
    const struct MirInsn *base;
    long element_index;
    unsigned long bits;
    int group;

    if (store->opcode != MIR_STORE_INDIRECT ||
        store->memory_size != 4 ||
        (store->memory_flags & (1 | 8)) != 0)
        return 0;
    index = mir_definition(store->src1);
    if (index == NULL || index->opcode != MIR_INDEX_ADDRESS ||
        index->immediate != 4 || index->memory_size != 4 ||
        !mir_machine_evaluate_constant(
            index->src2, &element_index, 0) ||
        element_index < 0 ||
        element_index >= MIR_FLOAT_SWEEP_MAX_VALUES)
        return 0;
    base = mir_definition(index->src1);
    if (base == NULL || base->opcode != MIR_ADDRESS ||
        base->name[0] == 0 ||
        (base->memory_flags & (1 | 8)) != 0 ||
        !mir_float_sweep_constant_bits_depth(
            store->src2, &bits, 0))
        return 0;
    for (group = 0; group < *group_count; ++group)
        if (!strcmp(group_names[group], base->name))
            break;
    if (group == *group_count) {
        if (*group_count >= MIR_FLOAT_SWEEP_GROUPS)
            return 0;
        snprintf(group_names[group], 64, "%s", base->name);
        ++*group_count;
    }
    if (element_index != plan->value_counts[group])
        return 0;
    plan->values[group][element_index] = bits;
    ++plan->value_counts[group];
    return 1;
}

static int mir_float_sweep_function(
    const struct MirInsn *call, int argument_count,
    struct Sym **function_out, int *arguments)
{
    struct Sym *function;

    if (call->opcode != MIR_CALL ||
        (call->memory_flags & MIR_CALL_FLAG_VARIADIC) != 0 ||
        !mir_float_report_call_arguments(
            call, argument_count, arguments))
        return 0;
    function = find_global(call->name);
    if (function == NULL || function->is_funcptr ||
        function->is_noreturn || !function->has_proto ||
        function->proto_variadic ||
        function->proto_nargs != argument_count ||
        !type_is_float(function->type) ||
        type_size(function->type) != 4)
        return 0;
    *function_out = function;
    return 1;
}

static int mir_float_sweep_print(
    struct MirFloatSweepSchedule *plan,
    const struct MirInsn *call, int argument_count,
    int *arguments, int expected_result,
    int *string_id, char name_out[64])
{
    const struct MirInsn *string;
    struct Sym *function;

    if (call->opcode != MIR_CALL ||
        (call->memory_flags & MIR_CALL_FLAG_VARIADIC) == 0 ||
        call->base_name[0] == 0 ||
        strlen(call->base_name) >= 64 ||
        !mir_float_report_call_arguments(
            call, argument_count, arguments) ||
        arguments[0] < 0 ||
        (expected_result >= 0 &&
         arguments[argument_count - 1] != expected_result))
        return 0;
    string = mir_definition(arguments[0]);
    function = find_global(call->name);
    if (string == NULL || string->opcode != MIR_STRING_ADDRESS ||
        string->immediate < 0 ||
        function == NULL || !function->has_proto ||
        !function->proto_variadic)
        return 0;
    if (plan->print_function == NULL)
        plan->print_function = function;
    else if (plan->print_function != function)
        return 0;
    *string_id = (int)string->immediate;
    snprintf(name_out, 64, "%s", call->base_name);
    return 1;
}

static int mir_float_sweep_unary_pair(
    struct MirFloatSweepSchedule *plan,
    int function_index, int print_index,
    struct MirFloatSweepUnary *item)
{
    int function_arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];
    int print_arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];

    if (!mir_float_sweep_function(
            &mir.insns[function_index], 1,
            &item->function, function_arguments) ||
        !mir_float_sweep_print(
            plan, &mir.insns[print_index], 3,
            print_arguments, mir.insns[function_index].dst,
            &item->format_string_id, item->print_name) ||
        print_arguments[1] != function_arguments[0])
        return 0;
    return 1;
}

static int mir_float_sweep_same_value(int left, int right)
{
    const struct MirInsn *left_definition;
    const struct MirInsn *right_definition;

    if (left == right)
        return 1;
    left_definition = mir_definition(left);
    right_definition = mir_definition(right);
    return left_definition != NULL && right_definition != NULL &&
           mir_machine_same_location(
               left_definition, right_definition);
}

static int mir_match_float_sweep_schedule(
    struct MirFloatSweepSchedule *plan)
{
    int extended_first_functions[4] = {233, 242, 251, 260};
    int extended_first_prints[4] = {235, 244, 253, 262};
    int extended_inverse_functions[2] = {298, 307};
    int extended_inverse_prints[2] = {300, 309};
    int extended_last_functions[3] = {400, 409, 418};
    int extended_last_prints[3] = {402, 411, 420};
    int comparison_first_functions[4] = {230, 239, 248, 257};
    int comparison_first_prints[4] = {232, 241, 250, 259};
    int comparison_inverse_functions[2] = {292, 301};
    int comparison_inverse_prints[2] = {294, 303};
    int *first_functions;
    int *first_prints;
    int *inverse_functions;
    int *inverse_prints;
    char group_names[MIR_FLOAT_SWEEP_GROUPS][64];
    int group_count = 0;
    int store_count = 0;
    int call_count = 0;
    int binary_call;
    int binary_print;
    int done_call;
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    memset(group_names, 0, sizeof(group_names));
    if (mir.count == 434 && mir.local_bytes == 180) {
        plan->variant = 1;
        first_functions = extended_first_functions;
        first_prints = extended_first_prints;
        inverse_functions = extended_inverse_functions;
        inverse_prints = extended_inverse_prints;
        binary_call = 361;
        binary_print = 363;
        done_call = 431;
    } else if (mir.count == 431 && mir.local_bytes == 188) {
        plan->variant = 2;
        first_functions = comparison_first_functions;
        first_prints = comparison_first_prints;
        inverse_functions = comparison_inverse_functions;
        inverse_prints = comparison_inverse_prints;
        binary_call = 346;
        binary_print = 348;
        done_call = 428;
    } else {
        return 0;
    }
    if (mir_cfg_block_count() != 13 || mir.has_vla ||
        mir.aggregate_temp_bytes != 0 || !mir_has_cfg_backedge() ||
        type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        if (mir.insns[instruction].opcode == MIR_STORE_INDIRECT) {
            ++store_count;
            if (!mir_float_sweep_store(
                    &mir.insns[instruction], group_names,
                    &group_count, plan))
                return mir_machine_reject(
                    "float-sweep-schedule", "stores");
        }
        if (mir.insns[instruction].opcode == MIR_CALL)
            ++call_count;
        if ((mir.insns[instruction].opcode == MIR_ADDRESS ||
             mir.insns[instruction].opcode == MIR_INDEX_ADDRESS ||
             mir.insns[instruction].opcode == MIR_MEMBER_ADDRESS ||
             mir.insns[instruction].opcode == MIR_LOAD ||
             mir.insns[instruction].opcode == MIR_STORE ||
             mir.insns[instruction].opcode == MIR_LOAD_INDIRECT ||
             mir.insns[instruction].opcode == MIR_STORE_INDIRECT) &&
            (mir.insns[instruction].memory_flags & 1) != 0)
            return mir_machine_reject(
                "float-sweep-schedule", "volatile");
    }
    if (store_count != 38 || group_count != 4 ||
        plan->value_counts[0] != 11 ||
        plan->value_counts[1] != 9 ||
        plan->value_counts[2] != 9 ||
        plan->value_counts[3] != 9 ||
        call_count != (plan->variant == 1 ? 21 : 19))
        return mir_machine_reject(
            "float-sweep-schedule", "population");
    for (item = 0; item < 4; ++item)
        if (!mir_float_sweep_unary_pair(
                plan, first_functions[item], first_prints[item],
                &plan->first[item]))
            return mir_machine_reject(
                "float-sweep-schedule", "first-calls");
    for (item = 0; item < 2; ++item)
        if (!mir_float_sweep_unary_pair(
                plan, inverse_functions[item],
                inverse_prints[item], &plan->inverse[item]))
            return mir_machine_reject(
                "float-sweep-schedule", "inverse-calls");
    {
        int function_arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];
        int print_arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];

        if (!mir_float_sweep_function(
                &mir.insns[binary_call], 2,
                &plan->binary_function, function_arguments) ||
            !mir_float_sweep_print(
                plan, &mir.insns[binary_print], 4,
                print_arguments, mir.insns[binary_call].dst,
                &plan->binary_format_string_id,
                plan->binary_print_name) ||
            !mir_float_sweep_same_value(
                print_arguments[1], function_arguments[0]) ||
            !mir_float_sweep_same_value(
                print_arguments[2], function_arguments[1]))
            return mir_machine_reject(
                "float-sweep-schedule", "binary-call");
    }
    if (plan->variant == 1) {
        for (item = 0; item < 3; ++item)
            if (!mir_float_sweep_unary_pair(
                    plan, extended_last_functions[item],
                    extended_last_prints[item], &plan->last[item]))
                return mir_machine_reject(
                    "float-sweep-schedule", "last-calls");
    } else {
        int first_arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];
        int slow_arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];
        int print_arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];
        int check_arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];
        const struct MirInsn *name;

        if (!mir_float_sweep_function(
                &mir.insns[393], 1, &plan->last[0].function,
                first_arguments) ||
            !mir_float_sweep_function(
                &mir.insns[398], 1, &plan->slow_sine_function,
                slow_arguments) ||
            first_arguments[0] != slow_arguments[0] ||
            !mir_float_sweep_print(
                plan, &mir.insns[408], 4, print_arguments,
                mir.insns[398].dst,
                &plan->compare_format_string_id,
                plan->compare_print_name) ||
            print_arguments[1] != first_arguments[0] ||
            print_arguments[2] != mir.insns[393].dst ||
            !mir_float_report_call_arguments(
                &mir.insns[417], 4, check_arguments) ||
            check_arguments[1] != mir.insns[393].dst ||
            check_arguments[2] != mir.insns[398].dst ||
            check_arguments[3] != first_arguments[0])
            return mir_machine_reject(
                "float-sweep-schedule", "compare-calls");
        name = mir_definition(check_arguments[0]);
        plan->check_function =
            find_global(mir.insns[417].name);
        if (name == NULL || name->opcode != MIR_STRING_ADDRESS ||
            name->immediate < 0 ||
            plan->check_function == NULL ||
            !plan->check_function->is_defined ||
            !plan->check_function->has_proto ||
            plan->check_function->proto_variadic ||
            plan->check_function->proto_nargs != 4)
            return mir_machine_reject(
                "float-sweep-schedule", "check-call");
        plan->compare_name_string_id = (int)name->immediate;
    }
    {
        int arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];

        if (!mir_float_sweep_print(
                plan, &mir.insns[done_call], 1, arguments, -1,
                &plan->done_string_id, plan->done_print_name))
            return mir_machine_reject(
                "float-sweep-schedule", "done-call");
    }
    return 1;
}

static void mir_float_kernel_load(MirStream *out, int offset)
{
    mir_machine_emit_ix_wide_load(out, offset);
}

static void mir_float_kernel_call_one(
    MirStream *out, struct Sym *function)
{
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, function);
    mir_emit_final_call_cleanup(out, 2);
}

static void mir_emit_float_subtract_call_schedule(
    MirStream *out, const struct MirFloatSubtractCallSchedule *plan)
{
    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_float_kernel_load(out, plan->parameter_offset);
    mir_float_kernel_call_one(out, plan->function);
    mir_stream_puts("\texx\n", out);
    mir_machine_emit_float_bits(out, plan->left_bits);
    mir_stream_puts("\tpush de\n\tpush hl\n\texx\n", out);
    mir_emit_runtime_call(out, "__fsf");
    mir_emit_final_call_cleanup(out, 2);
    mir_stream_puts("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static void mir_float_kernel_compare(
    MirStream *out, int offset, unsigned long bits, const char *helper)
{
    mir_float_kernel_load(out, offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, bits);
    mir_emit_runtime_call(out, helper);
    mir_emit_final_call_cleanup(out, 2);
}

static void mir_emit_float_atan2_schedule(
    MirStream *out, const struct MirFloatAtan2Schedule *plan)
{
    enum { RATIO = -4 };
    int x_nonzero = new_label();
    int y_not_positive = new_label();
    int y_not_negative = new_label();
    int x_not_positive = new_label();
    int y_negative = new_label();
    int finish = new_label();

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-4\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_float_kernel_compare(
        out, plan->x_offset, plan->zero_bits, "__feqf");
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", x_nonzero);
    mir_float_kernel_compare(
        out, plan->y_offset, plan->zero_bits, "__fltf");
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", y_not_positive);
    mir_machine_emit_float_bits(out, plan->half_pi_bits);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", finish, y_not_positive);
    mir_float_kernel_compare(
        out, plan->y_offset, plan->zero_bits, "__fgtf");
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", y_not_negative);
    mir_machine_emit_float_bits(
        out, plan->half_pi_bits ^ 0x80000000UL);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n",
            finish, y_not_negative);
    mir_machine_emit_float_bits(out, plan->zero_bits);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", finish, x_nonzero);

    mir_float_kernel_load(out, plan->y_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_float_kernel_load(out, plan->x_offset);
    mir_emit_runtime_call(out, "__fdf");
    mir_emit_final_call_cleanup(out, 2);
    mir_machine_emit_ix_wide_store(out, RATIO);
    mir_float_kernel_compare(
        out, plan->x_offset, plan->zero_bits, "__fltf");
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", x_not_positive);
    mir_float_kernel_load(out, RATIO);
    mir_float_kernel_call_one(out, plan->atan_function);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", finish, x_not_positive);
    mir_float_kernel_compare(
        out, plan->y_offset, plan->zero_bits, "__flef");
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", y_negative);
    mir_float_kernel_load(out, RATIO);
    mir_float_kernel_call_one(out, plan->atan_function);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->pi_bits);
    mir_emit_runtime_call(out, "__faf");
    mir_emit_final_call_cleanup(out, 2);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", finish, y_negative);
    mir_float_kernel_load(out, RATIO);
    mir_float_kernel_call_one(out, plan->atan_function);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->pi_bits);
    mir_emit_runtime_call(out, "__fsf");
    mir_emit_final_call_cleanup(out, 2);
    mir_stream_printf(out,
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            finish);
}

static void mir_emit_float_power_schedule(
    MirStream *out, const struct MirFloatPowerSchedule *plan)
{
    enum {
        INTEGER_EXPONENT = -4,
        RESULT = -8,
        FLOAT_LEFT = -12
    };
    int base_nonzero = new_label();
    int positive_base = new_label();
    int fractional_negative = new_label();
    int even_exponent = new_label();
    int finish = new_label();

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-12\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    mir_float_kernel_compare(
        out, plan->exponent_offset, plan->zero_bits, "__feqf");
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", base_nonzero);
    mir_machine_emit_float_bits(out, plan->one_bits);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", finish, base_nonzero);

    mir_float_kernel_compare(
        out, plan->base_offset, plan->zero_bits, "__feqf");
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", positive_base);
    mir_float_kernel_compare(
        out, plan->exponent_offset, plan->zero_bits, "__fltf");
    mir_machine_emit_float_bits(out, plan->zero_bits);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", finish, positive_base);

    mir_float_kernel_compare(
        out, plan->base_offset, plan->zero_bits, "__fgtf");
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    {
        int general_positive = new_label();
        mir_stream_printf(out, "\tjp z,L%d\n", general_positive);

        mir_float_kernel_load(out, plan->exponent_offset);
        mir_stream_puts("\tpush de\n\tpush hl\n", out);
        mir_float_kernel_load(out, plan->exponent_offset);
        mir_emit_runtime_call(out, "__ffl");
        mir_emit_runtime_call(out, "__flf");
        mir_emit_runtime_call(out, "__feqf");
        mir_emit_final_call_cleanup(out, 2);
        mir_stream_puts("\tld a,h\n\tor l\n", out);
        mir_stream_printf(out, "\tjp z,L%d\n", fractional_negative);

        mir_float_kernel_load(out, plan->exponent_offset);
        mir_emit_runtime_call(out, "__ffl");
        mir_machine_emit_ix_wide_store(out, INTEGER_EXPONENT);
        mir_emit_runtime_call(out, "__flf");
        mir_machine_emit_ix_wide_store(out, FLOAT_LEFT);
        mir_float_kernel_load(out, plan->base_offset);
        mir_stream_puts("\tld a,d\n\txor 128\n\tld d,a\n", out);
        mir_float_kernel_call_one(out, plan->log_function);
        mir_machine_emit_ix_wide_store(out, RESULT);
        mir_float_kernel_load(out, FLOAT_LEFT);
        mir_stream_puts("\tpush de\n\tpush hl\n", out);
        mir_float_kernel_load(out, RESULT);
        mir_emit_runtime_call(out, "__fmf");
        mir_emit_final_call_cleanup(out, 2);
        mir_float_kernel_call_one(out, plan->exp_function);
        mir_machine_emit_ix_wide_store(out, RESULT);
        mir_float_kernel_load(out, INTEGER_EXPONENT);
        mir_stream_puts("\tpush de\n\tpush hl\n", out);
        mir_stream_printf(out, "\tld hl,%lu\n\tld de,0\n",
                plan->two_long & 0xffffUL);
        mir_emit_runtime_call(out, "__lms");
        mir_emit_final_call_cleanup(out, 2);
        mir_stream_puts("\tld a,d\n\tor e\n\tor h\n\tor l\n", out);
        mir_stream_printf(out, "\tjp z,L%d\n", even_exponent);
        mir_float_kernel_load(out, RESULT);
        mir_stream_puts("\tld a,d\n\txor 128\n\tld d,a\n", out);
        mir_stream_printf(out, "\tjp L%d\nL%d:\n",
                finish, even_exponent);
        mir_float_kernel_load(out, RESULT);
        mir_stream_printf(out, "\tjp L%d\nL%d:\n",
                finish, fractional_negative);
        mir_machine_emit_float_bits(out, plan->zero_bits);
        mir_stream_printf(out, "\tjp L%d\nL%d:\n",
                finish, general_positive);

        mir_float_kernel_load(out, plan->exponent_offset);
        mir_machine_emit_ix_wide_store(out, FLOAT_LEFT);
        mir_float_kernel_load(out, plan->base_offset);
        mir_float_kernel_call_one(out, plan->log_function);
        mir_machine_emit_ix_wide_store(out, RESULT);
        mir_float_kernel_load(out, FLOAT_LEFT);
        mir_stream_puts("\tpush de\n\tpush hl\n", out);
        mir_float_kernel_load(out, RESULT);
        mir_emit_runtime_call(out, "__fmf");
        mir_emit_final_call_cleanup(out, 2);
        mir_float_kernel_call_one(out, plan->exp_function);
    }
    mir_stream_printf(out,
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            finish);
}

static void mir_float_kernel_binary_frames(
    MirStream *out, int left_offset, int right_offset, const char *helper)
{
    mir_float_kernel_load(out, left_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_float_kernel_load(out, right_offset);
    mir_emit_runtime_call(out, helper);
    mir_emit_final_call_cleanup(out, 2);
}

static void mir_float_kernel_binary_left_constant(
    MirStream *out, unsigned long left_bits, int right_offset,
    const char *helper)
{
    mir_machine_emit_float_bits(out, left_bits);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_float_kernel_load(out, right_offset);
    mir_emit_runtime_call(out, helper);
    mir_emit_final_call_cleanup(out, 2);
}

static void mir_float_kernel_binary_right_constant(
    MirStream *out, int left_offset, unsigned long right_bits,
    const char *helper)
{
    mir_float_kernel_load(out, left_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, right_bits);
    mir_emit_runtime_call(out, helper);
    mir_emit_final_call_cleanup(out, 2);
}

static void mir_emit_float_asin_schedule(
    MirStream *out, const struct MirFloatAsinSchedule *plan)
{
    enum {
        X = -4,
        SIGN = -8,
        X2 = -12,
        PRODUCT = -16,
        HORNER = -20
    };
    int nonnegative = new_label();
    int in_domain = new_label();
    int polynomial = new_label();
    int finish = new_label();

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-20\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_float_kernel_load(out, plan->parameter_offset);
    mir_machine_emit_ix_wide_store(out, X);
    mir_machine_emit_float_bits(out, plan->one_bits);
    mir_machine_emit_ix_wide_store(out, SIGN);

    mir_float_kernel_compare(out, X, plan->zero_bits, "__fgtf");
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", nonnegative);
    mir_machine_emit_float_bits(
        out, plan->one_bits ^ 0x80000000UL);
    mir_machine_emit_ix_wide_store(out, SIGN);
    mir_float_kernel_load(out, X);
    mir_stream_puts("\tld a,d\n\txor 128\n\tld d,a\n", out);
    mir_machine_emit_ix_wide_store(out, X);
    mir_stream_printf(out, "L%d:\n", nonnegative);

    mir_float_kernel_compare(out, X, plan->one_bits, "__fltf");
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", in_domain);
    mir_machine_emit_float_bits(out, plan->zero_bits);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", finish, in_domain);

    mir_float_kernel_compare(out, X, plan->half_bits, "__fltf");
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", polynomial);
    mir_float_kernel_binary_left_constant(
        out, plan->one_bits, X, "__fsf");
    mir_machine_emit_ix_wide_store(out, HORNER);
    mir_float_kernel_binary_left_constant(
        out, plan->half_bits, HORNER, "__fmf");
    mir_float_kernel_call_one(out, plan->sqrt_function);
    mir_float_kernel_call_one(out, plan->self_function);
    mir_machine_emit_ix_wide_store(out, HORNER);
    mir_float_kernel_binary_left_constant(
        out, plan->two_bits, HORNER, "__fmf");
    mir_machine_emit_ix_wide_store(out, HORNER);
    mir_float_kernel_binary_left_constant(
        out, plan->half_pi_bits, HORNER, "__fsf");
    mir_machine_emit_ix_wide_store(out, HORNER);
    mir_float_kernel_binary_frames(out, SIGN, HORNER, "__fmf");
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", finish, polynomial);

    mir_float_kernel_binary_frames(out, X, X, "__fmf");
    mir_machine_emit_ix_wide_store(out, X2);
    mir_float_kernel_binary_frames(out, X, X2, "__fmf");
    mir_machine_emit_ix_wide_store(out, PRODUCT);

    mir_float_kernel_binary_right_constant(
        out, X2, plan->coefficients[3], "__fmf");
    mir_machine_emit_ix_wide_store(out, HORNER);
    mir_float_kernel_binary_left_constant(
        out, plan->coefficients[2], HORNER, "__faf");
    mir_machine_emit_ix_wide_store(out, HORNER);
    mir_float_kernel_binary_frames(out, X2, HORNER, "__fmf");
    mir_machine_emit_ix_wide_store(out, HORNER);
    mir_float_kernel_binary_left_constant(
        out, plan->coefficients[1], HORNER, "__faf");
    mir_machine_emit_ix_wide_store(out, HORNER);
    mir_float_kernel_binary_frames(out, X2, HORNER, "__fmf");
    mir_machine_emit_ix_wide_store(out, HORNER);
    mir_float_kernel_binary_left_constant(
        out, plan->coefficients[0], HORNER, "__faf");
    mir_machine_emit_ix_wide_store(out, HORNER);
    mir_float_kernel_binary_frames(out, PRODUCT, HORNER, "__fmf");
    mir_machine_emit_ix_wide_store(out, HORNER);
    mir_float_kernel_binary_frames(out, X, HORNER, "__faf");
    mir_machine_emit_ix_wide_store(out, HORNER);
    mir_float_kernel_binary_frames(out, SIGN, HORNER, "__fmf");

    mir_stream_printf(out,
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            finish);
}

static void mir_float_sweep_emit_pool(
    MirStream *out, int label, const struct MirFloatSweepSchedule *plan)
{
    int column = 0;
    int group;
    int value;

    mir_stream_printf(out, "L%d:\n", label);
    for (group = 0; group < MIR_FLOAT_SWEEP_GROUPS; ++group)
        for (value = 0; value < plan->value_counts[group]; ++value) {
            unsigned long bits = plan->values[group][value];
            int byte;

            for (byte = 0; byte < 4; ++byte) {
                if (column == 0)
                    mir_stream_puts("\tdb ", out);
                else
                    mir_stream_putc(',', out);
                mir_stream_printf(out, "%lu", (bits >> (byte * 8)) & 255UL);
                if (++column == 16) {
                    mir_stream_putc('\n', out);
                    column = 0;
                }
            }
        }
    if (column != 0)
        mir_stream_putc('\n', out);
}

static void mir_float_sweep_set_pointer(MirStream *out, int offset)
{
    mir_stream_puts("\tld hl,0\n\tadd hl,sp\n", out);
    if (offset != 0)
        mir_stream_printf(out, "\tld de,%d\n\tadd hl,de\n", offset);
    mir_stream_puts("\tpush hl\n\tpop iy\n", out);
}

static void mir_float_sweep_load_iy(MirStream *out, int offset)
{
    mir_stream_printf(out,
            "\tld l,(iy+%d)\n\tld h,(iy+%d)\n"
            "\tld e,(iy+%d)\n\tld d,(iy+%d)\n",
            offset, offset + 1, offset + 2, offset + 3);
}

static void mir_float_sweep_advance(MirStream *out)
{
    mir_stream_puts("\texx\n\tld de,4\n\tadd iy,de\n\texx\n", out);
}

static void mir_float_sweep_unary_report(
    MirStream *out, const struct MirFloatSweepUnary *item)
{
    mir_float_sweep_load_iy(out, 0);
    mir_float_kernel_call_one(out, item->function);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_float_sweep_load_iy(out, 0);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            item->format_string_id);
    mir_emit_runtime_call(out, item->print_name);
    mir_emit_final_call_cleanup(out, 5);
}

static void mir_float_sweep_unary_group(
    MirStream *out, int pool_offset, int count,
    const struct MirFloatSweepUnary *items, int item_count)
{
    int loop = new_label();
    int item;

    mir_float_sweep_set_pointer(out, pool_offset);
    mir_stream_printf(out, "\tld (ix-1),%d\nL%d:\n", count, loop);
    for (item = 0; item < item_count; ++item)
        mir_float_sweep_unary_report(out, &items[item]);
    mir_float_sweep_advance(out);
    mir_stream_printf(out, "\tdec (ix-1)\n\tjp nz,L%d\n", loop);
}

static void mir_float_sweep_binary_group(
    MirStream *out, const struct MirFloatSweepSchedule *plan)
{
    enum { RESULT = -5 };
    int loop = new_label();

    mir_float_sweep_set_pointer(out, 80);
    mir_stream_printf(out, "\tld (ix-1),9\nL%d:\n", loop);
    mir_float_sweep_load_iy(out, 36);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_float_sweep_load_iy(out, 0);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->binary_function);
    mir_emit_final_call_cleanup(out, 4);
    mir_machine_emit_ix_wide_store(out, RESULT);
    mir_float_kernel_load(out, RESULT);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_float_sweep_load_iy(out, 36);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_float_sweep_load_iy(out, 0);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->binary_format_string_id);
    mir_emit_runtime_call(out, plan->binary_print_name);
    mir_emit_final_call_cleanup(out, 7);
    mir_float_sweep_advance(out);
    mir_stream_printf(out, "\tdec (ix-1)\n\tjp nz,L%d\n", loop);
}

static void mir_float_sweep_compare_group(
    MirStream *out, const struct MirFloatSweepSchedule *plan)
{
    enum {
        RESULT = -5,
        SLOW_RESULT = -9
    };
    int loop = new_label();

    mir_float_sweep_set_pointer(out, 0);
    mir_stream_printf(out, "\tld (ix-1),11\nL%d:\n", loop);
    mir_float_sweep_load_iy(out, 0);
    mir_float_kernel_call_one(out, plan->last[0].function);
    mir_machine_emit_ix_wide_store(out, RESULT);
    mir_float_sweep_load_iy(out, 0);
    mir_float_kernel_call_one(out, plan->slow_sine_function);
    mir_machine_emit_ix_wide_store(out, SLOW_RESULT);

    mir_float_kernel_load(out, SLOW_RESULT);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_float_kernel_load(out, RESULT);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_float_sweep_load_iy(out, 0);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->compare_format_string_id);
    mir_emit_runtime_call(out, plan->compare_print_name);
    mir_emit_final_call_cleanup(out, 7);

    mir_float_sweep_load_iy(out, 0);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_float_kernel_load(out, SLOW_RESULT);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_float_kernel_load(out, RESULT);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->compare_name_string_id);
    mir_machine_emit_symbol_call(out, plan->check_function);
    mir_emit_final_call_cleanup(out, 7);

    mir_float_sweep_advance(out);
    mir_stream_printf(out, "\tdec (ix-1)\n\tjp nz,L%d\n", loop);
}

static void mir_emit_float_sweep_schedule(
    MirStream *out, const struct MirFloatSweepSchedule *plan)
{
    int pool = new_label();

    mir_stream_puts(";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-161\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\tld hl,0\n\tadd hl,sp\n\tex de,hl\n", out);
    mir_stream_printf(out, "\tld hl,L%d\n\tld bc,152\n\tldir\n", pool);

    mir_float_sweep_unary_group(
        out, 0, 11, plan->first, 4);
    mir_float_sweep_unary_group(
        out, 44, 9, plan->inverse, 2);
    mir_float_sweep_binary_group(out, plan);
    if (plan->variant == 1)
        mir_float_sweep_unary_group(
            out, 0, 11, plan->last, 3);
    else
        mir_float_sweep_compare_group(out, plan);

    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->done_string_id);
    mir_emit_runtime_call(out, plan->done_print_name);
    mir_emit_final_call_cleanup(out, 1);
    mir_stream_puts("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tpop iy\n"
          ";@dcc.reg free=iy\n\tret\n", out);
    mir_float_sweep_emit_pool(out, pool, plan);
}

static int mir_match_float_tolerance_schedule(
    struct MirFloatToleranceSchedule *plan)
{
    const int expected_opcodes[44] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_PARAM,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_NOP,
        MIR_NOP, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_UNARY, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_LOAD,
        MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG, MIR_LOAD,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_LABEL
    };
    const int compact_opcodes[42] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_PARAM,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_NOP,
        MIR_NOP, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_UNARY, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_LOAD,
        MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_NOP, MIR_ARG, MIR_NOP,
        MIR_ARG, MIR_CALL, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_NOP, MIR_LABEL
    };
    const struct MirInsn *call;
    int arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];
    int compact;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    compact = mir.count == 42;
    if ((!compact && mir.count != 44) ||
        mir_cfg_block_count() != 3 ||
        mir.has_vla || mir.local_bytes != 4 ||
        mir.aggregate_temp_bytes != 0 ||
        type_size(mir.return_type) != 0)
        return mir_machine_reject(
            "float-tolerance-schedule", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            (compact
                ? compact_opcodes[instruction]
                : expected_opcodes[instruction]))
            return mir_machine_reject(
                "float-tolerance-schedule", "opcodes");
    call = &mir.insns[compact ? 35 : 41];
    if (!mir_float_tolerance_parameter(
            &mir.insns[1], 1, &plan->name_offset) ||
        !mir_float_tolerance_parameter(
            &mir.insns[2], 0, &plan->got_offset) ||
        !mir_float_tolerance_parameter(
            &mir.insns[3], 0, &plan->want_offset) ||
        !mir_float_tolerance_parameter(
            &mir.insns[4], 0, &plan->tolerance_offset) ||
        plan->got_offset != plan->name_offset + 2 ||
        plan->want_offset != plan->got_offset + 4 ||
        plan->tolerance_offset != plan->want_offset + 4)
        return mir_machine_reject(
            "float-tolerance-schedule", "parameters");
    if (!mir_machine_named_nonvolatile(&mir.insns[5]) ||
        !mir_machine_same_location(
            &mir.insns[5], &mir.insns[8]) ||
        !mir_machine_constant_equals(mir.insns[6].dst, 1) ||
        mir.insns[7].src1 != mir.insns[5].dst ||
        mir.insns[7].src2 != mir.insns[6].dst ||
        mir.insns[7].immediate != '+' ||
        mir.insns[8].src1 != mir.insns[7].dst)
        return mir_machine_reject(
            "float-tolerance-schedule", "checks");
    plan->checks = find_global(mir.insns[5].name);
    if (plan->checks == NULL || plan->checks->is_array ||
        plan->checks->is_volatile ||
        type_ptr_depth(plan->checks->type) != 0 ||
        type_size(plan->checks->type) != 2)
        return mir_machine_reject(
            "float-tolerance-schedule", "checks-symbol");
    if (mir.insns[11].src1 != mir.insns[2].dst ||
        mir.insns[11].src2 != mir.insns[3].dst ||
        mir.insns[11].immediate != '-' ||
        !type_is_float(mir.insns[11].secondary_offset) ||
        mir.insns[13].src1 != mir.insns[11].dst ||
        !mir_machine_same_location(
            &mir.insns[13], &mir.insns[23]))
        return mir_machine_reject(
            "float-tolerance-schedule", "difference");
    if (mir.insns[15].opcode != MIR_FLOAT_CONST ||
        mir.insns[15].immediate != 0 ||
        mir.insns[16].src1 != mir.insns[11].dst ||
        mir.insns[16].src2 != mir.insns[15].dst ||
        mir.insns[16].immediate != '<' ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        mir.insns[17].label != mir.insns[22].label)
        return mir_machine_reject(
            "float-tolerance-schedule", "absolute-branch");
    if (mir.insns[19].src1 != mir.insns[11].dst ||
        mir.insns[19].immediate != '-' ||
        mir.insns[21].src1 != mir.insns[19].dst)
        return mir_machine_reject(
            "float-tolerance-schedule", "absolute-value");
    if (mir.insns[13].object < 0 ||
        mir.insns[21].object != mir.insns[13].object ||
        strcmp(mir.insns[13].name, mir.insns[21].name) != 0 ||
        mir.insns[13].memory_size != 4 ||
        mir.insns[21].memory_size != 4)
        return mir_machine_reject(
            "float-tolerance-schedule", "absolute");
    if (
        mir.insns[25].src1 != mir.insns[23].dst ||
        mir.insns[25].src2 != mir.insns[4].dst ||
        mir.insns[25].immediate != '>' ||
        mir.insns[26].src1 != mir.insns[25].dst ||
        mir.insns[26].label !=
            mir.insns[compact ? 41 : 43].label)
        return mir_machine_reject(
            "float-tolerance-schedule", "tolerance");
    if (!mir_machine_named_nonvolatile(
            &mir.insns[compact ? 36 : 27]) ||
        !mir_machine_same_location(
            &mir.insns[compact ? 36 : 27],
            &mir.insns[compact ? 39 : 30]) ||
        !mir_machine_constant_equals(
            mir.insns[compact ? 37 : 28].dst, 1) ||
        mir.insns[compact ? 38 : 29].src1 !=
            mir.insns[compact ? 36 : 27].dst ||
        mir.insns[compact ? 38 : 29].src2 !=
            mir.insns[compact ? 37 : 28].dst ||
        mir.insns[compact ? 38 : 29].immediate != '+' ||
        mir.insns[compact ? 39 : 30].src1 !=
            mir.insns[compact ? 38 : 29].dst)
        return mir_machine_reject(
            "float-tolerance-schedule", "failures");
    plan->failures =
        find_global(mir.insns[compact ? 36 : 27].name);
    if (plan->failures == NULL || plan->failures == plan->checks ||
        plan->failures->is_array || plan->failures->is_volatile ||
        type_ptr_depth(plan->failures->type) != 0 ||
        type_size(plan->failures->type) != 2)
        return mir_machine_reject(
            "float-tolerance-schedule", "failure-symbol");
    if (!mir_float_report_call_arguments(
            call, compact ? 4 : 5, arguments))
        return mir_machine_reject(
            "float-tolerance-schedule", "report-arguments");
    if (
        arguments[0] != mir.insns[compact ? 27 : 31].dst ||
        arguments[1] != mir.insns[compact ? 29 : 33].dst ||
        arguments[2] != mir.insns[2].dst ||
        arguments[3] != mir.insns[3].dst ||
        !mir_machine_same_location(
            &mir.insns[1],
            &mir.insns[compact ? 29 : 33]) ||
        (!compact &&
         (arguments[4] != mir.insns[39].dst ||
          !mir_machine_same_location(
              &mir.insns[13], &mir.insns[39]))))
        return mir_machine_reject(
            "float-tolerance-schedule", "report-values");
    if (
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) !=
         MIR_CALL_FLAG_VARIADIC ||
        call->base_name[0] == 0 ||
        strlen(call->base_name) >= sizeof(plan->print_name))
        return mir_machine_reject(
            "float-tolerance-schedule", "report-call");
    plan->format_string_id =
        (int)mir.insns[compact ? 27 : 31].immediate;
    plan->report_diff = !compact;
    snprintf(plan->print_name, sizeof(plan->print_name), "%s",
             call->base_name);
    return plan->format_string_id >= 0;
}

static void mir_float_tolerance_load(
    MirStream *out, int offset)
{
    mir_stream_printf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tld e,(ix+%d)\n\tld d,(ix+%d)\n",
            offset, offset + 1, offset + 2, offset + 3);
}

static void mir_float_tolerance_load_diff(MirStream *out)
{
    mir_stream_puts("\tld l,(ix-4)\n\tld h,(ix-3)\n"
          "\tld e,(ix-2)\n\tld d,(ix-1)\n", out);
}

static void mir_float_tolerance_store_diff(MirStream *out)
{
    mir_stream_puts("\tld (ix-4),l\n\tld (ix-3),h\n"
          "\tld (ix-2),e\n\tld (ix-1),d\n", out);
}

static void mir_emit_float_tolerance_schedule(
    MirStream *out, const struct MirFloatToleranceSchedule *plan)
{
    int absolute = new_label();
    int done = new_label();

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tdec sp\n\tdec sp\n\tdec sp\n\tdec sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_word(out, plan->checks, 0);
    mir_stream_puts("\tinc hl\n", out);
    mir_machine_emit_global_word_store(out, plan->checks, 0);

    mir_float_tolerance_load(out, plan->got_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_float_tolerance_load(out, plan->want_offset);
    mir_emit_runtime_call(out, "__fsf");
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_float_tolerance_store_diff(out);

    mir_stream_puts("\tpush de\n\tpush hl\n\tld hl,0\n\tld de,0\n", out);
    mir_emit_runtime_call(out, "__fgtf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", absolute);
    mir_float_tolerance_load_diff(out);
    mir_stream_puts("\tld a,d\n\txor 128\n\tld d,a\n", out);
    mir_float_tolerance_store_diff(out);
    mir_stream_printf(out, "L%d:\n", absolute);

    mir_float_tolerance_load_diff(out);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_float_tolerance_load(out, plan->tolerance_offset);
    mir_emit_runtime_call(out, "__fltf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", done);

    if (plan->report_diff) {
        mir_machine_emit_global_word(out, plan->failures, 0);
        mir_stream_puts("\tinc hl\n", out);
        mir_machine_emit_global_word_store(
            out, plan->failures, 0);
        mir_float_tolerance_load_diff(out);
        mir_stream_puts("\tpush de\n\tpush hl\n", out);
    }
    mir_float_tolerance_load(out, plan->want_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_float_tolerance_load(out, plan->got_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->name_offset, plan->name_offset + 1,
            plan->format_string_id);
    mir_emit_runtime_call(out, plan->print_name);
    mir_emit_final_call_cleanup(
        out, plan->report_diff ? 8 : 6);
    if (!plan->report_diff) {
        mir_machine_emit_global_word(out, plan->failures, 0);
        mir_stream_puts("\tinc hl\n", out);
        mir_machine_emit_global_word_store(
            out, plan->failures, 0);
    }
    mir_stream_printf(out,
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            done);
}

static int mir_float_comparison_float_type(int type)
{
    return type_ptr_depth(type) == 0 &&
        type_is_float(type) &&
        type_size(type) == 4;
}

static int mir_float_comparison_signed_word_type(int type)
{
    return type_ptr_depth(type) == 0 &&
        !type_is_float(type) &&
        (type & 15) == TYPE_INT &&
        (type & TYPE_UNSIGNED) == 0 &&
        type_size(type) == 2;
}

static int mir_float_comparison_char_pointer_type(int type)
{
    return type_ptr_depth(type) == 1 &&
        (type & 15) == TYPE_CHAR &&
        type_size(type) == 2;
}

static int mir_float_comparison_unary_call(
    int argument_instruction, int call_instruction,
    int expected_value, struct Sym **function_out)
{
    const struct MirInsn *argument =
        &mir.insns[argument_instruction];
    const struct MirInsn *call = &mir.insns[call_instruction];
    struct Sym *function = find_global(call->name);
    const char *assembly_name;
    int arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];

    if (call->opcode != MIR_CALL ||
        call->src1 >= 0 ||
        call->secondary_offset < 0 ||
        call->memory_flags != 0 ||
        function == NULL ||
        function->storage != SC_FUNC ||
        function->is_funcptr ||
        function->is_noreturn ||
        !function->has_proto ||
        function->proto_variadic ||
        function->proto_nargs != 1 ||
        call->type != function->type ||
        !mir_float_comparison_float_type(function->type) ||
        !mir_float_comparison_float_type(
            function->proto_types[0]) ||
        argument->opcode != MIR_ARG ||
        argument->secondary_offset != call->secondary_offset ||
        argument->immediate != 0 ||
        argument->src1 != expected_value ||
        argument->type != function->proto_types[0] ||
        !mir_float_report_call_arguments(call, 1, arguments) ||
        arguments[0] != expected_value)
        return 0;
    assembly_name = asm_name_for(sym_asm_name(function));
    if (strcmp(assembly_name, "_fabsf") ||
        (call->base_name[0] != 0 &&
         strcmp(call->base_name, assembly_name)))
        return 0;
    *function_out = function;
    return 1;
}

static int mir_float_comparison_print_call(
    int call_instruction, const int argument_instructions[6],
    const int expected_values[6], struct Sym **function_out)
{
    const struct MirInsn *call = &mir.insns[call_instruction];
    struct Sym *function = find_global(call->name);
    const char *assembly_name;
    int arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];
    int argument;

    if (call->opcode != MIR_CALL ||
        call->src1 >= 0 ||
        call->secondary_offset < 0 ||
        call->memory_flags != MIR_CALL_FLAG_VARIADIC ||
        function == NULL ||
        function->storage != SC_FUNC ||
        function->is_funcptr ||
        function->is_noreturn ||
        !function->has_proto ||
        !function->proto_variadic ||
        function->proto_nargs != 1 ||
        call->type != function->type ||
        !mir_float_comparison_signed_word_type(function->type) ||
        !mir_float_comparison_char_pointer_type(
            function->proto_types[0]) ||
        !mir_float_report_call_arguments(call, 6, arguments))
        return 0;
    assembly_name = asm_name_for(sym_asm_name(function));
    if (call->base_name[0] != 0 &&
        strcmp(call->base_name, assembly_name))
        return 0;
    for (argument = 0; argument < 6; ++argument) {
        const struct MirInsn *arg =
            &mir.insns[argument_instructions[argument]];
        int expected_type = argument == 0
            ? function->proto_types[0] : TYPE_BOOL;

        if (arguments[argument] != expected_values[argument] ||
            arg->opcode != MIR_ARG ||
            arg->secondary_offset != call->secondary_offset ||
            arg->immediate != argument ||
            arg->src1 != expected_values[argument] ||
            arg->type != expected_type)
            return 0;
    }
    *function_out = function;
    return 1;
}

static int mir_match_float_comparison_report_schedule(
    struct MirFloatComparisonReportSchedule *plan)
{
    const int expected_opcodes[111] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_NOP, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_NOP, MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_STORE, MIR_NOP, MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_NOP, MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_STORE, MIR_NOP, MIR_FLOAT_CONST, MIR_BINARY, MIR_UNARY,
        MIR_STORE, MIR_NOP, MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL,
        MIR_JUMP, MIR_LABEL, MIR_PHI, MIR_STORE, MIR_NOP, MIR_FLOAT_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
        MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_PHI, MIR_STORE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG,
        MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL
    };
    const int epsilon_constants[5] = {18, 34, 46, 59, 83};
    const int zero_constants[3] = {14, 30, 75};
    const int print_argument_instructions[6] = {
        99, 101, 103, 105, 107, 109
    };
    const int print_values[6] = {
        98, 43, 72, 48, 96, 27
    };
    struct Sym *print_function;
    int arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 111 || mir.next_value != 60 ||
        mir_cfg_block_count() != 21 || mir.local_bytes != 13 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "float-comparison-report", "opcodes");
    if (!mir_float_tolerance_parameter(
            &mir.insns[1], 0, &plan->left_offset) ||
        !mir_float_tolerance_parameter(
            &mir.insns[2], 0, &plan->right_offset) ||
        plan->right_offset != plan->left_offset + 4 ||
        mir.insns[5].src1 != mir.insns[1].dst ||
        mir.insns[5].src2 != mir.insns[2].dst ||
        mir.insns[5].immediate != '-' ||
        !mir_float_comparison_float_type(mir.insns[1].type) ||
        !mir_float_comparison_float_type(mir.insns[2].type) ||
        !mir_float_comparison_float_type(mir.insns[5].type) ||
        !mir_machine_unobservable_local_store(&mir.insns[7]) ||
        mir.insns[7].src1 != mir.insns[5].dst ||
        mir.insns[7].memory_size != 4)
        return mir_machine_reject(
            "float-comparison-report", "parameters");
    if (!mir_float_comparison_unary_call(
            9, 10, mir.insns[5].dst,
            &plan->absolute_function) ||
        !mir_machine_unobservable_local_store(&mir.insns[12]) ||
        mir.insns[12].src1 != mir.insns[10].dst ||
        mir.insns[12].memory_size != 4 ||
        mir.insns[12].object == mir.insns[7].object)
        return mir_machine_reject(
            "float-comparison-report", "absolute");
    for (item = 0; item < 5; ++item) {
        const struct MirInsn *constant =
            &mir.insns[epsilon_constants[item]];

        if (constant->opcode != MIR_FLOAT_CONST ||
            (item != 0 &&
             constant->immediate !=
                 mir.insns[epsilon_constants[0]].immediate))
            return mir_machine_reject(
                "float-comparison-report", "epsilon");
    }
    for (item = 0; item < 3; ++item)
        if (mir.insns[zero_constants[item]].opcode !=
                MIR_FLOAT_CONST ||
            mir.insns[zero_constants[item]].immediate != 0)
            return mir_machine_reject(
                "float-comparison-report", "zero");
    if (mir.insns[15].immediate != '>' ||
        mir.insns[19].immediate != '>' ||
        mir.insns[31].immediate != '<' ||
        mir.insns[35].immediate != '>' ||
        mir.insns[47].immediate != '<' ||
        mir.insns[52].immediate != TOK_LE ||
        mir.insns[60].immediate != '<' ||
        mir.insns[76].immediate != TOK_GE ||
        mir.insns[84].immediate != '<')
        return mir_machine_reject(
            "float-comparison-report", "comparisons");
    if (mir.insns[15].src1 != mir.insns[5].dst ||
        mir.insns[15].src2 != mir.insns[14].dst ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[16].label != mir.insns[24].label ||
        mir.insns[19].src1 != mir.insns[10].dst ||
        mir.insns[19].src2 != mir.insns[18].dst ||
        mir.insns[20].src1 != mir.insns[19].dst ||
        mir.insns[20].label != mir.insns[24].label ||
        mir.insns[27].src1 != mir.insns[22].dst ||
        mir.insns[27].src2 != mir.insns[25].dst ||
        mir.insns[27].phi_pred1 != mir.insns[21].label ||
        mir.insns[27].phi_pred2 != mir.insns[24].label ||
        mir.insns[28].src1 != mir.insns[27].dst ||
        mir.insns[31].src1 != mir.insns[5].dst ||
        mir.insns[31].src2 != mir.insns[30].dst ||
        mir.insns[32].src1 != mir.insns[31].dst ||
        mir.insns[32].label != mir.insns[40].label ||
        mir.insns[35].src1 != mir.insns[10].dst ||
        mir.insns[35].src2 != mir.insns[34].dst ||
        mir.insns[36].src1 != mir.insns[35].dst ||
        mir.insns[36].label != mir.insns[40].label ||
        mir.insns[43].src1 != mir.insns[38].dst ||
        mir.insns[43].src2 != mir.insns[41].dst ||
        mir.insns[43].phi_pred1 != mir.insns[37].label ||
        mir.insns[43].phi_pred2 != mir.insns[40].label ||
        mir.insns[44].src1 != mir.insns[43].dst ||
        mir.insns[47].src1 != mir.insns[10].dst ||
        mir.insns[47].src2 != mir.insns[46].dst ||
        mir.insns[48].src1 != mir.insns[47].dst ||
        (mir.insns[48].type & 15) != TYPE_BOOL ||
        mir.insns[49].src1 != mir.insns[48].dst ||
        mir.insns[52].src1 != mir.insns[5].dst ||
        mir.insns[52].src2 != mir.insns[51].dst ||
        mir.insns[53].src1 != mir.insns[52].dst ||
        mir.insns[53].label != mir.insns[57].label ||
        mir.insns[60].src1 != mir.insns[10].dst ||
        mir.insns[60].src2 != mir.insns[59].dst ||
        mir.insns[61].src1 != mir.insns[60].dst ||
        mir.insns[61].label != mir.insns[65].label ||
        mir.insns[68].src1 != mir.insns[63].dst ||
        mir.insns[68].src2 != mir.insns[66].dst ||
        mir.insns[68].phi_pred1 != mir.insns[62].label ||
        mir.insns[68].phi_pred2 != mir.insns[65].label ||
        mir.insns[72].src1 != mir.insns[55].dst ||
        mir.insns[72].src2 != mir.insns[68].dst ||
        mir.insns[72].phi_pred1 != mir.insns[54].label ||
        mir.insns[72].phi_pred2 != mir.insns[69].label ||
        mir.insns[73].src1 != mir.insns[72].dst ||
        mir.insns[76].src1 != mir.insns[5].dst ||
        mir.insns[76].src2 != mir.insns[75].dst ||
        mir.insns[77].src1 != mir.insns[76].dst ||
        mir.insns[77].label != mir.insns[81].label ||
        mir.insns[84].src1 != mir.insns[10].dst ||
        mir.insns[84].src2 != mir.insns[83].dst ||
        mir.insns[85].src1 != mir.insns[84].dst ||
        mir.insns[85].label != mir.insns[89].label ||
        mir.insns[92].src1 != mir.insns[87].dst ||
        mir.insns[92].src2 != mir.insns[90].dst ||
        mir.insns[92].phi_pred1 != mir.insns[86].label ||
        mir.insns[92].phi_pred2 != mir.insns[89].label ||
        mir.insns[96].src1 != mir.insns[79].dst ||
        mir.insns[96].src2 != mir.insns[92].dst ||
        mir.insns[96].phi_pred1 != mir.insns[78].label ||
        mir.insns[96].phi_pred2 != mir.insns[93].label ||
        mir.insns[97].src1 != mir.insns[96].dst)
        return mir_machine_reject(
            "float-comparison-report", "comparison-graph");
    for (item = 0; item < 6; ++item)
        arguments[item] = mir.insns[print_values[item]].dst;
    if (!mir_float_comparison_print_call(
            110, print_argument_instructions,
            arguments, &print_function))
        return mir_machine_reject(
            "float-comparison-report", "print");
    plan->epsilon_bits =
        (unsigned long)mir.insns[epsilon_constants[0]].immediate;
    plan->format_string_id = (int)mir.insns[98].immediate;
    {
        const char *print_name =
            mir.insns[110].base_name[0] != 0
                ? mir.insns[110].base_name
                : print_function != NULL
                    ? asm_name_for(sym_asm_name(print_function))
                    : "";

        if (print_name[0] == 0 ||
            strlen(print_name) >= sizeof(plan->print_name))
            return mir_machine_reject(
                "float-comparison-report", "print-name");
        dcc_copy_str(
            plan->print_name, sizeof(plan->print_name),
            print_name);
    }
    return 1;
}

static void mir_float_comparison_load_diff(MirStream *out)
{
    mir_stream_puts("\tld l,(ix-4)\n\tld h,(ix-3)\n"
          "\tld e,(ix-2)\n\tld d,(ix-1)\n", out);
}

static void mir_emit_float_comparison_test(
    MirStream *out, const struct MirFloatComparisonReportSchedule *plan,
    const char *helper, unsigned long bits)
{
    (void)plan;
    mir_float_comparison_load_diff(out);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, bits);
    mir_emit_runtime_call(out, helper);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
}

static void mir_emit_float_comparison_report_schedule(
    MirStream *out, const struct MirFloatComparisonReportSchedule *plan)
{
    unsigned long negative_epsilon =
        plan->epsilon_bits ^ 0x80000000UL;
    int not_equal = new_label();
    int equal_done = new_label();

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tdec sp\n\tdec sp\n\tdec sp\n\tdec sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_float_tolerance_load(out, plan->left_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_float_tolerance_load(out, plan->right_offset);
    mir_emit_runtime_call(out, "__fsf");
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_float_tolerance_store_diff(out);

    mir_emit_float_comparison_test(
        out, plan, "__fltf", plan->epsilon_bits);
    mir_stream_puts("\tpush hl\n", out);
    mir_emit_float_comparison_test(
        out, plan, "__fltf", negative_epsilon);
    mir_stream_puts("\tpush hl\n", out);
    mir_emit_float_comparison_test(
        out, plan, "__fgtf", plan->epsilon_bits);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjr z,L%d\n", not_equal);
    mir_emit_float_comparison_test(
        out, plan, "__fltf", negative_epsilon);
    mir_stream_printf(out, "\tjr L%d\nL%d:\n\tld hl,0\nL%d:\n",
            equal_done, not_equal, equal_done);
    mir_stream_puts("\tpush hl\n", out);
    mir_emit_float_comparison_test(
        out, plan, "__fgtf", plan->epsilon_bits);
    mir_stream_puts("\tpush hl\n", out);
    mir_emit_float_comparison_test(
        out, plan, "__fgtf", negative_epsilon);
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->format_string_id);
    mir_emit_runtime_call(out, plan->print_name);
    mir_emit_final_call_cleanup(out, 6);
    mir_stream_puts("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_pi_digit_schedule(
    struct MirPiDigitSchedule *plan)
{
    const int series_calls[4] = {8, 16, 24, 31};
    const int series_constants[4] = {5, 13, 21, 28};
    const int series_values[4] = {1, 4, 5, 6};
    int arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 76 || mir.next_value != 48 ||
        mir_cfg_block_count() != 8 || mir.local_bytes != 14 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        (mir.return_type & 15) != TYPE_INT ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->parameter_offset) ||
        type_ptr_depth(mir.insns[1].type) != 0 ||
        type_size(mir.insns[1].type) != 2 ||
        (mir.insns[1].type & TYPE_UNSIGNED) == 0)
        return 0;
    for (item = 0; item < 4; ++item) {
        const struct MirInsn *call =
            &mir.insns[series_calls[item]];
        struct Sym *function = find_global(call->name);

        if (call->opcode != MIR_CALL || call->src1 >= 0 ||
            function == NULL || function->storage != SC_FUNC ||
            function->is_funcptr ||
            !mir_float_report_call_arguments(call, 2, arguments) ||
            arguments[0] != mir.insns[1].dst ||
            arguments[1] !=
                mir.insns[series_constants[item]].dst ||
            !mir_machine_constant_equals(
                arguments[1], series_values[item]))
            return mir_machine_reject(
                "pi-digit", "series-call");
        if (item == 0)
            plan->series_function = function;
        else if (function != plan->series_function)
            return mir_machine_reject(
                "pi-digit", "series-alias");
    }
    plan->fraction_function =
        find_global(mir.insns[36].name);
    plan->assert_function =
        find_global(mir.insns[69].name);
    if (plan->fraction_function == NULL ||
        plan->assert_function == NULL ||
        mir.insns[36].opcode != MIR_CALL ||
        mir.insns[69].opcode != MIR_CALL ||
        !mir_float_report_call_arguments(
            &mir.insns[36], 1, arguments) ||
        arguments[0] != mir.insns[32].dst ||
        !mir_float_report_call_arguments(
            &mir.insns[69], 1, arguments) ||
        arguments[0] != mir.insns[67].dst ||
        mir.insns[67].opcode != MIR_STRING_ADDRESS)
        return mir_machine_reject(
            "pi-digit", "support-calls");
    if (mir.insns[2].opcode != MIR_FLOAT_CONST ||
        (unsigned long)mir.insns[2].immediate != 0x40800000UL ||
        mir.insns[10].opcode != MIR_FLOAT_CONST ||
        (unsigned long)mir.insns[10].immediate != 0x40000000UL ||
        mir.insns[38].opcode != MIR_FLOAT_CONST ||
        (unsigned long)mir.insns[38].immediate != 0x41800000UL ||
        mir.insns[9].immediate != '*' ||
        mir.insns[17].immediate != '*' ||
        mir.insns[18].immediate != '-' ||
        mir.insns[25].immediate != '-' ||
        mir.insns[32].immediate != '-' ||
        mir.insns[40].immediate != '*' ||
        mir.insns[43].opcode != MIR_UNARY ||
        mir.insns[43].immediate != 0 ||
        mir.insns[47].immediate != TOK_GE ||
        mir.insns[51].immediate != TOK_LE ||
        !mir_machine_constant_equals(mir.insns[46].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[50].dst, 15) ||
        mir.insns[75].src1 != mir.insns[43].dst)
        return mir_machine_reject(
            "pi-digit", "operations");
    plan->assert_string = (int)mir.insns[67].immediate;
    return plan->assert_string >= 0;
}

static void mir_pi_digit_load_float(MirStream *out, int offset)
{
    mir_stream_printf(out,
            "\tld l,(ix%d)\n\tld h,(ix%d)\n"
            "\tld e,(ix%d)\n\tld d,(ix%d)\n",
            offset, offset + 1, offset + 2, offset + 3);
}

static void mir_pi_digit_store_float(MirStream *out, int offset)
{
    mir_stream_printf(out,
            "\tld (ix%d),l\n\tld (ix%d),h\n"
            "\tld (ix%d),e\n\tld (ix%d),d\n",
            offset, offset + 1, offset + 2, offset + 3);
}

static void mir_pi_digit_call_series(
    MirStream *out, const struct MirPiDigitSchedule *plan,
    int selector)
{
    int parameter = plan->parameter_offset + 2;

    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n", selector);
    mir_stream_printf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n",
            parameter, parameter + 1);
    mir_machine_emit_symbol_call(out, plan->series_function);
    mir_emit_final_call_cleanup(out, 2);
}

static void mir_pi_digit_multiply_constant(
    MirStream *out, unsigned long bits)
{
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, bits);
    mir_emit_runtime_call(out, "__fmf");
    mir_emit_final_call_cleanup(out, 2);
}

static void mir_emit_pi_digit_schedule(
    MirStream *out, const struct MirPiDigitSchedule *plan)
{
    int invalid = new_label();
    int valid = new_label();

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tdec sp\n\tdec sp\n\tdec sp\n\tdec sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_pi_digit_call_series(out, plan, 1);
    mir_pi_digit_multiply_constant(out, 0x40800000UL);
    mir_pi_digit_store_float(out, -4);

    mir_pi_digit_load_float(out, -4);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_pi_digit_call_series(out, plan, 4);
    mir_pi_digit_multiply_constant(out, 0x40000000UL);
    mir_emit_runtime_call(out, "__fsf");
    mir_emit_final_call_cleanup(out, 2);
    mir_pi_digit_store_float(out, -4);

    mir_pi_digit_load_float(out, -4);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_pi_digit_call_series(out, plan, 5);
    mir_emit_runtime_call(out, "__fsf");
    mir_emit_final_call_cleanup(out, 2);
    mir_pi_digit_store_float(out, -4);

    mir_pi_digit_load_float(out, -4);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_pi_digit_call_series(out, plan, 6);
    mir_emit_runtime_call(out, "__fsf");
    mir_emit_final_call_cleanup(out, 2);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->fraction_function);
    mir_emit_final_call_cleanup(out, 2);
    mir_pi_digit_multiply_constant(out, 0x41800000UL);
    mir_emit_runtime_call(out, "__ffi");
    mir_stream_puts("\tbit 7,h\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n\tld a,h\n\tor a\n"
            "\tjp nz,L%d\n\tld a,l\n\tcp 16\n\tjp c,L%d\n",
            invalid,
            invalid, valid);
    mir_stream_printf(out, "L%d:\n", invalid);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->assert_string);
    mir_machine_emit_symbol_call(out, plan->assert_function);
    mir_emit_final_call_cleanup(out, 1);
    mir_stream_printf(out, "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            valid);
}

static int mir_match_pi_series_schedule(
    struct MirPiSeriesSchedule *plan)
{
    int arguments[MIR_FLOAT_REPORT_MAX_CALL_ARGS];

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 129 || mir.next_value != 88 ||
        mir_cfg_block_count() != 7 || mir.local_bytes != 26 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        !mir_has_cfg_backedge() ||
        !type_is_float(mir.return_type) ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->n_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &plan->j_offset) ||
        (mir.insns[1].type & TYPE_UNSIGNED) == 0 ||
        (mir.insns[2].type & TYPE_UNSIGNED) == 0 ||
        type_size(mir.insns[1].type) != 2 ||
        type_size(mir.insns[2].type) != 2)
        return 0;
    plan->power_function = find_global(mir.insns[49].name);
    plan->normalize_function = find_global(mir.insns[62].name);
    plan->epsilon_function = find_global(mir.insns[103].name);
    if (plan->power_function == NULL ||
        plan->normalize_function == NULL ||
        plan->epsilon_function == NULL ||
        mir.insns[49].opcode != MIR_CALL ||
        mir.insns[62].opcode != MIR_CALL ||
        mir.insns[103].opcode != MIR_CALL ||
        mir.insns[127].opcode != MIR_CALL ||
        find_global(mir.insns[127].name) !=
            plan->normalize_function)
        return mir_machine_reject(
            "pi-series", "functions");
    if (!mir_float_report_call_arguments(
            &mir.insns[49], 2, arguments) ||
        arguments[0] != mir.insns[45].dst ||
        arguments[1] != mir.insns[34].dst ||
        !mir_float_report_call_arguments(
            &mir.insns[62], 1, arguments) ||
        arguments[0] != mir.insns[58].dst ||
        !mir_float_report_call_arguments(
            &mir.insns[103], 1, arguments) ||
        arguments[0] != mir.insns[89].dst ||
        !mir_float_report_call_arguments(
            &mir.insns[127], 1, arguments) ||
        arguments[0] != mir.insns[89].dst)
        return mir_machine_reject(
            "pi-series", "arguments");
    if (mir.insns[40].immediate != TOK_LE ||
        mir.insns[45].immediate != '-' ||
        mir.insns[57].immediate != '/' ||
        mir.insns[58].immediate != '+' ||
        mir.insns[68].immediate != '+' ||
        mir.insns[75].immediate != '+' ||
        mir.insns[81].immediate != '/' ||
        mir.insns[98].immediate != '/' ||
        mir.insns[104].immediate != '>' ||
        mir.insns[108].immediate != '+' ||
        mir.insns[113].immediate != '/' ||
        mir.insns[118].immediate != '+' ||
        !mir_machine_constant_equals(mir.insns[27].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[66].dst, 8) ||
        !mir_machine_constant_equals(mir.insns[74].dst, 1) ||
        (unsigned long)mir.insns[79].immediate != 0x3f800000UL ||
        (unsigned long)mir.insns[80].immediate != 0x41800000UL ||
        (unsigned long)mir.insns[112].immediate != 0x41800000UL ||
        (unsigned long)mir.insns[117].immediate != 0x41000000UL)
        return mir_machine_reject(
            "pi-series", "operations");
    return 1;
}

static void mir_pi_series_load_word(
    MirStream *out, int offset)
{
    mir_stream_printf(out,
            "\tld l,(ix%d)\n\tld h,(ix%d)\n",
            offset, offset + 1);
}

static void mir_pi_series_store_word(
    MirStream *out, int offset)
{
    mir_stream_printf(out,
            "\tld (ix%d),l\n\tld (ix%d),h\n",
            offset, offset + 1);
}

static void mir_emit_pi_series_schedule(
    MirStream *out, const struct MirPiSeriesSchedule *plan)
{
    enum {
        S = -4,
        DENOM = -6,
        K = -8,
        NUM = -12,
        FDENOM = -16
    };
    int first_loop = new_label();
    int first_body = new_label();
    int first_done = new_label();
    int second_loop = new_label();
    int second_done = new_label();
    int n = plan->n_offset + 2;
    int j = plan->j_offset + 2;

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-16\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\txor a\n"
          "\tld (ix-4),a\n\tld (ix-3),a\n"
          "\tld (ix-2),a\n\tld (ix-1),a\n", out);
    mir_stream_printf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n",
            j, j + 1);
    mir_pi_series_store_word(out, DENOM);
    mir_stream_puts("\txor a\n\tld (ix-8),a\n\tld (ix-7),a\n", out);
    mir_stream_printf(out, "L%d:\n", first_loop);
    mir_stream_printf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tld e,(ix-8)\n\tld d,(ix-7)\n"
            "\tor a\n\tsbc hl,de\n\tjp nc,L%d\n\tjp L%d\n"
            "L%d:\n",
            n, n + 1, first_body, first_done, first_body);
    mir_pi_series_load_word(out, DENOM);
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tld e,(ix-8)\n\tld d,(ix-7)\n"
            "\tor a\n\tsbc hl,de\n\tpush hl\n",
            n, n + 1);
    mir_machine_emit_symbol_call(out, plan->power_function);
    mir_emit_final_call_cleanup(out, 2);
    mir_emit_runtime_call(out, "__fuf");
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_pi_series_load_word(out, DENOM);
    mir_emit_runtime_call(out, "__fuf");
    mir_emit_runtime_call(out, "__fdf");
    mir_emit_final_call_cleanup(out, 2);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_pi_digit_load_float(out, S);
    mir_emit_runtime_call(out, "__faf");
    mir_emit_final_call_cleanup(out, 2);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->normalize_function);
    mir_emit_final_call_cleanup(out, 2);
    mir_pi_digit_store_float(out, S);
    mir_pi_series_load_word(out, DENOM);
    mir_stream_puts("\tld de,8\n\tadd hl,de\n", out);
    mir_pi_series_store_word(out, DENOM);
    {
        int incremented = new_label();

        mir_stream_puts("\tinc (ix-8)\n", out);
        mir_stream_printf(out,
                "\tjp nz,L%d\n\tinc (ix-7)\nL%d:\n",
                incremented, incremented);
    }
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", first_loop, first_done);

    mir_machine_emit_float_bits(out, 0x3d800000UL);
    mir_pi_digit_store_float(out, NUM);
    mir_pi_series_load_word(out, DENOM);
    mir_emit_runtime_call(out, "__fuf");
    mir_pi_digit_store_float(out, FDENOM);
    mir_stream_printf(out, "L%d:\n", second_loop);
    mir_pi_digit_load_float(out, NUM);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_pi_digit_load_float(out, FDENOM);
    mir_emit_runtime_call(out, "__fdf");
    mir_emit_final_call_cleanup(out, 2);
    mir_stream_puts("\tpush de\n\tpush hl\n\tpush de\n\tpush hl\n", out);
    mir_pi_digit_load_float(out, S);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->epsilon_function);
    mir_emit_final_call_cleanup(out, 2);
    mir_emit_runtime_call(out, "__fltf");
    mir_emit_final_call_cleanup(out, 2);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    {
        int add_fraction = new_label();

        mir_stream_printf(out,
                "\tjp nz,L%d\n\tpop bc\n\tpop bc\n\tjp L%d\n"
                "L%d:\n",
                add_fraction, second_done, add_fraction);
    }
    mir_pi_digit_load_float(out, S);
    mir_emit_runtime_call(out, "__faf");
    mir_emit_final_call_cleanup(out, 2);
    mir_pi_digit_store_float(out, S);
    mir_pi_digit_load_float(out, NUM);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, 0x41800000UL);
    mir_emit_runtime_call(out, "__fdf");
    mir_emit_final_call_cleanup(out, 2);
    mir_pi_digit_store_float(out, NUM);
    mir_pi_digit_load_float(out, FDENOM);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, 0x41000000UL);
    mir_emit_runtime_call(out, "__faf");
    mir_emit_final_call_cleanup(out, 2);
    mir_pi_digit_store_float(out, FDENOM);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", second_loop, second_done);
    mir_pi_digit_load_float(out, S);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->normalize_function);
    mir_emit_final_call_cleanup(out, 2);
    mir_stream_puts("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

int mir_try_emit_float_reports(MirStream *out)
{
    struct MirFloatLogSeriesSchedule float_log_series_schedule;
    struct MirFloatNormalizationSchedule float_normalization_schedule;
    struct MirFloatAtan2Schedule float_atan2_schedule;
    struct MirFloatAsinSchedule float_asin_schedule;
    struct MirFloatPowerSchedule float_power_schedule;
    struct MirFloatSweepSchedule float_sweep_schedule;
    struct MirFloatSubtractCallSchedule float_subtract_call;
    struct MirFloatToleranceSchedule float_tolerance_schedule;
    struct MirFloatComparisonReportSchedule
        float_comparison_report_schedule;
    struct MirPiDigitSchedule pi_digit_schedule;
    struct MirPiSeriesSchedule pi_series_schedule;
    struct MirRawConversionCheckSchedule raw_conversion_check_schedule;
    struct MirFloatReportSchedule float_report_schedule;

    if (mir_match_float_sweep_schedule(
            &float_sweep_schedule)) {
        mir_emit_float_sweep_schedule(
            out, &float_sweep_schedule);
        return 1;
    }
    if (mir_match_float_subtract_call_schedule(
            &float_subtract_call)) {
        mir_emit_float_subtract_call_schedule(
            out, &float_subtract_call);
        return 1;
    }
    if (mir_match_float_atan2_schedule(
            &float_atan2_schedule)) {
        mir_emit_float_atan2_schedule(
            out, &float_atan2_schedule);
        return 1;
    }
    if (mir_match_float_asin_schedule(
            &float_asin_schedule)) {
        mir_emit_float_asin_schedule(
            out, &float_asin_schedule);
        return 1;
    }
    if (mir_match_float_power_schedule(
            &float_power_schedule)) {
        mir_emit_float_power_schedule(
            out, &float_power_schedule);
        return 1;
    }
    if (mir_match_pi_series_schedule(&pi_series_schedule)) {
        mir_emit_pi_series_schedule(out, &pi_series_schedule);
        return 1;
    }
    if (mir_match_pi_digit_schedule(&pi_digit_schedule)) {
        mir_emit_pi_digit_schedule(out, &pi_digit_schedule);
        return 1;
    }
    if (mir_match_float_comparison_report_schedule(
            &float_comparison_report_schedule)) {
        mir_emit_float_comparison_report_schedule(
            out, &float_comparison_report_schedule);
        return 1;
    }
    if (mir_match_float_tolerance_schedule(
            &float_tolerance_schedule)) {
        mir_emit_float_tolerance_schedule(
            out, &float_tolerance_schedule);
        return 1;
    }
    if (mir_match_float_normalization_schedule(
            &float_normalization_schedule)) {
        mir_emit_float_normalization_schedule(
            out, &float_normalization_schedule);
        return 1;
    }
    if (mir_match_float_log_series_schedule(
            &float_log_series_schedule)) {
        mir_emit_float_log_series_schedule(
            out, &float_log_series_schedule);
        return 1;
    }
    if (mir_match_raw_conversion_check_schedule(
            &raw_conversion_check_schedule)) {
        mir_emit_raw_conversion_check_schedule(
            out, &raw_conversion_check_schedule);
        return 1;
    }
    if (mir_match_float_report_schedule(
            &float_report_schedule)) {
        mir_emit_float_report_schedule(
            out, &float_report_schedule);
        return 1;
    }
    return -1;
}
