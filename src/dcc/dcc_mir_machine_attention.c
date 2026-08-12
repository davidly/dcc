/* dcc_mir_machine_attention.c - strict attention-kernel schedules. */

#include "dcc_mir_machine_internal.h"

enum MirMatrixProductKind {
    MIR_MATRIX_PRODUCT_TRANSPOSED = 1,
    MIR_MATRIX_PRODUCT_OUTER = 2,
    MIR_MATRIX_PRODUCT_ADD = 3
};

struct MirMatrixProductSchedule {
    int kind;
    struct Sym *clear_function;
    struct Sym *convert_function;
    struct Sym *clamp_function;
    int matrix_stack_offset;
    int source_stack_offset;
    int vector_stack_offset;
    int rows_stack_offset;
    int columns_stack_offset;
};

struct MirSoftmaxSchedule {
    struct Sym *maximum_function;
    struct Sym *clamp_function;
    struct Sym *table;
    int table_offset;
    int vector_stack_offset;
    int length_stack_offset;
};

struct MirBackwardLocation {
    struct Sym *root;
    int offset;
};

enum MirBackwardLocationKind {
    MIR_BACKWARD_ATTENTION_OUTPUT_GRADIENTS,
    MIR_BACKWARD_LOGIT_GRADIENTS,
    MIR_BACKWARD_LOGITS,
    MIR_BACKWARD_TARGETS,
    MIR_BACKWARD_OUTPUT_WEIGHT_GRADIENTS,
    MIR_BACKWARD_ATTENTION_OUTPUT,
    MIR_BACKWARD_OUTPUT_WEIGHTS,
    MIR_BACKWARD_VALUE_STATE_GRADIENTS,
    MIR_BACKWARD_ATTENTION_SCORE_GRADIENTS,
    MIR_BACKWARD_ATTENTION_WORKSPACE,
    MIR_BACKWARD_QUERY_STATE_GRADIENTS,
    MIR_BACKWARD_GRADIENT_COLUMN,
    MIR_BACKWARD_KEY_STATE_GRADIENTS,
    MIR_BACKWARD_EMBEDDING_GRADIENTS,
    MIR_BACKWARD_QUERY_WEIGHTS,
    MIR_BACKWARD_QUERY_WEIGHT_GRADIENTS,
    MIR_BACKWARD_EMBEDDINGS,
    MIR_BACKWARD_KEY_WEIGHTS,
    MIR_BACKWARD_KEY_WEIGHT_GRADIENTS,
    MIR_BACKWARD_VALUE_WEIGHTS,
    MIR_BACKWARD_VALUE_WEIGHT_GRADIENTS,
    MIR_BACKWARD_TOKENS,
    MIR_BACKWARD_TOKEN_GRADIENTS,
    MIR_BACKWARD_POSITION_GRADIENTS,
    MIR_BACKWARD_LOCATION_COUNT
};

enum MirBackwardFunctionKind {
    MIR_BACKWARD_CLEAR_FUNCTION,
    MIR_BACKWARD_COPY_FUNCTION,
    MIR_BACKWARD_SOFTMAX_FUNCTION,
    MIR_BACKWARD_CLAMP_FUNCTION,
    MIR_BACKWARD_OUTER_PRODUCT_FUNCTION,
    MIR_BACKWARD_MATRIX_MULTIPLY_FUNCTION,
    MIR_BACKWARD_DOT_PRODUCT_FUNCTION,
    MIR_BACKWARD_SCALED_ADD_FUNCTION,
    MIR_BACKWARD_Q16_FUNCTION,
    MIR_BACKWARD_SHIFT_FUNCTION,
    MIR_BACKWARD_TRANSPOSE_FUNCTION,
    MIR_BACKWARD_MATRIX_ADD_FUNCTION,
    MIR_BACKWARD_FUNCTION_COUNT
};

struct MirBackwardPassSchedule {
    struct MirBackwardLocation locations[MIR_BACKWARD_LOCATION_COUNT];
    struct Sym *functions[MIR_BACKWARD_FUNCTION_COUNT];
};

static int mir_attention_call_arguments(
    const struct MirInsn *call, int count, int *arguments)
{
    int found = 0;
    int instruction;
    int argument;

    if (count < 0 || count > 5)
        return 0;
    for (argument = 0; argument < count; ++argument)
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

static int mir_match_matrix_product_word_type(int type)
{
    return type_ptr_depth(type) == 0 &&
           !type_is_float(type) &&
           (type & 15) == TYPE_INT &&
           (type & TYPE_UNSIGNED) == 0 &&
           type_size(type) == 2;
}

static int mir_match_matrix_product_long_type(int type)
{
    return type_ptr_depth(type) == 0 &&
           !type_is_float(type) &&
           (type & 15) == TYPE_LONG &&
           (type & TYPE_UNSIGNED) == 0 &&
           type_size(type) == 4;
}

static int mir_match_matrix_product_pointer_type(int type)
{
    return type_ptr_depth(type) == 1 &&
           !type_is_float(type) &&
           (type & 15) == TYPE_INT &&
           (type & TYPE_UNSIGNED) == 0 &&
           type_size(type) == 2;
}

static int mir_match_matrix_product_count_type(int type)
{
    return type_ptr_depth(type) == 0 &&
           !type_is_float(type) &&
           (type & 15) == TYPE_CHAR &&
           (type & TYPE_UNSIGNED) != 0 &&
           type_size(type) == 1;
}

static int mir_match_matrix_product_parameter(
    int instruction, int expected_offset, int is_pointer,
    int *stack_offset)
{
    const struct MirInsn *parameter = &mir.insns[instruction];

    if (parameter->opcode != MIR_PARAM ||
        (is_pointer
             ? !mir_match_matrix_product_pointer_type(parameter->type)
             : !mir_match_matrix_product_count_type(parameter->type)) ||
        !mir_machine_parameter_value_offset(
            parameter->dst, stack_offset) ||
        *stack_offset != expected_offset)
        return 0;
    return !is_pointer ||
           !mir_machine_pointee_is_volatile(parameter);
}

static int mir_match_matrix_product_call(
    const struct MirInsn *call, int argument,
    struct Sym **function_out)
{
    struct Sym *function;
    int actual;

    if (!mir_attention_call_arguments(call, 1, &actual) ||
        actual != argument ||
        !mir_match_matrix_product_word_type(call->type) ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0)
        return 0;
    function = find_global(call->name);
    if (function == NULL || !function->is_defined ||
        function->storage != SC_FUNC ||
        function->is_funcptr || function->is_noreturn ||
        !function->has_proto || function->proto_variadic ||
        function->proto_nargs != 1 ||
        !mir_match_matrix_product_long_type(
            function->proto_types[0]) ||
        (call->base_name[0] != 0 &&
         strcmp(call->base_name,
                asm_name_for(sym_asm_name(function)))))
        return 0;
    *function_out = function;
    return 1;
}

static int mir_match_matrix_product_clear(
    struct MirMatrixProductSchedule *plan)
{
    const struct MirInsn *call = &mir.insns[18];
    struct Sym *function;
    int arguments[3];

    if (!mir_attention_call_arguments(call, 3, arguments) ||
        arguments[0] != mir.insns[6].dst ||
        arguments[1] != mir.insns[9].dst ||
        arguments[2] != mir.insns[15].dst ||
        !mir_machine_same_location(
            &mir.insns[3], &mir.insns[6]) ||
        !mir_machine_constant_equals(mir.insns[9].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[12].dst, 2) ||
        mir.insns[14].opcode != MIR_UNARY ||
        mir.insns[14].immediate != 0 ||
        mir.insns[14].src1 != mir.insns[5].dst ||
        !mir_match_matrix_product_word_type(mir.insns[14].type) ||
        mir.insns[15].opcode != MIR_BINARY ||
        mir.insns[15].immediate != '*' ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        mir.insns[15].src2 != mir.insns[12].dst ||
        !mir_match_matrix_product_word_type(
            mir.insns[15].secondary_offset) ||
        type_ptr_depth(call->type) == 0 ||
        type_size(call->type) != 2 ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME |
          MIR_CALL_FLAG_INLINE_SUBSTITUTABLE)) != 0)
        return 0;
    function = find_global(call->name);
    if (function == NULL || function->storage != SC_FUNC ||
        function->is_funcptr || function->is_noreturn ||
        !function->has_proto || function->proto_variadic ||
        function->proto_nargs != 3 ||
        type_ptr_depth(function->proto_types[0]) == 0 ||
        type_size(function->proto_types[0]) != 2 ||
        type_ptr_depth(function->proto_types[1]) != 0 ||
        type_size(function->proto_types[1]) != 2 ||
        type_ptr_depth(function->proto_types[2]) != 0 ||
        type_size(function->proto_types[2]) != 2 ||
        strcmp(asm_name_for(sym_asm_name(function)), "__mset") ||
        (call->base_name[0] != 0 &&
         strcmp(call->base_name,
                asm_name_for(sym_asm_name(function)))))
        return 0;
    plan->clear_function = function;
    return 1;
}

