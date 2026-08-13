/* dcc_mir_machine_float_reports.c - strict float schedules. */

#include "dcc_mir_machine_internal.h"

#define MIR_FLOAT_REPORT_MAX_CHECKS 80
#define MIR_FLOAT_REPORT_MAX_LOCALS 4
#define MIR_FLOAT_REPORT_MAX_CALL_ARGS 4

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
    FILE *out, struct Sym *symbol, int offset)
{
    const char *name = asm_name_for(sym_asm_name(symbol));

    if ((symbol->storage == SC_EXTERN || symbol->needs_extrn) &&
        mir_extrn_should_emit(symbol))
        fprintf(out, "\textrn %s\n", name);
    fprintf(out, "\tld hl,%s\n", name);
    if (offset != 0)
        fprintf(out, "\tld de,%d\n\tadd hl,de\n", offset);
    fputs("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
          "\tinc hl\n\tld e,(hl)\n\tinc hl\n"
          "\tld d,(hl)\n\tld h,b\n\tld l,c\n", out);
}

static void mir_float_report_emit_stack_load(
    FILE *out, int offset, int width)
{
    if (width == 2) {
        fprintf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld a,(hl)\n\tinc hl\n"
                "\tld h,(hl)\n\tld l,a\n",
                offset);
        return;
    }
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n"
            "\tld b,(hl)\n\tinc hl\n"
            "\tld e,(hl)\n\tinc hl\n"
            "\tld d,(hl)\n\tld h,b\n\tld l,c\n",
            offset);
}

static void mir_float_report_push_value(FILE *out, int width)
{
    if (width == 4)
        fputs("\tpush de\n\tpush hl\n", out);
    else
        fputs("\tpush hl\n", out);
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
    FILE *out, const struct MirFloatReportSchedule *plan,
    int value);

