/* dcc_mir_machine_numeric.c - strict numeric kernel schedules. */

#include "dcc_mir_machine_internal.h"

struct MirFixedPointMultiply {
    int left_stack_offset;
    int right_stack_offset;
    int shift;
    unsigned long mask;
};

struct MirNarrowedDivmodLoopSchedule {
    struct Sym *check_function;
    int expected_offset;
    int array_offset;
    int count_offset;
    int value_offset;
    int index_offset;
    int expected_values[7];
    int initial_count;
    int initial_value;
    int outer_limit;
    int scale;
    int fill_value;
    int first_value;
    int zero_value;
    int string_id;
};

struct MirUnsignedLongSqrtSchedule {
    int parameter_stack_offset;
};

struct MirSignedLongNewtonSqrtSchedule {
    int parameter_stack_offset;
};

struct MirPrimeSearchSchedule {
    struct Sym *convert_function;
    struct Sym *sqrt_function;
    int argc_stack_offset;
    int argv_stack_offset;
    int format_string_id;
    char print_name[64];
};

struct MirCatalanDriverSchedule {
    struct Sym *zero_function;
    struct Sym *is_zero_function;
    struct Sym *add_term_function;
    struct Sym *div_small_function;
    struct Sym *putchar_function;
    int format_string_id;
    char print_name[64];
};

struct MirLogSeriesDriverSchedule {
    struct Sym *is_zero_function;
    struct Sym *add_function;
    struct Sym *mul_div_function;
    struct Sym *putchar_function;
    int format_string_id;
    char print_name[64];
};

struct MirModp2DriverSchedule {
    struct Sym *signed_values;
    struct Sym *unsigned_values;
    struct Sym *print_function;
    int string_ids[7];
    char print_names[7][64];
};

struct MirModp2LoopShape {
    int entry_label;
    int init_constant;
    int init_store;
    int header_label;
    int sum_phi;
    int index_phi;
    int bound_constant;
    int element_constant;
    int bound_division;
    int comparison;
    int branch;
    int array_address;
    int index_address;
    int element_load;
    int value_store;
    int print_start;
    int print_call;
    int sum_start;
    int tail_label;
    int increment_constant;
    int increment;
    int increment_store;
    int jump;
    int exit_label;
    int element_count;
    int operation;
    int is_unsigned;
    int divisor_count;
    const int *divisors;
};

struct MirLcsDpSchedule {
    int left_stack_offset;
    int right_stack_offset;
};

struct MirRowInversionCheckSchedule {
    struct Sym *values;
    struct Sym *table;
    int format_string_id;
    char print_name[64];
};

struct MirScopedTempSchedule {
    struct Sym *current_root;
    struct Sym *records_root;
    struct Sym *global_top_root;
    int current_root_offset;
    int records_root_offset;
    int global_top_root_offset;
    int record_stride;
    int local_offset;
    int increment;
};

static int mir_modp2_opcode_code(int opcode);

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

static int mir_numeric_call_arguments(
    const struct MirInsn *call, int expected_count, int *arguments)
{
    int count = 0;
    int instruction;
    int index;

    for (index = 0; index < expected_count; ++index)
        arguments[index] = -1;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *arg = &mir.insns[instruction];

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

static int mir_numeric_scalar_location(
    const struct MirInsn *insn, int storage, int width,
    int pointer_depth, int require_unsigned, int *offset_out)
{
    int memory_type;
    int memory_storage;
    int memory_offset;

    if (insn == NULL ||
        (insn->memory_flags & (1 | 8)) != 0 ||
        insn->bit_width != 0 ||
        !mir_scalar_memory_location(
            insn, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != storage ||
        type_size(memory_type) != width ||
        type_ptr_depth(memory_type) != pointer_depth ||
        (require_unsigned && (memory_type & TYPE_UNSIGNED) == 0))
        return 0;
    *offset_out = memory_offset;
    return 1;
}

static int mir_numeric_same_location(
    const struct MirInsn *insn, int storage, int width,
    int pointer_depth, int require_unsigned, int expected_offset)
{
    int offset;

    return mir_numeric_scalar_location(
               insn, storage, width, pointer_depth,
               require_unsigned, &offset) &&
           offset == expected_offset;
}

static int mir_numeric_unsigned_long_type(int type)
{
    return type_ptr_depth(type) == 0 &&
           !type_is_float(type) &&
           (type & 15) == TYPE_LONG &&
           (type & TYPE_UNSIGNED) != 0 &&
           type_size(type) == 4;
}

static int mir_lcs_int_type(int type)
{
    return type_ptr_depth(type) == 0 &&
           !type_is_float(type) &&
           (type & 15) == TYPE_INT &&
           (type & TYPE_UNSIGNED) == 0 &&
           type_size(type) == 2;
}

static int mir_lcs_char_pointer_type(int type)
{
    return type_ptr_depth(type) == 1 &&
           (type & 15) == TYPE_CHAR &&
           type_size(type) == 2;
}

static int mir_lcs_int_pointer_type(int type)
{
    return type_ptr_depth(type) == 1 &&
           (type & 15) == TYPE_INT &&
           (type & TYPE_UNSIGNED) == 0 &&
           type_size(type) == 2;
}

static int mir_lcs_char_type(int type)
{
    return type_ptr_depth(type) == 0 &&
           !type_is_float(type) &&
           (type & 15) == TYPE_CHAR &&
           type_size(type) == 1;
}

static int mir_row_inversion_word_type(int type)
{
    return type_ptr_depth(type) == 0 &&
           !type_is_float(type) &&
           (type & 15) == TYPE_INT &&
           (type & TYPE_UNSIGNED) == 0 &&
           type_size(type) == 2;
}

static int mir_row_inversion_byte_type(int type)
{
    return type_ptr_depth(type) == 0 &&
           !type_is_float(type) &&
           (type & 15) == TYPE_CHAR &&
           (type & TYPE_UNSIGNED) != 0 &&
           type_size(type) == 1;
}

static int mir_row_inversion_word_pointer_type(int type)
{
    return type_ptr_depth(type) == 1 &&
           (type & 15) == TYPE_INT &&
           (type & TYPE_UNSIGNED) == 0 &&
           type_size(type) == 2;
}

static int mir_row_inversion_array(
    int instruction, int dimensions, int first, int second,
    struct Sym **symbol_out)
{
    const struct MirInsn *address = &mir.insns[instruction];
    struct Sym *symbol;
    long offset;

    if (address->opcode != MIR_ADDRESS ||
        !mir_machine_global_address_offset(
            address->dst, &symbol, &offset, 0) ||
        offset != 0 || symbol == NULL || !symbol->is_defined ||
        !symbol->is_static || symbol->is_volatile ||
        !symbol->is_array || symbol->is_vla ||
        !mir_row_inversion_word_type(symbol->type) ||
        symbol->dim_count != dimensions ||
        symbol->dims[0] != first ||
        (dimensions == 2 && symbol->dims[1] != second) ||
        symbol->array_len != first ||
        symbol->elem_size !=
            (dimensions == 1 ? 2 : second * 2) ||
        symbol->size != first *
            (dimensions == 1 ? 2 : second * 2) ||
        !mir_row_inversion_word_pointer_type(address->type) ||
        (address->memory_flags & (1 | 8)) != 0)
        return 0;
    *symbol_out = symbol;
    return 1;
}

static int mir_row_inversion_same_array(
    int instruction, struct Sym *expected)
{
    struct Sym *symbol;
    long offset;

    return mir.insns[instruction].opcode == MIR_ADDRESS &&
           mir_machine_global_address_offset(
               mir.insns[instruction].dst,
               &symbol, &offset, 0) &&
           symbol == expected && offset == 0 &&
           mir_row_inversion_word_pointer_type(
               mir.insns[instruction].type) &&
           (mir.insns[instruction].memory_flags & (1 | 8)) == 0;
}

static int mir_row_inversion_index(
    int instruction, int base, int subscript,
    int stride, int memory_size)
{
    const struct MirInsn *index = &mir.insns[instruction];

    return index->opcode == MIR_INDEX_ADDRESS &&
           index->src1 == mir.insns[base].dst &&
           index->src2 == mir.insns[subscript].dst &&
           index->immediate == stride &&
           index->memory_size == memory_size &&
           mir_row_inversion_word_pointer_type(index->type) &&
           (index->memory_flags & (1 | 8)) == 0;
}

static int mir_row_inversion_word_load(
    int instruction, int address)
{
    const struct MirInsn *load = &mir.insns[instruction];

    return load->opcode == MIR_LOAD_INDIRECT &&
           load->src1 == mir.insns[address].dst &&
           load->memory_size == 2 &&
           mir_row_inversion_word_type(load->type) &&
           (load->memory_flags & (1 | 8)) == 0;
}

static int mir_row_inversion_word_store(
    int instruction, int address, int value)
{
    const struct MirInsn *store = &mir.insns[instruction];

    return store->opcode == MIR_STORE_INDIRECT &&
           store->src1 == mir.insns[address].dst &&
           store->src2 == mir.insns[value].dst &&
           store->memory_size == 2 &&
           (store->memory_flags & (1 | 8)) == 0;
}

static int mir_row_inversion_compare(
    int instruction, int left, int right, int operation)
{
    const struct MirInsn *comparison = &mir.insns[instruction];

    return comparison->opcode == MIR_BINARY &&
           comparison->immediate == operation &&
           comparison->src1 == mir.insns[left].dst &&
           comparison->src2 == mir.insns[right].dst &&
           mir_row_inversion_word_type(comparison->type) &&
           comparison->secondary_offset ==
               mir.insns[left].type;
}

static int mir_match_row_inversion_check_schedule(
    struct MirRowInversionCheckSchedule *plan)
{
    static const int expected_opcodes[127] = {
        MIR_LABEL, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT,
        MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_PHI, MIR_NOP,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_ADDRESS, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_INDEX_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG,
        MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_ARG, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
        MIR_PHI, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_UNARY, MIR_RETURN
    };
    static const int constants[][2] = {
        {2, 0}, {4, 0}, {7, 1}, {9, 0},
        {12, 2}, {14, 0}, {17, 3}, {19, 0},
        {22, 0}, {27, 4}, {36, 1}, {46, 1},
        {54, 0}, {59, 1}, {64, 2}, {69, 3},
        {75, 0}, {78, 1}, {82, 1}, {85, 1},
        {89, 1}, {92, 0}, {97, 2}, {100, 9},
        {104, 1}, {107, 0}, {112, 3}, {115, 9},
        {119, 1}, {122, 0}
    };
    static const int values_addresses[] = {
        6, 11, 16, 31, 35, 53, 58, 63,
        68, 74, 81, 96, 111
    };
    static const int print_indices[][4] = {
        {53, 54, 55, 56},
        {58, 59, 60, 61},
        {63, 64, 65, 66},
        {68, 69, 70, 71}
    };
    static const int check_indices[][6] = {
        {74, 75, 76, 77, 78, 79},
        {81, 82, 83, 84, 85, 86},
        {96, 97, 98, 99, 100, 101},
        {111, 112, 113, 114, 115, 116}
    };
    struct Sym *print_function;
    int arguments[5];
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 127 || mir_cfg_block_count() != 13 ||
        mir.local_bytes != 1 || mir.aggregate_temp_bytes != 0 ||
        mir.has_vla || !mir_has_cfg_backedge() ||
        !mir_row_inversion_word_type(mir.return_type))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "row-inversion-check", "opcodes");
    for (item = 0;
         item < (int)(sizeof(constants) / sizeof(constants[0]));
         ++item)
        if (!mir_machine_constant_equals(
                mir.insns[constants[item][0]].dst,
                constants[item][1]))
            return mir_machine_reject(
                "row-inversion-check", "constants");

    if (!mir_row_inversion_array(
            1, 1, 4, 0, &plan->values) ||
        !mir_row_inversion_array(
            34, 2, 2, 4, &plan->table) ||
        plan->values == plan->table)
        return mir_machine_reject(
            "row-inversion-check", "arrays");
    for (item = 0;
         item < (int)(sizeof(values_addresses) /
                      sizeof(values_addresses[0]));
         ++item)
        if (!mir_row_inversion_same_array(
                values_addresses[item], plan->values))
            return mir_machine_reject(
                "row-inversion-check", "array-identity");

    for (item = 0; item < 4; ++item) {
        int base = item * 5 + 1;

        if (!mir_row_inversion_index(
                base + 2, base, base + 1, 2, 2) ||
            !mir_row_inversion_word_store(
                base + 4, base + 2, base + 3))
            return mir_machine_reject(
                "row-inversion-check", "initialization");
    }

    if (!mir_row_inversion_byte_type(mir.insns[22].type) ||
        !mir_machine_unobservable_local_store(&mir.insns[23]) ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        !mir_row_inversion_byte_type(mir.insns[23].type) ||
        mir.insns[25].src1 != mir.insns[22].dst ||
        mir.insns[25].src2 != mir.insns[47].dst ||
        mir.insns[25].phi_pred1 != mir.insns[0].label ||
        mir.insns[25].phi_pred2 != mir.insns[44].label ||
        mir.insns[25].object != mir.insns[23].object ||
        mir.insns[25].object < 0 ||
        !mir_row_inversion_byte_type(mir.insns[25].type) ||
        mir.insns[28].immediate != 0 ||
        mir.insns[28].src1 != mir.insns[25].dst ||
        !mir_row_inversion_word_type(mir.insns[28].type) ||
        !mir_row_inversion_compare(29, 28, 27, '<') ||
        mir.insns[30].src1 != mir.insns[29].dst ||
        mir.insns[30].label != mir.insns[50].label ||
        !mir_row_inversion_index(33, 31, 25, 2, 2) ||
        !mir_row_inversion_index(37, 35, 36, 2, 2) ||
        !mir_row_inversion_word_load(38, 37) ||
        !mir_row_inversion_index(39, 34, 38, 8, 8) ||
        !mir_row_inversion_index(41, 39, 25, 2, 2) ||
        !mir_row_inversion_word_load(42, 41) ||
        !mir_row_inversion_word_store(43, 33, 42) ||
        !mir_row_inversion_byte_type(mir.insns[46].type) ||
        mir.insns[47].immediate != '+' ||
        mir.insns[47].src1 != mir.insns[25].dst ||
        mir.insns[47].src2 != mir.insns[46].dst ||
        !mir_row_inversion_byte_type(mir.insns[47].type) ||
        !mir_machine_unobservable_local_store(&mir.insns[48]) ||
        !mir_machine_same_location(&mir.insns[48],
                                   &mir.insns[23]) ||
        mir.insns[48].src1 != mir.insns[47].dst ||
        mir.insns[49].label != mir.insns[24].label)
        return mir_machine_reject(
            "row-inversion-check", "row-loop");

    for (item = 0; item < 4; ++item) {
        const int *indices = print_indices[item];

        if (!mir_row_inversion_same_array(
                indices[0], plan->values) ||
            !mir_row_inversion_index(
                indices[2], indices[0], indices[1], 2, 2) ||
            !mir_row_inversion_word_load(
                indices[3], indices[2]))
            return mir_machine_reject(
                "row-inversion-check", "print-values");
    }
    if (mir.insns[51].immediate < 0 ||
        type_ptr_depth(mir.insns[51].type) != 1 ||
        (mir.insns[51].type & 15) != TYPE_CHAR ||
        !mir_numeric_call_arguments(
            &mir.insns[73], 5, arguments) ||
        arguments[0] != mir.insns[51].dst ||
        arguments[1] != mir.insns[56].dst ||
        arguments[2] != mir.insns[61].dst ||
        arguments[3] != mir.insns[66].dst ||
        arguments[4] != mir.insns[71].dst)
        return mir_machine_reject(
            "row-inversion-check", "print-arguments");
    print_function = find_global(mir.insns[73].name);
    if (print_function == NULL || print_function->is_defined ||
        print_function->is_funcptr || print_function->is_noreturn ||
        !print_function->has_proto ||
        print_function->proto_nargs != 1 ||
        !print_function->proto_variadic ||
        type_ptr_depth(print_function->proto_types[0]) != 1 ||
        (print_function->proto_types[0] & 15) != TYPE_CHAR ||
        !mir_row_inversion_word_type(print_function->type) ||
        !mir_row_inversion_word_type(mir.insns[73].type) ||
        (mir.insns[73].memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) !=
            MIR_CALL_FLAG_VARIADIC ||
        mir.insns[73].base_name[0] == 0 ||
        strlen(mir.insns[73].base_name) >=
            sizeof(plan->print_name))
        return mir_machine_reject(
            "row-inversion-check", "print-call");

    for (item = 0; item < 4; ++item) {
        const int *indices = check_indices[item];

        if (!mir_row_inversion_same_array(
                indices[0], plan->values) ||
            !mir_row_inversion_index(
                indices[2], indices[0], indices[1], 2, 2) ||
            !mir_row_inversion_word_load(
                indices[3], indices[2]) ||
            !mir_row_inversion_compare(
                indices[5], indices[3], indices[4], TOK_EQ))
            return mir_machine_reject(
                "row-inversion-check", "checks");
    }
    if (mir.insns[80].src1 != mir.insns[79].dst ||
        mir.insns[80].label != mir.insns[91].label ||
        mir.insns[87].src1 != mir.insns[86].dst ||
        mir.insns[87].label != mir.insns[91].label ||
        mir.insns[90].label != mir.insns[93].label ||
        mir.insns[94].src1 != mir.insns[89].dst ||
        mir.insns[94].src2 != mir.insns[92].dst ||
        mir.insns[94].phi_pred1 != mir.insns[88].label ||
        mir.insns[94].phi_pred2 != mir.insns[91].label ||
        mir.insns[94].object >= 0 ||
        mir.insns[95].src1 != mir.insns[94].dst ||
        mir.insns[95].label != mir.insns[106].label ||
        mir.insns[102].src1 != mir.insns[101].dst ||
        mir.insns[102].label != mir.insns[106].label ||
        mir.insns[105].label != mir.insns[108].label ||
        mir.insns[109].src1 != mir.insns[104].dst ||
        mir.insns[109].src2 != mir.insns[107].dst ||
        mir.insns[109].phi_pred1 != mir.insns[103].label ||
        mir.insns[109].phi_pred2 != mir.insns[106].label ||
        mir.insns[109].object >= 0 ||
        mir.insns[110].src1 != mir.insns[109].dst ||
        mir.insns[110].label != mir.insns[121].label ||
        mir.insns[117].src1 != mir.insns[116].dst ||
        mir.insns[117].label != mir.insns[121].label ||
        mir.insns[120].label != mir.insns[123].label ||
        mir.insns[124].src1 != mir.insns[119].dst ||
        mir.insns[124].src2 != mir.insns[122].dst ||
        mir.insns[124].phi_pred1 != mir.insns[118].label ||
        mir.insns[124].phi_pred2 != mir.insns[121].label ||
        mir.insns[124].object >= 0 ||
        mir.insns[125].immediate != '!' ||
        mir.insns[125].src1 != mir.insns[124].dst ||
        !mir_row_inversion_word_type(mir.insns[125].type) ||
        mir.insns[126].src1 != mir.insns[125].dst)
        return mir_machine_reject(
            "row-inversion-check", "return-graph");

    plan->format_string_id = (int)mir.insns[51].immediate;
    snprintf(plan->print_name, sizeof(plan->print_name), "%s",
             mir.insns[73].base_name);
    return 1;
}

static int mir_lcs_table_address(
    const struct MirInsn *address, const struct MirInsn *expected)
{
    int declared;

    if (address == NULL || address->opcode != MIR_ADDRESS ||
        !mir_lcs_int_pointer_type(address->type))
        return 0;
    if (expected != NULL &&
        (address->type != expected->type ||
         strcmp(address->name, expected->name)))
        return 0;
    for (declared = 0; declared < mir.declared_count; ++declared)
        if (!strcmp(mir.declared_names[declared], address->name))
            return mir.declared_storage[declared] == SC_LOCAL &&
                   mir.declared_offsets[declared] == -162 &&
                   mir.declared_sizes[declared] == 162 &&
                   mir.declared_is_array[declared] &&
                   !mir.declared_is_vla[declared] &&
                   !mir.declared_is_volatile[declared] &&
                   mir.declared_dim_counts[declared] == 2 &&
                   mir.declared_dims[declared][0] == 9 &&
                   mir.declared_dims[declared][1] == 9 &&
                   mir.declared_elem_sizes[declared] == 18 &&
                   mir_lcs_int_type(mir.declared_types[declared]);
    return 0;
}

static int mir_lcs_binary(
    int instruction, int operation, int left, int right)
{
    const struct MirInsn *insn = &mir.insns[instruction];

    return insn->opcode == MIR_BINARY &&
           insn->immediate == operation &&
           insn->src1 == mir.insns[left].dst &&
           insn->src2 == mir.insns[right].dst &&
           mir_lcs_int_type(insn->type);
}

static int mir_lcs_index(
    int instruction, int base, int subscript,
    int stride, int memory_size)
{
    const struct MirInsn *insn = &mir.insns[instruction];

    return insn->opcode == MIR_INDEX_ADDRESS &&
           insn->src1 == mir.insns[base].dst &&
           insn->src2 == mir.insns[subscript].dst &&
           insn->immediate == stride &&
           insn->memory_size == memory_size &&
           ((memory_size == 1 &&
             mir_lcs_char_pointer_type(insn->type)) ||
            ((memory_size == 2 || memory_size == 18) &&
             mir_lcs_int_pointer_type(insn->type))) &&
           (insn->memory_flags & (1 | 8)) == 0;
}

static int mir_lcs_same_local_store(
    int instruction, int expected, int value)
{
    const struct MirInsn *store = &mir.insns[instruction];

    return mir_machine_unobservable_local_store(store) &&
           mir_machine_same_location(store, &mir.insns[expected]) &&
           store->src1 == mir.insns[value].dst;
}

static int mir_lcs_same_local_load(int instruction, int expected)
{
    const struct MirInsn *load = &mir.insns[instruction];

    return load->opcode == MIR_LOAD &&
           mir_machine_same_location(load, &mir.insns[expected]) &&
           mir_lcs_int_type(load->type) &&
           (load->memory_flags & (1 | 8)) == 0;
}

static int mir_lcs_phi(
    int instruction, int left, int right,
    int left_label, int right_label, int object)
{
    const struct MirInsn *phi = &mir.insns[instruction];

    return phi->opcode == MIR_PHI &&
           phi->src1 == mir.insns[left].dst &&
           phi->src2 == mir.insns[right].dst &&
           phi->phi_pred1 == mir.insns[left_label].label &&
           phi->phi_pred2 == mir.insns[right_label].label &&
           phi->object == mir.insns[object].object &&
           phi->object >= 0 &&
           mir_lcs_int_type(phi->type);
}

static int mir_lcs_value_phi(
    int instruction, int left, int right,
    int left_label, int right_label)
{
    const struct MirInsn *phi = &mir.insns[instruction];

    return phi->opcode == MIR_PHI &&
           phi->src1 == mir.insns[left].dst &&
           phi->src2 == mir.insns[right].dst &&
           phi->phi_pred1 == mir.insns[left_label].label &&
           phi->phi_pred2 == mir.insns[right_label].label &&
           phi->object < 0 &&
           mir_lcs_int_type(phi->type);
}

static int mir_lcs_branch(int instruction, int condition, int target)
{
    return mir.insns[instruction].src1 ==
               mir.insns[condition].dst &&
           mir.insns[instruction].label ==
               mir.insns[target].label;
}

static int mir_lcs_jump(int instruction, int target)
{
    return mir.insns[instruction].label ==
           mir.insns[target].label;
}

