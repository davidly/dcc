/**
 * @file dcc_mir_machine_byte_scans.c
 * @brief Emits exact schedules for byte scans, row operations, and fills.
 *
 * @par Role
 * Matches byte and row scans, byte/word fill and copy loops, global-stride
 * and prediction calls, plus hash, record, and file-line kernels. It keeps
 * the original selector order, including the numeric phase pass-through.
 *
 * @par Key entry point
 * mir_try_emit_byte_scan_kernels().
 */

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

struct MirFixedRowWordSum {
    int rows_stack_offset;
    int array_stack_offset;
};

struct MirFixedWideZero {
    int parameter_stack_offset;
    int count;
};

struct MirFixedMemberWideZero {
    int parameter_stack_offset;
    int member_offset;
    int byte_count;
};

struct MirConstantByteFill {
    int parameter_stack_offset;
    int count;
    int value;
};

struct MirAffineByteFill {
    int pointer_stack_offset;
    int base_stack_offset;
    int count;
    int value_from_parameter;
    int initial_value;
    int step;
};

struct MirPalindromeScan {
    int parameter_stack_offset;
};

struct MirDynamicRowScan {
    struct Sym *table;
    int row_stride;
    int element_stride;
    int count;
};

struct MirByteMismatchScan {
    int pointer_stack_offset;
    int base_stack_offset;
    int count;
};

struct MirVariableByteStepSum {
    int first_stack_offset;
    int step_stack_offset;
    int bound;
    int double_step_value;
    int has_double_step;
};

struct MirFixedReverseWordCopy {
    struct Sym *source;
    struct Sym *destination;
    int source_offset;
    int destination_offset;
    int count;
};

struct MirFixedRandomWordFill {
    struct Sym *destination;
    struct Sym *random_function;
    struct Sym *finish_function;
    int destination_offset;
    int count;
    int modulus;
};

struct MirGlobalByteCopyState {
    struct Sym *source;
    struct Sym *destination;
    struct Sym *state[3];
    int source_offset;
    int destination_offset;
    int state_offsets[3];
    int state_widths[3];
    int state_values[3];
    int count;
};

struct MirFixedGlobalStrideCall {
    struct Sym *function;
    struct Sym *fixed;
    struct Sym *first;
    struct Sym *second;
    int fixed_offset;
    int first_offset;
    int second_offset;
    int first_stride;
    int second_stride;
    int count;
};

struct MirFixedPredictionLoop {
    struct Sym *prefix_function;
    struct Sym *maximum_function;
    struct Sym *logits;
    struct Sym *targets;
    struct Sym *hits;
    struct Sym *total;
    int logits_offset;
    int targets_offset;
    int hits_offset;
    int total_offset;
    int count;
    int columns;
    int returns_bool;
};

struct MirRandomWideFill {
    struct Sym *function;
    int pointer_stack_offset;
    int count_stack_offset;
};

struct MirFixedByteBoardCall {
    struct Sym *board;
    struct Sym *function;
    int board_offset;
    int position_stack_offset;
    int count;
    int clear_value;
    int selected_value;
    int arguments[3];
};

struct MirConstantLoopCheck {
    struct Sym *function;
    int string_id;
};

struct MirGlobalByteCountdown {
    struct Sym *value;
    int parameter_stack_offset;
};

struct MirConditionalStringReport {
    struct Sym *function;
    int name_stack_offset;
    int condition_stack_offset;
    int format_string_id;
    int true_string_id;
    int false_string_id;
};

struct MirPointerMemberAny2 {
    int pointer_stack_offset;
    int value_stack_offset;
    int member_offsets[2];
};

struct MirGlobalRecordPop {
    struct Sym *state;
    int base_stack_offset;
    int target_stack_offset;
    int records_offset;
    int index_offset;
    int kind_offset;
    int value_offset;
    int stride;
    int wanted_kind;
};

struct MirLocalByteFillCallReports {
    struct Sym *functions[2];
    struct Sym *report_function;
    int string_ids[2];
    int call_count;
    int count;
    int fill_initial;
    int call_argument;
    int local_offset;
    int patch_offset;
    int patch_value;
    int patch_after_call;
};

struct MirFixedRowFind {
    struct Sym *table;
    int object_stack_offset;
    int output_stack_offset;
    int row_member_offset;
    int target_member_offset;
    int row_stride;
    int count;
};

struct MirRandomUniqueInit {
    struct Sym *producer;
    struct Sym *duplicate_check;
    struct Sym *copy_function;
    int object_stack_offset;
    int array_offset;
    int copy_offset;
    int final_offset;
    int count;
    int final_value;
};

struct MirFloatNanBits {
    int parameter_stack_offset;
};

struct MirSequentialScalarCallReport {
    struct Sym *functions[8];
    struct Sym *print_function;
    int arguments[8];
    int call_count;
    int string_id;
};

struct MirStringCheckReport {
    struct Sym *checks;
    struct Sym *failures;
    struct Sym *compare_function;
    struct Sym *print_function;
    int checks_offset;
    int failures_offset;
    int name_stack_offset;
    int got_stack_offset;
    int want_stack_offset;
    int string_id;
};

struct MirNoArgTestRunner {
    struct Sym *tests[12];
    struct Sym *checks;
    struct Sym *failures;
    struct Sym *print_function;
    int test_count;
    int checks_offset;
    int failures_offset;
    int counts_string_id;
    int result_string_id;
    int pass_string_id;
    int fail_string_id;
};

struct MirFloatModuloNormalize {
    struct Sym *function;
    int parameter_stack_offset;
    unsigned long one_bits;
};

struct MirFixedAllocationRunner {
    struct Sym *allocator;
    struct Sym *state;
    struct Sym *tests[2];
    struct Sym *print_function;
    int state_member_offset;
    int allocation_counts[2];
    int allocation_sizes[2];
    int iterations;
    int string_id;
};

struct MirStringPutcharLoop {
    struct Sym *function;
    int condition_string_id;
    int string_id;
};

struct MirFixedCallReductionReport {
    struct Sym *function;
    struct Sym *print_function;
    int last_index;
    int report_values[2];
    int string_id;
};

struct MirAggregateByteFillReturn {
    int base_stack_offset;
    int tag_stack_offset;
    int count;
    int tag_offset;
};

struct MirGlobalLastRecordKind {
    struct Sym *state;
    int count_offset;
    int records_offset;
    int member_offset;
    int stride;
    int wanted;
};

struct MirLocalByteFillCall {
    struct Sym *function;
    int count;
    int fill_value;
    int patch_offset;
    int patch_value;
};

struct MirFixedMemberInitCalls {
    struct Sym *function;
    int parameter_stack_offset;
    int name_offset;
    int count_offset;
    int array_offset;
    int stride;
    int count;
    int string_id;
};

struct MirVolatileMemberSum {
    struct Sym *base;
    struct Sym *function;
    int member_offset;
    int count;
};

struct MirMixedScalarCallReport {
    struct Sym *setup_function;
    struct Sym *functions[11];
    struct Sym *callback;
    struct Sym *print_function;
    int has_argument[11];
    int arguments[11];
    int string_id;
};

struct MirFileLineLoop {
    struct Sym *open_function;
    struct Sym *error_function;
    struct Sym *read_function;
    struct Sym *write_function;
    struct Sym *close_function;
    int path_string_id;
    int mode_string_id;
    int buffer_size;
    int stream_value;
};

struct MirWideHash33 {
    int parameter_stack_offset;
    int multiplier;
};

struct MirScaledGlobalLoad {
    struct Sym *root;
    int base_stack_offset;
    int scale_stack_offset;
    int index_stack_offset;
};

struct MirScaledGlobalStore {
    struct Sym *root;
    int base_stack_offset;
    int scale_stack_offset;
    int index_stack_offset;
    int value_stack_offset;
};

struct MirFixedGlobalStringCopies {
    struct Sym *root;
    struct Sym *index;
    struct Sym *copy_function;
    struct Sym *print_function;
    int source_string_ids[3];
    int format_string_id;
    int stride;
};

struct MirSignedMulClampAbs {
    int left_stack_offset;
    int right_stack_offset;
    int limit;
};

struct MirCompactRecordAppend {
    struct Sym *records;
    struct Sym *count;
    struct Sym *error_function;
    int parameter_stack_offsets[3];
    int field_offsets[3];
    int stride;
    int capacity;
    int string_id;
};

struct MirByteMismatchReporter {
    struct Sym *print_function;
    struct Sym *exit_function;
    int pointer_stack_offset;
    int value_stack_offset;
    int count_stack_offset;
    int mismatch_string_id;
    int failure_string_id;
};

static const struct MirInsn *mir_machine_call_argument_insn(
    const struct MirInsn *call, int index)
{
    const struct MirInsn *result = NULL;
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *arg = &mir.insns[instruction];

        if (arg->opcode != MIR_ARG ||
            arg->secondary_offset != call->secondary_offset ||
            arg->immediate != index)
            continue;
        if (result != NULL)
            return NULL;
        result = arg;
    }
    return result;
}

static int mir_machine_word_scalar_type(int type)
{
    return type_ptr_depth(type) == 0 &&
        !type_is_float(type) &&
        !type_is_struct_object(type) &&
        type_size(type) == 2;
}

static int mir_machine_char_pointer_type(int type)
{
    return type_ptr_depth(type) == 1 &&
        (type & 15) == TYPE_CHAR &&
        type_size(type) == 2;
}

static int mir_machine_match_local_fill_report_contract(
    const struct MirInsn *call, const struct MirInsn *report,
    int pointer_value, int scalar_value, int string_value,
    struct Sym **function_out, struct Sym **report_function_out)
{
    const struct MirInsn *call_pointer_arg;
    const struct MirInsn *call_scalar_arg;
    const struct MirInsn *report_string_arg;
    const struct MirInsn *report_value_arg;
    const struct MirInsn *pointer_definition;
    const struct MirInsn *scalar_definition;
    const struct MirInsn *string_definition;
    struct Sym *function;
    struct Sym *report_function;
    const char *report_assembly_name;
    int call_arguments[2];
    int report_arguments[2];

    if (call == NULL || report == NULL ||
        call->opcode != MIR_CALL || report->opcode != MIR_CALL ||
        call->src1 >= 0 || report->src1 >= 0 ||
        call->memory_flags != 0 ||
        report->memory_flags != MIR_CALL_FLAG_VARIADIC ||
        !mir_machine_two_call_arguments(call, call_arguments) ||
        !mir_machine_two_call_arguments(report, report_arguments) ||
        call_arguments[0] != pointer_value ||
        call_arguments[1] != scalar_value ||
        report_arguments[0] != string_value ||
        report_arguments[1] != call->dst)
        return 0;
    call_pointer_arg = mir_machine_call_argument_insn(call, 0);
    call_scalar_arg = mir_machine_call_argument_insn(call, 1);
    report_string_arg = mir_machine_call_argument_insn(report, 0);
    report_value_arg = mir_machine_call_argument_insn(report, 1);
    pointer_definition = mir_definition(pointer_value);
    scalar_definition = mir_definition(scalar_value);
    string_definition = mir_definition(string_value);
    if (call_pointer_arg == NULL || call_scalar_arg == NULL ||
        report_string_arg == NULL || report_value_arg == NULL ||
        pointer_definition == NULL || scalar_definition == NULL ||
        string_definition == NULL ||
        string_definition->opcode != MIR_STRING_ADDRESS)
        return 0;
    function = find_global(call->name);
    report_function = find_global(report->name);
    if (function == NULL || function->storage != SC_FUNC ||
        !function->is_defined || function->is_funcptr ||
        function->is_noreturn || !function->has_proto ||
        function->proto_variadic || function->proto_nargs != 2 ||
        function->type != call->type ||
        !mir_machine_word_scalar_type(call->type) ||
        !mir_machine_char_pointer_type(function->proto_types[0]) ||
        !mir_machine_word_scalar_type(function->proto_types[1]) ||
        call_pointer_arg->type != function->proto_types[0] ||
        call_scalar_arg->type != function->proto_types[1] ||
        pointer_definition->type != function->proto_types[0] ||
        scalar_definition->type != function->proto_types[1] ||
        (call->base_name[0] != 0 &&
         strcmp(call->base_name,
                asm_name_for(sym_asm_name(function)))))
        return 0;
    if (report_function == NULL ||
        report_function->storage != SC_FUNC ||
        report_function->is_defined ||
        report_function->is_funcptr ||
        report_function->is_noreturn ||
        !report_function->has_proto ||
        !report_function->proto_variadic ||
        report_function->proto_nargs != 1 ||
        report_function->type != report->type ||
        !mir_machine_word_scalar_type(report->type) ||
        !mir_machine_char_pointer_type(
            report_function->proto_types[0]) ||
        report_string_arg->type !=
            report_function->proto_types[0] ||
        string_definition->type !=
            report_function->proto_types[0] ||
        report_value_arg->type != call->type ||
        report->base_name[0] == 0)
        return 0;
    report_assembly_name =
        asm_name_for(sym_asm_name(report_function));
    if (strcmp(report->base_name, report_assembly_name))
        return 0;
    *function_out = function;
    *report_function_out = report_function;
    return 1;
}

static void mir_emit_stride_global_argument(
    MirStream *out, struct Sym *symbol, int offset, int stride)
{
    mir_stream_puts("\tpush iy\n\tpop hl\n", out);
    mir_emit_mul_hl_const(out, (unsigned long)stride);
    mir_machine_emit_global_address_de(out, symbol, offset);
    mir_stream_puts("\tadd hl,de\n\tpush hl\n", out);
}

static void mir_emit_file_buffer_address(
    MirStream *out, const struct MirFileLineLoop *plan)
{
    mir_stream_puts("\tpush ix\n\tpop hl\n", out);
    mir_machine_emit_hl_offset(out, -(plan->buffer_size + 2), 0);
}

static void mir_emit_member_init_parameter(
    MirStream *out, int stack_offset)
{
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n",
            stack_offset);
}

static void mir_emit_object_parameter_ix(
    MirStream *out, const struct MirRandomUniqueInit *plan)
{
    mir_stream_printf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n",
            plan->object_stack_offset + 2,
            plan->object_stack_offset + 3);
}

static int mir_match_fixed_row_word_sum(struct MirFixedRowWordSum *plan)
{
    static const int expected_opcodes[56] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_CONST, MIR_STORE, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP, MIR_PHI,
        MIR_NOP, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP,
        MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_LOAD, MIR_RETURN
    };
    const struct MirInsn *rows = &mir.insns[1];
    const struct MirInsn *array = &mir.insns[2];
    const struct MirInsn *outer_phi = &mir.insns[12];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 56 || mir_cfg_block_count() != 7 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_INT ||
        (mir.return_type & TYPE_UNSIGNED) != 0 ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    if ((rows->type & 15) != TYPE_INT ||
        (rows->type & TYPE_UNSIGNED) != 0 ||
        type_ptr_depth(rows->type) != 0 ||
        type_size(rows->type) != 2 ||
        (array->type & 15) != TYPE_INT ||
        type_ptr_depth(array->type) != 1 ||
        type_size(array->type) != 2 ||
        !mir_machine_parameter_value_offset(
            rows->dst, &plan->rows_stack_offset) ||
        !mir_machine_parameter_value_offset(
            array->dst, &plan->array_stack_offset) ||
        plan->rows_stack_offset != 2 ||
        plan->array_stack_offset != 4)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[4]) ||
        mir.insns[4].memory_size != 2 ||
        mir.insns[4].src1 != mir.insns[3].dst ||
        !mir_machine_constant_equals(mir.insns[5].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[7]) ||
        mir.insns[7].memory_size != 2 ||
        mir.insns[7].src1 != mir.insns[5].dst ||
        (outer_phi->type & 15) != TYPE_INT ||
        (outer_phi->type & TYPE_UNSIGNED) != 0 ||
        type_ptr_depth(outer_phi->type) != 0 ||
        type_size(outer_phi->type) != 2 ||
        outer_phi->src1 != mir.insns[5].dst ||
        outer_phi->src2 != mir.insns[50].dst ||
        outer_phi->phi_pred1 != mir.insns[0].label ||
        outer_phi->phi_pred2 != mir.insns[47].label)
        return 0;
    if (mir.insns[15].immediate != '<' ||
        mir.insns[15].src1 != outer_phi->dst ||
        mir.insns[15].src2 != rows->dst ||
        type_size(mir.insns[15].secondary_offset) != 2 ||
        (mir.insns[15].secondary_offset & TYPE_UNSIGNED) != 0 ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[16].label != mir.insns[53].label ||
        !mir_machine_constant_equals(mir.insns[17].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[19]) ||
        mir.insns[19].memory_size != 2 ||
        mir.insns[19].src1 != mir.insns[17].dst)
        return 0;
    if (!mir_machine_same_location(&mir.insns[19], &mir.insns[26]) ||
        !mir_machine_named_nonvolatile(&mir.insns[26]) ||
    (mir.insns[26].type & 15) != TYPE_INT ||
    (mir.insns[26].type & TYPE_UNSIGNED) != 0 ||
    type_ptr_depth(mir.insns[26].type) != 0 ||
    type_size(mir.insns[26].type) != 2 ||
        !mir_machine_constant_equals(mir.insns[27].dst, 3) ||
        mir.insns[28].immediate != '<' ||
        mir.insns[28].src1 != mir.insns[26].dst ||
        mir.insns[28].src2 != mir.insns[27].dst ||
        type_size(mir.insns[28].secondary_offset) != 2 ||
        (mir.insns[28].secondary_offset & TYPE_UNSIGNED) != 0 ||
        mir.insns[29].src1 != mir.insns[28].dst ||
        mir.insns[29].label != mir.insns[46].label ||
        !mir_machine_unobservable_local_store(&mir.insns[39]) ||
        !mir_machine_same_location(&mir.insns[4], &mir.insns[30]) ||
        (mir.insns[30].type & 15) != TYPE_INT ||
        (mir.insns[30].type & TYPE_UNSIGNED) != 0 ||
        type_ptr_depth(mir.insns[30].type) != 0 ||
        type_size(mir.insns[30].type) != 2 ||
        mir.insns[33].src1 != array->dst ||
        mir.insns[33].src2 != outer_phi->dst ||
        mir.insns[33].immediate != 6 ||
        mir.insns[33].memory_size != 2 ||
        !mir_machine_same_location(&mir.insns[19], &mir.insns[34]) ||
        mir.insns[35].src1 != mir.insns[33].dst ||
        mir.insns[35].src2 != mir.insns[34].dst ||
        mir.insns[35].immediate != 2 ||
        mir.insns[35].memory_size != 2 ||
        mir.insns[36].src1 != mir.insns[35].dst ||
        mir.insns[36].memory_size != 2 ||
        (mir.insns[36].type & 15) != TYPE_INT ||
        (mir.insns[36].type & TYPE_UNSIGNED) != 0 ||
        type_ptr_depth(mir.insns[36].type) != 0 ||
        (mir.insns[36].memory_flags & (1 | 8)) != 0 ||
        mir.insns[37].immediate != '+' ||
        type_size(mir.insns[37].secondary_offset) != 2 ||
        (mir.insns[37].secondary_offset & TYPE_UNSIGNED) != 0 ||
        mir.insns[37].src1 != mir.insns[30].dst ||
        mir.insns[37].src2 != mir.insns[36].dst ||
        mir.insns[39].src1 != mir.insns[37].dst ||
        mir.insns[39].memory_size != 2 ||
        !mir_machine_same_location(&mir.insns[4], &mir.insns[39]))
        return 0;
    if (!mir_machine_same_location(&mir.insns[19], &mir.insns[41]) ||
        !mir_machine_constant_equals(mir.insns[42].dst, 1) ||
        mir.insns[43].immediate != '+' ||
        type_size(mir.insns[43].secondary_offset) != 2 ||
        (mir.insns[43].secondary_offset & TYPE_UNSIGNED) != 0 ||
        mir.insns[43].src1 != mir.insns[41].dst ||
        mir.insns[43].src2 != mir.insns[42].dst ||
        !mir_machine_same_location(&mir.insns[19], &mir.insns[44]) ||
        mir.insns[44].src1 != mir.insns[43].dst ||
        mir.insns[44].memory_size != 2 ||
        mir.insns[45].label != mir.insns[20].label ||
        !mir_machine_constant_equals(mir.insns[49].dst, 1) ||
        mir.insns[50].immediate != '+' ||
        type_size(mir.insns[50].secondary_offset) != 2 ||
        (mir.insns[50].secondary_offset & TYPE_UNSIGNED) != 0 ||
        mir.insns[50].src1 != outer_phi->dst ||
        mir.insns[50].src2 != mir.insns[49].dst ||
        !mir_machine_same_location(&mir.insns[7], &mir.insns[51]) ||
        mir.insns[51].src1 != mir.insns[50].dst ||
        mir.insns[51].memory_size != 2 ||
        mir.insns[52].label != mir.insns[8].label ||
        !mir_machine_same_location(&mir.insns[4], &mir.insns[54]) ||
        (mir.insns[54].type & 15) != TYPE_INT ||
        (mir.insns[54].type & TYPE_UNSIGNED) != 0 ||
        type_ptr_depth(mir.insns[54].type) != 0 ||
        mir.insns[55].src1 != mir.insns[54].dst)
        return 0;
    return 1;
}

static void mir_emit_fixed_row_word_sum(
    MirStream *out, const struct MirFixedRowWordSum *plan)
{
    int done = new_label();
    int exit = new_label();
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
            "\tex de,hl\n\tadd hl,hl\n"
            "\tld d,h\n\tld e,l\n\tadd hl,hl\n\tadd hl,de\n"
            "\tex de,hl\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tpush bc\n\tpop iy\n\tadd iy,de\n"
            "\tld de,0\n"
            "L%d:\n\tld a,(bc)\n\tld l,a\n\tinc bc\n"
            "\tld a,(bc)\n\tld h,a\n\tinc bc\n"
            "\tadd hl,de\n\tex de,hl\n"
            "\tpush iy\n\tpop hl\n\tor a\n\tsbc hl,bc\n"
            "\tjp nz,L%d\n\tex de,hl\n\tjp L%d\n"
            "L%d:\n\tld hl,0\n"
            "L%d:\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            plan->rows_stack_offset + 2, done, done,
            plan->array_stack_offset + 2, loop, loop,
            exit, done, exit);
}

static int mir_match_fixed_wide_zero(struct MirFixedWideZero *plan)
{
    static const int expected_opcodes[42] = {
        MIR_LABEL, MIR_PARAM, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_PHI, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_RETURN, MIR_LABEL, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_RETURN
    };
    const struct MirInsn *parameter = &mir.insns[1];
    const struct MirInsn *index_phi = &mir.insns[7];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 42 || mir_cfg_block_count() != 5 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_INT ||
        type_ptr_depth(mir.return_type) != 0)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    if (type_ptr_depth(parameter->type) != 1 ||
        (parameter->type & 15) != TYPE_LONG ||
        mir_machine_pointee_is_volatile(parameter) ||
        !mir_machine_parameter_value_offset(
            parameter->dst, &plan->parameter_stack_offset) ||
        plan->parameter_stack_offset != 2 ||
        !mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[4]) ||
        mir.insns[4].memory_size != 2 ||
        mir.insns[4].src1 != mir.insns[2].dst)
        return 0;
    if ((index_phi->type & 15) != TYPE_INT ||
        (index_phi->type & TYPE_UNSIGNED) != 0 ||
        index_phi->src1 != mir.insns[2].dst ||
        index_phi->src2 != mir.insns[36].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[33].label ||
        mir.insns[20].immediate != '<' ||
        mir.insns[20].src1 != index_phi->dst ||
        mir.insns[20].src2 != mir.insns[19].dst ||
        mir.insns[21].src1 != mir.insns[20].dst ||
        mir.insns[21].label != mir.insns[39].label ||
        mir.insns[24].src1 != parameter->dst ||
        mir.insns[24].src2 != index_phi->dst ||
        mir.insns[24].immediate != 4 ||
        mir.insns[24].memory_size != 4 ||
        mir.insns[25].src1 != mir.insns[24].dst ||
        mir.insns[25].memory_size != 4 ||
        (mir.insns[25].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_constant_equals(mir.insns[27].dst, 0) ||
        mir.insns[28].immediate != TOK_NE ||
        mir.insns[28].src1 != mir.insns[25].dst ||
        mir.insns[28].src2 != mir.insns[27].dst ||
        mir.insns[29].src1 != mir.insns[28].dst ||
        mir.insns[29].label != mir.insns[32].label ||
        !mir_machine_constant_equals(mir.insns[30].dst, 0) ||
        mir.insns[31].src1 != mir.insns[30].dst ||
        !mir_machine_constant_equals(mir.insns[35].dst, 1) ||
        mir.insns[36].immediate != '+' ||
        mir.insns[36].src1 != index_phi->dst ||
        mir.insns[36].src2 != mir.insns[35].dst ||
        !mir_machine_same_location(&mir.insns[4], &mir.insns[37]) ||
        mir.insns[37].src1 != mir.insns[36].dst ||
        mir.insns[38].label != mir.insns[5].label ||
        !mir_machine_constant_equals(mir.insns[40].dst, 1) ||
        mir.insns[41].src1 != mir.insns[40].dst)
        return 0;
    if (mir.insns[19].immediate <= 0 ||
        mir.insns[19].immediate > 255)
        return 0;
    plan->count = (int)mir.insns[19].immediate;
    return 1;
}

static void mir_emit_fixed_wide_zero(
    MirStream *out, const struct MirFixedWideZero *plan)
{
    int done = new_label();
    int loop = new_label();
    int nonzero = new_label();

    mir_stream_printf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tpush de\n\tpop iy\n\tld b,%d\n"
            "L%d:\n\tld a,(iy+0)\n\tor (iy+1)\n"
            "\tor (iy+2)\n\tor (iy+3)\n\tjp nz,L%d\n"
            "\tinc iy\n\tinc iy\n\tinc iy\n\tinc iy\n"
            "\tdjnz L%d\n\tld hl,1\n\tjp L%d\n"
            "L%d:\n\tld hl,0\n"
            "L%d:\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            plan->parameter_stack_offset + 2, plan->count,
            loop, nonzero, loop, done, nonzero, done);
}

static int mir_byte_scan_unsigned_long_pointer_type(int type)
{
    return type_ptr_depth(type) == 1 &&
           !type_is_float(type) &&
           (type & 15) == TYPE_LONG &&
           (type & TYPE_UNSIGNED) != 0 &&
           type_size(type) == 2;
}

static int mir_match_fixed_member_wide_zero(
    struct MirFixedMemberWideZero *plan)
{
    static const int expected_opcodes[33] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_PHI, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_CONST, MIR_RETURN, MIR_LABEL, MIR_LABEL,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_RETURN
    };
    const struct MirInsn *parameter = &mir.insns[1];
    const struct MirInsn *index_phi = &mir.insns[7];
    const struct MirInsn *member = &mir.insns[14];
    const struct MirInsn *index = &mir.insns[16];
    const struct MirInsn *load = &mir.insns[17];
    int count;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 33 || mir_cfg_block_count() != 5 ||
        !mir_has_cfg_backedge() || mir.has_vla ||
        mir.aggregate_temp_bytes != 0 ||
        type_ptr_depth(mir.return_type) != 0 ||
        type_size(mir.return_type) != 2 ||
        type_is_float(mir.return_type))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
                expected_opcodes[instruction])
            return mir_machine_reject(
                "fixed-member-wide-zero", "opcodes");
    if (type_ptr_depth(parameter->type) != 1 ||
        type_size(parameter->type) != 2 ||
        !mir_machine_named_nonvolatile(parameter) ||
        mir_machine_pointee_is_volatile(parameter) ||
        !mir_machine_parameter_value_offset(
            parameter->dst, &plan->parameter_stack_offset) ||
        !mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[4]) ||
        mir.insns[4].src1 != mir.insns[3].dst)
        return mir_machine_reject(
            "fixed-member-wide-zero", "parameter");
    if (index_phi->src1 != mir.insns[3].dst ||
        index_phi->src2 != mir.insns[27].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[24].label ||
        !mir_machine_constant_equals(mir.insns[9].dst,
                                     member->memory_size / 4) ||
        mir.insns[10].src1 != index_phi->dst ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[11].src2 != mir.insns[9].dst ||
        mir.insns[11].immediate != '<' ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[12].label != mir.insns[30].label)
        return mir_machine_reject(
            "fixed-member-wide-zero", "loop-bound");
    plan->member_offset = (int)member->immediate;
    plan->byte_count = member->memory_size;
    if (plan->member_offset < 0 || plan->byte_count < 4 ||
        plan->byte_count > 256 || (plan->byte_count & 3) != 0 ||
        plan->member_offset + plan->byte_count - 1 > 32767 ||
        member->src1 != parameter->dst ||
        !mir_byte_scan_unsigned_long_pointer_type(member->type) ||
        (member->memory_flags & (1 | 4 | 8)) != 0 ||
        member->bit_width != 0 ||
        index->src1 != member->dst || index->src2 != index_phi->dst ||
        index->immediate != 4 || index->memory_size != 4 ||
        index->memory_flags != 0 || index->bit_width != 0 ||
        !mir_byte_scan_unsigned_long_pointer_type(index->type) ||
        load->src1 != index->dst || load->memory_size != 4 ||
        load->memory_flags != 0 || load->bit_width != 0 ||
        type_ptr_depth(load->type) != 0 ||
        (load->type & 15) != TYPE_LONG ||
        (load->type & TYPE_UNSIGNED) == 0 || type_size(load->type) != 4)
        return mir_machine_reject(
            "fixed-member-wide-zero", "memory");
    count = plan->byte_count / 4;
    if (!mir_machine_constant_equals(mir.insns[9].dst, count) ||
        !mir_machine_constant_equals(mir.insns[18].dst, 0) ||
        mir.insns[19].src1 != load->dst ||
        mir.insns[19].src2 != mir.insns[18].dst ||
        mir.insns[19].immediate != TOK_NE ||
        mir.insns[20].src1 != mir.insns[19].dst ||
        mir.insns[20].label != mir.insns[23].label ||
        !mir_machine_constant_equals(mir.insns[21].dst, 0) ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        !mir_machine_constant_equals(mir.insns[26].dst, 1) ||
        mir.insns[27].src1 != index_phi->dst ||
        mir.insns[27].src2 != mir.insns[26].dst ||
        mir.insns[27].immediate != '+' ||
        !mir_machine_unobservable_local_store(&mir.insns[28]) ||
        mir.insns[28].src1 != mir.insns[27].dst ||
        mir.insns[29].label != mir.insns[5].label ||
        !mir_machine_constant_equals(mir.insns[31].dst, 1) ||
        mir.insns[32].src1 != mir.insns[31].dst)
        return mir_machine_reject(
            "fixed-member-wide-zero", "flow");
    return 1;
}

static void mir_emit_fixed_member_wide_zero(
    MirStream *out, const struct MirFixedMemberWideZero *plan)
{
    int loop = new_label();
    int nonzero = new_label();

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n",
            plan->parameter_stack_offset);
    mir_machine_emit_hl_offset(out, plan->member_offset, 0);
    mir_stream_printf(out,
            "\tld b,%d\nL%d:\n\tld a,(hl)\n\tor a\n"
            "\tjp nz,L%d\n\tinc hl\n\tdjnz L%d\n"
            "\tld hl,1\n\tret\nL%d:\n\tld hl,0\n\tret\n",
            plan->byte_count & 255, loop, nonzero, loop, nonzero);
}

static int mir_match_fixed_member_wide_zero_fill(
    struct MirFixedMemberWideZero *plan)
{
    static const int expected_opcodes[26] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_PHI, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_LABEL,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL
    };
    const struct MirInsn *parameter = &mir.insns[1];
    const struct MirInsn *index_phi = &mir.insns[7];
    const struct MirInsn *member = &mir.insns[14];
    const struct MirInsn *index = &mir.insns[16];
    int count;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 26 || mir_cfg_block_count() != 4 ||
        !mir_has_cfg_backedge() || mir.has_vla ||
        mir.aggregate_temp_bytes != 0 || type_size(mir.return_type) != 0)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
                expected_opcodes[instruction])
            return mir_machine_reject(
                "fixed-member-wide-zero-fill", "opcodes");
    if (type_ptr_depth(parameter->type) != 1 ||
        type_size(parameter->type) != 2 ||
        !mir_machine_named_nonvolatile(parameter) ||
        mir_machine_pointee_is_volatile(parameter) ||
        !mir_machine_parameter_value_offset(
            parameter->dst, &plan->parameter_stack_offset) ||
        !mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[4]) ||
        mir.insns[4].src1 != mir.insns[3].dst ||
        index_phi->src1 != mir.insns[3].dst ||
        index_phi->src2 != mir.insns[22].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[19].label)
        return mir_machine_reject(
            "fixed-member-wide-zero-fill", "parameter");
    plan->member_offset = (int)member->immediate;
    plan->byte_count = member->memory_size;
    count = plan->byte_count / 4;
    if (plan->member_offset < 0 || plan->byte_count < 4 ||
        plan->byte_count > 256 || (plan->byte_count & 3) != 0 ||
        plan->member_offset + plan->byte_count - 1 > 32767 ||
        member->src1 != parameter->dst ||
        !mir_byte_scan_unsigned_long_pointer_type(member->type) ||
        (member->memory_flags & (1 | 4 | 8)) != 0 ||
        member->bit_width != 0 ||
        index->src1 != member->dst || index->src2 != index_phi->dst ||
        index->immediate != 4 || index->memory_size != 4 ||
        index->memory_flags != 0 || index->bit_width != 0 ||
        !mir_byte_scan_unsigned_long_pointer_type(index->type))
        return mir_machine_reject(
            "fixed-member-wide-zero-fill", "memory");
    if (!mir_machine_constant_equals(mir.insns[9].dst, count) ||
        mir.insns[10].src1 != index_phi->dst ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[11].src2 != mir.insns[9].dst ||
        mir.insns[11].immediate != '<' ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[12].label != mir.insns[25].label ||
        !mir_machine_constant_equals(mir.insns[17].dst, 0) ||
        mir.insns[18].src1 != index->dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        mir.insns[18].memory_size != 4 ||
        mir.insns[18].memory_flags != 0 || mir.insns[18].bit_width != 0 ||
        !mir_machine_constant_equals(mir.insns[21].dst, 1) ||
        mir.insns[22].src1 != index_phi->dst ||
        mir.insns[22].src2 != mir.insns[21].dst ||
        mir.insns[22].immediate != '+' ||
        !mir_machine_unobservable_local_store(&mir.insns[23]) ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[24].label != mir.insns[5].label)
        return mir_machine_reject(
            "fixed-member-wide-zero-fill", "flow");
    return 1;
}

static void mir_emit_fixed_member_wide_zero_fill(
    MirStream *out, const struct MirFixedMemberWideZero *plan)
{
    int loop = new_label();

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n",
            plan->parameter_stack_offset);
    mir_machine_emit_hl_offset(out, plan->member_offset, 0);
    mir_stream_printf(out,
            "\tld b,%d\n\txor a\nL%d:\n\tld (hl),a\n"
            "\tinc hl\n\tdjnz L%d\n\tret\n",
            plan->byte_count & 255, loop, loop);
}

static int mir_match_constant_byte_fill(struct MirConstantByteFill *plan)
{
    static const int expected_opcodes[26] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_PHI, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP,
        MIR_CONST, MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL
    };
    const struct MirInsn *parameter = &mir.insns[1];
    const struct MirInsn *index_phi = &mir.insns[7];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 26 || mir_cfg_block_count() != 4 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    if (type_ptr_depth(parameter->type) != 1 ||
        (parameter->type & 15) != TYPE_CHAR ||
        mir_machine_pointee_is_volatile(parameter) ||
        !mir_machine_parameter_value_offset(
            parameter->dst, &plan->parameter_stack_offset) ||
        plan->parameter_stack_offset != 2 ||
        !mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[4]) ||
        mir.insns[4].memory_size != 1 ||
        mir.insns[4].src1 != mir.insns[3].dst)
        return 0;
    if ((index_phi->type & 15) != TYPE_CHAR ||
        (index_phi->type & TYPE_UNSIGNED) == 0 ||
        index_phi->src1 != mir.insns[3].dst ||
        index_phi->src2 != mir.insns[22].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[19].label ||
        mir.insns[10].immediate != 0 ||
        mir.insns[10].src1 != index_phi->dst ||
        (mir.insns[10].type & 15) != TYPE_INT ||
        (mir.insns[10].type & TYPE_UNSIGNED) != 0 ||
        type_ptr_depth(mir.insns[10].type) != 0 ||
        type_size(mir.insns[10].type) != 2 ||
        mir.insns[11].immediate != '<' ||
        (mir.insns[11].secondary_offset & 15) != TYPE_INT ||
        (mir.insns[11].secondary_offset & TYPE_UNSIGNED) != 0 ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[11].src2 != mir.insns[9].dst ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[12].label != mir.insns[25].label ||
        mir.insns[15].src1 != parameter->dst ||
        mir.insns[15].src2 != index_phi->dst ||
        mir.insns[15].immediate != 1 ||
        mir.insns[15].memory_size != 1 ||
        mir.insns[18].src1 != mir.insns[15].dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        mir.insns[18].memory_size != 1 ||
        (mir.insns[18].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_constant_equals(mir.insns[21].dst, 1) ||
        (mir.insns[21].type & TYPE_UNSIGNED) == 0 ||
        type_size(mir.insns[21].type) != 1 ||
        mir.insns[22].immediate != '+' ||
        mir.insns[22].type != index_phi->type ||
        mir.insns[22].secondary_offset != index_phi->type ||
        mir.insns[22].src1 != index_phi->dst ||
        mir.insns[22].src2 != mir.insns[21].dst ||
        !mir_machine_same_location(&mir.insns[4], &mir.insns[23]) ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[23].memory_size != 1 ||
        mir.insns[24].label != mir.insns[5].label)
        return 0;
    if (mir.insns[9].immediate <= 0 || mir.insns[9].immediate > 255 ||
        (mir.insns[9].type & 15) != TYPE_INT ||
        (mir.insns[9].type & TYPE_UNSIGNED) != 0 ||
        type_size(mir.insns[17].type) != 1 ||
        mir.insns[17].immediate < -128 ||
        mir.insns[17].immediate > 255)
        return 0;
    plan->count = (int)mir.insns[9].immediate;
    plan->value = (int)mir.insns[17].immediate & 255;
    return 1;
}

static void mir_emit_constant_byte_fill(
    MirStream *out, const struct MirConstantByteFill *plan)
{
    int loop = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n"
            "\tld a,%d\n\tld b,%d\n"
            "L%d:\n\tld (hl),a\n\tinc hl\n\tdjnz L%d\n\tret\n",
            plan->parameter_stack_offset, plan->value,
            plan->count, loop, loop);
}