static void mir_emit_float_report_call(
    FILE *out, const struct MirFloatReportSchedule *plan,
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
    FILE *out, const struct MirFloatReportSchedule *plan,
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
            fprintf(out, "\tld hl,%lu\n",
                    (unsigned long)definition->immediate &
                        0xffffUL);
        return;
    case MIR_FLOAT_CONST:
        mir_machine_emit_float_bits(
            out, (unsigned long)definition->immediate);
        return;
    case MIR_STRING_ADDRESS:
        fprintf(out, "\tld hl,S%ld\n",
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
            fprintf(out,
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
        fputs("\tld a,d\n\txor 128\n\tld d,a\n", out);
        return;
    case MIR_BINARY:
        mir_emit_float_report_expression(
            out, plan, definition->src1);
        fputs("\tpush de\n\tpush hl\n", out);
        mir_emit_float_report_expression(
            out, plan, definition->src2);
        mir_emit_runtime_call(
            out, mir_float_report_binary_helper(definition));
        fputs("\tpop bc\n\tpop bc\n", out);
        return;
    case MIR_CALL:
        mir_emit_float_report_call(out, plan, definition);
        return;
    default:
        fatal("unsupported scheduled float-report expression");
    }
}

static void mir_emit_float_report_variant_expression(
    FILE *out, const struct MirFloatReportSchedule *plan,
    int value);

static void mir_emit_float_report_variant_call(
    FILE *out, const struct MirFloatReportSchedule *plan,
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
    FILE *out, const struct MirFloatReportSchedule *plan,
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
            fprintf(out, "\tld hl,%lu\n",
                    (unsigned long)definition->immediate &
                        0xffffUL);
        return;
    case MIR_FLOAT_CONST:
        mir_machine_emit_float_bits(
            out, (unsigned long)definition->immediate);
        return;
    case MIR_STRING_ADDRESS:
        fprintf(out, "\tld hl,S%ld\n",
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
            fprintf(out,
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
        fputs("\tld a,d\n\txor 128\n\tld d,a\n", out);
        return;
    case MIR_BINARY:
        mir_emit_float_report_variant_expression(
            out, plan, definition->src1);
        fputs("\tpush de\n\tpush hl\n", out);
        mir_emit_float_report_variant_expression(
            out, plan, definition->src2);
        mir_emit_runtime_call(
            out, mir_float_report_binary_helper(definition));
        fputs("\tpop bc\n\tpop bc\n", out);
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
    FILE *out, const struct MirFloatReportSchedule *plan)
{
    if (plan->frame_bytes > 0)
        fputs("\tld sp,ix\n\tpop ix\n", out);
    fputs("\tret\n", out);
}

static void mir_emit_float_report_schedule(
    FILE *out, const struct MirFloatReportSchedule *plan)
{
    int failure_string = new_label();
    int result_ready = new_label();
    int return_failure = new_label();
    int snapshot;
    int setup = 0;
    int check;

    if (plan->frame_bytes > 0) {
        fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
        fprintf(out,
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
    fputs("\tpush hl\n", out);
    mir_machine_emit_global_word(
        out, plan->checks_root, plan->checks_offset);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->summary_string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_emit_final_call_cleanup(out, 3);

    mir_machine_emit_global_word(
        out, plan->failures_root, plan->failures_offset);
    fputs("\tld a,h\n\tor l\n", out);
    if (plan->variant == MIR_FLOAT_REPORT_SHARED)
        fprintf(out,
                "\tjp nz,L%d\n\tld hl,S%d\n\tpush hl\n"
                "\tjp L%d\nL%d:\n"
                "\tld hl,S%d\n\tpush hl\nL%d:\n"
                "\tld hl,S%d\n\tpush hl\n",
                failure_string, plan->success_string_id,
                result_ready, failure_string,
                plan->failure_string_id, result_ready,
                plan->result_string_id);
    else
        fprintf(out,
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
    fputs("\tld a,h\n\tor l\n", out);
    if (plan->variant == MIR_FLOAT_REPORT_SHARED) {
        fprintf(out, "\tjp nz,L%d\n\tld hl,0\n",
                return_failure);
        mir_emit_float_report_epilogue(out, plan);
        fprintf(out, "L%d:\n\tld hl,1\n", return_failure);
        mir_emit_float_report_epilogue(out, plan);
    } else {
        fprintf(out, "\tjr z,L%d\n\tld hl,1\n",
                return_failure);
        mir_emit_float_report_epilogue(out, plan);
        fprintf(out, "L%d:\n\tld hl,0\n", return_failure);
        mir_emit_float_report_epilogue(out, plan);
    }
}

static void mir_emit_raw_conversion_check_schedule(
    FILE *out, const struct MirRawConversionCheckSchedule *plan)
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
        fputs("\tpush hl\n", out);
        fprintf(out, "\tld hl,S%d\n\tpush hl\n",
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
            fprintf(out, "\tld hl,%lu\n",
                    item->conversion_input & 0xffffUL);
            mir_emit_runtime_call(out, "__fif");
        } else {
            mir_machine_emit_float_bits(
                out, item->conversion_input);
            mir_emit_runtime_call(out, "__flf");
        }
        fputs("\tpush de\n\tpush hl\n", out);
        mir_emit_final_call_constant(
            out, item->input, item->input_width);
        mir_machine_emit_symbol_call(out, item->value_function);
        mir_emit_final_call_cleanup(
            out, item->input_width / 2);
        fputs("\tpush de\n\tpush hl\n", out);
        fprintf(out, "\tld hl,S%d\n\tpush hl\n",
                item->name_string_id);
        mir_machine_emit_symbol_call(
            out, plan->wide_check_function);
        mir_emit_final_call_cleanup(out, 5);
    }

    mir_machine_emit_global_word(
        out, plan->failures_root, plan->failures_offset);
    fputs("\tpush hl\n", out);
    mir_machine_emit_global_word(
        out, plan->checks_root, plan->checks_offset);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->summary_string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_emit_final_call_cleanup(out, 3);

    mir_machine_emit_global_word(
        out, plan->failures_root, plan->failures_offset);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out,
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
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out,
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
    FILE *out, int offset)
{
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            offset, offset + 1);
}

static void mir_emit_float_normalization_frame_float(
    FILE *out, int offset)
{
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
            offset, offset + 1, offset + 2, offset + 3);
}

static void mir_emit_float_normalization_store_frame_float(
    FILE *out, int offset)
{
    fprintf(out,
            "\tld (ix%+d),l\n\tld (ix%+d),h\n"
            "\tld (ix%+d),e\n\tld (ix%+d),d\n",
            offset, offset + 1, offset + 2, offset + 3);
}

static void mir_emit_float_log_iteration(FILE *out,
                                         unsigned long divisor_bits)
{
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, -4);
    mir_emit_runtime_call(out, "__fmf");
    mir_emit_final_call_cleanup(out, 2);

    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, divisor_bits);
    mir_emit_runtime_call(out, "__fdf");
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, -8);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_float_report_emit_stack_load(out, 4, 4);
    mir_emit_runtime_call(out, "__faf");
    mir_emit_final_call_cleanup(out, 4);
    mir_machine_emit_ix_wide_store(out, -8);
    fputs("\tpop hl\n\tpop de\n", out);
}