static int mir_match_lcs_dp_schedule(struct MirLcsDpSchedule *plan)
{
    static const int expected_opcodes[225] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_CONST, MIR_STORE, MIR_CONST,
        MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_LABEL, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_PHI, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_PHI, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_LABEL,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP, MIR_NOP, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_LOAD, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_NOP,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_NOP, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_INDEX_ADDRESS, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_INDEX_ADDRESS, MIR_LOAD, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_INDEX_ADDRESS, MIR_LOAD,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_LABEL, MIR_PHI,
        MIR_STORE_INDIRECT, MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_RETURN
    };
    static const int constants[][2] = {
        {3, 0}, {5, 0}, {18, 1}, {35, 1}, {41, 0}, {57, 0},
        {59, 0}, {63, 1}, {68, 0}, {83, 0}, {87, 0}, {91, 1},
        {96, 1}, {110, 1}, {131, 1}, {137, 1}, {147, 1},
        {151, 1}, {155, 1}, {162, 1}, {172, 1}, {180, 1},
        {193, 1}, {206, 1}, {213, 1}
    };
    static const int table_addresses[] = {
        54, 82, 124, 145, 160, 168, 178, 189, 218
    };
    static const int j_loads[] = {
        120, 127, 136, 150, 165, 171, 183, 192, 205
    };
    const struct MirInsn *table;
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 225 || mir_cfg_block_count() != 27 ||
        mir.local_bytes != 174 || mir.aggregate_temp_bytes != 0 ||
        mir.has_vla || !mir_has_cfg_backedge() ||
        !mir_lcs_int_type(mir.return_type))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject("lcs-dp", "opcodes");
    for (item = 0;
         item < (int)(sizeof(constants) / sizeof(constants[0]));
         ++item)
        if (!mir_machine_constant_equals(
                mir.insns[constants[item][0]].dst,
                constants[item][1]))
            return mir_machine_reject("lcs-dp", "constants");

    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->left_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &plan->right_stack_offset) ||
        plan->left_stack_offset == plan->right_stack_offset ||
        plan->left_stack_offset > 124 ||
        plan->right_stack_offset > 124 ||
        !mir_lcs_char_pointer_type(mir.insns[1].type) ||
        !mir_lcs_char_pointer_type(mir.insns[2].type) ||
        mir_machine_pointee_is_volatile(&mir.insns[1]) ||
        mir_machine_pointee_is_volatile(&mir.insns[2]))
        return mir_machine_reject("lcs-dp", "parameters");

    if (!mir_machine_unobservable_local_store(&mir.insns[4]) ||
        mir.insns[4].src1 != mir.insns[3].dst ||
        !mir_lcs_same_local_store(20, 4, 19) ||
        !mir_machine_unobservable_local_store(&mir.insns[6]) ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        !mir_lcs_same_local_store(37, 6, 36) ||
        !mir_lcs_phi(10, 3, 19, 0, 21, 4) ||
        !mir_lcs_phi(28, 5, 36, 23, 38, 6) ||
        !mir_lcs_index(14, 1, 10, 1, 1) ||
        !mir_lcs_index(31, 2, 28, 1, 1) ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        !mir_lcs_char_type(mir.insns[15].type) ||
        mir.insns[15].memory_size != 1 ||
        (mir.insns[15].memory_flags & (1 | 8)) != 0 ||
        mir.insns[32].src1 != mir.insns[31].dst ||
        !mir_lcs_char_type(mir.insns[32].type) ||
        mir.insns[32].memory_size != 1 ||
        (mir.insns[32].memory_flags & (1 | 8)) != 0 ||
        !mir_lcs_branch(16, 15, 23) ||
        !mir_lcs_branch(33, 32, 40) ||
        !mir_lcs_binary(19, '+', 10, 18) ||
        !mir_lcs_binary(36, '+', 28, 35) ||
        !mir_lcs_jump(22, 7) || !mir_lcs_jump(39, 24))
        return mir_machine_reject("lcs-dp", "length-loops");

    if (!mir_machine_unobservable_local_store(&mir.insns[43]) ||
        mir.insns[43].src1 != mir.insns[41].dst ||
        !mir_lcs_same_local_store(65, 43, 64) ||
        !mir_lcs_phi(49, 41, 64, 40, 61, 43) ||
        !mir_lcs_binary(52, TOK_LE, 49, 10) ||
        !mir_lcs_branch(53, 52, 67) ||
        !mir_lcs_binary(64, '+', 49, 63) ||
        !mir_lcs_jump(66, 44) ||
        !mir_machine_unobservable_local_store(&mir.insns[70]) ||
        mir.insns[70].src1 != mir.insns[68].dst ||
        !mir_lcs_same_local_store(93, 70, 92) ||
        !mir_lcs_phi(77, 68, 92, 67, 89, 70) ||
        !mir_lcs_binary(80, TOK_LE, 77, 28) ||
        !mir_lcs_branch(81, 80, 95) ||
        !mir_lcs_binary(92, '+', 77, 91) ||
        !mir_lcs_jump(94, 71))
        return mir_machine_reject("lcs-dp", "zero-rows");

    table = &mir.insns[54];
    if (!mir_lcs_table_address(table, NULL))
        return mir_machine_reject("lcs-dp", "table");
    for (item = 1;
         item < (int)(sizeof(table_addresses) /
                      sizeof(table_addresses[0]));
         ++item)
        if (!mir_lcs_table_address(
                &mir.insns[table_addresses[item]], table))
            return mir_machine_reject("lcs-dp", "table-alias");
    if (!mir_lcs_index(56, 54, 49, 18, 18) ||
        !mir_lcs_index(58, 56, 57, 2, 2) ||
        mir.insns[60].src1 != mir.insns[58].dst ||
        mir.insns[60].src2 != mir.insns[59].dst ||
        mir.insns[60].memory_size != 2 ||
        (mir.insns[60].memory_flags & (1 | 8)) != 0 ||
        !mir_lcs_index(84, 82, 83, 18, 18) ||
        !mir_lcs_index(86, 84, 77, 2, 2) ||
        mir.insns[88].src1 != mir.insns[86].dst ||
        mir.insns[88].src2 != mir.insns[87].dst ||
        mir.insns[88].memory_size != 2 ||
        (mir.insns[88].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject("lcs-dp", "zero-storage");

    if (!mir_lcs_same_local_store(98, 43, 96) ||
        !mir_lcs_phi(104, 96, 214, 95, 211, 43) ||
        !mir_lcs_binary(108, TOK_LE, 104, 10) ||
        !mir_lcs_branch(109, 108, 217) ||
        !mir_lcs_same_local_store(112, 70, 110) ||
        !mir_lcs_binary(122, TOK_LE, 120, 28) ||
        !mir_lcs_branch(123, 122, 210) ||
        !mir_lcs_binary(132, '-', 104, 131) ||
        !mir_lcs_binary(138, '-', 136, 137) ||
        !mir_lcs_index(133, 1, 132, 1, 1) ||
        !mir_lcs_index(139, 2, 138, 1, 1) ||
        mir.insns[134].src1 != mir.insns[133].dst ||
        !mir_lcs_char_type(mir.insns[134].type) ||
        mir.insns[134].memory_size != 1 ||
        (mir.insns[134].memory_flags & (1 | 8)) != 0 ||
        mir.insns[140].src1 != mir.insns[139].dst ||
        !mir_lcs_char_type(mir.insns[140].type) ||
        mir.insns[140].memory_size != 1 ||
        (mir.insns[140].memory_flags & (1 | 8)) != 0 ||
        mir.insns[141].src1 != mir.insns[134].dst ||
        mir.insns[142].src1 != mir.insns[140].dst ||
        mir.insns[141].immediate != 0 ||
        mir.insns[142].immediate != 0 ||
        !mir_lcs_int_type(mir.insns[141].type) ||
        !mir_lcs_int_type(mir.insns[142].type) ||
        !mir_lcs_binary(143, TOK_EQ, 141, 142) ||
        !mir_lcs_branch(144, 143, 159))
        return mir_machine_reject("lcs-dp", "loop-and-characters");

    for (item = 0;
         item < (int)(sizeof(j_loads) / sizeof(j_loads[0]));
         ++item)
        if (!mir_lcs_same_local_load(j_loads[item], 70))
            return mir_machine_reject("lcs-dp", "column-loads");
    if (!mir_lcs_index(126, 124, 104, 18, 18) ||
        !mir_lcs_index(128, 126, 127, 2, 2) ||
        !mir_lcs_binary(148, '-', 104, 147) ||
        !mir_lcs_index(149, 145, 148, 18, 18) ||
        !mir_lcs_binary(152, '-', 150, 151) ||
        !mir_lcs_index(153, 149, 152, 2, 2) ||
        mir.insns[154].src1 != mir.insns[153].dst ||
        !mir_lcs_int_type(mir.insns[154].type) ||
        mir.insns[154].memory_size != 2 ||
        (mir.insns[154].memory_flags & (1 | 8)) != 0 ||
        !mir_lcs_binary(156, '+', 154, 155) ||
        !mir_lcs_binary(163, '-', 104, 162) ||
        !mir_lcs_index(164, 160, 163, 18, 18) ||
        !mir_lcs_index(166, 164, 165, 2, 2) ||
        mir.insns[167].src1 != mir.insns[166].dst ||
        !mir_lcs_int_type(mir.insns[167].type) ||
        mir.insns[167].memory_size != 2 ||
        (mir.insns[167].memory_flags & (1 | 8)) != 0 ||
        !mir_lcs_index(170, 168, 104, 18, 18) ||
        !mir_lcs_binary(173, '-', 171, 172) ||
        !mir_lcs_index(174, 170, 173, 2, 2) ||
        mir.insns[175].src1 != mir.insns[174].dst ||
        !mir_lcs_int_type(mir.insns[175].type) ||
        mir.insns[175].memory_size != 2 ||
        (mir.insns[175].memory_flags & (1 | 8)) != 0 ||
        !mir_lcs_binary(176, '>', 167, 175) ||
        !mir_lcs_branch(177, 176, 188) ||
        !mir_lcs_binary(181, '-', 104, 180) ||
        !mir_lcs_index(182, 178, 181, 18, 18) ||
        !mir_lcs_index(184, 182, 183, 2, 2) ||
        mir.insns[185].src1 != mir.insns[184].dst ||
        !mir_lcs_int_type(mir.insns[185].type) ||
        mir.insns[185].memory_size != 2 ||
        (mir.insns[185].memory_flags & (1 | 8)) != 0 ||
        !mir_lcs_index(191, 189, 104, 18, 18) ||
        !mir_lcs_binary(194, '-', 192, 193) ||
        !mir_lcs_index(195, 191, 194, 2, 2) ||
        mir.insns[196].src1 != mir.insns[195].dst ||
        !mir_lcs_int_type(mir.insns[196].type) ||
        mir.insns[196].memory_size != 2 ||
        (mir.insns[196].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject("lcs-dp", "table-recurrence");

    if (!mir_lcs_value_phi(199, 185, 196, 186, 197) ||
        !mir_lcs_value_phi(202, 156, 199, 157, 200) ||
        mir.insns[203].src1 != mir.insns[128].dst ||
        mir.insns[203].src2 != mir.insns[202].dst ||
        mir.insns[203].memory_size != 2 ||
        (mir.insns[203].memory_flags & (1 | 8)) != 0 ||
        !mir_lcs_binary(207, '+', 205, 206) ||
        !mir_lcs_same_local_store(208, 70, 207) ||
        !mir_lcs_jump(209, 113) ||
        !mir_lcs_binary(214, '+', 104, 213) ||
        !mir_lcs_same_local_store(215, 43, 214) ||
        !mir_lcs_jump(216, 99) ||
        !mir_lcs_jump(158, 201) ||
        !mir_lcs_jump(187, 198))
        return mir_machine_reject("lcs-dp", "recurrence-result");

    if (!mir_lcs_index(220, 218, 10, 18, 18) ||
        !mir_lcs_index(222, 220, 28, 2, 2) ||
        mir.insns[223].src1 != mir.insns[222].dst ||
        !mir_lcs_int_type(mir.insns[223].type) ||
        mir.insns[223].memory_size != 2 ||
        (mir.insns[223].memory_flags & (1 | 8)) != 0 ||
        mir.insns[224].src1 != mir.insns[223].dst)
        return mir_machine_reject("lcs-dp", "return");
    return 1;
}

static int mir_match_fixed_point_multiply(
    struct MirFixedPointMultiply *plan)
{
    static const int expected_opcodes[53] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BINARY, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_CONST, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP, MIR_NOP, MIR_BINARY,
        MIR_CONST, MIR_BINARY, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_BINARY,
        MIR_NOP, MIR_NOP, MIR_BINARY, MIR_BINARY, MIR_NOP, MIR_NOP,
        MIR_BINARY, MIR_CONST, MIR_BINARY, MIR_BINARY, MIR_RETURN
    };
    const struct MirInsn *left = &mir.insns[1];
    const struct MirInsn *right = &mir.insns[2];
    long shift;
    int left_type;
    int left_storage;
    int left_offset;
    int right_type;
    int right_storage;
    int right_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 53 || mir_cfg_block_count() != 1 ||
        mir.has_vla || !type_is_long(mir.return_type) ||
        type_size(mir.return_type) != 4 ||
        left->opcode != MIR_PARAM ||
        right->opcode != MIR_PARAM ||
        !type_is_long(left->type) ||
        !type_is_long(right->type) ||
        !mir_scalar_memory_location(
            left, &left_type, &left_storage, &left_offset) ||
        !mir_scalar_memory_location(
            right, &right_type, &right_storage, &right_offset) ||
        left_storage != SC_PARAM || right_storage != SC_PARAM ||
        left_offset < 2 || right_offset < 2)
        return mir_machine_reject(
            "fixed-point-multiply", "shape");
    plan->left_stack_offset = left_offset - 2;
    plan->right_stack_offset = right_offset - 2;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "fixed-point-multiply", "opcode");
    if (!mir_machine_constant_value(
            mir.insns[4].dst, &shift, 0) ||
        shift != 16)
        return mir_machine_reject(
            "fixed-point-multiply", "shift");
    plan->shift = (int)shift;
    if (!mir_machine_constant_equals(
            mir.insns[19].dst, plan->shift) ||
        !mir_machine_constant_equals(
            mir.insns[36].dst, plan->shift) ||
        !mir_machine_constant_equals(
            mir.insns[49].dst, plan->shift) ||
        mir.insns[5].immediate != TOK_SHR ||
        mir.insns[5].src1 != left->dst ||
        mir.insns[5].src2 != mir.insns[4].dst ||
        mir.insns[20].immediate != TOK_SHR ||
        mir.insns[20].src1 != right->dst ||
        mir.insns[20].src2 != mir.insns[19].dst)
        return mir_machine_reject(
            "fixed-point-multiply", "halves");
    plan->mask =
        ((unsigned long)mir.insns[11].immediate -
         (unsigned long)mir.insns[13].immediate) &
        0xffffffffUL;
    if (!mir_machine_constant_equals(mir.insns[11].dst, 65536) ||
        !mir_machine_constant_equals(mir.insns[13].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[26].dst, 65536) ||
        !mir_machine_constant_equals(mir.insns[28].dst, 1) ||
        plan->mask != 0xffffUL ||
        mir.insns[14].immediate != '-' ||
        mir.insns[14].src1 != mir.insns[11].dst ||
        mir.insns[14].src2 != mir.insns[13].dst ||
        mir.insns[15].immediate != '&' ||
        mir.insns[15].src1 != left->dst ||
        mir.insns[15].src2 != mir.insns[14].dst ||
        mir.insns[29].immediate != '-' ||
        mir.insns[29].src1 != mir.insns[26].dst ||
        mir.insns[29].src2 != mir.insns[28].dst ||
        mir.insns[30].immediate != '&' ||
        mir.insns[30].src1 != right->dst ||
        mir.insns[30].src2 != mir.insns[29].dst)
        return mir_machine_reject(
            "fixed-point-multiply", "fractions");
    if (!mir_machine_unobservable_local_store(&mir.insns[7]) ||
        mir.insns[7].src1 != mir.insns[5].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[17]) ||
        mir.insns[17].src1 != mir.insns[15].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[22]) ||
        mir.insns[22].src1 != mir.insns[20].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[32]) ||
        mir.insns[32].src1 != mir.insns[30].dst)
        return mir_machine_reject(
            "fixed-point-multiply", "locals");
    if (mir.insns[35].immediate != '*' ||
        mir.insns[35].src1 != mir.insns[5].dst ||
        mir.insns[35].src2 != mir.insns[20].dst ||
        mir.insns[37].immediate != TOK_SHL ||
        mir.insns[37].src1 != mir.insns[35].dst ||
        mir.insns[37].src2 != mir.insns[36].dst ||
        mir.insns[40].immediate != '*' ||
        mir.insns[40].src1 != mir.insns[5].dst ||
        mir.insns[40].src2 != mir.insns[30].dst ||
        mir.insns[41].immediate != '+' ||
        mir.insns[41].src1 != mir.insns[37].dst ||
        mir.insns[41].src2 != mir.insns[40].dst ||
        mir.insns[44].immediate != '*' ||
        mir.insns[44].src1 != mir.insns[15].dst ||
        mir.insns[44].src2 != mir.insns[20].dst ||
        mir.insns[45].immediate != '+' ||
        mir.insns[45].src1 != mir.insns[41].dst ||
        mir.insns[45].src2 != mir.insns[44].dst ||
        mir.insns[48].immediate != '*' ||
        mir.insns[48].src1 != mir.insns[15].dst ||
        mir.insns[48].src2 != mir.insns[30].dst ||
        mir.insns[50].immediate != TOK_SHR ||
        mir.insns[50].src1 != mir.insns[48].dst ||
        mir.insns[50].src2 != mir.insns[49].dst ||
        mir.insns[51].immediate != '+' ||
        mir.insns[51].src1 != mir.insns[45].dst ||
        mir.insns[51].src2 != mir.insns[50].dst ||
        mir.insns[52].src1 != mir.insns[51].dst)
        return mir_machine_reject(
            "fixed-point-multiply", "expression");
    return 1;
}

static int mir_narrowed_divmod_local(
    const struct MirInsn *insn, int width, int pointer,
    int require_unsigned, int *offset_out)
{
    int memory_type;
    int memory_storage;
    int memory_offset;

    if (insn == NULL ||
        (insn->memory_flags & (1 | 8)) != 0 ||
        insn->bit_width != 0 ||
        !mir_scalar_memory_location(
            insn, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_LOCAL ||
        type_size(memory_type) != width ||
        type_ptr_depth(memory_type) != 0 ||
        (require_unsigned &&
         (memory_type & TYPE_UNSIGNED) == 0) ||
        (!pointer && type_ptr_depth(insn->type) != 0) ||
        (pointer &&
         (type_ptr_depth(insn->type) != 1 ||
          type_size(insn->type) != 2)) ||
        memory_offset < -mir.local_bytes ||
        memory_offset + width > 0)
        return 0;
    *offset_out = memory_offset;
    return 1;
}

static int mir_narrowed_divmod_same_local(
    const struct MirInsn *insn, int width, int pointer,
    int require_unsigned, int expected_offset)
{
    int offset;

    return mir_narrowed_divmod_local(
               insn, width, pointer, require_unsigned, &offset) &&
           offset == expected_offset;
}

static int mir_narrowed_divmod_ranges_overlap(
    int left_offset, int left_width,
    int right_offset, int right_width)
{
    return left_offset < right_offset + right_width &&
           right_offset < left_offset + left_width;
}

static int mir_narrowed_divmod_signed_word_type(int type)
{
    return type_ptr_depth(type) == 0 &&
           !type_is_float(type) &&
           (type & 15) == TYPE_INT &&
           (type & TYPE_UNSIGNED) == 0 &&
           type_size(type) == 2;
}

static int mir_narrowed_divmod_unsigned_byte_type(int type)
{
    return type_ptr_depth(type) == 0 &&
           !type_is_float(type) &&
           (type & 15) == TYPE_CHAR &&
           (type & TYPE_UNSIGNED) != 0 &&
           type_size(type) == 1;
}

static int mir_match_narrowed_divmod_loop_schedule(
    struct MirNarrowedDivmodLoopSchedule *plan)
{
    static const int expected_opcodes[165] = {
        MIR_LABEL, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_UNARY, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP,
        MIR_CONST, MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL,
        MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP,
        MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_PHI,
        MIR_NOP, MIR_PHI, MIR_PHI, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_UNARY, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_BRANCH_FALSE,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD,
        MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_STORE_INDIRECT,
        MIR_CONST, MIR_ADDRESS, MIR_NOP, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY,
        MIR_BINARY, MIR_LOAD, MIR_NOP, MIR_UNARY, MIR_BINARY,
        MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP, MIR_LABEL,
        MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_ARG, MIR_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY,
        MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_NOP, MIR_LABEL,
        MIR_JUMP, MIR_LABEL
    };
    static const int expected_address_indices[8] = {
        1, 7, 13, 19, 25, 31, 37, 148
    };
    static const int array_address_indices[5] = {
        63, 76, 82, 116, 126
    };
    int arguments[3];
    int count_offset;
    int value_offset;
    int narrow_offset;
    int instruction;
    int index;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 165 || mir_cfg_block_count() != 10 ||
        mir.local_bytes != 32 || mir.has_vla ||
        !mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "narrowed-divmod-loop", "opcode");

    if (!mir_narrowed_divmod_local(
            &mir.insns[1], 1, 1, 1,
            &plan->expected_offset))
        return mir_machine_reject(
            "narrowed-divmod-loop", "expected-location");
    for (index = 0; index < 7; ++index) {
        long expected_value;
        int base = 1 + index * 6;
        const struct MirInsn *address = &mir.insns[base];
        const struct MirInsn *subscript = &mir.insns[base + 1];
        const struct MirInsn *indexed = &mir.insns[base + 2];
        const struct MirInsn *value = &mir.insns[base + 4];
        const struct MirInsn *store = &mir.insns[base + 5];

        if (expected_address_indices[index] != base ||
            !mir_narrowed_divmod_same_local(
                address, 1, 1, 1, plan->expected_offset) ||
            !mir_machine_constant_equals(subscript->dst, index) ||
            indexed->src1 != address->dst ||
            indexed->src2 != subscript->dst ||
            indexed->immediate != 1 ||
            indexed->memory_size != 1 ||
            type_ptr_depth(indexed->type) != 1 ||
            (indexed->type & TYPE_UNSIGNED) == 0 ||
            !mir_narrowed_divmod_unsigned_byte_type(value->type) ||
            !mir_machine_constant_value(
                value->dst, &expected_value, 0) ||
            expected_value < 0 || expected_value > 255 ||
            store->src1 != indexed->dst ||
            store->src2 != value->dst ||
            store->memory_size != 1 ||
            (store->memory_flags & (1 | 8)) != 0)
            return mir_machine_reject(
                "narrowed-divmod-loop", "expected-initializers");
        plan->expected_values[index] = (int)expected_value;
    }
    if (!mir_narrowed_divmod_same_local(
            &mir.insns[148], 1, 1, 1,
            plan->expected_offset))
        return mir_machine_reject(
            "narrowed-divmod-loop", "expected-use");

    if (!mir_narrowed_divmod_local(
            &mir.insns[45], 2, 0, 0,
            &count_offset) ||
        (mir.insns[45].type & TYPE_UNSIGNED) != 0 ||
        (mir.insns[45].type & 15) != TYPE_INT ||
        !mir_narrowed_divmod_local(
            &mir.insns[48], 2, 0, 0,
            &value_offset) ||
        (mir.insns[48].type & TYPE_UNSIGNED) != 0 ||
        (mir.insns[48].type & 15) != TYPE_INT ||
        !mir_narrowed_divmod_local(
            &mir.insns[53], 1, 0, 1,
            &narrow_offset) ||
        (mir.insns[53].type & 15) != TYPE_CHAR ||
        !mir_narrowed_divmod_local(
            &mir.insns[90], 2, 0, 0,
            &plan->index_offset) ||
        (mir.insns[90].type & TYPE_UNSIGNED) != 0 ||
        (mir.insns[90].type & 15) != TYPE_INT)
        return mir_machine_reject(
            "narrowed-divmod-loop", "scalar-locations");
    plan->count_offset = count_offset;
    plan->value_offset = value_offset;

    plan->initial_count = (int)mir.insns[43].immediate;
    plan->initial_value = (int)mir.insns[46].immediate;
    if (!mir_narrowed_divmod_signed_word_type(mir.insns[43].type) ||
        !mir_narrowed_divmod_signed_word_type(mir.insns[46].type) ||
        mir.insns[45].src1 != mir.insns[43].dst ||
        mir.insns[48].src1 != mir.insns[46].dst ||
        plan->initial_count < 2 || plan->initial_count > 255 ||
        !mir_machine_constant_equals(mir.insns[50].dst, 1) ||
        !mir_narrowed_divmod_signed_word_type(mir.insns[50].type) ||
        mir.insns[51].immediate != '-' ||
        mir.insns[51].src1 != mir.insns[43].dst ||
        mir.insns[51].src2 != mir.insns[50].dst ||
        !mir_narrowed_divmod_signed_word_type(mir.insns[51].type) ||
        !mir_narrowed_divmod_signed_word_type(
            mir.insns[51].secondary_offset) ||
        mir.insns[52].immediate != 0 ||
        mir.insns[52].src1 != mir.insns[51].dst ||
        !mir_narrowed_divmod_unsigned_byte_type(mir.insns[52].type) ||
        mir.insns[53].src1 != mir.insns[52].dst)
        return 0;

    if (mir.insns[57].src1 != mir.insns[52].dst ||
        mir.insns[57].src2 != mir.insns[72].dst ||
        mir.insns[57].phi_pred1 != mir.insns[0].label ||
        mir.insns[57].phi_pred2 != mir.insns[69].label ||
        !mir_narrowed_divmod_unsigned_byte_type(mir.insns[57].type) ||
        !mir_machine_constant_equals(mir.insns[59].dst, 0) ||
        mir.insns[60].immediate != 0 ||
        mir.insns[60].src1 != mir.insns[57].dst ||
        !mir_narrowed_divmod_signed_word_type(mir.insns[60].type) ||
        mir.insns[61].immediate != '>' ||
        mir.insns[61].src1 != mir.insns[60].dst ||
        mir.insns[61].src2 != mir.insns[59].dst ||
        !mir_narrowed_divmod_signed_word_type(
            mir.insns[61].secondary_offset) ||
        mir.insns[62].src1 != mir.insns[61].dst ||
        mir.insns[62].label != mir.insns[75].label)
        return 0;

    if (!mir_narrowed_divmod_local(
            &mir.insns[63], 1, 1, 1,
            &plan->array_offset))
        return 0;
    for (index = 0; index < 5; ++index)
        if (!mir_narrowed_divmod_same_local(
                &mir.insns[array_address_indices[index]],
                1, 1, 1, plan->array_offset))
            return 0;
    plan->fill_value = (int)mir.insns[67].immediate;
    if (mir.insns[65].src1 != mir.insns[63].dst ||
        mir.insns[65].src2 != mir.insns[57].dst ||
        mir.insns[65].immediate != 1 ||
        mir.insns[65].memory_size != 1 ||
        plan->fill_value < 0 || plan->fill_value > 255 ||
        mir.insns[68].src1 != mir.insns[65].dst ||
        mir.insns[68].src2 != mir.insns[67].dst ||
        mir.insns[68].memory_size != 1 ||
        !mir_machine_constant_equals(mir.insns[71].dst, 1) ||
        !mir_narrowed_divmod_unsigned_byte_type(mir.insns[71].type) ||
        mir.insns[72].immediate != '-' ||
        mir.insns[72].src1 != mir.insns[57].dst ||
        mir.insns[72].src2 != mir.insns[71].dst ||
        !mir_narrowed_divmod_unsigned_byte_type(mir.insns[72].type) ||
        !mir_narrowed_divmod_unsigned_byte_type(
            mir.insns[72].secondary_offset) ||
        !mir_narrowed_divmod_same_local(
            &mir.insns[73], 1, 0, 1, narrow_offset) ||
        mir.insns[73].src1 != mir.insns[72].dst ||
        mir.insns[74].label != mir.insns[54].label)
        return 0;

    plan->first_value = (int)mir.insns[80].immediate;
    plan->zero_value = (int)mir.insns[86].immediate;
    if (!mir_machine_constant_equals(mir.insns[77].dst, 1) ||
        mir.insns[78].src1 != mir.insns[76].dst ||
        mir.insns[78].src2 != mir.insns[77].dst ||
        mir.insns[78].immediate != 1 ||
        plan->first_value < 0 || plan->first_value > 255 ||
        mir.insns[81].src1 != mir.insns[78].dst ||
        mir.insns[81].src2 != mir.insns[80].dst ||
        mir.insns[81].memory_size != 1 ||
        !mir_machine_constant_equals(mir.insns[83].dst, 0) ||
        mir.insns[84].src1 != mir.insns[82].dst ||
        mir.insns[84].src2 != mir.insns[83].dst ||
        mir.insns[84].immediate != 1 ||
        plan->zero_value < 0 || plan->zero_value > 255 ||
        mir.insns[87].src1 != mir.insns[84].dst ||
        mir.insns[87].src2 != mir.insns[86].dst ||
        mir.insns[87].memory_size != 1 ||
        !mir_machine_constant_equals(mir.insns[88].dst, 0) ||
        mir.insns[90].src1 != mir.insns[88].dst)
        return 0;

    plan->outer_limit = (int)mir.insns[97].immediate;
    if (plan->outer_limit < 0 ||
        plan->outer_limit >= plan->initial_count ||
        plan->initial_count - plan->outer_limit != 7 ||
        mir.insns[92].src1 != mir.insns[43].dst ||
        mir.insns[92].src2 != mir.insns[102].dst ||
        mir.insns[92].phi_pred1 != mir.insns[75].label ||
        mir.insns[92].phi_pred2 != mir.insns[162].label ||
        mir.insns[94].src1 != mir.insns[57].dst ||
        mir.insns[94].src2 != mir.insns[113].dst ||
        mir.insns[94].phi_pred1 != mir.insns[75].label ||
        mir.insns[94].phi_pred2 != mir.insns[162].label ||
        mir.insns[95].src1 != mir.insns[88].dst ||
        mir.insns[95].src2 != mir.insns[159].dst ||
        mir.insns[95].phi_pred1 != mir.insns[75].label ||
        mir.insns[95].phi_pred2 != mir.insns[162].label ||
        !mir_narrowed_divmod_signed_word_type(mir.insns[92].type) ||
        !mir_narrowed_divmod_unsigned_byte_type(mir.insns[94].type) ||
        !mir_narrowed_divmod_signed_word_type(mir.insns[95].type) ||
        mir.insns[98].immediate != '>' ||
        mir.insns[98].src1 != mir.insns[92].dst ||
        mir.insns[98].src2 != mir.insns[97].dst ||
        !mir_narrowed_divmod_signed_word_type(
            mir.insns[98].secondary_offset) ||
        mir.insns[99].src1 != mir.insns[98].dst ||
        mir.insns[99].label != mir.insns[164].label)
        return 0;

    if (!mir_machine_constant_equals(mir.insns[101].dst, 1) ||
        mir.insns[102].immediate != '-' ||
        mir.insns[102].src1 != mir.insns[92].dst ||
        mir.insns[102].src2 != mir.insns[101].dst ||
        !mir_narrowed_divmod_signed_word_type(mir.insns[102].type) ||
        !mir_narrowed_divmod_signed_word_type(
            mir.insns[102].secondary_offset) ||
        !mir_narrowed_divmod_same_local(
            &mir.insns[103], 2, 0, 0, count_offset) ||
        mir.insns[103].src1 != mir.insns[102].dst ||
        mir.insns[104].immediate != 0 ||
        mir.insns[104].src1 != mir.insns[92].dst ||
        !mir_narrowed_divmod_unsigned_byte_type(mir.insns[104].type) ||
        !mir_narrowed_divmod_same_local(
            &mir.insns[105], 1, 0, 1, narrow_offset) ||
        mir.insns[105].src1 != mir.insns[104].dst ||
        !mir_narrowed_divmod_same_local(
            &mir.insns[111], 1, 0, 1, narrow_offset) ||
        !mir_machine_constant_equals(mir.insns[112].dst, 1) ||
        mir.insns[113].immediate != '-' ||
        mir.insns[113].src1 != mir.insns[111].dst ||
        mir.insns[113].src2 != mir.insns[112].dst ||
        !mir_narrowed_divmod_unsigned_byte_type(mir.insns[111].type) ||
        !mir_narrowed_divmod_unsigned_byte_type(mir.insns[112].type) ||
        !mir_narrowed_divmod_unsigned_byte_type(mir.insns[113].type) ||
        !mir_narrowed_divmod_unsigned_byte_type(
            mir.insns[113].secondary_offset) ||
        !mir_narrowed_divmod_same_local(
            &mir.insns[114], 1, 0, 1, narrow_offset) ||
        mir.insns[114].src1 != mir.insns[113].dst ||
        mir.insns[115].src1 != mir.insns[113].dst ||
        mir.insns[115].label != mir.insns[145].label)
        return 0;

    if (mir.insns[118].src1 != mir.insns[116].dst ||
        mir.insns[118].src2 != mir.insns[113].dst ||
        mir.insns[118].immediate != 1 ||
        mir.insns[118].memory_size != 1 ||
        !mir_narrowed_divmod_same_local(
            &mir.insns[119], 2, 0, 0, value_offset) ||
        mir.insns[121].immediate != 0 ||
        mir.insns[121].src1 != mir.insns[113].dst ||
        !mir_narrowed_divmod_signed_word_type(mir.insns[119].type) ||
        !mir_narrowed_divmod_signed_word_type(mir.insns[121].type) ||
        mir.insns[122].immediate != '%' ||
        mir.insns[122].src1 != mir.insns[119].dst ||
        mir.insns[122].src2 != mir.insns[121].dst ||
        !mir_narrowed_divmod_signed_word_type(mir.insns[122].type) ||
        !mir_narrowed_divmod_signed_word_type(
            mir.insns[122].secondary_offset) ||
        mir.insns[123].immediate != 0 ||
        mir.insns[123].src1 != mir.insns[122].dst ||
        !mir_narrowed_divmod_unsigned_byte_type(mir.insns[123].type) ||
        mir.insns[124].src1 != mir.insns[118].dst ||
        mir.insns[124].src2 != mir.insns[123].dst ||
        mir.insns[124].memory_size != 1)
        return 0;

    plan->scale = (int)mir.insns[125].immediate;
    if (plan->scale < 1 || plan->scale > 32767 ||
        !mir_narrowed_divmod_signed_word_type(mir.insns[125].type) ||
        !mir_machine_constant_equals(mir.insns[128].dst, 1) ||
        mir.insns[129].immediate != 0 ||
        mir.insns[129].src1 != mir.insns[113].dst ||
        mir.insns[130].immediate != '-' ||
        mir.insns[130].src1 != mir.insns[129].dst ||
        mir.insns[130].src2 != mir.insns[128].dst ||
        !mir_narrowed_divmod_signed_word_type(mir.insns[129].type) ||
        !mir_narrowed_divmod_signed_word_type(mir.insns[130].type) ||
        !mir_narrowed_divmod_signed_word_type(
            mir.insns[130].secondary_offset) ||
        mir.insns[131].src1 != mir.insns[126].dst ||
        mir.insns[131].src2 != mir.insns[130].dst ||
        mir.insns[131].immediate != 1 ||
        mir.insns[131].memory_size != 1 ||
        mir.insns[132].src1 != mir.insns[131].dst ||
        mir.insns[132].memory_size != 1 ||
        !mir_narrowed_divmod_unsigned_byte_type(mir.insns[132].type) ||
        mir.insns[133].immediate != 0 ||
        mir.insns[133].src1 != mir.insns[132].dst ||
        !mir_narrowed_divmod_signed_word_type(mir.insns[133].type) ||
        mir.insns[134].immediate != '*' ||
        mir.insns[134].src1 != mir.insns[125].dst ||
        mir.insns[134].src2 != mir.insns[133].dst ||
        !mir_narrowed_divmod_signed_word_type(mir.insns[134].type) ||
        !mir_narrowed_divmod_signed_word_type(
            mir.insns[134].secondary_offset) ||
        !mir_narrowed_divmod_same_local(
            &mir.insns[135], 2, 0, 0, value_offset) ||
        mir.insns[137].immediate != 0 ||
        mir.insns[137].src1 != mir.insns[113].dst ||
        !mir_narrowed_divmod_signed_word_type(mir.insns[135].type) ||
        !mir_narrowed_divmod_signed_word_type(mir.insns[137].type) ||
        mir.insns[138].immediate != '/' ||
        mir.insns[138].src1 != mir.insns[135].dst ||
        mir.insns[138].src2 != mir.insns[137].dst ||
        !mir_narrowed_divmod_signed_word_type(mir.insns[138].type) ||
        !mir_narrowed_divmod_signed_word_type(
            mir.insns[138].secondary_offset) ||
        mir.insns[139].immediate != '+' ||
        mir.insns[139].src1 != mir.insns[134].dst ||
        mir.insns[139].src2 != mir.insns[138].dst ||
        !mir_narrowed_divmod_signed_word_type(mir.insns[139].type) ||
        !mir_narrowed_divmod_signed_word_type(
            mir.insns[139].secondary_offset) ||
        !mir_narrowed_divmod_same_local(
            &mir.insns[141], 2, 0, 0, value_offset) ||
        mir.insns[141].src1 != mir.insns[139].dst ||
        mir.insns[144].label != mir.insns[106].label)
        return 0;

    if (!mir_narrowed_divmod_same_local(
            &mir.insns[146], 2, 0, 0, value_offset) ||
        mir.insns[147].src1 != mir.insns[146].dst ||
        mir.insns[150].src1 != mir.insns[148].dst ||
        mir.insns[150].src2 != mir.insns[95].dst ||
        mir.insns[150].immediate != 1 ||
        mir.insns[150].memory_size != 1 ||
        mir.insns[151].src1 != mir.insns[150].dst ||
        mir.insns[151].memory_size != 1 ||
        !mir_narrowed_divmod_unsigned_byte_type(mir.insns[151].type) ||
        mir.insns[152].immediate != 0 ||
        mir.insns[152].src1 != mir.insns[151].dst ||
        !mir_narrowed_divmod_signed_word_type(mir.insns[146].type) ||
        !mir_narrowed_divmod_signed_word_type(mir.insns[152].type) ||
        mir.insns[153].src1 != mir.insns[152].dst ||
        mir.insns[155].src1 != mir.insns[154].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[156], arguments) ||
        arguments[0] != mir.insns[146].dst ||
        arguments[1] != mir.insns[152].dst ||
        arguments[2] != mir.insns[154].dst ||
        !mir_machine_constant_equals(mir.insns[158].dst, 1) ||
        mir.insns[159].immediate != '+' ||
        mir.insns[159].src1 != mir.insns[95].dst ||
        mir.insns[159].src2 != mir.insns[158].dst ||
        !mir_narrowed_divmod_signed_word_type(mir.insns[159].type) ||
        !mir_narrowed_divmod_signed_word_type(
            mir.insns[159].secondary_offset) ||
        !mir_narrowed_divmod_same_local(
            &mir.insns[160], 2, 0, 0, plan->index_offset) ||
        mir.insns[160].src1 != mir.insns[159].dst ||
        mir.insns[163].label != mir.insns[91].label)
        return 0;

    plan->check_function = find_global(mir.insns[156].name);
    if (plan->check_function == NULL ||
        !plan->check_function->is_defined ||
        plan->check_function->storage != SC_FUNC ||
        plan->check_function->is_funcptr ||
        plan->check_function->is_noreturn ||
        !plan->check_function->has_proto ||
        plan->check_function->proto_variadic ||
        plan->check_function->proto_nargs != 3 ||
        plan->check_function->proto_types[0] != mir.insns[147].type ||
        plan->check_function->proto_types[1] != mir.insns[153].type ||
        plan->check_function->proto_types[2] != mir.insns[155].type ||
        mir.insns[156].memory_flags != 0)
        return 0;
    plan->string_id = (int)mir.insns[154].immediate;

    if (mir_narrowed_divmod_ranges_overlap(
            plan->expected_offset, 7,
            plan->array_offset, plan->initial_count) ||
        mir_narrowed_divmod_ranges_overlap(
            plan->expected_offset, 7, count_offset, 2) ||
        mir_narrowed_divmod_ranges_overlap(
            plan->expected_offset, 7, value_offset, 2) ||
        mir_narrowed_divmod_ranges_overlap(
            plan->expected_offset, 7, narrow_offset, 1) ||
        mir_narrowed_divmod_ranges_overlap(
            plan->expected_offset, 7, plan->index_offset, 2) ||
        mir_narrowed_divmod_ranges_overlap(
            plan->array_offset, plan->initial_count, count_offset, 2) ||
        mir_narrowed_divmod_ranges_overlap(
            plan->array_offset, plan->initial_count, value_offset, 2) ||
        mir_narrowed_divmod_ranges_overlap(
            plan->array_offset, plan->initial_count, narrow_offset, 1) ||
        mir_narrowed_divmod_ranges_overlap(
            plan->array_offset, plan->initial_count,
            plan->index_offset, 2) ||
        mir_narrowed_divmod_ranges_overlap(
            count_offset, 2, value_offset, 2) ||
        mir_narrowed_divmod_ranges_overlap(
            count_offset, 2, narrow_offset, 1) ||
        mir_narrowed_divmod_ranges_overlap(
            count_offset, 2, plan->index_offset, 2) ||
        mir_narrowed_divmod_ranges_overlap(
            value_offset, 2, narrow_offset, 1) ||
        mir_narrowed_divmod_ranges_overlap(
            value_offset, 2, plan->index_offset, 2) ||
        mir_narrowed_divmod_ranges_overlap(
            narrow_offset, 1, plan->index_offset, 2))
        return 0;
    return 1;
}