static int mir_match_affine_byte_fill(struct MirAffineByteFill *plan)
{
    static const int expected_opcodes[31] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_NOP, MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_UNARY,
        MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL
    };
    const struct MirInsn *pointer = &mir.insns[1];
    const struct MirInsn *base = &mir.insns[2];
    const struct MirInsn *index_phi = &mir.insns[9];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 31 || mir_cfg_block_count() != 4 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (type_ptr_depth(pointer->type) != 1 ||
        (pointer->type & 15) != TYPE_CHAR ||
        mir_machine_pointee_is_volatile(pointer) ||
        (base->type & 15) != TYPE_INT ||
        type_ptr_depth(base->type) != 0 ||
        !mir_machine_parameter_value_offset(
            pointer->dst, &plan->pointer_stack_offset) ||
        !mir_machine_parameter_value_offset(
            base->dst, &plan->base_stack_offset) ||
        plan->pointer_stack_offset != 2 ||
        plan->base_stack_offset != 4 ||
        !mir_machine_constant_equals(mir.insns[4].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[5]) ||
        mir.insns[5].memory_size != 1)
        return 0;
    if ((index_phi->type & TYPE_UNSIGNED) == 0 ||
        type_size(index_phi->type) != 1 ||
        index_phi->src1 != mir.insns[4].dst ||
        index_phi->src2 != mir.insns[27].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[24].label ||
        mir.insns[12].immediate != 0 ||
        mir.insns[12].src1 != index_phi->dst ||
        mir.insns[13].immediate != '<' ||
        mir.insns[13].src1 != mir.insns[12].dst ||
        mir.insns[13].src2 != mir.insns[11].dst ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        mir.insns[14].label != mir.insns[30].label ||
        mir.insns[17].src1 != pointer->dst ||
        mir.insns[17].src2 != index_phi->dst ||
        mir.insns[17].immediate != 1 ||
        mir.insns[17].memory_size != 1 ||
        mir.insns[20].immediate != 0 ||
        mir.insns[20].src1 != index_phi->dst ||
        mir.insns[21].immediate != '+' ||
        mir.insns[21].src1 != base->dst ||
        mir.insns[21].src2 != mir.insns[20].dst ||
        mir.insns[22].immediate != 0 ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        type_size(mir.insns[22].type) != 1 ||
        mir.insns[23].src1 != mir.insns[17].dst ||
        mir.insns[23].src2 != mir.insns[22].dst ||
        mir.insns[23].memory_size != 1 ||
        (mir.insns[23].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_constant_equals(mir.insns[26].dst, 1) ||
        mir.insns[27].immediate != '+' ||
        mir.insns[27].src1 != index_phi->dst ||
        mir.insns[27].src2 != mir.insns[26].dst ||
        !mir_machine_same_location(&mir.insns[5], &mir.insns[28]) ||
        mir.insns[28].src1 != mir.insns[27].dst ||
        mir.insns[29].label != mir.insns[6].label)
        return 0;
    if (mir.insns[11].immediate <= 0 || mir.insns[11].immediate > 255)
        return 0;
    plan->count = (int)mir.insns[11].immediate;
    plan->value_from_parameter = 1;
    plan->step = 1;
    return 1;
}

static void mir_emit_affine_byte_fill(
    MirStream *out, const struct MirAffineByteFill *plan)
{
    int loop = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    if (plan->value_from_parameter)
        mir_stream_printf(out,
                "\tld hl,%d\n\tadd hl,sp\n\tld a,(hl)\n",
                plan->base_stack_offset);
    else
        mir_stream_printf(out, "\tld a,%d\n", plan->initial_value);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n"
            "\tld b,%d\n"
            "L%d:\n\tld (hl),a\n\tinc hl\n",
            plan->pointer_stack_offset, plan->count, loop);
    if (plan->step == 1)
        mir_stream_puts("\tinc a\n", out);
    else
        mir_stream_printf(out, "\tadd a,%d\n", plan->step);
    mir_stream_printf(out, "\tdjnz L%d\n\tret\n", loop);
}

static int mir_match_scaled_byte_fill(struct MirAffineByteFill *plan)
{
    static const int expected_opcodes[29] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_PHI, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_STORE_INDIRECT,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL
    };
    const struct MirInsn *parameter;
    const struct MirInsn *index_phi;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 29 || mir_cfg_block_count() != 4 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    parameter = &mir.insns[1];
    index_phi = &mir.insns[7];
    if (type_ptr_depth(parameter->type) != 1 ||
        (parameter->type & 15) != TYPE_CHAR ||
        mir_machine_pointee_is_volatile(parameter) ||
        !mir_machine_parameter_value_offset(
            parameter->dst, &plan->pointer_stack_offset) ||
        !mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[4]) ||
        mir.insns[4].memory_size != 1)
        return 0;
    if (index_phi->src1 != mir.insns[3].dst ||
        index_phi->src2 != mir.insns[25].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[22].label ||
        (index_phi->type & TYPE_UNSIGNED) == 0 ||
        type_size(index_phi->type) != 1 ||
        mir.insns[10].immediate != 0 ||
        mir.insns[10].src1 != index_phi->dst ||
        mir.insns[11].immediate != '<' ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[11].src2 != mir.insns[9].dst ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[12].label != mir.insns[28].label)
        return 0;
    if (mir.insns[15].src1 != parameter->dst ||
        mir.insns[15].src2 != index_phi->dst ||
        mir.insns[15].immediate != 1 ||
        mir.insns[15].memory_size != 1 ||
        mir.insns[18].immediate != 0 ||
        mir.insns[18].src1 != index_phi->dst ||
        mir.insns[19].immediate != '*' ||
        mir.insns[19].src1 != mir.insns[18].dst ||
        mir.insns[19].src2 != mir.insns[17].dst ||
        mir.insns[20].immediate != 0 ||
        mir.insns[20].src1 != mir.insns[19].dst ||
        type_size(mir.insns[20].type) != 1 ||
        mir.insns[21].src1 != mir.insns[15].dst ||
        mir.insns[21].src2 != mir.insns[20].dst ||
        mir.insns[21].memory_size != 1 ||
        (mir.insns[21].memory_flags & (1 | 8)) != 0)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[24].dst, 1) ||
        mir.insns[25].immediate != '+' ||
        mir.insns[25].src1 != index_phi->dst ||
        mir.insns[25].src2 != mir.insns[24].dst ||
        !mir_machine_same_location(&mir.insns[4], &mir.insns[26]) ||
        mir.insns[26].src1 != mir.insns[25].dst ||
        mir.insns[27].label != mir.insns[5].label ||
        mir.insns[9].immediate <= 0 ||
        mir.insns[9].immediate > 255 ||
        mir.insns[17].immediate <= 0 ||
        mir.insns[17].immediate > 255)
        return 0;
    plan->count = (int)mir.insns[9].immediate;
    plan->initial_value = 0;
    plan->step = (int)mir.insns[17].immediate;
    return 1;
}

static int mir_match_wide_left_shift_count(void)
{
    static const int expected_opcodes[30] = {
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_STORE, MIR_CONST, MIR_STORE,
        MIR_LABEL, MIR_PHI, MIR_PHI, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_RETURN
    };
    const struct MirInsn *wide_phi;
    const struct MirInsn *count_phi;
    int instruction;

    if (mir.count != 30 || mir_cfg_block_count() != 4 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    wide_phi = &mir.insns[7];
    count_phi = &mir.insns[8];
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (!mir_machine_constant_equals(mir.insns[2].dst, 1) ||
        type_ptr_depth(mir.insns[2].type) != 0 ||
        (mir.insns[2].type & 15) != TYPE_LONG ||
        (mir.insns[2].type & TYPE_UNSIGNED) == 0 ||
        type_size(mir.insns[2].type) != 4 ||
        !mir_machine_unobservable_local_store(&mir.insns[3]) ||
        mir.insns[3].memory_size != 4 ||
        !mir_machine_constant_equals(mir.insns[4].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[5]) ||
        mir.insns[5].memory_size != 2)
        return 0;
    if (wide_phi->src1 != mir.insns[2].dst ||
        wide_phi->src2 != mir.insns[21].dst ||
        wide_phi->phi_pred1 != mir.insns[0].label ||
        wide_phi->phi_pred2 != mir.insns[25].label ||
        wide_phi->type != mir.insns[2].type ||
        count_phi->src1 != mir.insns[4].dst ||
        count_phi->src2 != mir.insns[17].dst ||
        count_phi->phi_pred1 != mir.insns[0].label ||
        count_phi->phi_pred2 != mir.insns[25].label ||
        type_ptr_depth(count_phi->type) != 0 ||
        (count_phi->type & 15) != TYPE_INT ||
        type_size(count_phi->type) != 2)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[12].dst, 0) ||
        mir.insns[12].type != wide_phi->type ||
        mir.insns[13].immediate != TOK_NE ||
        mir.insns[13].src1 != mir.insns[12].dst ||
        mir.insns[13].src2 != wide_phi->dst ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        mir.insns[14].label != mir.insns[27].label ||
        !mir_machine_constant_equals(mir.insns[16].dst, 1) ||
        mir.insns[17].immediate != '+' ||
        mir.insns[17].src1 != count_phi->dst ||
        mir.insns[17].src2 != mir.insns[16].dst ||
        !mir_machine_same_location(&mir.insns[5], &mir.insns[18]) ||
        mir.insns[18].src1 != mir.insns[17].dst)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[20].dst, 1) ||
        mir.insns[20].type != wide_phi->type ||
        mir.insns[21].immediate != TOK_SHL ||
        mir.insns[21].src1 != wide_phi->dst ||
        mir.insns[21].src2 != mir.insns[20].dst ||
        mir.insns[21].type != wide_phi->type ||
        !mir_machine_same_location(&mir.insns[3], &mir.insns[23]) ||
        mir.insns[23].src1 != mir.insns[21].dst ||
        mir.insns[26].label != mir.insns[6].label ||
        mir.insns[29].src1 != count_phi->dst)
        return 0;
    return 1;
}

static void mir_emit_wide_left_shift_count(MirStream *out)
{
    int done = new_label();
    int loop = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,1\n\tld de,0\n\tld bc,0\n"
            "L%d:\n\tld a,d\n\tor e\n\tor h\n\tor l\n"
            "\tjp z,L%d\n\tinc bc\n\tadd hl,hl\n"
            "\trl e\n\trl d\n\tjp L%d\n"
            "L%d:\n\tld h,b\n\tld l,c\n\tret\n",
            loop, done, loop, done);
}

static int mir_match_palindrome_scan(struct MirPalindromeScan *plan)
{
    static const int expected_opcodes[64] = {
        MIR_LABEL, MIR_PARAM, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_NOP,
        MIR_PHI, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_CONST, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_PHI,
        MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_NOP, MIR_BINARY, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_UNARY, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_CONST, MIR_RETURN, MIR_LABEL, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_RETURN
    };
    const struct MirInsn *parameter;
    const struct MirInsn *length_phi;
    const struct MirInsn *index_phi;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 64 || mir_cfg_block_count() != 8 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_BOOL)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    parameter = &mir.insns[1];
    length_phi = &mir.insns[6];
    index_phi = &mir.insns[27];
    if (type_ptr_depth(parameter->type) != 1 ||
        (parameter->type & 15) != TYPE_CHAR ||
        mir_machine_pointee_is_volatile(parameter) ||
        !mir_machine_parameter_value_offset(
            parameter->dst, &plan->parameter_stack_offset) ||
        !mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[3]) ||
        mir.insns[3].memory_size != 2)
        return 0;
    if (length_phi->src1 != mir.insns[2].dst ||
        length_phi->src2 != mir.insns[14].dst ||
        length_phi->phi_pred1 != mir.insns[0].label ||
        length_phi->phi_pred2 != mir.insns[16].label ||
        type_ptr_depth(length_phi->type) != 0 ||
        (length_phi->type & 15) != TYPE_INT ||
        type_size(length_phi->type) != 2 ||
        mir.insns[9].src1 != parameter->dst ||
        mir.insns[9].src2 != length_phi->dst ||
        mir.insns[9].immediate != 1 ||
        mir.insns[9].memory_size != 1 ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[10].memory_size != 1 ||
        (mir.insns[10].memory_flags & (1 | 8)) != 0 ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[11].label != mir.insns[18].label)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[13].dst, 1) ||
        mir.insns[14].immediate != '+' ||
        mir.insns[14].src1 != length_phi->dst ||
        mir.insns[14].src2 != mir.insns[13].dst ||
        !mir_machine_same_location(&mir.insns[3], &mir.insns[15]) ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        mir.insns[17].label != mir.insns[4].label ||
        !mir_machine_constant_equals(mir.insns[22].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[23]) ||
        mir.insns[23].memory_size != 2)
        return 0;
    if (index_phi->src1 != mir.insns[22].dst ||
        index_phi->src2 != mir.insns[57].dst ||
        index_phi->phi_pred1 != mir.insns[18].label ||
        index_phi->phi_pred2 != mir.insns[54].label ||
        type_ptr_depth(index_phi->type) != 0 ||
        (index_phi->type & 15) != TYPE_INT ||
        type_size(index_phi->type) != 2 ||
        !mir_machine_constant_equals(mir.insns[30].dst, 2) ||
        mir.insns[31].immediate != '/' ||
        mir.insns[31].src1 != length_phi->dst ||
        mir.insns[31].src2 != mir.insns[30].dst ||
        mir.insns[32].immediate != '<' ||
        mir.insns[32].src1 != index_phi->dst ||
        mir.insns[32].src2 != mir.insns[31].dst ||
        mir.insns[33].src1 != mir.insns[32].dst ||
        mir.insns[33].label != mir.insns[60].label)
        return 0;
    if (mir.insns[36].src1 != parameter->dst ||
        mir.insns[36].src2 != index_phi->dst ||
        mir.insns[36].immediate != 1 ||
        mir.insns[36].memory_size != 1 ||
        mir.insns[37].src1 != mir.insns[36].dst ||
        mir.insns[37].memory_size != 1 ||
        (mir.insns[37].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_constant_equals(mir.insns[40].dst, 1) ||
        mir.insns[41].immediate != '-' ||
        mir.insns[41].src1 != length_phi->dst ||
        mir.insns[41].src2 != mir.insns[40].dst ||
        mir.insns[43].immediate != '-' ||
        mir.insns[43].src1 != mir.insns[41].dst ||
        mir.insns[43].src2 != index_phi->dst)
        return 0;
    if (mir.insns[44].src1 != parameter->dst ||
        mir.insns[44].src2 != mir.insns[43].dst ||
        mir.insns[44].immediate != 1 ||
        mir.insns[44].memory_size != 1 ||
        mir.insns[45].src1 != mir.insns[44].dst ||
        mir.insns[45].memory_size != 1 ||
        (mir.insns[45].memory_flags & (1 | 8)) != 0 ||
        mir.insns[46].immediate != 0 ||
        mir.insns[46].src1 != mir.insns[37].dst ||
        mir.insns[47].immediate != 0 ||
        mir.insns[47].src1 != mir.insns[45].dst ||
        mir.insns[48].immediate != TOK_NE ||
        mir.insns[48].src1 != mir.insns[46].dst ||
        mir.insns[48].src2 != mir.insns[47].dst ||
        mir.insns[49].src1 != mir.insns[48].dst ||
        mir.insns[49].label != mir.insns[53].label)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[51].dst, 0) ||
        mir.insns[52].src1 != mir.insns[51].dst ||
        !mir_machine_constant_equals(mir.insns[56].dst, 1) ||
        mir.insns[57].immediate != '+' ||
        mir.insns[57].src1 != index_phi->dst ||
        mir.insns[57].src2 != mir.insns[56].dst ||
        !mir_machine_same_location(&mir.insns[23], &mir.insns[58]) ||
        mir.insns[58].src1 != mir.insns[57].dst ||
        mir.insns[59].label != mir.insns[24].label ||
        !mir_machine_constant_equals(mir.insns[62].dst, 1) ||
        mir.insns[63].src1 != mir.insns[62].dst)
        return 0;
    return 1;
}

static void mir_emit_palindrome_scan(
    MirStream *out, const struct MirPalindromeScan *plan)
{
    int different = new_label();
    int found = new_label();
    int loop = new_label();
    int scan = new_label();
    int same = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tld h,b\n\tld l,c\n"
            "L%d:\n\tld a,(hl)\n\tor a\n"
            "\tjp z,L%d\n\tinc hl\n\tjp L%d\n"
            "L%d:\n\tdec hl\n\tex de,hl\n"
            "L%d:\n\tld h,d\n\tld l,e\n\tor a\n\tsbc hl,bc\n"
            "\tjp c,L%d\n\tjp z,L%d\n"
            "\tld a,(bc)\n\tld h,d\n\tld l,e\n\tcp (hl)\n"
            "\tjp nz,L%d\n\tinc bc\n\tdec de\n\tjp L%d\n"
            "L%d:\n\tld hl,1\n\tret\n"
            "L%d:\n\tld hl,0\n\tret\n",
            plan->parameter_stack_offset,
            scan, found, scan, found, loop, same, same,
            different, loop, same, different);
}

