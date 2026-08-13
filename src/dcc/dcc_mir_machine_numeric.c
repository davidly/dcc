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

int mir_try_emit_numeric_kernels(FILE *out, int phase)
{
    if (phase == 0) {
        struct MirUnsignedLongSqrtSchedule sqrt_plan;
        struct MirPrimeSearchSchedule prime_plan;
        struct MirCatalanDriverSchedule catalan_plan;

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
    } else if (phase == 1) {
        struct MirFixedPointMultiply plan;

        if (mir_match_fixed_point_multiply(&plan)) {
            mir_emit_fixed_point_multiply(out, &plan);
            return 1;
        }
    } else if (phase == 2) {
        struct MirNarrowedDivmodLoopSchedule plan;

        if (mir_match_narrowed_divmod_loop_schedule(&plan)) {
            mir_emit_narrowed_divmod_loop_schedule(out, &plan);
            return 1;
        }
    }
    return -1;
}
