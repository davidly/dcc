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

int mir_try_emit_call_runners(FILE *out, int phase)
{
    if (phase == 0) {
        struct MirMemoryExerciseRunner memory_plan;
        struct MirAllocationLifetimeRunner allocation_plan;
        struct MirCallbackRegistrationRunner callback_plan;
        struct MirForIncrementRunner for_increment_plan;
        struct MirByteEqualityRunner byte_equality_plan;
        struct MirGnarlyRunner gnarly_plan;
        struct MirNestedForRunner nested_for_plan;
        struct MirLongIndexCallRunner long_index_plan;
        struct MirFixedCallCheckRunner plan;

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