static int mir_match_matrix_product_schedule_kind(
    struct MirMatrixProductSchedule *plan, int kind)
{
    static const int transposed_opcodes[101] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_PARAM,
        MIR_PARAM, MIR_LOAD, MIR_NOP, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_NOP, MIR_CONST, MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_NOP,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL,
        MIR_LOAD, MIR_LOAD, MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP,
        MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_LOAD_INDIRECT,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL,
        MIR_LOAD, MIR_LOAD, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_LOAD, MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_LOAD_INDIRECT, MIR_STORE, MIR_LOAD, MIR_UNARY, MIR_NOP,
        MIR_UNARY, MIR_BINARY, MIR_ARG, MIR_CALL, MIR_STORE, MIR_LOAD,
        MIR_LOAD, MIR_INDEX_ADDRESS, MIR_STORE, MIR_LOAD, MIR_LOAD,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_LOAD, MIR_UNARY, MIR_BINARY,
        MIR_ARG, MIR_CALL, MIR_STORE_INDIRECT, MIR_LABEL, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL
    };
    static const int outer_opcodes[90] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_PARAM,
        MIR_PARAM, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_LOAD,
        MIR_LOAD, MIR_NOP, MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP, MIR_NOP,
        MIR_UNARY, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_LOAD_INDIRECT, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_LOAD,
        MIR_LOAD, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_LOAD, MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_LOAD, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_STORE, MIR_NOP, MIR_UNARY, MIR_LOAD,
        MIR_UNARY, MIR_BINARY, MIR_ARG, MIR_CALL, MIR_STORE, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_STORE, MIR_LOAD, MIR_LOAD,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_LOAD, MIR_UNARY, MIR_BINARY,
        MIR_ARG, MIR_CALL, MIR_STORE_INDIRECT, MIR_LABEL, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL
    };
    const int *expected = kind == MIR_MATRIX_PRODUCT_TRANSPOSED
        ? transposed_opcodes : outer_opcodes;
    int count = kind == MIR_MATRIX_PRODUCT_TRANSPOSED ? 101 : 90;
    int outer_initial = kind == MIR_MATRIX_PRODUCT_TRANSPOSED ? 20 : 7;
    int outer_store = outer_initial + 1;
    int outer_label = kind == MIR_MATRIX_PRODUCT_TRANSPOSED ? 22 : 9;
    int matrix_load = outer_label + 1;
    int source_load = outer_label + 2;
    int outer_phi = kind == MIR_MATRIX_PRODUCT_TRANSPOSED ? 27 : 15;
    int outer_conversion = outer_phi + 3;
    int rows_conversion = outer_phi + 4;
    int outer_compare = outer_phi + 5;
    int outer_branch = outer_phi + 6;
    int source_reload = kind == MIR_MATRIX_PRODUCT_TRANSPOSED ? 34 : 22;
    int source_step = source_reload + 2;
    int source_store = source_reload + 3;
    int scalar_load = source_reload + 4;
    int scalar_store = source_reload + 6;
    int inner_initial = kind == MIR_MATRIX_PRODUCT_TRANSPOSED ? 42 : 30;
    int inner_store = inner_initial + 1;
    int inner_label = inner_initial + 2;
    int inner_phi_load = kind == MIR_MATRIX_PRODUCT_TRANSPOSED ? 52 : 41;
    int columns_conversion = inner_phi_load + 3;
    int inner_compare = inner_phi_load + 4;
    int inner_branch = inner_phi_load + 5;
    int convert_call = kind == MIR_MATRIX_PRODUCT_TRANSPOSED ? 70 : 58;
    int clamp_call = kind == MIR_MATRIX_PRODUCT_TRANSPOSED ? 84 : 73;
    int inner_continue = kind == MIR_MATRIX_PRODUCT_TRANSPOSED ? 86 : 75;
    int inner_increment = inner_continue + 3;
    int inner_loop_store = inner_continue + 4;
    int inner_jump = inner_continue + 5;
    int inner_done = inner_continue + 6;
    int outer_continue = inner_continue + 8;
    int outer_increment = outer_continue + 3;
    int outer_loop_store = outer_continue + 4;
    int outer_jump = outer_continue + 5;
    int outer_done = outer_continue + 6;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != count || mir_cfg_block_count() != 7 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID ||
        mir.aggregate_temp_bytes != 0)
        return 0;
    for (instruction = 0; instruction < count; ++instruction)
        if (mir.insns[instruction].opcode != expected[instruction])
            return mir_machine_reject(
                "matrix-product-schedule", "opcodes");
    if (!mir_match_matrix_product_parameter(
            1, 2, 1, &plan->matrix_stack_offset) ||
        !mir_match_matrix_product_parameter(
            2, 4, 1, &plan->source_stack_offset) ||
        !mir_match_matrix_product_parameter(
            3, 6, 1, &plan->vector_stack_offset) ||
        !mir_match_matrix_product_parameter(
            4, 8, 0, &plan->rows_stack_offset) ||
        !mir_match_matrix_product_parameter(
            5, 10, 0, &plan->columns_stack_offset))
        return mir_machine_reject(
            "matrix-product-schedule", "parameters");
    plan->kind = kind;
    if (kind == MIR_MATRIX_PRODUCT_TRANSPOSED &&
        !mir_match_matrix_product_clear(plan))
        return mir_machine_reject(
            "matrix-product-schedule", "clear");

    if (!mir_machine_constant_equals(
            mir.insns[outer_initial].dst, 0) ||
        !mir_machine_unobservable_local_store(
            &mir.insns[outer_store]) ||
        mir.insns[outer_store].src1 !=
            mir.insns[outer_initial].dst ||
        !mir_machine_same_location(
            &mir.insns[1], &mir.insns[matrix_load]) ||
        !mir_machine_same_location(
            &mir.insns[2], &mir.insns[source_load]) ||
        mir.insns[outer_phi].src1 !=
            mir.insns[outer_initial].dst ||
        mir.insns[outer_phi].src2 !=
            mir.insns[outer_increment].dst ||
        mir.insns[outer_phi].phi_pred1 != mir.insns[0].label ||
        mir.insns[outer_phi].phi_pred2 !=
            mir.insns[outer_continue].label ||
        mir.insns[outer_phi].object !=
            mir.insns[outer_store].object ||
        !mir_match_matrix_product_count_type(
            mir.insns[outer_phi].type) ||
        mir.insns[outer_conversion].opcode != MIR_UNARY ||
        mir.insns[outer_conversion].immediate != 0 ||
        mir.insns[outer_conversion].src1 !=
            mir.insns[outer_phi].dst ||
        !mir_match_matrix_product_word_type(
            mir.insns[outer_conversion].type) ||
        mir.insns[rows_conversion].opcode != MIR_UNARY ||
        mir.insns[rows_conversion].immediate != 0 ||
        mir.insns[rows_conversion].src1 != mir.insns[4].dst ||
        !mir_match_matrix_product_word_type(
            mir.insns[rows_conversion].type) ||
        mir.insns[outer_compare].immediate != '<' ||
        mir.insns[outer_compare].src1 !=
            mir.insns[outer_conversion].dst ||
        mir.insns[outer_compare].src2 !=
            mir.insns[rows_conversion].dst ||
        !mir_match_matrix_product_word_type(
            mir.insns[outer_compare].secondary_offset) ||
        mir.insns[outer_branch].src1 !=
            mir.insns[outer_compare].dst ||
        mir.insns[outer_branch].label !=
            mir.insns[outer_done].label)
        return mir_machine_reject(
            "matrix-product-schedule", "outer-loop");

    if (!mir_machine_same_location(
            &mir.insns[2], &mir.insns[source_reload]) ||
        !mir_machine_constant_equals(
            mir.insns[source_reload + 1].dst, 2) ||
        mir.insns[source_step].immediate != '+' ||
        mir.insns[source_step].src1 !=
            mir.insns[source_reload].dst ||
        mir.insns[source_step].src2 !=
            mir.insns[source_reload + 1].dst ||
        !mir_machine_same_location(
            &mir.insns[2], &mir.insns[source_store]) ||
        mir.insns[source_store].src1 !=
            mir.insns[source_step].dst ||
        mir.insns[scalar_load].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[scalar_load].src1 !=
            mir.insns[source_reload].dst ||
        mir.insns[scalar_load].memory_size != 2 ||
        (mir.insns[scalar_load].memory_flags & (1 | 8)) != 0 ||
        !mir_match_matrix_product_word_type(
            mir.insns[scalar_load].type) ||
        !mir_machine_unobservable_local_store(
            &mir.insns[scalar_store]) ||
        mir.insns[scalar_store].src1 !=
            mir.insns[scalar_load].dst ||
        !mir_machine_constant_equals(
            mir.insns[inner_initial].dst, 0) ||
        !mir_machine_unobservable_local_store(
            &mir.insns[inner_store]) ||
        mir.insns[inner_store].src1 !=
            mir.insns[inner_initial].dst)
        return mir_machine_reject(
            "matrix-product-schedule", "source");

    if (mir.insns[inner_phi_load].opcode != MIR_LOAD ||
        !mir_machine_same_location(
            &mir.insns[inner_store],
            &mir.insns[inner_phi_load]) ||
        mir.insns[columns_conversion].opcode != MIR_UNARY ||
        mir.insns[columns_conversion].immediate != 0 ||
        mir.insns[columns_conversion].src1 != mir.insns[5].dst ||
        !mir_match_matrix_product_word_type(
            mir.insns[columns_conversion].type) ||
        mir.insns[inner_compare].immediate != '<' ||
        mir.insns[inner_compare].src1 !=
            mir.insns[inner_phi_load + 2].dst ||
        mir.insns[inner_compare].src2 !=
            mir.insns[columns_conversion].dst ||
        !mir_match_matrix_product_word_type(
            mir.insns[inner_compare].secondary_offset) ||
        mir.insns[inner_branch].src1 !=
            mir.insns[inner_compare].dst ||
        mir.insns[inner_branch].label !=
            mir.insns[inner_done].label)
        return mir_machine_reject(
            "matrix-product-schedule", "inner-loop");

    if (kind == MIR_MATRIX_PRODUCT_TRANSPOSED) {
        if (!mir_machine_same_location(
                &mir.insns[1], &mir.insns[58]) ||
            !mir_machine_constant_equals(mir.insns[59].dst, 2) ||
            mir.insns[60].immediate != '+' ||
            mir.insns[60].src1 != mir.insns[58].dst ||
            mir.insns[60].src2 != mir.insns[59].dst ||
            !mir_machine_same_location(
                &mir.insns[1], &mir.insns[61]) ||
            mir.insns[61].src1 != mir.insns[60].dst ||
            mir.insns[62].src1 != mir.insns[58].dst ||
            mir.insns[62].memory_size != 2 ||
            (mir.insns[62].memory_flags & (1 | 8)) != 0 ||
            !mir_machine_unobservable_local_store(
                &mir.insns[63]) ||
            mir.insns[63].src1 != mir.insns[62].dst ||
            !mir_machine_same_location(
                &mir.insns[63], &mir.insns[64]) ||
            mir.insns[65].src1 != mir.insns[64].dst ||
            !mir_match_matrix_product_long_type(
                mir.insns[65].type) ||
            mir.insns[67].src1 != mir.insns[scalar_load].dst ||
            !mir_match_matrix_product_long_type(
                mir.insns[67].type) ||
            mir.insns[68].immediate != '*' ||
            mir.insns[68].src1 != mir.insns[65].dst ||
            mir.insns[68].src2 != mir.insns[67].dst ||
            !mir_match_matrix_product_long_type(
                mir.insns[68].type) ||
            !mir_match_matrix_product_call(
                &mir.insns[convert_call],
                mir.insns[68].dst,
                &plan->convert_function) ||
            !mir_machine_same_location(
                &mir.insns[3], &mir.insns[72]) ||
            !mir_machine_same_location(
                &mir.insns[inner_store], &mir.insns[73]) ||
            mir.insns[74].src1 != mir.insns[72].dst ||
            mir.insns[74].src2 != mir.insns[73].dst ||
            mir.insns[74].immediate != 2 ||
            mir.insns[74].memory_size != 2 ||
            !mir_machine_unobservable_local_store(
                &mir.insns[75]) ||
            mir.insns[75].src1 != mir.insns[74].dst ||
            !mir_machine_same_location(
                &mir.insns[75], &mir.insns[76]) ||
            !mir_machine_same_location(
                &mir.insns[75], &mir.insns[77]) ||
            mir.insns[78].src1 != mir.insns[77].dst ||
            mir.insns[78].memory_size != 2 ||
            (mir.insns[78].memory_flags & (1 | 8)) != 0 ||
            mir.insns[79].src1 != mir.insns[78].dst ||
            !mir_match_matrix_product_long_type(
                mir.insns[79].type) ||
            !mir_machine_same_location(
                &mir.insns[convert_call + 1],
                &mir.insns[80]) ||
            mir.insns[81].src1 != mir.insns[80].dst ||
            !mir_match_matrix_product_long_type(
                mir.insns[81].type) ||
            mir.insns[82].immediate != '+' ||
            mir.insns[82].src1 != mir.insns[79].dst ||
            mir.insns[82].src2 != mir.insns[81].dst ||
            !mir_match_matrix_product_long_type(
                mir.insns[82].type) ||
            !mir_match_matrix_product_call(
                &mir.insns[clamp_call],
                mir.insns[82].dst,
                &plan->clamp_function) ||
            mir.insns[85].src1 != mir.insns[76].dst ||
            mir.insns[85].src2 != mir.insns[clamp_call].dst ||
            mir.insns[85].memory_size != 2 ||
            (mir.insns[85].memory_flags & (1 | 8)) != 0)
            return mir_machine_reject(
                "matrix-product-schedule", "transposed-body");
    } else {
        if (!mir_machine_same_location(
                &mir.insns[inner_store], &mir.insns[48]) ||
            mir.insns[49].src1 != mir.insns[3].dst ||
            mir.insns[49].src2 != mir.insns[48].dst ||
            mir.insns[49].immediate != 2 ||
            mir.insns[49].memory_size != 2 ||
            mir.insns[50].src1 != mir.insns[49].dst ||
            mir.insns[50].memory_size != 2 ||
            (mir.insns[50].memory_flags & (1 | 8)) != 0 ||
            !mir_machine_unobservable_local_store(
                &mir.insns[51]) ||
            mir.insns[51].src1 != mir.insns[50].dst ||
            mir.insns[53].src1 != mir.insns[scalar_load].dst ||
            !mir_match_matrix_product_long_type(
                mir.insns[53].type) ||
            !mir_machine_same_location(
                &mir.insns[51], &mir.insns[54]) ||
            mir.insns[55].src1 != mir.insns[54].dst ||
            !mir_match_matrix_product_long_type(
                mir.insns[55].type) ||
            mir.insns[56].immediate != '*' ||
            mir.insns[56].src1 != mir.insns[53].dst ||
            mir.insns[56].src2 != mir.insns[55].dst ||
            !mir_match_matrix_product_long_type(
                mir.insns[56].type) ||
            !mir_match_matrix_product_call(
                &mir.insns[convert_call],
                mir.insns[56].dst,
                &plan->convert_function) ||
            !mir_machine_same_location(
                &mir.insns[1], &mir.insns[60]) ||
            !mir_machine_constant_equals(mir.insns[61].dst, 2) ||
            mir.insns[62].immediate != '+' ||
            mir.insns[62].src1 != mir.insns[60].dst ||
            mir.insns[62].src2 != mir.insns[61].dst ||
            !mir_machine_same_location(
                &mir.insns[1], &mir.insns[63]) ||
            mir.insns[63].src1 != mir.insns[62].dst ||
            !mir_machine_unobservable_local_store(
                &mir.insns[64]) ||
            mir.insns[64].src1 != mir.insns[60].dst ||
            !mir_machine_same_location(
                &mir.insns[64], &mir.insns[65]) ||
            !mir_machine_same_location(
                &mir.insns[64], &mir.insns[66]) ||
            mir.insns[67].src1 != mir.insns[66].dst ||
            mir.insns[67].memory_size != 2 ||
            (mir.insns[67].memory_flags & (1 | 8)) != 0 ||
            mir.insns[68].src1 != mir.insns[67].dst ||
            !mir_match_matrix_product_long_type(
                mir.insns[68].type) ||
            !mir_machine_same_location(
                &mir.insns[convert_call + 1],
                &mir.insns[69]) ||
            mir.insns[70].src1 != mir.insns[69].dst ||
            !mir_match_matrix_product_long_type(
                mir.insns[70].type) ||
            mir.insns[71].immediate != '+' ||
            mir.insns[71].src1 != mir.insns[68].dst ||
            mir.insns[71].src2 != mir.insns[70].dst ||
            !mir_match_matrix_product_long_type(
                mir.insns[71].type) ||
            !mir_match_matrix_product_call(
                &mir.insns[clamp_call],
                mir.insns[71].dst,
                &plan->clamp_function) ||
            mir.insns[74].src1 != mir.insns[65].dst ||
            mir.insns[74].src2 != mir.insns[clamp_call].dst ||
            mir.insns[74].memory_size != 2 ||
            (mir.insns[74].memory_flags & (1 | 8)) != 0)
            return mir_machine_reject(
                "matrix-product-schedule", "outer-body");
    }

    if (!mir_machine_constant_equals(
            mir.insns[inner_increment - 1].dst, 1) ||
        mir.insns[inner_increment].immediate != '+' ||
        mir.insns[inner_increment].src1 !=
        mir.insns[inner_continue + 1].dst ||
        mir.insns[inner_increment].src2 !=
            mir.insns[inner_increment - 1].dst ||
        !mir_machine_same_location(
            &mir.insns[inner_store],
            &mir.insns[inner_loop_store]) ||
        mir.insns[inner_loop_store].src1 !=
            mir.insns[inner_increment].dst ||
        mir.insns[inner_jump].label !=
            mir.insns[inner_label].label ||
        !mir_machine_constant_equals(
            mir.insns[outer_increment - 1].dst, 1) ||
        mir.insns[outer_increment].immediate != '+' ||
        mir.insns[outer_increment].src1 !=
            mir.insns[outer_phi].dst ||
        mir.insns[outer_increment].src2 !=
            mir.insns[outer_increment - 1].dst ||
        !mir_machine_same_location(
            &mir.insns[outer_store],
            &mir.insns[outer_loop_store]) ||
        mir.insns[outer_loop_store].src1 !=
            mir.insns[outer_increment].dst ||
        mir.insns[outer_jump].label !=
            mir.insns[outer_label].label)
        return mir_machine_reject(
            "matrix-product-schedule", "increments");
    return 1;
}

