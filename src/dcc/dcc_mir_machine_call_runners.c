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

int mir_try_emit_call_runners(FILE *out, int phase)
{
    if (phase == 0) {
        struct MirLongIndexCallRunner long_index_plan;
        struct MirFixedCallCheckRunner plan;

        if (mir_match_long_index_call_runner(&long_index_plan)) {
            mir_emit_long_index_call_runner(
                out, &long_index_plan);
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