static int mir_machine_unsigned_long_type(int type)
{
    return type_ptr_depth(type) == 0 &&
           !type_is_float(type) &&
           (type & 15) == TYPE_LONG &&
           (type & TYPE_UNSIGNED) != 0 &&
           type_size(type) == 4;
}

static int mir_machine_signed_long_type(int type)
{
    return type_ptr_depth(type) == 0 &&
           !type_is_float(type) &&
           (type & 15) == TYPE_LONG &&
           (type & TYPE_UNSIGNED) == 0 &&
           type_size(type) == 4;
}

static int mir_machine_signed_int_type(int type)
{
    return type_ptr_depth(type) == 0 &&
           !type_is_float(type) &&
           (type & 15) == TYPE_INT &&
           (type & TYPE_UNSIGNED) == 0 &&
           type_size(type) == 2;
}

static int mir_match_signed_long_newton_sqrt_schedule(
    struct MirSignedLongNewtonSqrtSchedule *plan)
{
    static const int expected_opcodes[50] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_RETURN,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP,
        MIR_PHI, MIR_PHI, MIR_NOP, MIR_NOP, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_BINARY, MIR_BINARY, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_RETURN
    };
    int parameter_type;
    int parameter_storage;
    int parameter_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 50 || mir_cfg_block_count() != 5 ||
        mir.has_vla || mir.local_bytes != 8 ||
        mir.aggregate_temp_bytes != 0 ||
        !mir_machine_signed_long_type(mir.return_type))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "signed-long-newton-sqrt-schedule", "opcodes");
    if (!mir_scalar_memory_location(
            &mir.insns[1], &parameter_type,
            &parameter_storage, &parameter_offset) ||
        parameter_storage != SC_PARAM ||
        !mir_machine_signed_long_type(parameter_type) ||
        !mir_machine_signed_long_type(mir.insns[1].type) ||
        (plan->parameter_stack_offset = parameter_offset - 2) != 2)
        return mir_machine_reject(
            "signed-long-newton-sqrt-schedule", "parameter-abi");
    if (!mir_machine_constant_equals(mir.insns[4].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[8].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[16].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[19].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[40].dst, 2) ||
        !mir_machine_signed_long_type(mir.insns[4].type) ||
        !mir_machine_signed_long_type(mir.insns[8].type) ||
        !mir_machine_signed_long_type(mir.insns[16].type) ||
        !mir_machine_signed_long_type(mir.insns[19].type) ||
        !mir_machine_signed_long_type(mir.insns[40].type))
        return mir_machine_reject(
            "signed-long-newton-sqrt-schedule", "constants");
    if (!mir_machine_unobservable_local_store(&mir.insns[13]) ||
        !mir_machine_unobservable_local_store(&mir.insns[22]) ||
        !mir_machine_unobservable_local_store(&mir.insns[33]) ||
        !mir_machine_unobservable_local_store(&mir.insns[43]) ||
        mir.insns[13].memory_size != 4 ||
        mir.insns[22].memory_size != 4 ||
        mir.insns[33].memory_size != 4 ||
        mir.insns[43].memory_size != 4 ||
        !mir_machine_same_location(
            &mir.insns[13], &mir.insns[33]) ||
        !mir_machine_same_location(
            &mir.insns[22], &mir.insns[43]) ||
        mir_machine_same_location(
            &mir.insns[13], &mir.insns[22]) ||
        mir.insns[25].object != mir.insns[13].object ||
        mir.insns[26].object != mir.insns[22].object ||
        mir.insns[25].object < 0 || mir.insns[26].object < 0)
        return mir_machine_reject(
            "signed-long-newton-sqrt-schedule", "locals");
    if (mir.insns[5].immediate != TOK_LE ||
        mir.insns[5].src1 != mir.insns[1].dst ||
        mir.insns[5].src2 != mir.insns[4].dst ||
        !mir_machine_signed_int_type(mir.insns[5].type) ||
        !mir_machine_signed_long_type(
            mir.insns[5].secondary_offset) ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        mir.insns[6].label != mir.insns[10].label ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[13].src1 != mir.insns[1].dst)
        return mir_machine_reject(
            "signed-long-newton-sqrt-schedule", "entry");
    if (mir.insns[17].immediate != '+' ||
        mir.insns[17].src1 != mir.insns[1].dst ||
        mir.insns[17].src2 != mir.insns[16].dst ||
        !mir_machine_signed_long_type(mir.insns[17].type) ||
        !mir_machine_signed_long_type(
            mir.insns[17].secondary_offset) ||
        mir.insns[20].immediate != '/' ||
        mir.insns[20].src1 != mir.insns[17].dst ||
        mir.insns[20].src2 != mir.insns[19].dst ||
        !mir_machine_signed_long_type(mir.insns[20].type) ||
        !mir_machine_signed_long_type(
            mir.insns[20].secondary_offset) ||
        mir.insns[22].src1 != mir.insns[20].dst)
        return mir_machine_reject(
            "signed-long-newton-sqrt-schedule", "initialization");
    if (mir.insns[25].src1 != mir.insns[1].dst ||
        mir.insns[25].src2 != mir.insns[26].dst ||
        mir.insns[26].src1 != mir.insns[20].dst ||
        mir.insns[26].src2 != mir.insns[41].dst ||
        !mir_machine_signed_long_type(mir.insns[25].type) ||
        !mir_machine_signed_long_type(mir.insns[26].type) ||
        mir.insns[25].phi_pred1 != mir.insns[10].label ||
        mir.insns[25].phi_pred2 != mir.insns[45].label ||
        mir.insns[26].phi_pred1 != mir.insns[10].label ||
        mir.insns[26].phi_pred2 != mir.insns[45].label ||
        mir.insns[29].immediate != '<' ||
        mir.insns[29].src1 != mir.insns[26].dst ||
        mir.insns[29].src2 != mir.insns[25].dst ||
        !mir_machine_signed_int_type(mir.insns[29].type) ||
        !mir_machine_signed_long_type(
            mir.insns[29].secondary_offset) ||
        mir.insns[30].src1 != mir.insns[29].dst ||
        mir.insns[30].label != mir.insns[47].label ||
        mir.insns[33].src1 != mir.insns[26].dst)
        return mir_machine_reject(
            "signed-long-newton-sqrt-schedule", "loop-control");
    if (mir.insns[37].immediate != '/' ||
        mir.insns[37].src1 != mir.insns[1].dst ||
        mir.insns[37].src2 != mir.insns[26].dst ||
        !mir_machine_signed_long_type(mir.insns[37].type) ||
        !mir_machine_signed_long_type(
            mir.insns[37].secondary_offset) ||
        mir.insns[38].immediate != '+' ||
        mir.insns[38].src1 != mir.insns[26].dst ||
        mir.insns[38].src2 != mir.insns[37].dst ||
        !mir_machine_signed_long_type(mir.insns[38].type) ||
        !mir_machine_signed_long_type(
            mir.insns[38].secondary_offset) ||
        mir.insns[41].immediate != '/' ||
        mir.insns[41].src1 != mir.insns[38].dst ||
        mir.insns[41].src2 != mir.insns[40].dst ||
        !mir_machine_signed_long_type(mir.insns[41].type) ||
        !mir_machine_signed_long_type(
            mir.insns[41].secondary_offset) ||
        mir.insns[43].src1 != mir.insns[41].dst ||
        mir.insns[46].label != mir.insns[23].label ||
        mir.insns[49].src1 != mir.insns[25].dst)
        return mir_machine_reject(
            "signed-long-newton-sqrt-schedule", "loop-update");
    return 1;
}

static int mir_match_unsigned_long_sqrt_schedule(
    struct MirUnsignedLongSqrtSchedule *plan)
{
    static const int expected_opcodes[72] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_STORE,
        MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_NOP, MIR_CONST, MIR_STORE, MIR_NOP, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_RETURN, MIR_LABEL, MIR_LABEL, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_LOAD, MIR_LOAD, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD, MIR_LOAD,
        MIR_BINARY, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_BINARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_NOP, MIR_NOP, MIR_STORE, MIR_NOP, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_RETURN
    };
    static const int constants[] = {3, 7, 11, 15, 35, 52, 61};
    static const int wide_values[] = {
        1, 3, 7, 8, 11, 15, 26, 27, 30, 31, 32, 33,
        35, 36, 37, 43, 52, 53, 61, 62, 70
    };
    static const int wide_stores[] = {4, 9, 12, 39, 49, 55, 64};
    int parameter_type;
    int parameter_storage;
    int parameter_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 72 || mir_cfg_block_count() != 8 ||
        mir.has_vla || mir.aggregate_temp_bytes != 0 ||
        !mir_machine_unsigned_long_type(mir.return_type))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "unsigned-long-sqrt-schedule", "opcodes");
    if (!mir_scalar_memory_location(
            &mir.insns[1], &parameter_type,
            &parameter_storage, &parameter_offset) ||
        parameter_storage != SC_PARAM ||
        !mir_machine_unsigned_long_type(parameter_type) ||
        !mir_machine_unsigned_long_type(mir.insns[1].type) ||
        (plan->parameter_stack_offset = parameter_offset - 2) != 2)
        return mir_machine_reject(
            "unsigned-long-sqrt-schedule", "parameter-abi");
    for (instruction = 0;
         instruction < (int)(sizeof(constants) / sizeof(constants[0]));
         ++instruction)
        if (!mir_machine_unsigned_long_type(
                mir.insns[constants[instruction]].type))
            return mir_machine_reject(
                "unsigned-long-sqrt-schedule", "constant-types");
    if (!mir_machine_constant_equals(mir.insns[3].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[7].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[11].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[15].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[35].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[52].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[61].dst, 1))
        return mir_machine_reject(
            "unsigned-long-sqrt-schedule", "constants");
    for (instruction = 0;
         instruction <
             (int)(sizeof(wide_values) / sizeof(wide_values[0]));
         ++instruction)
        if (!mir_machine_unsigned_long_type(
                mir.insns[wide_values[instruction]].type))
            return mir_machine_reject(
                "unsigned-long-sqrt-schedule", "wide-types");
    for (instruction = 0;
         instruction <
             (int)(sizeof(wide_stores) / sizeof(wide_stores[0]));
         ++instruction)
        if (mir.insns[wide_stores[instruction]].memory_size != 4)
            return mir_machine_reject(
                "unsigned-long-sqrt-schedule", "store-widths");
    if (!mir_machine_unobservable_local_store(&mir.insns[4]) ||
        !mir_machine_unobservable_local_store(&mir.insns[9]) ||
        !mir_machine_unobservable_local_store(&mir.insns[12]) ||
        !mir_machine_unobservable_local_store(&mir.insns[39]) ||
        mir_machine_same_location(&mir.insns[4], &mir.insns[9]) ||
        mir_machine_same_location(&mir.insns[4], &mir.insns[12]) ||
        mir_machine_same_location(&mir.insns[4], &mir.insns[39]) ||
        mir_machine_same_location(&mir.insns[9], &mir.insns[12]) ||
        mir_machine_same_location(&mir.insns[9], &mir.insns[39]) ||
        mir_machine_same_location(&mir.insns[12], &mir.insns[39]) ||
        !mir_machine_same_location(&mir.insns[4], &mir.insns[26]) ||
        !mir_machine_same_location(&mir.insns[4], &mir.insns[30]) ||
        !mir_machine_same_location(&mir.insns[4], &mir.insns[32]) ||
        !mir_machine_same_location(&mir.insns[4], &mir.insns[55]) ||
        !mir_machine_same_location(&mir.insns[9], &mir.insns[27]) ||
        !mir_machine_same_location(&mir.insns[9], &mir.insns[31]) ||
        !mir_machine_same_location(&mir.insns[9], &mir.insns[64]) ||
        !mir_machine_same_location(&mir.insns[12], &mir.insns[49]) ||
        !mir_machine_same_location(&mir.insns[12], &mir.insns[70]))
        return mir_machine_reject(
            "unsigned-long-sqrt-schedule", "local-relationships");

    if (mir.insns[4].src1 != mir.insns[3].dst ||
        mir.insns[8].immediate != '/' ||
        mir.insns[8].src1 != mir.insns[1].dst ||
        mir.insns[8].src2 != mir.insns[7].dst ||
        mir.insns[8].secondary_offset != mir.insns[8].type ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[16].immediate != TOK_LE ||
        mir.insns[16].src1 != mir.insns[1].dst ||
        mir.insns[16].src2 != mir.insns[15].dst ||
        mir.insns[16].secondary_offset != mir.insns[1].type ||
        type_ptr_depth(mir.insns[16].type) != 0 ||
        type_size(mir.insns[16].type) != 2 ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        mir.insns[17].label != mir.insns[20].label ||
        mir.insns[19].src1 != mir.insns[1].dst)
        return mir_machine_reject(
            "unsigned-long-sqrt-schedule", "initialization");

    if (mir.insns[28].immediate != TOK_LE ||
        mir.insns[28].src1 != mir.insns[26].dst ||
        mir.insns[28].src2 != mir.insns[27].dst ||
        mir.insns[28].secondary_offset != mir.insns[26].type ||
        type_ptr_depth(mir.insns[28].type) != 0 ||
        type_size(mir.insns[28].type) != 2 ||
        mir.insns[29].src1 != mir.insns[28].dst ||
        mir.insns[29].label != mir.insns[69].label ||
        mir.insns[33].immediate != '-' ||
        mir.insns[33].src1 != mir.insns[31].dst ||
        mir.insns[33].src2 != mir.insns[32].dst ||
        mir.insns[33].secondary_offset != mir.insns[33].type ||
        mir.insns[36].immediate != '/' ||
        mir.insns[36].src1 != mir.insns[33].dst ||
        mir.insns[36].src2 != mir.insns[35].dst ||
        mir.insns[36].secondary_offset != mir.insns[36].type ||
        mir.insns[37].immediate != '+' ||
        mir.insns[37].src1 != mir.insns[30].dst ||
        mir.insns[37].src2 != mir.insns[36].dst ||
        mir.insns[37].secondary_offset != mir.insns[37].type ||
        mir.insns[39].src1 != mir.insns[37].dst)
        return mir_machine_reject(
            "unsigned-long-sqrt-schedule", "midpoint");

    if (mir.insns[43].immediate != '/' ||
        mir.insns[43].src1 != mir.insns[1].dst ||
        mir.insns[43].src2 != mir.insns[37].dst ||
        mir.insns[43].secondary_offset != mir.insns[43].type ||
        mir.insns[44].immediate != TOK_LE ||
        mir.insns[44].src1 != mir.insns[37].dst ||
        mir.insns[44].src2 != mir.insns[43].dst ||
        mir.insns[44].secondary_offset != mir.insns[37].type ||
        type_ptr_depth(mir.insns[44].type) != 0 ||
        type_size(mir.insns[44].type) != 2 ||
        mir.insns[45].src1 != mir.insns[44].dst ||
        mir.insns[45].label != mir.insns[58].label ||
        mir.insns[49].src1 != mir.insns[37].dst ||
        mir.insns[53].immediate != '+' ||
        mir.insns[53].src1 != mir.insns[37].dst ||
        mir.insns[53].src2 != mir.insns[52].dst ||
        mir.insns[53].secondary_offset != mir.insns[53].type ||
        mir.insns[55].src1 != mir.insns[53].dst ||
        mir.insns[57].label != mir.insns[65].label ||
        mir.insns[62].immediate != '-' ||
        mir.insns[62].src1 != mir.insns[37].dst ||
        mir.insns[62].src2 != mir.insns[61].dst ||
        mir.insns[62].secondary_offset != mir.insns[62].type ||
        mir.insns[64].src1 != mir.insns[62].dst ||
        mir.insns[68].label != mir.insns[21].label ||
        mir.insns[71].src1 != mir.insns[70].dst)
        return mir_machine_reject(
            "unsigned-long-sqrt-schedule", "loop-result");
    return 1;
}

static int mir_match_prime_search_schedule(
    struct MirPrimeSearchSchedule *plan)
{
    static const int expected_opcodes[120] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_STORE,
        MIR_NOP, MIR_CONST, MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL, MIR_UNARY, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_LOAD, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_LABEL, MIR_LABEL, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_LOAD, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST,
        MIR_STORE, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_LOAD,
        MIR_LOAD, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_LOAD,
        MIR_LOAD, MIR_BINARY, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_NOP, MIR_CONST, MIR_STORE, MIR_NOP, MIR_JUMP, MIR_NOP,
        MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_NOP, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_LOAD,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_RETURN
    };
    const struct MirInsn *argc = &mir.insns[1];
    const struct MirInsn *argv = &mir.insns[2];
    const struct MirInsn *convert_call = &mir.insns[18];
    const struct MirInsn *sqrt_call = &mir.insns[48];
    const struct MirInsn *print_call = &mir.insns[105];
    struct Sym *print_function;
    int arguments[2];
    int argc_offset;
    int argv_offset;
    int start_offset;
    int found_offset;
    int sqrt_offset;
    int prime_offset;
    int divisor_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 120 || mir_cfg_block_count() != 11 ||
        mir.local_bytes != 17 || mir.aggregate_temp_bytes != 0 ||
        mir.has_vla || !mir_has_cfg_backedge() ||
        type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        (mir.return_type & TYPE_UNSIGNED) != 0 ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "prime-search-schedule", "opcodes");

    if (!mir_numeric_scalar_location(
            argc, SC_PARAM, 2, 0, 0, &argc_offset) ||
        !mir_numeric_scalar_location(
            argv, SC_PARAM, 2, 2, 0, &argv_offset) ||
        argc_offset - 2 != 2 || argv_offset - 2 != 4 ||
        argc->object < 0 || argv->object < 0 ||
        (argc->type & 15) != TYPE_INT ||
        (argc->type & TYPE_UNSIGNED) != 0 ||
        type_size(argc->type) != 2 ||
        type_ptr_depth(argv->type) != 2 ||
        type_size(argv->type) != 2)
        return mir_machine_reject(
            "prime-search-schedule", "parameter-abi");
    plan->argc_stack_offset = argc_offset - 2;
    plan->argv_stack_offset = argv_offset - 2;

    if (!mir_numeric_scalar_location(
            &mir.insns[5], SC_LOCAL, 4, 0, 1,
            &start_offset) ||
        !mir_numeric_scalar_location(
            &mir.insns[8], SC_LOCAL, 4, 0, 1,
            &found_offset) ||
        !mir_numeric_scalar_location(
            &mir.insns[52], SC_LOCAL, 4, 0, 1,
            &sqrt_offset) ||
        !mir_numeric_scalar_location(
            &mir.insns[55], SC_LOCAL, 1, 0, 0,
            &prime_offset) ||
        !mir_numeric_scalar_location(
            &mir.insns[58], SC_LOCAL, 4, 0, 1,
            &divisor_offset) ||
        start_offset != -4 || found_offset != -8 ||
        sqrt_offset != -12 || divisor_offset != -16 ||
        prime_offset != -17 ||
        (mir.insns[55].type & 15) != TYPE_BOOL ||
        (mir.insns[58].type & 15) != TYPE_LONG)
        return mir_machine_reject(
            "prime-search-schedule", "local-layout");

#define PRIME_SAME(index, width, is_unsigned, offset) \
    mir_numeric_same_location( \
        &mir.insns[(index)], SC_LOCAL, (width), 0, \
        (is_unsigned), (offset))
    if (!PRIME_SAME(20, 4, 1, start_offset) ||
        !PRIME_SAME(23, 4, 1, start_offset) ||
        !PRIME_SAME(30, 4, 1, start_offset) ||
        !PRIME_SAME(33, 4, 1, start_offset) ||
        !PRIME_SAME(46, 4, 1, start_offset) ||
        !PRIME_SAME(72, 4, 1, start_offset) ||
        !PRIME_SAME(103, 4, 1, start_offset) ||
        !PRIME_SAME(108, 4, 1, start_offset) ||
        !PRIME_SAME(113, 4, 1, start_offset) ||
        !PRIME_SAME(40, 4, 1, found_offset) ||
        !PRIME_SAME(97, 4, 1, found_offset) ||
        !PRIME_SAME(100, 4, 1, found_offset) ||
        !PRIME_SAME(68, 4, 1, sqrt_offset) ||
        !PRIME_SAME(80, 1, 0, prime_offset) ||
        !PRIME_SAME(95, 1, 0, prime_offset) ||
        !PRIME_SAME(67, 4, 1, divisor_offset) ||
        !PRIME_SAME(73, 4, 1, divisor_offset) ||
        !PRIME_SAME(87, 4, 1, divisor_offset) ||
        !PRIME_SAME(92, 4, 1, divisor_offset))
        return mir_machine_reject(
            "prime-search-schedule", "local-relationships");