static int mir_match_matrix_product_add_schedule(
    struct MirMatrixProductSchedule *plan)
{
    static const int expected_opcodes[141] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_PARAM,
        MIR_PARAM, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL,
        MIR_LOAD, MIR_NOP, MIR_LOAD, MIR_NOP, MIR_NOP, MIR_PHI,
        MIR_NOP, MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_STORE, MIR_NOP,
        MIR_CONST, MIR_STORE, MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_LOAD,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_LOAD,
        MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LOAD, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_NOP, MIR_LOAD,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_BINARY,
        MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_UNARY, MIR_CONST, MIR_BINARY,
        MIR_UNARY, MIR_UNARY, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_UNARY, MIR_LABEL, MIR_LABEL, MIR_PHI,
        MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_LABEL, MIR_PHI,
        MIR_STORE, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_STORE, MIR_LOAD, MIR_LOAD, MIR_LOAD_INDIRECT, MIR_UNARY,
        MIR_LOAD, MIR_UNARY, MIR_BINARY, MIR_ARG, MIR_CALL,
        MIR_STORE_INDIRECT, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL
    };
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 141 || mir_cfg_block_count() != 19 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID ||
        mir.aggregate_temp_bytes != 0)
        return 0;
    for (instruction = 0; instruction < 141; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "matrix-product-add-schedule", "opcodes");
    if (!mir_match_matrix_product_parameter(
            1, 2, 1, &plan->matrix_stack_offset) ||
        !mir_match_matrix_product_parameter(
            2, 4, 1, &plan->source_stack_offset) ||
        !mir_match_matrix_product_parameter(
            3, 6, 1, &plan->vector_stack_offset) ||
        !mir_match_matrix_product_parameter(
            4, 8, 0, &plan->rows_stack_offset) ||
        !mir_match_matrix_product_parameter(
            5, 10, 0, &plan->columns_stack_offset))
        return mir_machine_reject(
            "matrix-product-add-schedule", "parameters");
    plan->kind = MIR_MATRIX_PRODUCT_ADD;

    if (!mir_machine_constant_equals(mir.insns[7].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[8]) ||
        mir.insns[8].src1 != mir.insns[7].dst ||
        !mir_machine_same_location(&mir.insns[1], &mir.insns[10]) ||
        !mir_machine_same_location(&mir.insns[3], &mir.insns[12]) ||
        mir.insns[15].src1 != mir.insns[7].dst ||
        mir.insns[15].src2 != mir.insns[137].dst ||
        mir.insns[15].phi_pred1 != mir.insns[0].label ||
        mir.insns[15].phi_pred2 != mir.insns[134].label ||
        mir.insns[15].object != mir.insns[8].object ||
        !mir_match_matrix_product_count_type(mir.insns[15].type) ||
        mir.insns[18].immediate != 0 ||
        mir.insns[18].src1 != mir.insns[15].dst ||
        !mir_match_matrix_product_word_type(mir.insns[18].type) ||
        mir.insns[19].immediate != 0 ||
        mir.insns[19].src1 != mir.insns[4].dst ||
        !mir_match_matrix_product_word_type(mir.insns[19].type) ||
        mir.insns[20].immediate != '<' ||
        mir.insns[20].src1 != mir.insns[18].dst ||
        mir.insns[20].src2 != mir.insns[19].dst ||
        !mir_match_matrix_product_word_type(
            mir.insns[20].secondary_offset) ||
        mir.insns[21].src1 != mir.insns[20].dst ||
        mir.insns[21].label != mir.insns[140].label)
        return mir_machine_reject(
            "matrix-product-add-schedule", "outer-loop");

    if (!mir_machine_constant_equals(mir.insns[23].dst, 0) ||
        !mir_match_matrix_product_long_type(mir.insns[23].type) ||
        !mir_machine_unobservable_local_store(&mir.insns[24]) ||
        mir.insns[24].src1 != mir.insns[23].dst ||
        !mir_machine_constant_equals(mir.insns[26].dst, 0) ||
        !mir_match_matrix_product_count_type(mir.insns[26].type) ||
        !mir_machine_unobservable_local_store(&mir.insns[27]) ||
        mir.insns[27].src1 != mir.insns[26].dst ||
        !mir_machine_same_location(&mir.insns[1], &mir.insns[29]) ||
        !mir_machine_same_location(&mir.insns[3], &mir.insns[31]) ||
        !mir_machine_same_location(&mir.insns[27], &mir.insns[37]) ||
        mir.insns[39].immediate != 0 ||
        mir.insns[39].src1 != mir.insns[37].dst ||
        !mir_match_matrix_product_word_type(mir.insns[39].type) ||
        mir.insns[40].immediate != 0 ||
        mir.insns[40].src1 != mir.insns[5].dst ||
        !mir_match_matrix_product_word_type(mir.insns[40].type) ||
        mir.insns[41].immediate != '<' ||
        mir.insns[41].src1 != mir.insns[39].dst ||
        mir.insns[41].src2 != mir.insns[40].dst ||
        !mir_match_matrix_product_word_type(
            mir.insns[41].secondary_offset) ||
        mir.insns[42].src1 != mir.insns[41].dst ||
        mir.insns[42].label != mir.insns[65].label)
        return mir_machine_reject(
            "matrix-product-add-schedule", "inner-loop");

    if (!mir_machine_same_location(&mir.insns[24], &mir.insns[43]) ||
        !mir_machine_same_location(&mir.insns[1], &mir.insns[44]) ||
        !mir_machine_constant_equals(mir.insns[45].dst, 2) ||
        mir.insns[46].immediate != '+' ||
        mir.insns[46].src1 != mir.insns[44].dst ||
        mir.insns[46].src2 != mir.insns[45].dst ||
        !mir_match_matrix_product_pointer_type(mir.insns[46].type) ||
        !mir_machine_same_location(&mir.insns[1], &mir.insns[47]) ||
        mir.insns[47].src1 != mir.insns[46].dst ||
        mir.insns[48].src1 != mir.insns[44].dst ||
        mir.insns[48].memory_size != 2 ||
        (mir.insns[48].memory_flags & (1 | 8)) != 0 ||
        !mir_match_matrix_product_word_type(mir.insns[48].type) ||
        mir.insns[49].immediate != 0 ||
        mir.insns[49].src1 != mir.insns[48].dst ||
        !mir_match_matrix_product_long_type(mir.insns[49].type) ||
        !mir_machine_same_location(&mir.insns[27], &mir.insns[51]) ||
        mir.insns[52].src1 != mir.insns[2].dst ||
        mir.insns[52].src2 != mir.insns[51].dst ||
        mir.insns[52].immediate != 2 ||
        mir.insns[52].memory_size != 2 ||
        mir.insns[53].src1 != mir.insns[52].dst ||
        mir.insns[53].memory_size != 2 ||
        (mir.insns[53].memory_flags & (1 | 8)) != 0 ||
        !mir_match_matrix_product_word_type(mir.insns[53].type) ||
        mir.insns[54].immediate != 0 ||
        mir.insns[54].src1 != mir.insns[53].dst ||
        !mir_match_matrix_product_long_type(mir.insns[54].type) ||
        mir.insns[55].immediate != '*' ||
        mir.insns[55].src1 != mir.insns[49].dst ||
        mir.insns[55].src2 != mir.insns[54].dst ||
        !mir_match_matrix_product_long_type(mir.insns[55].type) ||
        mir.insns[56].immediate != '+' ||
        mir.insns[56].src1 != mir.insns[43].dst ||
        mir.insns[56].src2 != mir.insns[55].dst ||
        !mir_match_matrix_product_long_type(mir.insns[56].type) ||
        !mir_machine_same_location(&mir.insns[24], &mir.insns[58]) ||
        mir.insns[58].src1 != mir.insns[56].dst)
        return mir_machine_reject(
            "matrix-product-add-schedule", "product");

    if (!mir_machine_same_location(&mir.insns[27], &mir.insns[60]) ||
        !mir_machine_constant_equals(mir.insns[61].dst, 1) ||
        mir.insns[62].immediate != '+' ||
        mir.insns[62].src1 != mir.insns[60].dst ||
        mir.insns[62].src2 != mir.insns[61].dst ||
        !mir_match_matrix_product_count_type(mir.insns[62].type) ||
        !mir_machine_same_location(&mir.insns[27], &mir.insns[63]) ||
        mir.insns[63].src1 != mir.insns[62].dst ||
        mir.insns[64].label != mir.insns[28].label)
        return mir_machine_reject(
            "matrix-product-add-schedule", "inner-increment");

    if (!mir_machine_same_location(&mir.insns[24], &mir.insns[66]) ||
        !mir_machine_constant_equals(mir.insns[70].dst, 8388352L) ||
        !mir_match_matrix_product_long_type(mir.insns[70].type) ||
        mir.insns[71].immediate != '>' ||
        mir.insns[71].src1 != mir.insns[66].dst ||
        mir.insns[71].src2 != mir.insns[70].dst ||
        !mir_match_matrix_product_long_type(
            mir.insns[71].secondary_offset) ||
        mir.insns[72].src1 != mir.insns[71].dst ||
        mir.insns[72].label != mir.insns[76].label ||
        !mir_machine_constant_equals(mir.insns[73].dst, 32767) ||
        mir.insns[75].label != mir.insns[115].label ||
        !mir_machine_same_location(&mir.insns[24], &mir.insns[77]) ||
        !mir_machine_constant_equals(mir.insns[82].dst, -8388608L) ||
        !mir_match_matrix_product_long_type(mir.insns[82].type) ||
        mir.insns[83].immediate != '<' ||
        mir.insns[83].src1 != mir.insns[77].dst ||
        mir.insns[83].src2 != mir.insns[82].dst ||
        !mir_match_matrix_product_long_type(
            mir.insns[83].secondary_offset) ||
        mir.insns[84].src1 != mir.insns[83].dst ||
        mir.insns[84].label != mir.insns[89].label ||
        !mir_machine_constant_equals(
            mir.insns[86].dst, 4294934528L) ||
        mir.insns[88].label != mir.insns[112].label)
        return mir_machine_reject(
            "matrix-product-add-schedule", "conversion-bounds");

    if (!mir_machine_same_location(&mir.insns[24], &mir.insns[90]) ||
        !mir_machine_constant_equals(mir.insns[92].dst, 0) ||
        mir.insns[93].immediate != '<' ||
        mir.insns[93].src1 != mir.insns[90].dst ||
        mir.insns[93].src2 != mir.insns[92].dst ||
        !mir_match_matrix_product_long_type(
            mir.insns[93].secondary_offset) ||
        mir.insns[94].src1 != mir.insns[93].dst ||
        mir.insns[94].label != mir.insns[103].label ||
        !mir_machine_same_location(&mir.insns[24], &mir.insns[95]) ||
        mir.insns[96].immediate != '-' ||
        mir.insns[96].src1 != mir.insns[95].dst ||
        !mir_match_matrix_product_long_type(mir.insns[96].type) ||
        !mir_machine_constant_equals(mir.insns[97].dst, 8) ||
        mir.insns[98].immediate != TOK_SHR ||
        mir.insns[98].src1 != mir.insns[96].dst ||
        mir.insns[98].src2 != mir.insns[97].dst ||
        !mir_match_matrix_product_long_type(mir.insns[98].type) ||
        mir.insns[99].immediate != '-' ||
        mir.insns[99].src1 != mir.insns[98].dst ||
        !mir_match_matrix_product_long_type(mir.insns[99].type) ||
        mir.insns[100].immediate != 0 ||
        mir.insns[100].src1 != mir.insns[99].dst ||
        !mir_match_matrix_product_word_type(mir.insns[100].type) ||
        mir.insns[102].label != mir.insns[109].label ||
        !mir_machine_same_location(&mir.insns[24], &mir.insns[104]) ||
        !mir_machine_constant_equals(mir.insns[105].dst, 8) ||
        mir.insns[106].immediate != TOK_SHR ||
        mir.insns[106].src1 != mir.insns[104].dst ||
        mir.insns[106].src2 != mir.insns[105].dst ||
        !mir_match_matrix_product_long_type(mir.insns[106].type) ||
        mir.insns[107].immediate != 0 ||
        mir.insns[107].src1 != mir.insns[106].dst ||
        !mir_match_matrix_product_word_type(mir.insns[107].type))
        return mir_machine_reject(
            "matrix-product-add-schedule", "conversion");

    if (mir.insns[110].src1 != mir.insns[100].dst ||
        mir.insns[110].src2 != mir.insns[107].dst ||
        mir.insns[110].phi_pred1 != mir.insns[101].label ||
        mir.insns[110].phi_pred2 != mir.insns[108].label ||
        mir.insns[113].src1 != mir.insns[86].dst ||
        mir.insns[113].src2 != mir.insns[110].dst ||
        mir.insns[113].phi_pred1 != mir.insns[87].label ||
        mir.insns[113].phi_pred2 != mir.insns[111].label ||
        mir.insns[116].src1 != mir.insns[73].dst ||
        mir.insns[116].src2 != mir.insns[113].dst ||
        mir.insns[116].phi_pred1 != mir.insns[74].label ||
        mir.insns[116].phi_pred2 != mir.insns[114].label ||
        !mir_machine_unobservable_local_store(&mir.insns[117]) ||
        mir.insns[117].src1 != mir.insns[116].dst)
        return mir_machine_reject(
            "matrix-product-add-schedule", "conversion-phis");

    if (!mir_machine_same_location(&mir.insns[3], &mir.insns[118]) ||
        !mir_machine_constant_equals(mir.insns[119].dst, 2) ||
        mir.insns[120].immediate != '+' ||
        mir.insns[120].src1 != mir.insns[118].dst ||
        mir.insns[120].src2 != mir.insns[119].dst ||
        !mir_match_matrix_product_pointer_type(mir.insns[120].type) ||
        !mir_machine_same_location(&mir.insns[3], &mir.insns[121]) ||
        mir.insns[121].src1 != mir.insns[120].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[122]) ||
        mir.insns[122].src1 != mir.insns[118].dst ||
        !mir_machine_same_location(&mir.insns[122], &mir.insns[123]) ||
        !mir_machine_same_location(&mir.insns[122], &mir.insns[124]) ||
        mir.insns[125].src1 != mir.insns[124].dst ||
        mir.insns[125].memory_size != 2 ||
        (mir.insns[125].memory_flags & (1 | 8)) != 0 ||
        !mir_match_matrix_product_word_type(mir.insns[125].type) ||
        mir.insns[126].immediate != 0 ||
        mir.insns[126].src1 != mir.insns[125].dst ||
        !mir_match_matrix_product_long_type(mir.insns[126].type) ||
        !mir_machine_same_location(&mir.insns[117], &mir.insns[127]) ||
        mir.insns[128].immediate != 0 ||
        mir.insns[128].src1 != mir.insns[127].dst ||
        !mir_match_matrix_product_long_type(mir.insns[128].type) ||
        mir.insns[129].immediate != '+' ||
        mir.insns[129].src1 != mir.insns[126].dst ||
        mir.insns[129].src2 != mir.insns[128].dst ||
        !mir_match_matrix_product_long_type(mir.insns[129].type) ||
        !mir_match_matrix_product_call(
            &mir.insns[131], mir.insns[129].dst,
            &plan->clamp_function) ||
        mir.insns[132].src1 != mir.insns[123].dst ||
        mir.insns[132].src2 != mir.insns[131].dst ||
        mir.insns[132].memory_size != 2 ||
        (mir.insns[132].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject(
            "matrix-product-add-schedule", "output");

    if (!mir_machine_constant_equals(mir.insns[136].dst, 1) ||
        mir.insns[137].immediate != '+' ||
        mir.insns[137].src1 != mir.insns[15].dst ||
        mir.insns[137].src2 != mir.insns[136].dst ||
        !mir_match_matrix_product_count_type(mir.insns[137].type) ||
        !mir_machine_same_location(&mir.insns[8], &mir.insns[138]) ||
        mir.insns[138].src1 != mir.insns[137].dst ||
        mir.insns[139].label != mir.insns[9].label)
        return mir_machine_reject(
            "matrix-product-add-schedule", "outer-increment");
    return 1;
}

static int mir_match_matrix_product_schedule(
    struct MirMatrixProductSchedule *plan)
{
    return mir_match_matrix_product_schedule_kind(
               plan, MIR_MATRIX_PRODUCT_TRANSPOSED) ||
           mir_match_matrix_product_schedule_kind(
               plan, MIR_MATRIX_PRODUCT_OUTER) ||
           mir_match_matrix_product_add_schedule(plan);
}

static int mir_match_softmax_call(
    const struct MirInsn *call, int argument_count,
    struct Sym **function_out)
{
    struct Sym *function;

    if (call == NULL || call->opcode != MIR_CALL ||
        !mir_match_matrix_product_word_type(call->type) ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0)
        return 0;
    function = find_global(call->name);
    if (function == NULL || !function->is_defined ||
        function->storage != SC_FUNC || function->is_funcptr ||
        function->is_noreturn || !function->has_proto ||
        function->proto_variadic ||
        function->proto_nargs != argument_count ||
        (call->base_name[0] != 0 &&
         strcmp(call->base_name,
                asm_name_for(sym_asm_name(function)))))
        return 0;
    *function_out = function;
    return 1;
}