static void mir_emit_float_log_series_schedule(
    FILE *out, const struct MirFloatLogSeriesSchedule *plan)
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

    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-10\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    mir_machine_emit_ix_wide_load(out, plan->value_frame_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, 0);
    mir_emit_runtime_call(out, "__fgtf");
    mir_emit_final_call_cleanup(out, 2);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjr z,L%d\n", nonnegative);
    mir_machine_emit_float_bits(out, 0);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, 0);
    mir_emit_runtime_call(out, "__fdf");
    mir_emit_final_call_cleanup(out, 2);
    fprintf(out, "\tjp L%d\nL%d:\n", finish, nonnegative);

    mir_machine_emit_ix_wide_load(out, plan->value_frame_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, 0);
    mir_emit_runtime_call(out, "__feqf");
    mir_emit_final_call_cleanup(out, 2);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjr z,L%d\n", nonzero);
    mir_machine_emit_float_bits(out, 1065353216UL ^ 0x80000000UL);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, 0);
    mir_emit_runtime_call(out, "__fdf");
    mir_emit_final_call_cleanup(out, 2);
    fprintf(out, "\tjp L%d\nL%d:\n", finish, nonzero);

    fputs("\tpush ix\n\tpop hl\n\tld de,-10\n\tadd hl,de\n"
          "\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, plan->value_frame_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->normalization_function);
    mir_emit_final_call_cleanup(out, 3);
    mir_machine_emit_ix_wide_store(out, -4);

    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, 1060439283UL);
    mir_emit_runtime_call(out, "__fgtf");
    mir_emit_final_call_cleanup(out, 2);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjr z,L%d\n", reduced);
    mir_machine_emit_ix_wide_load(out, -4);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, 1073741824UL);
    mir_emit_runtime_call(out, "__fmf");
    mir_emit_final_call_cleanup(out, 2);
    mir_machine_emit_ix_wide_store(out, -4);
    fputs("\tld a,(ix-10)\n\tdec (ix-10)\n\tor a\n", out);
    fprintf(out, "\tjr nz,L%d\n\tdec (ix-9)\nL%d:\n",
            reduced, reduced);

    mir_machine_emit_ix_wide_load(out, -4);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, 1065353216UL);
    mir_emit_runtime_call(out, "__fsf");
    mir_emit_final_call_cleanup(out, 2);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, -4);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, 1065353216UL);
    mir_emit_runtime_call(out, "__faf");
    mir_emit_final_call_cleanup(out, 2);
    mir_emit_runtime_call(out, "__fdf");
    mir_emit_final_call_cleanup(out, 2);

    fputs("\tpush de\n\tpush hl\n", out);
    mir_emit_runtime_call(out, "__fmf");
    mir_machine_emit_ix_wide_store(out, -4);
    fputs("\tpop hl\n\tpop de\n", out);
    mir_machine_emit_ix_wide_store(out, -8);

    for (index = 0; index < 5; ++index)
        mir_emit_float_log_iteration(out, divisor_bits[index]);

    mir_machine_emit_ix_wide_load(out, -8);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, 1073741824UL);
    mir_emit_runtime_call(out, "__fmf");
    mir_emit_final_call_cleanup(out, 2);
    fputs("\tpush de\n\tpush hl\n"
          "\tld l,(ix-10)\n\tld h,(ix-9)\n", out);
    mir_emit_runtime_call(out, "__fif");
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, 1060205080UL);
    mir_emit_runtime_call(out, "__fmaf");
    mir_emit_final_call_cleanup(out, 4);

    fprintf(out,
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            finish);
}