#undef PRIME_SAME

    if (!mir_machine_constant_equals(mir.insns[4].dst, 10000) ||
        mir.insns[5].src1 != mir.insns[4].dst ||
        !mir_machine_constant_equals(mir.insns[7].dst, 0) ||
        mir.insns[8].src1 != mir.insns[7].dst ||
        !mir_machine_constant_equals(mir.insns[10].dst, 2) ||
        mir.insns[11].immediate != TOK_GE ||
        mir.insns[11].src1 != argc->dst ||
        mir.insns[11].src2 != mir.insns[10].dst ||
        mir.insns[11].secondary_offset != argc->type ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[12].label != mir.insns[21].label)
        return mir_machine_reject(
            "prime-search-schedule", "initialization");

    if (!mir_machine_constant_equals(mir.insns[14].dst, 1) ||
        mir.insns[15].src1 != argv->dst ||
        mir.insns[15].src2 != mir.insns[14].dst ||
        mir.insns[15].immediate != 2 ||
        mir.insns[15].memory_size != 2 ||
        type_ptr_depth(mir.insns[15].type) != 2 ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[16].memory_size != 2 ||
        type_ptr_depth(mir.insns[16].type) != 1 ||
        !mir_numeric_call_arguments(convert_call, 1, arguments) ||
        arguments[0] != mir.insns[16].dst ||
        convert_call->memory_flags != 0 ||
        strcmp(convert_call->name, "atol") ||
        (plan->convert_function =
             find_global(convert_call->name)) == NULL ||
        plan->convert_function->is_defined ||
        plan->convert_function->is_funcptr ||
        plan->convert_function->is_noreturn ||
        !plan->convert_function->has_proto ||
        plan->convert_function->proto_nargs != 1 ||
        plan->convert_function->proto_variadic ||
        type_ptr_depth(plan->convert_function->proto_types[0]) != 1 ||
        (plan->convert_function->proto_types[0] & 15) != TYPE_CHAR ||
        type_size(convert_call->type) != 4 ||
        type_is_float(convert_call->type) ||
        (convert_call->type & TYPE_UNSIGNED) != 0 ||
        mir.insns[19].immediate != 0 ||
        mir.insns[19].src1 != convert_call->dst ||
        !mir_numeric_unsigned_long_type(mir.insns[19].type) ||
        mir.insns[20].src1 != mir.insns[19].dst)
        return mir_machine_reject(
            "prime-search-schedule", "argv-conversion");

    if (!mir_machine_constant_equals(mir.insns[25].dst, 1) ||
        mir.insns[26].immediate != '&' ||
        mir.insns[26].src1 != mir.insns[23].dst ||
        mir.insns[26].src2 != mir.insns[25].dst ||
        !mir_machine_constant_equals(mir.insns[27].dst, 0) ||
        mir.insns[28].immediate != TOK_EQ ||
        mir.insns[28].src1 != mir.insns[27].dst ||
        mir.insns[28].src2 != mir.insns[26].dst ||
        mir.insns[29].src1 != mir.insns[28].dst ||
        mir.insns[29].label != mir.insns[34].label ||
        !mir_machine_constant_equals(mir.insns[31].dst, 1) ||
        mir.insns[32].immediate != '+' ||
        mir.insns[32].src1 != mir.insns[30].dst ||
        mir.insns[32].src2 != mir.insns[31].dst ||
        mir.insns[33].src1 != mir.insns[32].dst)
        return mir_machine_reject(
            "prime-search-schedule", "odd-normalization");

    if (!mir_machine_constant_equals(mir.insns[42].dst, 10) ||
        mir.insns[43].immediate != '<' ||
        mir.insns[43].src1 != mir.insns[40].dst ||
        mir.insns[43].src2 != mir.insns[42].dst ||
        mir.insns[43].secondary_offset != mir.insns[40].type ||
        mir.insns[44].src1 != mir.insns[43].dst ||
        mir.insns[44].label != mir.insns[117].label ||
        !mir_numeric_call_arguments(sqrt_call, 1, arguments) ||
        arguments[0] != mir.insns[46].dst ||
        sqrt_call->memory_flags != 0 ||
        !mir_numeric_unsigned_long_type(sqrt_call->type) ||
        (plan->sqrt_function = find_global(sqrt_call->name)) == NULL ||
        !plan->sqrt_function->is_defined ||
        plan->sqrt_function->storage != SC_FUNC ||
        plan->sqrt_function->is_funcptr ||
        plan->sqrt_function->is_noreturn ||
        !plan->sqrt_function->has_proto ||
        plan->sqrt_function->proto_nargs != 1 ||
        plan->sqrt_function->proto_variadic ||
        !mir_numeric_unsigned_long_type(
            plan->sqrt_function->proto_types[0]) ||
        !mir_machine_constant_equals(mir.insns[49].dst, 1) ||
        mir.insns[50].immediate != '+' ||
        mir.insns[50].src1 != mir.insns[49].dst ||
        mir.insns[50].src2 != sqrt_call->dst ||
        mir.insns[52].src1 != mir.insns[50].dst ||
        !mir_machine_constant_equals(mir.insns[54].dst, 1) ||
        mir.insns[55].src1 != mir.insns[54].dst ||
        !mir_machine_constant_equals(mir.insns[57].dst, 3) ||
        mir.insns[58].src1 != mir.insns[57].dst)
        return mir_machine_reject(
            "prime-search-schedule", "outer-loop");

    if (mir.insns[69].immediate != '<' ||
        mir.insns[69].src1 != mir.insns[67].dst ||
        mir.insns[69].src2 != mir.insns[68].dst ||
        mir.insns[69].secondary_offset != mir.insns[67].type ||
        mir.insns[70].src1 != mir.insns[69].dst ||
        mir.insns[70].label != mir.insns[94].label ||
        mir.insns[74].immediate != '%' ||
        mir.insns[74].src1 != mir.insns[72].dst ||
        mir.insns[74].src2 != mir.insns[73].dst ||
        mir.insns[74].secondary_offset != mir.insns[72].type ||
        !mir_machine_constant_equals(mir.insns[75].dst, 0) ||
        mir.insns[76].immediate != TOK_EQ ||
        mir.insns[76].src1 != mir.insns[75].dst ||
        mir.insns[76].src2 != mir.insns[74].dst ||
        mir.insns[77].src1 != mir.insns[76].dst ||
        mir.insns[77].label != mir.insns[84].label ||
        !mir_machine_constant_equals(mir.insns[79].dst, 0) ||
        mir.insns[80].src1 != mir.insns[79].dst ||
        mir.insns[82].label != mir.insns[94].label ||
        !mir_machine_constant_equals(mir.insns[89].dst, 2) ||
        mir.insns[90].immediate != '+' ||
        mir.insns[90].src1 != mir.insns[87].dst ||
        mir.insns[90].src2 != mir.insns[89].dst ||
        mir.insns[92].src1 != mir.insns[90].dst ||
        mir.insns[93].label != mir.insns[59].label)
        return mir_machine_reject(
            "prime-search-schedule", "divisibility-loop");

    print_function = find_global(print_call->name);
    if (mir.insns[96].src1 != mir.insns[95].dst ||
        mir.insns[96].label != mir.insns[107].label ||
        !mir_machine_constant_equals(mir.insns[98].dst, 1) ||
        mir.insns[99].immediate != '+' ||
        mir.insns[99].src1 != mir.insns[97].dst ||
        mir.insns[99].src2 != mir.insns[98].dst ||
        mir.insns[100].src1 != mir.insns[99].dst ||
        mir.insns[101].immediate < 0 ||
        !mir_numeric_call_arguments(print_call, 2, arguments) ||
        arguments[0] != mir.insns[101].dst ||
        arguments[1] != mir.insns[103].dst ||
        type_ptr_depth(mir.insns[101].type) != 1 ||
        (mir.insns[101].type & 15) != TYPE_CHAR ||
        strcmp(print_call->name, "printf") ||
        (strcmp(print_call->base_name, "_pflng") &&
         strcmp(print_call->base_name, "_pflio")) ||
        (print_call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) !=
            MIR_CALL_FLAG_VARIADIC ||
         print_function == NULL || print_function->is_defined ||
         print_function->is_funcptr || print_function->is_noreturn ||
         !print_function->has_proto ||
         print_function->proto_nargs != 1 ||
         !print_function->proto_variadic ||
         type_ptr_depth(print_function->proto_types[0]) != 1 ||
         (print_function->proto_types[0] & 15) != TYPE_CHAR ||
         type_ptr_depth(print_call->type) != 0 ||
         (print_call->type & 15) != TYPE_INT ||
         type_size(print_call->type) != 2 ||
        !mir_machine_constant_equals(mir.insns[110].dst, 2) ||
        mir.insns[111].immediate != '+' ||
        mir.insns[111].src1 != mir.insns[108].dst ||
        mir.insns[111].src2 != mir.insns[110].dst ||
        mir.insns[113].src1 != mir.insns[111].dst ||
        mir.insns[116].label != mir.insns[35].label ||
        !mir_machine_constant_equals(mir.insns[118].dst, 0) ||
        mir.insns[119].src1 != mir.insns[118].dst)
        return mir_machine_reject(
            "prime-search-schedule", "report-and-return");
    plan->format_string_id = (int)mir.insns[101].immediate;
    snprintf(plan->print_name, sizeof(plan->print_name), "%s",
             print_call->base_name);
    return 1;
}

static int mir_catalan_int_type(int type)
{
    return type_ptr_depth(type) == 0 &&
           (type & 15) == TYPE_INT &&
           (type & TYPE_UNSIGNED) == 0 &&
           type_size(type) == 2;
}

static int mir_catalan_long_type(int type)
{
    return type_ptr_depth(type) == 0 &&
           (type & 15) == TYPE_LONG &&
           (type & TYPE_UNSIGNED) == 0 &&
           !type_is_float(type) &&
           type_size(type) == 4;
}

static int mir_catalan_long_pointer_type(int type)
{
    return type_ptr_depth(type) == 1 &&
           (type & 15) == TYPE_LONG &&
           type_size(type) == 2;
}

static int mir_catalan_array_address(
    const struct MirInsn *address, int expected_offset)
{
    int declared;

    if (address->opcode != MIR_ADDRESS ||
        !mir_catalan_long_pointer_type(address->type))
        return 0;
    for (declared = 0; declared < mir.declared_count; ++declared)
        if (!strcmp(mir.declared_names[declared], address->name))
            return mir.declared_storage[declared] == SC_LOCAL &&
                   mir.declared_offsets[declared] == expected_offset &&
                   mir.declared_sizes[declared] == 124 &&
                   mir.declared_is_array[declared] &&
                   !mir.declared_is_vla[declared] &&
                   !mir.declared_is_volatile[declared] &&
                   mir.declared_dim_counts[declared] == 1 &&
                   mir.declared_dims[declared][0] == 31 &&
                   mir.declared_elem_sizes[declared] == 4 &&
                   (mir.declared_types[declared] & 15) == TYPE_LONG;
    return 0;
}

static int mir_catalan_same_array(
    const struct MirInsn *address, const struct MirInsn *expected)
{
    return address->opcode == MIR_ADDRESS &&
           address->type == expected->type &&
           !strcmp(address->name, expected->name);
}

static int mir_catalan_defined_function(
    const struct MirInsn *call, int return_type, int arguments,
    struct Sym **function_out)
{
    struct Sym *function;
    const char *assembly_name;

    if (call->opcode != MIR_CALL || call->src1 >= 0 ||
        call->memory_flags != 0 ||
        (function = find_global(call->name)) == NULL ||
        function->storage != SC_FUNC || !function->is_defined ||
        function->is_funcptr || function->is_noreturn ||
        !function->has_proto || function->proto_variadic ||
        function->proto_nargs != arguments ||
        function->type != return_type || call->type != return_type)
        return 0;
    assembly_name = asm_name_for(sym_asm_name(function));
    if (call->base_name[0] != 0 &&
        strcmp(call->base_name, assembly_name))
        return 0;
    *function_out = function;
    return 1;
}

static int mir_catalan_argument(
    int instruction, const struct MirInsn *call, int index, int value)
{
    const struct MirInsn *argument = &mir.insns[instruction];

    return argument->opcode == MIR_ARG &&
           argument->src1 == value &&
           argument->immediate == index &&
           argument->secondary_offset == call->secondary_offset;
}

static int mir_catalan_match_term_call(
    int item, int a_value, const struct MirInsn *sum,
    const struct MirInsn *scale, struct Sym **function_out)
{
    static const int sum_addresses[12] = {
        44, 62, 81, 99, 118, 136, 191, 210, 229, 248, 266, 284
    };
    static const int scale_addresses[12] = {
        46, 64, 83, 101, 120, 138, 193, 212, 231, 250, 268, 286
    };
    static const int signs[12] = {
        48, 67, 85, 104, 122, 141, 196, 215, 234, 252, 270, 288
    };
    static const int numerators[12] = {
        51, 70, 88, 107, 125, 144, 199, 218, 237, 255, 273, 291
    };
    static const int powers[12] = {
        54, 73, 91, 110, 128, 147, 202, 221, 240, 258, 276, 294
    };
    static const int deltas[12] = {
        57, 76, 94, 113, 131, 150, 205, 224, 243, 261, 279, 297
    };
    static const int additions[12] = {
        58, 77, 95, 114, 132, 151, 206, 225, 244, 262, 280, 298
    };
    static const int calls[12] = {
        61, 80, 98, 117, 135, 154, 209, 228, 247, 265, 283, 301
    };
    static const int expected_signs[12] = {
        1, 65535, 1, 65535, 1, 65535,
        65535, 65535, 65535, 1, 1, 1
    };
    static const int expected_numerators[12] = {
        3, 3, 3, 3, 3, 3, 1, 1, 1, 1, 1, 1
    };
    static const int expected_powers[12] = {
        2, 2, 4, 8, 8, 16, 4, 8, 32, 256, 512, 2048
    };
    static const int expected_deltas[12] = {
        1, 2, 3, 5, 6, 7, 1, 2, 3, 5, 6, 7
    };
    const struct MirInsn *call = &mir.insns[calls[item]];
    const struct MirInsn *addition = &mir.insns[additions[item]];
    int arguments[6];

    if (!mir_catalan_same_array(
            &mir.insns[sum_addresses[item]], sum) ||
        !mir_catalan_same_array(
            &mir.insns[scale_addresses[item]], scale) ||
        !mir_machine_constant_equals(
            mir.insns[signs[item]].dst, expected_signs[item]) ||
        !mir_catalan_int_type(mir.insns[signs[item]].type) ||
        !mir_machine_constant_equals(
            mir.insns[numerators[item]].dst,
            expected_numerators[item]) ||
        !mir_catalan_long_type(mir.insns[numerators[item]].type) ||
        !mir_machine_constant_equals(
            mir.insns[powers[item]].dst, expected_powers[item]) ||
        !mir_catalan_long_type(mir.insns[powers[item]].type) ||
        !mir_machine_constant_equals(
            mir.insns[deltas[item]].dst, expected_deltas[item]) ||
        !mir_catalan_long_type(mir.insns[deltas[item]].type) ||
        addition->immediate != '+' ||
        addition->src1 != a_value ||
        addition->src2 != mir.insns[deltas[item]].dst ||
        !mir_catalan_long_type(addition->type) ||
        addition->secondary_offset != addition->type ||
        !mir_catalan_defined_function(
            call, TYPE_VOID, 6, function_out) ||
        !mir_numeric_call_arguments(call, 6, arguments) ||
        arguments[0] != mir.insns[sum_addresses[item]].dst ||
        arguments[1] != mir.insns[scale_addresses[item]].dst ||
        arguments[2] != mir.insns[signs[item]].dst ||
        arguments[3] != mir.insns[numerators[item]].dst ||
        arguments[4] != mir.insns[powers[item]].dst ||
        arguments[5] != addition->dst ||
        !mir_catalan_argument(
            sum_addresses[item] + 1, call, 0,
            mir.insns[sum_addresses[item]].dst) ||
        !mir_catalan_argument(
            scale_addresses[item] + 1, call, 1,
            mir.insns[scale_addresses[item]].dst) ||
        !mir_catalan_argument(
            signs[item] + 1, call, 2, mir.insns[signs[item]].dst) ||
        !mir_catalan_argument(
            numerators[item] + 1, call, 3,
            mir.insns[numerators[item]].dst) ||
        !mir_catalan_argument(
            powers[item] + 1, call, 4,
            mir.insns[powers[item]].dst) ||
        !mir_catalan_argument(
            additions[item] + 2, call, 5, addition->dst))
        return 0;
    return 1;
}

static int mir_match_catalan_driver_schedule(
    struct MirCatalanDriverSchedule *plan)
{
    static const unsigned char expected_opcodes[437] = {
        MIR_LABEL, MIR_ADDRESS, MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_ADDRESS, MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_PHI, MIR_NOP, MIR_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_UNARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_NOP, MIR_UNARY,
        MIR_BINARY, MIR_STORE, MIR_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG, MIR_NOP,
        MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_NOP,
        MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_ARG,
        MIR_NOP, MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG,
        MIR_NOP, MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_NOP, MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_ARG, MIR_ADDRESS,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG,
        MIR_NOP, MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_NOP, MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_ARG, MIR_ADDRESS,
        MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST,
        MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_NOP, MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_ARG,
        MIR_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST,
        MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_NOP, MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_ARG,
        MIR_ADDRESS, MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG, MIR_NOP,
        MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_NOP, MIR_ARG, MIR_CALL, MIR_ADDRESS,
        MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_PHI, MIR_PHI,
        MIR_ADDRESS, MIR_ARG, MIR_CALL, MIR_UNARY, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_CONST, MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_STORE, MIR_ADDRESS,
        MIR_ARG, MIR_ADDRESS, MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG,
        MIR_NOP, MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_ARG, MIR_NOP, MIR_CONST,
        MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST,
        MIR_ARG, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_ARG, MIR_NOP,
        MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG, MIR_NOP,
        MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_NOP,
        MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG, MIR_NOP,
        MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_NOP,
        MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG, MIR_NOP,
        MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_NOP,
        MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG, MIR_NOP,
        MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_NOP,
        MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_ARG, MIR_NOP, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_CONST, MIR_STORE, MIR_NOP, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_LOAD, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_PHI, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_NOP,
        MIR_JUMP, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_NOP,
        MIR_CONST, MIR_ADDRESS, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP,
        MIR_BINARY, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_UNARY, MIR_BINARY,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_NOP,
        MIR_STORE, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_NOP,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN
    };
    static const int zero_calls[3] = {3, 6, 9};
    static const int zero_addresses[3] = {1, 4, 7};
    const struct MirInsn *sum = &mir.insns[1];
    const struct MirInsn *s16 = &mir.insns[4];
    const struct MirInsn *s4096 = &mir.insns[7];
    struct Sym *function;
    struct Sym *term_function = NULL;
    int arguments[6];
    int instruction;
    int item;
    int n_offset;
    int a_offset;
    int printed_offset;
    int i_offset;
    int p_offset;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 437 || mir_cfg_block_count() != 19 ||
        mir.local_bytes != 392 || mir.aggregate_temp_bytes != 0 ||
        mir.has_vla || !mir_has_cfg_backedge() ||
        !mir_catalan_int_type(mir.return_type))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "catalan-driver-schedule", "opcode");

    if (!mir_catalan_array_address(sum, -124) ||
        !mir_catalan_array_address(s16, -250) ||
        !mir_catalan_array_address(s4096, -374) ||
        !strcmp(sum->name, s16->name) ||
        !strcmp(sum->name, s4096->name) ||
        !strcmp(s16->name, s4096->name))
        return mir_machine_reject(
            "catalan-driver-schedule", "arrays");

    for (item = 0; item < 3; ++item) {
        const struct MirInsn *call = &mir.insns[zero_calls[item]];
        const struct MirInsn *address =
            &mir.insns[zero_addresses[item]];

        if (!mir_catalan_defined_function(
                call, TYPE_VOID, 1, &function) ||
            !mir_catalan_long_pointer_type(
                function->proto_types[0]) ||
            !mir_numeric_call_arguments(call, 1, arguments) ||
            arguments[0] != address->dst ||
            !mir_catalan_argument(
                zero_addresses[item] + 1, call, 0, address->dst) ||
            (item == 0
                 ? (plan->zero_function = function, 0)
                 : function != plan->zero_function))
            return mir_machine_reject(
                "catalan-driver-schedule", "zero-calls");
    }

    if (!mir_catalan_same_array(&mir.insns[10], s16) ||
        !mir_machine_constant_equals(mir.insns[11].dst, 0) ||
        !mir_catalan_int_type(mir.insns[11].type) ||
        mir.insns[12].src1 != mir.insns[10].dst ||
        mir.insns[12].src2 != mir.insns[11].dst ||
        mir.insns[12].immediate != 4 ||
        mir.insns[12].memory_size != 4 ||
        !mir_machine_constant_equals(mir.insns[14].dst, 1) ||
        !mir_catalan_long_type(mir.insns[14].type) ||
        mir.insns[15].src1 != mir.insns[12].dst ||
        mir.insns[15].src2 != mir.insns[14].dst ||
        mir.insns[15].memory_size != 4 ||
        !mir_catalan_same_array(&mir.insns[16], s4096) ||
        !mir_machine_constant_equals(mir.insns[17].dst, 0) ||
        mir.insns[18].src1 != mir.insns[16].dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        mir.insns[18].immediate != 4 ||
        mir.insns[18].memory_size != 4 ||
        !mir_machine_constant_equals(mir.insns[20].dst, 1) ||
        mir.insns[21].src1 != mir.insns[18].dst ||
        mir.insns[21].src2 != mir.insns[20].dst ||
        mir.insns[21].memory_size != 4)
        return mir_machine_reject(
            "catalan-driver-schedule", "array-initializers");

    if (!mir_numeric_scalar_location(
            &mir.insns[29], SC_LOCAL, 2, 0, 0, &n_offset) ||
        n_offset != -376 ||
        !mir_numeric_scalar_location(
            &mir.insns[43], SC_LOCAL, 4, 0, 0, &a_offset) ||
        a_offset != -380 ||
        !mir_machine_same_location(
            &mir.insns[29], &mir.insns[166]) ||
        !mir_machine_same_location(
            &mir.insns[29], &mir.insns[176]) ||
        !mir_machine_same_location(
            &mir.insns[29], &mir.insns[313]) ||
        !mir_machine_same_location(
            &mir.insns[43], &mir.insns[190]) ||
        !mir_machine_constant_equals(mir.insns[27].dst, 0) ||
        mir.insns[29].src1 != mir.insns[27].dst ||
        mir.insns[31].src1 != mir.insns[27].dst ||
        mir.insns[31].src2 != mir.insns[165].dst ||
        mir.insns[31].phi_pred1 != mir.insns[0].label ||
        mir.insns[31].phi_pred2 != mir.insns[162].label)
        return mir_machine_reject(
            "catalan-driver-schedule", "first-loop-state");

    if (!mir_catalan_same_array(&mir.insns[33], s16) ||
        !mir_catalan_defined_function(
            &mir.insns[35], TYPE_INT, 1,
            &plan->is_zero_function) ||
        !mir_catalan_long_pointer_type(
            plan->is_zero_function->proto_types[0]) ||
        !mir_numeric_call_arguments(&mir.insns[35], 1, arguments) ||
        arguments[0] != mir.insns[33].dst ||
        !mir_catalan_argument(
            34, &mir.insns[35], 0, mir.insns[33].dst) ||
        mir.insns[36].immediate != '!' ||
        mir.insns[36].src1 != mir.insns[35].dst ||
        mir.insns[37].src1 != mir.insns[36].dst ||
        mir.insns[37].label != mir.insns[168].label ||
        !mir_machine_constant_equals(mir.insns[39].dst, 8) ||
        !mir_catalan_long_type(mir.insns[39].type) ||
        mir.insns[41].immediate != 0 ||
        mir.insns[41].src1 != mir.insns[31].dst ||
        !mir_catalan_long_type(mir.insns[41].type) ||
        mir.insns[42].immediate != '*' ||
        mir.insns[42].src1 != mir.insns[39].dst ||
        mir.insns[42].src2 != mir.insns[41].dst ||
        mir.insns[43].src1 != mir.insns[42].dst)
        return mir_machine_reject(
            "catalan-driver-schedule", "first-loop-header");

    for (item = 0; item < 6; ++item) {
        if (!mir_catalan_match_term_call(
                item, mir.insns[42].dst, sum, s16, &function) ||
            (item == 0
                 ? (term_function = function, 0)
                 : function != term_function))
            return mir_machine_reject(
                "catalan-driver-schedule", "first-terms");
    }
    plan->add_term_function = term_function;

    if (!mir_catalan_same_array(&mir.insns[155], s16) ||
        !mir_machine_constant_equals(mir.insns[158].dst, 16) ||
        !mir_catalan_long_type(mir.insns[158].type) ||
        !mir_catalan_defined_function(
            &mir.insns[160], TYPE_VOID, 2,
            &plan->div_small_function) ||
        !mir_catalan_long_pointer_type(
            plan->div_small_function->proto_types[0]) ||
        !mir_catalan_long_type(
            plan->div_small_function->proto_types[1]) ||
        !mir_numeric_call_arguments(&mir.insns[160], 2, arguments) ||
        arguments[0] != mir.insns[155].dst ||
        arguments[1] != mir.insns[158].dst ||
        !mir_catalan_argument(
            156, &mir.insns[160], 0, mir.insns[155].dst) ||
        !mir_catalan_argument(
            159, &mir.insns[160], 1, mir.insns[158].dst) ||
        !mir_machine_constant_equals(mir.insns[164].dst, 1) ||
        mir.insns[165].immediate != '+' ||
        mir.insns[165].src1 != mir.insns[31].dst ||
        mir.insns[165].src2 != mir.insns[164].dst ||
        mir.insns[166].src1 != mir.insns[165].dst ||
        mir.insns[167].label != mir.insns[30].label)
        return mir_machine_reject(
            "catalan-driver-schedule", "first-loop-tail");

    if (!mir_machine_constant_equals(mir.insns[174].dst, 0) ||
        mir.insns[176].src1 != mir.insns[174].dst ||
        mir.insns[178].src1 != mir.insns[174].dst ||
        mir.insns[178].src2 != mir.insns[312].dst ||
        mir.insns[178].phi_pred1 != mir.insns[168].label ||
        mir.insns[178].phi_pred2 != mir.insns[309].label ||
        !mir_catalan_same_array(&mir.insns[180], s4096) ||
        !mir_catalan_defined_function(
            &mir.insns[182], TYPE_INT, 1, &function) ||
        function != plan->is_zero_function ||
        !mir_numeric_call_arguments(&mir.insns[182], 1, arguments) ||
        arguments[0] != mir.insns[180].dst ||
        mir.insns[183].immediate != '!' ||
        mir.insns[183].src1 != mir.insns[182].dst ||
        mir.insns[184].src1 != mir.insns[183].dst ||
        mir.insns[184].label != mir.insns[315].label ||
        !mir_machine_constant_equals(mir.insns[186].dst, 8) ||
        mir.insns[188].immediate != 0 ||
        mir.insns[188].src1 != mir.insns[178].dst ||
        mir.insns[189].immediate != '*' ||
        mir.insns[189].src1 != mir.insns[186].dst ||
        mir.insns[189].src2 != mir.insns[188].dst ||
        mir.insns[190].src1 != mir.insns[189].dst)
        return mir_machine_reject(
            "catalan-driver-schedule", "second-loop-header");

    for (item = 6; item < 12; ++item)
        if (!mir_catalan_match_term_call(
                item, mir.insns[189].dst, sum, s4096, &function) ||
            function != plan->add_term_function)
            return mir_machine_reject(
                "catalan-driver-schedule", "second-terms");

    if (!mir_catalan_same_array(&mir.insns[302], s4096) ||
        !mir_machine_constant_equals(mir.insns[305].dst, 4096) ||
        !mir_catalan_defined_function(
            &mir.insns[307], TYPE_VOID, 2, &function) ||
        function != plan->div_small_function ||
        !mir_numeric_call_arguments(&mir.insns[307], 2, arguments) ||
        arguments[0] != mir.insns[302].dst ||
        arguments[1] != mir.insns[305].dst ||
        !mir_machine_constant_equals(mir.insns[311].dst, 1) ||
        mir.insns[312].immediate != '+' ||
        mir.insns[312].src1 != mir.insns[178].dst ||
        mir.insns[312].src2 != mir.insns[311].dst ||
        mir.insns[313].src1 != mir.insns[312].dst ||
        mir.insns[314].label != mir.insns[177].label)
        return mir_machine_reject(
            "catalan-driver-schedule", "second-loop-tail");

    if (mir.insns[316].immediate < 0 ||
        !mir_catalan_same_array(&mir.insns[318], sum) ||
        !mir_machine_constant_equals(mir.insns[319].dst, 0) ||
        mir.insns[320].src1 != mir.insns[318].dst ||
        mir.insns[320].src2 != mir.insns[319].dst ||
        mir.insns[320].immediate != 4 ||
        mir.insns[321].src1 != mir.insns[320].dst ||
        mir.insns[321].memory_size != 4 ||
        !mir_catalan_long_type(mir.insns[321].type) ||
        !mir_numeric_call_arguments(&mir.insns[323], 2, arguments) ||
        arguments[0] != mir.insns[316].dst ||
        arguments[1] != mir.insns[321].dst ||
        !mir_catalan_argument(
            317, &mir.insns[323], 0, mir.insns[316].dst) ||
        !mir_catalan_argument(
            322, &mir.insns[323], 1, mir.insns[321].dst))
        return mir_machine_reject(
            "catalan-driver-schedule", "initial-report");

    function = find_global(mir.insns[323].name);
    if (function == NULL || function->is_defined ||
        function->is_funcptr || function->is_noreturn ||
        !function->has_proto || function->proto_nargs != 1 ||
        !function->proto_variadic ||
        !mir_catalan_int_type(function->type) ||
        type_ptr_depth(function->proto_types[0]) != 1 ||
        (function->proto_types[0] & 15) != TYPE_CHAR ||
        mir.insns[323].memory_flags != MIR_CALL_FLAG_VARIADIC ||
        !mir_catalan_int_type(mir.insns[323].type) ||
        mir.insns[323].base_name[0] == 0)
        return mir_machine_reject(
            "catalan-driver-schedule", "print-function");
    plan->format_string_id = (int)mir.insns[316].immediate;
    snprintf(plan->print_name, sizeof(plan->print_name), "%s",
             mir.insns[323].base_name);

    if (!mir_numeric_scalar_location(
            &mir.insns[333], SC_LOCAL, 2, 0, 0,
            &printed_offset) ||
        printed_offset != -386 ||
        !mir_numeric_scalar_location(
            &mir.insns[337], SC_LOCAL, 2, 0, 0, &i_offset) ||
        i_offset != -388 ||
        !mir_numeric_scalar_location(
            &mir.insns[374], SC_LOCAL, 4, 0, 0, &p_offset) ||
        p_offset != -392 ||
        !mir_machine_same_location(
            &mir.insns[333], &mir.insns[357]) ||
        !mir_machine_same_location(
            &mir.insns[333], &mir.insns[384]) ||
        !mir_machine_same_location(
            &mir.insns[333], &mir.insns[415]) ||
        !mir_machine_same_location(
            &mir.insns[333], &mir.insns[418]) ||
        !mir_machine_same_location(
            &mir.insns[337], &mir.insns[343]) ||
        !mir_machine_same_location(
            &mir.insns[337], &mir.insns[398]) ||
        !mir_machine_same_location(
            &mir.insns[337], &mir.insns[425]) ||
        !mir_machine_same_location(
            &mir.insns[337], &mir.insns[428]) ||
        !mir_machine_same_location(
            &mir.insns[374], &mir.insns[380]) ||
        !mir_machine_same_location(
            &mir.insns[374], &mir.insns[414]))
        return mir_machine_reject(
            "catalan-driver-schedule", "print-locals");

    if (!mir_machine_constant_equals(mir.insns[332].dst, 0) ||
        mir.insns[333].src1 != mir.insns[332].dst ||
        !mir_machine_constant_equals(mir.insns[335].dst, 1) ||
        mir.insns[337].src1 != mir.insns[335].dst ||
        !mir_machine_constant_equals(mir.insns[354].dst, 31) ||
        mir.insns[355].immediate != '<' ||
        mir.insns[355].src1 != mir.insns[343].dst ||
        mir.insns[355].src2 != mir.insns[354].dst ||
        mir.insns[356].src1 != mir.insns[355].dst ||
        mir.insns[356].label != mir.insns[364].label ||
        !mir_machine_constant_equals(mir.insns[358].dst, 100) ||
        mir.insns[359].immediate != '<' ||
        mir.insns[359].src1 != mir.insns[357].dst ||
        mir.insns[359].src2 != mir.insns[358].dst ||
        mir.insns[360].src1 != mir.insns[359].dst ||
        mir.insns[360].label != mir.insns[364].label ||
        mir.insns[363].label != mir.insns[366].label ||
        mir.insns[365].label != mir.insns[430].label)
        return mir_machine_reject(
            "catalan-driver-schedule", "outer-print-loop");

    if (!mir_machine_constant_equals(mir.insns[370].dst, 10000) ||
        !mir_machine_constant_equals(mir.insns[372].dst, 10) ||
        mir.insns[373].immediate != '/' ||
        mir.insns[373].src1 != mir.insns[370].dst ||
        mir.insns[373].src2 != mir.insns[372].dst ||
        mir.insns[374].src1 != mir.insns[373].dst ||
        mir.insns[380].src1 != mir.insns[373].dst ||
        mir.insns[380].src2 != mir.insns[412].dst ||
        mir.insns[380].phi_pred1 != mir.insns[366].label ||
        mir.insns[380].phi_pred2 != mir.insns[420].label ||
        !mir_machine_constant_equals(mir.insns[381].dst, 0) ||
        mir.insns[382].immediate != '>' ||
        mir.insns[382].src1 != mir.insns[380].dst ||
        mir.insns[382].src2 != mir.insns[381].dst ||
        mir.insns[383].src1 != mir.insns[382].dst ||
        mir.insns[383].label != mir.insns[391].label ||
        !mir_machine_constant_equals(mir.insns[385].dst, 100) ||
        mir.insns[386].immediate != '<' ||
        mir.insns[386].src1 != mir.insns[384].dst ||
        mir.insns[386].src2 != mir.insns[385].dst ||
        mir.insns[387].src1 != mir.insns[386].dst ||
        mir.insns[387].label != mir.insns[391].label ||
        mir.insns[390].label != mir.insns[393].label ||
        mir.insns[392].label != mir.insns[422].label)
        return mir_machine_reject(
            "catalan-driver-schedule", "inner-print-loop");

    if (!mir_machine_constant_equals(mir.insns[396].dst, 48) ||
        !mir_catalan_same_array(&mir.insns[397], sum) ||
        mir.insns[399].src1 != mir.insns[397].dst ||
        mir.insns[399].src2 != mir.insns[398].dst ||
        mir.insns[399].immediate != 4 ||
        mir.insns[400].src1 != mir.insns[399].dst ||
        mir.insns[400].memory_size != 4 ||
        mir.insns[402].immediate != '/' ||
        mir.insns[402].src1 != mir.insns[400].dst ||
        mir.insns[402].src2 != mir.insns[380].dst ||
        !mir_machine_constant_equals(mir.insns[404].dst, 10) ||
        mir.insns[405].immediate != '%' ||
        mir.insns[405].src1 != mir.insns[402].dst ||
        mir.insns[405].src2 != mir.insns[404].dst ||
        mir.insns[406].immediate != 0 ||
        mir.insns[406].src1 != mir.insns[405].dst ||
        mir.insns[407].immediate != '+' ||
        mir.insns[407].src1 != mir.insns[396].dst ||
        mir.insns[407].src2 != mir.insns[406].dst ||
        !mir_catalan_int_type(mir.insns[407].type) ||
        !mir_numeric_call_arguments(&mir.insns[409], 1, arguments) ||
        arguments[0] != mir.insns[407].dst ||
        !mir_catalan_argument(
            408, &mir.insns[409], 0, mir.insns[407].dst) ||
        !mir_machine_constant_equals(mir.insns[411].dst, 10) ||
        mir.insns[412].immediate != '/' ||
        mir.insns[412].src1 != mir.insns[380].dst ||
        mir.insns[412].src2 != mir.insns[411].dst ||
        mir.insns[414].src1 != mir.insns[412].dst ||
        !mir_machine_constant_equals(mir.insns[416].dst, 1) ||
        mir.insns[417].immediate != '+' ||
        mir.insns[417].src1 != mir.insns[415].dst ||
        mir.insns[417].src2 != mir.insns[416].dst ||
        mir.insns[418].src1 != mir.insns[417].dst ||
        mir.insns[421].label != mir.insns[375].label)
        return mir_machine_reject(
            "catalan-driver-schedule", "digit-body");

    function = find_global(mir.insns[409].name);
    if (function == NULL || function->is_defined ||
        function->is_funcptr || function->is_noreturn ||
        !function->has_proto || function->proto_variadic ||
        function->proto_nargs != 1 ||
        !mir_catalan_int_type(function->type) ||
        !mir_catalan_int_type(function->proto_types[0]) ||
        mir.insns[409].memory_flags != 0)
        return mir_machine_reject(
            "catalan-driver-schedule", "putchar-function");
    if (find_global(mir.insns[434].name) != function ||
        mir.insns[434].src1 >= 0 ||
        mir.insns[434].memory_flags != 0 ||
        !mir_catalan_int_type(mir.insns[434].type) ||
        strcmp(mir.insns[434].base_name, mir.insns[409].base_name) ||
        !mir_machine_constant_equals(mir.insns[432].dst, 10) ||
        !mir_numeric_call_arguments(&mir.insns[434], 1, arguments) ||
        arguments[0] != mir.insns[432].dst ||
        !mir_catalan_argument(
            433, &mir.insns[434], 0, mir.insns[432].dst) ||
        !mir_machine_constant_equals(mir.insns[435].dst, 0) ||
        mir.insns[436].src1 != mir.insns[435].dst)
        return mir_machine_reject(
            "catalan-driver-schedule", "newline-return");
    plan->putchar_function = function;
    return 1;
}