static int mir_match_softmax_schedule(
    struct MirSoftmaxSchedule *plan)
{
    static const int expected_opcodes[132] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_LOAD, MIR_ARG, MIR_NOP,
        MIR_ARG, MIR_ADDRESS, MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_STORE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST,
        MIR_STORE, MIR_LOAD, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_LOAD,
        MIR_NOP, MIR_LOAD, MIR_NOP, MIR_PHI, MIR_PHI, MIR_NOP,
        MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_NOP, MIR_LOAD, MIR_LOAD_INDIRECT, MIR_BINARY, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_LOAD, MIR_ADDRESS, MIR_LOAD, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT, MIR_NOP, MIR_LOAD,
        MIR_LOAD_INDIRECT, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_STORE, MIR_LOAD, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_LOAD, MIR_NOP, MIR_LOAD, MIR_NOP, MIR_NOP, MIR_PHI,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_UNARY, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD,
        MIR_LOAD_INDIRECT, MIR_STORE, MIR_LOAD, MIR_UNARY, MIR_CONST,
        MIR_BINARY, MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_ARG, MIR_CALL,
        MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL
    };
    const struct MirInsn *sum_phi = &mir.insns[27];
    const struct MirInsn *first_index_phi = &mir.insns[28];
    const struct MirInsn *second_index_phi = &mir.insns[98];
    int maximum_arguments[3];
    int clamp_argument;
    int dummy_type;
    int dummy_storage;
    int dummy_offset;
    long table_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 132 || mir_cfg_block_count() != 9 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID ||
        mir.aggregate_temp_bytes != 0)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "softmax-schedule", "opcodes");

    if (!mir_match_matrix_product_parameter(
            1, 2, 1, &plan->vector_stack_offset) ||
        !mir_match_matrix_product_parameter(
            2, 4, 0, &plan->length_stack_offset) ||
        !mir_machine_same_location(
            &mir.insns[1], &mir.insns[3]) ||
        !mir_attention_call_arguments(
            &mir.insns[10], 3, maximum_arguments) ||
        maximum_arguments[0] != mir.insns[3].dst ||
        maximum_arguments[1] != mir.insns[2].dst ||
        maximum_arguments[2] != mir.insns[7].dst ||
        !mir_scalar_memory_location(
            &mir.insns[7], &dummy_type, &dummy_storage,
            &dummy_offset) ||
        dummy_storage != SC_LOCAL || dummy_offset >= 0 ||
        !mir_match_matrix_product_word_type(dummy_type) ||
        type_ptr_depth(mir.insns[7].type) != 1 ||
        type_size(mir.insns[7].type) != 2 ||
        !mir_match_softmax_call(
            &mir.insns[10], 3, &plan->maximum_function) ||
        !mir_match_matrix_product_pointer_type(
            plan->maximum_function->proto_types[0]) ||
        !mir_match_matrix_product_count_type(
            plan->maximum_function->proto_types[1]) ||
        !mir_match_matrix_product_pointer_type(
            plan->maximum_function->proto_types[2]) ||
        !mir_machine_unobservable_local_store(&mir.insns[12]) ||
        mir.insns[12].src1 != mir.insns[10].dst ||
        !mir_machine_constant_equals(mir.insns[13].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[15]) ||
        mir.insns[15].src1 != mir.insns[13].dst ||
        mir_machine_same_location(
            &mir.insns[7], &mir.insns[12]) ||
        mir_machine_same_location(
            &mir.insns[7], &mir.insns[15]) ||
        mir_machine_same_location(
            &mir.insns[12], &mir.insns[15]))
        return mir_machine_reject(
            "softmax-schedule", "entry");

    if (!mir_machine_constant_equals(mir.insns[17].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[18]) ||
        mir.insns[18].src1 != mir.insns[17].dst ||
        !mir_machine_same_location(
            &mir.insns[1], &mir.insns[19]) ||
        !mir_machine_unobservable_local_store(&mir.insns[21]) ||
        mir.insns[21].src1 != mir.insns[19].dst ||
        sum_phi->src1 != mir.insns[13].dst ||
        sum_phi->src2 != mir.insns[71].dst ||
        sum_phi->phi_pred1 != mir.insns[0].label ||
        sum_phi->phi_pred2 != mir.insns[75].label ||
        sum_phi->object != mir.insns[15].object ||
        !mir_match_matrix_product_word_type(sum_phi->type) ||
        first_index_phi->src1 != mir.insns[17].dst ||
        first_index_phi->src2 != mir.insns[78].dst ||
        first_index_phi->phi_pred1 != mir.insns[0].label ||
        first_index_phi->phi_pred2 != mir.insns[75].label ||
        first_index_phi->object != mir.insns[18].object ||
        !mir_match_matrix_product_count_type(first_index_phi->type) ||
        mir.insns[31].opcode != MIR_UNARY ||
        mir.insns[31].immediate != 0 ||
        mir.insns[31].src1 != first_index_phi->dst ||
        !mir_match_matrix_product_word_type(mir.insns[31].type) ||
        mir.insns[32].opcode != MIR_UNARY ||
        mir.insns[32].immediate != 0 ||
        mir.insns[32].src1 != mir.insns[2].dst ||
        !mir_match_matrix_product_word_type(mir.insns[32].type) ||
        mir.insns[33].immediate != '<' ||
        mir.insns[33].src1 != mir.insns[31].dst ||
        mir.insns[33].src2 != mir.insns[32].dst ||
        !mir_match_matrix_product_word_type(
            mir.insns[33].secondary_offset) ||
        mir.insns[34].src1 != mir.insns[33].dst ||
        mir.insns[34].label != mir.insns[85].label)
        return mir_machine_reject(
            "softmax-schedule", "first-loop");

    if (!mir_machine_same_location(
            &mir.insns[21], &mir.insns[36]) ||
        mir.insns[37].src1 != mir.insns[36].dst ||
        mir.insns[37].memory_size != 2 ||
        (mir.insns[37].memory_flags & (1 | 8)) != 0 ||
        !mir_match_matrix_product_word_type(mir.insns[37].type) ||
        mir.insns[38].immediate != '-' ||
        mir.insns[38].src1 != mir.insns[10].dst ||
        mir.insns[38].src2 != mir.insns[37].dst ||
        !mir_match_matrix_product_word_type(mir.insns[38].type) ||
        !mir_machine_unobservable_local_store(&mir.insns[40]) ||
        mir.insns[40].src1 != mir.insns[38].dst ||
        !mir_machine_constant_equals(mir.insns[42].dst, 0) ||
        mir.insns[43].immediate != '<' ||
        mir.insns[43].src1 != mir.insns[38].dst ||
        mir.insns[43].src2 != mir.insns[42].dst ||
        mir.insns[44].src1 != mir.insns[43].dst ||
        mir.insns[44].label != mir.insns[48].label ||
        !mir_machine_constant_equals(mir.insns[45].dst, 0) ||
        !mir_machine_same_location(
            &mir.insns[40], &mir.insns[47]) ||
        mir.insns[47].src1 != mir.insns[45].dst ||
        !mir_machine_same_location(
            &mir.insns[40], &mir.insns[49]) ||
        !mir_machine_constant_equals(mir.insns[50].dst, 3) ||
        mir.insns[51].immediate != TOK_SHR ||
        mir.insns[51].src1 != mir.insns[49].dst ||
        mir.insns[51].src2 != mir.insns[50].dst ||
        !mir_match_matrix_product_word_type(mir.insns[51].type) ||
        !mir_machine_unobservable_local_store(&mir.insns[53]) ||
        mir.insns[53].src1 != mir.insns[51].dst ||
        !mir_machine_constant_equals(mir.insns[55].dst, 255) ||
        mir.insns[56].immediate != '>' ||
        mir.insns[56].src1 != mir.insns[51].dst ||
        mir.insns[56].src2 != mir.insns[55].dst ||
        mir.insns[57].src1 != mir.insns[56].dst ||
        mir.insns[57].label != mir.insns[61].label ||
        !mir_machine_constant_equals(mir.insns[58].dst, 255) ||
        !mir_machine_same_location(
            &mir.insns[53], &mir.insns[60]) ||
        mir.insns[60].src1 != mir.insns[58].dst)
        return mir_machine_reject(
            "softmax-schedule", "exponential-index");

    if (!mir_machine_same_location(
            &mir.insns[21], &mir.insns[62]) ||
        !mir_machine_global_address_offset(
            mir.insns[63].dst, &plan->table,
            &table_offset, 0) ||
        table_offset < -32768 || table_offset > 32767 ||
        plan->table == NULL || plan->table->storage == SC_FUNC ||
        plan->table->is_volatile ||
        !mir_machine_same_location(
            &mir.insns[53], &mir.insns[64]) ||
        mir.insns[65].src1 != mir.insns[63].dst ||
        mir.insns[65].src2 != mir.insns[64].dst ||
        mir.insns[65].immediate != 2 ||
        mir.insns[65].memory_size != 2 ||
        mir.insns[66].src1 != mir.insns[65].dst ||
        mir.insns[66].memory_size != 2 ||
        (mir.insns[66].memory_flags & (1 | 8)) != 0 ||
        !mir_match_matrix_product_word_type(mir.insns[66].type) ||
        mir.insns[67].src1 != mir.insns[62].dst ||
        mir.insns[67].src2 != mir.insns[66].dst ||
        mir.insns[67].memory_size != 2 ||
        (mir.insns[67].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_same_location(
            &mir.insns[21], &mir.insns[69]) ||
        mir.insns[70].src1 != mir.insns[69].dst ||
        mir.insns[70].memory_size != 2 ||
        (mir.insns[70].memory_flags & (1 | 8)) != 0 ||
        mir.insns[71].immediate != '+' ||
        mir.insns[71].src1 != sum_phi->dst ||
        mir.insns[71].src2 != mir.insns[70].dst ||
        !mir_match_matrix_product_word_type(mir.insns[71].type) ||
        !mir_machine_same_location(
            &mir.insns[15], &mir.insns[73]) ||
        mir.insns[73].src1 != mir.insns[71].dst)
        return mir_machine_reject(
            "softmax-schedule", "table-and-sum");
    plan->table_offset = (int)table_offset;

    if (!mir_machine_constant_equals(mir.insns[77].dst, 1) ||
        mir.insns[78].immediate != '+' ||
        mir.insns[78].src1 != first_index_phi->dst ||
        mir.insns[78].src2 != mir.insns[77].dst ||
        !mir_match_matrix_product_count_type(mir.insns[78].type) ||
        !mir_machine_same_location(
            &mir.insns[18], &mir.insns[79]) ||
        mir.insns[79].src1 != mir.insns[78].dst ||
        !mir_machine_same_location(
            &mir.insns[21], &mir.insns[80]) ||
        !mir_machine_constant_equals(mir.insns[81].dst, 2) ||
        mir.insns[82].immediate != '+' ||
        mir.insns[82].src1 != mir.insns[80].dst ||
        mir.insns[82].src2 != mir.insns[81].dst ||
        !mir_match_matrix_product_pointer_type(mir.insns[82].type) ||
        !mir_machine_same_location(
            &mir.insns[21], &mir.insns[83]) ||
        mir.insns[83].src1 != mir.insns[82].dst ||
        mir.insns[84].label != mir.insns[22].label)
        return mir_machine_reject(
            "softmax-schedule", "first-increment");

    if (!mir_machine_constant_equals(mir.insns[87].dst, 0) ||
        !mir_machine_same_location(
            &mir.insns[18], &mir.insns[88]) ||
        mir.insns[88].src1 != mir.insns[87].dst ||
        !mir_machine_same_location(
            &mir.insns[1], &mir.insns[89]) ||
        !mir_machine_same_location(
            &mir.insns[21], &mir.insns[91]) ||
        mir.insns[91].src1 != mir.insns[89].dst ||
        second_index_phi->src1 != mir.insns[87].dst ||
        second_index_phi->src2 != mir.insns[124].dst ||
        second_index_phi->phi_pred1 != mir.insns[85].label ||
        second_index_phi->phi_pred2 != mir.insns[121].label ||
        second_index_phi->object != mir.insns[18].object ||
        !mir_match_matrix_product_count_type(second_index_phi->type) ||
        mir.insns[103].immediate != 0 ||
        mir.insns[103].src1 != second_index_phi->dst ||
        !mir_match_matrix_product_word_type(mir.insns[103].type) ||
        mir.insns[104].immediate != 0 ||
        mir.insns[104].src1 != mir.insns[2].dst ||
        !mir_match_matrix_product_word_type(mir.insns[104].type) ||
        mir.insns[105].immediate != '<' ||
        mir.insns[105].src1 != mir.insns[103].dst ||
        mir.insns[105].src2 != mir.insns[104].dst ||
        mir.insns[106].src1 != mir.insns[105].dst ||
        mir.insns[106].label != mir.insns[131].label)
        return mir_machine_reject(
            "softmax-schedule", "second-loop");

    if (!mir_machine_same_location(
            &mir.insns[21], &mir.insns[107]) ||
        !mir_machine_same_location(
            &mir.insns[21], &mir.insns[108]) ||
        mir.insns[109].src1 != mir.insns[108].dst ||
        mir.insns[109].memory_size != 2 ||
        (mir.insns[109].memory_flags & (1 | 8)) != 0 ||
        !mir_match_matrix_product_word_type(mir.insns[109].type) ||
        !mir_machine_unobservable_local_store(&mir.insns[110]) ||
        mir.insns[110].src1 != mir.insns[109].dst ||
        !mir_machine_same_location(
            &mir.insns[110], &mir.insns[111]) ||
        mir.insns[112].immediate != 0 ||
        mir.insns[112].src1 != mir.insns[111].dst ||
        !mir_match_matrix_product_long_type(mir.insns[112].type) ||
        !mir_machine_constant_equals(mir.insns[113].dst, 256) ||
        !mir_match_matrix_product_long_type(mir.insns[113].type) ||
        mir.insns[114].immediate != '*' ||
        mir.insns[114].src1 != mir.insns[112].dst ||
        mir.insns[114].src2 != mir.insns[113].dst ||
        !mir_match_matrix_product_long_type(mir.insns[114].type) ||
        mir.insns[116].immediate != 0 ||
        mir.insns[116].src1 != sum_phi->dst ||
        !mir_match_matrix_product_long_type(mir.insns[116].type) ||
        mir.insns[117].immediate != '/' ||
        mir.insns[117].src1 != mir.insns[114].dst ||
        mir.insns[117].src2 != mir.insns[116].dst ||
        !mir_match_matrix_product_long_type(mir.insns[117].type) ||
        !mir_attention_call_arguments(
            &mir.insns[119], 1, &clamp_argument) ||
        clamp_argument != mir.insns[117].dst ||
        !mir_match_softmax_call(
            &mir.insns[119], 1, &plan->clamp_function) ||
        !mir_match_matrix_product_long_type(
            plan->clamp_function->proto_types[0]) ||
        mir.insns[120].src1 != mir.insns[107].dst ||
        mir.insns[120].src2 != mir.insns[119].dst ||
        mir.insns[120].memory_size != 2 ||
        (mir.insns[120].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject(
            "softmax-schedule", "normalization");

    if (!mir_machine_constant_equals(mir.insns[123].dst, 1) ||
        mir.insns[124].immediate != '+' ||
        mir.insns[124].src1 != second_index_phi->dst ||
        mir.insns[124].src2 != mir.insns[123].dst ||
        !mir_match_matrix_product_count_type(mir.insns[124].type) ||
        !mir_machine_same_location(
            &mir.insns[18], &mir.insns[125]) ||
        mir.insns[125].src1 != mir.insns[124].dst ||
        !mir_machine_same_location(
            &mir.insns[21], &mir.insns[126]) ||
        !mir_machine_constant_equals(mir.insns[127].dst, 2) ||
        mir.insns[128].immediate != '+' ||
        mir.insns[128].src1 != mir.insns[126].dst ||
        mir.insns[128].src2 != mir.insns[127].dst ||
        !mir_match_matrix_product_pointer_type(mir.insns[128].type) ||
        !mir_machine_same_location(
            &mir.insns[21], &mir.insns[129]) ||
        mir.insns[129].src1 != mir.insns[128].dst ||
        mir.insns[130].label != mir.insns[92].label)
        return mir_machine_reject(
            "softmax-schedule", "second-increment");
    return 1;
}

static char mir_backward_opcode_code(int opcode)
{
    switch (opcode) {
    case MIR_LABEL: return 'L';
    case MIR_ADDRESS: return 'A';
    case MIR_NOP: return 'N';
    case MIR_ARG: return 'G';
    case MIR_CONST: return 'C';
    case MIR_BINARY: return 'B';
    case MIR_CALL: return 'K';
    case MIR_STORE: return 'S';
    case MIR_PHI: return 'P';
    case MIR_BRANCH_FALSE: return 'F';
    case MIR_INDEX_ADDRESS: return 'I';
    case MIR_LOAD_INDIRECT: return 'D';
    case MIR_STORE_INDIRECT: return 'T';
    case MIR_LOAD: return 'R';
    case MIR_UNARY: return 'U';
    case MIR_JUMP: return 'J';
    default: return 0;
    }
}

static int mir_backward_call_arguments(
    const struct MirInsn *call, int count, int arguments[5])
{
    return mir_attention_call_arguments(call, count, arguments);
}

static int mir_backward_pointer_abi(int type)
{
    return type_ptr_depth(type) == 1 &&
           type_size(type) == 2 && !type_is_float(type);
}

static int mir_backward_word_abi(int type)
{
    return type_ptr_depth(type) == 0 &&
           type_size(type) == 2 && !type_is_float(type);
}

static int mir_backward_count_abi(int type)
{
    return type_ptr_depth(type) == 0 &&
           type_size(type) == 1 && !type_is_float(type) &&
           (type & TYPE_UNSIGNED) != 0;
}

static int mir_backward_long_abi(int type)
{
    return type_ptr_depth(type) == 0 &&
           type_size(type) == 4 && !type_is_float(type) &&
           (type & TYPE_UNSIGNED) == 0;
}

static int mir_match_backward_function_abi(
    int kind, const struct Sym *function)
{
    const int *types = function->proto_types;

    switch (kind) {
    case MIR_BACKWARD_CLEAR_FUNCTION:
        return mir_backward_pointer_abi(types[0]) &&
               mir_backward_word_abi(types[1]) &&
               mir_backward_word_abi(types[2]);
    case MIR_BACKWARD_COPY_FUNCTION:
        return mir_backward_pointer_abi(types[0]) &&
               mir_backward_pointer_abi(types[1]) &&
               mir_backward_word_abi(types[2]);
    case MIR_BACKWARD_SOFTMAX_FUNCTION:
        return mir_backward_pointer_abi(types[0]) &&
               mir_backward_count_abi(types[1]);
    case MIR_BACKWARD_CLAMP_FUNCTION:
    case MIR_BACKWARD_Q16_FUNCTION:
        return mir_backward_long_abi(types[0]);
    case MIR_BACKWARD_OUTER_PRODUCT_FUNCTION:
    case MIR_BACKWARD_MATRIX_MULTIPLY_FUNCTION:
    case MIR_BACKWARD_TRANSPOSE_FUNCTION:
    case MIR_BACKWARD_MATRIX_ADD_FUNCTION:
        return mir_backward_pointer_abi(types[0]) &&
               mir_backward_pointer_abi(types[1]) &&
               mir_backward_pointer_abi(types[2]) &&
               mir_backward_count_abi(types[3]) &&
               mir_backward_count_abi(types[4]);
    case MIR_BACKWARD_DOT_PRODUCT_FUNCTION:
        return mir_backward_pointer_abi(types[0]) &&
               mir_backward_pointer_abi(types[1]) &&
               mir_backward_count_abi(types[2]);
    case MIR_BACKWARD_SCALED_ADD_FUNCTION:
        return mir_backward_word_abi(types[0]) &&
               mir_backward_pointer_abi(types[1]) &&
               mir_backward_pointer_abi(types[2]) &&
               mir_backward_count_abi(types[3]);
    case MIR_BACKWARD_SHIFT_FUNCTION:
        return mir_backward_word_abi(types[0]) &&
               mir_backward_word_abi(types[1]);
    default:
        return 0;
    }
}

static int mir_match_backward_pass_schedule(
    struct MirBackwardPassSchedule *plan)
{
    static const char expected_opcodes[] =
        "LANGCGNNCCNBNGKCNSLPNCBFANGANCBINGCCNBNGKAGNCGKAANIDIAANIDIDCBTCNSLNNRCB"
        "FARIARIDUCBGKTLRCBSJLAGANCBIGAGNCGNCGKAGAGANCBIGNCGNCGKNLNCBSJLANGCGNNCC"
        "NBNGKCNSLPNNCBFCNSLNNNRCBFANCBRBIANNNNCRCBBIGANCBIGNCGKTANNNNCNCBBRBIDGA"
        "NCBIGARCBIGNCGKNLRCBSJLLNCBSJLCNSLPNNNCBFANNNNCNCBBIGANCBIGNCGKNSCNSLNNN"
        "NRCBFANCBRBIDSRUNUBGKNSANNNNCNCBBRBIDSRUNUBGKNSANCBRBINGCGKTNLRCBSJLNLNC"
        "BSJLCNSLPNNNNNCBFANNCIGANCBIGANCBIGNCGNCGKLNCBSJLCNSLNNPNNNCBFCNSLNNNNNR"
        "CBFARIARCBNBIDTLRCBSJLACIGAGANCBIGNCGNCGKNLNCBSJLANGANGNNCCNBNGKCNSLPNNN"
        "NNCBFNCBNSAGANIGANIGNCGNCGKAGANIGANIGNCGNCGKAGANIGANIGNCGNCGKAGANIGANIGN"
        "CGNCGKAGANIGANIGNCGNCGKAGANIGANIGNCGNCGKNLNCBSJLCNSLPNNNNPNCBFNCBNSANIDN"
        "SCNSLNNNNNNNRCBFANRBIDSANCBRBISRRDURUBGKTANRBIDSANCBRBISRRDURUBGKTNLRCBS"
        "JLNLNCBSJL";
    static const long expected_constants[101] = {
        0, 128, 2, 0, 8, 10, 10, 2, 10, 256, 0, 10,
        128, 1, 16, 16, 10, 16, 16, 10, 1, 0, 128, 2,
        0, 8, 0, 8, 8, 256, 16, 16, 16, 384, 8, 16,
        16, 16, 1, 1, 0, 8, 384, 8, 8, 8, 0, 8,
        8, 384, 8, 8, 2, 1, 1, 0, 8, 128, 8, 16,
        8, 16, 1, 0, 8, 0, 8, 8, 1, 0, 16, 8,
        16, 1, 128, 2, 0, 8, 16, 16, 16, 16, 16, 16,
        16, 16, 16, 16, 16, 16, 16, 1, 0, 8, 16, 0,
        16, 16, 16, 1, 1
    };
    static const unsigned char expected_constant_types[101] = {
        2, 2, 2, 2, 2, 2, 2, 2, 33, 2, 2, 2, 4, 2, 2, 33, 33, 2, 33, 33,
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 33, 2, 2, 2, 2, 33, 2, 2,
        2, 2, 2, 2, 2, 33, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        33, 33, 2, 2, 2, 2, 2, 2, 2, 2, 2, 33, 33, 2, 2, 2, 2, 2, 2, 33,
        33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2
    };
    static const unsigned char expected_binary_ops[70] = {
        42, 60, 42, 42, 45, 60, 42, 43, 42, 42, 43, 42, 60, 60, 42, 43,
        42, 43, 42, 42, 43, 43, 42, 42, 43, 43, 60, 42, 43, 42, 60, 42,
        43, 45, 42, 43, 43, 42, 42, 43, 43, 43, 60, 42, 42, 43, 60, 60,
        42, 43, 43, 42, 43, 42, 60, 42, 43, 60, 42, 60, 43, 42, 43, 43,
        43, 42, 43, 43, 43, 43
    };
    static const unsigned char expected_binary_types[70] = {
        2, 2, 2, 2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 4, 2, 2, 2, 4, 2, 2,
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        2, 2, 2, 4, 2, 2, 2, 4, 2, 2
    };
    static const unsigned char expected_locations[61] = {
        0, 1, 2, 1, 1, 3, 1, 3, 1, 1, 4, 5, 1, 6, 1, 0, 7, 8, 9, 0,
        9, 0, 7, 9, 8, 8, 9, 8, 9, 8, 10, 11, 8, 9, 11, 12, 13, 0, 14, 10,
        13, 15, 16, 10, 17, 12, 13, 18, 16, 12, 19, 7, 13, 20, 16, 7, 21,
        13, 22, 13, 23
    };
    static const struct {
        int instruction;
        int function;
        int type;
        int count;
        int arguments[5];
    } expected_calls[24] = {
        {14, 0, 19, 3, {1, 4, 11, -1, -1}},
        {40, 1, 19, 3, {24, 31, 37, -1, -1}},
        {46, 2, 3, 2, {41, 44, -1, -1, -1}},
        {84, 3, 2, 1, {82, -1, -1, -1, -1}},
        {109, 4, 3, 5, {93, 99, 101, 104, 107}},
        {126, 5, 3, 5, {110, 112, 118, 121, 124}},
        {148, 0, 19, 3, {135, 138, 145, -1, -1}},
        {198, 6, 2, 3, {187, 193, 196, -1, -1}},
        {230, 7, 3, 4, {213, 219, 225, 228, -1}},
        {278, 6, 2, 3, {267, 273, 276, -1, -1}},
        {308, 3, 2, 1, {306, -1, -1, -1, -1}},
        {332, 8, 2, 1, {330, -1, -1, -1, -1}},
        {346, 9, 2, 2, {332, 344, -1, -1, -1}},
        {401, 10, 3, 5, {381, 387, 393, 396, 399}},
        {472, 10, 3, 5, {456, 458, 464, 467, 470}},
        {495, 1, 19, 3, {481, 484, 492, -1, -1}},
        {530, 11, 3, 5, {514, 518, 522, 525, 528}},
        {547, 4, 3, 5, {531, 535, 539, 542, 545}},
        {564, 11, 3, 5, {548, 552, 556, 559, 562}},
        {581, 4, 3, 5, {565, 569, 573, 576, 579}},
        {598, 11, 3, 5, {582, 586, 590, 593, 596}},
        {615, 4, 3, 5, {599, 603, 607, 610, 613}},
        {687, 3, 2, 1, {685, -1, -1, -1, -1}},
        {712, 3, 2, 1, {710, -1, -1, -1, -1}}
    };
    static const struct {
        int instruction;
        int source;
        int label;
    } expected_branches[12] = {
        {23, 22, 134}, {72, 71, 92}, {158, 157, 245},
        {169, 168, 238}, {256, 255, 363}, {292, 291, 355},
        {376, 375, 408}, {421, 420, 480}, {434, 433, 453},
        {508, 507, 623}, {637, 636, 729}, {663, 662, 721}
    };
    static const struct {
        int instruction;
        int label;
    } expected_jumps[12] = {
        {91, 66}, {133, 18}, {237, 162}, {244, 152},
        {354, 284}, {362, 249}, {407, 367}, {452, 425},
        {479, 412}, {622, 499}, {720, 652}, {728, 627}
    };
    static const struct {
        int instruction;
        int source1;
        int source2;
        int predecessor1;
        int predecessor2;
    } expected_phis[8] = {
        {19, 15, 131, 0, 128},
        {153, 149, 242, 134, 239},
        {250, 246, 360, 245, 357},
        {368, 364, 405, 363, 402},
        {415, 409, 477, 408, 474},
        {500, 496, 620, 480, 617},
        {628, 624, 726, 623, 723},
        {633, 511, 640, 623, 723}
    };
    int constant = 0;
    int binary = 0;
    int location = 0;
    int instruction;
    int call;
    int edge;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir.count != 730 ||
        mir_cfg_block_count() != 37 ||
        (mir.return_type & 15) != TYPE_VOID ||
        mir.aggregate_temp_bytes != 0)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        char code = mir_backward_opcode_code(insn->opcode);

        if (code == 0 || code != expected_opcodes[instruction])
            return mir_machine_reject(
                "backward-pass-schedule", "opcodes");
        if (insn->opcode == MIR_CONST) {
            if (constant >= 101 ||
                insn->type != expected_constant_types[constant] ||
                !mir_machine_constant_equals(
                    insn->dst, expected_constants[constant]))
                return mir_machine_reject(
                    "backward-pass-schedule", "constants");
            ++constant;
        } else if (insn->opcode == MIR_BINARY) {
            if (binary >= 70 ||
                insn->immediate != expected_binary_ops[binary] ||
                insn->type != expected_binary_types[binary] ||
                insn->secondary_offset !=
                    expected_binary_types[binary])
                return mir_machine_reject(
                    "backward-pass-schedule", "binary-operations");
            ++binary;
        } else if (insn->opcode == MIR_ADDRESS) {
            struct MirBackwardLocation *expected;
            struct Sym *root;
            long offset;
            int slot;

            if (location >= 61 ||
                !mir_machine_global_address_offset(
                    insn->dst, &root, &offset, 0) ||
                offset < -32768 || offset > 32767)
                return mir_machine_reject(
                    "backward-pass-schedule", "locations");
            slot = expected_locations[location++];
            expected = &plan->locations[slot];
            if (expected->root == NULL) {
                expected->root = root;
                expected->offset = (int)offset;
            } else if (expected->root != root ||
                       expected->offset != (int)offset) {
                return mir_machine_reject(
                    "backward-pass-schedule", "location-aliases");
            }
        } else if ((insn->opcode == MIR_INDEX_ADDRESS ||
                    insn->opcode == MIR_LOAD_INDIRECT ||
                    insn->opcode == MIR_STORE_INDIRECT) &&
                   (insn->memory_size != 2 ||
                    (insn->memory_flags & (1 | 8)) != 0)) {
            return mir_machine_reject(
                "backward-pass-schedule", "memory-width");
        } else if (insn->opcode == MIR_INDEX_ADDRESS &&
                   (insn->immediate != 2 ||
                    !mir_match_matrix_product_pointer_type(
                        insn->type))) {
            return mir_machine_reject(
                "backward-pass-schedule", "strides");
        }
    }
    if (constant != 101 || binary != 70 || location != 61)
        return mir_machine_reject(
            "backward-pass-schedule", "instruction-counts");

    for (call = 0; call < 24; ++call) {
        const struct MirInsn *insn =
            &mir.insns[expected_calls[call].instruction];
        struct Sym *function;
        int arguments[5] = {-1, -1, -1, -1, -1};
        int argument;

        if (insn->type != expected_calls[call].type ||
            (insn->memory_flags &
             (MIR_CALL_FLAG_VARIADIC |
              MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0 ||
            (function = find_global(insn->name)) == NULL ||
            function->storage != SC_FUNC ||
            function->is_funcptr || function->is_noreturn ||
            !function->has_proto || function->proto_variadic ||
            function->proto_nargs != expected_calls[call].count ||
            !mir_match_backward_function_abi(
                expected_calls[call].function, function) ||
            (insn->base_name[0] != 0 &&
             strcmp(insn->base_name,
                    asm_name_for(sym_asm_name(function)))) ||
            !mir_backward_call_arguments(
                insn, expected_calls[call].count, arguments))
            return mir_machine_reject(
                "backward-pass-schedule", "calls");
        if (plan->functions[expected_calls[call].function] == NULL)
            plan->functions[expected_calls[call].function] = function;
        else if (plan->functions[expected_calls[call].function] !=
                 function)
            return mir_machine_reject(
                "backward-pass-schedule", "call-relationships");
        for (argument = 0;
             argument < expected_calls[call].count;
             ++argument)
            if (mir_definition(arguments[argument]) !=
                &mir.insns[expected_calls[call].arguments[argument]])
                return mir_machine_reject(
                    "backward-pass-schedule", "call-arguments");
    }

    for (edge = 0; edge < 12; ++edge) {
        const struct MirInsn *branch =
            &mir.insns[expected_branches[edge].instruction];

        if (mir_definition(branch->src1) !=
                &mir.insns[expected_branches[edge].source] ||
            branch->label !=
                mir.insns[expected_branches[edge].label].label)
            return mir_machine_reject(
                "backward-pass-schedule", "branches");
    }
    for (edge = 0; edge < 12; ++edge)
        if (mir.insns[expected_jumps[edge].instruction].label !=
            mir.insns[expected_jumps[edge].label].label)
            return mir_machine_reject(
                "backward-pass-schedule", "jumps");
    for (edge = 0; edge < 8; ++edge) {
        const struct MirInsn *phi =
            &mir.insns[expected_phis[edge].instruction];

        if (mir_definition(phi->src1) !=
                &mir.insns[expected_phis[edge].source1] ||
            mir_definition(phi->src2) !=
                &mir.insns[expected_phis[edge].source2] ||
            phi->phi_pred1 !=
                mir.insns[expected_phis[edge].predecessor1].label ||
            phi->phi_pred2 !=
                mir.insns[expected_phis[edge].predecessor2].label ||
            !mir_match_matrix_product_word_type(phi->type))
            return mir_machine_reject(
                "backward-pass-schedule", "phis");
    }
    return 1;
}

static void mir_emit_matrix_product_stack_word_de(
    FILE *out, int stack_offset)
{
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n",
            stack_offset + 6);
}

static void mir_emit_matrix_product_source(
    FILE *out, const struct MirMatrixProductSchedule *plan)
{
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tinc de\n\tinc de\n"
            "\tld (hl),d\n\tdec hl\n\tld (hl),e\n"
            "\tdec de\n\tdec de\n\tex de,hl\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld hl,0\n\tadd hl,sp\n"
            "\tld (hl),e\n\tinc hl\n\tld (hl),d\n",
            plan->source_stack_offset + 6);
}