static int mir_match_dynamic_row_scan(struct MirDynamicRowScan *plan)
{
    static const int expected_opcodes[35] = {
        MIR_LABEL, MIR_CONST, MIR_STORE, MIR_ADDRESS, MIR_NOP, MIR_STORE,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_LOAD, MIR_PHI,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_ADDRESS, MIR_LOAD,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_RETURN
    };
    const struct MirInsn *column_phi;
    const struct MirInsn *table_address;
    int address_offset;
    int address_storage;
    int address_type;
    int instruction;
    int row_offset;
    int row_storage;
    int row_type;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 35 || mir_cfg_block_count() != 4 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return mir_machine_reject("dynamic-row-scan", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject("dynamic-row-scan", "opcode");
    column_phi = &mir.insns[11];
    table_address = &mir.insns[19];
    if (!mir_scalar_memory_location(
            &mir.insns[2], &row_type, &row_storage, &row_offset) ||
        !mir_scalar_memory_location(
            &mir.insns[3], &address_type, &address_storage,
            &address_offset) ||
        row_storage != SC_LOCAL || address_storage != row_storage ||
        address_offset != row_offset)
        return mir_machine_reject(
            "dynamic-row-scan", "setup-location");
    if (!mir_machine_constant_equals(mir.insns[1].dst, 0) ||
        mir.insns[2].memory_size != 2 ||
        (mir.insns[2].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject("dynamic-row-scan", "setup-row");
    if (mir.insns[5].src1 != mir.insns[3].dst ||
        mir.insns[5].memory_size != 2 ||
        (mir.insns[5].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject(
            "dynamic-row-scan", "setup-pointer");
    if (!mir_machine_constant_equals(mir.insns[6].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[8]) ||
        mir.insns[8].memory_size != 2)
        return mir_machine_reject(
            "dynamic-row-scan", "setup-column");
    if (!mir_machine_same_location(&mir.insns[2], &mir.insns[10]) ||
        column_phi->src1 != mir.insns[6].dst ||
        column_phi->src2 != mir.insns[29].dst ||
        column_phi->phi_pred1 != mir.insns[0].label ||
        column_phi->phi_pred2 != mir.insns[26].label ||
        type_ptr_depth(column_phi->type) != 0 ||
        (column_phi->type & 15) != TYPE_INT ||
        type_size(column_phi->type) != 2 ||
        mir.insns[13].immediate <= 0 ||
        mir.insns[13].immediate > 255 ||
        mir.insns[14].immediate != '<' ||
        mir.insns[14].src1 != column_phi->dst ||
        mir.insns[14].src2 != mir.insns[13].dst ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        mir.insns[15].label != mir.insns[32].label)
        return mir_machine_reject("dynamic-row-scan", "loop");
    if (!mir_machine_same_location(&mir.insns[5], &mir.insns[16]) ||
        !mir_machine_constant_equals(mir.insns[17].dst, 0) ||
        mir.insns[18].src1 != mir.insns[16].dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        mir.insns[18].immediate != 2 ||
        mir.insns[18].memory_size != 2 ||
        table_address->memory_flags != 0 ||
        !mir_machine_same_location(&mir.insns[2], &mir.insns[20]) ||
        mir.insns[21].src1 != table_address->dst ||
        mir.insns[21].src2 != mir.insns[20].dst ||
        mir.insns[21].immediate <= 0 ||
        mir.insns[21].memory_size <= 0 ||
        mir.insns[23].src1 != mir.insns[21].dst ||
        mir.insns[23].src2 != column_phi->dst ||
        mir.insns[23].immediate <= 0 ||
        mir.insns[23].memory_size != 2)
        return mir_machine_reject("dynamic-row-scan", "addresses");
    if (mir.insns[24].src1 != mir.insns[23].dst ||
        mir.insns[24].memory_size != 2 ||
        (mir.insns[24].memory_flags & (1 | 8)) != 0 ||
        mir.insns[25].src1 != mir.insns[18].dst ||
        mir.insns[25].src2 != mir.insns[24].dst ||
        mir.insns[25].memory_size != 2 ||
        (mir.insns[25].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_constant_equals(mir.insns[28].dst, 1) ||
        mir.insns[29].immediate != '+' ||
        mir.insns[29].src1 != column_phi->dst ||
        mir.insns[29].src2 != mir.insns[28].dst ||
        !mir_machine_same_location(&mir.insns[8], &mir.insns[30]) ||
        mir.insns[30].src1 != mir.insns[29].dst ||
        mir.insns[31].label != mir.insns[9].label ||
        !mir_machine_same_location(&mir.insns[2], &mir.insns[33]) ||
        mir.insns[34].src1 != mir.insns[33].dst)
        return mir_machine_reject("dynamic-row-scan", "body");
    plan->table = find_global(table_address->name);
    if (plan->table == NULL || !plan->table->is_defined ||
        plan->table->is_volatile)
        return mir_machine_reject("dynamic-row-scan", "table");
    plan->row_stride = (int)mir.insns[21].immediate;
    plan->element_stride = (int)mir.insns[23].immediate;
    plan->count = (int)mir.insns[13].immediate;
    if ((plan->row_stride & (plan->row_stride - 1)) != 0 ||
        plan->row_stride > 32767 ||
        plan->element_stride > 127 ||
        plan->count * plan->element_stride > 255)
        return mir_machine_reject("dynamic-row-scan", "bounds");
    return 1;
}

static void mir_emit_dynamic_row_scan(
    MirStream *out, const struct MirDynamicRowScan *plan)
{
    int loop = new_label();
    int step;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out, "\tld de,0\n\tld bc,0\nL%d:\n"
                 "\tld h,d\n\tld l,e\n", loop);
    for (step = 1; step < plan->row_stride; step <<= 1)
        mir_stream_puts("\tadd hl,hl\n", out);
    mir_stream_puts("\tadd hl,bc\n", out);
    mir_machine_emit_global_address_de(out, plan->table, 0);
    mir_stream_puts("\tadd hl,de\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n",
          out);
    for (step = 0; step < plan->element_stride; ++step)
        mir_stream_puts("\tinc bc\n", out);
    mir_stream_printf(out,
            "\tld a,c\n\tcp %d\n\tjp nz,L%d\n"
            "\tex de,hl\n\tret\n",
            plan->count * plan->element_stride, loop);
}

static int mir_match_byte_mismatch_scan(
    struct MirByteMismatchScan *plan)
{
    static const int expected_opcodes[42] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_NOP, MIR_UNARY,
        MIR_BINARY, MIR_UNARY, MIR_UNARY, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_UNARY, MIR_RETURN, MIR_LABEL,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_RETURN
    };
    const struct MirInsn *pointer = &mir.insns[1];
    const struct MirInsn *base = &mir.insns[2];
    const struct MirInsn *index_phi = &mir.insns[9];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 42 || mir_cfg_block_count() != 5 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return mir_machine_reject("byte-mismatch-scan", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject("byte-mismatch-scan", "opcode");
    if (type_ptr_depth(pointer->type) != 1 ||
        (pointer->type & 15) != TYPE_CHAR ||
        type_size(pointer->type) != 2 ||
        mir_machine_pointee_is_volatile(pointer) ||
        type_ptr_depth(base->type) != 0 ||
        (base->type & 15) != TYPE_INT ||
        type_size(base->type) != 2 ||
        !mir_machine_parameter_value_offset(
            pointer->dst, &plan->pointer_stack_offset) ||
        !mir_machine_parameter_value_offset(
            base->dst, &plan->base_stack_offset))
        return mir_machine_reject(
            "byte-mismatch-scan", "parameters");
    if (!mir_machine_constant_equals(mir.insns[4].dst, 0) ||
        type_size(mir.insns[4].type) != 1 ||
        (mir.insns[4].type & TYPE_UNSIGNED) == 0 ||
        !mir_machine_unobservable_local_store(&mir.insns[5]) ||
        mir.insns[5].memory_size != 1 ||
        mir.insns[5].src1 != mir.insns[4].dst ||
        index_phi->src1 != mir.insns[4].dst ||
        index_phi->src2 != mir.insns[35].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[32].label ||
        type_size(index_phi->type) != 1 ||
        (index_phi->type & TYPE_UNSIGNED) == 0)
        return mir_machine_reject(
            "byte-mismatch-scan", "index");
    if (mir.insns[11].immediate <= 0 ||
        mir.insns[11].immediate > 255 ||
        mir.insns[12].immediate != 0 ||
        mir.insns[12].src1 != index_phi->dst ||
        mir.insns[13].immediate != '<' ||
        mir.insns[13].src1 != mir.insns[12].dst ||
        mir.insns[13].src2 != mir.insns[11].dst ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        mir.insns[14].label != mir.insns[38].label)
        return mir_machine_reject(
            "byte-mismatch-scan", "bound");
    plan->count = (int)mir.insns[11].immediate;
    if (mir.insns[17].src1 != pointer->dst ||
        mir.insns[17].src2 != index_phi->dst ||
        mir.insns[17].immediate != 1 ||
        mir.insns[17].memory_size != 1 ||
        mir.insns[18].src1 != mir.insns[17].dst ||
        mir.insns[18].memory_size != 1 ||
        (mir.insns[18].memory_flags & (1 | 8)) != 0 ||
        mir.insns[21].immediate != 0 ||
        mir.insns[21].src1 != index_phi->dst ||
        mir.insns[22].immediate != '+' ||
        mir.insns[22].src1 != base->dst ||
        mir.insns[22].src2 != mir.insns[21].dst ||
        mir.insns[23].immediate != 0 ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        type_size(mir.insns[23].type) != 1 ||
        mir.insns[24].immediate != 0 ||
        mir.insns[24].src1 != mir.insns[18].dst ||
        mir.insns[25].immediate != 0 ||
        mir.insns[25].src1 != mir.insns[23].dst ||
        mir.insns[26].immediate != TOK_NE ||
        mir.insns[26].src1 != mir.insns[24].dst ||
        mir.insns[26].src2 != mir.insns[25].dst ||
        mir.insns[27].src1 != mir.insns[26].dst ||
        mir.insns[27].label != mir.insns[31].label ||
        mir.insns[29].immediate != 0 ||
        mir.insns[29].src1 != index_phi->dst ||
        mir.insns[30].src1 != mir.insns[29].dst)
        return mir_machine_reject(
            "byte-mismatch-scan", "comparison");
    if (!mir_machine_constant_equals(mir.insns[34].dst, 1) ||
        type_size(mir.insns[34].type) != 1 ||
        mir.insns[35].immediate != '+' ||
        mir.insns[35].src1 != index_phi->dst ||
        mir.insns[35].src2 != mir.insns[34].dst ||
        mir.insns[36].object != mir.insns[5].object ||
        mir.insns[36].memory_size != 1 ||
        mir.insns[36].src1 != mir.insns[35].dst ||
        mir.insns[37].label != mir.insns[6].label ||
        !mir_machine_constant_equals(mir.insns[40].dst, 65535) ||
        mir.insns[41].src1 != mir.insns[40].dst)
        return mir_machine_reject(
            "byte-mismatch-scan", "result");
    return 1;
}

static void mir_emit_byte_mismatch_scan(
    MirStream *out, const struct MirByteMismatchScan *plan)
{
    int loop = new_label();
    int mismatch = new_label();

    mir_stream_printf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tpush bc\n\tpop iy\n"
            "\tld de,%d\n\tadd iy,de\n"
            "\tld hl,%d\n\tadd hl,sp\n\tld e,(hl)\n"
            "L%d:\n\tld a,(bc)\n\tcp e\n\tjp nz,L%d\n"
            "\tinc bc\n\tinc e\n"
            "\tpush iy\n\tpop hl\n\tor a\n\tsbc hl,bc\n"
            "\tjp nz,L%d\n"
            "\tld hl,65535\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n"
            "L%d:\n\tld h,b\n\tld l,c\n"
            "\tpush iy\n\tpop de\n\tor a\n\tsbc hl,de\n"
            "\tld de,%d\n\tadd hl,de\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            plan->pointer_stack_offset + 2, plan->count,
            plan->base_stack_offset + 2,
            loop, mismatch, loop, mismatch, plan->count);
}

static int mir_match_variable_byte_step_sum(
    struct MirVariableByteStepSum *plan)
{
    static const int expected_opcodes[41] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_CONST, MIR_STORE, MIR_NOP,
        MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_PHI, MIR_PHI, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP,
        MIR_UNARY, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP,
        MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_NOP,
        MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_RETURN
    };
    const struct MirInsn *first = &mir.insns[1];
    const struct MirInsn *step = &mir.insns[2];
    const struct MirInsn *sum_phi = &mir.insns[15];
    const struct MirInsn *value_phi = &mir.insns[16];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 41 || mir_cfg_block_count() != 4 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return mir_machine_reject(
            "variable-byte-step-sum", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "variable-byte-step-sum", "opcode");
    if (type_ptr_depth(first->type) != 0 ||
        type_size(first->type) != 1 ||
        (first->type & TYPE_UNSIGNED) == 0 ||
        type_ptr_depth(step->type) != 0 ||
        type_size(step->type) != 1 ||
        (step->type & TYPE_UNSIGNED) == 0 ||
        !mir_machine_parameter_value_offset(
            first->dst, &plan->first_stack_offset) ||
        !mir_machine_parameter_value_offset(
            step->dst, &plan->step_stack_offset))
        return mir_machine_reject(
            "variable-byte-step-sum", "parameters");
    if (!mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[4]) ||
        mir.insns[4].memory_size != 2 ||
        mir.insns[4].src1 != mir.insns[3].dst ||
        mir.insns[7].immediate != 0 ||
        mir.insns[7].src1 != first->dst ||
        mir.insns[8].immediate != 0 ||
        mir.insns[8].src1 != step->dst ||
        mir.insns[9].immediate != '+' ||
        mir.insns[9].src1 != mir.insns[7].dst ||
        mir.insns[9].src2 != mir.insns[8].dst ||
        mir.insns[10].immediate != 0 ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        type_size(mir.insns[10].type) != 1 ||
        !mir_machine_unobservable_local_store(&mir.insns[11]) ||
        mir.insns[11].memory_size != 1 ||
        mir.insns[11].src1 != mir.insns[10].dst)
        return mir_machine_reject(
            "variable-byte-step-sum", "initializers");
    if (sum_phi->src1 != mir.insns[3].dst ||
        sum_phi->src2 != mir.insns[25].dst ||
        sum_phi->phi_pred1 != mir.insns[0].label ||
        sum_phi->phi_pred2 != mir.insns[28].label ||
        value_phi->src1 != mir.insns[10].dst ||
        value_phi->src2 != mir.insns[34].dst ||
        value_phi->phi_pred1 != mir.insns[0].label ||
        value_phi->phi_pred2 != mir.insns[28].label ||
        type_size(value_phi->type) != 1 ||
        (value_phi->type & TYPE_UNSIGNED) == 0)
        return mir_machine_reject(
            "variable-byte-step-sum", "phis");
    if (mir.insns[18].immediate <= 0 ||
        mir.insns[18].immediate > 255 ||
        mir.insns[19].immediate != 0 ||
        mir.insns[19].src1 != value_phi->dst ||
        mir.insns[20].immediate != '<' ||
        mir.insns[20].src1 != mir.insns[19].dst ||
        mir.insns[20].src2 != mir.insns[18].dst ||
        mir.insns[21].src1 != mir.insns[20].dst ||
        mir.insns[21].label != mir.insns[38].label)
        return mir_machine_reject(
            "variable-byte-step-sum", "condition");
    plan->bound = (int)mir.insns[18].immediate;
    if (mir.insns[24].immediate != 0 ||
        mir.insns[24].src1 != value_phi->dst ||
        mir.insns[25].immediate != '+' ||
        mir.insns[25].src1 != sum_phi->dst ||
        mir.insns[25].src2 != mir.insns[24].dst ||
        mir.insns[27].object != mir.insns[4].object ||
        mir.insns[27].src1 != mir.insns[25].dst ||
        mir.insns[31].immediate != 0 ||
        mir.insns[31].src1 != value_phi->dst ||
        mir.insns[32].immediate != 0 ||
        mir.insns[32].src1 != step->dst ||
        mir.insns[33].immediate != '+' ||
        mir.insns[33].src1 != mir.insns[31].dst ||
        mir.insns[33].src2 != mir.insns[32].dst ||
        mir.insns[34].immediate != 0 ||
        mir.insns[34].src1 != mir.insns[33].dst ||
        type_size(mir.insns[34].type) != 1 ||
        mir.insns[36].object != mir.insns[11].object ||
        mir.insns[36].src1 != mir.insns[34].dst ||
        mir.insns[37].label != mir.insns[12].label ||
        mir.insns[40].src1 != sum_phi->dst)
        return mir_machine_reject(
            "variable-byte-step-sum", "body");
    return 1;
}

static int mir_match_alias_byte_step_sum(
    struct MirVariableByteStepSum *plan)
{
    static const int expected_opcodes[59] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_ADDRESS, MIR_NOP, MIR_STORE,
        MIR_CONST, MIR_STORE, MIR_NOP, MIR_NOP, MIR_UNARY, MIR_UNARY,
        MIR_BINARY, MIR_UNARY, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP,
        MIR_LOAD, MIR_PHI, MIR_LOAD, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_LOAD, MIR_UNARY, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_LOAD, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD_INDIRECT, MIR_NOP, MIR_UNARY,
        MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_STORE_INDIRECT, MIR_LABEL,
        MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_UNARY, MIR_UNARY,
        MIR_BINARY, MIR_UNARY, MIR_NOP, MIR_STORE, MIR_JUMP, MIR_LABEL,
        MIR_NOP, MIR_RETURN
    };
    const struct MirInsn *first = &mir.insns[1];
    const struct MirInsn *step = &mir.insns[2];
    const struct MirInsn *alias_store = &mir.insns[5];
    const struct MirInsn *sum_store = &mir.insns[7];
    const struct MirInsn *value_store = &mir.insns[14];
    const struct MirInsn *sum_phi = &mir.insns[19];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 59 || mir_cfg_block_count() != 5 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return mir_machine_reject(
            "alias-byte-step-sum", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "alias-byte-step-sum", "opcode");
    if (type_size(first->type) != 1 ||
        (first->type & TYPE_UNSIGNED) == 0 ||
        type_size(step->type) != 1 ||
        (step->type & TYPE_UNSIGNED) == 0 ||
        !mir_machine_parameter_value_offset(
            first->dst, &plan->first_stack_offset) ||
        !mir_machine_parameter_value_offset(
            step->dst, &plan->step_stack_offset))
        return mir_machine_reject(
            "alias-byte-step-sum", "parameters");
    if (mir.insns[3].src1 >= 0 ||
        !mir_machine_unobservable_local_store(alias_store) ||
        alias_store->src1 != mir.insns[3].dst ||
        alias_store->memory_size != 2 ||
        !mir_machine_constant_equals(mir.insns[6].dst, 0) ||
        !mir_machine_unobservable_local_store(sum_store) ||
        sum_store->src1 != mir.insns[6].dst ||
        sum_store->memory_size != 2 ||
        mir.insns[10].immediate != 0 ||
        mir.insns[10].src1 != first->dst ||
        mir.insns[11].immediate != 0 ||
        mir.insns[11].src1 != step->dst ||
        mir.insns[12].immediate != '+' ||
        mir.insns[12].src1 != mir.insns[10].dst ||
        mir.insns[12].src2 != mir.insns[11].dst ||
        mir.insns[13].immediate != 0 ||
        mir.insns[13].src1 != mir.insns[12].dst ||
        type_size(mir.insns[13].type) != 1 ||
        value_store->src1 != mir.insns[13].dst ||
        value_store->memory_size != 1)
        return mir_machine_reject(
            "alias-byte-step-sum", "initializers");
    if (!mir_machine_same_location(value_store, &mir.insns[18]) ||
        !mir_machine_same_location(value_store, &mir.insns[20]) ||
        !mir_machine_same_location(value_store, &mir.insns[26]) ||
        !mir_machine_same_location(value_store, &mir.insns[31]) ||
        !mir_machine_same_location(value_store, &mir.insns[47]) ||
        sum_phi->src1 != mir.insns[6].dst ||
        sum_phi->src2 != mir.insns[28].dst ||
        sum_phi->phi_pred1 != mir.insns[0].label ||
        sum_phi->phi_pred2 != mir.insns[46].label)
        return mir_machine_reject(
            "alias-byte-step-sum", "state");
    if (mir.insns[21].immediate <= 0 ||
        mir.insns[21].immediate > 255 ||
        mir.insns[22].immediate != 0 ||
        mir.insns[22].src1 != mir.insns[20].dst ||
        mir.insns[23].immediate != '<' ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[23].src2 != mir.insns[21].dst ||
        mir.insns[24].src1 != mir.insns[23].dst ||
        mir.insns[24].label != mir.insns[56].label)
        return mir_machine_reject(
            "alias-byte-step-sum", "condition");
    plan->bound = (int)mir.insns[21].immediate;
    if (mir.insns[27].immediate != 0 ||
        mir.insns[27].src1 != mir.insns[26].dst ||
        mir.insns[28].immediate != '+' ||
        mir.insns[28].src1 != sum_phi->dst ||
        mir.insns[28].src2 != mir.insns[27].dst ||
        mir.insns[30].object != sum_store->object ||
        mir.insns[30].src1 != mir.insns[28].dst ||
        mir.insns[33].immediate != 0 ||
        mir.insns[33].src1 != mir.insns[31].dst ||
        mir.insns[34].immediate != TOK_EQ ||
        mir.insns[34].src1 != mir.insns[33].dst ||
        mir.insns[34].src2 != mir.insns[32].dst ||
        mir.insns[35].src1 != mir.insns[34].dst ||
        mir.insns[35].label != mir.insns[44].label)
        return mir_machine_reject(
            "alias-byte-step-sum", "sum-guard");
    plan->double_step_value = (int)mir.insns[32].immediate;
    plan->has_double_step = 1;
    if (!mir_machine_same_location(alias_store, &mir.insns[36]) ||
        mir.insns[37].src1 != mir.insns[36].dst ||
        mir.insns[37].memory_size != 1 ||
        (mir.insns[37].memory_flags & (1 | 8)) != 0 ||
        mir.insns[39].immediate != 0 ||
        mir.insns[39].src1 != mir.insns[37].dst ||
        mir.insns[40].immediate != 0 ||
        mir.insns[40].src1 != step->dst ||
        mir.insns[41].immediate != '+' ||
        mir.insns[41].src1 != mir.insns[39].dst ||
        mir.insns[41].src2 != mir.insns[40].dst ||
        mir.insns[42].immediate != 0 ||
        mir.insns[42].src1 != mir.insns[41].dst ||
        mir.insns[43].src1 != mir.insns[36].dst ||
        mir.insns[43].src2 != mir.insns[42].dst ||
        mir.insns[43].memory_size != 1 ||
        (mir.insns[43].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject(
            "alias-byte-step-sum", "alias-update");
    if (mir.insns[49].immediate != 0 ||
        mir.insns[49].src1 != mir.insns[47].dst ||
        mir.insns[50].immediate != 0 ||
        mir.insns[50].src1 != step->dst ||
        mir.insns[51].immediate != '+' ||
        mir.insns[51].src1 != mir.insns[49].dst ||
        mir.insns[51].src2 != mir.insns[50].dst ||
        mir.insns[52].immediate != 0 ||
        mir.insns[52].src1 != mir.insns[51].dst ||
        !mir_machine_same_location(value_store, &mir.insns[54]) ||
        mir.insns[54].src1 != mir.insns[52].dst ||
        mir.insns[55].label != mir.insns[15].label ||
        mir.insns[58].src1 != sum_phi->dst)
        return mir_machine_reject(
            "alias-byte-step-sum", "step");
    return 1;
}

static void mir_emit_variable_byte_step_sum(
    MirStream *out, const struct MirVariableByteStepSum *plan)
{
    int done = new_label();
    int loop = new_label();
    int normal_step = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld a,(hl)\n"
            "\tld hl,%d\n\tadd hl,sp\n\tld c,(hl)\n"
            "\tadd a,c\n\tld b,a\n\tld de,0\n"
            "L%d:\n\tld a,b\n\tcp %d\n\tjp nc,L%d\n"
            "\tld l,b\n\tld h,0\n\tadd hl,de\n\tex de,hl\n",
            plan->first_stack_offset,
            plan->step_stack_offset,
            loop, plan->bound, done);
    if (plan->has_double_step)
        mir_stream_printf(out,
                "\tld a,b\n\tcp %d\n\tjp nz,L%d\n"
                "\tadd a,c\n\tld b,a\nL%d:\n",
                plan->double_step_value, normal_step, normal_step);
    mir_stream_printf(out,
            "\tld a,b\n\tadd a,c\n\tld b,a\n\tjp L%d\n"
            "L%d:\n\tex de,hl\n\tret\n",
            loop, done);
}

static int mir_match_fixed_reverse_word_copy(
    struct MirFixedReverseWordCopy *plan)
{
    static const int expected_opcodes[31] = {
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_PHI,
        MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_ADDRESS, MIR_NOP,
        MIR_NOP, MIR_CONST, MIR_NOP, MIR_UNARY, MIR_BINARY,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT, MIR_LABEL,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL
    };
    const struct MirInsn *index_phi = &mir.insns[5];
    const struct MirInsn *destination = &mir.insns[11];
    const struct MirInsn *source = &mir.insns[14];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 31 || mir_cfg_block_count() != 4 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return mir_machine_reject(
            "fixed-reverse-word-copy", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "fixed-reverse-word-copy", "opcode");
    if (!mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[3]) ||
        mir.insns[3].memory_size != 1 ||
        index_phi->src1 != mir.insns[2].dst ||
        index_phi->src2 != mir.insns[27].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[24].label ||
        mir.insns[7].immediate <= 0 ||
        mir.insns[7].immediate > 127 ||
        mir.insns[8].immediate != 0 ||
        mir.insns[8].src1 != index_phi->dst ||
        mir.insns[9].immediate != '<' ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].src2 != mir.insns[7].dst ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[10].label != mir.insns[30].label)
        return mir_machine_reject(
            "fixed-reverse-word-copy", "loop");
    plan->count = (int)mir.insns[7].immediate;
    if (!mir_scalar_memory_location(
            destination, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        destination->type == 0 ||
        mir.insns[13].src1 != destination->dst ||
        mir.insns[13].src2 != index_phi->dst ||
        mir.insns[13].immediate != 2 ||
        mir.insns[13].memory_size != 2)
        return mir_machine_reject(
            "fixed-reverse-word-copy", "destination");
    plan->destination = find_global(destination->name);
    plan->destination_offset = memory_offset;
    if (plan->destination == NULL ||
        plan->destination->is_volatile)
        return mir_machine_reject(
            "fixed-reverse-word-copy", "destination-symbol");
    if (!mir_scalar_memory_location(
            source, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        mir.insns[17].immediate != plan->count - 1 ||
        mir.insns[19].immediate != 0 ||
        mir.insns[19].src1 != index_phi->dst ||
        mir.insns[20].immediate != '-' ||
        mir.insns[20].src1 != mir.insns[17].dst ||
        mir.insns[20].src2 != mir.insns[19].dst ||
        mir.insns[21].src1 != source->dst ||
        mir.insns[21].src2 != mir.insns[20].dst ||
        mir.insns[21].immediate != 2 ||
        mir.insns[21].memory_size != 2 ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        mir.insns[22].memory_size != 2 ||
        (mir.insns[22].memory_flags & (1 | 8)) != 0 ||
        mir.insns[23].src1 != mir.insns[13].dst ||
        mir.insns[23].src2 != mir.insns[22].dst ||
        mir.insns[23].memory_size != 2 ||
        (mir.insns[23].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject(
            "fixed-reverse-word-copy", "source");
    plan->source = find_global(source->name);
    plan->source_offset = memory_offset;
    if (plan->source == NULL || plan->source->is_volatile ||
        plan->source == plan->destination)
        return mir_machine_reject(
            "fixed-reverse-word-copy", "source-symbol");
    if (!mir_machine_constant_equals(mir.insns[26].dst, 1) ||
        mir.insns[27].immediate != '+' ||
        mir.insns[27].src1 != index_phi->dst ||
        mir.insns[27].src2 != mir.insns[26].dst ||
        mir.insns[28].object != mir.insns[3].object ||
        mir.insns[28].src1 != mir.insns[27].dst ||
        mir.insns[29].label != mir.insns[4].label)
        return mir_machine_reject(
            "fixed-reverse-word-copy", "increment");
    return 1;
}

static void mir_emit_fixed_reverse_word_copy(
    MirStream *out, const struct MirFixedReverseWordCopy *plan)
{
    int loop = new_label();

    mir_stream_printf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_address_de(
        out, plan->source,
        plan->source_offset + (plan->count - 1) * 2);
    mir_stream_puts("\tpush de\n\tpop iy\n", out);
    mir_machine_emit_global_address_de(
        out, plan->destination, plan->destination_offset);
    mir_stream_printf(out,
            "\tld c,%d\n"
            "L%d:\n\tld a,(iy+0)\n\tld (de),a\n\tinc de\n"
            "\tld a,(iy+1)\n\tld (de),a\n\tinc de\n"
            "\tdec iy\n\tdec iy\n\tdec c\n\tjp nz,L%d\n"
            "\tpop iy\n;@dcc.reg free=iy\n\tret\n",
            plan->count, loop, loop);
}

static int mir_match_fixed_random_word_fill(
    struct MirFixedRandomWordFill *plan)
{
    static const int expected_opcodes[26] = {
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_PHI,
        MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_CALL, MIR_CONST,
        MIR_BINARY, MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_CALL
    };
    const struct MirInsn *index_phi = &mir.insns[5];
    const struct MirInsn *destination = &mir.insns[11];
    const struct MirInsn *random_call = &mir.insns[14];
    const struct MirInsn *finish_call = &mir.insns[25];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 26 || mir_cfg_block_count() != 4 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return mir_machine_reject(
            "fixed-random-word-fill", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "fixed-random-word-fill", "opcode");
    if (!mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[3]) ||
        mir.insns[3].memory_size != 1 ||
        index_phi->src1 != mir.insns[2].dst ||
        index_phi->src2 != mir.insns[21].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[18].label ||
        mir.insns[7].immediate <= 0 ||
        mir.insns[7].immediate > 127 ||
        mir.insns[8].immediate != 0 ||
        mir.insns[8].src1 != index_phi->dst ||
        mir.insns[9].immediate != '<' ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].src2 != mir.insns[7].dst ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[10].label != mir.insns[24].label)
        return mir_machine_reject(
            "fixed-random-word-fill", "loop");
    plan->count = (int)mir.insns[7].immediate;
    if (!mir_scalar_memory_location(
            destination, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        mir.insns[13].src1 != destination->dst ||
        mir.insns[13].src2 != index_phi->dst ||
        mir.insns[13].immediate != 2 ||
        mir.insns[13].memory_size != 2 ||
        !mir_machine_call_has_no_arguments(random_call) ||
        random_call->type != mir.insns[16].type ||
        mir.insns[15].immediate <= 1 ||
        mir.insns[15].immediate > 32767 ||
        mir.insns[16].immediate != '%' ||
        mir.insns[16].src1 != random_call->dst ||
        mir.insns[16].src2 != mir.insns[15].dst ||
        mir.insns[17].src1 != mir.insns[13].dst ||
        mir.insns[17].src2 != mir.insns[16].dst ||
        mir.insns[17].memory_size != 2 ||
        (mir.insns[17].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject(
            "fixed-random-word-fill", "body");
    plan->destination = find_global(destination->name);
    plan->destination_offset = memory_offset;
    plan->random_function = find_global(random_call->name);
    plan->finish_function = find_global(finish_call->name);
    plan->modulus = (int)mir.insns[15].immediate;
    if (plan->destination == NULL ||
        plan->destination->is_volatile ||
        plan->random_function == NULL ||
        !plan->random_function->is_defined ||
        plan->finish_function == NULL ||
        !plan->finish_function->is_defined ||
        plan->random_function->is_funcptr ||
        plan->finish_function->is_funcptr ||
        !mir_machine_call_has_no_arguments(finish_call))
        return mir_machine_reject(
            "fixed-random-word-fill", "symbols");
    if (!mir_machine_constant_equals(mir.insns[20].dst, 1) ||
        mir.insns[21].immediate != '+' ||
        mir.insns[21].src1 != index_phi->dst ||
        mir.insns[21].src2 != mir.insns[20].dst ||
        mir.insns[22].object != mir.insns[3].object ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        mir.insns[23].label != mir.insns[4].label)
        return mir_machine_reject(
            "fixed-random-word-fill", "increment");
    return 1;
}

static void mir_emit_fixed_random_word_fill(
    MirStream *out, const struct MirFixedRandomWordFill *plan)
{
    int loop = new_label();

    mir_stream_printf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_address_de(
        out, plan->destination, plan->destination_offset);
    mir_stream_puts("\tpush de\n\tpop iy\n", out);
    mir_stream_printf(out, "L%d:\n", loop);
    mir_machine_emit_symbol_call(out, plan->random_function);
    mir_stream_printf(out, "\tld de,%d\n", plan->modulus);
    mir_emit_runtime_call(out, "__mods");
    mir_stream_puts("\tld (iy+0),l\n\tld (iy+1),h\n\tinc iy\n\tinc iy\n"
          "\tpush iy\n\tpop hl\n", out);
    mir_machine_emit_global_address_de(
        out, plan->destination,
        plan->destination_offset + plan->count * 2);
    mir_stream_puts("\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", loop);
    mir_machine_emit_symbol_call(out, plan->finish_function);
    mir_stream_puts("\tpop iy\n;@dcc.reg free=iy\n\tret\n", out);
}

static int mir_match_global_byte_copy_state(
    struct MirGlobalByteCopyState *plan)
{
    static const int expected_opcodes[42] = {
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_PHI,
        MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT, MIR_LABEL,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_CONST, MIR_STORE
    };
    static const int state_constants[3] = { 26, 30, 40 };
    static const int state_stores[3] = { 28, 32, 41 };
    const struct MirInsn *index_phi = &mir.insns[5];
    const struct MirInsn *destination = &mir.insns[11];
    const struct MirInsn *source = &mir.insns[14];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;
    int state;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 42 || mir_cfg_block_count() != 4 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return mir_machine_reject(
            "global-byte-copy-state", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "global-byte-copy-state", "opcode");
    if (!mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[3]) ||
        index_phi->src1 != mir.insns[2].dst ||
        index_phi->src2 != mir.insns[22].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[19].label ||
        mir.insns[7].immediate <= 0 ||
        mir.insns[7].immediate > 255 ||
        mir.insns[8].immediate != 0 ||
        mir.insns[8].src1 != index_phi->dst ||
        mir.insns[9].immediate != '<' ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].src2 != mir.insns[7].dst ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[10].label != mir.insns[25].label)
        return mir_machine_reject(
            "global-byte-copy-state", "loop");
    plan->count = (int)mir.insns[7].immediate;
    if (!mir_scalar_memory_location(
            destination, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        mir.insns[13].src1 != destination->dst ||
        mir.insns[13].src2 != index_phi->dst ||
        mir.insns[13].immediate != 1 ||
        mir.insns[13].memory_size != 1)
        return mir_machine_reject(
            "global-byte-copy-state", "destination");
    plan->destination = find_global(destination->name);
    plan->destination_offset = memory_offset;
    if (!mir_scalar_memory_location(
            source, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        mir.insns[16].src1 != source->dst ||
        mir.insns[16].src2 != index_phi->dst ||
        mir.insns[16].immediate != 1 ||
        mir.insns[16].memory_size != 1 ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        mir.insns[17].memory_size != 1 ||
        (mir.insns[17].memory_flags & (1 | 8)) != 0 ||
        mir.insns[18].src1 != mir.insns[13].dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        mir.insns[18].memory_size != 1 ||
        (mir.insns[18].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject(
            "global-byte-copy-state", "source");
    plan->source = find_global(source->name);
    plan->source_offset = memory_offset;
    if (plan->source == NULL || plan->destination == NULL ||
        plan->source->is_volatile || plan->destination->is_volatile ||
        plan->source == plan->destination)
        return mir_machine_reject(
            "global-byte-copy-state", "copy-symbols");
    if (!mir_machine_constant_equals(mir.insns[21].dst, 1) ||
        mir.insns[22].immediate != '+' ||
        mir.insns[22].src1 != index_phi->dst ||
        mir.insns[22].src2 != mir.insns[21].dst ||
        mir.insns[23].object != mir.insns[3].object ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[24].label != mir.insns[4].label)
        return mir_machine_reject(
            "global-byte-copy-state", "increment");
    for (state = 0; state < 3; ++state) {
        const struct MirInsn *store =
            &mir.insns[state_stores[state]];
        long value;

        if (!mir_machine_constant_value(
                mir.insns[state_constants[state]].dst, &value, 0) ||
            !mir_machine_named_nonvolatile(store) ||
            !mir_scalar_memory_location(
                store, &memory_type, &memory_storage, &memory_offset) ||
            memory_storage != SC_GLOBAL ||
            (type_size(memory_type) != 1 &&
             type_size(memory_type) != 2) ||
            store->src1 !=
                mir.insns[state_constants[state]].dst)
            return mir_machine_reject(
                "global-byte-copy-state", "state");
        plan->state[state] = find_global(store->name);
        plan->state_offsets[state] = memory_offset;
        plan->state_widths[state] = type_size(memory_type);
        plan->state_values[state] =
            (int)((unsigned long)value &
                  (plan->state_widths[state] == 1
                       ? 0xffUL : 0xffffUL));
        if (plan->state[state] == NULL ||
            plan->state[state]->is_volatile)
            return mir_machine_reject(
                "global-byte-copy-state", "state-symbol");
    }
    return 1;
}

static void mir_emit_global_byte_copy_state(
    MirStream *out, const struct MirGlobalByteCopyState *plan)
{
    int state;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_address_de(
        out, plan->source, plan->source_offset);
    mir_stream_puts("\tex de,hl\n", out);
    mir_machine_emit_global_address_de(
        out, plan->destination, plan->destination_offset);
    mir_stream_printf(out, "\tld bc,%d\n\tldir\n", plan->count);
    for (state = 0; state < 3; ++state) {
        if (plan->state_widths[state] == 1) {
            mir_stream_printf(out, "\tld a,%d\n", plan->state_values[state]);
            mir_machine_emit_global_byte_a(
                out, plan->state[state],
                plan->state_offsets[state], 1);
        } else {
            mir_stream_printf(out, "\tld hl,%d\n", plan->state_values[state]);
            mir_machine_emit_global_word_store(
                out, plan->state[state],
                plan->state_offsets[state]);
        }
    }
    mir_stream_puts("\tret\n", out);
}

static int mir_match_fixed_global_stride_call(
    struct MirFixedGlobalStrideCall *plan)
{
    static const int expected_opcodes[35] = {
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_PHI,
        MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_NOP, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_INDEX_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_NOP,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_INDEX_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_JUMP, MIR_LABEL
    };
    const struct MirInsn *index_phi = &mir.insns[5];
    const struct MirInsn *fixed = &mir.insns[11];
    const struct MirInsn *first = &mir.insns[13];
    const struct MirInsn *second = &mir.insns[20];
    const struct MirInsn *call = &mir.insns[27];
    int arguments[3];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 35 || mir_cfg_block_count() != 4 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return mir_machine_reject(
            "fixed-global-stride-call", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "fixed-global-stride-call", "opcode");
    if (!mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[3]) ||
        index_phi->src1 != mir.insns[2].dst ||
        index_phi->src2 != mir.insns[31].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[28].label ||
        mir.insns[7].immediate <= 0 ||
        mir.insns[7].immediate > 255 ||
        mir.insns[8].immediate != 0 ||
        mir.insns[8].src1 != index_phi->dst ||
        mir.insns[9].immediate != '<' ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].src2 != mir.insns[7].dst ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[10].label != mir.insns[34].label)
        return mir_machine_reject(
            "fixed-global-stride-call", "loop");
    plan->count = (int)mir.insns[7].immediate;
    if (!mir_scalar_memory_location(
            fixed, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL)
        return mir_machine_reject(
            "fixed-global-stride-call", "fixed");
    plan->fixed = find_global(fixed->name);
    plan->fixed_offset = memory_offset;
    if (!mir_scalar_memory_location(
            first, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        mir.insns[16].immediate != 0 ||
        mir.insns[16].src1 != index_phi->dst ||
        mir.insns[17].immediate != '*' ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        mir.insns[17].src2 != mir.insns[15].dst ||
        mir.insns[18].src1 != first->dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        mir.insns[18].immediate <= 0 ||
        mir.insns[18].memory_size != 2)
        return mir_machine_reject(
            "fixed-global-stride-call", "first");
    plan->first = find_global(first->name);
    plan->first_offset = memory_offset;
    plan->first_stride =
        (int)(mir.insns[15].immediate * mir.insns[18].immediate);
    if (!mir_scalar_memory_location(
            second, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        mir.insns[23].immediate != 0 ||
        mir.insns[23].src1 != index_phi->dst ||
        mir.insns[24].immediate != '*' ||
        mir.insns[24].src1 != mir.insns[23].dst ||
        mir.insns[24].src2 != mir.insns[22].dst ||
        mir.insns[25].src1 != second->dst ||
        mir.insns[25].src2 != mir.insns[24].dst ||
        mir.insns[25].immediate <= 0 ||
        mir.insns[25].memory_size != 2)
        return mir_machine_reject(
            "fixed-global-stride-call", "second");
    plan->second = find_global(second->name);
    plan->second_offset = memory_offset;
    plan->second_stride =
        (int)(mir.insns[22].immediate * mir.insns[25].immediate);
    if (plan->fixed == NULL || plan->first == NULL ||
        plan->second == NULL ||
        plan->fixed->is_volatile || plan->first->is_volatile ||
        plan->second->is_volatile ||
        plan->first_stride <= 0 || plan->first_stride > 32767 ||
        plan->second_stride <= 0 || plan->second_stride > 32767 ||
        !mir_machine_three_call_arguments(call, arguments) ||
        arguments[0] != fixed->dst ||
        arguments[1] != mir.insns[18].dst ||
        arguments[2] != mir.insns[25].dst)
        return mir_machine_reject(
            "fixed-global-stride-call", "arguments");
    plan->function = find_global(call->name);
    if (plan->function == NULL || !plan->function->is_defined ||
        plan->function->is_funcptr || plan->function->is_noreturn ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0)
        return mir_machine_reject(
            "fixed-global-stride-call", "function");
    if (!mir_machine_constant_equals(mir.insns[30].dst, 1) ||
        mir.insns[31].immediate != '+' ||
        mir.insns[31].src1 != index_phi->dst ||
        mir.insns[31].src2 != mir.insns[30].dst ||
        mir.insns[32].object != mir.insns[3].object ||
        mir.insns[32].src1 != mir.insns[31].dst ||
        mir.insns[33].label != mir.insns[4].label)
        return mir_machine_reject(
            "fixed-global-stride-call", "increment");
    return 1;
}

static void mir_emit_fixed_global_stride_call(
    MirStream *out, const struct MirFixedGlobalStrideCall *plan)
{
    int loop = new_label();

    mir_stream_printf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\tld iy,0\n", out);
    mir_stream_printf(out, "L%d:\n", loop);
    mir_emit_stride_global_argument(
        out, plan->second, plan->second_offset,
        plan->second_stride);
    mir_emit_stride_global_argument(
        out, plan->first, plan->first_offset,
        plan->first_stride);
    mir_machine_emit_global_address_de(
        out, plan->fixed, plan->fixed_offset);
    mir_stream_puts("\tpush de\n", out);
    mir_machine_emit_symbol_call(out, plan->function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tinc iy\n"
          "\tpush iy\n\tpop hl\n\tld a,l\n", out);
    mir_stream_printf(out, "\tcp %d\n\tjp nz,L%d\n"
                 "\tpop iy\n;@dcc.reg free=iy\n\tret\n",
            plan->count, loop);
}

static int mir_match_fixed_prediction_count(
    struct MirFixedPredictionLoop *plan)
{
    static const int expected_opcodes[51] = {
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_PHI,
        MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_ADDRESS, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_INDEX_ADDRESS, MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG,
        MIR_ADDRESS, MIR_NOP, MIR_ARG, MIR_CALL, MIR_LOAD, MIR_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_NOP,
        MIR_STORE, MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL
    };
    const struct MirInsn *index_phi = &mir.insns[5];
    const struct MirInsn *call = &mir.insns[24];
    int arguments[3];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 51 || mir_cfg_block_count() != 5 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return mir_machine_reject("fixed-prediction-count", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "fixed-prediction-count", "opcode");
    if (!mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[3]) ||
        index_phi->src1 != mir.insns[2].dst ||
        index_phi->src2 != mir.insns[47].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[44].label ||
        mir.insns[7].immediate <= 0 ||
        mir.insns[7].immediate > 255 ||
        mir.insns[8].src1 != index_phi->dst ||
        mir.insns[9].immediate != '<' ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].src2 != mir.insns[7].dst ||
        mir.insns[10].label != mir.insns[50].label)
        return mir_machine_reject("fixed-prediction-count", "loop");
    plan->count = (int)mir.insns[7].immediate;
    plan->columns = (int)mir.insns[13].immediate;
    if (plan->columns <= 0 || plan->columns > 255 ||
        !mir_scalar_memory_location(
            &mir.insns[11], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        mir.insns[15].immediate != '*' ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        mir.insns[15].src2 != mir.insns[13].dst ||
        mir.insns[16].src1 != mir.insns[11].dst ||
        mir.insns[16].src2 != mir.insns[15].dst ||
        mir.insns[16].immediate != 2)
        return mir_machine_reject(
            "fixed-prediction-count", "logits");
    plan->logits = find_global(mir.insns[11].name);
    plan->logits_offset = memory_offset;
    if (!mir_machine_three_call_arguments(call, arguments) ||
        arguments[0] != mir.insns[16].dst ||
        arguments[1] != mir.insns[19].dst ||
        arguments[2] != mir.insns[21].dst ||
        mir.insns[19].immediate != plan->columns)
        return mir_machine_reject(
            "fixed-prediction-count", "call");
    plan->maximum_function = find_global(call->name);
    if (plan->maximum_function == NULL ||
        !plan->maximum_function->is_defined ||
        plan->maximum_function->is_funcptr ||
        !mir_scalar_memory_location(
            &mir.insns[26], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        mir.insns[28].src1 != mir.insns[26].dst ||
        mir.insns[28].src2 != index_phi->dst ||
        mir.insns[28].immediate != 2 ||
        mir.insns[29].src1 != mir.insns[28].dst ||
        mir.insns[30].immediate != TOK_EQ ||
        mir.insns[30].src1 != mir.insns[25].dst ||
        mir.insns[30].src2 != mir.insns[29].dst ||
        mir.insns[31].label != mir.insns[37].label)
        return mir_machine_reject(
            "fixed-prediction-count", "target");
    plan->targets = find_global(mir.insns[26].name);
    plan->targets_offset = memory_offset;
    if (!mir_scalar_memory_location(
            &mir.insns[32], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        !mir_machine_same_location(&mir.insns[32], &mir.insns[36]) ||
        mir.insns[34].immediate != '+' ||
        mir.insns[34].src1 != mir.insns[32].dst ||
        !mir_machine_constant_equals(mir.insns[34].src2, 1))
        return mir_machine_reject("fixed-prediction-count", "hits");
    plan->hits = find_global(mir.insns[32].name);
    plan->hits_offset = memory_offset;
    if (!mir_scalar_memory_location(
            &mir.insns[38], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        !mir_machine_same_location(&mir.insns[38], &mir.insns[42]) ||
        mir.insns[40].immediate != '+' ||
        mir.insns[40].src1 != mir.insns[38].dst ||
        !mir_machine_constant_equals(mir.insns[40].src2, 1) ||
        !mir_machine_constant_equals(mir.insns[46].dst, 1) ||
        mir.insns[47].immediate != '+' ||
        mir.insns[47].src1 != index_phi->dst ||
        mir.insns[49].label != mir.insns[4].label)
        return mir_machine_reject("fixed-prediction-count", "total");
    plan->total = find_global(mir.insns[38].name);
    plan->total_offset = memory_offset;
    if (plan->logits == NULL || plan->targets == NULL ||
        plan->hits == NULL || plan->total == NULL ||
        plan->logits->is_volatile || plan->targets->is_volatile ||
        plan->hits->is_volatile || plan->total->is_volatile)
        return mir_machine_reject("fixed-prediction-count", "symbols");
    return 1;
}

static int mir_match_fixed_prediction_check(
    struct MirFixedPredictionLoop *plan)
{
    static const int expected_opcodes[51] = {
        MIR_LABEL, MIR_CALL, MIR_NOP, MIR_CONST, MIR_STORE, MIR_NOP,
        MIR_CONST, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_PHI, MIR_NOP,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS,
        MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_INDEX_ADDRESS,
        MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG, MIR_ADDRESS, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_LOAD, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST,
        MIR_STORE, MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_RETURN
    };
    const struct MirInsn *index_phi = &mir.insns[10];
    const struct MirInsn *call = &mir.insns[29];
    int arguments[3];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 51 || mir_cfg_block_count() != 5 ||
        mir.has_vla || !type_is_bool(mir.return_type))
        return mir_machine_reject("fixed-prediction-check", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "fixed-prediction-check", "opcode");
    if (!mir_machine_call_has_no_arguments(&mir.insns[1]) ||
        !mir_machine_constant_equals(mir.insns[3].dst, 1) ||
        !mir_machine_unobservable_local_store(&mir.insns[4]) ||
        !mir_machine_constant_equals(mir.insns[6].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[7]) ||
        index_phi->src1 != mir.insns[6].dst ||
        index_phi->src2 != mir.insns[45].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[42].label ||
        mir.insns[12].immediate <= 0 ||
        mir.insns[14].immediate != '<' ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        mir.insns[14].src2 != mir.insns[12].dst ||
        mir.insns[15].label != mir.insns[48].label)
        return mir_machine_reject(
            "fixed-prediction-check", "loop");
    plan->count = (int)mir.insns[12].immediate;
    plan->columns = (int)mir.insns[18].immediate;
    plan->returns_bool = 1;
    plan->prefix_function = find_global(mir.insns[1].name);
    if (plan->count <= 0 || plan->count > 255 ||
        plan->columns <= 0 || plan->columns > 255 ||
        plan->prefix_function == NULL ||
        !plan->prefix_function->is_defined ||
        !mir_scalar_memory_location(
            &mir.insns[16], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        mir.insns[20].immediate != '*' ||
        mir.insns[21].src1 != mir.insns[16].dst ||
        mir.insns[21].src2 != mir.insns[20].dst ||
        mir.insns[21].immediate != 2)
        return mir_machine_reject(
            "fixed-prediction-check", "logits");
    plan->logits = find_global(mir.insns[16].name);
    plan->logits_offset = memory_offset;
    if (!mir_machine_three_call_arguments(call, arguments) ||
        arguments[0] != mir.insns[21].dst ||
        arguments[1] != mir.insns[24].dst ||
        arguments[2] != mir.insns[26].dst ||
        mir.insns[24].immediate != plan->columns)
        return mir_machine_reject(
            "fixed-prediction-check", "call");
    plan->maximum_function = find_global(call->name);
    if (plan->maximum_function == NULL ||
        !plan->maximum_function->is_defined ||
        !mir_scalar_memory_location(
            &mir.insns[31], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        mir.insns[33].src1 != mir.insns[31].dst ||
        mir.insns[33].src2 != index_phi->dst ||
        mir.insns[33].immediate != 2 ||
        mir.insns[34].src1 != mir.insns[33].dst ||
        mir.insns[35].immediate != TOK_NE ||
        mir.insns[35].src1 != mir.insns[30].dst ||
        mir.insns[35].src2 != mir.insns[34].dst ||
        mir.insns[36].label != mir.insns[40].label)
        return mir_machine_reject(
            "fixed-prediction-check", "target");
    plan->targets = find_global(mir.insns[31].name);
    plan->targets_offset = memory_offset;
    if (!mir_machine_constant_equals(mir.insns[38].dst, 0) ||
        mir.insns[39].object != mir.insns[4].object ||
        !mir_machine_constant_equals(mir.insns[44].dst, 1) ||
        mir.insns[45].immediate != '+' ||
        mir.insns[45].src1 != index_phi->dst ||
        mir.insns[47].label != mir.insns[8].label ||
        mir.insns[49].object != mir.insns[4].object ||
        mir.insns[50].src1 != mir.insns[49].dst ||
        plan->logits == NULL || plan->targets == NULL ||
        plan->logits->is_volatile || plan->targets->is_volatile)
        return mir_machine_reject(
            "fixed-prediction-check", "result");
    return 1;
}

static void mir_emit_fixed_prediction_count(
    MirStream *out, const struct MirFixedPredictionLoop *plan)
{
    int after_update = new_label();
    int different = new_label();
    int loop = new_label();

    mir_stream_printf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tdec sp\n\tdec sp\n",
            mir.name);
    if (plan->returns_bool)
        mir_stream_puts("\tdec sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    if (plan->prefix_function != NULL)
        mir_machine_emit_symbol_call(out, plan->prefix_function);
    if (plan->returns_bool)
        mir_stream_puts("\tld (ix-3),1\n", out);
    mir_stream_puts("\tld iy,0\n", out);
    mir_stream_printf(out, "L%d:\n"
                 "\tpush ix\n\tpop hl\n\tdec hl\n\tdec hl\n\tpush hl\n"
                 "\tld hl,%d\n\tpush hl\n",
            loop, plan->columns);
    mir_emit_stride_global_argument(
        out, plan->logits, plan->logits_offset,
        plan->columns * 2);
    mir_machine_emit_symbol_call(out, plan->maximum_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tld c,(ix-2)\n\tld b,(ix-1)\n"
          "\tpush iy\n\tpop hl\n\tadd hl,hl\n", out);
    mir_machine_emit_global_address_de(
        out, plan->targets, plan->targets_offset);
    mir_stream_puts("\tadd hl,de\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld a,b\n\tcp d\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n\tld a,c\n\tcp e\n\tjp nz,L%d\n",
            different, different);
    if (plan->returns_bool) {
        mir_stream_printf(out, "\tjp L%d\n", after_update);
    } else {
        mir_machine_emit_global_word(out, plan->hits, plan->hits_offset);
        mir_stream_puts("\tinc hl\n", out);
        mir_machine_emit_global_word_store(
            out, plan->hits, plan->hits_offset);
    }
    mir_stream_printf(out, "L%d:\n", different);
    if (plan->returns_bool)
        mir_stream_puts("\tld (ix-3),0\n", out);
    else {
        mir_machine_emit_global_word(out, plan->total, plan->total_offset);
        mir_stream_puts("\tinc hl\n", out);
        mir_machine_emit_global_word_store(
            out, plan->total, plan->total_offset);
    }
    if (plan->returns_bool)
        mir_stream_printf(out, "L%d:\n", after_update);
    mir_stream_puts("\tinc iy\n\tpush iy\n\tpop hl\n\tld a,l\n", out);
    mir_stream_printf(out, "\tcp %d\n\tjp nz,L%d\n", plan->count, loop);
    if (plan->returns_bool)
        mir_stream_puts("\tld l,(ix-3)\n\tld h,0\n", out);
    mir_stream_puts("\tld sp,ix\n\tpop ix\n\tpop iy\n"
          ";@dcc.reg free=iy\n\tret\n", out);
}

static int mir_match_random_wide_fill(
    struct MirRandomWideFill *plan)
{
    static const int expected_opcodes[37] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP, MIR_NOP, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_CALL, MIR_CONST, MIR_BINARY, MIR_CONST,
        MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_NOP, MIR_UNARY, MIR_CONST, MIR_BINARY, MIR_STORE_INDIRECT,
        MIR_NOP, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_JUMP, MIR_LABEL
    };
    const struct MirInsn *pointer = &mir.insns[1];
    const struct MirInsn *count = &mir.insns[2];
    const struct MirInsn *index_phi = &mir.insns[9];
    const struct MirInsn *call = &mir.insns[14];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 37 || mir_cfg_block_count() != 4 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return mir_machine_reject("random-wide-fill", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject("random-wide-fill", "opcode");
    if (type_ptr_depth(pointer->type) != 1 ||
        type_size(count->type) != 2 ||
        (count->type & TYPE_UNSIGNED) != 0 ||
        !mir_machine_parameter_value_offset(
            pointer->dst, &plan->pointer_stack_offset) ||
        !mir_machine_parameter_value_offset(
            count->dst, &plan->count_stack_offset) ||
        !mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[5]) ||
        index_phi->src1 != mir.insns[3].dst ||
        index_phi->src2 != mir.insns[33].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[30].label ||
        mir.insns[12].immediate != '<' ||
        mir.insns[12].src1 != index_phi->dst ||
        mir.insns[12].src2 != count->dst ||
        mir.insns[13].label != mir.insns[36].label)
        return mir_machine_reject("random-wide-fill", "loop");
    if (!mir_machine_call_has_no_arguments(call) ||
        mir.insns[15].immediate != 255 ||
        mir.insns[16].immediate != '&' ||
        mir.insns[16].src1 != call->dst ||
        mir.insns[16].src2 != mir.insns[15].dst ||
        mir.insns[17].immediate != 128 ||
        mir.insns[18].immediate != '-' ||
        mir.insns[18].src1 != mir.insns[16].dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        mir.insns[23].src1 != pointer->dst ||
        mir.insns[23].src2 != index_phi->dst ||
        mir.insns[23].immediate != 4 ||
        mir.insns[25].immediate != 0 ||
        mir.insns[25].src1 != mir.insns[18].dst ||
        mir.insns[26].immediate != 256 ||
        mir.insns[27].immediate != '*' ||
        mir.insns[27].src1 != mir.insns[25].dst ||
        mir.insns[27].src2 != mir.insns[26].dst ||
        mir.insns[28].src1 != mir.insns[23].dst ||
        mir.insns[28].src2 != mir.insns[27].dst ||
        mir.insns[28].memory_size != 4)
        return mir_machine_reject("random-wide-fill", "body");
    plan->function = find_global(call->name);
    if (plan->function == NULL || !plan->function->is_defined ||
        plan->function->is_funcptr ||
        !mir_machine_constant_equals(mir.insns[32].dst, 1) ||
        mir.insns[33].immediate != '+' ||
        mir.insns[33].src1 != index_phi->dst ||
        mir.insns[35].label != mir.insns[6].label)
        return mir_machine_reject("random-wide-fill", "result");
    return 1;
}

static void mir_emit_random_wide_fill(
    MirStream *out, const struct MirRandomWideFill *plan)
{
    int done = new_label();
    int loop = new_label();

    mir_stream_printf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tdec sp\n\tdec sp\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld c,(ix%+d)\n\tld b,(ix%+d)\n"
            "\tbit 7,b\n\tjp nz,L%d\n"
            "\tld a,b\n\tor c\n\tjp z,L%d\n"
            "\tpush hl\n\tld h,b\n\tld l,c\n"
            "\tadd hl,hl\n\tadd hl,hl\n"
            "\tpop de\n\tadd hl,de\n"
            "\tld (ix-2),l\n\tld (ix-1),h\n"
            "\tpush de\n\tpop iy\n"
            "L%d:\n",
            plan->pointer_stack_offset + 4,
            plan->pointer_stack_offset + 5,
            plan->count_stack_offset + 4,
            plan->count_stack_offset + 5,
            done, done, loop);
    mir_machine_emit_symbol_call(out, plan->function);
    mir_stream_puts("\tld h,0\n\tld a,l\n\tsub 128\n"
          "\tld l,0\n\tld h,a\n\trlca\n\tsbc a,a\n"
          "\tld e,a\n\tld d,a\n"
          "\tld (iy+0),l\n\tld (iy+1),h\n"
          "\tld (iy+2),e\n\tld (iy+3),d\n"
          "\tinc iy\n\tinc iy\n\tinc iy\n\tinc iy\n"
          "\tpush iy\n\tpop hl\n"
          "\tld c,(ix-2)\n\tld b,(ix-1)\n"
          "\tor a\n\tsbc hl,bc\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\nL%d:\n"
                 "\tld sp,ix\n\tpop ix\n\tpop iy\n"
                 ";@dcc.reg free=iy\n\tret\n",
            loop, done);
}

static int mir_match_fixed_byte_board_call(
    struct MirFixedByteBoardCall *plan)
{
    static const int expected_opcodes[46] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_PHI, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_NOP, MIR_CONST, MIR_ARG, MIR_NOP,
        MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG, MIR_NOP,
        MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN
    };
    const struct MirInsn *position = &mir.insns[1];
    int call_arguments[4];
    int memory_type, memory_storage, memory_offset;
    int argument, instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 46 || mir_cfg_block_count() != 4 ||
        mir.has_vla || type_size(mir.return_type) != 2 ||
        type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(position->type) != 1 ||
        type_ptr_depth(position->type) != 0)
        return mir_machine_reject("fixed-byte-board-call", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "fixed-byte-board-call", "opcode");
    if (!mir_machine_parameter_value_offset(
            position->dst, &plan->position_stack_offset) ||
        !mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[4]) ||
        mir.insns[4].memory_size != 1 ||
        mir.insns[7].src1 != mir.insns[3].dst ||
        mir.insns[7].src2 != mir.insns[22].dst ||
        mir.insns[7].phi_pred1 != mir.insns[0].label ||
        mir.insns[7].phi_pred2 != mir.insns[19].label ||
        mir.insns[9].immediate <= 0 ||
        mir.insns[9].immediate > 255 ||
        mir.insns[10].immediate != 0 ||
        mir.insns[10].src1 != mir.insns[7].dst ||
        mir.insns[11].immediate != '<' ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[11].src2 != mir.insns[9].dst ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[12].label != mir.insns[25].label ||
        mir.insns[15].src2 != mir.insns[7].dst ||
        mir.insns[15].immediate != 1 ||
        mir.insns[18].src1 != mir.insns[15].dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        mir.insns[18].memory_size != 1 ||
        !mir_machine_constant_equals(mir.insns[21].dst, 1) ||
        mir.insns[22].immediate != '+' ||
        mir.insns[22].src1 != mir.insns[7].dst ||
        mir.insns[22].src2 != mir.insns[21].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[23]) ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[24].label != mir.insns[5].label)
        return mir_machine_reject("fixed-byte-board-call", "loop");
    if (!mir_scalar_memory_location(
            &mir.insns[13], &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        mir.insns[15].src1 != mir.insns[13].dst ||
        type_size(memory_type) != 1)
        return mir_machine_reject("fixed-byte-board-call", "board");
    plan->count = (int)mir.insns[9].immediate;
    plan->clear_value = (int)mir.insns[17].immediate & 0xff;
    plan->board = find_global(mir.insns[13].name);
    plan->board_offset = memory_offset;
    if (plan->board == NULL || plan->board->is_volatile ||
        mir.insns[26].opcode != MIR_ADDRESS ||
        strcmp(mir.insns[26].name, mir.insns[13].name) ||
        mir.insns[28].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[28].src1 != mir.insns[26].dst ||
        mir.insns[28].src2 != position->dst ||
        mir.insns[30].opcode != MIR_CONST ||
        mir.insns[31].opcode != MIR_STORE_INDIRECT ||
        mir.insns[31].src1 != mir.insns[28].dst ||
        mir.insns[31].src2 != mir.insns[30].dst)
        return mir_machine_reject("fixed-byte-board-call", "selected");
    plan->selected_value = (int)mir.insns[30].immediate & 0xff;
    if (mir.insns[43].opcode != MIR_CALL ||
        !mir_machine_four_call_arguments(
            &mir.insns[43], call_arguments))
        return mir_machine_reject("fixed-byte-board-call", "call");
    for (argument = 0; argument < 3; ++argument) {
        long value;
        if (!mir_machine_constant_value(
                call_arguments[argument], &value, 0))
            return mir_machine_reject(
                "fixed-byte-board-call", "call-constant");
        plan->arguments[argument] = (int)value & 0xffff;
    }
    if (call_arguments[3] != position->dst ||
        !mir_machine_constant_equals(mir.insns[44].dst, 0) ||
        mir.insns[45].opcode != MIR_RETURN ||
        mir.insns[45].src1 != mir.insns[44].dst)
        return mir_machine_reject("fixed-byte-board-call", "return");
    plan->function = find_global(mir.insns[43].name);
    if (plan->function == NULL || !plan->function->is_defined ||
        plan->function->is_funcptr)
        return mir_machine_reject("fixed-byte-board-call", "function");
    return 1;
}

static void mir_emit_fixed_byte_board_call(
    MirStream *out, const struct MirFixedByteBoardCall *plan)
{
    int loop = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_address_de(
        out, plan->board, plan->board_offset);
    mir_stream_printf(out,
            "\tex de,hl\n\tld a,%d\n\tld b,%d\n"
            "L%d:\n\tld (hl),a\n\tinc hl\n\tdjnz L%d\n"
            "\tld hl,%d\n\tadd hl,sp\n\tld c,(hl)\n\tld b,0\n",
            plan->clear_value, plan->count, loop, loop,
            plan->position_stack_offset);
    mir_machine_emit_global_address_de(
        out, plan->board, plan->board_offset);
    mir_stream_puts("\tld h,b\n\tld l,c\n\tadd hl,de\n", out);
    mir_stream_printf(out, "\tld (hl),%d\n\tpush bc\n",
            plan->selected_value);
    mir_stream_printf(out,
            "\tld hl,%d\n\tpush hl\n"
            "\tld hl,%d\n\tpush hl\n"
            "\tld hl,%d\n\tpush hl\n",
            plan->arguments[2],
            plan->arguments[1], plan->arguments[0]);
    mir_machine_emit_symbol_call(out, plan->function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tld hl,0\n\tret\n", out);
}

static int mir_match_constant_loop_check(
    struct MirConstantLoopCheck *plan)
{
    static const int expected_opcodes[35] = {
        MIR_LABEL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST,
        MIR_STORE, MIR_LABEL, MIR_PHI, MIR_PHI, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP,
        MIR_UNARY, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL
    };
    const struct MirInsn *sum_phi;
    const struct MirInsn *index_phi;
    int arguments[2];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 35 || mir_cfg_block_count() != 4 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    sum_phi = &mir.insns[8];
    index_phi = &mir.insns[9];
    if (!mir_machine_constant_equals(mir.insns[1].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[3]) ||
        mir.insns[3].memory_size != 2 ||
        !mir_machine_constant_equals(mir.insns[5].dst, 1) ||
        !mir_machine_unobservable_local_store(&mir.insns[6]) ||
        mir.insns[6].memory_size != 1)
        return 0;
    if (sum_phi->src1 != mir.insns[1].dst ||
        sum_phi->src2 != mir.insns[18].dst ||
        sum_phi->phi_pred1 != mir.insns[0].label ||
        sum_phi->phi_pred2 != mir.insns[21].label ||
        type_ptr_depth(sum_phi->type) != 0 ||
        (sum_phi->type & 15) != TYPE_INT ||
        type_size(sum_phi->type) != 2 ||
        index_phi->src1 != mir.insns[5].dst ||
        index_phi->src2 != mir.insns[24].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[21].label ||
        (index_phi->type & TYPE_UNSIGNED) == 0 ||
        type_size(index_phi->type) != 1)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[11].dst, 10) ||
        mir.insns[12].immediate != 0 ||
        mir.insns[12].src1 != index_phi->dst ||
        mir.insns[13].immediate != TOK_LE ||
        mir.insns[13].src1 != mir.insns[12].dst ||
        mir.insns[13].src2 != mir.insns[11].dst ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        mir.insns[14].label != mir.insns[27].label)
        return 0;
    if (mir.insns[17].immediate != 0 ||
        mir.insns[17].src1 != index_phi->dst ||
        mir.insns[18].immediate != '+' ||
        mir.insns[18].src1 != sum_phi->dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        !mir_machine_same_location(&mir.insns[3], &mir.insns[20]) ||
        mir.insns[20].src1 != mir.insns[18].dst ||
        !mir_machine_constant_equals(mir.insns[23].dst, 1) ||
        mir.insns[24].immediate != '+' ||
        mir.insns[24].src1 != index_phi->dst ||
        mir.insns[24].src2 != mir.insns[23].dst ||
        !mir_machine_same_location(&mir.insns[6], &mir.insns[25]) ||
        mir.insns[25].src1 != mir.insns[24].dst ||
        mir.insns[26].label != mir.insns[7].label)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[29].dst, 55) ||
        mir.insns[30].immediate != TOK_EQ ||
        mir.insns[30].src1 != sum_phi->dst ||
        mir.insns[30].src2 != mir.insns[29].dst ||
        mir.insns[31].src1 != mir.insns[30].dst ||
        mir.insns[33].src1 != mir.insns[32].dst ||
        !mir_machine_two_call_arguments(&mir.insns[34], arguments) ||
        arguments[0] != mir.insns[30].dst ||
        arguments[1] != mir.insns[32].dst)
        return 0;
    plan->function = find_global(mir.insns[34].name);
    if (plan->function == NULL || !plan->function->is_defined ||
        plan->function->storage != SC_FUNC ||
        plan->function->is_funcptr ||
        plan->function->is_noreturn ||
        !plan->function->has_proto ||
        plan->function->proto_nargs != 2 ||
        plan->function->proto_variadic ||
        plan->function->proto_types[0] != mir.insns[31].type ||
        plan->function->proto_types[1] != mir.insns[33].type ||
        mir.insns[34].memory_flags != 0)
        return 0;
    plan->string_id = (int)mir.insns[32].immediate;
    return 1;
}

static void mir_emit_constant_loop_check(
    MirStream *out, const struct MirConstantLoopCheck *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n"
                 "\tld hl,1\n\tpush hl\n",
            plan->string_id);
    mir_machine_emit_symbol_call(out, plan->function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tret\n", out);
}

static int mir_match_global_byte_countdown(
    struct MirGlobalByteCountdown *plan)
{
    static const int expected_opcodes[36] = {
        MIR_LABEL, MIR_PARAM, MIR_LOAD, MIR_LOAD, MIR_BINARY, MIR_LOAD,
        MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_PHI, MIR_PHI, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_LOAD, MIR_BINARY, MIR_LOAD, MIR_BINARY, MIR_LOAD, MIR_BINARY,
        MIR_RETURN
    };
    const struct MirInsn *parameter;
    const struct MirInsn *sum_phi;
    const struct MirInsn *count_phi;
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
    parameter = &mir.insns[1];
    sum_phi = &mir.insns[14];
    count_phi = &mir.insns[15];
    if (type_ptr_depth(parameter->type) != 0 ||
        (parameter->type & 15) != TYPE_CHAR ||
        (parameter->type & TYPE_UNSIGNED) == 0 ||
        !mir_machine_parameter_value_offset(
            parameter->dst, &plan->parameter_stack_offset) ||
        !mir_machine_named_nonvolatile(&mir.insns[2]) ||
        type_ptr_depth(mir.insns[2].type) != 0 ||
        (mir.insns[2].type & 15) != TYPE_INT ||
        type_size(mir.insns[2].type) != 2 ||
        !mir_machine_same_location(&mir.insns[2], &mir.insns[3]) ||
        !mir_machine_same_location(&mir.insns[2], &mir.insns[5]))
        return 0;
    if (mir.insns[4].immediate != '+' ||
        mir.insns[4].src1 != mir.insns[2].dst ||
        mir.insns[4].src2 != mir.insns[3].dst ||
        mir.insns[6].immediate != '+' ||
        mir.insns[6].src1 != mir.insns[4].dst ||
        mir.insns[6].src2 != mir.insns[5].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[8]) ||
        mir.insns[8].src1 != mir.insns[6].dst ||
        mir.insns[8].memory_size != 2 ||
        !mir_machine_unobservable_local_store(&mir.insns[11]) ||
        mir.insns[11].src1 != parameter->dst ||
        mir.insns[11].memory_size != 1)
        return 0;
    if (sum_phi->src1 != mir.insns[6].dst ||
        sum_phi->src2 != mir.insns[23].dst ||
        sum_phi->phi_pred1 != mir.insns[0].label ||
        sum_phi->phi_pred2 != mir.insns[25].label ||
        type_ptr_depth(sum_phi->type) != 0 ||
        (sum_phi->type & 15) != TYPE_INT ||
        type_size(sum_phi->type) != 2 ||
        count_phi->src1 != parameter->dst ||
        count_phi->src2 != mir.insns[18].dst ||
        count_phi->phi_pred1 != mir.insns[0].label ||
        count_phi->phi_pred2 != mir.insns[25].label ||
        (count_phi->type & TYPE_UNSIGNED) == 0 ||
        type_size(count_phi->type) != 1)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[17].dst, 1) ||
        mir.insns[18].immediate != '-' ||
        mir.insns[18].src1 != count_phi->dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        !mir_machine_same_location(&mir.insns[11], &mir.insns[19]) ||
        mir.insns[19].src1 != mir.insns[18].dst ||
        mir.insns[20].src1 != mir.insns[18].dst ||
        mir.insns[20].label != mir.insns[27].label ||
        !mir_machine_constant_equals(mir.insns[22].dst, 1) ||
        mir.insns[23].immediate != '+' ||
        mir.insns[23].src1 != sum_phi->dst ||
        mir.insns[23].src2 != mir.insns[22].dst ||
        !mir_machine_same_location(&mir.insns[8], &mir.insns[24]) ||
        mir.insns[24].src1 != mir.insns[23].dst ||
        mir.insns[26].label != mir.insns[12].label)
        return 0;
    if (!mir_machine_same_location(&mir.insns[2], &mir.insns[29]) ||
        mir.insns[30].immediate != '+' ||
        mir.insns[30].src1 != sum_phi->dst ||
        mir.insns[30].src2 != mir.insns[29].dst ||
        !mir_machine_same_location(&mir.insns[2], &mir.insns[31]) ||
        mir.insns[32].immediate != '+' ||
        mir.insns[32].src1 != mir.insns[30].dst ||
        mir.insns[32].src2 != mir.insns[31].dst ||
        !mir_machine_same_location(&mir.insns[2], &mir.insns[33]) ||
        mir.insns[34].immediate != '+' ||
        mir.insns[34].src1 != mir.insns[32].dst ||
        mir.insns[34].src2 != mir.insns[33].dst ||
        mir.insns[35].src1 != mir.insns[34].dst)
        return 0;
    plan->value = find_global(mir.insns[2].name);
    if (plan->value == NULL || !plan->value->is_defined ||
        plan->value->is_volatile)
        return 0;
    return 1;
}