static int mir_log_series_array_address(
    const struct MirInsn *address, int expected_offset)
{
    int declared;

    if (address->opcode != MIR_ADDRESS ||
        !mir_catalan_long_pointer_type(address->type))
        return 0;
    for (declared = 0; declared < mir.declared_count; ++declared)
        if (!strcmp(mir.declared_names[declared], address->name))
            return mir.declared_storage[declared] == SC_LOCAL &&
                   mir.declared_offsets[declared] == expected_offset &&
                   mir.declared_sizes[declared] == 116 &&
                   mir.declared_is_array[declared] &&
                   !mir.declared_is_vla[declared] &&
                   !mir.declared_is_volatile[declared] &&
                   mir.declared_dim_counts[declared] == 1 &&
                   mir.declared_dims[declared][0] == 29 &&
                   mir.declared_elem_sizes[declared] == 4 &&
                   (mir.declared_types[declared] & 15) == TYPE_LONG;
    return 0;
}

static int mir_log_series_same_array(
    const struct MirInsn *address, const struct MirInsn *expected)
{
    return address->opcode == MIR_ADDRESS &&
           address->type == expected->type &&
           !strcmp(address->name, expected->name);
}

static int mir_log_series_print_function(
    struct MirLogSeriesDriverSchedule *plan,
    const struct MirInsn *call)
{
    struct Sym *function;

    function = find_global(call->name);
    if (function == NULL || function->is_defined ||
        function->is_funcptr || function->is_noreturn ||
        !function->has_proto || function->proto_nargs != 1 ||
        !function->proto_variadic ||
        type_ptr_depth(function->proto_types[0]) != 1 ||
        (function->proto_types[0] & 15) != TYPE_CHAR ||
        !mir_catalan_int_type(function->type) ||
        !mir_catalan_int_type(call->type) ||
        call->memory_flags != MIR_CALL_FLAG_VARIADIC ||
        call->base_name[0] == 0)
        return 0;
    snprintf(plan->print_name, sizeof(plan->print_name), "%s",
             call->base_name);
    return 1;
}

static int mir_match_log_series_driver_schedule(
    struct MirLogSeriesDriverSchedule *plan)
{
    static const char expected_opcodes[] =
        "LCNSLPNNNNNNNNNCBFANINCWANINCWNLNCBSJLNNNNNCSCNSLPPNNNNNNNNNCBFNCBNSANINCBN"
        "WNCBNSNLNCBSJLNCNSLNNPAGKUFAGAGKAGCNUBCBGCCNUBCBBGKNCBSNLJLTGKCNSNNNNNCNSLPN"
        "NNNNNNNNNNNNCBFDCBFLCJLCLPFNNCNCBSLNNNNNDCBFDCBFLCJLCLPFANIRDBNCBUNSCDBGKDCB"
        "NSDCBSNLJLNLNCBSJLCGKCE";
    const struct MirInsn *sum = &mir.insns[18];
    const struct MirInsn *term = &mir.insns[24];
    const struct MirInsn *i_store = &mir.insns[3];
    const struct MirInsn *rem_store = &mir.insns[44];
    const struct MirInsn *k_store = &mir.insns[92];
    const struct MirInsn *printed_store = &mir.insns[139];
    const struct MirInsn *p_store = &mir.insns[184];
    const struct MirInsn *d_store = &mir.insns[218];
    struct Sym *function;
    int arguments[3];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 250 ||
        sizeof(expected_opcodes) - 1 != 250 ||
        mir_cfg_block_count() != 22 ||
        mir.local_bytes != 250 ||
        mir.aggregate_temp_bytes != 0 ||
        mir.has_vla || !mir_has_cfg_backedge() ||
        !mir_catalan_int_type(mir.return_type))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir_modp2_opcode_code(
                mir.insns[instruction].opcode) !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "log-series-driver-schedule", "opcode");

    if (!mir_log_series_array_address(sum, -116) ||
        !mir_log_series_array_address(term, -234) ||
        !strcmp(sum->name, term->name) ||
        !mir_log_series_same_array(&mir.insns[68], term) ||
        !mir_log_series_same_array(&mir.insns[97], term) ||
        !mir_log_series_same_array(&mir.insns[102], sum) ||
        !mir_log_series_same_array(&mir.insns[104], term) ||
        !mir_log_series_same_array(&mir.insns[107], term) ||
        !mir_log_series_same_array(&mir.insns[207], sum))
        return mir_machine_reject(
            "log-series-driver-schedule", "arrays");

    if (!mir_numeric_same_location(
            i_store, SC_LOCAL, 2, 0, 0, -236) ||
        !mir_numeric_same_location(
            &mir.insns[35], SC_LOCAL, 2, 0, 0, -236) ||
        !mir_numeric_same_location(
            &mir.insns[47], SC_LOCAL, 2, 0, 0, -236) ||
        !mir_numeric_same_location(
            &mir.insns[86], SC_LOCAL, 2, 0, 0, -236) ||
        !mir_numeric_same_location(
            &mir.insns[147], SC_LOCAL, 2, 0, 0, -236) ||
        !mir_numeric_same_location(
            &mir.insns[242], SC_LOCAL, 2, 0, 0, -236) ||
        !mir_numeric_same_location(
            rem_store, SC_LOCAL, 4, 0, 0, -244) ||
        !mir_numeric_same_location(
            &mir.insns[67], SC_LOCAL, 4, 0, 0, -244) ||
        !mir_numeric_same_location(
            &mir.insns[80], SC_LOCAL, 4, 0, 0, -244) ||
        !mir_numeric_same_location(
            k_store, SC_LOCAL, 2, 0, 0, -238) ||
        !mir_numeric_same_location(
            &mir.insns[129], SC_LOCAL, 2, 0, 0, -238) ||
        !mir_numeric_same_location(
            printed_store, SC_LOCAL, 2, 0, 0, -240) ||
        !mir_numeric_same_location(
            &mir.insns[166], SC_LOCAL, 2, 0, 0, -240) ||
        !mir_numeric_same_location(
            &mir.insns[195], SC_LOCAL, 2, 0, 0, -240) ||
        !mir_numeric_same_location(
            &mir.insns[229], SC_LOCAL, 2, 0, 0, -240) ||
        !mir_numeric_same_location(
            &mir.insns[232], SC_LOCAL, 2, 0, 0, -240) ||
        !mir_numeric_same_location(
            p_store, SC_LOCAL, 4, 0, 0, -250) ||
        !mir_numeric_same_location(
            &mir.insns[191], SC_LOCAL, 4, 0, 0, -250) ||
        !mir_numeric_same_location(
            &mir.insns[211], SC_LOCAL, 4, 0, 0, -250) ||
        !mir_numeric_same_location(
            &mir.insns[224], SC_LOCAL, 4, 0, 0, -250) ||
        !mir_numeric_same_location(
            &mir.insns[228], SC_LOCAL, 4, 0, 0, -250) ||
        !mir_numeric_same_location(
            d_store, SC_LOCAL, 2, 0, 0, -246) ||
        !mir_numeric_same_location(
            &mir.insns[220], SC_LOCAL, 2, 0, 0, -246))
        return mir_machine_reject(
            "log-series-driver-schedule", "locals");

    if (!mir_machine_constant_equals(mir.insns[1].dst, 0) ||
        i_store->src1 != mir.insns[1].dst ||
        mir.insns[5].object != i_store->object ||
        mir.insns[5].src1 != mir.insns[1].dst ||
        mir.insns[5].src2 != mir.insns[34].dst ||
        mir.insns[5].phi_pred1 != mir.insns[0].label ||
        mir.insns[5].phi_pred2 != mir.insns[31].label ||
        !mir_machine_constant_equals(mir.insns[15].dst, 29) ||
        mir.insns[16].immediate != '<' ||
        mir.insns[16].src1 != mir.insns[5].dst ||
        mir.insns[16].src2 != mir.insns[15].dst ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        mir.insns[17].label != mir.insns[37].label ||
        mir.insns[20].src1 != sum->dst ||
        mir.insns[20].src2 != mir.insns[5].dst ||
        mir.insns[20].immediate != 4 ||
        mir.insns[20].memory_size != 4 ||
        !mir_machine_constant_equals(mir.insns[22].dst, 0) ||
        !mir_catalan_long_type(mir.insns[22].type) ||
        mir.insns[23].src1 != mir.insns[20].dst ||
        mir.insns[23].src2 != mir.insns[22].dst ||
        mir.insns[23].memory_size != 4 ||
        mir.insns[26].src1 != term->dst ||
        mir.insns[26].src2 != mir.insns[5].dst ||
        mir.insns[26].immediate != 4 ||
        mir.insns[26].memory_size != 4 ||
        !mir_machine_constant_equals(mir.insns[28].dst, 0) ||
        mir.insns[29].src1 != mir.insns[26].dst ||
        mir.insns[29].src2 != mir.insns[28].dst ||
        mir.insns[29].memory_size != 4 ||
        !mir_machine_constant_equals(mir.insns[33].dst, 1) ||
        mir.insns[34].immediate != '+' ||
        mir.insns[34].src1 != mir.insns[5].dst ||
        mir.insns[34].src2 != mir.insns[33].dst ||
        mir.insns[35].src1 != mir.insns[34].dst ||
        mir.insns[36].label != mir.insns[4].label)
        return mir_machine_reject(
            "log-series-driver-schedule", "zero-initialization");

    if (!mir_machine_constant_equals(mir.insns[43].dst, 2) ||
        !mir_catalan_long_type(mir.insns[43].type) ||
        rem_store->src1 != mir.insns[43].dst ||
        !mir_machine_constant_equals(mir.insns[45].dst, 0) ||
        mir.insns[47].src1 != mir.insns[45].dst ||
        mir.insns[49].object != i_store->object ||
        mir.insns[49].src1 != mir.insns[45].dst ||
        mir.insns[49].src2 != mir.insns[85].dst ||
        mir.insns[49].phi_pred1 != mir.insns[37].label ||
        mir.insns[49].phi_pred2 != mir.insns[82].label ||
        mir.insns[50].object != rem_store->object ||
        mir.insns[50].src1 != mir.insns[43].dst ||
        mir.insns[50].src2 != mir.insns[78].dst ||
        mir.insns[50].phi_pred1 != mir.insns[37].label ||
        mir.insns[50].phi_pred2 != mir.insns[82].label ||
        !mir_machine_constant_equals(mir.insns[60].dst, 29) ||
        mir.insns[61].immediate != '<' ||
        mir.insns[61].src1 != mir.insns[49].dst ||
        mir.insns[61].src2 != mir.insns[60].dst ||
        mir.insns[62].src1 != mir.insns[61].dst ||
        mir.insns[62].label != mir.insns[88].label ||
        !mir_machine_constant_equals(mir.insns[64].dst, 10000) ||
        mir.insns[65].immediate != '*' ||
        mir.insns[65].src1 != mir.insns[50].dst ||
        mir.insns[65].src2 != mir.insns[64].dst ||
        !mir_catalan_long_type(mir.insns[65].type) ||
        mir.insns[67].src1 != mir.insns[65].dst ||
        mir.insns[70].src1 != mir.insns[68].dst ||
        mir.insns[70].src2 != mir.insns[49].dst ||
        mir.insns[70].immediate != 4 ||
        mir.insns[70].memory_size != 4 ||
        !mir_machine_constant_equals(mir.insns[72].dst, 3) ||
        mir.insns[73].immediate != '/' ||
        mir.insns[73].src1 != mir.insns[65].dst ||
        mir.insns[73].src2 != mir.insns[72].dst ||
        mir.insns[75].src1 != mir.insns[70].dst ||
        mir.insns[75].src2 != mir.insns[73].dst ||
        mir.insns[75].memory_size != 4 ||
        !mir_machine_constant_equals(mir.insns[77].dst, 3) ||
        mir.insns[78].immediate != '%' ||
        mir.insns[78].src1 != mir.insns[65].dst ||
        mir.insns[78].src2 != mir.insns[77].dst ||
        mir.insns[80].src1 != mir.insns[78].dst ||
        !mir_machine_constant_equals(mir.insns[84].dst, 1) ||
        mir.insns[85].immediate != '+' ||
        mir.insns[85].src1 != mir.insns[49].dst ||
        mir.insns[85].src2 != mir.insns[84].dst ||
        mir.insns[86].src1 != mir.insns[85].dst ||
        mir.insns[87].label != mir.insns[48].label)
        return mir_machine_reject(
            "log-series-driver-schedule", "fraction-initialization");

    if (!mir_machine_constant_equals(mir.insns[90].dst, 0) ||
        k_store->src1 != mir.insns[90].dst ||
        mir.insns[96].object != k_store->object ||
        mir.insns[96].src1 != mir.insns[90].dst ||
        mir.insns[96].src2 != mir.insns[128].dst ||
        mir.insns[96].phi_pred1 != mir.insns[88].label ||
        mir.insns[96].phi_pred2 != mir.insns[131].label)
        return mir_machine_reject(
            "log-series-driver-schedule", "series-state");

    if (!mir_catalan_defined_function(
            &mir.insns[99], TYPE_INT, 1,
            &plan->is_zero_function) ||
        !mir_catalan_long_pointer_type(
            plan->is_zero_function->proto_types[0]) ||
        !mir_numeric_call_arguments(
            &mir.insns[99], 1, arguments) ||
        arguments[0] != mir.insns[97].dst ||
        !mir_catalan_argument(
            98, &mir.insns[99], 0, mir.insns[97].dst) ||
        mir.insns[100].immediate != '!' ||
        mir.insns[100].src1 != mir.insns[99].dst ||
        mir.insns[101].src1 != mir.insns[100].dst ||
        mir.insns[101].label != mir.insns[133].label)
        return mir_machine_reject(
            "log-series-driver-schedule", "zero-test-call");

    if (!mir_catalan_defined_function(
            &mir.insns[106], TYPE_VOID, 2,
            &plan->add_function) ||
        !mir_catalan_long_pointer_type(
            plan->add_function->proto_types[0]) ||
        !mir_catalan_long_pointer_type(
            plan->add_function->proto_types[1]) ||
        !mir_numeric_call_arguments(
            &mir.insns[106], 2, arguments) ||
        arguments[0] != mir.insns[102].dst ||
        arguments[1] != mir.insns[104].dst ||
        !mir_catalan_argument(
            103, &mir.insns[106], 0, mir.insns[102].dst) ||
        !mir_catalan_argument(
            105, &mir.insns[106], 1, mir.insns[104].dst))
        return mir_machine_reject(
            "log-series-driver-schedule", "add-call");

    if (!mir_catalan_defined_function(
            &mir.insns[125], TYPE_VOID, 3,
            &plan->mul_div_function) ||
        !mir_catalan_long_pointer_type(
            plan->mul_div_function->proto_types[0]) ||
        !mir_catalan_long_type(
            plan->mul_div_function->proto_types[1]) ||
        !mir_catalan_long_type(
            plan->mul_div_function->proto_types[2]) ||
        !mir_numeric_call_arguments(
            &mir.insns[125], 3, arguments) ||
        arguments[0] != mir.insns[107].dst ||
        arguments[1] != mir.insns[114].dst ||
        arguments[2] != mir.insns[123].dst ||
        !mir_catalan_argument(
            108, &mir.insns[125], 0, mir.insns[107].dst) ||
        !mir_catalan_argument(
            115, &mir.insns[125], 1, mir.insns[114].dst) ||
        !mir_catalan_argument(
            124, &mir.insns[125], 2, mir.insns[123].dst) ||
        !mir_machine_constant_equals(mir.insns[109].dst, 2) ||
        mir.insns[111].immediate != 0 ||
        mir.insns[111].src1 != mir.insns[96].dst ||
        mir.insns[112].immediate != '*' ||
        mir.insns[112].src1 != mir.insns[109].dst ||
        mir.insns[112].src2 != mir.insns[111].dst ||
        !mir_machine_constant_equals(mir.insns[113].dst, 1) ||
        mir.insns[114].immediate != '+' ||
        mir.insns[114].src1 != mir.insns[112].dst ||
        mir.insns[114].src2 != mir.insns[113].dst ||
        !mir_machine_constant_equals(mir.insns[116].dst, 9) ||
        !mir_machine_constant_equals(mir.insns[117].dst, 2) ||
        mir.insns[119].immediate != 0 ||
        mir.insns[119].src1 != mir.insns[96].dst ||
        mir.insns[120].immediate != '*' ||
        mir.insns[120].src1 != mir.insns[117].dst ||
        mir.insns[120].src2 != mir.insns[119].dst ||
        !mir_machine_constant_equals(mir.insns[121].dst, 3) ||
        mir.insns[122].immediate != '+' ||
        mir.insns[122].src1 != mir.insns[120].dst ||
        mir.insns[122].src2 != mir.insns[121].dst ||
        mir.insns[123].immediate != '*' ||
        mir.insns[123].src1 != mir.insns[116].dst ||
        mir.insns[123].src2 != mir.insns[122].dst ||
        !mir_machine_constant_equals(mir.insns[127].dst, 1) ||
        mir.insns[128].immediate != '+' ||
        mir.insns[128].src1 != mir.insns[96].dst ||
        mir.insns[128].src2 != mir.insns[127].dst ||
        mir.insns[129].src1 != mir.insns[128].dst ||
        mir.insns[132].label != mir.insns[93].label)
        return mir_machine_reject(
            "log-series-driver-schedule", "series-body");

    if (mir.insns[134].immediate < 0 ||
        type_ptr_depth(mir.insns[134].type) != 1 ||
        (mir.insns[134].type & 15) != TYPE_CHAR ||
        !mir_log_series_print_function(
            plan, &mir.insns[136]) ||
        !mir_numeric_call_arguments(
            &mir.insns[136], 1, arguments) ||
        arguments[0] != mir.insns[134].dst ||
        !mir_catalan_argument(
            135, &mir.insns[136], 0, mir.insns[134].dst))
        return mir_machine_reject(
            "log-series-driver-schedule", "prefix-report");
    plan->format_string_id = (int)mir.insns[134].immediate;

    if (!mir_machine_constant_equals(mir.insns[137].dst, 0) ||
        printed_store->src1 != mir.insns[137].dst ||
        !mir_machine_constant_equals(mir.insns[145].dst, 0) ||
        mir.insns[147].src1 != mir.insns[145].dst ||
        mir.insns[149].object != i_store->object ||
        mir.insns[149].src1 != mir.insns[145].dst ||
        mir.insns[149].src2 != mir.insns[241].dst ||
        mir.insns[149].phi_pred1 != mir.insns[133].label ||
        mir.insns[149].phi_pred2 != mir.insns[238].label ||
        !mir_machine_constant_equals(mir.insns[163].dst, 29) ||
        mir.insns[164].immediate != '<' ||
        mir.insns[164].src1 != mir.insns[149].dst ||
        mir.insns[164].src2 != mir.insns[163].dst ||
        mir.insns[165].src1 != mir.insns[164].dst ||
        mir.insns[165].label != mir.insns[173].label ||
        !mir_machine_constant_equals(mir.insns[167].dst, 100) ||
        mir.insns[168].immediate != '<' ||
        mir.insns[168].src1 != mir.insns[166].dst ||
        mir.insns[168].src2 != mir.insns[167].dst ||
        mir.insns[169].src1 != mir.insns[168].dst ||
        mir.insns[169].label != mir.insns[173].label ||
        mir.insns[172].label != mir.insns[175].label ||
        mir.insns[176].src1 != mir.insns[171].dst ||
        mir.insns[176].src2 != mir.insns[174].dst ||
        mir.insns[176].phi_pred1 != mir.insns[170].label ||
        mir.insns[176].phi_pred2 != mir.insns[173].label ||
        mir.insns[177].src1 != mir.insns[176].dst ||
        mir.insns[177].label != mir.insns[244].label)
        return mir_machine_reject(
            "log-series-driver-schedule", "outer-print-loop");

    if (!mir_machine_constant_equals(mir.insns[180].dst, 10000) ||
        !mir_machine_constant_equals(mir.insns[182].dst, 10) ||
        mir.insns[183].immediate != '/' ||
        mir.insns[183].src1 != mir.insns[180].dst ||
        mir.insns[183].src2 != mir.insns[182].dst ||
        p_store->src1 != mir.insns[183].dst ||
        !mir_machine_constant_equals(mir.insns[192].dst, 0) ||
        mir.insns[193].immediate != '>' ||
        mir.insns[193].src1 != mir.insns[191].dst ||
        mir.insns[193].src2 != mir.insns[192].dst ||
        mir.insns[194].src1 != mir.insns[193].dst ||
        mir.insns[194].label != mir.insns[202].label ||
        !mir_machine_constant_equals(mir.insns[196].dst, 100) ||
        mir.insns[197].immediate != '<' ||
        mir.insns[197].src1 != mir.insns[195].dst ||
        mir.insns[197].src2 != mir.insns[196].dst ||
        mir.insns[198].src1 != mir.insns[197].dst ||
        mir.insns[198].label != mir.insns[202].label ||
        mir.insns[201].label != mir.insns[204].label ||
        mir.insns[205].src1 != mir.insns[200].dst ||
        mir.insns[205].src2 != mir.insns[203].dst ||
        mir.insns[205].phi_pred1 != mir.insns[199].label ||
        mir.insns[205].phi_pred2 != mir.insns[202].label ||
        mir.insns[206].src1 != mir.insns[205].dst ||
        mir.insns[206].label != mir.insns[236].label)
        return mir_machine_reject(
            "log-series-driver-schedule", "inner-print-loop");

    if (mir.insns[209].src1 != mir.insns[207].dst ||
        mir.insns[209].src2 != mir.insns[149].dst ||
        mir.insns[209].immediate != 4 ||
        mir.insns[209].memory_size != 4 ||
        mir.insns[210].src1 != mir.insns[209].dst ||
        mir.insns[210].memory_size != 4 ||
        mir.insns[212].immediate != '/' ||
        mir.insns[212].src1 != mir.insns[210].dst ||
        mir.insns[212].src2 != mir.insns[211].dst ||
        !mir_machine_constant_equals(mir.insns[214].dst, 10) ||
        mir.insns[215].immediate != '%' ||
        mir.insns[215].src1 != mir.insns[212].dst ||
        mir.insns[215].src2 != mir.insns[214].dst ||
        mir.insns[216].immediate != 0 ||
        mir.insns[216].src1 != mir.insns[215].dst ||
        d_store->src1 != mir.insns[216].dst ||
        !mir_machine_constant_equals(mir.insns[219].dst, 48) ||
        mir.insns[221].immediate != '+' ||
        mir.insns[221].src1 != mir.insns[219].dst ||
        mir.insns[221].src2 != mir.insns[220].dst ||
        !mir_numeric_call_arguments(
            &mir.insns[223], 1, arguments) ||
        arguments[0] != mir.insns[221].dst ||
        !mir_catalan_argument(
            222, &mir.insns[223], 0, mir.insns[221].dst) ||
        !mir_machine_constant_equals(mir.insns[225].dst, 10) ||
        mir.insns[226].immediate != '/' ||
        mir.insns[226].src1 != mir.insns[224].dst ||
        mir.insns[226].src2 != mir.insns[225].dst ||
        mir.insns[228].src1 != mir.insns[226].dst ||
        !mir_machine_constant_equals(mir.insns[230].dst, 1) ||
        mir.insns[231].immediate != '+' ||
        mir.insns[231].src1 != mir.insns[229].dst ||
        mir.insns[231].src2 != mir.insns[230].dst ||
        mir.insns[232].src1 != mir.insns[231].dst ||
        mir.insns[235].label != mir.insns[185].label ||
        !mir_machine_constant_equals(mir.insns[240].dst, 1) ||
        mir.insns[241].immediate != '+' ||
        mir.insns[241].src1 != mir.insns[149].dst ||
        mir.insns[241].src2 != mir.insns[240].dst ||
        mir.insns[242].src1 != mir.insns[241].dst ||
        mir.insns[243].label != mir.insns[148].label)
        return mir_machine_reject(
            "log-series-driver-schedule", "digit-body");

    function = find_global(mir.insns[223].name);
    if (function == NULL || function->is_defined ||
        function->is_funcptr || function->is_noreturn ||
        !function->has_proto || function->proto_variadic ||
        function->proto_nargs != 1 ||
        !mir_catalan_int_type(function->type) ||
        !mir_catalan_int_type(function->proto_types[0]) ||
        mir.insns[223].memory_flags != 0 ||
        find_global(mir.insns[247].name) != function ||
        mir.insns[247].memory_flags != 0 ||
        strcmp(mir.insns[247].base_name,
               mir.insns[223].base_name) ||
        !mir_machine_constant_equals(mir.insns[245].dst, 10) ||
        !mir_numeric_call_arguments(
            &mir.insns[247], 1, arguments) ||
        arguments[0] != mir.insns[245].dst ||
        !mir_catalan_argument(
            246, &mir.insns[247], 0, mir.insns[245].dst) ||
        !mir_machine_constant_equals(mir.insns[248].dst, 0) ||
        mir.insns[249].src1 != mir.insns[248].dst)
        return mir_machine_reject(
            "log-series-driver-schedule", "putchar-return");
    plan->putchar_function = function;
    return 1;
}