static void mir_emit_matrix_product_wide_sum(FILE *out)
{
    fputs("\tld a,h\n\trlca\n\tsbc a,a\n"
          "\tld d,a\n\tld e,a\n\tpush de\n\tpush hl\n"
          "\tld l,c\n\tld h,b\n"
          "\tld a,h\n\trlca\n\tsbc a,a\n"
          "\tld d,a\n\tld e,a\n"
          "\tpop bc\n\tor a\n\tadd hl,bc\n"
          "\tex de,hl\n\tpop bc\n\tadc hl,bc\n\tex de,hl\n",
          out);
}

static void mir_emit_matrix_product_convert(
    FILE *out, const struct MirMatrixProductSchedule *plan)
{
    mir_emit_runtime_call(out, "__m1s");
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->convert_function);
    fputs("\tpop bc\n\tpop bc\n", out);
}

static void mir_emit_matrix_product_clamp(
    FILE *out, const struct MirMatrixProductSchedule *plan)
{
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->clamp_function);
    fputs("\tpop bc\n\tpop bc\n", out);
}

static void mir_emit_matrix_product_add_accumulator(FILE *out)
{
    fputs("\tpush de\n\tpush hl\n"
          "\tld hl,8\n\tadd hl,sp\n"
          "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
          "\tpop de\n\tex de,hl\n\tadd hl,bc\n\tex de,hl\n"
          "\tdec hl\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n"
          "\tinc hl\n\tpop de\n"
          "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
          "\tex de,hl\n\tadc hl,bc\n\tex de,hl\n"
          "\tdec hl\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n",
          out);
}