static void mir_emit_global_byte_countdown(
    MirStream *out, const struct MirGlobalByteCountdown *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld a,(hl)\n"
            "\tdec a\n\tld c,a\n\tld b,0\n",
            plan->parameter_stack_offset);
    mir_machine_emit_global_word(out, plan->value, 0);
    mir_stream_puts("\tld d,h\n\tld e,l\n\tadd hl,hl\n"
          "\tadd hl,de\n\tadd hl,hl\n\tadd hl,bc\n\tret\n", out);
}

static int mir_match_conditional_string_report(
    struct MirConditionalStringReport *plan)
{
    static const int expected_opcodes[19] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_LOAD, MIR_ARG, MIR_NOP, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_LABEL,
        MIR_LABEL, MIR_PHI, MIR_ARG, MIR_CALL
    };
    int arguments[3];
    int memory_offset;
    int memory_storage;
    int memory_type;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 19 || mir_cfg_block_count() != 5 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (type_ptr_depth(mir.insns[1].type) != 1 ||
        (mir.insns[1].type & 15) != TYPE_CHAR ||
        type_ptr_depth(mir.insns[2].type) != 0 ||
        (mir.insns[2].type & 15) != TYPE_INT ||
        type_size(mir.insns[2].type) != 2 ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &plan->condition_stack_offset) ||
        !mir_scalar_memory_location(
            &mir.insns[5], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_PARAM ||
        type_ptr_depth(memory_type) != 1 ||
        (memory_type & 15) != TYPE_CHAR ||
        !mir_machine_same_location(&mir.insns[1], &mir.insns[5]))
        return 0;
    plan->name_stack_offset = memory_offset - 2;
    if (plan->name_stack_offset < 0 ||
        mir.insns[4].src1 != mir.insns[3].dst ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        mir.insns[8].src1 != mir.insns[2].dst ||
        mir.insns[8].label != mir.insns[12].label ||
        mir.insns[11].label != mir.insns[15].label ||
        mir.insns[16].src1 != mir.insns[9].dst ||
        mir.insns[16].src2 != mir.insns[13].dst ||
        mir.insns[16].phi_pred1 != mir.insns[10].label ||
        mir.insns[16].phi_pred2 != mir.insns[14].label ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[18], arguments) ||
        arguments[0] != mir.insns[3].dst ||
        arguments[1] != mir.insns[5].dst ||
        arguments[2] != mir.insns[16].dst)
        return 0;
    plan->function = find_global(mir.insns[18].name);
    if (plan->function == NULL ||
        strcmp(mir.insns[18].name, "printf") ||
        (mir.insns[18].memory_flags & MIR_CALL_FLAG_VARIADIC) == 0 ||
        (mir.insns[18].memory_flags &
         MIR_CALL_FLAG_FORMAT_RUNTIME) != 0)
        return 0;
    plan->format_string_id = (int)mir.insns[3].immediate;
    plan->true_string_id = (int)mir.insns[9].immediate;
    plan->false_string_id = (int)mir.insns[13].immediate;
    return 1;
}

static void mir_emit_conditional_string_report(
    MirStream *out, const struct MirConditionalStringReport *plan)
{
    int selected = new_label();
    int false_string = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n"
            "\tjp z,L%d\n\tld hl,S%d\n\tjp L%d\n"
            "L%d:\n\tld hl,S%d\n"
            "L%d:\n\tpush hl\n\tpush bc\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->name_stack_offset,
            plan->condition_stack_offset,
            false_string, plan->true_string_id, selected,
            false_string, plan->false_string_id,
            selected, plan->format_string_id);
    mir_machine_emit_symbol_call(out, plan->function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tret\n", out);
}

static int mir_match_byte_mismatch_reporter(
    struct MirByteMismatchReporter *plan)
{
    const struct MirInsn *print_call = &mir.insns[48];
    const struct MirInsn *exit_call = &mir.insns[51];
    int print_args[3];
    int exit_arg;
    int type, storage, offset;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 69 || mir_cfg_block_count() != 5 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID ||
        mir.insns[1].opcode != MIR_PARAM ||
        mir.insns[2].opcode != MIR_PARAM ||
        mir.insns[3].opcode != MIR_PARAM ||
        !mir_machine_same_location(&mir.insns[1], &mir.insns[4]) ||
        mir.insns[6].opcode != MIR_STORE ||
        mir.insns[6].src1 != mir.insns[4].dst ||
        !mir_machine_constant_equals(mir.insns[8].dst, 255) ||
        mir.insns[9].immediate != '&' ||
        !mir_machine_constant_equals(mir.insns[16].dst, 0) ||
        mir.insns[24].opcode != MIR_PHI ||
        mir.insns[27].immediate != '<' ||
        mir.insns[27].src1 != mir.insns[24].dst ||
        mir.insns[27].src2 != mir.insns[3].dst ||
        mir.insns[28].src1 != mir.insns[27].dst ||
        mir.insns[28].label != mir.insns[65].label)
        return mir_machine_reject("byte-mismatch-reporter", "shape");
    if (!mir_machine_same_location(&mir.insns[6], &mir.insns[29]) ||
        mir.insns[30].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[30].src1 != mir.insns[29].dst ||
        mir.insns[30].memory_size != 1 ||
        mir.insns[34].immediate != TOK_NE ||
        mir.insns[34].src1 != mir.insns[32].dst ||
        mir.insns[34].src2 != mir.insns[33].dst ||
        mir.insns[35].src1 != mir.insns[34].dst ||
        mir.insns[35].label != mir.insns[53].label ||
        mir.insns[36].opcode != MIR_STRING_ADDRESS ||
        !mir_machine_three_call_arguments(print_call, print_args) ||
        print_args[0] != mir.insns[36].dst ||
        print_args[1] != mir.insns[41].dst ||
        print_args[2] != mir.insns[46].dst ||
        !mir_machine_single_call_argument(exit_call, &exit_arg) ||
        !mir_machine_constant_equals(exit_arg, 1))
        return mir_machine_reject("byte-mismatch-reporter", "mismatch");
    if (!mir_machine_same_location(&mir.insns[6], &mir.insns[54]) ||
        !mir_machine_constant_equals(mir.insns[55].dst, 1) ||
        mir.insns[56].immediate != '+' ||
        !mir_machine_same_location(&mir.insns[6], &mir.insns[57]) ||
        !mir_machine_constant_equals(mir.insns[61].dst, 1) ||
        mir.insns[62].immediate != '+' ||
        mir.insns[64].label != mir.insns[19].label ||
        mir.insns[66].opcode != MIR_STRING_ADDRESS ||
        mir.insns[68].opcode != MIR_CALL)
        return mir_machine_reject("byte-mismatch-reporter", "loop");
    plan->print_function = find_global(print_call->name);
    plan->exit_function = find_global(exit_call->name);
    if (plan->print_function == NULL || plan->exit_function == NULL ||
        strcmp(mir.insns[68].name, print_call->name))
        return mir_machine_reject("byte-mismatch-reporter", "symbols");
    plan->mismatch_string_id = (int)mir.insns[36].immediate;
    plan->failure_string_id = (int)mir.insns[66].immediate;
#define MISMATCH_PARAM(insn, field) \
    do { \
        if (!mir_scalar_memory_location( \
                (insn), &type, &storage, &offset) || \
            storage != SC_PARAM || offset < 2) \
            return mir_machine_reject( \
                "byte-mismatch-reporter", "parameter"); \
        (field) = offset - 2; \
    } while (0)
    MISMATCH_PARAM(&mir.insns[1], plan->pointer_stack_offset);
    MISMATCH_PARAM(&mir.insns[2], plan->value_stack_offset);
    MISMATCH_PARAM(&mir.insns[3], plan->count_stack_offset);
#undef MISMATCH_PARAM
    return 1;
}

static void mir_emit_byte_mismatch_reporter(
    MirStream *out, const struct MirByteMismatchReporter *plan)
{
    int loop = new_label();
    int mismatch = new_label();
    int done = new_label();

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld c,(ix+%d)\n\tld b,(ix+%d)\n"
            "\tld a,b\n\tor c\n\tjp z,L%d\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tld a,(ix+%d)\n"
            "L%d:\n\tcpi\n\tjp nz,L%d\n\tjp pe,L%d\n\tjp L%d\n"
            "L%d:\n\tdec hl\n\tld e,(hl)\n\tld d,0\n\tpush de\n"
            "\tld e,(ix+%d)\n\tld d,(ix+%d)\n"
            "\tor a\n\tsbc hl,de\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->count_stack_offset + 2,
            plan->count_stack_offset + 3,
            done,
            plan->pointer_stack_offset + 2,
            plan->pointer_stack_offset + 3,
            plan->value_stack_offset + 2,
            loop, mismatch, loop, done,
            mismatch,
            plan->pointer_stack_offset + 2,
            plan->pointer_stack_offset + 3,
            plan->mismatch_string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tld hl,1\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->exit_function);
    mir_stream_puts("\tpop bc\n", out);
    mir_stream_printf(out, "L%d:\n\tld hl,S%d\n\tpush hl\n",
            done, plan->failure_string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_stream_puts("\tpop bc\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_compact_record_append(
    struct MirCompactRecordAppend *plan)
{
    static const int root_indices[3] = { 12, 19, 25 };
    static const int count_indices[3] = { 13, 20, 26 };
    static const int index_indices[3] = { 14, 21, 27 };
    static const int member_indices[3] = { 15, 22, 28 };
    static const int store_indices[3] = { 18, 24, 30 };
    int type, storage, offset;
    int field;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 36 || mir_cfg_block_count() != 2 ||
        mir.has_vla || type_size(mir.return_type) != 2 ||
        mir.insns[4].opcode != MIR_LOAD ||
        mir.insns[6].immediate != TOK_GE ||
        mir.insns[6].src1 != mir.insns[4].dst ||
        mir.insns[6].src2 != mir.insns[5].dst ||
        mir.insns[7].src1 != mir.insns[6].dst ||
        mir.insns[7].label != mir.insns[11].label ||
        mir.insns[8].opcode != MIR_STRING_ADDRESS ||
        mir.insns[10].opcode != MIR_CALL ||
        !mir_machine_single_call_argument(&mir.insns[10], &field) ||
        field != mir.insns[8].dst)
        return mir_machine_reject("compact-record-append", "guard");
    plan->capacity = (int)mir.insns[5].immediate;
    plan->string_id = (int)mir.insns[8].immediate;
    plan->count = find_global(mir.insns[4].name);
    plan->error_function = find_global(mir.insns[10].name);
    if (plan->capacity <= 0 || plan->capacity > 32767 ||
        plan->count == NULL || plan->count->is_volatile ||
        plan->error_function == NULL || !plan->error_function->is_defined)
        return mir_machine_reject("compact-record-append", "guard-symbols");
    for (field = 0; field < 3; ++field) {
        const struct MirInsn *root = &mir.insns[root_indices[field]];
        const struct MirInsn *count = &mir.insns[count_indices[field]];
        const struct MirInsn *index = &mir.insns[index_indices[field]];
        const struct MirInsn *member = &mir.insns[member_indices[field]];
        const struct MirInsn *store = &mir.insns[store_indices[field]];

        if (root->opcode != MIR_LOAD ||
            !mir_machine_same_location(&mir.insns[4], count) ||
            index->opcode != MIR_INDEX_ADDRESS ||
            index->src1 != root->dst ||
            index->src2 != count->dst ||
            index->immediate <= 0 ||
            member->opcode != MIR_MEMBER_ADDRESS ||
            member->src1 != index->dst ||
            store->opcode != MIR_STORE_INDIRECT ||
            store->src1 != member->dst ||
            store->memory_size != (field == 0 ? 1 : 2) ||
            (field == 0
                 ? (mir.insns[17].src1 != mir.insns[1].dst ||
                    store->src2 != mir.insns[17].dst)
                 : store->src2 != mir.insns[1 + field].dst))
            return mir_machine_reject("compact-record-append", "fields");
        if (field == 0) {
            plan->records = find_global(root->name);
            plan->stride = (int)index->immediate;
        } else if (strcmp(root->name, mir.insns[12].name) ||
                   index->immediate != plan->stride) {
            return mir_machine_reject(
                "compact-record-append", "field-consistency");
        }
        plan->field_offsets[field] = (int)member->immediate;
    }
    if (plan->records == NULL || plan->records->is_volatile ||
        plan->stride <= 0 || plan->stride > 255 ||
        !mir_machine_same_location(&mir.insns[4], &mir.insns[31]) ||
        !mir_machine_constant_equals(mir.insns[32].dst, 1) ||
        mir.insns[33].immediate != '+' ||
        mir.insns[33].src1 != mir.insns[31].dst ||
        !mir_machine_same_location(&mir.insns[4], &mir.insns[34]) ||
        mir.insns[34].src1 != mir.insns[33].dst ||
        mir.insns[35].src1 != mir.insns[31].dst)
        return mir_machine_reject("compact-record-append", "tail");
    for (field = 0; field < 3; ++field) {
        if (!mir_scalar_memory_location(
                &mir.insns[1 + field], &type, &storage, &offset) ||
            storage != SC_PARAM || offset < 2)
            return mir_machine_reject(
                "compact-record-append", "parameters");
        plan->parameter_stack_offsets[field] = offset - 2;
    }
    return 1;
}

static void mir_emit_compact_record_append(
    MirStream *out, const struct MirCompactRecordAppend *plan)
{
    int ready = new_label();

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_word(out, plan->count, 0);
    mir_stream_printf(out,
            "\tld de,%d\n\tor a\n\tsbc hl,de\n\tjp c,L%d\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->capacity, ready, plan->string_id);
    mir_machine_emit_symbol_call(out, plan->error_function);
    mir_stream_puts("\tpop bc\n", out);
    mir_stream_printf(out, "L%d:\n", ready);
    mir_machine_emit_global_word(out, plan->count, 0);
    mir_stream_puts("\tld b,h\n\tld c,l\n\tadd hl,hl\n\tadd hl,hl\n"
          "\tadd hl,bc\n\tpush hl\n", out);
    mir_machine_emit_global_word(out, plan->records, 0);
    mir_stream_puts("\tex de,hl\n\tpop hl\n\tadd hl,de\n", out);
    mir_machine_emit_hl_offset(out, plan->field_offsets[0], 0);
    mir_stream_printf(out, "\tld a,(ix+%d)\n\tld (hl),a\n",
            plan->parameter_stack_offsets[0] + 2);
    mir_machine_emit_hl_offset(
        out, plan->field_offsets[1] - plan->field_offsets[0], 0);
    mir_stream_printf(out,
            "\tld e,(ix+%d)\n\tld d,(ix+%d)\n"
            "\tld (hl),e\n\tinc hl\n\tld (hl),d\n",
            plan->parameter_stack_offsets[1] + 2,
            plan->parameter_stack_offsets[1] + 3);
    mir_machine_emit_hl_offset(
        out, plan->field_offsets[2] - plan->field_offsets[1] - 1, 0);
    mir_stream_printf(out,
            "\tld e,(ix+%d)\n\tld d,(ix+%d)\n"
            "\tld (hl),e\n\tinc hl\n\tld (hl),d\n",
            plan->parameter_stack_offsets[2] + 2,
            plan->parameter_stack_offsets[2] + 3);
    mir_stream_puts("\tinc bc\n\tld h,b\n\tld l,c\n", out);
    mir_machine_emit_global_word_store(out, plan->count, 0);
    mir_stream_puts("\tdec hl\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_signed_mul_clamp_abs(struct MirSignedMulClampAbs *plan)
{
    int type, storage, offset;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 45 || mir_cfg_block_count() != 13 ||
        mir.has_vla || type_size(mir.return_type) != 2 ||
        mir.insns[1].opcode != MIR_PARAM ||
        mir.insns[2].opcode != MIR_PARAM ||
        mir.insns[5].opcode != MIR_BINARY ||
        mir.insns[5].immediate != '*' ||
        mir.insns[5].src1 != mir.insns[1].dst ||
        mir.insns[5].src2 != mir.insns[2].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[6]) ||
        mir.insns[6].src1 != mir.insns[5].dst)
        return mir_machine_reject("signed-mul-clamp-abs", "shape");
    plan->limit = (int)mir.insns[8].immediate;
    if (plan->limit <= 0 || plan->limit > 32767 ||
        mir.insns[9].immediate != '>' ||
        !mir_machine_same_location(&mir.insns[6], &mir.insns[7]) ||
        mir.insns[9].src1 != mir.insns[7].dst ||
        mir.insns[9].src2 != mir.insns[8].dst ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        !mir_machine_constant_equals(mir.insns[11].dst, plan->limit) ||
        !mir_machine_same_location(&mir.insns[6], &mir.insns[15]) ||
        mir.insns[18].immediate != '<' ||
        mir.insns[18].src1 != mir.insns[15].dst ||
        !mir_machine_constant_equals(
            mir.insns[17].dst, 65536 - plan->limit) ||
        !mir_machine_constant_equals(
            mir.insns[21].dst, 65536 - plan->limit) ||
        !mir_machine_same_location(&mir.insns[6], &mir.insns[25]) ||
        !mir_machine_constant_equals(mir.insns[26].dst, 0) ||
        mir.insns[27].immediate != '<' ||
        !mir_machine_same_location(&mir.insns[6], &mir.insns[29]) ||
        mir.insns[30].immediate != '-' ||
        !mir_machine_same_location(&mir.insns[6], &mir.insns[34]) ||
        mir.insns[44].src1 != mir.insns[43].dst)
        return mir_machine_reject("signed-mul-clamp-abs", "flow");
    if (!mir_scalar_memory_location(
            &mir.insns[1], &type, &storage, &offset) ||
        storage != SC_PARAM || offset < 2)
        return mir_machine_reject("signed-mul-clamp-abs", "left");
    plan->left_stack_offset = offset - 2;
    if (!mir_scalar_memory_location(
            &mir.insns[2], &type, &storage, &offset) ||
        storage != SC_PARAM || offset < 2)
        return mir_machine_reject("signed-mul-clamp-abs", "right");
    plan->right_stack_offset = offset - 2;
    return 1;
}

static void mir_emit_signed_mul_clamp_abs(
    MirStream *out, const struct MirSignedMulClampAbs *plan)
{
    int negative = new_label();
    int upper = new_label();
    int lower = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tld h,b\n\tld l,c\n",
            plan->right_stack_offset, plan->left_stack_offset);
    mir_emit_runtime_call(out, "__mulu");
    mir_stream_puts("\tld b,h\n\tld c,l\n\tbit 7,b\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n\tld a,b\n\tor a\n\tjp nz,L%d\n"
                 "\tld a,c\n\tcp %d\n\tjp nc,L%d\n"
                 "\tld h,b\n\tld l,c\n\tret\n"
                 "L%d:\n\tld h,b\n\tld l,c\n\tld de,%d\n"
                 "\tor a\n\tsbc hl,de\n\tjp c,L%d\n"
                 "\tld hl,0\n\tor a\n\tsbc hl,bc\n\tret\n"
                 "L%d:\n\tld hl,%d\n\tret\n"
                 "L%d:\n\tld hl,%d\n\tret\n",
            negative, upper, plan->limit + 1, upper,
            negative, 65536 - plan->limit, lower,
            upper, plan->limit,
            lower, 65536 - plan->limit);
}

static int mir_match_fixed_global_string_copies(
    struct MirFixedGlobalStringCopies *plan)
{
    int copy;
    int print_arguments[5] = { 0 };
    int print_count = 0;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 50 || mir_cfg_block_count() != 1 ||
        mir.has_vla || type_size(mir.return_type) != 2)
        return mir_machine_reject("fixed-global-string-copies", "shape");
    for (copy = 0; copy < 3; ++copy) {
        int base = 1 + copy * 10;
        const struct MirInsn *root = &mir.insns[base];
        const struct MirInsn *load = &mir.insns[base + 1];
        const struct MirInsn *store = &mir.insns[base + 4];
        const struct MirInsn *address = &mir.insns[base + 5];
        const struct MirInsn *string = &mir.insns[base + 7];
        const struct MirInsn *call = &mir.insns[base + 9];
        int args[2];

        if (root->opcode != MIR_ADDRESS ||
            load->opcode != MIR_LOAD ||
            !mir_machine_constant_equals(mir.insns[base + 2].dst, 1) ||
            mir.insns[base + 3].immediate != '+' ||
            mir.insns[base + 3].src1 != load->dst ||
            store->opcode != MIR_STORE ||
            store->src1 != mir.insns[base + 3].dst ||
            !mir_machine_same_location(load, store) ||
            address->opcode != MIR_INDEX_ADDRESS ||
            address->src1 != root->dst ||
            address->src2 != load->dst ||
            address->immediate <= 0 ||
            string->opcode != MIR_STRING_ADDRESS ||
            call->opcode != MIR_CALL ||
            !mir_machine_two_call_arguments(call, args) ||
            args[0] != address->dst || args[1] != string->dst)
            return mir_machine_reject(
                "fixed-global-string-copies", "copy");
        if (copy == 0) {
            plan->root = find_global(root->name);
            plan->index = find_global(load->name);
            plan->copy_function = find_global(call->name);
            plan->stride = (int)address->immediate;
        } else if (strcmp(root->name, mir.insns[1].name) ||
                   strcmp(load->name, mir.insns[2].name) ||
                   strcmp(call->name, mir.insns[10].name) ||
                   address->immediate != plan->stride) {
            return mir_machine_reject(
                "fixed-global-string-copies", "consistency");
        }
        plan->source_string_ids[copy] = (int)string->immediate;
    }
    if (plan->root == NULL || plan->root->is_volatile ||
        plan->index == NULL || plan->index->is_volatile ||
        plan->copy_function == NULL || plan->copy_function->is_funcptr ||
        plan->stride != 16 ||
        mir.insns[31].opcode != MIR_STRING_ADDRESS ||
        mir.insns[47].opcode != MIR_CALL ||
        strcmp(mir.insns[47].name, "printf"))
        return mir_machine_reject("fixed-global-string-copies", "report");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *arg = &mir.insns[instruction];
        int index;
        if (arg->opcode != MIR_ARG ||
            arg->secondary_offset != mir.insns[47].secondary_offset)
            continue;
        index = (int)arg->immediate;
        if (index < 0 || index >= 5 || print_arguments[index] != 0)
            return mir_machine_reject(
                "fixed-global-string-copies", "print-args");
        print_arguments[index] = arg->src1 + 1;
        ++print_count;
    }
    if (print_count != 5 ||
        print_arguments[0] != mir.insns[31].dst + 1 ||
        print_arguments[1] != mir.insns[33].dst + 1 ||
        print_arguments[2] != mir.insns[37].dst + 1 ||
        print_arguments[3] != mir.insns[41].dst + 1 ||
        print_arguments[4] != mir.insns[45].dst + 1 ||
        !mir_machine_same_location(&mir.insns[2], &mir.insns[33]) ||
        !mir_machine_constant_equals(mir.insns[36].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[40].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[44].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[48].dst, 0) ||
        mir.insns[49].src1 != mir.insns[48].dst)
        return mir_machine_reject(
            "fixed-global-string-copies", "print-order");
    plan->print_function = find_global(mir.insns[47].name);
    plan->format_string_id = (int)mir.insns[31].immediate;
    if (plan->print_function == NULL)
        return mir_machine_reject("fixed-global-string-copies", "symbols");
    return 1;
}

static void mir_emit_fixed_global_string_copies(
    MirStream *out, const struct MirFixedGlobalStringCopies *plan)
{
    int copy;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (copy = 0; copy < 3; ++copy) {
        mir_machine_emit_global_word(out, plan->index, 0);
        mir_stream_puts("\tpush hl\n\tinc hl\n", out);
        mir_machine_emit_global_word_store(out, plan->index, 0);
        mir_stream_puts("\tpop hl\n\tadd hl,hl\n\tadd hl,hl\n"
              "\tadd hl,hl\n\tadd hl,hl\n", out);
        mir_machine_emit_global_address_de(out, plan->root, 0);
        mir_stream_puts("\tadd hl,de\n\tex de,hl\n", out);
        mir_stream_printf(out, "\tld hl,S%d\n",
                plan->source_string_ids[copy]);
        mir_emit_runtime_call(out, "__scf");
    }
    for (copy = 2; copy >= 0; --copy) {
        mir_machine_emit_global_address_hl(
            out, plan->root, copy * plan->stride);
        mir_stream_puts("\tpush hl\n", out);
    }
    mir_machine_emit_global_word(out, plan->index, 0);
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", plan->format_string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    for (copy = 0; copy < 5; ++copy)
        mir_stream_puts("\tpop bc\n", out);
    mir_stream_puts("\tld hl,0\n\tret\n", out);
}