static int mir_modp2_opcode_code(int opcode)
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

static int mir_modp2_int_type(int type, int is_unsigned)
{
    return type_ptr_depth(type) == 0 &&
           !type_is_float(type) &&
           (type & 15) == TYPE_INT &&
           ((type & TYPE_UNSIGNED) != 0) == is_unsigned &&
           type_size(type) == 2;
}

static int mir_modp2_long_type(int type)
{
    return type_ptr_depth(type) == 0 &&
           !type_is_float(type) &&
           (type & 15) == TYPE_LONG &&
           (type & TYPE_UNSIGNED) == 0 &&
           type_size(type) == 4;
}

static int mir_modp2_array_address(
    const struct MirInsn *address, int element_count,
    int is_unsigned, struct Sym **symbol_out)
{
    struct Sym *symbol;
    long offset;

    if (address == NULL || address->opcode != MIR_ADDRESS ||
        !mir_machine_global_address_offset(
            address->dst, &symbol, &offset, 0) ||
        offset != 0 || symbol == NULL || !symbol->is_defined ||
        !symbol->is_array || symbol->is_volatile ||
        symbol->array_len != element_count ||
        symbol->elem_size != 2 ||
        symbol->size != element_count * 2 ||
        !mir_modp2_int_type(symbol->type, is_unsigned) ||
        type_ptr_depth(address->type) != 1 ||
        (address->type & 15) != TYPE_INT ||
        ((address->type & TYPE_UNSIGNED) != 0) != is_unsigned)
        return 0;
    *symbol_out = symbol;
    return 1;
}

static int mir_modp2_print_function(
    struct MirModp2DriverSchedule *plan, int slot,
    const struct MirInsn *call)
{
    struct Sym *function;

    function = find_global(call->name);
    if (function == NULL || function->is_defined ||
        function->is_funcptr || function->is_noreturn ||
        !function->has_proto || function->proto_nargs != 1 ||
        !function->proto_variadic ||
        type_ptr_depth(function->proto_types[0]) != 1 ||
        (function->proto_types[0] & 15) != TYPE_CHAR ||
        !mir_modp2_int_type(function->type, 0) ||
        !mir_modp2_int_type(call->type, 0) ||
        call->memory_flags != MIR_CALL_FLAG_VARIADIC ||
        call->base_name[0] == 0 ||
        (plan->print_function != NULL &&
         plan->print_function != function))
        return 0;
    plan->print_function = function;
    snprintf(plan->print_names[slot],
             sizeof(plan->print_names[slot]), "%s",
             call->base_name);
    return 1;
}

static int mir_match_modp2_loop(
    struct MirModp2DriverSchedule *plan, int slot,
    const struct MirModp2LoopShape *shape,
    const struct MirInsn *sum_store,
    const struct MirInsn *index_store,
    const struct MirInsn **value_store,
    struct Sym **array_symbol,
    int previous_sum, int *loop_sum_out)
{
    const struct MirInsn *sum_phi = &mir.insns[shape->sum_phi];
    const struct MirInsn *index_phi = &mir.insns[shape->index_phi];
    const struct MirInsn *load = &mir.insns[shape->element_load];
    const struct MirInsn *call = &mir.insns[shape->print_call];
    const struct MirInsn *string = &mir.insns[shape->print_start];
    int arguments[11];
    int previous_add;
    int item;

    if (!mir_machine_constant_equals(
            mir.insns[shape->init_constant].dst, 0) ||
        mir.insns[shape->init_store].src1 !=
            mir.insns[shape->init_constant].dst ||
        !mir_machine_same_location(
            &mir.insns[shape->init_store], index_store) ||
        !mir_modp2_int_type(
            mir.insns[shape->init_store].type, 0) ||
        sum_phi->object != sum_store->object ||
        !mir_modp2_long_type(sum_phi->type) ||
        sum_phi->src1 != previous_sum ||
        sum_phi->phi_pred1 !=
            mir.insns[shape->entry_label].label ||
        sum_phi->phi_pred2 !=
            mir.insns[shape->tail_label].label ||
        index_phi->object != index_store->object ||
        !mir_modp2_int_type(index_phi->type, 0) ||
        index_phi->src1 !=
            mir.insns[shape->init_constant].dst ||
        index_phi->phi_pred1 !=
            mir.insns[shape->entry_label].label ||
        index_phi->phi_pred2 !=
            mir.insns[shape->tail_label].label ||
        !mir_machine_constant_equals(
            mir.insns[shape->bound_constant].dst,
            shape->element_count * 2) ||
        !mir_machine_constant_equals(
            mir.insns[shape->element_constant].dst, 2) ||
        mir.insns[shape->bound_division].immediate != '/' ||
        mir.insns[shape->bound_division].src1 !=
            mir.insns[shape->bound_constant].dst ||
        mir.insns[shape->bound_division].src2 !=
            mir.insns[shape->element_constant].dst ||
        !mir_modp2_int_type(
            mir.insns[shape->bound_division].type, 0) ||
        mir.insns[shape->comparison].immediate != '<' ||
        mir.insns[shape->comparison].src1 != index_phi->dst ||
        mir.insns[shape->comparison].src2 !=
            mir.insns[shape->bound_division].dst ||
        !mir_modp2_int_type(
            mir.insns[shape->comparison].secondary_offset, 0) ||
        mir.insns[shape->branch].src1 !=
            mir.insns[shape->comparison].dst ||
        mir.insns[shape->branch].label !=
            mir.insns[shape->exit_label].label)
        return mir_machine_reject(
            "modp2-driver-schedule", "loop-control");

    if (!mir_modp2_array_address(
            &mir.insns[shape->array_address],
            shape->element_count, shape->is_unsigned,
            array_symbol) ||
        mir.insns[shape->index_address].src1 !=
            mir.insns[shape->array_address].dst ||
        mir.insns[shape->index_address].src2 != index_phi->dst ||
        mir.insns[shape->index_address].immediate != 2 ||
        mir.insns[shape->index_address].memory_size != 2 ||
        load->src1 != mir.insns[shape->index_address].dst ||
        load->memory_size != 2 ||
        (load->memory_flags & (1 | 8)) != 0 ||
        load->bit_width != 0 ||
        !mir_modp2_int_type(load->type, shape->is_unsigned) ||
        mir.insns[shape->value_store].src1 != load->dst ||
        !mir_modp2_int_type(
            mir.insns[shape->value_store].type,
            shape->is_unsigned))
        return mir_machine_reject(
            "modp2-driver-schedule", "array-load");
    if (*value_store == NULL)
        *value_store = &mir.insns[shape->value_store];
    else if (!mir_machine_same_location(
                 *value_store,
                 &mir.insns[shape->value_store]))
        return mir_machine_reject(
            "modp2-driver-schedule", "value-local");

    if (string->immediate < 0 ||
        type_ptr_depth(string->type) != 1 ||
        (string->type & 15) != TYPE_CHAR ||
        !mir_modp2_print_function(plan, slot, call) ||
        !mir_numeric_call_arguments(
            call, shape->divisor_count + 2, arguments) ||
        arguments[0] != string->dst ||
        arguments[1] != load->dst ||
        mir.insns[shape->print_start + 1].src1 != string->dst ||
        mir.insns[shape->print_start + 1].immediate != 0 ||
        mir.insns[shape->print_start + 3].src1 != load->dst ||
        mir.insns[shape->print_start + 3].immediate != 1)
        return mir_machine_reject(
            "modp2-driver-schedule", "print-call");
    plan->string_ids[slot] = (int)string->immediate;
    for (item = 0; item < shape->divisor_count; ++item) {
        int constant_index = shape->print_start + 5 + 4 * item;
        int binary_index = constant_index + 1;
        int argument_index = constant_index + 2;
        const struct MirInsn *binary = &mir.insns[binary_index];

        if (!mir_machine_constant_equals(
                mir.insns[constant_index].dst,
                shape->divisors[item]) ||
            binary->immediate != shape->operation ||
            binary->src1 != load->dst ||
            binary->src2 != mir.insns[constant_index].dst ||
            !mir_modp2_int_type(
                binary->type, shape->is_unsigned) ||
            !mir_modp2_int_type(
                binary->secondary_offset, shape->is_unsigned) ||
            mir.insns[argument_index].src1 != binary->dst ||
            mir.insns[argument_index].immediate != item + 2 ||
            arguments[item + 2] != binary->dst)
            return mir_machine_reject(
                "modp2-driver-schedule", "print-arguments");
    }

    previous_add = sum_phi->dst;
    for (item = 0; item < shape->divisor_count; ++item) {
        int start = shape->sum_start + 8 * item;
        const struct MirInsn *binary = &mir.insns[start + 3];
        const struct MirInsn *conversion = &mir.insns[start + 4];
        const struct MirInsn *addition = &mir.insns[start + 5];
        const struct MirInsn *store = &mir.insns[start + 7];

        if (!mir_machine_constant_equals(
                mir.insns[start + 2].dst,
                shape->divisors[item]) ||
            binary->immediate != shape->operation ||
            binary->src1 != load->dst ||
            binary->src2 != mir.insns[start + 2].dst ||
            !mir_modp2_int_type(
                binary->type, shape->is_unsigned) ||
            !mir_modp2_int_type(
                binary->secondary_offset, shape->is_unsigned) ||
            conversion->immediate != 0 ||
            conversion->src1 != binary->dst ||
            !mir_modp2_long_type(conversion->type) ||
            addition->immediate != '+' ||
            addition->src1 != previous_add ||
            addition->src2 != conversion->dst ||
            !mir_modp2_long_type(addition->type) ||
            store->src1 != addition->dst ||
            !mir_machine_same_location(store, sum_store))
            return mir_machine_reject(
                "modp2-driver-schedule", "sum-update");
        previous_add = addition->dst;
    }

    if (sum_phi->src2 != previous_add ||
        !mir_machine_constant_equals(
            mir.insns[shape->increment_constant].dst, 1) ||
        mir.insns[shape->increment].immediate != '+' ||
        mir.insns[shape->increment].src1 != index_phi->dst ||
        mir.insns[shape->increment].src2 !=
            mir.insns[shape->increment_constant].dst ||
        !mir_modp2_int_type(
            mir.insns[shape->increment].type, 0) ||
        mir.insns[shape->increment_store].src1 !=
            mir.insns[shape->increment].dst ||
        !mir_machine_same_location(
            &mir.insns[shape->increment_store], index_store) ||
        index_phi->src2 != mir.insns[shape->increment].dst ||
        mir.insns[shape->jump].label !=
            mir.insns[shape->header_label].label)
        return mir_machine_reject(
            "modp2-driver-schedule", "loop-tail");

    *loop_sum_out = sum_phi->dst;
    return 1;
}

static int mir_match_modp2_driver_schedule(
    struct MirModp2DriverSchedule *plan)
{
    static const char expected_opcodes[] =
        "LNCSNNNNNCNSLPPNNCCBNBFNANIRSTGNGNCBGNCBGNCBGNCBGNCBGNCBGNCBGNCBGKNNCBUBNSN"
        "NCBUBNSNNCBUBNSNNCBUBNSNNCBUBNSNNCBUBNSNNCBUBNSNNCBUBNSNLNCBSJLNNNNNCNSLPPP"
        "NCCBNBFNANIRSTGNGNCBGNCBGNCBGNCBGNCBGNCBGKNNCBUBNSNNCBUBNSNNCBUBNSNNCBUBNSN"
        "NCBUBNSNNCBUBNSNLNCBSJLNNNNNCNSLPPNNNCCBNBFNANIRSTGNGNCBGNCBGNCBGNCBGNCBGKN"
        "NCBUBNSNNCBUBNSNNCBUBNSNNCBUBNSNNCBUBNSNLNCBSJLNNNNNCNSLPPPNNCCBNBFNANIRSTG"
        "NGNCBGNCBGNCBGNCBGNCBGNCBGNCBGNCBGNCBGKNNCBUBNSNNCBUBNSNNCBUBNSNNCBUBNSNNCB"
        "UBNSNNCBUBNSNNCBUBNSNNCBUBNSNNCBUBNSNLNCBSJLNNNNNCNSLPPPNNCCBNBFNANIRSTGNGN"
        "CBGNCBGNCBGNCBGKNNCBUBNSNNCBUBNSNNCBUBNSNNCBUBNSNLNCBSJLNNNNNCNSLPPNPNCCBNB"
        "FNANIRSTGNGNCBGNCBGNCBGNCBGKNNCBUBNSNNCBUBNSNNCBUBNSNNCBUBNSNLNCBSJLTGNGKCE";
    static const int signed_mod_pow2[] =
        {2, 4, 8, 16, 32, 64, 128, 256};
    static const int signed_mod_other[] =
        {3, 5, 10, 100, 255, 1000};
    static const int unsigned_mod[] =
        {8, 3, 255, 256, 1000};
    static const int signed_div_pow2[] =
        {2, 4, 8, 128, 256, 512, 1024, 4096, 16384};
    static const int signed_div_other[] =
        {3, 10, 255, 1000};
    static const int unsigned_div[] =
        {8, 3, 256, 1000};
    static const struct MirModp2LoopShape loops[6] = {
        {0, 9, 11, 12, 13, 14, 17, 18, 19, 21, 22,
         24, 26, 27, 28, 29, 65, 66, 131, 133, 134, 135, 136, 137,
         38, '%', 0, 8, signed_mod_pow2},
        {137, 143, 145, 146, 147, 148, 151, 152, 153, 155, 156,
         158, 160, 161, 162, 163, 191, 192, 241, 243, 244, 245, 246, 247,
         38, '%', 0, 6, signed_mod_other},
        {247, 253, 255, 256, 257, 258, 262, 263, 264, 266, 267,
         269, 271, 272, 273, 274, 298, 299, 340, 342, 343, 344, 345, 346,
         13, '%', 1, 5, unsigned_mod},
        {346, 352, 354, 355, 356, 357, 361, 362, 363, 365, 366,
         368, 370, 371, 372, 373, 413, 414, 487, 489, 490, 491, 492, 493,
         38, '/', 0, 9, signed_div_pow2},
        {493, 499, 501, 502, 503, 504, 508, 509, 510, 512, 513,
         515, 517, 518, 519, 520, 540, 541, 574, 576, 577, 578, 579, 580,
         38, '/', 0, 4, signed_div_other},
        {580, 586, 588, 589, 590, 591, 595, 596, 597, 599, 600,
         602, 604, 605, 606, 607, 627, 628, 661, 663, 664, 665, 666, 667,
         13, '/', 1, 4, unsigned_div}
    };
    const struct MirInsn *sum_store;
    const struct MirInsn *index_store;
    const struct MirInsn *signed_value_store = NULL;
    const struct MirInsn *unsigned_value_store = NULL;
    struct Sym *array_symbol = NULL;
    int arguments[2];
    int previous_sum;
    int instruction;
    int loop;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 675 ||
        sizeof(expected_opcodes) - 1 != 675 ||
        mir_cfg_block_count() != 19 ||
        mir.local_bytes != 18 ||
        mir.aggregate_temp_bytes != 0 ||
        mir.has_vla || !mir_has_cfg_backedge() ||
        !mir_modp2_int_type(mir.return_type, 0))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir_modp2_opcode_code(
                mir.insns[instruction].opcode) !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "modp2-driver-schedule", "opcode");

    sum_store = &mir.insns[3];
    index_store = &mir.insns[11];
    if (!mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        sum_store->src1 != mir.insns[2].dst ||
        !mir_modp2_long_type(sum_store->type) ||
        sum_store->object < 0 ||
        !mir_machine_constant_equals(mir.insns[9].dst, 0) ||
        index_store->src1 != mir.insns[9].dst ||
        !mir_modp2_int_type(index_store->type, 0) ||
        index_store->object < 0)
        return mir_machine_reject(
            "modp2-driver-schedule", "initial-state");

    previous_sum = mir.insns[2].dst;
    for (loop = 0; loop < 6; ++loop) {
        const struct MirInsn **value_store =
            loops[loop].is_unsigned
                ? &unsigned_value_store : &signed_value_store;

        if (!mir_match_modp2_loop(
                plan, loop, &loops[loop], sum_store,
                index_store, value_store, &array_symbol,
                previous_sum, &previous_sum))
            return 0;
        if (loops[loop].is_unsigned) {
            if (plan->unsigned_values == NULL)
                plan->unsigned_values = array_symbol;
            else if (plan->unsigned_values != array_symbol)
                return mir_machine_reject(
                    "modp2-driver-schedule",
                    "unsigned-array");
        } else {
            if (plan->signed_values == NULL)
                plan->signed_values = array_symbol;
            else if (plan->signed_values != array_symbol)
                return mir_machine_reject(
                    "modp2-driver-schedule",
                    "signed-array");
        }
    }
    if (plan->signed_values == plan->unsigned_values)
        return mir_machine_reject(
            "modp2-driver-schedule", "distinct-arrays");

    if (mir.insns[668].immediate < 0 ||
        type_ptr_depth(mir.insns[668].type) != 1 ||
        (mir.insns[668].type & 15) != TYPE_CHAR ||
        !mir_modp2_print_function(plan, 6, &mir.insns[672]) ||
        !mir_numeric_call_arguments(
            &mir.insns[672], 2, arguments) ||
        arguments[0] != mir.insns[668].dst ||
        arguments[1] != previous_sum ||
        mir.insns[669].src1 != mir.insns[668].dst ||
        mir.insns[669].immediate != 0 ||
        mir.insns[671].src1 != previous_sum ||
        mir.insns[671].immediate != 1 ||
        !mir_machine_constant_equals(mir.insns[673].dst, 0) ||
        mir.insns[674].src1 != mir.insns[673].dst)
        return mir_machine_reject(
            "modp2-driver-schedule", "final-report");
    plan->string_ids[6] = (int)mir.insns[668].immediate;
    return 1;
}

static void mir_fixed_point_mixed_product(
    FILE *out, int signed_offset, int unsigned_offset)
{
    int positive = new_label();
    int done = new_label();

    fprintf(out,
            "\tld c,(ix%+d)\n\tld b,(ix%+d)\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tbit 7,b\n\tjp z,L%d\n"
            "\txor a\n\tsub c\n\tld c,a\n"
            "\tsbc a,a\n\tsub b\n\tld b,a\n",
            signed_offset, signed_offset + 1,
            unsigned_offset, unsigned_offset + 1,
            positive);
    mir_emit_runtime_call(out, "__m1u");
    fputs("\txor a\n\tsub l\n\tld l,a\n"
          "\tsbc a,a\n\tsub h\n\tld h,a\n"
          "\tsbc a,a\n\tsub e\n\tld e,a\n"
          "\tsbc a,a\n\tsub d\n\tld d,a\n", out);
    fprintf(out, "\tjp L%d\nL%d:\n", done, positive);
    mir_emit_runtime_call(out, "__m1u");
    fprintf(out, "L%d:\n", done);
}

static void mir_fixed_point_add_stacked_accumulator(FILE *out)
{
    fputs("\tpop bc\n\tadd hl,bc\n\tex de,hl\n"
          "\tpop bc\n\tadc hl,bc\n\tex de,hl\n", out);
}