static void mir_emit_float_normalization_schedule(
    FILE *out, const struct MirFloatNormalizationSchedule *plan)
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
    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_emit_float_normalization_frame_word(
        out, plan->exponent_frame_offset);
    fputs("\txor a\n\tld (hl),a\n\tinc hl\n\tld (hl),a\n", out);

    mir_emit_float_normalization_frame_float(
        out, plan->value_frame_offset);
    fputs("\tpush de\n\tpush hl\n\tld hl,0\n\tld de,0\n", out);
    mir_emit_runtime_call(out, "__feqf");
    fputs("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    fprintf(out,
            "\tjr z,L%d\n\tld hl,0\n\tld de,0\n\tjp L%d\nL%d:\n",
            nonzero, finish, nonzero);

    mir_emit_float_normalization_frame_float(
        out, plan->value_frame_offset);
    fputs("\tpush de\n\tpush hl\n\tld hl,0\n\tld de,0\n", out);
    mir_emit_runtime_call(out, "__fgtf");
    fputs("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjr z,L%d\n", nonnegative);
    mir_emit_float_normalization_frame_word(
        out, plan->exponent_frame_offset);
    fputs("\tpush hl\n", out);
    mir_emit_float_normalization_frame_float(
        out, plan->value_frame_offset);
    fputs("\tld a,d\n\txor 128\n\tld d,a\n"
          "\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->function);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tld a,d\n\txor 128\n\tld d,a\n", out);
    fprintf(out, "\tjp L%d\n", finish);

    fprintf(out, "L%d:\nL%d:\n", nonnegative, upper_loop);
    mir_emit_float_normalization_frame_float(
        out, plan->value_frame_offset);
    fputs("\tpush de\n\tpush hl\n\tld hl,0\n\tld de,16256\n", out);
    mir_emit_runtime_call(out, "__flef");
    fputs("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjr z,L%d\n", upper_done);
    mir_emit_float_normalization_frame_float(
        out, plan->value_frame_offset);
    fputs("\tpush de\n\tpush hl\n\tld hl,0\n\tld de,16128\n", out);
    mir_emit_runtime_call(out, "__fmf");
    fputs("\tpop bc\n\tpop bc\n", out);
    mir_emit_float_normalization_store_frame_float(
        out, plan->value_frame_offset);
    mir_emit_float_normalization_frame_word(
        out, plan->exponent_frame_offset);
    fputs("\tinc (hl)\n", out);
    fprintf(out,
            "\tjr nz,L%d\n\tinc hl\n\tinc (hl)\n\tjr L%d\n"
            "L%d:\nL%d:\n",
            upper_loop, upper_loop,
            upper_done, lower_loop);

    mir_emit_float_normalization_frame_float(
        out, plan->value_frame_offset);
    fputs("\tpush de\n\tpush hl\n\tld hl,0\n\tld de,16128\n", out);
    mir_emit_runtime_call(out, "__fgtf");
    fputs("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjr z,L%d\n", lower_done);
    mir_emit_float_normalization_frame_float(
        out, plan->value_frame_offset);
    fputs("\tpush de\n\tpush hl\n\tld hl,0\n\tld de,16384\n", out);
    mir_emit_runtime_call(out, "__fmf");
    fputs("\tpop bc\n\tpop bc\n", out);
    mir_emit_float_normalization_store_frame_float(
        out, plan->value_frame_offset);
    mir_emit_float_normalization_frame_word(
        out, plan->exponent_frame_offset);
    fputs("\tld a,(hl)\n\tdec (hl)\n\tor a\n", out);
    fprintf(out,
            "\tjr nz,L%d\n\tinc hl\n\tdec (hl)\n\tjr L%d\nL%d:\n",
            lower_loop, lower_loop, lower_done);
    mir_emit_float_normalization_frame_float(
        out, plan->value_frame_offset);
    fprintf(out,
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            finish);
}

int mir_try_emit_float_reports(FILE *out)
{
    struct MirFloatLogSeriesSchedule float_log_series_schedule;
    struct MirFloatNormalizationSchedule float_normalization_schedule;
    struct MirRawConversionCheckSchedule raw_conversion_check_schedule;
    struct MirFloatReportSchedule float_report_schedule;

    if (mir_match_float_normalization_schedule(
            &float_normalization_schedule)) {
        mir_emit_float_normalization_schedule(
            out, &float_normalization_schedule);
        if (mir_stream_size(out) <
            mir_stream_size(mir.capture_stream))
            return 1;
        return mir_machine_reject(
            "float-normalization-schedule", "text-cost");
    }
    if (mir_match_float_log_series_schedule(
            &float_log_series_schedule)) {
        mir_emit_float_log_series_schedule(
            out, &float_log_series_schedule);
        if (mir_stream_size(out) <
            mir_stream_size(mir.capture_stream))
            return 1;
        return mir_machine_reject(
            "float-log-series-schedule", "text-cost");
    }
    if (mir_match_raw_conversion_check_schedule(
            &raw_conversion_check_schedule)) {
        mir_emit_raw_conversion_check_schedule(
            out, &raw_conversion_check_schedule);
        if (mir_stream_size(out) <
            mir_stream_size(mir.capture_stream))
            return 1;
        return mir_machine_reject(
            "raw-conversion-check-schedule", "text-cost");
    }
    if (mir_match_float_report_schedule(
            &float_report_schedule)) {
        mir_emit_float_report_schedule(
            out, &float_report_schedule);
        if (mir_stream_size(out) <
            mir_stream_size(mir.capture_stream))
            return 1;
        return mir_machine_reject(
            "float-report-schedule", "text-cost");
    }
    return -1;
}