static int mir_match_scaled_global_store(struct MirScaledGlobalStore *plan)
{
    const struct MirInsn *base = &mir.insns[1];
    const struct MirInsn *scale = &mir.insns[2];
    const struct MirInsn *index = &mir.insns[3];
    const struct MirInsn *value = &mir.insns[4];
    int type, storage, offset;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 53 || mir_cfg_block_count() != 4 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID ||
        mir.insns[7].immediate != TOK_EQ ||
        mir.insns[7].src1 != scale->dst ||
        !mir_machine_constant_equals(mir.insns[6].dst, 1) ||
        mir.insns[8].src1 != mir.insns[7].dst ||
        mir.insns[8].label != mir.insns[22].label)
        return mir_machine_reject("scaled-global-store", "shape");
    if (mir.insns[10].opcode != MIR_ADDRESS ||
        mir.insns[14].immediate != '*' ||
        mir.insns[14].src1 != index->dst ||
        mir.insns[14].src2 != scale->dst ||
        mir.insns[15].immediate != '+' ||
        mir.insns[15].src1 != base->dst ||
        mir.insns[15].src2 != mir.insns[14].dst ||
        mir.insns[16].src1 != mir.insns[10].dst ||
        mir.insns[16].src2 != mir.insns[15].dst ||
        mir.insns[18].src1 != value->dst ||
        mir.insns[19].src1 != mir.insns[16].dst ||
        mir.insns[19].src2 != mir.insns[18].dst ||
        mir.insns[19].memory_size != 1 ||
        mir.insns[21].label != mir.insns[52].label)
        return mir_machine_reject("scaled-global-store", "byte");
    if (mir.insns[23].opcode != MIR_ADDRESS ||
        mir.insns[27].immediate != '*' ||
        mir.insns[27].src1 != index->dst ||
        mir.insns[27].src2 != scale->dst ||
        mir.insns[28].immediate != '+' ||
        mir.insns[28].src1 != base->dst ||
        mir.insns[28].src2 != mir.insns[27].dst ||
        mir.insns[29].src1 != mir.insns[23].dst ||
        mir.insns[29].src2 != mir.insns[28].dst ||
        mir.insns[34].src1 != mir.insns[29].dst ||
        mir.insns[34].memory_size != 1 ||
        mir.insns[35].opcode != MIR_ADDRESS ||
        mir.insns[39].immediate != '*' ||
        mir.insns[39].src1 != index->dst ||
        mir.insns[39].src2 != scale->dst ||
        mir.insns[40].immediate != '+' ||
        mir.insns[40].src1 != base->dst ||
        mir.insns[40].src2 != mir.insns[39].dst ||
        !mir_machine_constant_equals(mir.insns[41].dst, 1) ||
        mir.insns[42].immediate != '+' ||
        mir.insns[43].src1 != mir.insns[35].dst ||
        mir.insns[43].src2 != mir.insns[42].dst ||
        mir.insns[50].src1 != mir.insns[43].dst ||
        mir.insns[50].memory_size != 1)
        return mir_machine_reject("scaled-global-store", "word");
    plan->root = find_global(mir.insns[10].name);
    if (plan->root == NULL || plan->root->is_volatile ||
        strcmp(mir.insns[10].name, mir.insns[23].name) ||
        strcmp(mir.insns[10].name, mir.insns[35].name))
        return mir_machine_reject("scaled-global-store", "root");
#define STORE_PARAM_OFFSET(insn, field) \
    do { \
        if (!mir_scalar_memory_location( \
                (insn), &type, &storage, &offset) || \
            storage != SC_PARAM || offset < 2) \
            return mir_machine_reject( \
                "scaled-global-store", "parameter"); \
        (field) = offset - 2; \
    } while (0)
    STORE_PARAM_OFFSET(base, plan->base_stack_offset);
    STORE_PARAM_OFFSET(scale, plan->scale_stack_offset);
    STORE_PARAM_OFFSET(index, plan->index_stack_offset);
    STORE_PARAM_OFFSET(value, plan->value_stack_offset);
#undef STORE_PARAM_OFFSET
    return 1;
}

static void mir_emit_scaled_global_store(
    MirStream *out, const struct MirScaledGlobalStore *plan)
{
    int byte_store = new_label();
    int done = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tld h,b\n\tld l,c\n",
            plan->scale_stack_offset, plan->index_stack_offset);
    mir_emit_runtime_call(out, "__mulu");
    mir_stream_printf(out,
            "\tex de,hl\n\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tex de,hl\n\tadd hl,bc\n",
            plan->base_stack_offset);
    mir_machine_emit_global_address_de(out, plan->root, 0);
    mir_stream_puts("\tadd hl,de\n\tpush hl\n", out);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tdec a\n\tld c,a\n"
            "\tinc hl\n\tld a,(hl)\n\tor c\n\tpop hl\n",
            plan->value_stack_offset + 2,
            plan->scale_stack_offset + 2);
    mir_stream_printf(out, "\tjp z,L%d\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n"
                 "\tjp L%d\nL%d:\n\tld (hl),e\nL%d:\n\tret\n",
            byte_store, done, byte_store, done);
}

static int mir_match_scaled_global_load(struct MirScaledGlobalLoad *plan)
{
    const struct MirInsn *base = &mir.insns[1];
    const struct MirInsn *scale = &mir.insns[2];
    const struct MirInsn *index = &mir.insns[3];
    int type, storage, offset;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 44 || mir_cfg_block_count() != 2 ||
        mir.has_vla || type_size(mir.return_type) != 2 ||
        mir.insns[6].opcode != MIR_BINARY ||
        mir.insns[6].immediate != TOK_EQ ||
        mir.insns[6].src1 != scale->dst ||
        !mir_machine_constant_equals(mir.insns[5].dst, 1) ||
        mir.insns[7].src1 != mir.insns[6].dst ||
        mir.insns[7].label != mir.insns[18].label)
        return mir_machine_reject("scaled-global-load", "shape");
    if (mir.insns[8].opcode != MIR_ADDRESS ||
        mir.insns[12].immediate != '*' ||
        mir.insns[12].src1 != index->dst ||
        mir.insns[12].src2 != scale->dst ||
        mir.insns[13].immediate != '+' ||
        mir.insns[13].src1 != base->dst ||
        mir.insns[13].src2 != mir.insns[12].dst ||
        mir.insns[14].src1 != mir.insns[8].dst ||
        mir.insns[14].src2 != mir.insns[13].dst ||
        mir.insns[14].immediate != 1 ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        mir.insns[15].memory_size != 1 ||
        mir.insns[17].src1 != mir.insns[16].dst)
        return mir_machine_reject("scaled-global-load", "byte");
    if (mir.insns[19].opcode != MIR_ADDRESS ||
        mir.insns[23].immediate != '*' ||
        mir.insns[23].src1 != index->dst ||
        mir.insns[23].src2 != scale->dst ||
        mir.insns[24].immediate != '+' ||
        mir.insns[24].src1 != base->dst ||
        mir.insns[24].src2 != mir.insns[23].dst ||
        mir.insns[25].src1 != mir.insns[19].dst ||
        mir.insns[25].src2 != mir.insns[24].dst ||
        mir.insns[26].src1 != mir.insns[25].dst ||
        mir.insns[26].memory_size != 1 ||
        mir.insns[31].immediate != '*' ||
        mir.insns[31].src1 != index->dst ||
        mir.insns[31].src2 != scale->dst ||
        mir.insns[32].immediate != '+' ||
        mir.insns[32].src1 != base->dst ||
        mir.insns[32].src2 != mir.insns[31].dst ||
        !mir_machine_constant_equals(mir.insns[33].dst, 1) ||
        mir.insns[34].immediate != '+' ||
        mir.insns[35].src1 != mir.insns[27].dst ||
        mir.insns[35].src2 != mir.insns[34].dst ||
        mir.insns[36].src1 != mir.insns[35].dst ||
        mir.insns[36].memory_size != 1 ||
        !mir_machine_constant_equals(mir.insns[37].dst, 8) ||
        mir.insns[39].immediate != TOK_SHL ||
        mir.insns[41].immediate != '|' ||
        mir.insns[43].src1 != mir.insns[41].dst)
        return mir_machine_reject("scaled-global-load", "word");
    plan->root = find_global(mir.insns[8].name);
    if (plan->root == NULL || plan->root->is_volatile ||
        strcmp(mir.insns[8].name, mir.insns[19].name) ||
        strcmp(mir.insns[8].name, mir.insns[27].name) ||
        !mir_scalar_memory_location(
            base, &type, &storage, &offset) ||
        storage != SC_PARAM || offset < 2)
        return mir_machine_reject("scaled-global-load", "base");
    plan->base_stack_offset = offset - 2;
    if (!mir_scalar_memory_location(
            scale, &type, &storage, &offset) ||
        storage != SC_PARAM || offset < 2)
        return mir_machine_reject("scaled-global-load", "scale");
    plan->scale_stack_offset = offset - 2;
    if (!mir_scalar_memory_location(
            index, &type, &storage, &offset) ||
        storage != SC_PARAM || offset < 2)
        return mir_machine_reject("scaled-global-load", "index");
    plan->index_stack_offset = offset - 2;
    return 1;
}

static void mir_emit_scaled_global_load(
    MirStream *out, const struct MirScaledGlobalLoad *plan)
{
    int byte_load = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tld h,b\n\tld l,c\n",
            plan->scale_stack_offset,
            plan->index_stack_offset);
    mir_emit_runtime_call(out, "__mulu");
    mir_stream_printf(out,
            "\tex de,hl\n\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tex de,hl\n\tadd hl,bc\n",
            plan->base_stack_offset);
    mir_machine_emit_global_address_de(out, plan->root, 0);
    mir_stream_puts("\tadd hl,de\n\tpush hl\n", out);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tdec a\n\tld c,a\n"
            "\tinc hl\n\tld a,(hl)\n\tor c\n\tpop hl\n",
            plan->scale_stack_offset + 2);
    mir_stream_printf(out, "\tjp z,L%d\n", byte_load);
    mir_stream_puts("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
          "\tld h,b\n\tld l,c\n\tret\n", out);
    mir_stream_printf(out, "L%d:\n\tld l,(hl)\n\tld h,0\n\tret\n", byte_load);
}

static int mir_match_wide_hash33(struct MirWideHash33 *plan)
{
    const struct MirInsn *parameter = &mir.insns[1];
    const struct MirInsn *hash_phi = &mir.insns[7];
    int type, storage, offset;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 32 || mir_cfg_block_count() != 4 ||
        mir.has_vla || type_size(mir.return_type) != 4 ||
        !mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[4]) ||
        hash_phi->opcode != MIR_PHI ||
        hash_phi->src1 != mir.insns[3].dst ||
        hash_phi->src2 != mir.insns[24].dst ||
        mir.insns[9].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].memory_size != 1 ||
        !mir_machine_constant_equals(mir.insns[10].dst, 0) ||
        mir.insns[12].immediate != TOK_NE ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[12].src2 != mir.insns[10].dst ||
        mir.insns[13].src1 != mir.insns[12].dst ||
        mir.insns[13].label != mir.insns[29].label)
        return mir_machine_reject("wide-hash33", "shape");
    plan->multiplier = (int)mir.insns[15].immediate;
    if (plan->multiplier != 33 ||
        mir.insns[16].immediate != '*' ||
        mir.insns[16].src1 != hash_phi->dst ||
        mir.insns[16].src2 != mir.insns[15].dst ||
        mir.insns[19].immediate != '+' ||
        mir.insns[19].src1 != mir.insns[17].dst ||
        !mir_machine_constant_equals(mir.insns[18].dst, 1) ||
        mir.insns[20].src1 != mir.insns[19].dst ||
        !mir_machine_same_location(parameter, &mir.insns[17]) ||
        mir.insns[21].src1 != mir.insns[17].dst ||
        mir.insns[24].immediate != '+' ||
        mir.insns[24].src1 != mir.insns[16].dst ||
        mir.insns[24].src2 != mir.insns[23].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[26]) ||
        mir.insns[28].label != mir.insns[5].label ||
        mir.insns[31].src1 != hash_phi->dst)
        return mir_machine_reject("wide-hash33", "flow");
    if (!mir_scalar_memory_location(
            parameter, &type, &storage, &offset) ||
        storage != SC_PARAM || offset < 2)
        return mir_machine_reject("wide-hash33", "parameter");
    plan->parameter_stack_offset = offset - 2;
    return 1;
}

static void mir_emit_wide_hash33(
    MirStream *out, const struct MirWideHash33 *plan)
{
    int loop = new_label();
    int done = new_label();
    int shift;

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tdec sp\n\tdec sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tld (ix-2),l\n\tld (ix-1),h\n"
            "\tld hl,0\n\tld de,0\n"
            "L%d:\n\tld c,(ix-2)\n\tld b,(ix-1)\n"
            "\tld a,(bc)\n\tor a\n\tjp z,L%d\n"
            "\tinc bc\n\tld (ix-2),c\n\tld (ix-1),b\n"
            "\tpush de\n\tpush hl\n",
            plan->parameter_stack_offset + 2,
            plan->parameter_stack_offset + 3,
            loop, done);
    for (shift = 0; shift < 5; ++shift)
        mir_stream_puts("\tadd hl,hl\n\trl e\n\trl d\n", out);
    mir_stream_puts("\tpop bc\n\tadd hl,bc\n\tex de,hl\n"
          "\tpop bc\n\tadc hl,bc\n\tex de,hl\n"
          "\tld c,a\n\tld b,0\n\tadd hl,bc\n\tex de,hl\n"
          "\tld bc,0\n\tadc hl,bc\n\tex de,hl\n", out);
    mir_stream_printf(out,
            "\tjp L%d\nL%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            loop, done);
}

static int mir_match_file_line_loop(struct MirFileLineLoop *plan)
{
    int args2[2];
    int args3[3];
    int arg;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 40 || mir_cfg_block_count() != 5 ||
        mir.has_vla || type_size(mir.return_type) != 2 ||
        mir.insns[1].opcode != MIR_STRING_ADDRESS ||
        mir.insns[3].opcode != MIR_STRING_ADDRESS ||
        mir.insns[5].opcode != MIR_CALL ||
        !mir_machine_two_call_arguments(&mir.insns[5], args2) ||
        args2[0] != mir.insns[1].dst ||
        args2[1] != mir.insns[3].dst ||
        mir.insns[6].opcode != MIR_STORE ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        !mir_machine_same_location(&mir.insns[6], &mir.insns[7]) ||
        mir.insns[8].opcode != MIR_UNARY ||
        mir.insns[8].immediate != '!' ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].label != mir.insns[16].label)
        return mir_machine_reject("file-line-loop", "open");
    if (mir.insns[10].opcode != MIR_STRING_ADDRESS ||
        mir.insns[12].opcode != MIR_CALL ||
        !mir_machine_single_call_argument(&mir.insns[12], &arg) ||
        arg != mir.insns[10].dst ||
        !mir_machine_constant_equals(mir.insns[13].dst, 1) ||
        mir.insns[14].src1 != mir.insns[13].dst)
        return mir_machine_reject("file-line-loop", "error");
    if (mir.insns[18].opcode != MIR_ADDRESS ||
        mir.insns[20].opcode != MIR_CONST ||
        mir.insns[24].opcode != MIR_CALL ||
        !mir_machine_three_call_arguments(&mir.insns[24], args3) ||
        args3[0] != mir.insns[18].dst ||
        args3[1] != mir.insns[20].dst ||
        args3[2] != mir.insns[22].dst ||
        !mir_machine_same_location(&mir.insns[6], &mir.insns[22]) ||
        mir.insns[25].src1 != mir.insns[24].dst ||
        mir.insns[25].label != mir.insns[34].label)
        return mir_machine_reject("file-line-loop", "read");
    plan->buffer_size = (int)mir.insns[20].immediate;
    if (plan->buffer_size <= 0 || plan->buffer_size > 255 ||
        mir.insns[26].opcode != MIR_ADDRESS ||
        mir.insns[31].opcode != MIR_CALL ||
        !mir_machine_two_call_arguments(&mir.insns[31], args2) ||
        args2[0] != mir.insns[26].dst ||
        args2[1] != mir.insns[28].dst ||
        mir.insns[33].label != mir.insns[17].label ||
        !mir_machine_same_location(&mir.insns[6], &mir.insns[35]) ||
        mir.insns[37].opcode != MIR_CALL ||
        !mir_machine_single_call_argument(&mir.insns[37], &arg) ||
        arg != mir.insns[35].dst ||
        !mir_machine_constant_equals(mir.insns[38].dst, 0) ||
        mir.insns[39].src1 != mir.insns[38].dst)
        return mir_machine_reject("file-line-loop", "tail");
    plan->stream_value = (int)mir.insns[28].immediate;
    plan->path_string_id = (int)mir.insns[1].immediate;
    plan->mode_string_id = (int)mir.insns[3].immediate;
    plan->open_function = find_global(mir.insns[5].name);
    plan->error_function = find_global(mir.insns[12].name);
    plan->read_function = find_global(mir.insns[24].name);
    plan->write_function = find_global(mir.insns[31].name);
    plan->close_function = find_global(mir.insns[37].name);
    if (plan->open_function == NULL || plan->error_function == NULL ||
        plan->read_function == NULL || plan->write_function == NULL ||
        plan->close_function == NULL)
        return mir_machine_reject("file-line-loop", "symbols");
    return 1;
}

static void mir_emit_file_line_loop(
    MirStream *out, const struct MirFileLineLoop *plan)
{
    int opened = new_label();
    int loop = new_label();
    int done = new_label();

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    mir_stream_printf(out,
            "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n",
            plan->buffer_size + 2);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,S%d\n\tpush hl\n\tld hl,S%d\n\tpush hl\n",
            plan->mode_string_id, plan->path_string_id);
    mir_machine_emit_symbol_call(out, plan->open_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld (ix-2),l\n\tld (ix-1),h\n"
          "\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n\tld hl,S%d\n\tpush hl\n",
            opened, plan->path_string_id);
    mir_machine_emit_symbol_call(out, plan->error_function);
    mir_stream_puts("\tpop bc\n\tld hl,1\n\tld sp,ix\n\tpop ix\n\tret\n", out);
    mir_stream_printf(out, "L%d:\nL%d:\n", opened, loop);
    mir_stream_puts("\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n", plan->buffer_size);
    mir_emit_file_buffer_address(out, plan);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->read_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n\tld hl,%d\n\tpush hl\n",
            done, plan->stream_value);
    mir_emit_file_buffer_address(out, plan);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->write_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", loop, done);
    mir_stream_puts("\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->close_function);
    mir_stream_puts("\tpop bc\n\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_volatile_local_widths(void)
{
    static const int prefix_opcodes[46] = {
        MIR_LABEL, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_CONST, MIR_STORE_INDIRECT, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_BINARY, MIR_LOAD, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_LABEL, MIR_PHI, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_JUMP
    };
    int instruction;

    if (mir.count != 87 || mir_cfg_block_count() != 13 ||
        mir.has_vla || type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < 46; ++instruction)
        if (mir.insns[instruction].opcode != prefix_opcodes[instruction])
            return 0;
    for (instruction = 46; instruction < mir.count; ++instruction)
        switch (mir.insns[instruction].opcode) {
        case MIR_CONST:
        case MIR_BINARY:
        case MIR_BRANCH_FALSE:
        case MIR_LABEL:
        case MIR_PHI:
        case MIR_NOP:
        case MIR_JUMP:
        case MIR_RETURN:
            break;
        default:
            return 0;
        }
    if (!mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[4].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[7].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[9].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[11].dst, 3) ||
        !mir_machine_constant_equals(mir.insns[15].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[19].dst, 1) ||
        mir.insns[22].immediate != '+' ||
        mir.insns[24].immediate != '+' ||
        !mir_machine_constant_equals(mir.insns[27].dst, 0) ||
        mir.insns[34].immediate != '<' ||
        !mir_machine_constant_equals(mir.insns[33].dst, 3) ||
        !mir_machine_constant_equals(mir.insns[37].dst, 1) ||
        mir.insns[38].immediate != '+' ||
        !mir_machine_constant_equals(mir.insns[42].dst, 1) ||
        mir.insns[43].immediate != '+' ||
        mir.insns[45].label != mir.insns[30].label)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[47].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[48].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[51].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[52].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[63].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[64].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[76].dst, 9) ||
        mir.insns[77].immediate != TOK_EQ ||
        mir.insns[86].opcode != MIR_RETURN)
        return 0;
    return 1;
}

static void mir_emit_volatile_local_widths(MirStream *out)
{
    int loop = new_label();
    int done = new_label();
    int result = new_label();

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-8\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\tld (ix-8),1\n\tld (ix-7),0\n"
          "\tld (ix-6),2\n\tld (ix-5),0\n"
          "\tld (ix-4),3\n\tld (ix-3),0\n"
          "\tld l,(ix-8)\n\tld h,(ix-7)\n"
          "\tld e,(ix-6)\n\tld d,(ix-5)\n\tadd hl,de\n"
          "\tld e,(ix-4)\n\tld d,(ix-3)\n\tadd hl,de\n"
          "\tld b,h\n\tld c,l\n"
          "\tld (ix-2),0\n\tld (ix-1),0\n", out);
    mir_stream_printf(out, "L%d:\n", loop);
    mir_stream_puts("\tld l,(ix-2)\n\tld h,(ix-1)\n"
          "\tld de,3\n\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp nc,L%d\n", done);
    mir_stream_puts("\tinc bc\n"
          "\tld l,(ix-2)\n\tld h,(ix-1)\n\tinc hl\n"
          "\tld (ix-2),l\n\tld (ix-1),h\n", out);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", loop, done);
    mir_stream_puts("\tld h,b\n\tld l,c\n\tld de,9\n\tor a\n\tsbc hl,de\n"
          "\tld a,h\n\tor l\n\tld hl,0\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n\tinc hl\nL%d:\n", result, result);
    mir_stream_puts("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_mixed_scalar_call_report(
    struct MirMixedScalarCallReport *plan)
{
    static const int call_indices[11] = {
        7, 12, 16, 18, 20, 22, 24, 26, 28, 31, 35
    };
    static const int result_arg_indices[11] = {
        8, 13, 17, 19, 21, 23, 25, 27, 29, 32, 36
    };
    static const int value_indices[11] = {
        5, 10, 14, -1, -1, -1, -1, -1, -1, -1, 33
    };
    const struct MirInsn *print_call = &mir.insns[37];
    int print_arguments[12] = { 0 };
    int print_count = 0;
    int call_number;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 40 || mir_cfg_block_count() != 1 ||
        mir.has_vla || type_size(mir.return_type) != 2 ||
        mir.insns[0].opcode != MIR_LABEL ||
        mir.insns[1].opcode != MIR_CALL ||
        !mir_machine_call_has_no_arguments(&mir.insns[1]) ||
        mir.insns[2].opcode != MIR_STRING_ADDRESS ||
        mir.insns[3].opcode != MIR_ARG ||
        mir.insns[3].src1 != mir.insns[2].dst)
        return mir_machine_reject("mixed-scalar-call-report", "shape");
    plan->setup_function = find_global(mir.insns[1].name);
    if (plan->setup_function == NULL ||
        !plan->setup_function->is_defined ||
        plan->setup_function->is_funcptr)
        return mir_machine_reject("mixed-scalar-call-report", "setup");
    for (call_number = 0; call_number < 11; ++call_number) {
        int call_index = call_indices[call_number];
        int result_index = result_arg_indices[call_number];
        const struct MirInsn *call = &mir.insns[call_index];
        long value;

        if (call->opcode != MIR_CALL ||
            mir.insns[result_index].opcode != MIR_ARG ||
            mir.insns[result_index].src1 != call->dst ||
            type_size(call->type) != 2 ||
            (call->memory_flags &
             (MIR_CALL_FLAG_VARIADIC |
              MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0)
            return mir_machine_reject("mixed-scalar-call-report", "calls");
        if (call_number == 9) {
            if (mir.insns[30].opcode != MIR_LOAD ||
                call->src1 != mir.insns[30].dst ||
                !mir_machine_call_has_no_arguments(call))
                return mir_machine_reject(
                    "mixed-scalar-call-report", "callback");
            plan->callback = find_global(mir.insns[30].name);
            if (plan->callback == NULL)
                return mir_machine_reject(
                    "mixed-scalar-call-report", "callback-missing");
            if ((mir.insns[30].memory_flags & 8) != 0)
                return mir_machine_reject(
                    "mixed-scalar-call-report", "callback-symbol");
            continue;
        }
        plan->functions[call_number] = find_global(call->name);
        if (plan->functions[call_number] == NULL ||
            !plan->functions[call_number]->is_defined ||
            plan->functions[call_number]->is_funcptr)
            return mir_machine_reject(
                "mixed-scalar-call-report", "function");
        if (value_indices[call_number] >= 0) {
            int call_argument;
            int value_index = value_indices[call_number];

            if (!mir_machine_constant_value(
                    mir.insns[value_index].dst, &value, 0) ||
                value < -32768 || value > 65535 ||
                !mir_machine_single_call_argument(call, &call_argument) ||
                call_argument != mir.insns[value_index].dst)
                return mir_machine_reject(
                    "mixed-scalar-call-report", "call-argument");
            plan->has_argument[call_number] = 1;
            plan->arguments[call_number] = (int)value & 0xffff;
        } else if (!mir_machine_call_has_no_arguments(call)) {
            return mir_machine_reject(
                "mixed-scalar-call-report", "noarg-call");
        }
    }
    if (print_call->opcode != MIR_CALL ||
        strcmp(print_call->name, "printf") ||
        (print_call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != MIR_CALL_FLAG_VARIADIC)
        return mir_machine_reject("mixed-scalar-call-report", "print");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *arg = &mir.insns[instruction];
        int index;

        if (arg->opcode != MIR_ARG ||
            arg->secondary_offset != print_call->secondary_offset)
            continue;
        index = (int)arg->immediate;
        if (index < 0 || index >= 12 || print_arguments[index] != 0)
            return mir_machine_reject(
                "mixed-scalar-call-report", "print-arguments");
        print_arguments[index] = arg->src1 + 1;
        ++print_count;
    }
    if (print_count != 12 ||
        print_arguments[0] != mir.insns[2].dst + 1)
        return mir_machine_reject("mixed-scalar-call-report", "print-prefix");
    for (call_number = 0; call_number < 11; ++call_number)
        if (print_arguments[call_number + 1] !=
            mir.insns[call_indices[call_number]].dst + 1)
            return mir_machine_reject(
                "mixed-scalar-call-report", "print-order");
    plan->print_function = find_global(print_call->name);
    plan->string_id = (int)mir.insns[2].immediate;
    if (plan->print_function == NULL ||
        plan->print_function->is_defined ||
        !mir_machine_constant_equals(mir.insns[38].dst, 0) ||
        mir.insns[39].opcode != MIR_RETURN ||
        mir.insns[39].src1 != mir.insns[38].dst)
        return mir_machine_reject("mixed-scalar-call-report", "return");
    return 1;
}

static void mir_emit_mixed_scalar_call_report(
    MirStream *out, const struct MirMixedScalarCallReport *plan)
{
    int call_number;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_symbol_call(out, plan->setup_function);
    for (call_number = 10; call_number >= 0; --call_number) {
        if (call_number == 9) {
            mir_machine_emit_global_word(out, plan->callback, 0);
            mir_emit_runtime_call(out, "__call_hl");
        } else {
            if (plan->has_argument[call_number]) {
                mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n",
                        plan->arguments[call_number]);
            }
            mir_machine_emit_symbol_call(
                out, plan->functions[call_number]);
            if (plan->has_argument[call_number])
                mir_stream_puts("\tpop bc\n", out);
        }
        mir_stream_puts("\tpush hl\n", out);
    }
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", plan->string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    for (call_number = 0; call_number < 12; ++call_number)
        mir_stream_puts("\tpop bc\n", out);
    mir_stream_puts("\tld hl,0\n\tret\n", out);
}

static int mir_match_volatile_member_sum(struct MirVolatileMemberSum *plan)
{
    const struct MirInsn *index_phi = &mir.insns[6];
    const struct MirInsn *total_phi = &mir.insns[7];
    const struct MirInsn *call = &mir.insns[28];

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 35 || mir_cfg_block_count() != 4 ||
        mir.has_vla || type_size(mir.return_type) != 2 ||
        !mir_machine_constant_equals(mir.insns[1].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[2]) ||
        !mir_machine_unobservable_local_store(&mir.insns[4]) ||
        index_phi->opcode != MIR_PHI ||
        index_phi->src1 != mir.insns[1].dst ||
        index_phi->src2 != mir.insns[17].dst ||
        total_phi->opcode != MIR_PHI ||
        total_phi->src1 != mir.insns[3].dst ||
        total_phi->src2 != mir.insns[25].dst ||
        mir.insns[10].immediate != '<' ||
        mir.insns[10].src1 != index_phi->dst ||
        mir.insns[10].src2 != mir.insns[9].dst ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[11].label != mir.insns[32].label)
        return mir_machine_reject("volatile-member-sum", "shape");
    plan->count = (int)mir.insns[9].immediate;
    if (plan->count <= 0 || plan->count > 255 ||
        mir.insns[12].opcode != MIR_LOAD ||
        mir.insns[13].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[13].src1 != mir.insns[12].dst ||
        mir.insns[14].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        (mir.insns[14].memory_flags & (1 | 8)) == 0 ||
        !mir_machine_constant_equals(mir.insns[16].dst, 1) ||
        mir.insns[17].immediate != '+' ||
        mir.insns[17].src1 != index_phi->dst ||
        mir.insns[17].src2 != mir.insns[16].dst ||
        mir.insns[19].src1 != mir.insns[14].dst ||
        mir.insns[19].src2 != index_phi->dst ||
        mir.insns[19].immediate != 2 ||
        mir.insns[21].src1 != mir.insns[19].dst ||
        mir.insns[23].src1 != mir.insns[21].dst ||
        mir.insns[24].src1 != mir.insns[23].dst ||
        mir.insns[25].immediate != '+' ||
        mir.insns[25].src1 != total_phi->dst ||
        mir.insns[25].src2 != mir.insns[24].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[27]) ||
        !mir_machine_call_has_no_arguments(call) ||
        mir.insns[31].label != mir.insns[5].label ||
        mir.insns[34].src1 != total_phi->dst)
        return mir_machine_reject("volatile-member-sum", "flow");
    plan->base = find_global(mir.insns[12].name);
    plan->function = find_global(call->name);
    plan->member_offset = (int)mir.insns[13].immediate;
    if (plan->base == NULL || plan->base->is_volatile ||
        plan->function == NULL || !plan->function->is_defined ||
        plan->function->is_funcptr ||
        plan->member_offset < -32768 || plan->member_offset > 32767)
        return mir_machine_reject("volatile-member-sum", "symbols");
    return 1;
}

static void mir_emit_volatile_member_sum(
    MirStream *out, const struct MirVolatileMemberSum *plan)
{
    int loop = new_label();
    int done = new_label();

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n\tdec sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\tld (ix-1),0\n\tld bc,0\n", out);
    mir_stream_printf(out,
            "L%d:\n\tld a,(ix-1)\n\tcp %d\n\tjp nc,L%d\n"
            "\tpush bc\n",
            loop, plan->count, done);
    mir_machine_emit_global_word(out, plan->base, 0);
    mir_machine_emit_hl_offset(out, plan->member_offset, 0);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld c,(ix-1)\n\tld b,0\n\tinc (ix-1)\n"
          "\tex de,hl\n\tadd hl,bc\n\tadd hl,bc\n"
          "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n"
          "\tpop bc\n\tadd hl,bc\n\tld b,h\n\tld c,l\n"
          "\tpush bc\n", out);
    mir_machine_emit_symbol_call(out, plan->function);
    mir_stream_puts("\tpop bc\n", out);
    mir_stream_printf(out,
            "\tjp L%d\nL%d:\n\tld h,b\n\tld l,c\n"
            "\tld sp,ix\n\tpop ix\n\tret\n",
            loop, done);
}

static int mir_match_fixed_member_init_calls(
    struct MirFixedMemberInitCalls *plan)
{
    const struct MirInsn *parameter = &mir.insns[1];
    const struct MirInsn *index_phi = &mir.insns[14];
    const struct MirInsn *call = &mir.insns[28];
    int args[2];

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 36 || mir_cfg_block_count() != 4 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID ||
        mir.insns[0].opcode != MIR_LABEL ||
        parameter->opcode != MIR_PARAM ||
        !mir_machine_parameter_value_offset(
            parameter->dst, &plan->parameter_stack_offset) ||
        mir.insns[2].opcode != MIR_LOAD ||
        !mir_machine_same_location(parameter, &mir.insns[2]) ||
        mir.insns[3].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[3].src1 != mir.insns[2].dst ||
        mir.insns[4].opcode != MIR_STRING_ADDRESS ||
        mir.insns[5].opcode != MIR_STORE_INDIRECT ||
        mir.insns[5].src1 != mir.insns[3].dst ||
        mir.insns[5].src2 != mir.insns[4].dst ||
        !mir_machine_same_location(parameter, &mir.insns[6]) ||
        mir.insns[7].src1 != mir.insns[6].dst ||
        mir.insns[9].src1 != mir.insns[7].dst ||
        mir.insns[9].src2 != mir.insns[8].dst)
        return mir_machine_reject("fixed-member-init-calls", "setup");
    plan->name_offset = (int)mir.insns[3].immediate;
    plan->count_offset = (int)mir.insns[7].immediate;
    plan->count = (int)mir.insns[8].immediate;
    plan->string_id = (int)mir.insns[4].immediate;
    if (plan->count <= 0 || plan->count > 16 ||
        !mir_machine_constant_equals(mir.insns[11].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[12]) ||
        index_phi->src1 != mir.insns[11].dst ||
        index_phi->src2 != mir.insns[32].dst ||
        mir.insns[18].immediate != '<' ||
        mir.insns[18].src1 != mir.insns[17].dst ||
        mir.insns[18].src2 != mir.insns[16].dst ||
        mir.insns[19].src1 != mir.insns[18].dst ||
        mir.insns[19].label != mir.insns[35].label ||
        !mir_machine_same_location(parameter, &mir.insns[20]) ||
        mir.insns[21].src1 != mir.insns[20].dst ||
        mir.insns[23].src1 != mir.insns[21].dst ||
        mir.insns[23].src2 != index_phi->dst ||
        mir.insns[23].immediate <= 0 ||
        !mir_machine_two_call_arguments(call, args) ||
        args[0] != mir.insns[23].dst ||
        args[1] != mir.insns[26].dst ||
        mir.insns[26].src1 != index_phi->dst ||
        !mir_machine_constant_equals(mir.insns[31].dst, 1) ||
        mir.insns[32].immediate != '+' ||
        mir.insns[32].src1 != index_phi->dst ||
        !mir_machine_unobservable_local_store(&mir.insns[33]) ||
        mir.insns[34].label != mir.insns[13].label)
        return mir_machine_reject("fixed-member-init-calls", "loop");
    plan->array_offset = (int)mir.insns[21].immediate;
    plan->stride = (int)mir.insns[23].immediate;
    plan->function = find_global(call->name);
    if (plan->function == NULL || !plan->function->is_defined ||
        plan->function->is_funcptr ||
        plan->stride <= 0 || plan->stride > 255 ||
        plan->name_offset < -32768 || plan->name_offset > 32767 ||
        plan->count_offset < -32768 || plan->count_offset > 32767 ||
        plan->array_offset < -32768 || plan->array_offset > 32767)
        return mir_machine_reject("fixed-member-init-calls", "constants");
    return 1;
}

static void mir_emit_fixed_member_init_calls(
    MirStream *out, const struct MirFixedMemberInitCalls *plan)
{
    int index;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_emit_member_init_parameter(out, plan->parameter_stack_offset);
    mir_machine_emit_hl_offset(out, plan->name_offset, 0);
    mir_stream_printf(out,
            "\tld de,S%d\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n",
            plan->string_id);
    mir_emit_member_init_parameter(out, plan->parameter_stack_offset);
    mir_machine_emit_hl_offset(out, plan->count_offset, 0);
    mir_stream_printf(out,
            "\tld (hl),%d\n\tinc hl\n\tld (hl),0\n",
            plan->count);
    for (index = 0; index < plan->count; ++index) {
        mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n", index);
        mir_emit_member_init_parameter(
            out, plan->parameter_stack_offset + 2);
        mir_machine_emit_hl_offset(
            out, plan->array_offset + index * plan->stride, 0);
        mir_stream_puts("\tpush hl\n", out);
        mir_machine_emit_symbol_call(out, plan->function);
        mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    }
    mir_stream_puts("\tret\n", out);
}

static int mir_match_local_byte_fill_call(struct MirLocalByteFillCall *plan)
{
    static const int expected_opcodes[41] = {
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_PHI,
        MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT,
        MIR_ADDRESS, MIR_NOP, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CONST,
        MIR_NOP, MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN
    };
    const struct MirInsn *index_phi = &mir.insns[5];
    const struct MirInsn *call = &mir.insns[38];
    int arguments[3];
    int type, storage, offset;
    int type2, storage2, offset2;
    long patch_index;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 41 || mir_cfg_block_count() != 4 ||
        mir.has_vla || type_size(mir.return_type) != 2)
        return mir_machine_reject("local-byte-fill-call", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject("local-byte-fill-call", "opcode");
    if (!mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[3]) ||
        index_phi->src1 != mir.insns[2].dst ||
        index_phi->src2 != mir.insns[20].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[17].label ||
        mir.insns[9].immediate != '<' ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].src2 != mir.insns[7].dst ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[10].label != mir.insns[23].label)
        return mir_machine_reject("local-byte-fill-call", "loop");
    plan->count = (int)mir.insns[7].immediate;
    plan->fill_value = (int)mir.insns[15].immediate & 0xff;
    if (plan->count <= 0 || plan->count > 255 ||
        !mir_scalar_memory_location(
            &mir.insns[11], &type, &storage, &offset) ||
        storage != SC_LOCAL || offset != -plan->count ||
        mir.insns[13].src1 != mir.insns[11].dst ||
        mir.insns[13].src2 != index_phi->dst ||
        mir.insns[13].immediate != 1 ||
        mir.insns[16].src1 != mir.insns[13].dst ||
        mir.insns[16].src2 != mir.insns[15].dst ||
        mir.insns[16].memory_size != 1 ||
        !mir_machine_constant_equals(mir.insns[19].dst, 1) ||
        mir.insns[20].immediate != '+' ||
        mir.insns[20].src1 != index_phi->dst ||
        mir.insns[20].src2 != mir.insns[19].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[21]) ||
        mir.insns[22].label != mir.insns[4].label)
        return mir_machine_reject("local-byte-fill-call", "fill");
    if (!mir_scalar_memory_location(
            &mir.insns[24], &type2, &storage2, &offset2) ||
        storage2 != storage || offset2 != offset ||
        !mir_machine_constant_value(
            mir.insns[25].dst, &patch_index, 0) ||
        patch_index < 0 || patch_index >= plan->count ||
        mir.insns[26].src1 != mir.insns[24].dst ||
        mir.insns[26].src2 != mir.insns[25].dst ||
        mir.insns[26].immediate != 1 ||
        mir.insns[29].src1 != mir.insns[26].dst ||
        mir.insns[29].src2 != mir.insns[28].dst ||
        mir.insns[29].memory_size != 1)
        return mir_machine_reject("local-byte-fill-call", "patch");
    plan->patch_offset = (int)patch_index;
    plan->patch_value = (int)mir.insns[28].immediate & 0xff;
    if (!mir_scalar_memory_location(
            &mir.insns[30], &type2, &storage2, &offset2) ||
        storage2 != storage || offset2 != offset ||
        !mir_machine_three_call_arguments(call, arguments) ||
        arguments[0] != mir.insns[30].dst ||
        arguments[1] != mir.insns[33].dst ||
        arguments[2] != mir.insns[35].dst ||
        mir.insns[33].immediate != plan->fill_value ||
        mir.insns[35].immediate != plan->count ||
        !mir_machine_constant_equals(mir.insns[39].dst, 0) ||
        mir.insns[40].src1 != mir.insns[39].dst)
        return mir_machine_reject("local-byte-fill-call", "call");
    plan->function = find_global(call->name);
    if (plan->function == NULL || !plan->function->is_defined ||
        plan->function->is_funcptr ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0)
        return mir_machine_reject("local-byte-fill-call", "function");
    return 1;
}