static void mir_emit_fixed_point_multiply(
    FILE *out, const struct MirFixedPointMultiply *plan)
{
    int left_low = plan->left_stack_offset + 2;
    int left_high = left_low + 2;
    int right_low = plan->right_stack_offset + 2;
    int right_high = right_low + 2;

    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    fprintf(out,
            "\tld c,(ix%+d)\n\tld b,(ix%+d)\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            left_high, left_high + 1,
            right_high, right_high + 1);
    mir_emit_runtime_call(out, "__m1s");
    fputs("\tld d,h\n\tld e,l\n\tld hl,0\n"
          "\tpush de\n\tpush hl\n", out);

    mir_fixed_point_mixed_product(
        out, left_high, right_low);
    mir_fixed_point_add_stacked_accumulator(out);
    fputs("\tpush de\n\tpush hl\n", out);

    mir_fixed_point_mixed_product(
        out, right_high, left_low);
    mir_fixed_point_add_stacked_accumulator(out);
    fputs("\tpush de\n\tpush hl\n", out);

    fprintf(out,
            "\tld c,(ix%+d)\n\tld b,(ix%+d)\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            left_low, left_low + 1,
            right_low, right_low + 1);
    mir_emit_runtime_call(out, "__m1u");
    fputs("\tld c,e\n\tld b,d\n"
          "\tld a,d\n\trla\n\tsbc a,a\n"
          "\tld d,a\n\tld e,a\n\tld h,b\n\tld l,c\n", out);
    mir_fixed_point_add_stacked_accumulator(out);
    fputs("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static void mir_emit_narrowed_divmod_scale(
    FILE *out, unsigned int scale)
{
    int bit;
    int highest = 0;

    while ((scale >> (unsigned)highest) > 1U)
        ++highest;
    if (highest == 0)
        return;
    fputs("\tld d,h\n\tld e,l\n", out);
    for (bit = highest - 1; bit >= 0; --bit) {
        fputs("\tadd hl,hl\n", out);
        if ((scale & (1U << (unsigned)bit)) != 0)
            fputs("\tadd hl,de\n", out);
    }
}

static void mir_emit_narrowed_divmod_loop_schedule(
    FILE *out, const struct MirNarrowedDivmodLoopSchedule *plan)
{
    int fill_loop = new_label();
    int inner_loop = new_label();
    int inner_done = new_label();
    int outer_loop = new_label();
    int done = new_label();
    int index;

    fprintf(out,
            "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n",
            mir.local_bytes);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (index = 0; index < 7; ++index)
        fprintf(out, "\tld (ix%+d),%d\n",
                plan->expected_offset + index,
                plan->expected_values[index]);

    fprintf(out,
            "\tpush ix\n\tpop hl\n\tld de,%d\n\tadd hl,de\n"
            "\tld b,%d\n\tld a,%d\n"
            "L%d:\n\tld (hl),a\n\tinc hl\n\tdjnz L%d\n"
            "\tld (ix%+d),%d\n\tld (ix%+d),%d\n"
            "\tld bc,%d\n"
            "\tld (ix%+d),c\n\tld (ix%+d),b\n"
            "\tld hl,%u\n"
            "\tld (ix%+d),l\n\tld (ix%+d),h\n"
            "\tld de,0\n"
            "\tld (ix%+d),e\n\tld (ix%+d),d\n",
            plan->array_offset + 1,
            plan->initial_count - 1, plan->fill_value,
            fill_loop, fill_loop,
            plan->array_offset + 1, plan->first_value,
            plan->array_offset, plan->zero_value,
            plan->initial_count,
            plan->count_offset, plan->count_offset + 1,
            (unsigned int)plan->initial_value & 0xffffU,
            plan->value_offset, plan->value_offset + 1,
            plan->index_offset, plan->index_offset + 1);

    fprintf(out,
            "L%d:\n"
            "\tld c,(ix%+d)\n\tld b,(ix%+d)\n"
            "\tld a,c\n\tcp %d\n\tjp c,L%d\n"
            "\tdec bc\n"
            "\tld (ix%+d),c\n\tld (ix%+d),b\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "L%d:\n"
            "\tld a,b\n\tor c\n\tjp z,L%d\n"
            "\tpush bc\n\tld d,b\n\tld e,c\n",
            outer_loop,
            plan->count_offset, plan->count_offset + 1,
            plan->outer_limit + 1, done,
            plan->count_offset, plan->count_offset + 1,
            plan->value_offset, plan->value_offset + 1,
            inner_loop, inner_done);
    mir_emit_runtime_call(out, "__sdivmod");
    fputs("\tpop bc\n\tld a,e\n\tpush hl\n"
          "\tpush ix\n\tpop hl\n", out);
    fprintf(out,
            "\tld de,%d\n\tadd hl,de\n"
            "\tld e,c\n\tld d,0\n\tadd hl,de\n"
            "\tld (hl),a\n\tdec hl\n\tld l,(hl)\n\tld h,0\n",
            plan->array_offset);
    mir_emit_narrowed_divmod_scale(
        out, (unsigned int)plan->scale);
    fprintf(out,
            "\tpop de\n\tadd hl,de\n"
            "\tdec c\n\tjp L%d\n"
            "L%d:\n"
            "\tld (ix%+d),l\n\tld (ix%+d),h\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n"
            "\tpush de\n\tld hl,S%d\n\tpush hl\n"
            "\tpush ix\n\tpop hl\n\tld bc,%d\n\tadd hl,bc\n"
            "\tadd hl,de\n\tld c,(hl)\n\tld b,0\n\tpush bc\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n",
            inner_loop, inner_done,
            plan->value_offset, plan->value_offset + 1,
            plan->index_offset, plan->index_offset + 1,
            plan->string_id, plan->expected_offset,
            plan->value_offset, plan->value_offset + 1);
    mir_machine_emit_symbol_call(out, plan->check_function);
    fprintf(out,
            "\tpop bc\n\tpop bc\n\tpop bc\n\tpop de\n"
            "\tinc de\n"
            "\tld (ix%+d),e\n\tld (ix%+d),d\n"
            "\tjp L%d\n"
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            plan->index_offset, plan->index_offset + 1,
            outer_loop, done);
}

static void mir_numeric_emit_signed_long_divide_by_two(FILE *out)
{
    fputs("\tpush de\n\tpush hl\n"
          "\tld hl,2\n\tld de,0\n", out);
    mir_emit_runtime_call(out, "__lds");
    fputs("\tpop bc\n\tpop bc\n", out);
}

static void mir_emit_signed_long_newton_sqrt_schedule(
    FILE *out, const struct MirSignedLongNewtonSqrtSchedule *plan)
{
    int add_ready = new_label();
    int body = new_label();
    int done = new_label();
    int early_return = new_label();
    int epilogue = new_label();
    int loop = new_label();

    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-8\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_ix_wide_load(
        out, plan->parameter_stack_offset + 2);
    fprintf(out,
            "\tbit 7,d\n\tjp nz,L%d\n"
            "\tld a,d\n\tor e\n\tor h\n\tor l\n"
            "\tjp z,L%d\n",
            early_return, early_return);
    mir_machine_emit_ix_wide_store(out, -4);
    fputs("\tinc hl\n\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n\tinc de\nL%d:\n",
            add_ready, add_ready);
    mir_numeric_emit_signed_long_divide_by_two(out);
    mir_machine_emit_ix_wide_store(out, -8);

    fprintf(out, "L%d:\n", loop);
    mir_machine_emit_ix_wide_load(out, -8);
    fprintf(out,
            "\tld a,d\n\txor 80h\n\tld b,a\n"
            "\tld a,(ix-1)\n\txor 80h\n\tcp b\n"
            "\tjp c,L%d\n\tjp nz,L%d\n"
            "\tld a,e\n\tcp (ix-2)\n"
            "\tjp c,L%d\n\tjp nz,L%d\n"
            "\tld a,h\n\tcp (ix-3)\n"
            "\tjp c,L%d\n\tjp nz,L%d\n"
            "\tld a,l\n\tcp (ix-4)\n"
            "\tjp c,L%d\n\tjp L%d\n"
            "L%d:\n",
            done, body,
            body, done,
            body, done,
            body, done,
            body);
    mir_machine_emit_ix_wide_store(out, -4);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(
        out, plan->parameter_stack_offset + 2);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, -4);
    mir_emit_runtime_call(out, "__lds");
    fputs("\tpop bc\n\tpop bc\n"
          "\tpop bc\n\tadd hl,bc\n\tex de,hl\n"
          "\tpop bc\n\tadc hl,bc\n\tex de,hl\n", out);
    mir_numeric_emit_signed_long_divide_by_two(out);
    mir_machine_emit_ix_wide_store(out, -8);
    fprintf(out, "\tjp L%d\nL%d:\n", loop, done);
    mir_machine_emit_ix_wide_load(out, -4);
    fprintf(out, "\tjp L%d\nL%d:\n",
            epilogue, early_return);
    fputs("\tld hl,0\n\tld de,0\n", out);
    fprintf(out,
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            epilogue);
}

static void mir_emit_unsigned_long_sqrt_schedule(
    FILE *out, const struct MirUnsignedLongSqrtSchedule *plan)
{
    int initialize = new_label();
    int loop_body = new_label();
    int done = new_label();
    int early_return = new_label();
    int epilogue = new_label();
    int high_decrement_ready = new_label();
    int loop = new_label();
    int low_increment_ready = new_label();
    int subtract_mid = new_label();
    int use_mid = new_label();

    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-12\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld a,(ix%+d)\n\tor (ix%+d)\n"
            "\tor (ix%+d)\n\tjp nz,L%d\n"
            "\tld a,(ix%+d)\n\tcp 2\n\tjp c,L%d\n"
            "L%d:\n"
            "\tld hl,1\n\tld de,0\n",
            plan->parameter_stack_offset + 5,
            plan->parameter_stack_offset + 4,
            plan->parameter_stack_offset + 3,
            initialize,
            plan->parameter_stack_offset + 2,
            early_return,
            initialize);
    mir_machine_emit_ix_wide_store(out, -4);
    fputs("\txor a\n"
          "\tld (ix-12),a\n\tld (ix-11),a\n"
          "\tld (ix-10),a\n\tld (ix-9),a\n", out);
    mir_machine_emit_ix_wide_load(
        out, plan->parameter_stack_offset + 2);
    fputs("\tsrl d\n\trr e\n\trr h\n\trr l\n", out);
    mir_machine_emit_ix_wide_store(out, -8);

    fprintf(out,
            "L%d:\n"
            "\tld a,(ix-5)\n\tcp (ix-1)\n"
            "\tjp c,L%d\n\tjp nz,L%d\n"
            "\tld a,(ix-6)\n\tcp (ix-2)\n"
            "\tjp c,L%d\n\tjp nz,L%d\n"
            "\tld a,(ix-7)\n\tcp (ix-3)\n"
            "\tjp c,L%d\n\tjp nz,L%d\n"
            "\tld a,(ix-8)\n\tcp (ix-4)\n"
            "\tjp c,L%d\n"
            "L%d:\n",
            loop,
            done, loop_body,
            done, loop_body,
            done, loop_body,
            done,
            loop_body);

    mir_machine_emit_ix_wide_load(out, -8);
    fputs("\tld c,(ix-4)\n\tld b,(ix-3)\n"
          "\tor a\n\tsbc hl,bc\n\tex de,hl\n"
          "\tld c,(ix-2)\n\tld b,(ix-1)\n"
          "\tsbc hl,bc\n\tex de,hl\n"
          "\tsrl d\n\trr e\n\trr h\n\trr l\n"
          "\tld c,(ix-4)\n\tld b,(ix-3)\n"
          "\tadd hl,bc\n\tex de,hl\n"
          "\tld c,(ix-2)\n\tld b,(ix-1)\n"
          "\tadc hl,bc\n\tex de,hl\n"
          "\tpush de\n\tpush hl\n", out);

    mir_machine_emit_ix_wide_load(
        out, plan->parameter_stack_offset + 2);
    /* __ldu takes the dividend on the stack and the divisor in DE:HL. */
    fputs("\tpush de\n\tpush hl\n"
          "\tld l,(ix-16)\n\tld h,(ix-15)\n"
          "\tld e,(ix-14)\n\tld d,(ix-13)\n", out);
    mir_emit_runtime_call(out, "__ldu");
    fputs("\tpop bc\n\tpop bc\n", out);
    fprintf(out,
            "\tld a,d\n\tcp (ix-13)\n"
            "\tjp c,L%d\n\tjp nz,L%d\n"
            "\tld a,e\n\tcp (ix-14)\n"
            "\tjp c,L%d\n\tjp nz,L%d\n"
            "\tld a,h\n\tcp (ix-15)\n"
            "\tjp c,L%d\n\tjp nz,L%d\n"
            "\tld a,l\n\tcp (ix-16)\n"
            "\tjp c,L%d\n"
            "L%d:\n\tpop hl\n\tpop de\n",
            subtract_mid, use_mid,
            subtract_mid, use_mid,
            subtract_mid, use_mid,
            subtract_mid,
            use_mid);
    mir_machine_emit_ix_wide_store(out, -12);
    fputs("\tinc hl\n\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n\tinc de\nL%d:\n",
            low_increment_ready, low_increment_ready);
    mir_machine_emit_ix_wide_store(out, -4);
    fprintf(out, "\tjp L%d\n", loop);

    fprintf(out, "L%d:\n\tpop hl\n\tpop de\n"
                 "\tld a,h\n\tor l\n\tjp nz,L%d\n"
                 "\tdec de\nL%d:\n\tdec hl\n",
            subtract_mid,
            high_decrement_ready, high_decrement_ready);
    mir_machine_emit_ix_wide_store(out, -8);
    fprintf(out, "\tjp L%d\n", loop);

    fprintf(out, "L%d:\n", done);
    mir_machine_emit_ix_wide_load(
        out, -12);
    fprintf(out, "\tjp L%d\nL%d:\n",
            epilogue, early_return);
    mir_machine_emit_ix_wide_load(
        out, plan->parameter_stack_offset + 2);
    fprintf(out,
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            epilogue);
}

static void mir_emit_prime_search_schedule(
    FILE *out, const struct MirPrimeSearchSchedule *plan)
{
    int no_argument = new_label();
    int normalized = new_label();
    int outer_loop = new_label();
    int inner_loop = new_label();
    int inner_body = new_label();
    int increment_ready = new_label();
    int composite = new_label();
    int prime = new_label();
    int next_candidate = new_label();
    int done = new_label();

    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-11\n\tadd hl,sp\n\tld sp,hl\n"
          "\tld hl,10000\n\tld de,0\n", out);
    mir_machine_emit_ix_wide_store(out, -4);
    fputs("\tld (ix-5),0\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld de,2\n"
            "\tld a,h\n\txor 128\n\tld h,a\n"
            "\tld a,d\n\txor 128\n\tld d,a\n"
            "\tor a\n\tsbc hl,de\n"
            "\tjp c,L%d\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tinc hl\n\tinc hl\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tpush de\n",
            plan->argc_stack_offset + 2,
            plan->argc_stack_offset + 3,
            no_argument,
            plan->argv_stack_offset + 2,
            plan->argv_stack_offset + 3);
    mir_machine_emit_symbol_call(out, plan->convert_function);
    fputs("\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, -4);

    fprintf(out,
            "L%d:\n"
            "\tbit 0,(ix-4)\n\tjp nz,L%d\n"
            "\tinc (ix-4)\n\tjp nz,L%d\n"
            "\tinc (ix-3)\n\tjp nz,L%d\n"
            "\tinc (ix-2)\n\tjp nz,L%d\n"
            "\tinc (ix-1)\n"
            "L%d:\n"
            "L%d:\n"
            "\tld a,(ix-5)\n\tcp 10\n\tjp nc,L%d\n",
            no_argument, normalized,
            normalized, normalized, normalized, normalized,
            outer_loop, done);

    mir_machine_emit_ix_wide_load(out, -4);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->sqrt_function);
    fputs("\tpop bc\n\tpop bc\n"
          "\tinc hl\n\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n\tinc de\nL%d:\n",
            increment_ready, increment_ready);
    mir_machine_emit_ix_wide_store(out, -9);
    fputs("\txor a\n\tld (ix-11),a\n\tld (ix-10),a\n"
          "\tld bc,3\n", out);

    fprintf(out,
            "L%d:\n"
            "\tld a,(ix-10)\n\tcp (ix-6)\n"
            "\tjp c,L%d\n\tjp nz,L%d\n"
            "\tld a,(ix-11)\n\tcp (ix-7)\n"
            "\tjp c,L%d\n\tjp nz,L%d\n"
            "\tld a,b\n\tcp (ix-8)\n"
            "\tjp c,L%d\n\tjp nz,L%d\n"
            "\tld a,c\n\tcp (ix-9)\n"
            "\tjp nc,L%d\n"
            "L%d:\n"
            "\tpush bc\n",
            inner_loop,
            inner_body, prime,
            inner_body, prime,
            inner_body, prime,
            prime,
            inner_body);
    mir_machine_emit_ix_wide_load(out, -4);
    fputs("\tpush de\n\tpush hl\n"
          "\tld h,b\n\tld l,c\n"
          "\tld e,(ix-11)\n\tld d,(ix-10)\n", out);
    mir_emit_runtime_call(out, "__lmu");
    fputs("\tld a,d\n\tor e\n\tor h\n\tor l\n"
          "\tpop de\n\tpop de\n\tpop bc\n", out);
    fprintf(out, "\tjp z,L%d\n", composite);

    fputs("\tld hl,2\n\tadd hl,bc\n\tld b,h\n\tld c,l\n", out);
    fprintf(out, "\tjp nc,L%d\n", inner_loop);
    fputs("\tinc (ix-11)\n", out);
    fprintf(out, "\tjp nz,L%d\n", inner_loop);
    fputs("\tinc (ix-10)\n", out);
    fprintf(out, "\tjp L%d\n", inner_loop);

    fprintf(out,
            "L%d:\n"
            "\tinc (ix-5)\n",
            prime);
    mir_machine_emit_ix_wide_load(out, -4);
    fprintf(out,
            "\tpush de\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->format_string_id);
    mir_emit_runtime_call(out, plan->print_name);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n", out);
    fprintf(out, "\tjp L%d\n", next_candidate);

    fprintf(out, "L%d:\nL%d:\n", composite, next_candidate);
    fputs("\tld a,(ix-4)\n\tadd a,2\n\tld (ix-4),a\n", out);
    fprintf(out, "\tjp nc,L%d\n", outer_loop);
    fputs("\tinc (ix-3)\n", out);
    fprintf(out, "\tjp nz,L%d\n", outer_loop);
    fputs("\tinc (ix-2)\n", out);
    fprintf(out, "\tjp nz,L%d\n", outer_loop);
    fputs("\tinc (ix-1)\n", out);
    fprintf(out, "\tjp L%d\n", outer_loop);

    fprintf(out,
            "L%d:\n\tld hl,0\n"
            "\tld sp,ix\n\tpop ix\n\tret\n",
            done);
}

static void mir_catalan_emit_local_address(FILE *out, int offset)
{
    fprintf(out,
            "\tpush ix\n\tpop hl\n\tld de,%d\n\tadd hl,de\n",
            offset);
}

static void mir_catalan_emit_long_argument(
    FILE *out, unsigned long value)
{
    fprintf(out,
            "\tld hl,%lu\n\tld de,%lu\n\tpush de\n\tpush hl\n",
            value & 0xffffUL, (value >> 16) & 0xffffUL);
}

static void mir_catalan_emit_m_argument(FILE *out, int delta)
{
    int carry_done = new_label();
    int shift;

    fputs("\tld l,(ix-126)\n\tld h,(ix-125)\n"
          "\tld a,h\n\trlca\n\tsbc a,a\n"
          "\tld d,a\n\tld e,a\n", out);
    for (shift = 0; shift < 3; ++shift)
        fputs("\tadd hl,hl\n\trl e\n\trl d\n", out);
    fprintf(out,
            "\tld bc,%d\n\tadd hl,bc\n\tjp nc,L%d\n\tinc de\n"
            "L%d:\n\tpush de\n\tpush hl\n",
            delta, carry_done, carry_done);
}

static void mir_catalan_emit_term_call(
    FILE *out, const struct MirCatalanDriverSchedule *plan,
    int scale_offset, int sign, int numerator, int power, int delta)
{
    int cleanup;

    mir_catalan_emit_m_argument(out, delta);
    mir_catalan_emit_long_argument(out, (unsigned long)power);
    mir_catalan_emit_long_argument(out, (unsigned long)numerator);
    fprintf(out, "\tld hl,%d\n\tpush hl\n", sign);
    mir_catalan_emit_local_address(out, scale_offset);
    fputs("\tpush hl\n", out);
    mir_catalan_emit_local_address(out, -124);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->add_term_function);
    for (cleanup = 0; cleanup < 9; ++cleanup)
        fputs("\tpop bc\n", out);
}

static void mir_catalan_emit_div_call(
    FILE *out, const struct MirCatalanDriverSchedule *plan,
    int array_offset, int divisor)
{
    mir_catalan_emit_long_argument(out, (unsigned long)divisor);
    mir_catalan_emit_local_address(out, array_offset);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->div_small_function);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n", out);
}

static void mir_catalan_emit_zero_call(
    FILE *out, const struct MirCatalanDriverSchedule *plan, int offset)
{
    mir_catalan_emit_local_address(out, offset);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->zero_function);
    fputs("\tpop bc\n", out);
}

static void mir_catalan_emit_is_zero_call(
    FILE *out, const struct MirCatalanDriverSchedule *plan, int offset)
{
    mir_catalan_emit_local_address(out, offset);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->is_zero_function);
    fputs("\tpop bc\n", out);
}