static void mir_emit_matrix_product_q16_to_word(FILE *out)
{
    int negative = new_label();
    int convert_positive = new_label();
    int maximum = new_label();
    int minimum = new_label();
    int done = new_label();

    fprintf(out,
            "\tld a,d\n\tor a\n\tjp m,L%d\n\tjp nz,L%d\n"
            "\tld a,e\n\tcp 128\n\tjp nc,L%d\n"
            "\tcp 127\n\tjp c,L%d\n"
            "\tld a,h\n\tcp 255\n\tjp c,L%d\n"
            "\tld a,l\n\tor a\n\tjp nz,L%d\n"
            "L%d:\n\tld l,h\n\tld h,e\n\tjp L%d\n"
            "L%d:\n\tld a,d\n\tcp 255\n\tjp nz,L%d\n"
            "\tld a,e\n\tcp 128\n\tjp c,L%d\n"
            "\tld a,l\n\tld l,h\n\tld h,e\n\tor a\n"
            "\tjp z,L%d\n\tinc hl\n\tjp L%d\n"
            "L%d:\n\tld hl,32767\n\tjp L%d\n"
            "L%d:\n\tld hl,32768\n"
            "L%d:\n",
            negative, maximum, maximum, convert_positive,
            convert_positive, maximum, convert_positive, done,
            negative, minimum, minimum, done, done,
            maximum, done, minimum, done);
}

static void mir_emit_matrix_product_add_schedule(
    FILE *out, const struct MirMatrixProductSchedule *plan)
{
    int outer = new_label();
    int inner = new_label();
    int row_done = new_label();
    int done = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fputs("\tpush ix\n\tld hl,0\n\tpush hl\n\tpush hl\n", out);
    mir_emit_matrix_product_stack_word_de(
        out, plan->matrix_stack_offset);
    fputs("\tpush de\n\tpop ix\n", out);
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld b,(hl)\n"
            "L%d:\n\tld a,b\n\tor a\n\tjp z,L%d\n"
            "\txor a\n\tld hl,0\n\tadd hl,sp\n"
            "\tld (hl),a\n\tinc hl\n\tld (hl),a\n"
            "\tinc hl\n\tld (hl),a\n\tinc hl\n\tld (hl),a\n",
            plan->rows_stack_offset + 6, outer, done);
    mir_emit_matrix_product_stack_word_de(
        out, plan->source_stack_offset);
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld c,(hl)\n"
            "L%d:\n\tld a,c\n\tor a\n\tjp z,L%d\n"
            "\tpush bc\n"
            "\tld l,(ix+0)\n\tld h,(ix+1)\n"
            "\tinc ix\n\tinc ix\n"
            "\tld a,(de)\n\tld c,a\n\tinc de\n"
            "\tld a,(de)\n\tld b,a\n\tinc de\n\tpush de\n",
            plan->columns_stack_offset + 6, inner, row_done);
    mir_emit_runtime_call(out, "__m1s");
    mir_emit_matrix_product_add_accumulator(out);
    fprintf(out,
            "\tpop de\n\tpop bc\n\tdec c\n\tjp L%d\n"
            "L%d:\n\tpush bc\n"
            "\tld hl,2\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld l,c\n\tld h,b\n",
            inner, row_done);
    mir_emit_matrix_product_q16_to_word(out);
    fputs("\tpush hl\n", out);
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tinc de\n\tinc de\n"
            "\tld (hl),d\n\tdec hl\n\tld (hl),e\n"
            "\tdec de\n\tdec de\n\tpush de\n"
            "\tld a,(de)\n\tld l,a\n\tinc de\n"
            "\tld a,(de)\n\tld h,a\n\tdec de\n"
            "\tpush hl\n\tld hl,4\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n\tpop hl\n",
            plan->vector_stack_offset + 10);
    mir_emit_matrix_product_wide_sum(out);
    mir_emit_matrix_product_clamp(out, plan);
    fprintf(out,
            "\tpop de\n\tld a,l\n\tld (de),a\n\tinc de\n"
            "\tld a,h\n\tld (de),a\n"
            "\tpop hl\n\tpop bc\n\tdec b\n\tjp L%d\n"
            "L%d:\n\tpop hl\n\tpop hl\n\tpop ix\n\tret\n",
            outer, done);
}

static void mir_emit_matrix_product_schedule(
    FILE *out, const struct MirMatrixProductSchedule *plan)
{
    int outer;
    int inner;
    int row_done;
    int done;

    if (plan->kind == MIR_MATRIX_PRODUCT_ADD) {
        mir_emit_matrix_product_add_schedule(out, plan);
        return;
    }
    outer = new_label();
    inner = new_label();
    row_done = new_label();
    done = new_label();
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fputs("\tpush ix\n\tld hl,0\n\tpush hl\n\tpush hl\n", out);

    if (plan->kind == MIR_MATRIX_PRODUCT_TRANSPOSED) {
        fprintf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld c,(hl)\n\tld b,0\n\tsla c\n\trl b\n",
                plan->columns_stack_offset + 6);
        mir_emit_matrix_product_stack_word_de(
            out, plan->vector_stack_offset);
        fputs("\tex de,hl\n\tld e,0\n", out);
        mir_emit_runtime_call(out, "__msf");
    }

    mir_emit_matrix_product_stack_word_de(
        out, plan->matrix_stack_offset);
    fputs("\tpush de\n\tpop ix\n", out);
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld a,(hl)\n"
            "\tld hl,2\n\tadd hl,sp\n\tld (hl),a\n"
            "L%d:\n\tld b,(hl)\n\tld a,b\n\tor a\n\tjp z,L%d\n",
            plan->rows_stack_offset + 6, outer, done);

    mir_emit_matrix_product_source(out, plan);
    mir_emit_matrix_product_stack_word_de(
        out, plan->vector_stack_offset);
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld c,(hl)\n"
            "L%d:\n\tld a,c\n\tor a\n\tjp z,L%d\n",
            plan->columns_stack_offset + 6, inner, row_done);

    if (plan->kind == MIR_MATRIX_PRODUCT_TRANSPOSED) {
        fputs("\tpush bc\n\tpush de\n"
              "\tld hl,4\n\tadd hl,sp\n"
              "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
              "\tld l,(ix+0)\n\tld h,(ix+1)\n"
              "\tinc ix\n\tinc ix\n",
              out);
    } else {
        fputs("\tld a,(de)\n\tld l,a\n\tinc de\n"
              "\tld a,(de)\n\tld h,a\n\tinc de\n"
              "\tpush bc\n\tpush de\n\tpush hl\n"
              "\tld hl,6\n\tadd hl,sp\n"
              "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
              "\tpop hl\n",
              out);
    }
    mir_emit_matrix_product_convert(out, plan);
    fputs("\tpop de\n\tpop bc\n", out);

    if (plan->kind == MIR_MATRIX_PRODUCT_TRANSPOSED) {
        fputs("\tpush bc\n\tpush de\n"
              "\tld a,(de)\n\tld c,a\n\tinc de\n"
              "\tld a,(de)\n\tld b,a\n\tdec de\n",
              out);
        mir_emit_matrix_product_wide_sum(out);
        mir_emit_matrix_product_clamp(out, plan);
        fputs("\tpop de\n\tld a,l\n\tld (de),a\n\tinc de\n"
              "\tld a,h\n\tld (de),a\n\tinc de\n\tpop bc\n",
              out);
    } else {
        fputs("\tpush bc\n\tpush de\n\tpush ix\n\tpop de\n"
              "\tinc ix\n\tinc ix\n\tpush de\n"
              "\tld a,(de)\n\tld c,a\n\tinc de\n"
              "\tld a,(de)\n\tld b,a\n\tdec de\n",
              out);
        mir_emit_matrix_product_wide_sum(out);
        mir_emit_matrix_product_clamp(out, plan);
        fputs("\tpop de\n\tld a,l\n\tld (de),a\n\tinc de\n"
              "\tld a,h\n\tld (de),a\n"
              "\tpop de\n\tpop bc\n",
              out);
    }

    fprintf(out,
            "\tdec c\n\tjp L%d\n"
            "L%d:\n\tdec b\n\tld hl,2\n\tadd hl,sp\n"
            "\tld (hl),b\n\tjp nz,L%d\n"
            "L%d:\n\tpop hl\n\tpop hl\n\tpop ix\n\tret\n",
            inner, row_done, outer, done);
}