static void mir_emit_local_byte_fill_call(
    MirStream *out, const struct MirLocalByteFillCall *plan)
{
    int loop = new_label();

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    mir_stream_printf(out, "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n", plan->count);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tpush ix\n\tpop hl\n\tld de,-%d\n\tadd hl,de\n"
            "\tld a,%d\n\tld b,%d\n"
            "L%d:\n\tld (hl),a\n\tinc hl\n\tdjnz L%d\n"
            "\tld (ix%+d),%d\n"
            "\tld hl,%d\n\tpush hl\n"
            "\tld hl,%d\n\tpush hl\n"
            "\tpush ix\n\tpop hl\n\tld de,-%d\n\tadd hl,de\n\tpush hl\n",
            plan->count, plan->fill_value, plan->count,
            loop, loop,
            -plan->count + plan->patch_offset, plan->patch_value,
            plan->count, plan->fill_value, plan->count);
    mir_machine_emit_symbol_call(out, plan->function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_global_last_record_kind(
    struct MirGlobalLastRecordKind *plan)
{
    static const int expected_opcodes[31] = {
        MIR_LABEL, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_RETURN,
        MIR_LABEL, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST,
        MIR_BINARY, MIR_BINARY, MIR_CONST, MIR_CONST, MIR_BINARY,
        MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY, MIR_RETURN
    };
    int memory_type, memory_storage, memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 31 || mir_cfg_block_count() != 2 ||
        mir.has_vla || type_size(mir.return_type) != 2)
        return mir_machine_reject("global-last-record-kind", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject("global-last-record-kind", "opcode");
    if (!mir_machine_named_nonvolatile(&mir.insns[1]) ||
        !mir_machine_same_location(&mir.insns[1], &mir.insns[10]) ||
        !mir_machine_same_location(&mir.insns[1], &mir.insns[13]) ||
        mir.insns[2].src1 != mir.insns[1].dst ||
        mir.insns[3].src1 != mir.insns[2].dst ||
        mir.insns[3].memory_size != 2 ||
        !mir_machine_constant_equals(mir.insns[4].dst, 0) ||
        mir.insns[5].immediate != TOK_LE ||
        mir.insns[5].src1 != mir.insns[3].dst ||
        mir.insns[5].src2 != mir.insns[4].dst ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        mir.insns[6].label != mir.insns[9].label ||
        !mir_machine_constant_equals(mir.insns[7].dst, 0) ||
        mir.insns[8].src1 != mir.insns[7].dst)
        return mir_machine_reject("global-last-record-kind", "count");
    plan->state = find_global(mir.insns[1].name);
    plan->count_offset = (int)mir.insns[2].immediate;
    if (plan->state == NULL || plan->state->is_volatile ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[12].memory_size != 2 ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        mir.insns[15].memory_size != 2 ||
        mir.insns[14].immediate == mir.insns[11].immediate)
        return mir_machine_reject("global-last-record-kind", "members");
    if (
        !mir_machine_constant_equals(mir.insns[16].dst, 4) ||
        mir.insns[17].immediate != '*' ||
        mir.insns[17].src1 != mir.insns[15].dst ||
        mir.insns[17].src2 != mir.insns[16].dst ||
        mir.insns[18].immediate != '+' ||
        mir.insns[18].src1 != mir.insns[12].dst ||
        mir.insns[18].src2 != mir.insns[17].dst)
        return mir_machine_reject("global-last-record-kind", "index");
    if (
        !mir_machine_constant_equals(mir.insns[19].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[20].dst, 4) ||
        mir.insns[21].immediate != '*' ||
        mir.insns[21].src1 != mir.insns[19].dst ||
        mir.insns[21].src2 != mir.insns[20].dst ||
        mir.insns[22].immediate != '-' ||
        mir.insns[22].src1 != mir.insns[18].dst ||
        mir.insns[22].src2 != mir.insns[21].dst)
        return mir_machine_reject("global-last-record-kind", "address");
    if (!mir_machine_unobservable_local_store(&mir.insns[24]) ||
        mir.insns[24].src1 != mir.insns[22].dst)
        return mir_machine_reject("global-last-record-kind", "local");
    plan->records_offset = (int)mir.insns[11].immediate;
    plan->stride = (int)mir.insns[16].immediate;
    if (!mir_machine_same_location(&mir.insns[24], &mir.insns[25]) ||
        mir.insns[26].src1 != mir.insns[25].dst ||
        mir.insns[27].src1 != mir.insns[26].dst ||
        mir.insns[27].memory_size != 2 ||
        mir.insns[29].immediate != TOK_EQ ||
        mir.insns[29].src1 != mir.insns[27].dst ||
        mir.insns[29].src2 != mir.insns[28].dst ||
        mir.insns[30].src1 != mir.insns[29].dst)
        return mir_machine_reject("global-last-record-kind", "result");
    plan->member_offset = (int)mir.insns[26].immediate;
    plan->wanted = (int)mir.insns[28].immediate;
    if (plan->stride != 4 ||
        plan->count_offset < -32768 || plan->count_offset > 32767 ||
        plan->records_offset < -32768 || plan->records_offset > 32767 ||
        plan->member_offset < -32768 || plan->member_offset > 32767)
        return mir_machine_reject("global-last-record-kind", "constants");
    if (!mir_scalar_memory_location(
            &mir.insns[1], &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL || memory_offset != 0)
        return mir_machine_reject("global-last-record-kind", "global");
    return 1;
}

static void mir_emit_global_last_record_kind(
    MirStream *out, const struct MirGlobalLastRecordKind *plan)
{
    int false_result = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_word(out, plan->state, 0);
    mir_machine_emit_hl_offset(out, plan->count_offset, 0);
    mir_stream_puts("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n\tbit 7,b\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", false_result);
    mir_stream_puts("\tld a,b\n\tor c\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n\tdec bc\n\tpush bc\n", false_result);
    mir_machine_emit_global_word(out, plan->state, 0);
    mir_machine_emit_hl_offset(out, plan->records_offset, 0);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpop bc\n"
          "\tld h,b\n\tld l,c\n\tadd hl,hl\n\tadd hl,hl\n"
          "\tadd hl,de\n", out);
    mir_machine_emit_hl_offset(out, plan->member_offset, 0);
    mir_stream_printf(out,
            "\tld a,(hl)\n\txor %d\n\tld c,a\n"
            "\tinc hl\n\tld a,(hl)\n\txor %d\n\tor c\n"
            "\tld hl,0\n\tret nz\n\tinc hl\n\tret\n"
            "L%d:\n\tld hl,0\n\tret\n",
            plan->wanted & 0xff,
            (plan->wanted >> 8) & 0xff,
            false_result);
}

static int mir_match_aggregate_byte_fill_return(
    struct MirAggregateByteFillReturn *plan)
{
    static const int expected_opcodes[39] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP, MIR_NOP,
        MIR_UNARY, MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_STORE_INDIRECT,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_NOP,
        MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_RETURN
    };
    const struct MirInsn *base = &mir.insns[1];
    const struct MirInsn *tag = &mir.insns[2];
    const struct MirInsn *index_phi = &mir.insns[9];
    int memory_type, memory_storage, memory_offset;
    int return_type, return_storage, return_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 39 || mir_cfg_block_count() != 4 ||
        mir.has_vla || type_size(base->type) != 1 ||
        type_size(tag->type) != 2)
        return mir_machine_reject("aggregate-byte-fill-return", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "aggregate-byte-fill-return", "opcode");
    if (!mir_machine_parameter_value_offset(
            base->dst, &plan->base_stack_offset) ||
        !mir_machine_parameter_value_offset(
            tag->dst, &plan->tag_stack_offset) ||
        !mir_machine_constant_equals(mir.insns[4].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[5]) ||
        index_phi->src1 != mir.insns[4].dst ||
        index_phi->src2 != mir.insns[29].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[26].label ||
        mir.insns[13].immediate != '<' ||
        mir.insns[13].src1 != mir.insns[12].dst ||
        mir.insns[13].src2 != mir.insns[11].dst ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        mir.insns[14].label != mir.insns[32].label)
        return mir_machine_reject("aggregate-byte-fill-return", "loop");
    plan->count = (int)mir.insns[11].immediate;
    if (plan->count <= 0 || plan->count > 255 ||
        !mir_scalar_memory_location(
            &mir.insns[15], &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_LOCAL ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[18].src1 != mir.insns[16].dst ||
        mir.insns[18].src2 != index_phi->dst ||
        mir.insns[18].immediate != 1 ||
        mir.insns[21].src1 != base->dst ||
        mir.insns[22].src1 != index_phi->dst ||
        mir.insns[23].immediate != '+' ||
        mir.insns[23].src1 != mir.insns[21].dst ||
        mir.insns[23].src2 != mir.insns[22].dst ||
        mir.insns[24].src1 != mir.insns[23].dst ||
        mir.insns[25].src1 != mir.insns[18].dst ||
        mir.insns[25].src2 != mir.insns[24].dst ||
        mir.insns[25].memory_size != 1 ||
        (mir.insns[25].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_constant_equals(mir.insns[28].dst, 1) ||
        mir.insns[29].immediate != '+' ||
        mir.insns[29].src1 != index_phi->dst ||
        mir.insns[29].src2 != mir.insns[28].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[30]) ||
        mir.insns[31].label != mir.insns[6].label)
        return mir_machine_reject("aggregate-byte-fill-return", "fill");
    if (!mir_scalar_memory_location(
            &mir.insns[33], &return_type, &return_storage, &return_offset) ||
        return_storage != memory_storage || return_offset != memory_offset ||
        mir.insns[34].src1 != mir.insns[33].dst ||
        mir.insns[36].src1 != mir.insns[34].dst ||
        mir.insns[36].src2 != tag->dst ||
        mir.insns[36].memory_size != 2 ||
        (mir.insns[36].memory_flags & (1 | 8)) != 0 ||
        !mir_scalar_memory_location(
            &mir.insns[37], &return_type, &return_storage, &return_offset) ||
        return_storage != memory_storage || return_offset != memory_offset ||
        mir.insns[38].src1 != mir.insns[37].dst)
        return mir_machine_reject("aggregate-byte-fill-return", "return");
    plan->tag_offset = (int)mir.insns[34].immediate;
    if (plan->tag_offset != plan->count ||
        memory_offset != -(plan->tag_offset + 2))
        return mir_machine_reject("aggregate-byte-fill-return", "layout");
    return 1;
}

static void mir_emit_aggregate_byte_fill_return(
    MirStream *out, const struct MirAggregateByteFillReturn *plan)
{
    int loop = new_label();

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld l,(ix+4)\n\tld h,(ix+5)\n"
            "\tld a,(ix+%d)\n\tld b,%d\n"
            "L%d:\n\tld (hl),a\n\tinc hl\n\tinc a\n\tdjnz L%d\n"
            "\tld e,(ix+%d)\n\tld d,(ix+%d)\n"
            "\tld (hl),e\n\tinc hl\n\tld (hl),d\n"
            "\tld sp,ix\n\tpop ix\n\tret\n",
            plan->base_stack_offset + 2,
            plan->count, loop, loop,
            plan->tag_stack_offset + 2,
            plan->tag_stack_offset + 3);
}

static int mir_match_fixed_call_reduction_report(
    struct MirFixedCallReductionReport *plan)
{
    static const int expected_opcodes[46] = {
        MIR_LABEL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST,
        MIR_STORE, MIR_LABEL, MIR_PHI, MIR_PHI, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP,
        MIR_UNARY, MIR_ARG, MIR_CALL, MIR_BINARY, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_ARG,
        MIR_NOP, MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN
    };
    const struct MirInsn *sum_phi = &mir.insns[8];
    const struct MirInsn *index_phi = &mir.insns[9];
    const struct MirInsn *loop_call = &mir.insns[19];
    const struct MirInsn *first_report_call = &mir.insns[35];
    const struct MirInsn *second_report_call = &mir.insns[39];
    const struct MirInsn *print_call = &mir.insns[43];
    int call_argument;
    int print_arguments[4];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 46 || mir_cfg_block_count() != 4 ||
        mir.has_vla || type_size(mir.return_type) != 2)
        return mir_machine_reject("fixed-call-reduction-report", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "fixed-call-reduction-report", "opcode");
    if (!mir_machine_constant_equals(mir.insns[1].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[3]) ||
        !mir_machine_constant_equals(mir.insns[5].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[6]) ||
        sum_phi->src1 != mir.insns[1].dst ||
        sum_phi->src2 != mir.insns[20].dst ||
        sum_phi->phi_pred1 != mir.insns[0].label ||
        sum_phi->phi_pred2 != mir.insns[23].label ||
        index_phi->src1 != mir.insns[5].dst ||
        index_phi->src2 != mir.insns[26].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[23].label ||
        mir.insns[13].immediate != TOK_LE ||
        mir.insns[13].src1 != mir.insns[12].dst ||
        mir.insns[13].src2 != mir.insns[11].dst ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        mir.insns[14].label != mir.insns[29].label)
        return mir_machine_reject("fixed-call-reduction-report", "loop");
    plan->last_index = (int)mir.insns[11].immediate;
    if (plan->last_index < 0 || plan->last_index >= 255 ||
        mir.insns[17].src1 != index_phi->dst ||
        !mir_machine_single_call_argument(loop_call, &call_argument) ||
        call_argument != mir.insns[17].dst ||
        mir.insns[20].immediate != '+' ||
        mir.insns[20].src1 != sum_phi->dst ||
        mir.insns[20].src2 != loop_call->dst ||
        !mir_machine_unobservable_local_store(&mir.insns[22]) ||
        !mir_machine_constant_equals(mir.insns[25].dst, 1) ||
        mir.insns[26].immediate != '+' ||
        mir.insns[26].src1 != index_phi->dst ||
        mir.insns[26].src2 != mir.insns[25].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[27]) ||
        mir.insns[28].label != mir.insns[7].label)
        return mir_machine_reject("fixed-call-reduction-report", "body");
    plan->function = find_global(loop_call->name);
    if (plan->function == NULL || !plan->function->is_defined ||
        plan->function->is_funcptr ||
        strcmp(loop_call->name, first_report_call->name) ||
        strcmp(loop_call->name, second_report_call->name) ||
        !mir_machine_single_call_argument(
            first_report_call, &call_argument) ||
        call_argument != mir.insns[33].dst ||
        !mir_machine_single_call_argument(
            second_report_call, &call_argument) ||
        call_argument != mir.insns[37].dst)
        return mir_machine_reject("fixed-call-reduction-report", "calls");
    plan->report_values[0] = (int)mir.insns[33].immediate;
    plan->report_values[1] = (int)mir.insns[37].immediate;
    if (!mir_machine_four_call_arguments(print_call, print_arguments) ||
        print_arguments[0] != mir.insns[30].dst ||
        print_arguments[1] != first_report_call->dst ||
        print_arguments[2] != second_report_call->dst ||
        print_arguments[3] != sum_phi->dst ||
        strcmp(print_call->name, "printf") ||
        (print_call->memory_flags & MIR_CALL_FLAG_VARIADIC) == 0 ||
        !mir_machine_constant_equals(mir.insns[44].dst, 0) ||
        mir.insns[45].src1 != mir.insns[44].dst)
        return mir_machine_reject("fixed-call-reduction-report", "report");
    plan->print_function = find_global(print_call->name);
    plan->string_id = (int)mir.insns[30].immediate;
    if (plan->print_function == NULL || plan->print_function->is_defined)
        return mir_machine_reject("fixed-call-reduction-report", "print");
    return 1;
}

static void mir_emit_fixed_call_reduction_report(
    MirStream *out, const struct MirFixedCallReductionReport *plan)
{
    int loop = new_label();

    mir_stream_printf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n\tdec sp\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\tld iy,0\n\tld (ix-1),0\n", out);
    mir_stream_printf(out, "L%d:\n", loop);
    mir_stream_puts("\tld l,(ix-1)\n\tld h,0\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->function);
    mir_stream_puts("\tpop bc\n\tpush iy\n\tpop de\n\tadd hl,de\n"
          "\tpush hl\n\tpop iy\n\tinc (ix-1)\n\tld a,(ix-1)\n", out);
    mir_stream_printf(out, "\tcp %d\n\tjp c,L%d\n\tpush iy\n",
            plan->last_index + 1, loop);
    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n", plan->report_values[1]);
    mir_machine_emit_symbol_call(out, plan->function);
    mir_stream_puts("\tpop bc\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n", plan->report_values[0]);
    mir_machine_emit_symbol_call(out, plan->function);
    mir_stream_puts("\tpop bc\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", plan->string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tld hl,0\n\tld sp,ix\n\tpop ix\n\tpop iy\n"
          ";@dcc.reg free=iy\n\tret\n", out);
}

static int mir_match_string_putchar_loop(
    struct MirStringPutcharLoop *plan)
{
    static const int expected_opcodes[44] = {
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_STRING_ADDRESS, MIR_STORE, MIR_STRING_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_NOP,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_LABEL, MIR_PHI, MIR_LOAD, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BRANCH_FALSE,
        MIR_LOAD, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_LABEL, MIR_CONST, MIR_RETURN
    };
    const struct MirInsn *call = &mir.insns[35];
    int call_argument;
    long numerator;
    long denominator;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 44 || mir_cfg_block_count() != 7 ||
        mir.has_vla || type_size(mir.return_type) != 2)
        return mir_machine_reject("string-putchar-loop", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject("string-putchar-loop", "opcode");
    numerator = (long)((unsigned long)mir.insns[2].immediate & 0xffffUL);
    denominator = (long)((unsigned long)mir.insns[3].immediate & 0xffffUL);
    if (numerator >= 32768)
        numerator -= 65536;
    if (denominator >= 32768)
        denominator -= 65536;
    if (mir.insns[2].opcode != MIR_CONST ||
        mir.insns[3].opcode != MIR_CONST ||
        mir.insns[4].immediate != '/' ||
        mir.insns[4].src1 != mir.insns[2].dst ||
        mir.insns[4].src2 != mir.insns[3].dst ||
        denominator == 0 || numerator / denominator != -1 ||
        !mir_machine_unobservable_local_store(&mir.insns[5]) ||
        mir.insns[5].src1 != mir.insns[4].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[7]) ||
        mir.insns[7].src1 != mir.insns[6].dst ||
        !mir_machine_constant_equals(mir.insns[9].dst, 1) ||
        mir.insns[10].src1 != mir.insns[8].dst ||
        mir.insns[10].src2 != mir.insns[9].dst ||
        mir.insns[10].immediate != 1 ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        (mir.insns[11].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_constant_equals(mir.insns[12].dst, 0) ||
        mir.insns[14].immediate != '<' ||
        mir.insns[14].src1 != mir.insns[11].dst ||
        mir.insns[14].src2 != mir.insns[12].dst ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        mir.insns[15].label != mir.insns[18].label ||
        mir.insns[17].label != mir.insns[41].label)
        return mir_machine_reject("string-putchar-loop", "condition");
    if (mir.insns[20].src1 != mir.insns[4].dst ||
        mir.insns[20].src2 != mir.insns[24].dst ||
        mir.insns[20].phi_pred1 != mir.insns[18].label ||
        mir.insns[20].phi_pred2 != mir.insns[37].label ||
        !mir_machine_same_location(&mir.insns[7], &mir.insns[21]) ||
        !mir_machine_constant_equals(mir.insns[23].dst, 1) ||
        mir.insns[24].immediate != '+' ||
        mir.insns[24].src1 != mir.insns[20].dst ||
        mir.insns[24].src2 != mir.insns[23].dst ||
        !mir_machine_same_location(&mir.insns[5], &mir.insns[25]) ||
        mir.insns[25].src1 != mir.insns[24].dst ||
        mir.insns[26].src1 != mir.insns[21].dst ||
        mir.insns[26].src2 != mir.insns[24].dst ||
        mir.insns[26].immediate != 1 ||
        mir.insns[27].src1 != mir.insns[26].dst ||
        mir.insns[28].src1 != mir.insns[27].dst ||
        mir.insns[28].label != mir.insns[39].label)
        return mir_machine_reject("string-putchar-loop", "loop");
    if (!mir_machine_same_location(&mir.insns[7], &mir.insns[29]) ||
        mir.insns[31].src1 != mir.insns[29].dst ||
    mir.insns[31].src2 != mir.insns[24].dst ||
        mir.insns[31].immediate != 1 ||
        mir.insns[32].src1 != mir.insns[31].dst ||
        mir.insns[33].src1 != mir.insns[32].dst ||
        !mir_machine_single_call_argument(call, &call_argument) ||
        call_argument != mir.insns[33].dst ||
        mir.insns[38].label != mir.insns[19].label ||
        !mir_machine_constant_equals(mir.insns[42].dst, 0) ||
        mir.insns[43].src1 != mir.insns[42].dst)
        return mir_machine_reject("string-putchar-loop", "call");
    plan->function = find_global(call->name);
    if (plan->function == NULL || plan->function->storage != SC_FUNC ||
        plan->function->is_funcptr ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0)
        return mir_machine_reject("string-putchar-loop", "function");
    plan->condition_string_id = (int)mir.insns[8].immediate;
    plan->string_id = (int)mir.insns[6].immediate;
    return 1;
}

static void mir_emit_string_putchar_loop(
    MirStream *out, const struct MirStringPutcharLoop *plan)
{
    int loop = new_label();
    int done = new_label();

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tdec sp\n\tdec sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,S%d\n\tld (ix-2),l\n\tld (ix-1),h\n"
            "\tld a,(S%d+1)\n\tand 128\n\tjp nz,L%d\n"
            "L%d:\n\tld l,(ix-2)\n\tld h,(ix-1)\n"
            "\tld a,(hl)\n\tor a\n\tjp z,L%d\n"
            "\tinc hl\n\tld (ix-2),l\n\tld (ix-1),h\n"
            "\tld l,a\n\trlca\n\tsbc a,a\n\tld h,a\n\tpush hl\n",
            plan->string_id, plan->condition_string_id,
            done, loop, done);
    mir_machine_emit_symbol_call(out, plan->function);
    mir_stream_puts("\tpop bc\n", out);
    mir_stream_printf(out,
            "\tjp L%d\nL%d:\n\tld hl,0\n"
            "\tld sp,ix\n\tpop ix\n\tret\n",
            loop, done);
}