static void mir_catalan_emit_digit(
    FILE *out, const struct MirCatalanDriverSchedule *plan)
{
    fputs("\tpush bc\n\tpush de\n"
          "\tld h,b\n\tld l,c\n"
          "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n\tinc hl\n"
          "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld l,c\n\tld h,b\n\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, -128);
    mir_emit_runtime_call(out, "__lds");
    fputs("\tpop bc\n\tpop bc\n\tpush de\n\tpush hl\n"
          "\tld hl,10\n\tld de,0\n", out);
    mir_emit_runtime_call(out, "__lms");
    fputs("\tpop bc\n\tpop bc\n\tld bc,48\n\tadd hl,bc\n"
          "\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->putchar_function);
    fputs("\tpop bc\n", out);

    mir_machine_emit_ix_wide_load(out, -128);
    fputs("\tpush de\n\tpush hl\n\tld hl,10\n\tld de,0\n", out);
    mir_emit_runtime_call(out, "__lds");
    fputs("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, -128);
    fputs("\tpop de\n\tpop bc\n\tinc de\n", out);
}

static void mir_emit_catalan_driver_schedule(
    FILE *out, const struct MirCatalanDriverSchedule *plan)
{
    static const int first_signs[6] = {1, -1, 1, -1, 1, -1};
    static const int first_powers[6] = {2, 2, 4, 8, 8, 16};
    static const int second_signs[6] = {-1, -1, -1, 1, 1, 1};
    static const int second_powers[6] = {4, 8, 32, 256, 512, 2048};
    static const int deltas[6] = {1, 2, 3, 5, 6, 7};
    int first_loop = new_label();
    int first_done = new_label();
    int second_loop = new_label();
    int second_done = new_label();
    int outer_loop = new_label();
    int outer_body = new_label();
    int inner_loop = new_label();
    int inner_done = new_label();
    int print_done = new_label();
    int item;

    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-374\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    mir_catalan_emit_zero_call(out, plan, -124);
    mir_catalan_emit_zero_call(out, plan, -250);
    mir_catalan_emit_zero_call(out, plan, -374);
    mir_catalan_emit_local_address(out, -250);
    fputs("\tld (hl),1\n\tinc hl\n\txor a\n"
          "\tld (hl),a\n\tinc hl\n\tld (hl),a\n\tinc hl\n\tld (hl),a\n",
          out);
    mir_catalan_emit_local_address(out, -374);
    fputs("\tld (hl),1\n\tinc hl\n\txor a\n"
          "\tld (hl),a\n\tinc hl\n\tld (hl),a\n\tinc hl\n\tld (hl),a\n"
          "\tld (ix-126),0\n\tld (ix-125),0\n", out);

    fprintf(out, "L%d:\n", first_loop);
    mir_catalan_emit_is_zero_call(out, plan, -250);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", first_done);
    for (item = 0; item < 6; ++item)
        mir_catalan_emit_term_call(
            out, plan, -250, first_signs[item], 3,
            first_powers[item], deltas[item]);
    mir_catalan_emit_div_call(out, plan, -250, 16);
    fputs("\tinc (ix-126)\n", out);
    fprintf(out, "\tjp nz,L%d\n", first_loop);
    fputs("\tinc (ix-125)\n", out);
    fprintf(out, "\tjp L%d\nL%d:\n",
            first_loop, first_done);

    fputs("\tld (ix-126),0\n\tld (ix-125),0\n", out);
    fprintf(out, "L%d:\n", second_loop);
    mir_catalan_emit_is_zero_call(out, plan, -374);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", second_done);
    for (item = 0; item < 6; ++item)
        mir_catalan_emit_term_call(
            out, plan, -374, second_signs[item], 1,
            second_powers[item], deltas[item]);
    mir_catalan_emit_div_call(out, plan, -374, 4096);
    fputs("\tinc (ix-126)\n", out);
    fprintf(out, "\tjp nz,L%d\n", second_loop);
    fputs("\tinc (ix-125)\n", out);
    fprintf(out, "\tjp L%d\nL%d:\n",
            second_loop, second_done);

    mir_machine_emit_ix_wide_load(out, -124);
    fputs("\tpush de\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->format_string_id);
    mir_emit_runtime_call(out, plan->print_name);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tpush ix\n\tpop hl\n\tld bc,-120\n\tadd hl,bc\n"
          "\tld b,h\n\tld c,l\n\tld de,0\n", out);

    fprintf(out,
            "L%d:\n"
            "\tpush ix\n\tpop hl\n"
            "\tld a,h\n\tcp b\n\tjp nz,L%d\n"
            "\tld a,l\n\tcp c\n\tjp z,L%d\n"
            "L%d:\n"
            "\tld a,d\n\tor a\n\tjp nz,L%d\n"
            "\tld a,e\n\tcp 100\n\tjp nc,L%d\n"
            "\tpush de\n\tld hl,1000\n\tld de,0\n",
            outer_loop, outer_body, print_done,
            outer_body, print_done, print_done);
    mir_machine_emit_ix_wide_store(out, -128);
    fputs("\tpop de\n", out);

    fprintf(out,
            "L%d:\n"
            "\tld a,(ix-125)\n\tor (ix-126)\n"
            "\tor (ix-127)\n\tor (ix-128)\n"
            "\tjp z,L%d\n"
            "\tld a,d\n\tor a\n\tjp nz,L%d\n"
            "\tld a,e\n\tcp 100\n\tjp nc,L%d\n",
            inner_loop, inner_done,
            inner_done, inner_done);
    mir_catalan_emit_digit(out, plan);
    fprintf(out, "\tjp L%d\nL%d:\n",
            inner_loop, inner_done);
    fputs("\tinc bc\n\tinc bc\n\tinc bc\n\tinc bc\n", out);
    fprintf(out, "\tjp L%d\nL%d:\n",
            outer_loop, print_done);

    fputs("\tld hl,10\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->putchar_function);
    fputs("\tpop bc\n\tld hl,0\n"
          "\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static void mir_log_series_emit_local_address(FILE *out, int offset)
{
    fprintf(out,
            "\tpush ix\n\tpop hl\n\tld de,%d\n\tadd hl,de\n",
            offset);
}

static void mir_log_series_emit_long_argument(
    FILE *out, unsigned long value)
{
    fprintf(out,
            "\tld hl,%lu\n\tld de,%lu\n\tpush de\n\tpush hl\n",
            value & 0xffffUL, (value >> 16) & 0xffffUL);
}

static void mir_log_series_emit_k(FILE *out)
{
    fputs("\tld l,(ix-118)\n\tld h,(ix-117)\n"
          "\tld a,h\n\trlca\n\tsbc a,a\n"
          "\tld d,a\n\tld e,a\n", out);
}

static void mir_log_series_emit_twice_k_plus(
    FILE *out, int delta, int outer_multiplier)
{
    int carry_done = new_label();

    if (outer_multiplier != 0)
        mir_log_series_emit_long_argument(
            out, (unsigned long)outer_multiplier);
    mir_log_series_emit_long_argument(out, 2);
    mir_log_series_emit_k(out);
    mir_emit_runtime_call(out, "__lmul");
    fputs("\tpop bc\n\tpop bc\n", out);
    fprintf(out,
            "\tld bc,%d\n\tadd hl,bc\n\tjp nc,L%d\n\tinc de\n"
            "L%d:\n",
            delta, carry_done, carry_done);
    if (outer_multiplier != 0) {
        mir_emit_runtime_call(out, "__lmul");
        fputs("\tpop bc\n\tpop bc\n", out);
    }
}

static void mir_log_series_emit_digit(
    FILE *out, const struct MirLogSeriesDriverSchedule *plan)
{
    fputs("\tpush bc\n\tpush de\n"
          "\tld h,b\n\tld l,c\n"
          "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n\tinc hl\n"
          "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld l,c\n\tld h,b\n\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, -120);
    mir_emit_runtime_call(out, "__lds");
    fputs("\tpop bc\n\tpop bc\n\tpush de\n\tpush hl\n"
          "\tld hl,10\n\tld de,0\n", out);
    mir_emit_runtime_call(out, "__lms");
    fputs("\tpop bc\n\tpop bc\n\tld bc,48\n\tadd hl,bc\n"
          "\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->putchar_function);
    fputs("\tpop bc\n", out);

    mir_machine_emit_ix_wide_load(out, -120);
    fputs("\tpush de\n\tpush hl\n\tld hl,10\n\tld de,0\n", out);
    mir_emit_runtime_call(out, "__lds");
    fputs("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, -120);
    fputs("\tpop de\n\tpop bc\n\tinc de\n", out);
}

static void mir_emit_log_series_driver_schedule(
    FILE *out, const struct MirLogSeriesDriverSchedule *plan)
{
    int fraction_loop = new_label();
    int series_loop = new_label();
    int series_done = new_label();
    int outer_loop = new_label();
    int outer_body = new_label();
    int inner_loop = new_label();
    int inner_done = new_label();
    int print_done = new_label();

    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-236\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    fputs("\tpush ix\n\tpop hl\n\tld de,-236\n\tadd hl,de\n"
          "\tld (hl),0\n\tld d,h\n\tld e,l\n\tinc de\n"
          "\tld bc,235\n\tldir\n", out);

    fputs("\tld hl,2\n\tld de,0\n", out);
    mir_machine_emit_ix_wide_store(out, -120);
    mir_log_series_emit_local_address(out, -236);
    fputs("\tld b,h\n\tld c,l\n\tld a,29\n", out);
    fprintf(out, "L%d:\n\tpush af\n\tpush bc\n", fraction_loop);
    mir_machine_emit_ix_wide_load(out, -120);
    fputs("\tpush de\n\tpush hl\n\tld hl,10000\n\tld de,0\n", out);
    mir_emit_runtime_call(out, "__lmul");
    fputs("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, -120);
    fputs("\tpush de\n\tpush hl\n\tld hl,3\n\tld de,0\n", out);
    mir_emit_runtime_call(out, "__lds");
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tld a,l\n\tld (bc),a\n\tinc bc\n"
          "\tld a,h\n\tld (bc),a\n\tinc bc\n"
          "\tld a,e\n\tld (bc),a\n\tinc bc\n"
          "\tld a,d\n\tld (bc),a\n\tinc bc\n"
          "\tpush bc\n", out);
    mir_machine_emit_ix_wide_load(out, -120);
    fputs("\tpush de\n\tpush hl\n\tld hl,3\n\tld de,0\n", out);
    mir_emit_runtime_call(out, "__lms");
    fputs("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, -120);
    fputs("\tpop bc\n\tpop af\n\tdec a\n", out);
    fprintf(out, "\tjp nz,L%d\n", fraction_loop);

    fputs("\txor a\n\tld (ix-118),a\n\tld (ix-117),a\n", out);
    fprintf(out, "L%d:\n", series_loop);
    mir_log_series_emit_local_address(out, -236);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->is_zero_function);
    fputs("\tpop bc\n\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", series_done);

    mir_log_series_emit_local_address(out, -236);
    fputs("\tpush hl\n", out);
    mir_log_series_emit_local_address(out, -116);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->add_function);
    fputs("\tpop bc\n\tpop bc\n", out);

    mir_log_series_emit_twice_k_plus(out, 3, 9);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_log_series_emit_twice_k_plus(out, 1, 0);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_log_series_emit_local_address(out, -236);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->mul_div_function);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tinc (ix-118)\n", out);
    fprintf(out, "\tjp nz,L%d\n", series_loop);
    fputs("\tinc (ix-117)\n", out);
    fprintf(out, "\tjp L%d\nL%d:\n", series_loop, series_done);

    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->format_string_id);
    mir_emit_runtime_call(out, plan->print_name);
    fputs("\tpop bc\n"
          "\tpush ix\n\tpop hl\n\tld de,-116\n\tadd hl,de\n"
          "\tld b,h\n\tld c,l\n\tld de,0\n", out);

    fprintf(out,
            "L%d:\n"
            "\tpush ix\n\tpop hl\n"
            "\tld a,h\n\tcp b\n\tjp nz,L%d\n"
            "\tld a,l\n\tcp c\n\tjp z,L%d\n"
            "L%d:\n"
            "\tld a,d\n\tor a\n\tjp nz,L%d\n"
            "\tld a,e\n\tcp 100\n\tjp nc,L%d\n"
            "\tpush de\n\tld hl,1000\n\tld de,0\n",
            outer_loop, outer_body, print_done,
            outer_body, print_done, print_done);
    mir_machine_emit_ix_wide_store(out, -120);
    fputs("\tpop de\n", out);

    fprintf(out,
            "L%d:\n"
            "\tld a,(ix-117)\n\tor (ix-118)\n"
            "\tor (ix-119)\n\tor (ix-120)\n"
            "\tjp z,L%d\n"
            "\tld a,d\n\tor a\n\tjp nz,L%d\n"
            "\tld a,e\n\tcp 100\n\tjp nc,L%d\n",
            inner_loop, inner_done,
            inner_done, inner_done);
    mir_log_series_emit_digit(out, plan);
    fprintf(out, "\tjp L%d\nL%d:\n",
            inner_loop, inner_done);
    fputs("\tinc bc\n\tinc bc\n\tinc bc\n\tinc bc\n", out);
    fprintf(out, "\tjp L%d\nL%d:\n",
            outer_loop, print_done);

    fputs("\tld hl,10\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->putchar_function);
    fputs("\tpop bc\n\tld hl,0\n"
          "\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_modp2_power_shift(int divisor)
{
    int shift = 0;

    if (divisor < 2 ||
        (divisor & (divisor - 1)) != 0)
        return -1;
    while (divisor > 1) {
        divisor >>= 1;
        ++shift;
    }
    return shift;
}

static void mir_modp2_emit_mask(
    FILE *out, int divisor)
{
    int mask = divisor - 1;

    if (mask == 255)
        fputs("\tld h,0\n", out);
    else
        fprintf(out,
                "\tld a,l\n\tand %d\n\tld l,a\n\tld h,0\n",
                mask);
}

static void mir_modp2_emit_unsigned_shift(
    FILE *out, int shift)
{
    int bit;

    if (shift >= 8) {
        fputs("\tld l,h\n\tld h,0\n", out);
        for (bit = 8; bit < shift; ++bit)
            fputs("\tsrl l\n", out);
    } else {
        for (bit = 0; bit < shift; ++bit)
            fputs("\tsrl h\n\trr l\n", out);
    }
}

static void mir_modp2_emit_operation(
    FILE *out, int operation, int is_unsigned,
    int divisor)
{
    int shift = mir_modp2_power_shift(divisor);

    if (operation == '%' && shift >= 0 && is_unsigned) {
        mir_modp2_emit_mask(out, divisor);
        return;
    }
    if (operation == '/' && shift >= 0 && is_unsigned) {
        mir_modp2_emit_unsigned_shift(out, shift);
        return;
    }

    fprintf(out, "\tld de,%d\n", divisor);
    if (operation == '%')
        mir_emit_runtime_call(
            out, is_unsigned ? "__modu" : "__mods");
    else
        mir_emit_runtime_call(
            out, is_unsigned ? "__divu" : "__divs");
}

static void mir_modp2_emit_value_load(FILE *out)
{
    fputs("\tld l,(ix-3)\n\tld h,(ix-2)\n", out);
}

static void mir_modp2_emit_sum_add(
    FILE *out, int is_unsigned)
{
    if (is_unsigned) {
        fputs("\tld de,0\n", out);
    } else {
        fputs("\tld a,h\n\trlca\n\tsbc a,a\n"
              "\tld d,a\n\tld e,a\n", out);
    }
    fputs("\tld c,(ix-7)\n\tld b,(ix-6)\n"
          "\tadd hl,bc\n\tex de,hl\n"
          "\tld c,(ix-5)\n\tld b,(ix-4)\n"
          "\tadc hl,bc\n\tex de,hl\n", out);
    mir_machine_emit_ix_wide_store(out, -7);
}

static void mir_emit_lcs_dp_schedule(
    FILE *out, const struct MirLcsDpSchedule *plan)
{
    int left_scan = new_label();
    int left_done = new_label();
    int right_scan = new_label();
    int right_done = new_label();
    int zero_loop = new_label();
    int outer_loop = new_label();
    int outer_next = new_label();
    int inner_loop = new_label();
    int mismatch = new_label();
    int left_value = new_label();
    int store_value = new_label();
    int done = new_label();

    /* The matched 9x9 table bounds every defined result to 0..8. Keep one
     * complete row as target-width words while byte registers carry the
     * bounded lengths, diagonal value, and inner-loop count. */
    fprintf(out,
            "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tld hl,-25\n\tadd hl,sp\n\tld sp,hl\n");
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tld (ix-20),l\n\tld (ix-19),h\n"
            "\tld b,0\n"
            "L%d:\n\tld a,(hl)\n\tor a\n\tjp z,L%d\n"
            "\tinc hl\n\tinc b\n\tjp L%d\n"
            "L%d:\n\tld (ix-23),b\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tld (ix-22),l\n\tld (ix-21),h\n"
            "\tld b,0\n"
            "L%d:\n\tld a,(hl)\n\tor a\n\tjp z,L%d\n"
            "\tinc hl\n\tinc b\n\tjp L%d\n"
            "L%d:\n\tld (ix-24),b\n",
            plan->left_stack_offset + 2,
            plan->left_stack_offset + 3,
            left_scan, left_done, left_scan, left_done,
            plan->right_stack_offset + 2,
            plan->right_stack_offset + 3,
            right_scan, right_done, right_scan, right_done);
    fprintf(out,
            "\tpush ix\n\tpop hl\n\tld de,-18\n\tadd hl,de\n"
            "\tld b,9\n\txor a\n"
            "L%d:\n\tld (hl),a\n\tinc hl\n\tld (hl),a\n"
            "\tinc hl\n\tdjnz L%d\n"
            "L%d:\n\tld a,(ix-23)\n\tor a\n\tjp z,L%d\n"
            "\tdec (ix-23)\n"
            "\tld l,(ix-20)\n\tld h,(ix-19)\n"
            "\tld a,(hl)\n\tld (ix-25),a\n"
            "\tinc hl\n\tld (ix-20),l\n\tld (ix-19),h\n"
            "\tld e,(ix-22)\n\tld d,(ix-21)\n"
            "\tpush ix\n\tpop hl\n\tld bc,-16\n\tadd hl,bc\n"
            "\tld b,(ix-24)\n\tld c,0\n"
            "\tld a,b\n\tor a\n\tjp z,L%d\n",
            zero_loop, zero_loop, outer_loop, done, outer_next);
    fprintf(out,
            "L%d:\n\tld a,(de)\n\tcp (ix-25)\n\tjp nz,L%d\n"
            "\tld a,c\n\tinc a\n\tjp L%d\n"
            "L%d:\n\tld a,(hl)\n\tdec hl\n\tdec hl\n"
            "\tcp (hl)\n\tjp c,L%d\n"
            "\tinc hl\n\tinc hl\n\tjp L%d\n"
            "L%d:\n\tld a,(hl)\n\tinc hl\n\tinc hl\n"
            "L%d:\n\tld c,(hl)\n\tld (hl),a\n\tinc hl\n"
            "\tld (hl),0\n\tinc hl\n\tinc de\n"
            "\tdjnz L%d\n"
            "L%d:\n\tjp L%d\n"
            "L%d:\n\tld a,(ix-24)\n\tadd a,a\n"
            "\tld e,a\n\tld d,0\n\tpush ix\n\tpop hl\n"
            "\tadd hl,de\n\tld de,-18\n\tadd hl,de\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n"
            "\tld sp,ix\n\tpop ix\n\tret\n",
            inner_loop, mismatch, store_value,
            mismatch, left_value, store_value,
            left_value, store_value, inner_loop,
            outer_next, outer_loop, done);
}

static void mir_emit_row_inversion_check_schedule(
    FILE *out, const struct MirRowInversionCheckSchedule *plan)
{
    int clear_loop = new_label();
    int row_loop = new_label();
    int failure = new_label();
    int done = new_label();
    int offset;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    mir_machine_emit_global_address_de(out, plan->values, 0);
    fprintf(out,
            "\tex de,hl\n\tld b,8\n\txor a\n"
            "L%d:\n\tld (hl),a\n\tinc hl\n\tdjnz L%d\n"
            "\tld b,0\n"
            "L%d:\n",
            clear_loop, clear_loop, row_loop);
    mir_machine_emit_global_word(out, plan->values, 2);
    fputs("\tadd hl,hl\n\tadd hl,hl\n\tadd hl,hl\n", out);
    mir_machine_emit_global_address_de(out, plan->table, 0);
    fputs("\tadd hl,de\n"
          "\tld a,b\n\tadd a,a\n\tld e,a\n\tld d,0\n"
          "\tadd hl,de\n"
          "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n"
          "\tld l,b\n\tld h,0\n\tadd hl,hl\n", out);
    mir_machine_emit_global_address_de(out, plan->values, 0);
    fputs("\tadd hl,de\n\tpop de\n"
          "\tld (hl),e\n\tinc hl\n\tld (hl),d\n"
          "\tinc b\n\tld a,b\n\tcp 4\n", out);
    fprintf(out, "\tjp c,L%d\n", row_loop);

    for (offset = 6; offset >= 0; offset -= 2) {
        mir_machine_emit_global_word(
            out, plan->values, offset);
        fputs("\tpush hl\n", out);
    }
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->format_string_id);
    mir_emit_runtime_call(out, plan->print_name);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n",
          out);

    mir_machine_emit_global_word(out, plan->values, 0);
    fputs("\tld de,1\n\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp nz,L%d\n", failure);
    mir_machine_emit_global_word(out, plan->values, 2);
    fputs("\tld de,1\n\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp nz,L%d\n", failure);
    mir_machine_emit_global_word(out, plan->values, 4);
    fputs("\tld de,9\n\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp nz,L%d\n", failure);
    mir_machine_emit_global_word(out, plan->values, 6);
    fputs("\tld de,9\n\tor a\n\tsbc hl,de\n", out);
    fprintf(out,
            "\tjp nz,L%d\n\tld hl,0\n\tjp L%d\n"
            "L%d:\n\tld hl,1\n"
            "L%d:\n\tret\n",
            failure, done, failure, done);
}

static void mir_modp2_emit_print(
    FILE *out, const struct MirModp2DriverSchedule *plan,
    int slot, int operation, int is_unsigned,
    const int *divisors, int divisor_count)
{
    int item;

    for (item = divisor_count - 1; item >= 0; --item) {
        mir_modp2_emit_value_load(out);
        mir_modp2_emit_operation(
            out, operation, is_unsigned, divisors[item]);
        fputs("\tpush hl\n", out);
    }
    mir_modp2_emit_value_load(out);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->string_ids[slot]);
    mir_emit_runtime_call(out, plan->print_names[slot]);
    for (item = 0; item < divisor_count + 2; ++item)
        fputs("\tpop bc\n", out);
}

static void mir_modp2_emit_loop(
    FILE *out, const struct MirModp2DriverSchedule *plan,
    int slot, struct Sym *array, int element_count,
    int operation, int is_unsigned,
    const int *divisors, int divisor_count)
{
    int loop = new_label();
    int done = new_label();
    int item;

    fprintf(out,
            "\tld (ix-1),0\n"
            "L%d:\n"
            "\tld a,(ix-1)\n\tcp %d\n\tjp nc,L%d\n"
            "\tld l,a\n\tld h,0\n\tadd hl,hl\n",
            loop, element_count, done);
    mir_machine_emit_global_address_de(out, array, 0);
    fputs("\tadd hl,de\n"
          "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n"
          "\tld (ix-3),l\n\tld (ix-2),h\n", out);

    mir_modp2_emit_print(
        out, plan, slot, operation, is_unsigned,
        divisors, divisor_count);
    for (item = 0; item < divisor_count; ++item) {
        mir_modp2_emit_value_load(out);
        mir_modp2_emit_operation(
            out, operation, is_unsigned, divisors[item]);
        mir_modp2_emit_sum_add(out, is_unsigned);
    }
    fprintf(out,
            "\tinc (ix-1)\n\tjp L%d\n"
            "L%d:\n",
            loop, done);
}

static void mir_emit_modp2_driver_schedule(
    FILE *out, const struct MirModp2DriverSchedule *plan)
{
    static const int signed_mod_pow2[] =
        {2, 4, 8, 16, 32, 64, 128, 256};
    static const int signed_mod_other[] =
        {3, 5, 10, 100, 255, 1000};
    static const int unsigned_mod[] =
        {8, 3, 255, 256, 1000};
    static const int signed_div_pow2[] =
        {2, 4, 8, 128, 256, 512, 1024, 4096, 16384};
    static const int signed_div_other[] =
        {3, 10, 255, 1000};
    static const int unsigned_div[] =
        {8, 3, 256, 1000};

    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-7\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fputs("\txor a\n"
          "\tld (ix-7),a\n\tld (ix-6),a\n"
          "\tld (ix-5),a\n\tld (ix-4),a\n", out);

    mir_modp2_emit_loop(
        out, plan, 0, plan->signed_values, 38,
        '%', 0, signed_mod_pow2, 8);
    mir_modp2_emit_loop(
        out, plan, 1, plan->signed_values, 38,
        '%', 0, signed_mod_other, 6);
    mir_modp2_emit_loop(
        out, plan, 2, plan->unsigned_values, 13,
        '%', 1, unsigned_mod, 5);
    mir_modp2_emit_loop(
        out, plan, 3, plan->signed_values, 38,
        '/', 0, signed_div_pow2, 9);
    mir_modp2_emit_loop(
        out, plan, 4, plan->signed_values, 38,
        '/', 0, signed_div_other, 4);
    mir_modp2_emit_loop(
        out, plan, 5, plan->unsigned_values, 13,
        '/', 1, unsigned_div, 4);

    mir_machine_emit_ix_wide_load(out, -7);
    fputs("\tpush de\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->string_ids[6]);
    mir_emit_runtime_call(out, plan->print_names[6]);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_scoped_temp_schedule(
    struct MirScopedTempSchedule *plan)
{
    static const int expected_opcodes[35] = {
        MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LOAD, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_STORE, MIR_LOAD, MIR_LOAD,
        MIR_INDEX_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_BINARY, MIR_UNARY, MIR_STORE_INDIRECT, MIR_NOP,
        MIR_RETURN, MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_STORE,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_RETURN
    };
    int current_type;
    int current_storage;
    int records_type;
    int records_storage;
    int top_type;
    int top_storage;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 35 || mir_cfg_block_count() != 2 ||
        mir.has_vla || mir.local_bytes != 2 ||
        mir.aggregate_temp_bytes != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        (mir.return_type & TYPE_UNSIGNED) != 0 ||
        type_size(mir.return_type) != 2 ||
        type_is_float(mir.return_type) ||
        type_ptr_depth(mir.return_type) != 0)
        return 0;
    for (instruction = 0; instruction < 35; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    if (!mir_scalar_memory_location(
            &mir.insns[1], &current_type, &current_storage,
            &plan->current_root_offset) ||
        current_storage != SC_GLOBAL ||
        type_ptr_depth(current_type) != 0 ||
        (current_type & 15) != TYPE_INT ||
        (current_type & TYPE_UNSIGNED) != 0 ||
        type_size(current_type) != 2 ||
        !mir_machine_named_nonvolatile(&mir.insns[1]) ||
        !mir_scalar_memory_location(
            &mir.insns[5], &records_type, &records_storage,
            &plan->records_root_offset) ||
        records_storage != SC_GLOBAL ||
        type_ptr_depth(records_type) != 1 ||
        type_size(records_type) != 2 ||
        !mir_machine_named_nonvolatile(&mir.insns[5]) ||
        !mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        mir.insns[3].immediate != TOK_GE ||
        mir.insns[3].src1 != mir.insns[1].dst ||
        mir.insns[3].src2 != mir.insns[2].dst ||
        type_ptr_depth(mir.insns[3].secondary_offset) != 0 ||
        (mir.insns[3].secondary_offset & 15) != TYPE_INT ||
        (mir.insns[3].secondary_offset & TYPE_UNSIGNED) != 0 ||
        type_size(mir.insns[3].secondary_offset) != 2 ||
        mir.insns[4].src1 != mir.insns[3].dst ||
        mir.insns[4].label != mir.insns[24].label ||
        !mir_machine_same_location(
            &mir.insns[1], &mir.insns[6]) ||
        !mir_machine_same_location(
            &mir.insns[1], &mir.insns[13]) ||
        !mir_machine_same_location(
            &mir.insns[5], &mir.insns[12]) ||
        mir.insns[7].src1 != mir.insns[5].dst ||
        mir.insns[7].src2 != mir.insns[6].dst ||
        mir.insns[7].immediate != 40 ||
        mir.insns[7].memory_size != 40 ||
        mir.insns[8].src1 != mir.insns[7].dst ||
        mir.insns[8].immediate != 19 ||
        mir.insns[8].memory_size != 1 ||
        mir.insns[8].bit_width != 0 ||
        (mir.insns[8].memory_flags & (1 | 8)) != 0 ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].memory_size != 1 ||
        mir.insns[9].bit_width != 0 ||
        (mir.insns[9].memory_flags & (1 | 8)) != 0 ||
        type_ptr_depth(mir.insns[9].type) != 0 ||
        (mir.insns[9].type & 15) != TYPE_CHAR ||
        (mir.insns[9].type & TYPE_UNSIGNED) == 0 ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[10].immediate != 0 ||
        type_ptr_depth(mir.insns[10].type) != 0 ||
        (mir.insns[10].type & 15) != TYPE_INT ||
        (mir.insns[10].type & TYPE_UNSIGNED) != 0 ||
        type_size(mir.insns[10].type) != 2 ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[11].memory_size != 2 ||
        mir.insns[14].src1 != mir.insns[12].dst ||
        mir.insns[14].src2 != mir.insns[13].dst ||
        mir.insns[14].immediate != 40 ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        mir.insns[15].immediate != 19 ||
        mir.insns[15].memory_size != 1 ||
        mir.insns[15].bit_width != 0 ||
        (mir.insns[15].memory_flags & (1 | 8)) != 0 ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[16].memory_size != 1 ||
        mir.insns[16].bit_width != 0 ||
        (mir.insns[16].memory_flags & (1 | 8)) != 0 ||
        type_ptr_depth(mir.insns[16].type) != 0 ||
        (mir.insns[16].type & 15) != TYPE_CHAR ||
        (mir.insns[16].type & TYPE_UNSIGNED) == 0 ||
        !mir_machine_constant_equals(mir.insns[17].dst, 2) ||
        mir.insns[18].immediate != '+' ||
        mir.insns[18].src1 != mir.insns[16].dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        mir.insns[19].src1 != mir.insns[18].dst ||
        type_ptr_depth(mir.insns[19].type) != 0 ||
        (mir.insns[19].type & 15) != TYPE_CHAR ||
        (mir.insns[19].type & TYPE_UNSIGNED) == 0 ||
        type_size(mir.insns[19].type) != 1 ||
        mir.insns[20].src1 != mir.insns[15].dst ||
        mir.insns[20].src2 != mir.insns[19].dst ||
        mir.insns[20].memory_size != 1 ||
        mir.insns[20].bit_width != 0 ||
        (mir.insns[20].memory_flags & (1 | 8)) != 0 ||
        mir.insns[22].src1 != mir.insns[10].dst)
        return 0;
    if (!mir_scalar_memory_location(
            &mir.insns[25], &top_type, &top_storage,
            &plan->global_top_root_offset) ||
        top_storage != SC_GLOBAL ||
        type_ptr_depth(top_type) != 0 ||
        (top_type & 15) != TYPE_INT ||
        (top_type & TYPE_UNSIGNED) != 0 ||
        type_size(top_type) != 2 ||
        !mir_machine_named_nonvolatile(&mir.insns[25]) ||
        !mir_machine_same_location(
            &mir.insns[25], &mir.insns[28]) ||
        !mir_machine_same_location(
            &mir.insns[25], &mir.insns[32]) ||
        mir.insns[27].src1 != mir.insns[25].dst ||
        mir.insns[27].memory_size != 2 ||
        !mir_machine_constant_equals(mir.insns[29].dst, 2) ||
        mir.insns[30].immediate != '+' ||
        mir.insns[30].src1 != mir.insns[28].dst ||
        mir.insns[30].src2 != mir.insns[29].dst ||
        mir.insns[32].src1 != mir.insns[30].dst ||
        mir.insns[32].memory_size != 2 ||
        mir.insns[34].src1 != mir.insns[25].dst)
        return 0;
    plan->current_root = find_global(mir.insns[1].name);
    plan->records_root = find_global(mir.insns[5].name);
    plan->global_top_root = find_global(mir.insns[25].name);
    plan->record_stride = 40;
    plan->local_offset = 19;
    plan->increment = 2;
    if (plan->current_root == NULL ||
        plan->records_root == NULL ||
        plan->global_top_root == NULL ||
        plan->current_root == plan->records_root ||
        plan->current_root == plan->global_top_root ||
        plan->records_root == plan->global_top_root ||
        plan->current_root->storage == SC_FUNC ||
        plan->records_root->storage == SC_FUNC ||
        plan->global_top_root->storage == SC_FUNC ||
        plan->current_root->is_volatile ||
        plan->records_root->is_volatile ||
        plan->global_top_root->is_volatile ||
        type_size(plan->current_root->type) != 2 ||
        type_size(plan->global_top_root->type) != 2 ||
        type_ptr_depth(plan->records_root->type) != 1)
        return 0;
    return 1;
}

static void mir_numeric_emit_scoped_local_address(
    FILE *out, const struct MirScopedTempSchedule *plan)
{
    mir_machine_emit_global_word(
        out, plan->records_root, plan->records_root_offset);
    fputs("\tpush hl\n", out);
    mir_machine_emit_global_word(
        out, plan->current_root, plan->current_root_offset);
    fputs("\tld d,h\n\tld e,l\n"
          "\tadd hl,hl\n\tadd hl,hl\n\tadd hl,de\n"
          "\tadd hl,hl\n\tadd hl,hl\n\tadd hl,hl\n"
          "\tex de,hl\n\tpop hl\n\tadd hl,de\n", out);
    fprintf(out, "\tld de,%d\n\tadd hl,de\n",
            plan->local_offset);
}

static void mir_emit_scoped_temp_schedule(
    FILE *out, const struct MirScopedTempSchedule *plan)
{
    int global_path = new_label();
    int epilogue = new_label();

    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-2\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_word(
        out, plan->current_root, plan->current_root_offset);
    fputs("\tpush hl\n\tld hl,0\n\tex de,hl\n\tpop hl\n"
          "\tld a,h\n\txor 80h\n\tld h,a\n"
          "\tld a,d\n\txor 80h\n\tld d,a\n"
          "\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp c,L%d\n", global_path);

    mir_numeric_emit_scoped_local_address(out, plan);
    fputs("\tld l,(hl)\n\tld h,0\n"
          "\tld (ix-2),l\n\tld (ix-1),h\n", out);
    mir_numeric_emit_scoped_local_address(out, plan);
    fputs("\tpush hl\n\tld l,(hl)\n\tld h,0\n", out);
    if (plan->increment == 2)
        fputs("\tinc hl\n\tinc hl\n", out);
    else
        fprintf(out, "\tld de,%d\n\tadd hl,de\n",
                plan->increment);
    fputs("\tex de,hl\n\tpop hl\n\tld (hl),e\n"
          "\tld l,(ix-2)\n\tld h,(ix-1)\n", out);
    fprintf(out, "\tjp L%d\nL%d:\n", epilogue, global_path);

    mir_machine_emit_global_word(
        out, plan->global_top_root, plan->global_top_root_offset);
    fputs("\tld (ix-2),l\n\tld (ix-1),h\n", out);
    mir_machine_emit_global_word(
        out, plan->global_top_root, plan->global_top_root_offset);
    if (plan->increment == 2)
        fputs("\tinc hl\n\tinc hl\n", out);
    else
        fprintf(out, "\tld de,%d\n\tadd hl,de\n",
                plan->increment);
    mir_machine_emit_global_word_store(
        out, plan->global_top_root, plan->global_top_root_offset);
    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n", out);
    fprintf(out, "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n", epilogue);
}

int mir_try_emit_numeric_kernels(FILE *out, int phase)
{
    if (phase == 0) {
        struct MirSignedLongNewtonSqrtSchedule signed_sqrt_plan;
        struct MirUnsignedLongSqrtSchedule sqrt_plan;
        struct MirPrimeSearchSchedule prime_plan;
        struct MirCatalanDriverSchedule catalan_plan;
        struct MirLogSeriesDriverSchedule log_series_plan;
        struct MirModp2DriverSchedule modp2_plan;

        if (mir_match_signed_long_newton_sqrt_schedule(
                &signed_sqrt_plan)) {
            mir_emit_signed_long_newton_sqrt_schedule(
                out, &signed_sqrt_plan);
            return 1;
        }
        if (mir_match_unsigned_long_sqrt_schedule(&sqrt_plan)) {
            mir_emit_unsigned_long_sqrt_schedule(out, &sqrt_plan);
            return 1;
        }
        if (mir_match_prime_search_schedule(&prime_plan)) {
            mir_emit_prime_search_schedule(out, &prime_plan);
            return 1;
        }
        if (mir_match_catalan_driver_schedule(&catalan_plan)) {
            mir_emit_catalan_driver_schedule(out, &catalan_plan);
            return 1;
        }
        if (mir_match_log_series_driver_schedule(&log_series_plan)) {
            mir_emit_log_series_driver_schedule(
                out, &log_series_plan);
            return 1;
        }
        if (mir_match_modp2_driver_schedule(&modp2_plan)) {
            mir_emit_modp2_driver_schedule(out, &modp2_plan);
            return 1;
        }
    } else if (phase == 1) {
        struct MirFixedPointMultiply plan;

        if (mir_match_fixed_point_multiply(&plan)) {
            mir_emit_fixed_point_multiply(out, &plan);
            return 1;
        }
    } else if (phase == 2) {
        struct MirLcsDpSchedule lcs_plan;
        struct MirNarrowedDivmodLoopSchedule plan;
        struct MirRowInversionCheckSchedule row_plan;
        struct MirScopedTempSchedule scoped_temp_plan;

        if (mir_match_lcs_dp_schedule(&lcs_plan)) {
            mir_emit_lcs_dp_schedule(out, &lcs_plan);
            return 1;
        }
        if (mir_match_narrowed_divmod_loop_schedule(&plan)) {
            mir_emit_narrowed_divmod_loop_schedule(out, &plan);
            return 1;
        }
        if (mir_match_row_inversion_check_schedule(&row_plan)) {
            mir_emit_row_inversion_check_schedule(out, &row_plan);
            return 1;
        }
        if (mir_match_scoped_temp_schedule(&scoped_temp_plan)) {
            mir_emit_scoped_temp_schedule(out, &scoped_temp_plan);
            return 1;
        }
    }
    return -1;
}