static void mir_emit_softmax_schedule(
    FILE *out, const struct MirSoftmaxSchedule *plan)
{
    int exponential_loop = new_label();
    int exponential_ready = new_label();
    int nonnegative = new_label();
    int index_ready = new_label();
    int normalization_loop = new_label();
    int done = new_label();

    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-6\n\tadd hl,sp\n\tld sp,hl\n",
          out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    fputs("\tpush ix\n\tpop hl\n\tdec hl\n\tdec hl\n"
          "\tpush hl\n", out);
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,0\n\tpush hl\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n",
            plan->length_stack_offset + 2,
            plan->vector_stack_offset + 2,
            plan->vector_stack_offset + 3);
    mir_machine_emit_symbol_call(out, plan->maximum_function);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tld (ix-4),l\n\tld (ix-3),h\n"
          "\txor a\n\tld (ix-6),a\n\tld (ix-5),a\n",
          out);

    fprintf(out,
            "\tld a,(ix%+d)\n\tor a\n\tjp z,L%d\n"
            "\tld b,a\n\tld e,(ix%+d)\n\tld d,(ix%+d)\n"
            "L%d:\n\tpush bc\n\tpush de\n"
            "\tld a,(de)\n\tld c,a\n\tinc de\n"
            "\tld a,(de)\n\tld b,a\n\tld d,b\n\tld e,c\n"
            "\tld l,(ix-4)\n\tld h,(ix-3)\n"
            "\tor a\n\tsbc hl,de\n\tbit 7,h\n"
            "\tjp z,L%d\n\tld hl,0\n"
            "L%d:\n\tsra h\n\trr l\n\tsra h\n\trr l\n"
            "\tsra h\n\trr l\n\tld a,h\n\tor a\n"
            "\tjp z,L%d\n\tld hl,255\n"
            "L%d:\n\tadd hl,hl\n",
            plan->length_stack_offset + 2, exponential_ready,
            plan->vector_stack_offset + 2,
            plan->vector_stack_offset + 3,
            exponential_loop, nonnegative, nonnegative,
            index_ready, index_ready);
    mir_machine_emit_global_address_de(
        out, plan->table, plan->table_offset);
    fputs("\tadd hl,de\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tpop bc\n\tld a,e\n\tld (bc),a\n\tinc bc\n"
          "\tld a,d\n\tld (bc),a\n\tinc bc\n"
          "\tld l,(ix-6)\n\tld h,(ix-5)\n\tadd hl,de\n"
          "\tld (ix-6),l\n\tld (ix-5),h\n"
          "\tld d,b\n\tld e,c\n\tpop bc\n\tdjnz ",
          out);
    fprintf(out, "L%d\n", exponential_loop);

    fprintf(out,
            "L%d:\n\tld a,(ix%+d)\n\tor a\n\tjp z,L%d\n"
            "\tld b,a\n\tld e,(ix%+d)\n\tld d,(ix%+d)\n"
            "L%d:\n\tpush bc\n\tpush de\n"
            "\tld a,(de)\n\tld l,a\n\tinc de\n"
            "\tld a,(de)\n\tld h,a\n\tld a,h\n\trlca\n"
            "\tsbc a,a\n\tld d,a\n\tld e,a\n"
            "\tld d,e\n\tld e,h\n\tld h,l\n\tld l,0\n"
            "\tpush de\n\tpush hl\n"
            "\tld l,(ix-6)\n\tld h,(ix-5)\n"
            "\tld a,h\n\trlca\n\tsbc a,a\n"
            "\tld d,a\n\tld e,a\n",
            exponential_ready, plan->length_stack_offset + 2, done,
            plan->vector_stack_offset + 2,
            plan->vector_stack_offset + 3,
            normalization_loop);
    mir_emit_runtime_call(out, "__lds");
    fputs("\tpop bc\n\tpop bc\n\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->clamp_function);
    fputs("\tpop bc\n\tpop bc\n\tpop de\n"
          "\tld a,l\n\tld (de),a\n\tinc de\n"
          "\tld a,h\n\tld (de),a\n\tinc de\n"
          "\tpop bc\n\tdjnz ", out);
    fprintf(out,
            "L%d\nL%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            normalization_loop, done);
}

static void mir_emit_backward_address_hl(
    FILE *out, const struct MirBackwardPassSchedule *plan,
    int location, int byte_offset)
{
    const struct MirBackwardLocation *global =
        &plan->locations[location];

    mir_machine_emit_global_address_de(
        out, global->root, global->offset + byte_offset);
    fputs("\tex de,hl\n", out);
}

static void mir_emit_backward_push_address(
    FILE *out, const struct MirBackwardPassSchedule *plan,
    int location, int byte_offset)
{
    mir_emit_backward_address_hl(
        out, plan, location, byte_offset);
    fputs("\tpush hl\n", out);
}

static void mir_emit_backward_cleanup(FILE *out, int words)
{
    while (words-- > 0)
        fputs("\tpop bc\n", out);
}

static void mir_emit_backward_byte_index_address(
    FILE *out, const struct MirBackwardPassSchedule *plan,
    int location, int byte_offset, int outer_frame_offset,
    int outer_scale, int inner_frame_offset, int inner_scale)
{
    if (outer_frame_offset != 0) {
        fprintf(out,
                "\tld l,(ix%+d)\n\tld h,0\n",
                outer_frame_offset);
        mir_emit_mul_hl_const(out, (unsigned long)outer_scale);
    } else {
        fputs("\tld hl,0\n", out);
    }
    if (inner_frame_offset != 0) {
        fprintf(out,
                "\tld c,(ix%+d)\n\tld b,0\n",
                inner_frame_offset);
        if (inner_scale == 2)
            fputs("\tsla c\n\trl b\n", out);
        else if (inner_scale != 1) {
            fputs("\tpush hl\n\tld l,c\n\tld h,b\n", out);
            mir_emit_mul_hl_const(
                out, (unsigned long)inner_scale);
            fputs("\tld c,l\n\tld b,h\n\tpop hl\n", out);
        }
        fputs("\tadd hl,bc\n", out);
    }
    mir_machine_emit_global_address_de(
        out, plan->locations[location].root,
        plan->locations[location].offset + byte_offset);
    fputs("\tadd hl,de\n", out);
}

static void mir_emit_backward_word_index_address(
    FILE *out, const struct MirBackwardPassSchedule *plan,
    int location, int byte_offset, int word_frame_offset,
    int word_scale, int inner_frame_offset)
{
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            word_frame_offset, word_frame_offset + 1);
    mir_emit_mul_hl_const(out, (unsigned long)word_scale);
    if (inner_frame_offset != 0)
        fprintf(out,
                "\tld c,(ix%+d)\n\tld b,0\n"
                "\tsla c\n\trl b\n\tadd hl,bc\n",
                inner_frame_offset);
    mir_machine_emit_global_address_de(
        out, plan->locations[location].root,
        plan->locations[location].offset + byte_offset);
    fputs("\tadd hl,de\n", out);
}

static void mir_emit_backward_load_word(FILE *out)
{
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n", out);
}

static void mir_emit_backward_sign_extend(FILE *out)
{
    fputs("\tld a,h\n\trlca\n\tsbc a,a\n"
          "\tld d,a\n\tld e,a\n", out);
}

static void mir_emit_backward_combine_long(
    FILE *out, int subtract)
{
    fputs("\tld c,l\n\tld b,h\n\tpop hl\n", out);
    if (subtract)
        fputs("\tor a\n\tsbc hl,bc\n", out);
    else
        fputs("\tadd hl,bc\n", out);
    fputs("\tex (sp),hl\n", out);
    if (subtract)
        fputs("\tsbc hl,de\n", out);
    else
        fputs("\tadc hl,de\n", out);
    fputs("\tex de,hl\n\tpop hl\n", out);
}

static void mir_emit_backward_long_call(
    FILE *out, struct Sym *function)
{
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, function);
    mir_emit_backward_cleanup(out, 2);
}

static void mir_emit_backward_clear(
    FILE *out, const struct MirBackwardPassSchedule *plan,
    int location, int bytes)
{
    fprintf(out, "\tld hl,%d\n\tpush hl\n"
                 "\tld hl,0\n\tpush hl\n", bytes);
    mir_emit_backward_push_address(out, plan, location, 0);
    mir_machine_emit_symbol_call(
        out, plan->functions[MIR_BACKWARD_CLEAR_FUNCTION]);
    mir_emit_backward_cleanup(out, 3);
}

static void mir_emit_backward_copy(
    FILE *out, const struct MirBackwardPassSchedule *plan,
    int destination, int source, int bytes)
{
    fprintf(out, "\tld hl,%d\n\tpush hl\n", bytes);
    mir_emit_backward_push_address(out, plan, source, 0);
    mir_emit_backward_push_address(out, plan, destination, 0);
    mir_machine_emit_symbol_call(
        out, plan->functions[MIR_BACKWARD_COPY_FUNCTION]);
    mir_emit_backward_cleanup(out, 3);
}

static void mir_emit_backward_clamped_add(
    FILE *out, const struct MirBackwardPassSchedule *plan,
    int destination, int destination_byte_offset,
    int destination_outer_frame, int destination_outer_scale,
    int source, int source_byte_offset,
    int source_outer_frame, int source_outer_scale,
    int inner_frame)
{
    mir_emit_backward_byte_index_address(
        out, plan, source, source_byte_offset,
        source_outer_frame, source_outer_scale,
        inner_frame, 2);
    mir_emit_backward_load_word(out);
    fputs("\tld (ix-4),l\n\tld (ix-3),h\n", out);

    mir_emit_backward_byte_index_address(
        out, plan, destination, destination_byte_offset,
        destination_outer_frame, destination_outer_scale,
        inner_frame, 2);
    fputs("\tpush hl\n", out);
    mir_emit_backward_load_word(out);
    mir_emit_backward_sign_extend(out);
    fputs("\tpush de\n\tpush hl\n"
          "\tld l,(ix-4)\n\tld h,(ix-3)\n", out);
    mir_emit_backward_sign_extend(out);
    mir_emit_backward_combine_long(out, 0);
    mir_emit_backward_long_call(
        out, plan->functions[MIR_BACKWARD_CLAMP_FUNCTION]);
    fputs("\tpop de\n\tld a,l\n\tld (de),a\n"
          "\tinc de\n\tld a,h\n\tld (de),a\n", out);
}