static int mir_match_fixed_allocation_runner(
    struct MirFixedAllocationRunner *plan)
{
    static const int expected_opcodes[61] = {
        MIR_LABEL, MIR_CONST, MIR_NOP, MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_UNARY, MIR_STORE, MIR_LOAD, MIR_UNARY,
        MIR_BRANCH_FALSE, MIR_CONST, MIR_RETURN, MIR_LABEL, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_CONST, MIR_NOP, MIR_ARG, MIR_CONST, MIR_NOP,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE_INDIRECT, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_RETURN, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_STORE,
        MIR_LABEL, MIR_PHI, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_CALL, MIR_CALL, MIR_NOP, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN
    };
    const struct MirInsn *first_call = &mir.insns[7];
    const struct MirInsn *second_call = &mir.insns[25];
    const struct MirInsn *index_phi = &mir.insns[40];
    int first_arguments[2];
    int second_arguments[2];
    int memory_type, memory_storage, memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 61 || mir_cfg_block_count() != 6 ||
        mir.has_vla || type_size(mir.return_type) != 2)
        return mir_machine_reject("fixed-allocation-runner", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "fixed-allocation-runner", "opcode");
    if (!mir_machine_two_call_arguments(
            first_call, first_arguments) ||
        first_arguments[0] != mir.insns[1].dst ||
        first_arguments[1] != mir.insns[4].dst ||
        !mir_machine_two_call_arguments(
            second_call, second_arguments) ||
        second_arguments[0] != mir.insns[19].dst ||
        second_arguments[1] != mir.insns[22].dst ||
        strcmp(first_call->name, second_call->name) ||
        (first_call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0 ||
        (second_call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0)
        return mir_machine_reject("fixed-allocation-runner", "allocations");
    plan->allocation_counts[0] = (int)mir.insns[1].immediate;
    plan->allocation_sizes[0] = (int)mir.insns[4].immediate;
    plan->allocation_counts[1] = (int)mir.insns[19].immediate;
    plan->allocation_sizes[1] = (int)mir.insns[22].immediate;
    if (plan->allocation_counts[0] <= 0 ||
        plan->allocation_sizes[0] <= 0 ||
        plan->allocation_counts[1] <= 0 ||
        plan->allocation_sizes[1] <= 0 ||
        mir.insns[9].src1 != first_call->dst ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        !mir_machine_named_nonvolatile(&mir.insns[10]) ||
        !mir_machine_same_location(&mir.insns[10], &mir.insns[11]) ||
        mir.insns[12].immediate != '!' ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[13].src1 != mir.insns[12].dst ||
        mir.insns[13].label != mir.insns[16].label ||
        !mir_machine_constant_equals(mir.insns[14].dst, 1) ||
        mir.insns[15].src1 != mir.insns[14].dst)
        return mir_machine_reject("fixed-allocation-runner", "first-result");
    plan->allocator = find_global(first_call->name);
    plan->state = find_global(mir.insns[10].name);
    if (plan->allocator == NULL ||
        plan->allocator->storage != SC_FUNC ||
        plan->allocator->is_funcptr || plan->allocator->is_noreturn ||
        plan->state == NULL || plan->state->is_volatile ||
        !mir_machine_same_location(&mir.insns[10], &mir.insns[17]) ||
        mir.insns[18].src1 != mir.insns[17].dst ||
        mir.insns[27].src1 != mir.insns[18].dst ||
        mir.insns[27].src2 != second_call->dst ||
        mir.insns[27].memory_size != 2 ||
        (mir.insns[27].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_same_location(&mir.insns[10], &mir.insns[28]) ||
        mir.insns[29].src1 != mir.insns[28].dst ||
        mir.insns[29].immediate != mir.insns[18].immediate ||
        mir.insns[30].src1 != mir.insns[29].dst ||
        mir.insns[30].memory_size != 2 ||
        mir.insns[31].immediate != '!' ||
        mir.insns[31].src1 != mir.insns[30].dst ||
        mir.insns[32].src1 != mir.insns[31].dst ||
        mir.insns[32].label != mir.insns[35].label ||
        !mir_machine_constant_equals(mir.insns[33].dst, 1) ||
        mir.insns[34].src1 != mir.insns[33].dst)
        return mir_machine_reject("fixed-allocation-runner", "state");
    plan->state_member_offset = (int)mir.insns[18].immediate;
    if (!mir_machine_constant_equals(mir.insns[37].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[38]) ||
        index_phi->src1 != mir.insns[37].dst ||
        index_phi->src2 != mir.insns[52].dst ||
        index_phi->phi_pred1 != mir.insns[35].label ||
        index_phi->phi_pred2 != mir.insns[49].label ||
        mir.insns[43].immediate != 0 ||
        mir.insns[43].src1 != index_phi->dst ||
        mir.insns[44].immediate != '<' ||
        mir.insns[44].src1 != mir.insns[43].dst ||
        mir.insns[44].src2 != mir.insns[42].dst ||
        mir.insns[45].src1 != mir.insns[44].dst ||
        mir.insns[45].label != mir.insns[55].label ||
        !mir_machine_call_has_no_arguments(&mir.insns[46]) ||
        !mir_machine_call_has_no_arguments(&mir.insns[47]) ||
        !mir_machine_constant_equals(mir.insns[51].dst, 1) ||
        mir.insns[52].immediate != '+' ||
        mir.insns[52].src1 != index_phi->dst ||
        mir.insns[52].src2 != mir.insns[51].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[53]) ||
        mir.insns[53].src1 != mir.insns[52].dst ||
        mir.insns[54].label != mir.insns[39].label)
        return mir_machine_reject("fixed-allocation-runner", "loop");
    plan->iterations = (int)mir.insns[42].immediate;
    if (plan->iterations <= 0 || plan->iterations > 32767)
        return mir_machine_reject("fixed-allocation-runner", "iterations");
    for (instruction = 0; instruction < 2; ++instruction) {
        const struct MirInsn *call = &mir.insns[46 + instruction];

        plan->tests[instruction] = find_global(call->name);
        if (plan->tests[instruction] == NULL ||
            !plan->tests[instruction]->is_defined ||
            plan->tests[instruction]->is_funcptr ||
            (call->memory_flags &
             (MIR_CALL_FLAG_VARIADIC |
              MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0)
            return mir_machine_reject("fixed-allocation-runner", "tests");
    }
    if (mir.insns[56].opcode != MIR_STRING_ADDRESS ||
        mir.insns[57].src1 != mir.insns[56].dst ||
        !mir_machine_single_call_argument(
            &mir.insns[58], &memory_offset) ||
        memory_offset != mir.insns[56].dst ||
        strcmp(mir.insns[58].name, "printf") ||
        (mir.insns[58].memory_flags & MIR_CALL_FLAG_VARIADIC) == 0 ||
        !mir_machine_constant_equals(mir.insns[59].dst, 0) ||
        mir.insns[60].src1 != mir.insns[59].dst)
        return mir_machine_reject("fixed-allocation-runner", "print");
    plan->print_function = find_global(mir.insns[58].name);
    plan->string_id = (int)mir.insns[56].immediate;
    if (plan->print_function == NULL ||
        plan->print_function->is_defined ||
        !mir_scalar_memory_location(
            &mir.insns[10], &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL || memory_offset != 0)
        return mir_machine_reject("fixed-allocation-runner", "symbols");
    return 1;
}

static void mir_emit_fixed_allocation_runner(
    MirStream *out, const struct MirFixedAllocationRunner *plan)
{
    int allocation_failed = new_label();
    int loop = new_label();
    int finish = new_label();

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n\tdec sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tpush hl\n\tld hl,%d\n\tpush hl\n",
            plan->allocation_sizes[0], plan->allocation_counts[0]);
    mir_machine_emit_symbol_call(out, plan->allocator);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_global_word_store(out, plan->state, 0);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", allocation_failed);
    mir_stream_printf(out,
            "\tld hl,%d\n\tpush hl\n\tld hl,%d\n\tpush hl\n",
            plan->allocation_sizes[1], plan->allocation_counts[1]);
    mir_machine_emit_symbol_call(out, plan->allocator);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpush hl\n", out);
    mir_machine_emit_global_word(out, plan->state, 0);
    mir_machine_emit_hl_offset(out, plan->state_member_offset, 0);
    mir_stream_puts("\tpop de\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n"
          "\tld a,d\n\tor e\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n\tld (ix-1),0\nL%d:\n",
            allocation_failed, loop);
    mir_machine_emit_symbol_call(out, plan->tests[0]);
    mir_machine_emit_symbol_call(out, plan->tests[1]);
    mir_stream_puts("\tinc (ix-1)\n\tld a,(ix-1)\n", out);
    mir_stream_printf(out, "\tcp %d\n\tjp c,L%d\n\tld hl,S%d\n\tpush hl\n",
            plan->iterations, loop, plan->string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_stream_puts("\tpop bc\n\tld hl,0\n", out);
    mir_stream_printf(out, "\tjp L%d\n", finish);
    mir_stream_printf(out,
            "L%d:\n\tld hl,1\nL%d:\n"
            "\tld sp,ix\n\tpop ix\n\tret\n",
            allocation_failed, finish);
}

static int mir_match_float_modulo_normalize(
    struct MirFloatModuloNormalize *plan)
{
    static const int expected_opcodes[20] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_ARG, MIR_FLOAT_CONST, MIR_ARG,
        MIR_CALL, MIR_STORE, MIR_NOP, MIR_FLOAT_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_FLOAT_CONST, MIR_BINARY, MIR_NOP,
        MIR_STORE, MIR_LABEL, MIR_LOAD, MIR_RETURN
    };
    const struct MirInsn *parameter = &mir.insns[1];
    const struct MirInsn *call = &mir.insns[6];
    int arguments[2];
    int memory_type, memory_storage, memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 20 || mir_cfg_block_count() != 2 ||
        mir.has_vla || !type_is_float(mir.return_type) ||
        type_size(mir.return_type) != 4 ||
        !type_is_float(parameter->type) ||
        type_size(parameter->type) != 4)
        return mir_machine_reject("float-modulo-normalize", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "float-modulo-normalize", "opcode");
    if (!mir_machine_two_call_arguments(call, arguments) ||
        arguments[0] != parameter->dst ||
        arguments[1] != mir.insns[4].dst ||
        !type_is_float(call->type) || type_size(call->type) != 4 ||
        !mir_machine_unobservable_local_store(&mir.insns[7]) ||
        mir.insns[7].src1 != call->dst ||
        (mir.insns[7].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject("float-modulo-normalize", "call");
    plan->one_bits =
        (unsigned long)mir.insns[4].immediate & 0xffffffffUL;
    if (plan->one_bits !=
            ((unsigned long)mir.insns[13].immediate & 0xffffffffUL) ||
        ((unsigned long)mir.insns[9].immediate & 0xffffffffUL) != 0 ||
        mir.insns[10].immediate != '<' ||
        mir.insns[10].src1 != call->dst ||
        mir.insns[10].src2 != mir.insns[9].dst ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[11].label != mir.insns[17].label ||
        mir.insns[14].immediate != '+' ||
        mir.insns[14].src1 != call->dst ||
        mir.insns[14].src2 != mir.insns[13].dst ||
        !mir_machine_same_location(&mir.insns[7], &mir.insns[16]) ||
        mir.insns[16].src1 != mir.insns[14].dst ||
        !mir_machine_same_location(&mir.insns[7], &mir.insns[18]) ||
        mir.insns[19].src1 != mir.insns[18].dst)
        return mir_machine_reject("float-modulo-normalize", "flow");
    plan->function = find_global(call->name);
    if (plan->function == NULL || plan->function->storage != SC_FUNC ||
        plan->function->is_funcptr || plan->function->is_noreturn ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0 ||
        !mir_scalar_memory_location(
            parameter, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2)
        return mir_machine_reject("float-modulo-normalize", "function");
    plan->parameter_stack_offset = memory_offset - 2;
    return 1;
}

static void mir_emit_float_modulo_normalize(
    MirStream *out, const struct MirFloatModuloNormalize *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_float_bits(out, plan->one_bits);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_emit_wide_parameter(out, plan->parameter_stack_offset + 4);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tpush de\n\tpush hl\n\tld hl,0\n\tld de,0\n", out);
    mir_emit_runtime_call(out, "__fgtf");
    mir_stream_puts("\tld a,h\n\tor l\n\tpop hl\n\tpop de\n\tret z\n"
          "\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->one_bits);
    mir_emit_runtime_call(out, "__faf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tret\n", out);
}

static int mir_match_noarg_test_runner(struct MirNoArgTestRunner *plan)
{
    const struct MirInsn *counts_call;
    const struct MirInsn *result_call;
    int count_arguments[3];
    int result_arguments[2];
    int memory_type, memory_storage, memory_offset;
    int base;
    int test;

    memset(plan, 0, sizeof(*plan));
    if (mir.count < 36 || mir_cfg_block_count() != 9 ||
        mir.has_vla || type_size(mir.return_type) != 2 ||
        mir.insns[0].opcode != MIR_LABEL)
        return mir_machine_reject("noarg-test-runner", "shape");
    for (test = 1; test < mir.count &&
         mir.insns[test].opcode == MIR_CALL; ++test) {
        struct Sym *function;

        if (test > 12 ||
            !mir_machine_call_has_no_arguments(&mir.insns[test]) ||
            (mir.insns[test].type & 15) != TYPE_VOID ||
            (mir.insns[test].memory_flags &
             (MIR_CALL_FLAG_VARIADIC |
              MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0)
            return mir_machine_reject("noarg-test-runner", "test-call");
        function = find_global(mir.insns[test].name);
        if (function == NULL || !function->is_defined ||
            function->is_funcptr || function->is_noreturn)
            return mir_machine_reject("noarg-test-runner", "test-symbol");
        plan->tests[plan->test_count++] = function;
    }
    base = 1 + plan->test_count;
    if (plan->test_count == 0 || base + 34 != mir.count)
        return mir_machine_reject("noarg-test-runner", "test-count");
    if (mir.insns[base].opcode != MIR_STRING_ADDRESS ||
        mir.insns[base + 1].opcode != MIR_ARG ||
        mir.insns[base + 1].src1 != mir.insns[base].dst ||
        mir.insns[base + 2].opcode != MIR_LOAD ||
        mir.insns[base + 3].opcode != MIR_ARG ||
        mir.insns[base + 3].src1 != mir.insns[base + 2].dst ||
        mir.insns[base + 4].opcode != MIR_LOAD ||
        mir.insns[base + 5].opcode != MIR_ARG ||
        mir.insns[base + 5].src1 != mir.insns[base + 4].dst ||
        mir.insns[base + 6].opcode != MIR_CALL)
        return mir_machine_reject("noarg-test-runner", "counts-shape");
    counts_call = &mir.insns[base + 6];
    if (!mir_machine_three_call_arguments(
            counts_call, count_arguments) ||
        count_arguments[0] != mir.insns[base].dst ||
        count_arguments[1] != mir.insns[base + 2].dst ||
        count_arguments[2] != mir.insns[base + 4].dst ||
        strcmp(counts_call->name, "printf") ||
        (counts_call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != MIR_CALL_FLAG_VARIADIC)
        return mir_machine_reject("noarg-test-runner", "counts-call");
    if (!mir_machine_named_nonvolatile(&mir.insns[base + 2]) ||
        !mir_scalar_memory_location(
            &mir.insns[base + 2], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL || type_size(memory_type) != 2)
        return mir_machine_reject("noarg-test-runner", "checks");
    plan->checks = find_global(mir.insns[base + 2].name);
    plan->checks_offset = memory_offset;
    if (!mir_machine_named_nonvolatile(&mir.insns[base + 4]) ||
        !mir_scalar_memory_location(
            &mir.insns[base + 4], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL || type_size(memory_type) != 2)
        return mir_machine_reject("noarg-test-runner", "failures");
    plan->failures = find_global(mir.insns[base + 4].name);
    plan->failures_offset = memory_offset;
    if (plan->checks == NULL || plan->checks->is_volatile ||
        plan->failures == NULL || plan->failures->is_volatile)
        return mir_machine_reject("noarg-test-runner", "globals");
    if (mir.insns[base + 7].opcode != MIR_STRING_ADDRESS ||
        mir.insns[base + 8].opcode != MIR_ARG ||
        mir.insns[base + 8].src1 != mir.insns[base + 7].dst ||
        !mir_machine_same_location(
            &mir.insns[base + 4], &mir.insns[base + 9]) ||
        !mir_machine_constant_equals(mir.insns[base + 10].dst, 0) ||
        mir.insns[base + 11].opcode != MIR_BINARY ||
        mir.insns[base + 11].immediate != TOK_EQ ||
        mir.insns[base + 11].src1 != mir.insns[base + 9].dst ||
        mir.insns[base + 11].src2 != mir.insns[base + 10].dst ||
        mir.insns[base + 12].opcode != MIR_BRANCH_FALSE ||
        mir.insns[base + 12].src1 != mir.insns[base + 11].dst ||
        mir.insns[base + 12].label != mir.insns[base + 16].label ||
        mir.insns[base + 13].opcode != MIR_STRING_ADDRESS ||
        mir.insns[base + 15].opcode != MIR_JUMP ||
        mir.insns[base + 15].label != mir.insns[base + 19].label ||
        mir.insns[base + 17].opcode != MIR_STRING_ADDRESS ||
        !mir_machine_phi_merge(
            base + 20, base + 13, base + 17, base + 14, base + 18) ||
        mir.insns[base + 21].opcode != MIR_ARG ||
        mir.insns[base + 21].src1 != mir.insns[base + 20].dst ||
        mir.insns[base + 22].opcode != MIR_CALL)
        return mir_machine_reject("noarg-test-runner", "result-shape");
    result_call = &mir.insns[base + 22];
    if (!mir_machine_two_call_arguments(
            result_call, result_arguments) ||
        result_arguments[0] != mir.insns[base + 7].dst ||
        result_arguments[1] != mir.insns[base + 20].dst ||
        strcmp(result_call->name, "printf") ||
        (result_call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != MIR_CALL_FLAG_VARIADIC)
        return mir_machine_reject("noarg-test-runner", "result-call");
    if (!mir_machine_same_location(
            &mir.insns[base + 4], &mir.insns[base + 23]) ||
        mir.insns[base + 24].opcode != MIR_BRANCH_FALSE ||
        mir.insns[base + 24].src1 != mir.insns[base + 23].dst ||
        !mir_machine_constant_equals(mir.insns[base + 25].dst, 1) ||
        mir.insns[base + 27].opcode != MIR_JUMP ||
        mir.insns[base + 27].label != mir.insns[base + 31].label ||
        !mir_machine_constant_equals(mir.insns[base + 29].dst, 0) ||
        !mir_machine_phi_merge(
            base + 32, base + 25, base + 29, base + 26, base + 30) ||
        mir.insns[base + 33].opcode != MIR_RETURN ||
        mir.insns[base + 33].src1 != mir.insns[base + 32].dst)
        return mir_machine_reject("noarg-test-runner", "return");
    plan->print_function = find_global(counts_call->name);
    if (plan->print_function == NULL ||
        plan->print_function->is_defined ||
        plan->print_function != find_global(result_call->name))
        return mir_machine_reject("noarg-test-runner", "print-symbol");
    plan->counts_string_id = (int)mir.insns[base].immediate;
    plan->result_string_id = (int)mir.insns[base + 7].immediate;
    plan->pass_string_id = (int)mir.insns[base + 13].immediate;
    plan->fail_string_id = (int)mir.insns[base + 17].immediate;
    return 1;
}

static void mir_emit_noarg_test_runner(
    MirStream *out, const struct MirNoArgTestRunner *plan)
{
    int failed = new_label();
    int selected = new_label();
    int test;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (test = 0; test < plan->test_count; ++test)
        mir_machine_emit_symbol_call(out, plan->tests[test]);
    mir_machine_emit_global_word(
        out, plan->failures, plan->failures_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_global_word(
        out, plan->checks, plan->checks_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->counts_string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_global_word(
        out, plan->failures, plan->failures_offset);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out,
            "\tjp nz,L%d\n\tld hl,S%d\n\tjp L%d\n"
            "L%d:\n\tld hl,S%d\nL%d:\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            failed, plan->pass_string_id, selected,
            failed, plan->fail_string_id, selected,
            plan->result_string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_global_word(
        out, plan->failures, plan->failures_offset);
    mir_stream_puts("\tld a,h\n\tor l\n\tld hl,0\n\tret z\n\tinc hl\n\tret\n",
          out);
}

static int mir_match_string_check_report(struct MirStringCheckReport *plan)
{
    static const int expected_opcodes[47] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_LOAD, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_LABEL
    };
    const struct MirInsn *name = &mir.insns[1];
    const struct MirInsn *got = &mir.insns[2];
    const struct MirInsn *want = &mir.insns[3];
    const struct MirInsn *checks_load = &mir.insns[4];
    const struct MirInsn *checks_store = &mir.insns[7];
    const struct MirInsn *compare_call = &mir.insns[20];
    const struct MirInsn *failures_load = &mir.insns[36];
    const struct MirInsn *failures_store = &mir.insns[39];
    const struct MirInsn *print_call = &mir.insns[44];
    int compare_arguments[2];
    int print_arguments[2];
    int memory_type, memory_storage, memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 47 || mir_cfg_block_count() != 9 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return mir_machine_reject("string-check-report", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject("string-check-report", "opcode");
    if (type_ptr_depth(name->type) == 0 ||
        type_ptr_depth(got->type) == 0 ||
        type_ptr_depth(want->type) == 0 ||
        !mir_machine_parameter_value_offset(
            name->dst, &plan->name_stack_offset) ||
        !mir_machine_parameter_value_offset(
            got->dst, &plan->got_stack_offset) ||
        !mir_machine_parameter_value_offset(
            want->dst, &plan->want_stack_offset))
        return mir_machine_reject("string-check-report", "parameters");
    if (!mir_machine_named_nonvolatile(checks_load) ||
        !mir_machine_same_location(checks_load, checks_store) ||
        !mir_scalar_memory_location(
            checks_load, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL || type_size(memory_type) != 2 ||
        !mir_machine_constant_equals(mir.insns[5].dst, 1) ||
        mir.insns[6].immediate != '+' ||
        mir.insns[6].src1 != checks_load->dst ||
        mir.insns[6].src2 != mir.insns[5].dst ||
        checks_store->src1 != mir.insns[6].dst ||
        checks_store->memory_size != 2)
        return mir_machine_reject("string-check-report", "checks");
    plan->checks = find_global(checks_load->name);
    plan->checks_offset = memory_offset;
    if (plan->checks == NULL || plan->checks->is_volatile ||
        !mir_machine_same_location(got, &mir.insns[8]) ||
        !mir_machine_constant_equals(mir.insns[9].dst, 0) ||
        mir.insns[10].immediate != TOK_EQ ||
        mir.insns[10].src1 != mir.insns[8].dst ||
        mir.insns[10].src2 != mir.insns[9].dst ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[11].label != mir.insns[15].label ||
        !mir_machine_constant_equals(mir.insns[13].dst, 1) ||
        mir.insns[14].label != mir.insns[33].label)
        return mir_machine_reject("string-check-report", "null-check");
    if (!mir_machine_same_location(got, &mir.insns[16]) ||
        !mir_machine_same_location(want, &mir.insns[18]) ||
        !mir_machine_two_call_arguments(
            compare_call, compare_arguments) ||
        compare_arguments[0] != mir.insns[16].dst ||
        compare_arguments[1] != mir.insns[18].dst ||
        !mir_machine_constant_equals(mir.insns[21].dst, 0) ||
        mir.insns[22].immediate != TOK_NE ||
        mir.insns[22].src1 != compare_call->dst ||
        mir.insns[22].src2 != mir.insns[21].dst ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[23].label != mir.insns[27].label ||
        !mir_machine_boolean_merge(30, 25, 28, 24, 27) ||
        mir.insns[26].label != mir.insns[29].label ||
        mir.insns[32].label != mir.insns[33].label ||
        !mir_machine_phi_merge(34, 13, 30, 12, 31) ||
        mir.insns[35].src1 != mir.insns[34].dst ||
        mir.insns[35].label != mir.insns[46].label)
        return mir_machine_reject("string-check-report", "compare");
    plan->compare_function = find_global(compare_call->name);
    if (plan->compare_function == NULL ||
        plan->compare_function->is_defined ||
        plan->compare_function->is_funcptr ||
        (compare_call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0)
        return mir_machine_reject("string-check-report", "compare-symbol");
    if (!mir_machine_named_nonvolatile(failures_load) ||
        !mir_machine_same_location(failures_load, failures_store) ||
        !mir_scalar_memory_location(
            failures_load, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL || type_size(memory_type) != 2 ||
        !mir_machine_constant_equals(mir.insns[37].dst, 1) ||
        mir.insns[38].immediate != '+' ||
        mir.insns[38].src1 != failures_load->dst ||
        mir.insns[38].src2 != mir.insns[37].dst ||
        failures_store->src1 != mir.insns[38].dst ||
        failures_store->memory_size != 2 ||
        !mir_machine_same_location(name, &mir.insns[42]) ||
        !mir_machine_two_call_arguments(print_call, print_arguments) ||
        print_arguments[0] != mir.insns[40].dst ||
        print_arguments[1] != mir.insns[42].dst ||
        strcmp(print_call->name, "printf") ||
        (print_call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != MIR_CALL_FLAG_VARIADIC)
        return mir_machine_reject("string-check-report", "failure");
    plan->failures = find_global(failures_load->name);
    plan->print_function = find_global(print_call->name);
    plan->failures_offset = memory_offset;
    plan->string_id = (int)mir.insns[40].immediate;
    if (plan->failures == NULL || plan->failures->is_volatile ||
        plan->print_function == NULL || plan->print_function->is_defined)
        return mir_machine_reject("string-check-report", "symbols");
    return 1;
}

static void mir_emit_string_check_report(
    MirStream *out, const struct MirStringCheckReport *plan)
{
    int failure = new_label();
    int done = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_word(
        out, plan->checks, plan->checks_offset);
    mir_stream_puts("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->checks, plan->checks_offset);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n"
            "\tjp z,L%d\n",
            plan->got_stack_offset, failure);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n",
            plan->want_stack_offset);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n",
            plan->got_stack_offset + 2);
    mir_machine_emit_symbol_call(out, plan->compare_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\nL%d:\n", done, failure);
    mir_machine_emit_global_word(
        out, plan->failures, plan->failures_offset);
    mir_stream_puts("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->failures, plan->failures_offset);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->name_stack_offset, plan->string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_stream_printf(out, "L%d:\n\tret\n", done);
}

static int mir_match_sequential_scalar_call_report(
    struct MirSequentialScalarCallReport *plan)
{
    const struct MirInsn *print_call;
    int print_arguments[9] = { 0 };
    int print_argument_count = 0;
    int instruction;
    int call_index;

    memset(plan, 0, sizeof(*plan));
    if (mir.count < 10 || (mir.count - 6) % 4 != 0 ||
        mir_cfg_block_count() != 1 || mir.has_vla ||
        type_size(mir.return_type) != 2 ||
        type_ptr_depth(mir.return_type) != 0 ||
        mir.insns[0].opcode != MIR_LABEL ||
        mir.insns[1].opcode != MIR_STRING_ADDRESS ||
        mir.insns[2].opcode != MIR_ARG ||
        mir.insns[2].src1 != mir.insns[1].dst)
        return mir_machine_reject(
            "sequential-scalar-call-report", "shape");
    plan->call_count = (mir.count - 6) / 4;
    if (plan->call_count < 2 || plan->call_count > 8)
        return mir_machine_reject(
            "sequential-scalar-call-report", "call-count");
    for (call_index = 0; call_index < plan->call_count; ++call_index) {
        int base = 3 + call_index * 4;
        const struct MirInsn *constant = &mir.insns[base];
        const struct MirInsn *argument = &mir.insns[base + 1];
        const struct MirInsn *call = &mir.insns[base + 2];
        const struct MirInsn *result_argument = &mir.insns[base + 3];
        struct Sym *function;
        int call_argument;
        long value;

        if (constant->opcode != MIR_CONST ||
            !mir_machine_constant_value(constant->dst, &value, 0) ||
            value < -32768 || value > 65535 ||
            argument->opcode != MIR_ARG ||
            argument->src1 != constant->dst ||
            call->opcode != MIR_CALL ||
            !mir_machine_single_call_argument(call, &call_argument) ||
            call_argument != constant->dst ||
            type_size(call->type) != 2 ||
            type_ptr_depth(call->type) != 0 ||
            result_argument->opcode != MIR_ARG ||
            result_argument->src1 != call->dst ||
            (call->memory_flags &
             (MIR_CALL_FLAG_VARIADIC |
              MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0)
            return mir_machine_reject(
                "sequential-scalar-call-report", "call");
        function = find_global(call->name);
        if (function == NULL || !function->is_defined ||
            function->is_funcptr || function->is_noreturn)
            return mir_machine_reject(
                "sequential-scalar-call-report", "function");
        plan->functions[call_index] = function;
        plan->arguments[call_index] = (int)value & 0xffff;
    }
    print_call = &mir.insns[3 + plan->call_count * 4];
    if (print_call->opcode != MIR_CALL ||
        strcmp(print_call->name, "printf") ||
        (print_call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != MIR_CALL_FLAG_VARIADIC)
        return mir_machine_reject(
            "sequential-scalar-call-report", "print");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *argument = &mir.insns[instruction];
        int index;

        if (argument->opcode != MIR_ARG ||
            argument->secondary_offset != print_call->secondary_offset)
            continue;
        index = (int)argument->immediate;
        if (index < 0 || index > plan->call_count ||
            print_arguments[index] != 0)
            return mir_machine_reject(
                "sequential-scalar-call-report", "print-arguments");
        print_arguments[index] = argument->src1 + 1;
        ++print_argument_count;
    }
    if (print_argument_count != plan->call_count + 1 ||
        print_arguments[0] != mir.insns[1].dst + 1)
        return mir_machine_reject(
            "sequential-scalar-call-report", "print-prefix");
    for (call_index = 0; call_index < plan->call_count; ++call_index)
        if (print_arguments[call_index + 1] !=
            mir.insns[5 + call_index * 4].dst + 1)
            return mir_machine_reject(
                "sequential-scalar-call-report", "print-order");
    plan->print_function = find_global(print_call->name);
    if (plan->print_function == NULL ||
        plan->print_function->is_defined ||
        mir.insns[mir.count - 2].opcode != MIR_CONST ||
        mir.insns[mir.count - 2].immediate != 0 ||
        mir.insns[mir.count - 1].opcode != MIR_RETURN ||
        mir.insns[mir.count - 1].src1 !=
            mir.insns[mir.count - 2].dst)
        return mir_machine_reject(
            "sequential-scalar-call-report", "return");
    plan->string_id = (int)mir.insns[1].immediate;
    return 1;
}

static void mir_emit_sequential_scalar_call_report(
    MirStream *out, const struct MirSequentialScalarCallReport *plan)
{
    int call_index;

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (call_index = plan->call_count - 1;
         call_index >= 0; --call_index) {
        mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n",
                plan->arguments[call_index]);
        mir_machine_emit_symbol_call(out, plan->functions[call_index]);
        mir_stream_puts("\tpop bc\n\tpush hl\n", out);
    }
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", plan->string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    for (call_index = 0; call_index < plan->call_count + 1; ++call_index)
        mir_stream_puts("\tpop bc\n", out);
    mir_stream_puts("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_float_nan_bits(struct MirFloatNanBits *plan)
{
    static const int expected_opcodes[32] = {
        MIR_LABEL, MIR_PARAM, MIR_ADDRESS, MIR_NOP, MIR_LOAD_INDIRECT,
        MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_NOP, MIR_CONST, MIR_NOP, MIR_BINARY, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_RETURN
    };
    const struct MirInsn *parameter = &mir.insns[1];
    int memory_type, memory_storage, memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 32 || mir_cfg_block_count() != 4 ||
        mir.has_vla || type_size(mir.return_type) != 2 ||
        type_ptr_depth(mir.return_type) != 0 ||
        !type_is_float(parameter->type) ||
        type_size(parameter->type) != 4)
        return mir_machine_reject("float-nan-bits", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject("float-nan-bits", "opcode");
    if (strcmp(mir.insns[2].name, parameter->name) ||
        mir.insns[4].src1 != mir.insns[2].dst ||
        mir.insns[4].memory_size != 4 ||
        (mir.insns[4].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_unobservable_local_store(&mir.insns[5]) ||
        mir.insns[5].src1 != mir.insns[4].dst ||
        !mir_machine_constant_equals(mir.insns[7].dst, 23) ||
        mir.insns[8].immediate != TOK_SHR ||
        mir.insns[8].src1 != mir.insns[4].dst ||
        mir.insns[8].src2 != mir.insns[7].dst ||
        !mir_machine_constant_equals(mir.insns[10].dst, 255) ||
        mir.insns[11].immediate != '&' ||
        mir.insns[11].src1 != mir.insns[8].dst ||
        mir.insns[11].src2 != mir.insns[10].dst ||
        !mir_machine_constant_equals(mir.insns[13].dst, 255) ||
        mir.insns[14].immediate != TOK_EQ ||
        mir.insns[14].src1 != mir.insns[11].dst ||
        mir.insns[14].src2 != mir.insns[13].dst ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        mir.insns[15].label != mir.insns[27].label)
        return mir_machine_reject("float-nan-bits", "exponent");
    if (!mir_machine_constant_equals(mir.insns[17].dst, 8388607L) ||
        mir.insns[19].immediate != '&' ||
        mir.insns[19].src1 != mir.insns[4].dst ||
        mir.insns[19].src2 != mir.insns[17].dst ||
        !mir_machine_constant_equals(mir.insns[21].dst, 0) ||
        mir.insns[22].immediate != TOK_NE ||
        mir.insns[22].src1 != mir.insns[19].dst ||
        mir.insns[22].src2 != mir.insns[21].dst ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[23].label != mir.insns[27].label ||
        !mir_machine_boolean_merge(30, 25, 28, 24, 27) ||
        mir.insns[26].label != mir.insns[29].label ||
        mir.insns[31].src1 != mir.insns[30].dst)
        return mir_machine_reject("float-nan-bits", "mantissa");
    if (!mir_scalar_memory_location(
            parameter, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2)
        return mir_machine_reject("float-nan-bits", "parameter");
    plan->parameter_stack_offset = memory_offset - 2;
    return 1;
}

static void mir_emit_float_nan_bits(
    MirStream *out, const struct MirFloatNanBits *plan)
{
    int false_result = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tld b,a\n"
            "\tinc hl\n\tld a,(hl)\n\tld c,a\n"
            "\tand 127\n\tor b\n\tld b,a\n"
            "\tld a,c\n\tand 128\n\tjp z,L%d\n"
            "\tinc hl\n\tld a,(hl)\n\tand 127\n\tcp 127\n"
            "\tjp nz,L%d\n\tld a,b\n\tor a\n\tjp z,L%d\n"
            "\tld hl,1\n\tret\n"
            "L%d:\n\tld hl,0\n\tret\n",
            plan->parameter_stack_offset,
            false_result, false_result, false_result, false_result);
}

static int mir_match_random_unique_init(
    struct MirRandomUniqueInit *plan)
{
    static const int expected_opcodes[53] = {
        MIR_LABEL, MIR_PARAM, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_NOP,
        MIR_STORE, MIR_LABEL, MIR_NOP, MIR_PHI, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_MEMBER_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_CALL, MIR_STORE_INDIRECT,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_MEMBER_ADDRESS,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_ARG, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_CONST,
        MIR_RETURN
    };
    const struct MirInsn *object = &mir.insns[1];
    const struct MirInsn *array_member = &mir.insns[15];
    const struct MirInsn *duplicate_call = &mir.insns[34];
    const struct MirInsn *copy_call = &mir.insns[46];
    int duplicate_arguments[2];
    int copy_arguments[3];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 53 || mir_cfg_block_count() != 7 ||
        mir.has_vla || type_size(mir.return_type) != 2 ||
        type_ptr_depth(mir.return_type) != 0 ||
        type_ptr_depth(object->type) != 1)
        return mir_machine_reject("random-unique-init", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject("random-unique-init", "opcode");
    if (!mir_machine_parameter_value_offset(
            object->dst, &plan->object_stack_offset) ||
        !mir_machine_constant_equals(mir.insns[4].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[6]) ||
        mir.insns[6].memory_size != 2 ||
        mir.insns[9].src1 != mir.insns[4].dst ||
        mir.insns[9].src2 != mir.insns[23].dst ||
        mir.insns[9].phi_pred1 != mir.insns[2].label ||
        mir.insns[9].phi_pred2 != mir.insns[20].label ||
        mir.insns[12].immediate != '<' ||
        mir.insns[12].src1 != mir.insns[9].dst ||
        mir.insns[12].src2 != mir.insns[11].dst ||
        mir.insns[13].src1 != mir.insns[12].dst ||
        mir.insns[13].label != mir.insns[26].label)
        return mir_machine_reject("random-unique-init", "loop");
    plan->count = (int)mir.insns[11].immediate;
    if (plan->count <= 0 || plan->count > 255 ||
        array_member->src1 != object->dst ||
        mir.insns[17].src1 != array_member->dst ||
        mir.insns[17].src2 != mir.insns[9].dst ||
        mir.insns[17].immediate != 2 ||
        mir.insns[18].memory_flags != 0 ||
        mir.insns[19].src1 != mir.insns[17].dst ||
        mir.insns[19].src2 != mir.insns[18].dst ||
        mir.insns[19].memory_size != 2 ||
        (mir.insns[19].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_constant_equals(mir.insns[22].dst, 1) ||
        mir.insns[23].immediate != '+' ||
        mir.insns[23].src1 != mir.insns[9].dst ||
        mir.insns[23].src2 != mir.insns[22].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[24]) ||
        mir.insns[24].src1 != mir.insns[23].dst ||
        mir.insns[25].label != mir.insns[7].label)
        return mir_machine_reject("random-unique-init", "fill");
    plan->producer = find_global(mir.insns[18].name);
    plan->array_offset = (int)array_member->immediate;
    if (plan->producer == NULL || !plan->producer->is_defined ||
        plan->producer->is_funcptr ||
        (mir.insns[18].memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0 ||
        mir.insns[30].src1 != object->dst ||
        mir.insns[30].immediate != plan->array_offset ||
        !mir_machine_two_call_arguments(
            duplicate_call, duplicate_arguments) ||
        duplicate_arguments[0] != mir.insns[30].dst ||
        duplicate_arguments[1] != mir.insns[32].dst ||
        mir.insns[32].immediate != plan->count ||
        mir.insns[35].src1 != duplicate_call->dst ||
        mir.insns[35].label != mir.insns[37].label ||
        mir.insns[36].label != mir.insns[2].label)
        return mir_machine_reject("random-unique-init", "duplicate");
    plan->duplicate_check = find_global(duplicate_call->name);
    if (plan->duplicate_check == NULL ||
        !plan->duplicate_check->is_defined ||
        plan->duplicate_check->is_funcptr ||
        (duplicate_call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0 ||
        mir.insns[39].src1 != object->dst ||
        mir.insns[42].src1 != object->dst ||
        mir.insns[42].immediate != plan->array_offset ||
        !mir_machine_three_call_arguments(copy_call, copy_arguments) ||
        copy_arguments[0] != mir.insns[39].dst ||
        copy_arguments[1] != mir.insns[42].dst ||
        copy_arguments[2] != mir.insns[44].dst ||
        mir.insns[44].immediate != plan->count)
        return mir_machine_reject("random-unique-init", "copy");
    plan->copy_function = find_global(copy_call->name);
    plan->copy_offset = (int)mir.insns[39].immediate;
    if (plan->copy_function == NULL || !plan->copy_function->is_defined ||
        plan->copy_function->is_funcptr ||
        (copy_call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0 ||
        mir.insns[48].src1 != object->dst ||
        mir.insns[50].src1 != mir.insns[48].dst ||
        mir.insns[50].src2 != mir.insns[49].dst ||
        mir.insns[50].memory_size != 2 ||
        (mir.insns[50].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_constant_equals(mir.insns[51].dst, 0) ||
        mir.insns[52].src1 != mir.insns[51].dst)
        return mir_machine_reject("random-unique-init", "result");
    plan->final_offset = (int)mir.insns[48].immediate;
    plan->final_value = (int)mir.insns[49].immediate;
    if (plan->array_offset < -32768 || plan->array_offset > 32767 ||
        plan->copy_offset < -32768 || plan->copy_offset > 32767 ||
        plan->final_offset < -32768 || plan->final_offset > 32767 ||
        plan->final_value < -32768 || plan->final_value > 32767)
        return mir_machine_reject("random-unique-init", "constants");
    return 1;
}

static void mir_emit_random_unique_init(
    MirStream *out, const struct MirRandomUniqueInit *plan)
{
    int outer_label = new_label();
    int inner_label = new_label();

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tdec sp\n\tdec sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "L%d:\n\tld (ix-2),0\n\tld (ix-1),0\n"
            "L%d:\n",
            outer_label, inner_label);
    mir_emit_object_parameter_ix(out, plan);
    mir_machine_emit_hl_offset(out, plan->array_offset, 0);
    mir_stream_puts("\tld e,(ix-2)\n\tld d,(ix-1)\n"
          "\tex de,hl\n\tadd hl,hl\n\tex de,hl\n"
          "\tadd hl,de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->producer);
    mir_stream_puts("\tpop de\n\tex de,hl\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n"
          "\tinc (ix-2)\n\tld a,(ix-2)\n", out);
    mir_stream_printf(out, "\tcp %d\n\tjp c,L%d\n",
            plan->count, inner_label);
    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n", plan->count);
    mir_emit_object_parameter_ix(out, plan);
    mir_machine_emit_hl_offset(out, plan->array_offset, 0);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->duplicate_check);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", outer_label);
    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n", plan->count);
    mir_emit_object_parameter_ix(out, plan);
    mir_machine_emit_hl_offset(out, plan->array_offset, 0);
    mir_stream_puts("\tpush hl\n", out);
    mir_emit_object_parameter_ix(out, plan);
    mir_machine_emit_hl_offset(out, plan->copy_offset, 0);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->copy_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_emit_object_parameter_ix(out, plan);
    mir_machine_emit_hl_offset(out, plan->final_offset, 0);
    mir_stream_printf(out,
            "\tld (hl),%d\n\tinc hl\n\tld (hl),%d\n"
            "\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n",
            plan->final_value & 0xff,
            (plan->final_value >> 8) & 0xff);
}

static int mir_match_fixed_row_find(struct MirFixedRowFind *plan)
{
    static const int expected_opcodes[56] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_MEMBER_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_PHI, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_LOAD, MIR_INDEX_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP,
        MIR_STORE_INDIRECT, MIR_CONST, MIR_RETURN, MIR_NOP, MIR_LABEL,
        MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_RETURN
    };
    const struct MirInsn *object = &mir.insns[1];
    const struct MirInsn *output = &mir.insns[2];
    const struct MirInsn *row_member = &mir.insns[4];
    const struct MirInsn *table_address = &mir.insns[23];
    const struct MirInsn *target_member = &mir.insns[33];
    const struct MirInsn *index_phi = &mir.insns[17];
    int memory_type, memory_storage, memory_offset;
    long target_index;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 56 || mir_cfg_block_count() != 5 ||
        mir.has_vla || type_size(mir.return_type) != 2 ||
        type_ptr_depth(mir.return_type) != 0 ||
        type_ptr_depth(object->type) != 1 ||
        type_ptr_depth(output->type) != 1)
        return mir_machine_reject("fixed-row-find", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject("fixed-row-find", "opcode");
    if (!mir_machine_parameter_value_offset(
            object->dst, &plan->object_stack_offset) ||
        !mir_scalar_memory_location(
            output, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2)
        return mir_machine_reject("fixed-row-find", "parameters");
    plan->output_stack_offset = memory_offset - 2;
    if (row_member->src1 != object->dst ||
        !mir_machine_constant_equals(mir.insns[5].dst, 0) ||
        mir.insns[6].src1 != row_member->dst ||
        mir.insns[6].src2 != mir.insns[5].dst ||
        mir.insns[6].immediate != 2 ||
        mir.insns[7].src1 != mir.insns[6].dst ||
        mir.insns[7].memory_size != 2 ||
        (mir.insns[7].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_unobservable_local_store(&mir.insns[9]) ||
        mir.insns[9].src1 != mir.insns[7].dst ||
        !mir_machine_constant_equals(mir.insns[11].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[12]) ||
        index_phi->src1 != mir.insns[11].dst ||
        index_phi->src2 != mir.insns[50].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[47].label)
        return mir_machine_reject("fixed-row-find", "setup");
    plan->row_member_offset = (int)row_member->immediate;
    plan->count = (int)mir.insns[19].immediate;
    if (plan->count != 3 ||
        mir.insns[20].immediate != 0 ||
        mir.insns[20].src1 != index_phi->dst ||
        mir.insns[21].immediate != '<' ||
        mir.insns[21].src1 != mir.insns[20].dst ||
        mir.insns[21].src2 != mir.insns[19].dst ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        mir.insns[22].label != mir.insns[53].label ||
        !mir_scalar_memory_location(
            table_address, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL || memory_offset != 0 ||
        !mir_machine_same_location(&mir.insns[9], &mir.insns[24]) ||
        mir.insns[25].src1 != table_address->dst ||
        mir.insns[25].src2 != mir.insns[24].dst ||
        mir.insns[25].immediate <= 0 ||
        mir.insns[27].src1 != mir.insns[25].dst ||
        mir.insns[27].src2 != index_phi->dst ||
        mir.insns[27].immediate != 2 ||
        mir.insns[28].src1 != mir.insns[27].dst ||
        mir.insns[28].memory_size != 2 ||
        (mir.insns[28].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_unobservable_local_store(&mir.insns[30]) ||
        mir.insns[30].src1 != mir.insns[28].dst)
        return mir_machine_reject("fixed-row-find", "row");
    plan->table = find_global(table_address->name);
    plan->row_stride = (int)mir.insns[25].immediate;
    if (plan->table == NULL || plan->table->is_volatile ||
        plan->row_stride <= 0 || plan->row_stride > 16 ||
        plan->row_member_offset < -32768 ||
        plan->row_member_offset > 32767 ||
        !mir_machine_same_location(object, &mir.insns[32]) ||
        target_member->src1 != mir.insns[32].dst ||
        !mir_machine_constant_value(
            mir.insns[34].dst, &target_index, 0) ||
        target_index < 0 || target_index > 32767 ||
        mir.insns[35].src1 != target_member->dst ||
        mir.insns[35].src2 != mir.insns[34].dst ||
        mir.insns[35].immediate != 2 ||
        mir.insns[36].src1 != mir.insns[35].dst ||
        mir.insns[36].memory_size != 2 ||
        (mir.insns[36].memory_flags & (1 | 8)) != 0 ||
        mir.insns[37].immediate != TOK_EQ ||
        mir.insns[37].src1 != mir.insns[28].dst ||
        mir.insns[37].src2 != mir.insns[36].dst ||
        mir.insns[38].src1 != mir.insns[37].dst ||
        mir.insns[38].label != mir.insns[45].label)
        return mir_machine_reject("fixed-row-find", "target");
    plan->target_member_offset =
        (int)target_member->immediate + (int)target_index * 2;
    if (plan->target_member_offset < -32768 ||
        plan->target_member_offset > 32767)
        return mir_machine_reject("fixed-row-find", "target-offset");
    if (!mir_machine_same_location(output, &mir.insns[39]) ||
        mir.insns[41].src1 != mir.insns[39].dst ||
        mir.insns[41].src2 != mir.insns[28].dst ||
        mir.insns[41].memory_size != 2 ||
        (mir.insns[41].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_constant_equals(mir.insns[42].dst, 1) ||
        mir.insns[43].src1 != mir.insns[42].dst ||
        !mir_machine_same_location(&mir.insns[12], &mir.insns[48]) ||
        !mir_machine_constant_equals(mir.insns[49].dst, 1) ||
        mir.insns[50].immediate != '+' ||
        mir.insns[50].src1 != mir.insns[48].dst ||
        mir.insns[50].src2 != mir.insns[49].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[51]) ||
        mir.insns[51].src1 != mir.insns[50].dst ||
        mir.insns[52].label != mir.insns[13].label ||
        !mir_machine_constant_equals(mir.insns[54].dst, 0) ||
        mir.insns[55].src1 != mir.insns[54].dst)
        return mir_machine_reject("fixed-row-find", "result");
    return 1;
}

static void mir_emit_fixed_row_find(
    MirStream *out, const struct MirFixedRowFind *plan)
{
    int add;
    int element;
    int next_labels[3];

    for (element = 0; element < 3; ++element)
        next_labels[element] = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n",
            plan->object_stack_offset);
    mir_machine_emit_hl_offset(out, plan->row_member_offset, 0);
    mir_stream_puts("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n\tld hl,0\n", out);
    for (add = 0; add < plan->row_stride; ++add)
        mir_stream_puts("\tadd hl,bc\n", out);
    mir_machine_emit_global_address_de(out, plan->table, 0);
    mir_stream_puts("\tadd hl,de\n\tpush hl\n", out);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n",
            plan->object_stack_offset + 2);
    mir_machine_emit_hl_offset(out, plan->target_member_offset, 0);
    mir_stream_puts("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n\tpop hl\n", out);
    for (element = 0; element < plan->count; ++element) {
        mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tinc hl\n"
              "\tld a,e\n\txor c\n", out);
        mir_stream_printf(out, "\tjp nz,L%d\n", next_labels[element]);
        mir_stream_puts("\tld a,d\n\txor b\n", out);
        mir_stream_printf(out, "\tjp nz,L%d\n", next_labels[element]);
        mir_stream_printf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
                "\tld h,b\n\tld l,c\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n"
                "\tld hl,1\n\tret\n"
                "L%d:\n",
                plan->output_stack_offset,
                next_labels[element]);
    }
    mir_stream_puts("\tld hl,0\n\tret\n", out);
}

static int mir_match_affine_local_fill_call_reports(
    struct MirLocalByteFillCallReports *plan)
{
    const struct MirInsn *index_phi = &mir.insns[5];
    int helper_args[2];
    int report_args[2];
    int type, storage, offset;
    int call_number;
    long patch_index;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 53 || mir_cfg_block_count() != 4 ||
        mir.has_vla || type_size(mir.return_type) != 2 ||
        !mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[3]) ||
        index_phi->opcode != MIR_PHI ||
        index_phi->src1 != mir.insns[2].dst ||
        index_phi->src2 != mir.insns[23].dst ||
        mir.insns[9].immediate != '<' ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].src2 != mir.insns[7].dst ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[10].label != mir.insns[26].label)
        return mir_machine_reject(
            "affine-local-fill-call-reports", "loop");
    plan->count = (int)mir.insns[7].immediate;
    plan->fill_initial = (int)mir.insns[14].immediate;
    if (plan->count <= 0 || plan->count > 255 ||
        !mir_scalar_memory_location(
            &mir.insns[11], &type, &storage, &offset) ||
        storage != SC_LOCAL || offset != -plan->count ||
        mir.insns[13].src1 != mir.insns[11].dst ||
        mir.insns[13].src2 != index_phi->dst ||
        mir.insns[13].immediate != 1 ||
        mir.insns[17].immediate != '+' ||
        mir.insns[17].src1 != mir.insns[14].dst ||
        mir.insns[17].src2 != mir.insns[16].dst ||
        mir.insns[19].src1 != mir.insns[13].dst ||
        mir.insns[19].src2 != mir.insns[18].dst ||
        mir.insns[19].memory_size != 1 ||
        !mir_machine_constant_equals(mir.insns[22].dst, 1) ||
        mir.insns[23].immediate != '+' ||
        !mir_machine_unobservable_local_store(&mir.insns[24]) ||
        mir.insns[25].label != mir.insns[4].label)
        return mir_machine_reject(
            "affine-local-fill-call-reports", "fill");
    plan->local_offset = offset;
    for (call_number = 0; call_number < 2; ++call_number) {
        int base = call_number == 0 ? 27 : 42;
        const struct MirInsn *string = &mir.insns[base];
        const struct MirInsn *address = &mir.insns[base + 2];
        const struct MirInsn *call = &mir.insns[base + 6];
        const struct MirInsn *report = &mir.insns[base + 8];
        struct Sym *function;
        struct Sym *report_function;

        if (string->opcode != MIR_STRING_ADDRESS ||
            address->opcode != MIR_ADDRESS ||
            strcmp(address->name, mir.insns[11].name) ||
            !mir_machine_two_call_arguments(call, helper_args) ||
            helper_args[0] != address->dst ||
            !mir_machine_constant_equals(helper_args[1], plan->fill_initial) ||
            !mir_machine_two_call_arguments(report, report_args) ||
            report_args[0] != string->dst ||
            report_args[1] != call->dst)
            return mir_machine_reject(
                "affine-local-fill-call-reports", "calls");
        if (!mir_machine_match_local_fill_report_contract(
                call, report, address->dst, helper_args[1],
                string->dst, &function, &report_function))
            return mir_machine_reject(
                "affine-local-fill-call-reports", "call-contract");
        plan->functions[call_number] = function;
        plan->string_ids[call_number] = (int)string->immediate;
        if (call_number == 0)
            plan->report_function = report_function;
        else if (plan->report_function != report_function)
            return mir_machine_reject(
                "affine-local-fill-call-reports", "report");
    }
    if (plan->report_function == NULL ||
        mir.insns[36].opcode != MIR_ADDRESS ||
        !mir_machine_constant_value(
        mir.insns[37].dst, &patch_index, 0) ||
    patch_index < 0 || patch_index >= plan->count ||
        mir.insns[38].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[41].opcode != MIR_STORE_INDIRECT ||
        !mir_machine_constant_equals(mir.insns[40].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[51].dst, 0) ||
        mir.insns[52].src1 != mir.insns[51].dst)
        return mir_machine_reject(
            "affine-local-fill-call-reports", "patch");
    plan->patch_offset = (int)patch_index;
    plan->patch_value = 0;
    plan->patch_after_call = 1;
    plan->call_argument = plan->fill_initial;
    plan->call_count = 2;
    return 1;
}

static int mir_match_local_byte_fill_call_reports(
    struct MirLocalByteFillCallReports *plan)
{
    static const int prefix_opcodes[27] = {
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_PHI,
        MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_STORE_INDIRECT, MIR_LABEL,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL
    };
    const struct MirInsn *index_phi = &mir.insns[5];
    int memory_type, memory_storage, memory_offset;
    int instruction = 27;
    int call_index;

    memset(plan, 0, sizeof(*plan));
    plan->patch_offset = -1;
    if ((mir.count != 44 && mir.count != 47) ||
        mir_cfg_block_count() != 4 || mir.has_vla ||
        type_size(mir.return_type) != 2 ||
        type_ptr_depth(mir.return_type) != 0)
        return mir_machine_reject(
            "local-byte-fill-call-reports", "shape");
    for (call_index = 0; call_index < 27; ++call_index)
        if (mir.insns[call_index].opcode != prefix_opcodes[call_index])
            return mir_machine_reject(
                "local-byte-fill-call-reports", "prefix-opcode");
    if (!mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[3]) ||
        mir.insns[3].memory_size != 1 ||
        index_phi->src1 != mir.insns[2].dst ||
        index_phi->src2 != mir.insns[23].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[20].label ||
        mir.insns[8].immediate != 0 ||
        mir.insns[8].src1 != index_phi->dst ||
        mir.insns[9].immediate != '<' ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].src2 != mir.insns[7].dst ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[10].label != mir.insns[26].label)
        return mir_machine_reject(
            "local-byte-fill-call-reports", "loop");
    plan->count = (int)mir.insns[7].immediate;
    plan->fill_initial = 1;
    plan->call_argument = plan->count;
    if (plan->count <= 0 || plan->count > 255 ||
        !mir_scalar_memory_location(
            &mir.insns[11], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_LOCAL || memory_offset != -plan->count ||
        mir.insns[13].src1 != mir.insns[11].dst ||
        mir.insns[13].src2 != index_phi->dst ||
        mir.insns[13].immediate != 1 ||
        !mir_machine_constant_equals(mir.insns[15].dst, 1) ||
        mir.insns[16].immediate != 0 ||
        mir.insns[16].src1 != index_phi->dst ||
        mir.insns[17].immediate != '+' ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        mir.insns[17].src2 != mir.insns[15].dst ||
        mir.insns[18].immediate != 0 ||
        mir.insns[18].src1 != mir.insns[17].dst ||
        mir.insns[19].src1 != mir.insns[13].dst ||
        mir.insns[19].src2 != mir.insns[18].dst ||
        mir.insns[19].memory_size != 1 ||
        (mir.insns[19].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_constant_equals(mir.insns[22].dst, 1) ||
        mir.insns[23].immediate != '+' ||
        mir.insns[23].src1 != index_phi->dst ||
        mir.insns[23].src2 != mir.insns[22].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[24]) ||
        mir.insns[24].src1 != mir.insns[23].dst ||
        mir.insns[25].label != mir.insns[4].label)
        return mir_machine_reject(
            "local-byte-fill-call-reports", "fill");
    plan->local_offset = memory_offset;
    if (mir.insns[instruction].opcode == MIR_ADDRESS) {
        int patch_type, patch_storage, patch_base_offset;
        long patch_index, patch_value;

        if (instruction + 5 >= mir.count ||
            !mir_scalar_memory_location(
                &mir.insns[instruction], &patch_type, &patch_storage,
                &patch_base_offset) ||
            patch_storage != memory_storage ||
            patch_base_offset != memory_offset ||
            !mir_machine_constant_value(
                mir.insns[instruction + 1].dst, &patch_index, 0) ||
            patch_index < 0 || patch_index >= plan->count ||
            mir.insns[instruction + 2].opcode != MIR_INDEX_ADDRESS ||
            mir.insns[instruction + 2].src1 !=
                mir.insns[instruction].dst ||
            mir.insns[instruction + 2].src2 !=
                mir.insns[instruction + 1].dst ||
            mir.insns[instruction + 2].immediate != 1 ||
            mir.insns[instruction + 3].opcode != MIR_NOP ||
            !mir_machine_constant_value(
                mir.insns[instruction + 4].dst, &patch_value, 0) ||
            mir.insns[instruction + 5].opcode != MIR_STORE_INDIRECT ||
            mir.insns[instruction + 5].src1 !=
                mir.insns[instruction + 2].dst ||
            mir.insns[instruction + 5].src2 !=
                mir.insns[instruction + 4].dst ||
            mir.insns[instruction + 5].memory_size != 1 ||
            (mir.insns[instruction + 5].memory_flags & (1 | 8)) != 0)
            return mir_machine_reject(
                "local-byte-fill-call-reports", "patch");
        plan->patch_offset = (int)patch_index;
        plan->patch_value = (int)patch_value & 0xff;
        instruction += 6;
    }
    while (instruction + 8 < mir.count - 2 &&
           plan->call_count < 2) {
        const struct MirInsn *string = &mir.insns[instruction];
        const struct MirInsn *address = &mir.insns[instruction + 2];
        const struct MirInsn *count = &mir.insns[instruction + 4];
        const struct MirInsn *call = &mir.insns[instruction + 6];
        const struct MirInsn *report = &mir.insns[instruction + 8];
        int call_arguments[2];
        int report_arguments[2];
        int address_type, address_storage, address_offset;
        struct Sym *function;
        struct Sym *report_function;

        if (string->opcode != MIR_STRING_ADDRESS ||
            mir.insns[instruction + 1].opcode != MIR_ARG ||
            mir.insns[instruction + 1].src1 != string->dst ||
            address->opcode != MIR_ADDRESS ||
            !mir_scalar_memory_location(
                address, &address_type, &address_storage, &address_offset) ||
            address_storage != memory_storage ||
            address_offset != memory_offset ||
            mir.insns[instruction + 3].opcode != MIR_ARG ||
            mir.insns[instruction + 3].src1 != address->dst ||
            count->opcode != MIR_CONST ||
            count->immediate != plan->count ||
            mir.insns[instruction + 5].opcode != MIR_ARG ||
            mir.insns[instruction + 5].src1 != count->dst ||
            call->opcode != MIR_CALL ||
            !mir_machine_two_call_arguments(call, call_arguments) ||
            call_arguments[0] != address->dst ||
            call_arguments[1] != count->dst ||
            mir.insns[instruction + 7].opcode != MIR_ARG ||
            mir.insns[instruction + 7].src1 != call->dst ||
            report->opcode != MIR_CALL ||
            !mir_machine_two_call_arguments(report, report_arguments) ||
            report_arguments[0] != string->dst ||
            report_arguments[1] != call->dst ||
            !mir_machine_match_local_fill_report_contract(
                call, report, address->dst, count->dst,
                string->dst, &function, &report_function))
            return mir_machine_reject(
                "local-byte-fill-call-reports", "calls");
        plan->functions[plan->call_count] = function;
        plan->string_ids[plan->call_count] = (int)string->immediate;
        if (plan->report_function == NULL)
            plan->report_function = report_function;
        else if (plan->report_function != report_function)
            return mir_machine_reject(
                "local-byte-fill-call-reports", "report-symbol");
        ++plan->call_count;
        instruction += 9;
    }
    if (plan->call_count == 0 || instruction + 2 != mir.count ||
        mir.insns[instruction].opcode != MIR_CONST ||
        mir.insns[instruction].immediate != 0 ||
        mir.insns[instruction + 1].opcode != MIR_RETURN ||
        mir.insns[instruction + 1].src1 != mir.insns[instruction].dst)
        return mir_machine_reject(
            "local-byte-fill-call-reports", "return");
    return 1;
}

static void mir_emit_local_byte_fill_call_reports(
    MirStream *out, const struct MirLocalByteFillCallReports *plan)
{
    int call_index;
    int loop_label = new_label();

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld sp,hl\n",
            plan->local_offset);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tpush ix\n\tpop hl\n\tld de,%d\n\tadd hl,de\n"
            "\tld a,%d\n\tld b,%d\n"
            "L%d:\n\tld (hl),a\n\tinc hl\n\tinc a\n\tdjnz L%d\n",
            plan->local_offset, plan->fill_initial, plan->count,
            loop_label, loop_label);
    for (call_index = 0; call_index < plan->call_count; ++call_index) {
        if (plan->patch_offset >= 0 &&
            call_index == plan->patch_after_call)
            mir_stream_printf(out, "\tld (ix%+d),%d\n",
                    plan->local_offset + plan->patch_offset,
                    plan->patch_value);
        mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n",
                plan->call_argument);
        mir_stream_puts("\tpush ix\n\tpop hl\n", out);
        mir_machine_emit_hl_offset(out, plan->local_offset, 0);
        mir_stream_puts("\tpush hl\n", out);
        mir_machine_emit_symbol_call(
            out, plan->functions[call_index]);
        mir_stream_puts("\tpop bc\n\tpop bc\n\tpush hl\n", out);
        mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
                plan->string_ids[call_index]);
        mir_machine_emit_symbol_call(out, plan->report_function);
        mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    }
    mir_stream_puts("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_global_record_pop(
    struct MirGlobalRecordPop *plan)
{
    static const int prefix_opcodes[24] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_LABEL, MIR_NOP, MIR_LOAD,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_LOAD,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY, MIR_STORE_INDIRECT,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT
    };
    static const int direct_opcodes[30] = {
        MIR_INDEX_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_INDEX_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_STORE_INDIRECT, MIR_CONST, MIR_RETURN, MIR_NOP, MIR_LABEL,
        MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_RETURN
    };
    static const int helper_opcodes[29] = {
        MIR_CONST, MIR_BINARY, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT,
        MIR_CONST, MIR_RETURN, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_LABEL,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_RETURN
    };
    const struct MirInsn *base = &mir.insns[1];
    const struct MirInsn *target = &mir.insns[2];
    const struct MirInsn *target_load;
    const struct MirInsn *value_load;
    const struct MirInsn *records_member = &mir.insns[19];
    const struct MirInsn *index_member = &mir.insns[7];
    int memory_type, memory_storage, memory_offset;
    long wanted_kind;
    int direct;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    direct = mir.count == 54;
    if ((mir.count != 54 && mir.count != 53) ||
        mir_cfg_block_count() != 5 || mir.has_vla ||
        type_size(mir.return_type) != 2 ||
        type_ptr_depth(mir.return_type) != 0 ||
        type_size(base->type) != 2 ||
        type_ptr_depth(base->type) != 0 ||
        type_ptr_depth(target->type) != 1)
        return mir_machine_reject("global-record-pop", "shape");
    for (instruction = 0; instruction < 24; ++instruction)
        if (mir.insns[instruction].opcode != prefix_opcodes[instruction])
            return mir_machine_reject("global-record-pop", "prefix-opcode");
    for (instruction = 24; instruction < mir.count; ++instruction) {
        int expected = direct
            ? direct_opcodes[instruction - 24]
            : helper_opcodes[instruction - 24];
        if (mir.insns[instruction].opcode != expected)
            return mir_machine_reject("global-record-pop", "tail-opcode");
    }
    if (!mir_scalar_memory_location(
            base, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2)
        return mir_machine_reject("global-record-pop", "base");
    plan->base_stack_offset = memory_offset - 2;
    if (!mir_scalar_memory_location(
            target, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2)
        return mir_machine_reject("global-record-pop", "target");
    plan->target_stack_offset = memory_offset - 2;
    if (!mir_machine_named_nonvolatile(&mir.insns[6]) ||
        !mir_machine_same_location(&mir.insns[6], &mir.insns[12]) ||
        !mir_machine_same_location(&mir.insns[6], &mir.insns[18]) ||
        !mir_machine_same_location(&mir.insns[6], &mir.insns[21]) ||
        index_member->src1 != mir.insns[6].dst ||
        mir.insns[8].src1 != index_member->dst ||
        mir.insns[8].memory_size != 2 ||
        (mir.insns[8].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_same_location(base, &mir.insns[9]) ||
        mir.insns[10].immediate != '>' ||
        mir.insns[10].src1 != mir.insns[8].dst ||
        mir.insns[10].src2 != mir.insns[9].dst ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[11].label != mir.insns[direct ? 51 : 50].label)
        return mir_machine_reject("global-record-pop", "loop-test");
    plan->state = find_global(mir.insns[6].name);
    if (plan->state == NULL || plan->state->is_volatile ||
        plan->state->storage == SC_EXTERN ||
        index_member->immediate < -128 || index_member->immediate > 126 ||
        mir.insns[13].src1 != mir.insns[12].dst ||
        mir.insns[13].immediate != index_member->immediate ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        (mir.insns[14].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_constant_equals(mir.insns[15].dst, 1) ||
        mir.insns[16].immediate != '-' ||
        mir.insns[16].src1 != mir.insns[14].dst ||
        mir.insns[16].src2 != mir.insns[15].dst ||
        mir.insns[17].src1 != mir.insns[13].dst ||
        mir.insns[17].src2 != mir.insns[16].dst ||
        mir.insns[17].memory_size != 2 ||
        (mir.insns[17].memory_flags & (1 | 8)) != 0 ||
        records_member->src1 != mir.insns[18].dst ||
        mir.insns[20].src1 != records_member->dst ||
        mir.insns[20].memory_size != 2 ||
        (mir.insns[20].memory_flags & (1 | 8)) != 0 ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        mir.insns[22].immediate != index_member->immediate ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[23].memory_size != 2 ||
        (mir.insns[23].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject("global-record-pop", "state");
    plan->index_offset = (int)index_member->immediate;
    plan->records_offset = (int)records_member->immediate;
    if (plan->records_offset < -128 || plan->records_offset > 126)
        return mir_machine_reject("global-record-pop", "state-offsets");
    if (direct) {
        if (mir.insns[24].src1 != mir.insns[20].dst ||
            mir.insns[24].src2 != mir.insns[23].dst ||
            mir.insns[24].immediate <= 0 ||
            mir.insns[25].src1 != mir.insns[24].dst ||
            mir.insns[26].src1 != mir.insns[25].dst ||
            mir.insns[26].memory_size != 2 ||
            (mir.insns[26].memory_flags & (1 | 8)) != 0 ||
            !mir_machine_unobservable_local_store(&mir.insns[28]) ||
            mir.insns[28].src1 != mir.insns[26].dst ||
            !mir_machine_constant_value(
                mir.insns[30].dst, &wanted_kind, 0) ||
            mir.insns[31].immediate != TOK_EQ ||
            mir.insns[31].src1 != mir.insns[26].dst ||
            mir.insns[31].src2 != mir.insns[30].dst ||
            mir.insns[32].src1 != mir.insns[31].dst ||
            mir.insns[32].label != mir.insns[47].label)
            return mir_machine_reject("global-record-pop", "direct-kind");
        plan->stride = (int)mir.insns[24].immediate;
        plan->kind_offset = (int)mir.insns[25].immediate;
        plan->wanted_kind = (int)wanted_kind;
        target_load = &mir.insns[33];
        value_load = &mir.insns[42];
        if (!mir_machine_same_location(&mir.insns[6], &mir.insns[34]) ||
            mir.insns[35].src1 != mir.insns[34].dst ||
            mir.insns[35].immediate != plan->records_offset ||
            mir.insns[36].src1 != mir.insns[35].dst ||
            !mir_machine_same_location(&mir.insns[6], &mir.insns[37]) ||
            mir.insns[38].src1 != mir.insns[37].dst ||
            mir.insns[38].immediate != plan->index_offset ||
            mir.insns[39].src1 != mir.insns[38].dst ||
            mir.insns[40].src1 != mir.insns[36].dst ||
            mir.insns[40].src2 != mir.insns[39].dst ||
            mir.insns[40].immediate != plan->stride ||
            mir.insns[41].src1 != mir.insns[40].dst)
            return mir_machine_reject("global-record-pop", "direct-value");
        plan->value_offset = (int)mir.insns[41].immediate;
    } else {
        long stride;
        if (!mir_machine_constant_value(mir.insns[24].dst, &stride, 0) ||
            stride <= 0 || stride > 255 ||
            mir.insns[25].immediate != '*' ||
            mir.insns[25].src1 != mir.insns[23].dst ||
            mir.insns[25].src2 != mir.insns[24].dst ||
            mir.insns[26].immediate != '+' ||
            mir.insns[26].src1 != mir.insns[20].dst ||
            mir.insns[26].src2 != mir.insns[25].dst ||
            !mir_machine_unobservable_local_store(&mir.insns[28]) ||
            mir.insns[28].src1 != mir.insns[26].dst ||
            !mir_machine_same_location(&mir.insns[28], &mir.insns[29]) ||
            mir.insns[30].src1 != mir.insns[29].dst ||
            mir.insns[31].src1 != mir.insns[30].dst ||
            (mir.insns[31].memory_flags & (1 | 8)) != 0 ||
            !mir_machine_unobservable_local_store(&mir.insns[33]) ||
            mir.insns[33].src1 != mir.insns[31].dst ||
            !mir_machine_constant_value(
                mir.insns[35].dst, &wanted_kind, 0) ||
            mir.insns[36].immediate != TOK_EQ ||
            mir.insns[36].src1 != mir.insns[31].dst ||
            mir.insns[36].src2 != mir.insns[35].dst ||
            mir.insns[37].src1 != mir.insns[36].dst ||
            mir.insns[37].label != mir.insns[46].label)
            return mir_machine_reject("global-record-pop", "helper-kind");
        plan->stride = (int)stride;
        plan->kind_offset = (int)mir.insns[30].immediate;
        plan->wanted_kind = (int)wanted_kind;
        target_load = &mir.insns[38];
        value_load = &mir.insns[41];
        if (!mir_machine_same_location(&mir.insns[28], &mir.insns[39]) ||
            mir.insns[40].src1 != mir.insns[39].dst)
            return mir_machine_reject("global-record-pop", "helper-value");
        plan->value_offset = (int)mir.insns[40].immediate;
    }
    if (!mir_machine_same_location(target, target_load) ||
        value_load->src1 !=
            mir.insns[direct ? 41 : 40].dst ||
        value_load->memory_size != 2 ||
        (value_load->memory_flags & (1 | 8)) != 0 ||
        mir.insns[direct ? 43 : 42].src1 != target_load->dst ||
        mir.insns[direct ? 43 : 42].src2 != value_load->dst ||
        mir.insns[direct ? 43 : 42].memory_size != 2 ||
        (mir.insns[direct ? 43 : 42].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_constant_equals(
            mir.insns[direct ? 44 : 43].dst, 1) ||
        mir.insns[direct ? 45 : 44].src1 !=
            mir.insns[direct ? 44 : 43].dst ||
        mir.insns[direct ? 50 : 49].label != mir.insns[3].label ||
        !mir_machine_constant_equals(
            mir.insns[direct ? 52 : 51].dst, 0) ||
        mir.insns[direct ? 53 : 52].src1 !=
            mir.insns[direct ? 52 : 51].dst)
        return mir_machine_reject("global-record-pop", "result");
    if (plan->stride <= 0 || plan->stride > 16 ||
        plan->kind_offset < 0 || plan->kind_offset + 1 >= plan->stride ||
        plan->value_offset < 0 || plan->value_offset + 1 >= plan->stride ||
        wanted_kind < -32768 || wanted_kind > 32767)
        return mir_machine_reject("global-record-pop", "constants");
    return 1;
}

static void mir_emit_global_record_pop(
    MirStream *out, const struct MirGlobalRecordPop *plan)
{
    int add;
    int loop_label = new_label();
    int done_label = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "L%d:\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n",
            loop_label, plan->base_stack_offset);
    mir_machine_emit_global_word(out, plan->state, 0);
    mir_machine_emit_hl_offset(out, plan->index_offset, 0);
    mir_stream_printf(out,
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n"
            "\tld a,h\n\txor 128\n\tld h,a\n"
            "\tld a,b\n\txor 128\n\tld b,a\n"
            "\tor a\n\tsbc hl,bc\n\tjp c,L%d\n"
            "\tld a,h\n\tor l\n\tjp z,L%d\n",
            done_label, done_label);
    mir_machine_emit_global_word(out, plan->state, 0);
    mir_machine_emit_hl_offset(out, plan->index_offset, 0);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tdec de\n"
          "\tld (hl),d\n\tdec hl\n\tld (hl),e\n"
          "\tld b,d\n\tld c,e\n", out);
    mir_machine_emit_global_word(out, plan->state, 0);
    mir_machine_emit_hl_offset(out, plan->records_offset, 0);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tld hl,0\n", out);
    for (add = 0; add < plan->stride; ++add)
        mir_stream_puts("\tadd hl,bc\n", out);
    mir_stream_puts("\tadd hl,de\n", out);
    mir_machine_emit_hl_offset(out, plan->kind_offset, 0);
    mir_stream_printf(out,
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld a,e\n\tcp %d\n\tjp nz,L%d\n"
            "\tld a,d\n\tcp %d\n\tjp nz,L%d\n",
            plan->wanted_kind & 0xff, loop_label,
            (plan->wanted_kind >> 8) & 0xff, loop_label);
    mir_machine_emit_hl_offset(
        out, plan->value_offset - plan->kind_offset - 1, 0);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n", out);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tld h,b\n\tld l,c\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n"
            "\tld hl,1\n\tret\n"
            "L%d:\n\tld hl,0\n\tret\n",
            plan->target_stack_offset, done_label);
}

static int mir_match_pointer_member_any2(
    struct MirPointerMemberAny2 *plan)
{
    static const int expected_opcodes[35] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST,
        MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_PHI,
        MIR_RETURN
    };
    const struct MirInsn *pointer = &mir.insns[1];
    const struct MirInsn *value = &mir.insns[2];
    const struct MirInsn *first_member = &mir.insns[5];
    const struct MirInsn *second_member = &mir.insns[17];
    long first_index_value, second_index_value;
    int first_index, second_index;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 35 || mir_cfg_block_count() != 8 ||
        mir.has_vla || type_size(mir.return_type) != 2 ||
        type_ptr_depth(mir.return_type) != 0 ||
        type_ptr_depth(pointer->type) != 1 ||
        type_size(value->type) != 2 ||
        type_ptr_depth(value->type) != 0)
        return mir_machine_reject("pointer-member-any2", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject("pointer-member-any2", "opcode");
    if (!mir_machine_parameter_value_offset(
            pointer->dst, &plan->pointer_stack_offset) ||
        !mir_machine_parameter_value_offset(
            value->dst, &plan->value_stack_offset) ||
        first_member->src1 != pointer->dst ||
        second_member->src1 != pointer->dst ||
        first_member->immediate != second_member->immediate ||
        strcmp(first_member->name, second_member->name) ||
        !mir_machine_constant_value(
            mir.insns[6].dst, &first_index_value, 0) ||
        !mir_machine_constant_value(
            mir.insns[18].dst, &second_index_value, 0))
        return mir_machine_reject("pointer-member-any2", "addresses");
    first_index = (int)first_index_value;
    second_index = (int)second_index_value;
    if ((long)first_index != first_index_value ||
        (long)second_index != second_index_value)
        return mir_machine_reject("pointer-member-any2", "index-range");
    if (first_index < 0 || first_index > 32767 ||
        second_index != first_index + 1 ||
        first_member->immediate < 0 ||
        first_member->immediate > 32767 ||
        first_index >
            (32767 - first_member->immediate) / 2 ||
        second_index >
            (32767 - second_member->immediate) / 2 ||
        mir.insns[7].src1 != first_member->dst ||
        mir.insns[7].src2 != mir.insns[6].dst ||
        mir.insns[7].immediate != 2 ||
        mir.insns[19].src1 != second_member->dst ||
        mir.insns[19].src2 != mir.insns[18].dst ||
        mir.insns[19].immediate != 2 ||
        mir.insns[8].src1 != mir.insns[7].dst ||
        mir.insns[8].memory_size != 2 ||
        mir.insns[20].src1 != mir.insns[19].dst ||
        mir.insns[20].memory_size != 2)
        return mir_machine_reject("pointer-member-any2", "indexes");
    if (mir.insns[9].immediate != TOK_EQ ||
        mir.insns[9].src1 != value->dst ||
        mir.insns[9].src2 != mir.insns[8].dst ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[10].label != mir.insns[14].label ||
        !mir_machine_constant_equals(mir.insns[12].dst, 1) ||
        mir.insns[13].label != mir.insns[32].label ||
        mir.insns[21].immediate != TOK_EQ ||
        mir.insns[21].src1 != value->dst ||
        mir.insns[21].src2 != mir.insns[20].dst ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        mir.insns[22].label != mir.insns[26].label ||
        !mir_machine_constant_equals(mir.insns[24].dst, 1) ||
        mir.insns[25].label != mir.insns[28].label ||
        !mir_machine_constant_equals(mir.insns[27].dst, 0) ||
        mir.insns[29].src1 != mir.insns[24].dst ||
        mir.insns[29].src2 != mir.insns[27].dst ||
        mir.insns[29].phi_pred1 != mir.insns[23].label ||
        mir.insns[29].phi_pred2 != mir.insns[26].label ||
        mir.insns[31].label != mir.insns[32].label ||
        mir.insns[33].src1 != mir.insns[12].dst ||
        mir.insns[33].src2 != mir.insns[29].dst ||
        mir.insns[33].phi_pred1 != mir.insns[11].label ||
        mir.insns[33].phi_pred2 != mir.insns[30].label ||
        mir.insns[34].src1 != mir.insns[33].dst)
        return mir_machine_reject("pointer-member-any2", "flow");
    plan->member_offsets[0] =
        (int)first_member->immediate + first_index * 2;
    plan->member_offsets[1] =
        (int)second_member->immediate + second_index * 2;
    return 1;
}

static void mir_emit_pointer_member_any2(
    MirStream *out, const struct MirPointerMemberAny2 *plan)
{
    int match = new_label();
    int member;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n",
            plan->value_stack_offset);
    for (member = 0; member < 2; ++member) {
        mir_stream_printf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n",
                plan->pointer_stack_offset);
        mir_machine_emit_hl_offset(
            out, plan->member_offsets[member], 0);
        mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
              "\tld a,e\n\txor c\n\tld e,a\n"
              "\tld a,d\n\txor b\n\tor e\n", out);
        mir_stream_printf(out, "\tjp z,L%d\n", match);
    }
    mir_stream_puts("\tld hl,0\n\tret\n", out);
    mir_stream_printf(out, "L%d:\n\tld hl,1\n\tret\n", match);
}

int mir_try_emit_byte_scan_kernels(MirStream *out)
{
    struct MirFixedRowWordSum fixed_row_word_sum;
    struct MirFixedWideZero fixed_wide_zero;
    struct MirFixedMemberWideZero fixed_member_wide_zero;
    struct MirConstantByteFill constant_byte_fill;
    struct MirAffineByteFill affine_byte_fill;
    struct MirPalindromeScan palindrome_scan;
    struct MirDynamicRowScan dynamic_row_scan;
    struct MirByteMismatchScan byte_mismatch_scan;
    struct MirVariableByteStepSum variable_byte_step_sum;
    struct MirFixedReverseWordCopy fixed_reverse_word_copy;
    struct MirFixedRandomWordFill fixed_random_word_fill;
    struct MirGlobalByteCopyState global_byte_copy_state;
    struct MirFixedGlobalStrideCall fixed_global_stride_call;
    struct MirFixedPredictionLoop fixed_prediction_loop;
    struct MirRandomWideFill random_wide_fill;
    struct MirFixedByteBoardCall fixed_byte_board_call;
    struct MirConstantLoopCheck constant_loop_check;
    struct MirGlobalByteCountdown global_byte_countdown;
    struct MirConditionalStringReport conditional_string_report;
    struct MirByteMismatchReporter byte_mismatch_reporter;
    struct MirCompactRecordAppend compact_record_append;
    struct MirSignedMulClampAbs signed_mul_clamp_abs;
    struct MirFixedGlobalStringCopies fixed_global_string_copies;
    struct MirScaledGlobalStore scaled_global_store;
    struct MirScaledGlobalLoad scaled_global_load;
    struct MirWideHash33 wide_hash33;
    struct MirFileLineLoop file_line_loop;
    struct MirMixedScalarCallReport mixed_scalar_call_report;
    struct MirVolatileMemberSum volatile_member_sum;
    struct MirFixedMemberInitCalls fixed_member_init_calls;
    struct MirLocalByteFillCall local_byte_fill_call;
    struct MirGlobalLastRecordKind global_last_record_kind;
    struct MirAggregateByteFillReturn aggregate_byte_fill_return;
    struct MirFixedCallReductionReport fixed_call_reduction_report;
    struct MirStringPutcharLoop string_putchar_loop;
    struct MirFixedAllocationRunner fixed_allocation_runner;
    struct MirFloatModuloNormalize float_modulo_normalize;
    struct MirNoArgTestRunner noarg_test_runner;
    struct MirStringCheckReport string_check_report;
    struct MirSequentialScalarCallReport sequential_scalar_call_report;
    struct MirFloatNanBits float_nan_bits;
    struct MirRandomUniqueInit random_unique_init;
    struct MirFixedRowFind fixed_row_find;
    struct MirLocalByteFillCallReports local_byte_fill_call_reports;
    struct MirGlobalRecordPop global_record_pop;
    struct MirPointerMemberAny2 pointer_member_any2;

    if (mir_match_fixed_row_word_sum(&fixed_row_word_sum)) {
        mir_emit_fixed_row_word_sum(out, &fixed_row_word_sum);
        return 1;
    }
    if (mir_match_fixed_wide_zero(&fixed_wide_zero)) {
        mir_emit_fixed_wide_zero(out, &fixed_wide_zero);
        return 1;
    }
    if (mir_match_fixed_member_wide_zero(&fixed_member_wide_zero)) {
        mir_emit_fixed_member_wide_zero(out, &fixed_member_wide_zero);
        return 1;
    }
    if (mir_match_fixed_member_wide_zero_fill(
            &fixed_member_wide_zero)) {
        mir_emit_fixed_member_wide_zero_fill(
            out, &fixed_member_wide_zero);
        return 1;
    }
    if (mir_match_constant_byte_fill(&constant_byte_fill)) {
        mir_emit_constant_byte_fill(out, &constant_byte_fill);
        return 1;
    }
    if (mir_match_affine_byte_fill(&affine_byte_fill)) {
        mir_emit_affine_byte_fill(out, &affine_byte_fill);
        return 1;
    }
    if (mir_match_scaled_byte_fill(&affine_byte_fill)) {
        mir_emit_affine_byte_fill(out, &affine_byte_fill);
        return 1;
    }
    if (mir_match_wide_left_shift_count()) {
        mir_emit_wide_left_shift_count(out);
        return 1;
    }
    if (mir_match_palindrome_scan(&palindrome_scan)) {
        mir_emit_palindrome_scan(out, &palindrome_scan);
        return 1;
    }
    if (mir_match_dynamic_row_scan(&dynamic_row_scan)) {
        mir_emit_dynamic_row_scan(out, &dynamic_row_scan);
        return 1;
    }
    if (mir_match_byte_mismatch_scan(
            &byte_mismatch_scan)) {
        mir_emit_byte_mismatch_scan(
            out, &byte_mismatch_scan);
        return 1;
    }
    if (mir_match_variable_byte_step_sum(
            &variable_byte_step_sum) ||
        mir_match_alias_byte_step_sum(
            &variable_byte_step_sum)) {
        mir_emit_variable_byte_step_sum(
            out, &variable_byte_step_sum);
        return 1;
    }
    if (mir_match_fixed_reverse_word_copy(
            &fixed_reverse_word_copy)) {
        mir_emit_fixed_reverse_word_copy(
            out, &fixed_reverse_word_copy);
        return 1;
    }
    if (mir_match_fixed_random_word_fill(
            &fixed_random_word_fill)) {
        mir_emit_fixed_random_word_fill(
            out, &fixed_random_word_fill);
        return 1;
    }
    if (mir_match_global_byte_copy_state(
            &global_byte_copy_state)) {
        mir_emit_global_byte_copy_state(
            out, &global_byte_copy_state);
        return 1;
    }
    if (mir_match_fixed_global_stride_call(
            &fixed_global_stride_call)) {
        mir_emit_fixed_global_stride_call(
            out, &fixed_global_stride_call);
        return 1;
    }
    if (mir_match_fixed_prediction_count(
            &fixed_prediction_loop) ||
        mir_match_fixed_prediction_check(
            &fixed_prediction_loop)) {
        mir_emit_fixed_prediction_count(
            out, &fixed_prediction_loop);
        return 1;
    }
    if (mir_match_random_wide_fill(&random_wide_fill)) {
        mir_emit_random_wide_fill(out, &random_wide_fill);
        return 1;
    }
    if (mir_match_fixed_byte_board_call(
            &fixed_byte_board_call)) {
        mir_emit_fixed_byte_board_call(
            out, &fixed_byte_board_call);
        return 1;
    }
    if (mir_match_constant_loop_check(&constant_loop_check)) {
        mir_emit_constant_loop_check(out, &constant_loop_check);
        return 1;
    }
    {
        int numeric_result =
            mir_try_emit_numeric_kernels(out, 2);

        if (numeric_result >= 0)
            return numeric_result;
    }
    if (mir_match_global_byte_countdown(
            &global_byte_countdown)) {
        mir_emit_global_byte_countdown(
            out, &global_byte_countdown);
        return 1;
    }
    if (mir_match_conditional_string_report(
            &conditional_string_report)) {
        mir_emit_conditional_string_report(
            out, &conditional_string_report);
        return 1;
    }
    if (mir_match_byte_mismatch_reporter(&byte_mismatch_reporter)) {
        mir_emit_byte_mismatch_reporter(out, &byte_mismatch_reporter);
        return 1;
    }
    if (mir_match_compact_record_append(&compact_record_append)) {
        mir_emit_compact_record_append(out, &compact_record_append);
        return 1;
    }
    if (mir_match_signed_mul_clamp_abs(&signed_mul_clamp_abs)) {
        mir_emit_signed_mul_clamp_abs(out, &signed_mul_clamp_abs);
        return 1;
    }
    if (mir_match_fixed_global_string_copies(
            &fixed_global_string_copies)) {
        mir_emit_fixed_global_string_copies(
            out, &fixed_global_string_copies);
        return 1;
    }
    if (mir_match_scaled_global_store(&scaled_global_store)) {
        mir_emit_scaled_global_store(out, &scaled_global_store);
        return 1;
    }
    if (mir_match_scaled_global_load(&scaled_global_load)) {
        mir_emit_scaled_global_load(out, &scaled_global_load);
        return 1;
    }
    if (mir_match_wide_hash33(&wide_hash33)) {
        mir_emit_wide_hash33(out, &wide_hash33);
        return 1;
    }
    if (mir_match_file_line_loop(&file_line_loop)) {
        mir_emit_file_line_loop(out, &file_line_loop);
        return 1;
    }
    if (mir_match_volatile_local_widths()) {
        mir_emit_volatile_local_widths(out);
        return 1;
    }
    if (mir_match_mixed_scalar_call_report(
            &mixed_scalar_call_report)) {
        mir_emit_mixed_scalar_call_report(
            out, &mixed_scalar_call_report);
        return 1;
    }
    if (mir_match_volatile_member_sum(&volatile_member_sum)) {
        mir_emit_volatile_member_sum(out, &volatile_member_sum);
        return 1;
    }
    if (mir_match_fixed_member_init_calls(
            &fixed_member_init_calls)) {
        mir_emit_fixed_member_init_calls(
            out, &fixed_member_init_calls);
        return 1;
    }
    if (mir_match_local_byte_fill_call(&local_byte_fill_call)) {
        mir_emit_local_byte_fill_call(out, &local_byte_fill_call);
        return 1;
    }
    if (mir_match_global_last_record_kind(
            &global_last_record_kind)) {
        mir_emit_global_last_record_kind(
            out, &global_last_record_kind);
        return 1;
    }
    if (mir_match_aggregate_byte_fill_return(
            &aggregate_byte_fill_return)) {
        mir_emit_aggregate_byte_fill_return(
            out, &aggregate_byte_fill_return);
        return 1;
    }
    if (mir_match_fixed_call_reduction_report(
            &fixed_call_reduction_report)) {
        mir_emit_fixed_call_reduction_report(
            out, &fixed_call_reduction_report);
        return 1;
    }
    if (mir_match_string_putchar_loop(&string_putchar_loop)) {
        mir_emit_string_putchar_loop(out, &string_putchar_loop);
        return 1;
    }
    if (mir_match_fixed_allocation_runner(
            &fixed_allocation_runner)) {
        mir_emit_fixed_allocation_runner(
            out, &fixed_allocation_runner);
        return 1;
    }
    if (mir_match_float_modulo_normalize(
            &float_modulo_normalize)) {
        mir_emit_float_modulo_normalize(
            out, &float_modulo_normalize);
        return 1;
    }
    if (mir_match_noarg_test_runner(&noarg_test_runner)) {
        mir_emit_noarg_test_runner(out, &noarg_test_runner);
        return 1;
    }
    if (mir_match_string_check_report(&string_check_report)) {
        mir_emit_string_check_report(out, &string_check_report);
        return 1;
    }
    if (mir_match_sequential_scalar_call_report(
            &sequential_scalar_call_report)) {
        mir_emit_sequential_scalar_call_report(
            out, &sequential_scalar_call_report);
        return 1;
    }
    if (mir_match_float_nan_bits(&float_nan_bits)) {
        mir_emit_float_nan_bits(out, &float_nan_bits);
        return 1;
    }
    if (mir_match_random_unique_init(&random_unique_init)) {
        mir_emit_random_unique_init(out, &random_unique_init);
        return 1;
    }
    if (mir_match_fixed_row_find(&fixed_row_find)) {
        mir_emit_fixed_row_find(out, &fixed_row_find);
        return 1;
    }
    if (mir_match_affine_local_fill_call_reports(
            &local_byte_fill_call_reports) ||
        mir_match_local_byte_fill_call_reports(
            &local_byte_fill_call_reports)) {
        mir_emit_local_byte_fill_call_reports(
            out, &local_byte_fill_call_reports);
        return 1;
    }
    if (mir_match_global_record_pop(&global_record_pop)) {
        mir_emit_global_record_pop(out, &global_record_pop);
        return 1;
    }
    if (mir_match_pointer_member_any2(&pointer_member_any2)) {
        mir_emit_pointer_member_any2(out, &pointer_member_any2);
        return 1;
    }
    return -1;
}