static void mir_emit_backward_pass_schedule(
    FILE *out, const struct MirBackwardPassSchedule *plan)
{
    int step1 = new_label();
    int step1_scale = new_label();
    int step1_scale_done = new_label();
    int step2_outer = new_label();
    int step2_inner = new_label();
    int step2_inner_done = new_label();
    int step2_row_done = new_label();
    int step3_outer = new_label();
    int step3_inner = new_label();
    int step3_inner_done = new_label();
    int step4_query = new_label();
    int step4_key = new_label();
    int step4_copy = new_label();
    int step4_copy_done = new_label();
    int step5 = new_label();
    int step6_outer = new_label();
    int step6_inner = new_label();
    int done = new_label();
    int shift;

    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-6\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    mir_emit_backward_clear(
        out, plan, MIR_BACKWARD_ATTENTION_OUTPUT_GRADIENTS, 256);
    fputs("\txor a\n\tld (ix-1),a\n", out);
    fprintf(out, "L%d:\n", step1);
    fputs("\tld a,(ix-1)\n\tcp 8\n", out);
    fprintf(out, "\tjp z,L%d\n", step2_outer);

    fputs("\tld hl,20\n\tpush hl\n", out);
    mir_emit_backward_byte_index_address(
        out, plan, MIR_BACKWARD_LOGITS, 0, -1, 20, 0, 0);
    fputs("\tpush hl\n", out);
    mir_emit_backward_push_address(
        out, plan, MIR_BACKWARD_LOGIT_GRADIENTS, 0);
    mir_machine_emit_symbol_call(
        out, plan->functions[MIR_BACKWARD_COPY_FUNCTION]);
    mir_emit_backward_cleanup(out, 3);

    fputs("\tld hl,10\n\tpush hl\n", out);
    mir_emit_backward_push_address(
        out, plan, MIR_BACKWARD_LOGIT_GRADIENTS, 0);
    mir_machine_emit_symbol_call(
        out, plan->functions[MIR_BACKWARD_SOFTMAX_FUNCTION]);
    mir_emit_backward_cleanup(out, 2);

    mir_emit_backward_byte_index_address(
        out, plan, MIR_BACKWARD_TARGETS, 0, -1, 2, 0, 0);
    mir_emit_backward_load_word(out);
    fputs("\tadd hl,hl\n", out);
    mir_machine_emit_global_address_de(
        out, plan->locations[MIR_BACKWARD_LOGIT_GRADIENTS].root,
        plan->locations[MIR_BACKWARD_LOGIT_GRADIENTS].offset);
    fputs("\tadd hl,de\n\tpush hl\n", out);
    mir_emit_backward_load_word(out);
    fputs("\tld de,256\n\tor a\n\tsbc hl,de\n"
          "\tpop de\n\tld a,l\n\tld (de),a\n"
          "\tinc de\n\tld a,h\n\tld (de),a\n"
          "\txor a\n\tld (ix-2),a\n", out);

    fprintf(out, "L%d:\n", step1_scale);
    fputs("\tld a,(ix-2)\n\tcp 10\n", out);
    fprintf(out, "\tjp z,L%d\n", step1_scale_done);
    mir_emit_backward_byte_index_address(
        out, plan, MIR_BACKWARD_LOGIT_GRADIENTS, 0,
        -2, 2, 0, 0);
    fputs("\tpush hl\n", out);
    mir_emit_backward_load_word(out);
    mir_emit_backward_sign_extend(out);
    for (shift = 0; shift < 7; ++shift)
        fputs("\tadd hl,hl\n\trl e\n\trl d\n", out);
    mir_emit_backward_long_call(
        out, plan->functions[MIR_BACKWARD_CLAMP_FUNCTION]);
    fputs("\tpop de\n\tld a,l\n\tld (de),a\n"
          "\tinc de\n\tld a,h\n\tld (de),a\n"
          "\tinc (ix-2)\n", out);
    fprintf(out, "\tjp L%d\nL%d:\n",
            step1_scale, step1_scale_done);

    fputs("\tld hl,10\n\tpush hl\n"
          "\tld hl,16\n\tpush hl\n", out);
    mir_emit_backward_push_address(
        out, plan, MIR_BACKWARD_LOGIT_GRADIENTS, 0);
    mir_emit_backward_byte_index_address(
        out, plan, MIR_BACKWARD_ATTENTION_OUTPUT, 0,
        -1, 32, 0, 0);
    fputs("\tpush hl\n", out);
    mir_emit_backward_push_address(
        out, plan, MIR_BACKWARD_OUTPUT_WEIGHT_GRADIENTS, 0);
    mir_machine_emit_symbol_call(
        out, plan->functions[MIR_BACKWARD_OUTER_PRODUCT_FUNCTION]);
    mir_emit_backward_cleanup(out, 5);

    fputs("\tld hl,10\n\tpush hl\n"
          "\tld hl,16\n\tpush hl\n", out);
    mir_emit_backward_byte_index_address(
        out, plan, MIR_BACKWARD_ATTENTION_OUTPUT_GRADIENTS, 0,
        -1, 32, 0, 0);
    fputs("\tpush hl\n", out);
    mir_emit_backward_push_address(
        out, plan, MIR_BACKWARD_LOGIT_GRADIENTS, 0);
    mir_emit_backward_push_address(
        out, plan, MIR_BACKWARD_OUTPUT_WEIGHTS, 0);
    mir_machine_emit_symbol_call(
        out, plan->functions[MIR_BACKWARD_MATRIX_MULTIPLY_FUNCTION]);
    mir_emit_backward_cleanup(out, 5);
    fputs("\tinc (ix-1)\n", out);
    fprintf(out, "\tjp L%d\n", step1);

    fprintf(out, "L%d:\n", step2_outer);
    mir_emit_backward_clear(
        out, plan, MIR_BACKWARD_VALUE_STATE_GRADIENTS, 256);
    fputs("\txor a\n\tld (ix-1),a\n", out);
    fprintf(out, "L%d:\n", step2_inner_done);
    fputs("\tld a,(ix-1)\n\tcp 8\n", out);
    fprintf(out, "\tjp z,L%d\n\txor a\n\tld (ix-2),a\n",
            step3_outer);
    fprintf(out, "L%d:\n", step2_inner);
    fputs("\tld a,(ix-2)\n\tcp 8\n", out);
    fprintf(out, "\tjp z,L%d\n", step2_row_done);

    mir_emit_backward_byte_index_address(
        out, plan, MIR_BACKWARD_ATTENTION_SCORE_GRADIENTS, 0,
        -1, 16, -2, 2);
    fputs("\tpush hl\n\tld hl,16\n\tpush hl\n", out);
    mir_emit_backward_byte_index_address(
        out, plan, MIR_BACKWARD_ATTENTION_OUTPUT_GRADIENTS, 0,
        -1, 32, 0, 0);
    fputs("\tpush hl\n", out);
    mir_emit_backward_byte_index_address(
        out, plan, MIR_BACKWARD_ATTENTION_WORKSPACE, 512,
        -2, 32, 0, 0);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(
        out, plan->functions[MIR_BACKWARD_DOT_PRODUCT_FUNCTION]);
    mir_emit_backward_cleanup(out, 3);
    fputs("\tpop de\n\tld a,l\n\tld (de),a\n"
          "\tinc de\n\tld a,h\n\tld (de),a\n", out);

    mir_emit_backward_byte_index_address(
        out, plan, MIR_BACKWARD_ATTENTION_WORKSPACE, 768,
        -1, 16, -2, 2);
    mir_emit_backward_load_word(out);
    fputs("\tld (ix-4),l\n\tld (ix-3),h\n"
          "\tld hl,16\n\tpush hl\n", out);
    mir_emit_backward_byte_index_address(
        out, plan, MIR_BACKWARD_VALUE_STATE_GRADIENTS, 0,
        -2, 32, 0, 0);
    fputs("\tpush hl\n", out);
    mir_emit_backward_byte_index_address(
        out, plan, MIR_BACKWARD_ATTENTION_OUTPUT_GRADIENTS, 0,
        -1, 32, 0, 0);
    fputs("\tpush hl\n\tld l,(ix-4)\n\tld h,(ix-3)\n"
          "\tpush hl\n", out);
    mir_machine_emit_symbol_call(
        out, plan->functions[MIR_BACKWARD_SCALED_ADD_FUNCTION]);
    mir_emit_backward_cleanup(out, 4);
    fputs("\tinc (ix-2)\n", out);
    fprintf(out, "\tjp L%d\nL%d:\n\tinc (ix-1)\n\tjp L%d\n",
            step2_inner, step2_row_done, step2_inner_done);

    fprintf(out, "L%d:\n", step3_outer);
    fputs("\txor a\n\tld (ix-1),a\n", out);
    {
        int step3_rows = new_label();

        fprintf(out, "L%d:\n", step3_rows);
        fputs("\tld a,(ix-1)\n\tcp 8\n", out);
        fprintf(out, "\tjp z,L%d\n", step4_query);
        fputs("\tld hl,8\n\tpush hl\n", out);
        mir_emit_backward_byte_index_address(
            out, plan, MIR_BACKWARD_ATTENTION_SCORE_GRADIENTS, 0,
            -1, 16, 0, 0);
        fputs("\tpush hl\n", out);
        mir_emit_backward_byte_index_address(
            out, plan, MIR_BACKWARD_ATTENTION_WORKSPACE, 768,
            -1, 16, 0, 0);
        fputs("\tpush hl\n", out);
        mir_machine_emit_symbol_call(
            out, plan->functions[MIR_BACKWARD_DOT_PRODUCT_FUNCTION]);
        mir_emit_backward_cleanup(out, 3);
        fputs("\tld (ix-6),l\n\tld (ix-5),h\n"
              "\txor a\n\tld (ix-2),a\n", out);
        fprintf(out, "L%d:\n", step3_inner);
        fputs("\tld a,(ix-2)\n\tcp 8\n", out);
        fprintf(out, "\tjp z,L%d\n", step3_inner_done);

        mir_emit_backward_byte_index_address(
            out, plan, MIR_BACKWARD_ATTENTION_SCORE_GRADIENTS, 0,
            -1, 16, -2, 2);
        mir_emit_backward_load_word(out);
        mir_emit_backward_sign_extend(out);
        fputs("\tpush de\n\tpush hl\n"
              "\tld l,(ix-6)\n\tld h,(ix-5)\n", out);
        mir_emit_backward_sign_extend(out);
        mir_emit_backward_combine_long(out, 1);
        mir_emit_backward_long_call(
            out, plan->functions[MIR_BACKWARD_CLAMP_FUNCTION]);
        fputs("\tld (ix-4),l\n\tld (ix-3),h\n", out);

        mir_emit_backward_byte_index_address(
            out, plan, MIR_BACKWARD_ATTENTION_WORKSPACE, 768,
            -1, 16, -2, 2);
        mir_emit_backward_load_word(out);
        fputs("\tld c,(ix-4)\n\tld b,(ix-3)\n", out);
        mir_emit_runtime_call(out, "__m1s");
        mir_emit_backward_long_call(
            out, plan->functions[MIR_BACKWARD_Q16_FUNCTION]);
        fputs("\tld (ix-4),l\n\tld (ix-3),h\n", out);

        mir_emit_backward_byte_index_address(
            out, plan, MIR_BACKWARD_ATTENTION_SCORE_GRADIENTS, 0,
            -1, 16, -2, 2);
        fputs("\tpush hl\n\tld hl,2\n\tpush hl\n"
              "\tld l,(ix-4)\n\tld h,(ix-3)\n\tpush hl\n", out);
        mir_machine_emit_symbol_call(
            out, plan->functions[MIR_BACKWARD_SHIFT_FUNCTION]);
        mir_emit_backward_cleanup(out, 2);
        fputs("\tpop de\n\tld a,l\n\tld (de),a\n"
              "\tinc de\n\tld a,h\n\tld (de),a\n"
              "\tinc (ix-2)\n", out);
        fprintf(out, "\tjp L%d\nL%d:\n\tinc (ix-1)\n\tjp L%d\n",
                step3_inner, step3_inner_done, step3_rows);
    }

    fprintf(out, "L%d:\n", step4_query);
    fputs("\txor a\n\tld (ix-1),a\n", out);
    {
        int query_loop = new_label();

        fprintf(out, "L%d:\n", query_loop);
        fputs("\tld a,(ix-1)\n\tcp 8\n", out);
        fprintf(out, "\tjp z,L%d\n", step4_key);
        fputs("\tld hl,16\n\tpush hl\n"
              "\tld hl,8\n\tpush hl\n", out);
        mir_emit_backward_byte_index_address(
            out, plan, MIR_BACKWARD_QUERY_STATE_GRADIENTS, 0,
            -1, 32, 0, 0);
        fputs("\tpush hl\n", out);
        mir_emit_backward_byte_index_address(
            out, plan, MIR_BACKWARD_ATTENTION_SCORE_GRADIENTS, 0,
            -1, 16, 0, 0);
        fputs("\tpush hl\n", out);
        mir_emit_backward_push_address(
            out, plan, MIR_BACKWARD_ATTENTION_WORKSPACE, 256);
        mir_machine_emit_symbol_call(
            out, plan->functions[MIR_BACKWARD_TRANSPOSE_FUNCTION]);
        mir_emit_backward_cleanup(out, 5);
        fputs("\tinc (ix-1)\n", out);
        fprintf(out, "\tjp L%d\n", query_loop);
    }

    fprintf(out, "L%d:\n", step4_key);
    fputs("\txor a\n\tld (ix-2),a\n", out);
    {
        int key_loop = new_label();

        fprintf(out, "L%d:\n", key_loop);
        fputs("\tld a,(ix-2)\n\tcp 8\n", out);
        fprintf(out, "\tjp z,L%d\n\txor a\n\tld (ix-1),a\n",
                step5);
        fprintf(out, "L%d:\n", step4_copy);
        fputs("\tld a,(ix-1)\n\tcp 8\n", out);
        fprintf(out, "\tjp z,L%d\n", step4_copy_done);
        mir_emit_backward_byte_index_address(
            out, plan, MIR_BACKWARD_ATTENTION_SCORE_GRADIENTS, 0,
            -1, 16, -2, 2);
        mir_emit_backward_load_word(out);
        fputs("\tpush hl\n", out);
        mir_emit_backward_byte_index_address(
            out, plan, MIR_BACKWARD_GRADIENT_COLUMN, 0,
            -1, 2, 0, 0);
        fputs("\tpop de\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n"
              "\tinc (ix-1)\n", out);
        fprintf(out, "\tjp L%d\nL%d:\n",
                step4_copy, step4_copy_done);
        fputs("\tld hl,16\n\tpush hl\n"
              "\tld hl,8\n\tpush hl\n", out);
        mir_emit_backward_byte_index_address(
            out, plan, MIR_BACKWARD_KEY_STATE_GRADIENTS, 0,
            -2, 32, 0, 0);
        fputs("\tpush hl\n", out);
        mir_emit_backward_push_address(
            out, plan, MIR_BACKWARD_GRADIENT_COLUMN, 0);
        mir_emit_backward_push_address(
            out, plan, MIR_BACKWARD_ATTENTION_WORKSPACE, 0);
        mir_machine_emit_symbol_call(
            out, plan->functions[MIR_BACKWARD_TRANSPOSE_FUNCTION]);
        mir_emit_backward_cleanup(out, 5);
        fputs("\tinc (ix-2)\n", out);
        fprintf(out, "\tjp L%d\n", key_loop);
    }

    fprintf(out, "L%d:\n", step5);
    mir_emit_backward_copy(
        out, plan, MIR_BACKWARD_EMBEDDING_GRADIENTS,
        MIR_BACKWARD_ATTENTION_OUTPUT_GRADIENTS, 256);
    fputs("\txor a\n\tld (ix-1),a\n", out);
    {
        int projection_loop = new_label();

        fprintf(out, "L%d:\n", projection_loop);
        fputs("\tld a,(ix-1)\n\tcp 8\n", out);
        fprintf(out, "\tjp z,L%d\n", step6_outer);

#define MIR_BACKWARD_EMIT_MATRIX_CALL(function_slot, matrix_slot, input_slot, output_slot) \
        fputs("\tld hl,16\n\tpush hl\n" \
              "\tld hl,16\n\tpush hl\n", out); \
        mir_emit_backward_byte_index_address( \
            out, plan, output_slot, 0, -1, 32, 0, 0); \
        fputs("\tpush hl\n", out); \
        mir_emit_backward_byte_index_address( \
            out, plan, input_slot, 0, -1, 32, 0, 0); \
        fputs("\tpush hl\n", out); \
        mir_emit_backward_push_address(out, plan, matrix_slot, 0); \
        mir_machine_emit_symbol_call(out, plan->functions[function_slot]); \
        mir_emit_backward_cleanup(out, 5)

        MIR_BACKWARD_EMIT_MATRIX_CALL(
            MIR_BACKWARD_MATRIX_ADD_FUNCTION,
            MIR_BACKWARD_QUERY_WEIGHTS,
            MIR_BACKWARD_QUERY_STATE_GRADIENTS,
            MIR_BACKWARD_EMBEDDING_GRADIENTS);
        MIR_BACKWARD_EMIT_MATRIX_CALL(
            MIR_BACKWARD_OUTER_PRODUCT_FUNCTION,
            MIR_BACKWARD_QUERY_WEIGHT_GRADIENTS,
            MIR_BACKWARD_EMBEDDINGS,
            MIR_BACKWARD_QUERY_STATE_GRADIENTS);
        MIR_BACKWARD_EMIT_MATRIX_CALL(
            MIR_BACKWARD_MATRIX_ADD_FUNCTION,
            MIR_BACKWARD_KEY_WEIGHTS,
            MIR_BACKWARD_KEY_STATE_GRADIENTS,
            MIR_BACKWARD_EMBEDDING_GRADIENTS);
        MIR_BACKWARD_EMIT_MATRIX_CALL(
            MIR_BACKWARD_OUTER_PRODUCT_FUNCTION,
            MIR_BACKWARD_KEY_WEIGHT_GRADIENTS,
            MIR_BACKWARD_EMBEDDINGS,
            MIR_BACKWARD_KEY_STATE_GRADIENTS);
        MIR_BACKWARD_EMIT_MATRIX_CALL(
            MIR_BACKWARD_MATRIX_ADD_FUNCTION,
            MIR_BACKWARD_VALUE_WEIGHTS,
            MIR_BACKWARD_VALUE_STATE_GRADIENTS,
            MIR_BACKWARD_EMBEDDING_GRADIENTS);
        MIR_BACKWARD_EMIT_MATRIX_CALL(
            MIR_BACKWARD_OUTER_PRODUCT_FUNCTION,
            MIR_BACKWARD_VALUE_WEIGHT_GRADIENTS,
            MIR_BACKWARD_EMBEDDINGS,
            MIR_BACKWARD_VALUE_STATE_GRADIENTS);
#undef MIR_BACKWARD_EMIT_MATRIX_CALL

        fputs("\tinc (ix-1)\n", out);
        fprintf(out, "\tjp L%d\n", projection_loop);
    }

    fprintf(out, "L%d:\n", step6_outer);
    fputs("\txor a\n\tld (ix-1),a\n", out);
    {
        int embedding_outer = new_label();

        fprintf(out, "L%d:\n", embedding_outer);
        fputs("\tld a,(ix-1)\n\tcp 8\n", out);
        fprintf(out, "\tjp z,L%d\n", done);
        mir_emit_backward_byte_index_address(
            out, plan, MIR_BACKWARD_TOKENS, 0,
            -1, 2, 0, 0);
        mir_emit_backward_load_word(out);
        fputs("\tld (ix-6),l\n\tld (ix-5),h\n"
              "\txor a\n\tld (ix-2),a\n", out);
        fprintf(out, "L%d:\n", step6_inner);
        fputs("\tld a,(ix-2)\n\tcp 16\n", out);
        {
            int embedding_inner_done = new_label();

            fprintf(out, "\tjp z,L%d\n", embedding_inner_done);

            mir_emit_backward_byte_index_address(
                out, plan, MIR_BACKWARD_EMBEDDING_GRADIENTS, 0,
                -1, 32, -2, 2);
            mir_emit_backward_load_word(out);
            fputs("\tld (ix-4),l\n\tld (ix-3),h\n", out);
            mir_emit_backward_word_index_address(
                out, plan, MIR_BACKWARD_TOKEN_GRADIENTS, 0,
                -6, 32, -2);
            fputs("\tpush hl\n", out);
            mir_emit_backward_load_word(out);
            mir_emit_backward_sign_extend(out);
            fputs("\tpush de\n\tpush hl\n"
                  "\tld l,(ix-4)\n\tld h,(ix-3)\n", out);
            mir_emit_backward_sign_extend(out);
            mir_emit_backward_combine_long(out, 0);
            mir_emit_backward_long_call(
                out, plan->functions[MIR_BACKWARD_CLAMP_FUNCTION]);
            fputs("\tpop de\n\tld a,l\n\tld (de),a\n"
                  "\tinc de\n\tld a,h\n\tld (de),a\n", out);

            mir_emit_backward_clamped_add(
                out, plan,
                MIR_BACKWARD_POSITION_GRADIENTS, 0, -1, 32,
                MIR_BACKWARD_EMBEDDING_GRADIENTS, 0, -1, 32,
                -2);
            fputs("\tinc (ix-2)\n", out);
            fprintf(out, "\tjp L%d\nL%d:\n\tinc (ix-1)\n\tjp L%d\n",
                    step6_inner, embedding_inner_done,
                    embedding_outer);
        }
    }

    fprintf(out,
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            done);
}

int mir_try_emit_attention_kernels(FILE *out)
{
    struct MirMatrixProductSchedule matrix_product_schedule;
    struct MirSoftmaxSchedule softmax_schedule;
    struct MirBackwardPassSchedule backward_pass_schedule;

    if (mir_match_matrix_product_schedule(
            &matrix_product_schedule)) {
        mir_emit_matrix_product_schedule(
            out, &matrix_product_schedule);
        return 1;
    }
    if (mir_match_softmax_schedule(&softmax_schedule)) {
        mir_emit_softmax_schedule(out, &softmax_schedule);
        return 1;
    }
    if (mir_match_backward_pass_schedule(
            &backward_pass_schedule)) {
        mir_emit_backward_pass_schedule(
            out, &backward_pass_schedule);
        return 1;
    }
    return -1;
}
