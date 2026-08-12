/* dcc_mir_machine_scanners.c - strict scanner/parser schedules. */

#include "dcc_mir_machine_internal.h"

struct MirCommentScanSchedule {
    struct Sym *state_root;
    int state_root_offset;
    int source_offset;
    int length_offset;
    int cursor_offset;
    int line_offset;
};

struct MirBoundedStringMatchSchedule {
    int pointer_stack_offset;
    int expected[5];
};

struct MirStarCommentScanSchedule {
    struct Sym *source_root;
    struct Sym *length_root;
    struct Sym *cursor_root;
    struct Sym *line_root;
    int source_offset;
    int length_offset;
    int cursor_offset;
    int line_offset;
    int opening_character;
    int closing_character;
};

struct MirWhitespaceScanSchedule {
    struct Sym *state_root;
    struct Sym *space_function;
    int state_root_offset;
    int source_offset;
    int length_offset;
    int cursor_offset;
    int line_offset;
};

struct MirActionDecodeSchedule {
    struct Sym *trim_function;
    struct Sym *prefix_function;
    struct Sym *label_function;
    struct Sym *search_function;
    struct Sym *assignment_function;
    int statement_stack_offset;
    int text_stack_offset;
    int action_offset;
    int target_offset;
    int default_action;
    int goto_action;
    int return_action;
    int goto_string_ids[2];
    int return_string_id;
    int separator;
    int assignment_mode;
};

struct MirBufferedDeclarationSchedule {
    struct Sym *count_root;
    struct Sym *statements_root;
    struct Sym *copy_function;
    struct Sym *trim_function;
    struct Sym *prefix_function;
    struct Sym *declaration_function;
    int count_offset;
    int statements_offset;
    int cursor_offset;
    int buffer_offset;
    int frame_bytes;
    int record_stride;
    int text_offset;
    int string_ids[3];
    int declaration_types[3];
};

struct MirSymbolFindSchedule {
    struct Sym *symbols_root;
    struct Sym *count_root;
    struct Sym *memory_top_root;
    struct Sym *compare_function;
    struct Sym *error_function;
    struct Sym *copy_function;
    int symbols_offset;
    int count_offset;
    int memory_top_offset;
    int name_stack_offset;
    int record_stride;
    int name_field_offset;
    int name_field_size;
    int scalar_field_offset;
    int base_field_offset;
    int size_field_offset;
    int symbol_limit;
    int memory_limit;
    int symbol_error_string_id;
    int memory_error_string_id;
};

struct MirStateMember {
    struct Sym *root;
    int root_offset;
    int member_offset;
};

struct MirGlobalScalar {
    struct Sym *root;
    int offset;
    int type;
};

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

static int mir_machine_same_state_member(
    const struct MirStateMember *left,
    const struct MirStateMember *right)
{
    return left->root == right->root &&
           left->root_offset == right->root_offset &&
           left->member_offset == right->member_offset;
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

static int mir_match_bounded_string_byte(
    int instruction, int pointer_value, int offset, int *expected_out)
{
    const struct MirInsn *offset_constant = &mir.insns[instruction];
    const struct MirInsn *address = &mir.insns[instruction + 1];
    const struct MirInsn *load = &mir.insns[instruction + 2];
    const struct MirInsn *expected = &mir.insns[instruction + 3];
    const struct MirInsn *conversion = &mir.insns[instruction + 4];
    const struct MirInsn *comparison = &mir.insns[instruction + 5];
    const struct MirInsn *branch = &mir.insns[instruction + 6];

    if (!mir_machine_constant_equals(
            offset_constant->dst, offset) ||
        type_ptr_depth(offset_constant->type) != 0 ||
        type_size(offset_constant->type) != 2 ||
        (offset_constant->type & TYPE_UNSIGNED) != 0 ||
        address->src1 != pointer_value ||
        address->src2 != offset_constant->dst ||
        address->immediate != 1 ||
        address->memory_size != 1 ||
        address->bit_width != 0 ||
        (address->memory_flags & (1 | 8)) != 0 ||
        type_ptr_depth(address->type) != 1 ||
        (address->type & 15) != TYPE_CHAR ||
        type_size(address->type) != 2 ||
        load->src1 != address->dst ||
        load->memory_size != 1 ||
        load->bit_width != 0 ||
        (load->memory_flags & (1 | 8)) != 0 ||
        type_ptr_depth(load->type) != 0 ||
        (load->type & 15) != TYPE_CHAR ||
        type_size(load->type) != 1 ||
        (load->type & TYPE_UNSIGNED) != 0 ||
        type_ptr_depth(expected->type) != 0 ||
        (expected->type & 15) != TYPE_INT ||
        type_size(expected->type) != 2 ||
        (expected->type & TYPE_UNSIGNED) != 0 ||
        expected->immediate < 0 || expected->immediate > 127 ||
        conversion->src1 != load->dst ||
        conversion->immediate != 0 ||
        type_ptr_depth(conversion->type) != 0 ||
        (conversion->type & 15) != TYPE_INT ||
        type_size(conversion->type) != 2 ||
        (conversion->type & TYPE_UNSIGNED) != 0 ||
        comparison->src1 != conversion->dst ||
        comparison->src2 != expected->dst ||
        comparison->immediate != TOK_EQ ||
        type_ptr_depth(comparison->type) != 0 ||
        (comparison->type & 15) != TYPE_INT ||
        type_size(comparison->type) != 2 ||
        (comparison->type & TYPE_UNSIGNED) != 0 ||
        type_ptr_depth(comparison->secondary_offset) != 0 ||
        (comparison->secondary_offset & 15) != TYPE_INT ||
        type_size(comparison->secondary_offset) != 2 ||
        (comparison->secondary_offset & TYPE_UNSIGNED) != 0 ||
        branch->src1 != comparison->dst)
        return 0;
    *expected_out = (int)expected->immediate;
    return 1;
}

static int mir_match_bounded_string_boolean_join(
    int success_label, int one, int jump, int failure_label,
    int zero, int join_label, int phi)
{
    return mir_machine_constant_equals(mir.insns[one].dst, 1) &&
        mir.insns[jump].label == mir.insns[join_label].label &&
        mir_machine_constant_equals(mir.insns[zero].dst, 0) &&
        mir.insns[phi].src1 == mir.insns[one].dst &&
        mir.insns[phi].src2 == mir.insns[zero].dst &&
        mir.insns[phi].phi_pred1 == mir.insns[success_label].label &&
        mir.insns[phi].phi_pred2 == mir.insns[failure_label].label;
}

static int mir_match_bounded_string_match_schedule(
    struct MirBoundedStringMatchSchedule *plan)
{
    static const int expected_opcodes[74] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST,
        MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
        MIR_PHI, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
        MIR_PHI, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
        MIR_PHI, MIR_RETURN
    };
    static const int byte_instructions[5] = { 3, 11, 27, 43, 59 };
    int instruction;
    int byte;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 74 || mir_cfg_block_count() != 13 ||
        mir.has_vla || mir.local_bytes != 0 ||
        mir.aggregate_temp_bytes != 0 ||
        type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2 ||
        (mir.return_type & TYPE_UNSIGNED) != 0)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "bounded-string-match-schedule", "opcodes");

    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->pointer_stack_offset) ||
        plan->pointer_stack_offset > 32767 ||
        type_ptr_depth(mir.insns[1].type) != 1 ||
        (mir.insns[1].type & 15) != TYPE_CHAR ||
        type_size(mir.insns[1].type) != 2 ||
        mir_machine_pointee_is_volatile(&mir.insns[1]) ||
        mir.insns[2].object != mir.insns[1].object ||
        mir.insns[2].object < 0)
        return mir_machine_reject(
            "bounded-string-match-schedule", "parameter");

    for (byte = 0; byte < 5; ++byte) {
        if (!mir_match_bounded_string_byte(
                byte_instructions[byte], mir.insns[1].dst,
                byte, &plan->expected[byte]))
            return mir_machine_reject(
                "bounded-string-match-schedule", "byte");
        if ((byte < 4 &&
             (plan->expected[byte] <= 0 ||
              plan->expected[byte] > 127)) ||
            (byte == 4 && plan->expected[byte] != 0))
            return mir_machine_reject(
                "bounded-string-match-schedule", "terminator");
    }

    if (mir.insns[9].label != mir.insns[21].label ||
        mir.insns[17].label != mir.insns[21].label ||
        mir.insns[10].object != mir.insns[1].object ||
        !mir_match_bounded_string_boolean_join(
            18, 19, 20, 21, 22, 23, 24) ||
        mir.insns[25].src1 != mir.insns[24].dst ||
        mir.insns[25].label != mir.insns[37].label ||
        mir.insns[26].object != mir.insns[1].object ||
        mir.insns[33].label != mir.insns[37].label ||
        !mir_match_bounded_string_boolean_join(
            34, 35, 36, 37, 38, 39, 40) ||
        mir.insns[41].src1 != mir.insns[40].dst ||
        mir.insns[41].label != mir.insns[53].label ||
        mir.insns[42].object != mir.insns[1].object ||
        mir.insns[49].label != mir.insns[53].label ||
        !mir_match_bounded_string_boolean_join(
            50, 51, 52, 53, 54, 55, 56) ||
        mir.insns[57].src1 != mir.insns[56].dst ||
        mir.insns[57].label != mir.insns[69].label ||
        mir.insns[58].object != mir.insns[1].object ||
        mir.insns[65].label != mir.insns[69].label ||
        !mir_match_bounded_string_boolean_join(
            66, 67, 68, 69, 70, 71, 72) ||
        mir.insns[73].src1 != mir.insns[72].dst)
        return mir_machine_reject(
            "bounded-string-match-schedule", "short-circuit");
    return 1;
}

static int mir_match_scanner_global_scalar(
    const struct MirInsn *insn, int base_type, int pointer_depth,
    int width, struct MirGlobalScalar *location_out)
{
    int memory_type;
    int memory_storage;
    int memory_offset;
    struct Sym *root;

    if (!mir_scalar_memory_location(
            insn, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        type_ptr_depth(memory_type) != pointer_depth ||
        (memory_type & 15) != base_type ||
        type_size(memory_type) != width ||
        (pointer_depth == 0 && (memory_type & TYPE_UNSIGNED) != 0) ||
        !mir_machine_named_nonvolatile(insn))
        return 0;
    root = find_global(insn->name);
    if (root == NULL || !root->is_defined || root->is_volatile)
        return 0;
    location_out->root = root;
    location_out->offset = memory_offset;
    location_out->type = memory_type;
    return 1;
}

static int mir_scanner_same_global(
    const struct MirGlobalScalar *left,
    const struct MirGlobalScalar *right)
{
    return left->root == right->root &&
        left->offset == right->offset &&
        left->type == right->type;
}

static int mir_match_star_comment_char_load(
    int address_index, int pointer_value, int index_value)
{
    const struct MirInsn *address = &mir.insns[address_index];
    const struct MirInsn *load = &mir.insns[address_index + 1];

    return address->src1 == pointer_value &&
        address->src2 == index_value &&
        address->immediate == 1 &&
        address->memory_size == 1 &&
        address->bit_width == 0 &&
        (address->memory_flags & (1 | 8)) == 0 &&
        type_ptr_depth(address->type) == 1 &&
        (address->type & 15) == TYPE_CHAR &&
        type_size(address->type) == 2 &&
        load->src1 == address->dst &&
        load->memory_size == 1 &&
        load->bit_width == 0 &&
        (load->memory_flags & (1 | 8)) == 0 &&
        type_ptr_depth(load->type) == 0 &&
        (load->type & 15) == TYPE_CHAR &&
        type_size(load->type) == 1 &&
        (load->type & TYPE_UNSIGNED) == 0;
}

static int mir_match_star_comment_conversion(
    int conversion_index, int source_value)
{
    const struct MirInsn *conversion = &mir.insns[conversion_index];

    return conversion->src1 == source_value &&
        conversion->immediate == 0 &&
        type_ptr_depth(conversion->type) == 0 &&
        (conversion->type & 15) == TYPE_INT &&
        type_size(conversion->type) == 2 &&
        (conversion->type & TYPE_UNSIGNED) == 0;
}

static int mir_match_star_comment_comparison(
    int comparison_index, int left_value, int right_value,
    int operation, int operand_type)
{
    const struct MirInsn *comparison = &mir.insns[comparison_index];

    return comparison->src1 == left_value &&
        comparison->src2 == right_value &&
        comparison->immediate == operation &&
        type_ptr_depth(comparison->type) == 0 &&
        (comparison->type & 15) == TYPE_INT &&
        type_size(comparison->type) == 2 &&
        (comparison->type & TYPE_UNSIGNED) == 0 &&
        comparison->secondary_offset == operand_type;
}

static int mir_match_star_comment_long_add(
    int binary_index, int left_value, int right_value)
{
    const struct MirInsn *binary = &mir.insns[binary_index];

    return binary->src1 == left_value &&
        binary->src2 == right_value &&
        binary->immediate == '+' &&
        type_ptr_depth(binary->type) == 0 &&
        (binary->type & 15) == TYPE_LONG &&
        type_size(binary->type) == 4 &&
        (binary->type & TYPE_UNSIGNED) == 0 &&
        binary->secondary_offset == binary->type;
}

static int mir_match_star_comment_scan_schedule(
    struct MirStarCommentScanSchedule *plan)
{
    static const int expected_opcodes[86] = {
        MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_NOP,
        MIR_STORE, MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_LOAD, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_UNARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE,
        MIR_LOAD, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_LABEL, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_LOAD, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_LOAD, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_LABEL
    };
    struct MirGlobalScalar cursor[8];
    struct MirGlobalScalar length[2];
    struct MirGlobalScalar source[3];
    struct MirGlobalScalar line[2];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 86 || mir_cfg_block_count() != 12 ||
        mir.has_vla || mir.local_bytes != 0 ||
        mir.aggregate_temp_bytes != 0 ||
        type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < 86; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "star-comment-scan-schedule", "opcodes");

    if (!mir_match_scanner_global_scalar(
            &mir.insns[1], TYPE_LONG, 0, 4, &cursor[0]) ||
        !mir_match_scanner_global_scalar(
            &mir.insns[6], TYPE_LONG, 0, 4, &cursor[1]) ||
        !mir_match_scanner_global_scalar(
            &mir.insns[8], TYPE_LONG, 0, 4, &cursor[2]) ||
        !mir_match_scanner_global_scalar(
            &mir.insns[16], TYPE_LONG, 0, 4, &cursor[3]) ||
        !mir_match_scanner_global_scalar(
            &mir.insns[24], TYPE_LONG, 0, 4, &cursor[4]) ||
        !mir_match_scanner_global_scalar(
            &mir.insns[52], TYPE_LONG, 0, 4, &cursor[5]) ||
        !mir_match_scanner_global_scalar(
            &mir.insns[64], TYPE_LONG, 0, 4, &cursor[6]) ||
        !mir_match_scanner_global_scalar(
            &mir.insns[67], TYPE_LONG, 0, 4, &cursor[7]) ||
        !mir_scanner_same_global(&cursor[0], &cursor[1]) ||
        !mir_scanner_same_global(&cursor[0], &cursor[2]) ||
        !mir_scanner_same_global(&cursor[0], &cursor[3]) ||
        !mir_scanner_same_global(&cursor[0], &cursor[4]) ||
        !mir_scanner_same_global(&cursor[0], &cursor[5]) ||
        !mir_scanner_same_global(&cursor[0], &cursor[6]) ||
        !mir_scanner_same_global(&cursor[0], &cursor[7]) ||
        !mir_machine_constant_equals(mir.insns[3].dst, 2) ||
        !mir_match_star_comment_long_add(
            4, mir.insns[1].dst, mir.insns[3].dst) ||
        mir.insns[6].src1 != mir.insns[4].dst ||
        !mir_machine_constant_equals(mir.insns[10].dst, 1) ||
        !mir_match_star_comment_long_add(
            11, mir.insns[8].dst, mir.insns[10].dst))
        return mir_machine_reject(
            "star-comment-scan-schedule", "cursor");

    if (!mir_match_scanner_global_scalar(
            &mir.insns[12], TYPE_LONG, 0, 4, &length[0]) ||
        !mir_match_scanner_global_scalar(
            &mir.insns[76], TYPE_LONG, 0, 4, &length[1]) ||
        !mir_scanner_same_global(&length[0], &length[1]) ||
        cursor[0].root == length[0].root ||
        !mir_match_star_comment_comparison(
            13, mir.insns[11].dst, mir.insns[12].dst,
            '<', mir.insns[12].type) ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        mir.insns[14].label != mir.insns[46].label)
        return mir_machine_reject(
            "star-comment-scan-schedule", "bound");

    if (!mir_match_scanner_global_scalar(
            &mir.insns[15], TYPE_CHAR, 1, 2, &source[0]) ||
        !mir_match_scanner_global_scalar(
            &mir.insns[23], TYPE_CHAR, 1, 2, &source[1]) ||
        !mir_match_scanner_global_scalar(
            &mir.insns[51], TYPE_CHAR, 1, 2, &source[2]) ||
        !mir_scanner_same_global(&source[0], &source[1]) ||
        !mir_scanner_same_global(&source[0], &source[2]) ||
        source[0].root == cursor[0].root ||
        source[0].root == length[0].root ||
        !mir_match_star_comment_char_load(
            17, mir.insns[15].dst, mir.insns[16].dst) ||
        !mir_match_star_comment_char_load(
            28, mir.insns[23].dst, mir.insns[27].dst) ||
        !mir_match_star_comment_char_load(
            53, mir.insns[51].dst, mir.insns[52].dst) ||
        !mir_match_star_comment_conversion(
            20, mir.insns[18].dst) ||
        !mir_match_star_comment_conversion(
            31, mir.insns[29].dst) ||
        !mir_match_star_comment_conversion(
            56, mir.insns[54].dst))
        return mir_machine_reject(
            "star-comment-scan-schedule", "characters");

    if (!mir_machine_constant_equals(mir.insns[19].dst, '*') ||
        !mir_match_star_comment_comparison(
            21, mir.insns[20].dst, mir.insns[19].dst,
            TOK_EQ, mir.insns[19].type) ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        mir.insns[22].label != mir.insns[37].label ||
        !mir_machine_constant_equals(mir.insns[26].dst, 1) ||
        !mir_match_star_comment_long_add(
            27, mir.insns[24].dst, mir.insns[26].dst) ||
        !mir_machine_constant_equals(mir.insns[30].dst, ')') ||
        !mir_match_star_comment_comparison(
            32, mir.insns[31].dst, mir.insns[30].dst,
            TOK_EQ, mir.insns[30].type) ||
        mir.insns[33].src1 != mir.insns[32].dst ||
        mir.insns[33].label != mir.insns[37].label ||
        !mir_machine_constant_equals(mir.insns[35].dst, 1) ||
        mir.insns[36].label != mir.insns[39].label ||
        !mir_machine_constant_equals(mir.insns[38].dst, 0) ||
        mir.insns[40].src1 != mir.insns[35].dst ||
        mir.insns[40].src2 != mir.insns[38].dst ||
        mir.insns[40].phi_pred1 != mir.insns[34].label ||
        mir.insns[40].phi_pred2 != mir.insns[37].label ||
        mir.insns[41].src1 != mir.insns[40].dst ||
        mir.insns[41].immediate != '!' ||
        mir.insns[42].src1 != mir.insns[41].dst ||
        mir.insns[42].label != mir.insns[46].label ||
        !mir_machine_constant_equals(mir.insns[44].dst, 1) ||
        mir.insns[45].label != mir.insns[48].label ||
        !mir_machine_constant_equals(mir.insns[47].dst, 0) ||
        mir.insns[49].src1 != mir.insns[44].dst ||
        mir.insns[49].src2 != mir.insns[47].dst ||
        mir.insns[49].phi_pred1 != mir.insns[43].label ||
        mir.insns[49].phi_pred2 != mir.insns[46].label ||
        mir.insns[50].src1 != mir.insns[49].dst ||
        mir.insns[50].label != mir.insns[71].label)
        return mir_machine_reject(
            "star-comment-scan-schedule", "delimiter");

    if (!mir_machine_constant_equals(mir.insns[55].dst, '\n') ||
        !mir_match_star_comment_comparison(
            57, mir.insns[56].dst, mir.insns[55].dst,
            TOK_EQ, mir.insns[55].type) ||
        mir.insns[58].src1 != mir.insns[57].dst ||
        mir.insns[58].label != mir.insns[63].label ||
        !mir_match_scanner_global_scalar(
            &mir.insns[59], TYPE_INT, 0, 2, &line[0]) ||
        !mir_match_scanner_global_scalar(
            &mir.insns[62], TYPE_INT, 0, 2, &line[1]) ||
        !mir_scanner_same_global(&line[0], &line[1]) ||
        line[0].root == source[0].root ||
        line[0].root == cursor[0].root ||
        line[0].root == length[0].root ||
        !mir_machine_constant_equals(mir.insns[60].dst, 1) ||
        mir.insns[61].src1 != mir.insns[59].dst ||
        mir.insns[61].src2 != mir.insns[60].dst ||
        mir.insns[61].immediate != '+' ||
        mir.insns[61].type != mir.insns[59].type ||
        mir.insns[61].secondary_offset != mir.insns[59].type ||
        mir.insns[62].src1 != mir.insns[61].dst ||
        !mir_machine_constant_equals(mir.insns[65].dst, 1) ||
        !mir_match_star_comment_long_add(
            66, mir.insns[64].dst, mir.insns[65].dst) ||
        mir.insns[67].src1 != mir.insns[66].dst ||
        mir.insns[70].label != mir.insns[7].label)
        return mir_machine_reject(
            "star-comment-scan-schedule", "body");

    if (!mir_match_scanner_global_scalar(
            &mir.insns[72], TYPE_LONG, 0, 4, &cursor[1]) ||
        !mir_match_scanner_global_scalar(
            &mir.insns[79], TYPE_LONG, 0, 4, &cursor[2]) ||
        !mir_match_scanner_global_scalar(
            &mir.insns[84], TYPE_LONG, 0, 4, &cursor[3]) ||
        !mir_scanner_same_global(&cursor[0], &cursor[1]) ||
        !mir_scanner_same_global(&cursor[0], &cursor[2]) ||
        !mir_scanner_same_global(&cursor[0], &cursor[3]) ||
        !mir_machine_constant_equals(mir.insns[74].dst, 1) ||
        !mir_match_star_comment_long_add(
            75, mir.insns[72].dst, mir.insns[74].dst) ||
        !mir_match_star_comment_comparison(
            77, mir.insns[75].dst, mir.insns[76].dst,
            '<', mir.insns[76].type) ||
        mir.insns[78].src1 != mir.insns[77].dst ||
        mir.insns[78].label != mir.insns[85].label ||
        !mir_machine_constant_equals(mir.insns[81].dst, 2) ||
        !mir_match_star_comment_long_add(
            82, mir.insns[79].dst, mir.insns[81].dst) ||
        mir.insns[84].src1 != mir.insns[82].dst)
        return mir_machine_reject(
            "star-comment-scan-schedule", "exit");

    plan->source_root = source[0].root;
    plan->length_root = length[0].root;
    plan->cursor_root = cursor[0].root;
    plan->line_root = line[0].root;
    plan->source_offset = source[0].offset;
    plan->length_offset = length[0].offset;
    plan->cursor_offset = cursor[0].offset;
    plan->line_offset = line[0].offset;
    plan->opening_character = (int)mir.insns[19].immediate;
    plan->closing_character = (int)mir.insns[30].immediate;
    return 1;
}

static int mir_match_comment_member_load(
    int member_index, int load_index, int width, int pointer,
    struct MirStateMember *member_out)
{
    const struct MirInsn *member = &mir.insns[member_index];
    const struct MirInsn *load = &mir.insns[load_index];

    return member->opcode == MIR_MEMBER_ADDRESS &&
        load->opcode == MIR_LOAD_INDIRECT &&
        load->src1 == member->dst &&
        load->memory_size == width &&
        load->bit_width == 0 &&
        (load->memory_flags & (1 | 8)) == 0 &&
        type_size(load->type) == width &&
        (pointer ? type_ptr_depth(load->type) > 0 :
                   (type_ptr_depth(load->type) == 0 &&
                    (load->type & TYPE_UNSIGNED) == 0)) &&
        mir_machine_state_member_address(
            member->dst, member_out);
}

static int mir_match_comment_scan_schedule(
    struct MirCommentScanSchedule *plan)
{
    static const int expected_opcodes[60] = {
        MIR_LABEL, MIR_LABEL, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_BINARY, MIR_STORE_INDIRECT,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY,
        MIR_UNARY, MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY,
        MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_STORE_INDIRECT, MIR_NOP, MIR_JUMP, MIR_NOP,
        MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL
    };
    struct MirStateMember cursor_condition;
    struct MirStateMember length_condition;
    struct MirStateMember source;
    struct MirStateMember cursor_update;
    struct MirStateMember line;
    struct MirStateMember cursor_eof;
    struct MirStateMember length_eof;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 60 || mir_cfg_block_count() != 7 ||
        mir.has_vla || mir.local_bytes != 2 ||
        mir.aggregate_temp_bytes != 0 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < 60; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "comment-scan-schedule", "opcodes");

    if (!mir_match_comment_member_load(
            3, 4, 4, 0, &cursor_condition) ||
        !mir_match_comment_member_load(
            6, 7, 4, 0, &length_condition) ||
        mir.insns[8].immediate != '<' ||
        mir.insns[8].src1 != mir.insns[4].dst ||
        mir.insns[8].src2 != mir.insns[7].dst ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].label != mir.insns[59].label)
        return mir_machine_reject(
            "comment-scan-schedule", "condition");

    if (!mir_match_comment_member_load(
            11, 12, 2, 1, &source) ||
        !mir_match_comment_member_load(
            14, 15, 4, 0, &cursor_update) ||
        !mir_machine_same_state_member(
            &cursor_condition, &cursor_update) ||
        !mir_machine_constant_equals(mir.insns[16].dst, 1) ||
        mir.insns[17].immediate != '+' ||
        mir.insns[17].src1 != mir.insns[15].dst ||
        mir.insns[17].src2 != mir.insns[16].dst ||
        mir.insns[17].type != mir.insns[15].type ||
        mir.insns[18].src1 != mir.insns[14].dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        mir.insns[18].memory_size != 4 ||
        mir.insns[18].bit_width != 0 ||
        (mir.insns[18].memory_flags & (1 | 8)) != 0 ||
        mir.insns[19].src1 != mir.insns[12].dst ||
        mir.insns[19].src2 != mir.insns[15].dst ||
        mir.insns[19].immediate != 1 ||
        mir.insns[19].memory_size != 1 ||
        mir.insns[20].src1 != mir.insns[19].dst ||
        mir.insns[20].memory_size != 1 ||
        mir.insns[20].bit_width != 0 ||
        (mir.insns[20].memory_flags & (1 | 8)) != 0 ||
        mir.insns[21].src1 != mir.insns[20].dst ||
        mir.insns[21].immediate != 0 ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        mir.insns[22].immediate != 0 ||
        type_size(mir.insns[22].type) != 2 ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[23].memory_size != 2 ||
        !mir_machine_unobservable_local_store(
            &mir.insns[23]) ||
        mir.insns[24].object != mir.insns[23].object ||
        mir.insns[24].object < 0)
        return mir_machine_reject(
            "comment-scan-schedule", "cursor-read");

    if (!mir_machine_constant_equals(mir.insns[25].dst, '\n') ||
        mir.insns[26].immediate != TOK_EQ ||
        mir.insns[26].src1 != mir.insns[22].dst ||
        mir.insns[26].src2 != mir.insns[25].dst ||
        mir.insns[27].src1 != mir.insns[26].dst ||
        mir.insns[27].label != mir.insns[34].label ||
        !mir_match_comment_member_load(
            29, 30, 2, 0, &line) ||
        !mir_machine_constant_equals(mir.insns[31].dst, 1) ||
        mir.insns[32].immediate != '+' ||
        mir.insns[32].src1 != mir.insns[30].dst ||
        mir.insns[32].src2 != mir.insns[31].dst ||
        mir.insns[33].src1 != mir.insns[29].dst ||
        mir.insns[33].src2 != mir.insns[32].dst ||
        mir.insns[33].memory_size != 2 ||
        mir.insns[33].bit_width != 0 ||
        (mir.insns[33].memory_flags & (1 | 8)) != 0 ||
        mir.insns[35].object != mir.insns[23].object)
        return mir_machine_reject(
            "comment-scan-schedule", "line");

    if (!mir_machine_constant_equals(mir.insns[36].dst, ')') ||
        mir.insns[37].immediate != TOK_EQ ||
        mir.insns[37].src1 != mir.insns[22].dst ||
        mir.insns[37].src2 != mir.insns[36].dst ||
        mir.insns[38].src1 != mir.insns[37].dst ||
        mir.insns[38].label != mir.insns[41].label ||
        mir.insns[40].label != mir.insns[59].label ||
        mir.insns[42].object != mir.insns[23].object ||
        !mir_machine_constant_equals(mir.insns[43].dst, 0x1a) ||
        mir.insns[44].immediate != TOK_EQ ||
        mir.insns[44].src1 != mir.insns[22].dst ||
        mir.insns[44].src2 != mir.insns[43].dst ||
        mir.insns[45].src1 != mir.insns[44].dst ||
        mir.insns[45].label != mir.insns[55].label ||
        !mir_machine_state_member_address(
            mir.insns[47].dst, &cursor_eof) ||
        !mir_match_comment_member_load(
            49, 50, 4, 0, &length_eof) ||
        !mir_machine_same_state_member(
            &cursor_condition, &cursor_eof) ||
        !mir_machine_same_state_member(
            &length_condition, &length_eof) ||
        mir.insns[51].src1 != mir.insns[47].dst ||
        mir.insns[51].src2 != mir.insns[50].dst ||
        mir.insns[51].memory_size != 4 ||
        mir.insns[51].bit_width != 0 ||
        (mir.insns[51].memory_flags & (1 | 8)) != 0 ||
        mir.insns[53].label != mir.insns[59].label ||
        mir.insns[58].label != mir.insns[1].label)
        return mir_machine_reject(
            "comment-scan-schedule", "terminators");

    if (cursor_condition.root != length_condition.root ||
        cursor_condition.root != source.root ||
        cursor_condition.root != line.root ||
        cursor_condition.root_offset !=
            length_condition.root_offset ||
        cursor_condition.root_offset != source.root_offset ||
        cursor_condition.root_offset != line.root_offset ||
        cursor_condition.member_offset ==
            length_condition.member_offset ||
        cursor_condition.member_offset == source.member_offset ||
        cursor_condition.member_offset == line.member_offset ||
        length_condition.member_offset == source.member_offset ||
        length_condition.member_offset == line.member_offset ||
        source.member_offset == line.member_offset ||
        source.member_offset < -128 ||
        source.member_offset + 1 > 127 ||
        cursor_condition.member_offset < -128 ||
        cursor_condition.member_offset + 3 > 127 ||
        length_condition.member_offset < -128 ||
        length_condition.member_offset + 3 > 127 ||
        line.member_offset < -128 ||
        line.member_offset + 1 > 127 ||
        !mir_machine_only_root_loads(
            cursor_condition.root,
            cursor_condition.root_offset))
        return mir_machine_reject(
            "comment-scan-schedule", "state");

    plan->state_root = cursor_condition.root;
    plan->state_root_offset = cursor_condition.root_offset;
    plan->source_offset = source.member_offset;
    plan->length_offset = length_condition.member_offset;
    plan->cursor_offset = cursor_condition.member_offset;
    plan->line_offset = line.member_offset;
    return 1;
}

static int mir_match_whitespace_member_load(
    int member_index, int load_index, int width, int pointer,
    struct MirStateMember *member_out)
{
    const struct MirInsn *member = &mir.insns[member_index];
    const struct MirInsn *load = &mir.insns[load_index];
    struct Sym *root;
    long root_offset;

    if (member->opcode != MIR_MEMBER_ADDRESS ||
        member->bit_width != 0 ||
        (member->memory_flags & (1 | 8)) != 0 ||
        load->opcode != MIR_LOAD_INDIRECT ||
        load->src1 != member->dst ||
        load->memory_size != width ||
        load->bit_width != 0 ||
        (load->memory_flags & (1 | 8)) != 0 ||
        type_size(load->type) != width ||
        (pointer ? type_ptr_depth(load->type) == 0 :
                   (type_ptr_depth(load->type) != 0 ||
                    (load->type & TYPE_UNSIGNED) != 0)) ||
        !mir_machine_global_address_offset(
            member->src1, &root, &root_offset, 0) ||
        root_offset < -32768 || root_offset > 32767 ||
        member->immediate < -32768 ||
        member->immediate > 32767)
        return 0;
    member_out->root = root;
    member_out->root_offset = (int)root_offset;
    member_out->member_offset = (int)member->immediate;
    return 1;
}

static int mir_whitespace_members_overlap(
    const struct MirStateMember *left, int left_width,
    const struct MirStateMember *right, int right_width)
{
    return left->member_offset <
               right->member_offset + right_width &&
           right->member_offset <
               left->member_offset + left_width;
}

static int mir_match_whitespace_scan_schedule(
    struct MirWhitespaceScanSchedule *plan)
{
    static const int expected_opcodes[60] = {
        MIR_LABEL, MIR_LABEL, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY,
        MIR_UNARY, MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST,
        MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY,
        MIR_STORE_INDIRECT, MIR_LABEL, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST,
        MIR_BINARY, MIR_STORE_INDIRECT, MIR_NOP, MIR_LABEL,
        MIR_JUMP, MIR_LABEL
    };
    struct MirStateMember cursor_condition;
    struct MirStateMember length_condition;
    struct MirStateMember source_condition;
    struct MirStateMember cursor_condition_source;
    struct MirStateMember source_body;
    struct MirStateMember cursor_body;
    struct MirStateMember line;
    struct MirStateMember cursor_update;
    int call_argument;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 60 || mir_cfg_block_count() != 8 ||
        mir.has_vla || mir.local_bytes != 0 ||
        mir.aggregate_temp_bytes != 0 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < 60; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "whitespace-scan-schedule", "opcodes");

    if (!mir_match_whitespace_member_load(
            3, 4, 4, 0, &cursor_condition) ||
        !mir_match_whitespace_member_load(
            6, 7, 4, 0, &length_condition) ||
        mir.insns[8].immediate != '<' ||
        mir.insns[8].src1 != mir.insns[4].dst ||
        mir.insns[8].src2 != mir.insns[7].dst ||
        type_size(mir.insns[8].secondary_offset) != 4 ||
        type_size(mir.insns[8].type) != 2 ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].label != mir.insns[26].label)
        return mir_machine_reject(
            "whitespace-scan-schedule", "bound");

    if (!mir_match_whitespace_member_load(
            11, 12, 2, 1, &source_condition) ||
        !mir_match_whitespace_member_load(
            14, 15, 4, 0, &cursor_condition_source) ||
        !mir_machine_same_state_member(
            &cursor_condition, &cursor_condition_source) ||
        mir.insns[16].src1 != mir.insns[12].dst ||
        mir.insns[16].src2 != mir.insns[15].dst ||
        mir.insns[16].immediate != 1 ||
        mir.insns[16].memory_size != 1 ||
        (mir.insns[16].memory_flags & (1 | 8)) != 0 ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        mir.insns[17].memory_size != 1 ||
        mir.insns[17].bit_width != 0 ||
        (mir.insns[17].memory_flags & (1 | 8)) != 0 ||
        type_ptr_depth(mir.insns[17].type) != 0 ||
        type_size(mir.insns[17].type) != 1 ||
        (mir.insns[17].type & TYPE_UNSIGNED) != 0 ||
        mir.insns[18].src1 != mir.insns[17].dst ||
        mir.insns[18].immediate != 0 ||
        type_size(mir.insns[18].type) != 1 ||
        (mir.insns[18].type & TYPE_UNSIGNED) == 0 ||
        mir.insns[19].src1 != mir.insns[18].dst ||
        mir.insns[19].immediate != 0 ||
        type_ptr_depth(mir.insns[19].type) != 0 ||
        type_size(mir.insns[19].type) != 2 ||
        mir.insns[20].src1 != mir.insns[19].dst ||
        !mir_machine_single_call_argument(
            &mir.insns[21], &call_argument) ||
        call_argument != mir.insns[19].dst ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        mir.insns[22].label != mir.insns[26].label)
        return mir_machine_reject(
            "whitespace-scan-schedule", "helper-call");
    plan->space_function = find_global(mir.insns[21].name);
    if (plan->space_function == NULL ||
        plan->space_function->storage != SC_FUNC ||
        plan->space_function->is_funcptr ||
        plan->space_function->is_noreturn ||
        !plan->space_function->has_proto ||
        plan->space_function->proto_nargs != 1 ||
        plan->space_function->proto_variadic ||
        type_ptr_depth(plan->space_function->proto_types[0]) != 0 ||
        type_size(plan->space_function->proto_types[0]) != 2 ||
        type_ptr_depth(mir.insns[21].type) != 0 ||
        type_size(mir.insns[21].type) != 2 ||
        (mir.insns[21].memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0 ||
        (mir.insns[21].base_name[0] != 0 &&
         strcmp(mir.insns[21].base_name,
                asm_name_for(sym_asm_name(
                    plan->space_function)))))
        return mir_machine_reject(
            "whitespace-scan-schedule", "helper-function");

    if (!mir_machine_constant_equals(mir.insns[24].dst, 1) ||
        mir.insns[25].label != mir.insns[28].label ||
        !mir_machine_constant_equals(mir.insns[27].dst, 0) ||
        mir.insns[29].src1 != mir.insns[24].dst ||
        mir.insns[29].src2 != mir.insns[27].dst ||
        mir.insns[29].phi_pred1 != mir.insns[23].label ||
        mir.insns[29].phi_pred2 != mir.insns[26].label ||
        mir.insns[30].src1 != mir.insns[29].dst ||
        mir.insns[30].label != mir.insns[59].label)
        return mir_machine_reject(
            "whitespace-scan-schedule", "short-circuit");

    if (!mir_match_whitespace_member_load(
            32, 33, 2, 1, &source_body) ||
        !mir_match_whitespace_member_load(
            35, 36, 4, 0, &cursor_body) ||
        !mir_machine_same_state_member(
            &source_condition, &source_body) ||
        !mir_machine_same_state_member(
            &cursor_condition, &cursor_body) ||
        mir.insns[37].src1 != mir.insns[33].dst ||
        mir.insns[37].src2 != mir.insns[36].dst ||
        mir.insns[37].immediate != 1 ||
        mir.insns[37].memory_size != 1 ||
        (mir.insns[37].memory_flags & (1 | 8)) != 0 ||
        mir.insns[38].src1 != mir.insns[37].dst ||
        mir.insns[38].memory_size != 1 ||
        mir.insns[38].bit_width != 0 ||
        (mir.insns[38].memory_flags & (1 | 8)) != 0 ||
        mir.insns[38].type != mir.insns[17].type ||
        !mir_machine_constant_equals(mir.insns[39].dst, '\n') ||
        mir.insns[40].src1 != mir.insns[38].dst ||
        mir.insns[40].immediate != 0 ||
        type_size(mir.insns[40].type) != 2 ||
        mir.insns[41].immediate != TOK_EQ ||
        mir.insns[41].src1 != mir.insns[40].dst ||
        mir.insns[41].src2 != mir.insns[39].dst ||
        mir.insns[42].src1 != mir.insns[41].dst ||
        mir.insns[42].label != mir.insns[49].label)
        return mir_machine_reject(
            "whitespace-scan-schedule", "body-read");

    if (!mir_match_whitespace_member_load(
            44, 45, 2, 0, &line) ||
        !mir_machine_constant_equals(mir.insns[46].dst, 1) ||
        mir.insns[47].immediate != '+' ||
        mir.insns[47].src1 != mir.insns[45].dst ||
        mir.insns[47].src2 != mir.insns[46].dst ||
        mir.insns[48].src1 != mir.insns[44].dst ||
        mir.insns[48].src2 != mir.insns[47].dst ||
        mir.insns[48].memory_size != 2 ||
        mir.insns[48].bit_width != 0 ||
        (mir.insns[48].memory_flags & (1 | 8)) != 0 ||
        !mir_match_whitespace_member_load(
            51, 52, 4, 0, &cursor_update) ||
        !mir_machine_same_state_member(
            &cursor_condition, &cursor_update) ||
        !mir_machine_constant_equals(mir.insns[53].dst, 1) ||
        mir.insns[54].immediate != '+' ||
        mir.insns[54].src1 != mir.insns[52].dst ||
        mir.insns[54].src2 != mir.insns[53].dst ||
        mir.insns[55].src1 != mir.insns[51].dst ||
        mir.insns[55].src2 != mir.insns[54].dst ||
        mir.insns[55].memory_size != 4 ||
        mir.insns[55].bit_width != 0 ||
        (mir.insns[55].memory_flags & (1 | 8)) != 0 ||
        mir.insns[58].label != mir.insns[1].label)
        return mir_machine_reject(
            "whitespace-scan-schedule", "updates");

    if (cursor_condition.root != length_condition.root ||
        cursor_condition.root != source_condition.root ||
        cursor_condition.root != line.root ||
        cursor_condition.root_offset !=
            length_condition.root_offset ||
        cursor_condition.root_offset !=
            source_condition.root_offset ||
        cursor_condition.root_offset != line.root_offset ||
        cursor_condition.member_offset ==
            length_condition.member_offset ||
        cursor_condition.member_offset ==
            source_condition.member_offset ||
        cursor_condition.member_offset == line.member_offset ||
        length_condition.member_offset ==
            source_condition.member_offset ||
        length_condition.member_offset == line.member_offset ||
        source_condition.member_offset == line.member_offset ||
        mir_whitespace_members_overlap(
            &source_condition, 2, &length_condition, 4) ||
        mir_whitespace_members_overlap(
            &source_condition, 2, &cursor_condition, 4) ||
        mir_whitespace_members_overlap(
            &source_condition, 2, &line, 2) ||
        mir_whitespace_members_overlap(
            &length_condition, 4, &cursor_condition, 4) ||
        mir_whitespace_members_overlap(
            &length_condition, 4, &line, 2) ||
        mir_whitespace_members_overlap(
            &cursor_condition, 4, &line, 2) ||
        source_condition.member_offset < -128 ||
        source_condition.member_offset + 1 > 127 ||
        cursor_condition.member_offset < -128 ||
        cursor_condition.member_offset + 3 > 127 ||
        length_condition.member_offset < -128 ||
        length_condition.member_offset + 3 > 127 ||
        line.member_offset < -128 ||
        line.member_offset + 1 > 127)
        return mir_machine_reject(
            "whitespace-scan-schedule", "state");

    plan->state_root = cursor_condition.root;
    plan->state_root_offset = cursor_condition.root_offset;
    plan->source_offset = source_condition.member_offset;
    plan->length_offset = length_condition.member_offset;
    plan->cursor_offset = cursor_condition.member_offset;
    plan->line_offset = line.member_offset;
    return 1;
}

static int mir_match_action_decode_member(
    int load_index, int member_index, int parameter_index,
    int *offset_out)
{
    const struct MirInsn *load = &mir.insns[load_index];
    const struct MirInsn *member = &mir.insns[member_index];

    if (load->opcode != MIR_LOAD ||
        !mir_machine_same_location(
            load, &mir.insns[parameter_index]) ||
        member->opcode != MIR_MEMBER_ADDRESS ||
        member->src1 != load->dst ||
        member->memory_size != 2 ||
        member->bit_width != 0 ||
        (member->memory_flags & (1 | 8)) != 0 ||
        member->immediate < -32768 ||
        member->immediate > 32767)
        return 0;
    *offset_out = (int)member->immediate;
    return 1;
}

static int mir_match_action_decode_call(
    const struct MirInsn *call, int argument_count,
    struct Sym **function_out)
{
    struct Sym *function = find_global(call->name);

    if (function == NULL || function->storage != SC_FUNC ||
        function->is_funcptr || function->is_noreturn ||
        !function->has_proto ||
        function->proto_nargs != argument_count ||
        function->proto_variadic ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME |
          MIR_CALL_FLAG_INLINE_SUBSTITUTABLE)) != 0 ||
        (call->base_name[0] != 0 &&
         strcmp(call->base_name,
                asm_name_for(sym_asm_name(function)))))
        return 0;
    *function_out = function;
    return 1;
}

static int mir_match_action_decode_string(
    int instruction, int *string_id)
{
    const struct MirInsn *string = &mir.insns[instruction];

    if (string->opcode != MIR_STRING_ADDRESS ||
        !mir_match_action_decode_pointer_type(string->type))
        return 0;
    *string_id = (int)string->immediate;
    return 1;
}

static int mir_match_action_decode_schedule(
    struct MirActionDecodeSchedule *plan)
{
    static const int expected_opcodes[80] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_LOAD, MIR_ARG,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_LOAD, MIR_ARG, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
        MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_CONST, MIR_STORE_INDIRECT, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_STORE_INDIRECT, MIR_RETURN, MIR_NOP, MIR_LABEL,
        MIR_LOAD, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_BRANCH_FALSE, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT,
        MIR_RETURN, MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE,
        MIR_LOAD, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_RETURN, MIR_NOP, MIR_LABEL
    };
    int arguments2[2];
    int arguments3[3];
    int argument;
    int action_offset;
    int instruction;
    long constant;
    struct Sym *function;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 80 || mir_cfg_block_count() != 11 ||
        mir.has_vla || mir.local_bytes != 0 ||
        mir.aggregate_temp_bytes != 0 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < 80; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "action-decode-schedule", "opcodes");

    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst,
            &plan->statement_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &plan->text_stack_offset) ||
        plan->statement_stack_offset ==
            plan->text_stack_offset ||
        plan->statement_stack_offset + 3 > 127 ||
        plan->text_stack_offset + 3 > 127 ||
        !mir_match_action_decode_pointer_type(
            mir.insns[1].type) ||
        !mir_match_action_decode_pointer_type(
            mir.insns[2].type) ||
        mir_machine_pointee_is_volatile(&mir.insns[1]) ||
        mir_machine_pointee_is_volatile(&mir.insns[2]) ||
        !mir_machine_same_location(
            &mir.insns[2], &mir.insns[3]) ||
        !mir_machine_single_call_argument(
            &mir.insns[5], &argument) ||
        argument != mir.insns[3].dst ||
        !mir_match_action_decode_call(
            &mir.insns[5], 1, &plan->trim_function) ||
        !mir_match_action_decode_pointer_type(
            plan->trim_function->proto_types[0]) ||
        (mir.insns[5].type & 15) != TYPE_VOID)
        return mir_machine_reject(
            "action-decode-schedule", "entry");

    if (!mir_match_action_decode_member(
            6, 7, 1, &plan->action_offset) ||
        !mir_machine_constant_value(
            mir.insns[8].dst, &constant, 0) ||
        constant < -32768 || constant > 65535 ||
        mir.insns[9].src1 != mir.insns[7].dst ||
        mir.insns[9].src2 != mir.insns[8].dst ||
        mir.insns[9].memory_size != 2 ||
        mir.insns[9].bit_width != 0 ||
        (mir.insns[9].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject(
            "action-decode-schedule", "default-store");
    plan->default_action = (int)((unsigned long)constant & 0xffffUL);

    if (!mir_machine_same_location(
            &mir.insns[2], &mir.insns[10]) ||
        !mir_machine_two_call_arguments(
            &mir.insns[14], arguments2) ||
        arguments2[0] != mir.insns[10].dst ||
        arguments2[1] != mir.insns[12].dst ||
        !mir_match_action_decode_string(
            12, &plan->goto_string_ids[0]) ||
        !mir_match_action_decode_call(
            &mir.insns[14], 2, &plan->prefix_function) ||
        !mir_match_action_decode_pointer_type(
            plan->prefix_function->proto_types[0]) ||
        !mir_match_action_decode_pointer_type(
            plan->prefix_function->proto_types[1]) ||
        !mir_match_action_decode_word_type(
            mir.insns[14].type) ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        mir.insns[15].label != mir.insns[19].label ||
        !mir_machine_same_location(
            &mir.insns[2], &mir.insns[20]) ||
        !mir_machine_two_call_arguments(
            &mir.insns[24], arguments2) ||
        arguments2[0] != mir.insns[20].dst ||
        arguments2[1] != mir.insns[22].dst ||
        !mir_match_action_decode_string(
            22, &plan->goto_string_ids[1]) ||
        !mir_match_action_decode_call(
            &mir.insns[24], 2, &function) ||
        function !=
            plan->prefix_function ||
        mir.insns[24].type != mir.insns[14].type ||
        mir.insns[25].src1 != mir.insns[24].dst ||
        mir.insns[25].label != mir.insns[29].label ||
        !mir_machine_constant_equals(
            mir.insns[17].dst, 1) ||
        mir.insns[18].label != mir.insns[35].label ||
        !mir_machine_constant_equals(
            mir.insns[27].dst, 1) ||
        mir.insns[28].label != mir.insns[31].label ||
        !mir_machine_constant_equals(
            mir.insns[30].dst, 0) ||
        mir.insns[32].src1 != mir.insns[27].dst ||
        mir.insns[32].src2 != mir.insns[30].dst ||
        mir.insns[32].phi_pred1 != mir.insns[26].label ||
        mir.insns[32].phi_pred2 != mir.insns[29].label ||
        mir.insns[34].label != mir.insns[35].label ||
        mir.insns[36].src1 != mir.insns[17].dst ||
        mir.insns[36].src2 != mir.insns[32].dst ||
        mir.insns[36].phi_pred1 != mir.insns[16].label ||
        mir.insns[36].phi_pred2 != mir.insns[33].label ||
        mir.insns[37].src1 != mir.insns[36].dst ||
        mir.insns[37].label != mir.insns[50].label ||
        plan->goto_string_ids[0] ==
            plan->goto_string_ids[1])
        return mir_machine_reject(
            "action-decode-schedule", "goto-test");

    if (!mir_match_action_decode_member(
            38, 39, 1, &action_offset) ||
        action_offset != plan->action_offset ||
        !mir_machine_constant_value(
            mir.insns[40].dst, &constant, 0) ||
        constant < -32768 || constant > 65535 ||
        mir.insns[41].src1 != mir.insns[39].dst ||
        mir.insns[41].src2 != mir.insns[40].dst ||
        mir.insns[41].memory_size != 2 ||
        mir.insns[41].bit_width != 0 ||
        (mir.insns[41].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject(
            "action-decode-schedule", "goto-store");
    plan->goto_action = (int)((unsigned long)constant & 0xffffUL);

    if (!mir_match_action_decode_member(
            42, 43, 1, &plan->target_offset) ||
        (plan->target_offset < plan->action_offset + 2 &&
         plan->action_offset < plan->target_offset + 2))
        return mir_machine_reject(
            "action-decode-schedule", "target-member");
    if (!mir_machine_same_location(
            &mir.insns[2], &mir.insns[44]) ||
        !mir_machine_single_call_argument(
            &mir.insns[46], &argument) ||
        argument != mir.insns[44].dst ||
        !mir_match_action_decode_call(
            &mir.insns[46], 1, &plan->label_function) ||
        !mir_match_action_decode_pointer_type(
            plan->label_function->proto_types[0]) ||
        !mir_match_action_decode_word_type(
            mir.insns[46].type) ||
        mir.insns[47].src1 != mir.insns[43].dst ||
        mir.insns[47].src2 != mir.insns[46].dst ||
        mir.insns[47].memory_size != 2 ||
        mir.insns[47].bit_width != 0 ||
        (mir.insns[47].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject(
            "action-decode-schedule", "target-call");

    if (!mir_machine_same_location(
            &mir.insns[2], &mir.insns[51]) ||
        !mir_machine_two_call_arguments(
            &mir.insns[55], arguments2) ||
        arguments2[0] != mir.insns[51].dst ||
        arguments2[1] != mir.insns[53].dst ||
        !mir_match_action_decode_string(
            53, &plan->return_string_id) ||
        plan->return_string_id ==
            plan->goto_string_ids[0] ||
        plan->return_string_id ==
            plan->goto_string_ids[1] ||
        !mir_match_action_decode_call(
            &mir.insns[55], 2, &function) ||
        function !=
            plan->prefix_function ||
        mir.insns[55].type != mir.insns[14].type ||
        mir.insns[56].src1 != mir.insns[55].dst ||
        mir.insns[56].label != mir.insns[63].label ||
        !mir_match_action_decode_member(
            57, 58, 1, &action_offset) ||
        action_offset != plan->action_offset ||
        !mir_machine_constant_value(
            mir.insns[59].dst, &constant, 0) ||
        constant < -32768 || constant > 65535 ||
        mir.insns[60].src1 != mir.insns[58].dst ||
        mir.insns[60].src2 != mir.insns[59].dst ||
        mir.insns[60].memory_size != 2 ||
        mir.insns[60].bit_width != 0 ||
        (mir.insns[60].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject(
            "action-decode-schedule", "return-test");
    plan->return_action =
        (int)((unsigned long)constant & 0xffffUL);
    if (plan->default_action == plan->goto_action ||
        plan->default_action == plan->return_action ||
        plan->goto_action == plan->return_action)
        return mir_machine_reject(
            "action-decode-schedule", "action-values");

    if (!mir_machine_same_location(
            &mir.insns[2], &mir.insns[64]) ||
        !mir_machine_two_call_arguments(
            &mir.insns[68], arguments2) ||
        arguments2[0] != mir.insns[64].dst ||
        arguments2[1] != mir.insns[66].dst ||
        !mir_machine_constant_value(
            mir.insns[66].dst, &constant, 0) ||
        constant < 0 || constant > 255 ||
        !mir_match_action_decode_call(
            &mir.insns[68], 2, &plan->search_function) ||
        !mir_match_action_decode_pointer_type(
            plan->search_function->proto_types[0]) ||
        !mir_match_action_decode_word_type(
            plan->search_function->proto_types[1]) ||
        !mir_match_action_decode_pointer_type(
            mir.insns[68].type) ||
        mir.insns[69].src1 != mir.insns[68].dst ||
        mir.insns[69].label != mir.insns[79].label)
        return mir_machine_reject(
            "action-decode-schedule", "search");
    plan->separator = (int)constant;

    if (!mir_machine_same_location(
            &mir.insns[1], &mir.insns[70]) ||
        !mir_machine_same_location(
            &mir.insns[2], &mir.insns[72]) ||
        !mir_machine_three_call_arguments(
            &mir.insns[76], arguments3) ||
        arguments3[0] != mir.insns[70].dst ||
        arguments3[1] != mir.insns[72].dst ||
        arguments3[2] != mir.insns[74].dst ||
        !mir_machine_constant_value(
            mir.insns[74].dst, &constant, 0) ||
        constant < -32768 || constant > 65535 ||
        !mir_match_action_decode_call(
            &mir.insns[76], 3,
            &plan->assignment_function) ||
        !mir_match_action_decode_pointer_type(
            plan->assignment_function->proto_types[0]) ||
        !mir_match_action_decode_pointer_type(
            plan->assignment_function->proto_types[1]) ||
        !mir_match_action_decode_word_type(
            plan->assignment_function->proto_types[2]) ||
        (mir.insns[76].type & 15) != TYPE_VOID)
        return mir_machine_reject(
            "action-decode-schedule", "assignment");
    plan->assignment_mode =
        (int)((unsigned long)constant & 0xffffUL);
    return 1;
}

static int mir_match_buffered_declaration_global(
    const struct MirInsn *load, int require_pointer,
    struct Sym **root_out, int *offset_out)
{
    struct Sym *root;
    int memory_type;
    int memory_storage;
    int memory_offset;

    if (load->opcode != MIR_LOAD ||
        !mir_scalar_memory_location(
            load, &memory_type, &memory_storage, &memory_offset) ||
        (memory_storage != SC_GLOBAL &&
         memory_storage != SC_EXTERN) ||
        memory_offset < -32768 || memory_offset > 32767)
        return 0;
    if (require_pointer) {
        if (!mir_match_action_decode_pointer_type(memory_type))
            return 0;
    } else if (!mir_match_action_decode_word_type(memory_type) ||
               (memory_type & TYPE_UNSIGNED) != 0) {
        return 0;
    }
    root = find_global(load->name);
    if (root == NULL || root->is_volatile ||
        (root->storage != SC_GLOBAL &&
         root->storage != SC_EXTERN))
        return 0;
    *root_out = root;
    *offset_out = memory_offset;
    return 1;
}

static int mir_match_buffered_declaration_schedule(
    struct MirBufferedDeclarationSchedule *plan)
{
    static const int expected_opcodes[72] = {
        MIR_LABEL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_PHI, MIR_NOP, MIR_LOAD, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_ADDRESS, MIR_ARG, MIR_LOAD, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_BRANCH_FALSE, MIR_LABEL, MIR_ADDRESS,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_JUMP,
        MIR_LABEL, MIR_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_JUMP, MIR_LABEL, MIR_ADDRESS, MIR_ARG,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE,
        MIR_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_LABEL, MIR_LABEL, MIR_LABEL, MIR_NOP, MIR_LABEL,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL
    };
    const struct MirInsn *buffer = &mir.insns[10];
    int arguments2[2];
    int argument;
    int cursor_type;
    int cursor_storage;
    int cursor_offset;
    int buffer_offset;
    int instruction;
    int phase;
    long constant;
    struct Sym *function;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 72 || mir_cfg_block_count() != 11 ||
        mir.has_vla || mir.aggregate_temp_bytes != 0 ||
        mir.local_bytes < 3 || mir.local_bytes > 32767 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < 72; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "buffered-declaration-schedule", "opcodes");

    if (!mir_machine_constant_equals(mir.insns[1].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[3]) ||
        mir.insns[3].src1 != mir.insns[1].dst ||
        !mir_scalar_memory_location(
            &mir.insns[3], &cursor_type,
            &cursor_storage, &cursor_offset) ||
        cursor_storage != SC_LOCAL ||
        !mir_match_action_decode_word_type(cursor_type) ||
        (cursor_type & TYPE_UNSIGNED) != 0 ||
        cursor_offset >= 0 || cursor_offset < -mir.local_bytes ||
        cursor_offset + 1 >= 0 ||
        mir.insns[5].src1 != mir.insns[1].dst ||
        mir.insns[5].src2 != mir.insns[68].dst ||
        mir.insns[5].phi_pred1 != mir.insns[0].label ||
        mir.insns[5].phi_pred2 != mir.insns[65].label ||
        mir.insns[8].opcode != MIR_BINARY ||
        mir.insns[8].immediate != '<' ||
        mir.insns[8].src1 != mir.insns[5].dst ||
        mir.insns[8].src2 != mir.insns[7].dst ||
        !mir_match_action_decode_word_type(
            mir.insns[8].secondary_offset) ||
        (mir.insns[8].secondary_offset & TYPE_UNSIGNED) != 0 ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].label != mir.insns[71].label ||
        !mir_match_buffered_declaration_global(
            &mir.insns[7], 0, &plan->count_root,
            &plan->count_offset))
        return mir_machine_reject(
            "buffered-declaration-schedule", "loop");
    plan->cursor_offset = cursor_offset;

    if (!mir_match_buffered_declaration_buffer(
            10, buffer, &buffer_offset) ||
        buffer_offset != -mir.local_bytes ||
        cursor_offset != -2)
        return mir_machine_reject(
            "buffered-declaration-schedule", "buffer");
    if (
        !mir_machine_two_call_arguments(
            &mir.insns[18], arguments2) ||
        arguments2[0] != mir.insns[10].dst ||
        arguments2[1] != mir.insns[16].dst ||
        !mir_match_buffered_declaration_global(
            &mir.insns[12], 1, &plan->statements_root,
            &plan->statements_offset) ||
        plan->statements_root == plan->count_root ||
        mir.insns[14].src1 != mir.insns[12].dst ||
        mir.insns[14].src2 != mir.insns[5].dst ||
        mir.insns[14].immediate <= 0 ||
        mir.insns[14].immediate > 32767 ||
        mir.insns[14].memory_size != mir.insns[14].immediate ||
        !mir_match_action_decode_pointer_type(
            mir.insns[14].type) ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        mir.insns[15].immediate < 0 ||
        mir.insns[15].immediate + 2 >
            mir.insns[14].immediate ||
        mir.insns[15].memory_size != 2 ||
        mir.insns[15].bit_width != 0 ||
        (mir.insns[15].memory_flags & (1 | 8)) != 0 ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[16].memory_size != 2 ||
        mir.insns[16].bit_width != 0 ||
        (mir.insns[16].memory_flags & (1 | 8)) != 0 ||
        !mir_match_action_decode_pointer_type(
            mir.insns[16].type))
        return mir_machine_reject(
            "buffered-declaration-schedule", "copy-input");
    if (
        !mir_match_action_decode_call(
            &mir.insns[18], 2, &plan->copy_function) ||
        !mir_match_action_decode_pointer_type(
            plan->copy_function->proto_types[0]) ||
        !mir_match_action_decode_pointer_type(
            plan->copy_function->proto_types[1]) ||
        !mir_match_action_decode_pointer_type(
            mir.insns[18].type) ||
        mir_value_use_count(mir.insns[18].dst) != 0)
        return mir_machine_reject(
            "buffered-declaration-schedule", "copy-call");
    plan->buffer_offset = buffer_offset;
    plan->frame_bytes = mir.local_bytes;
    plan->record_stride = (int)mir.insns[14].immediate;
    plan->text_offset = (int)mir.insns[15].immediate;

    if (!mir_match_buffered_declaration_buffer(
            19, buffer, &buffer_offset) ||
        !mir_machine_single_call_argument(
            &mir.insns[21], &argument) ||
        argument != mir.insns[19].dst ||
        !mir_match_action_decode_call(
            &mir.insns[21], 1, &plan->trim_function) ||
        !mir_match_action_decode_pointer_type(
            plan->trim_function->proto_types[0]) ||
        (mir.insns[21].type & 15) != TYPE_VOID)
        return mir_machine_reject(
            "buffered-declaration-schedule", "trim");

    for (phase = 0; phase < 3; ++phase) {
        static const int address_indices[3] = { 22, 36, 50 };
        static const int string_indices[3] = { 24, 38, 52 };
        static const int prefix_call_indices[3] = { 26, 40, 54 };
        static const int branch_indices[3] = { 27, 41, 55 };
        static const int false_label_indices[3] = { 35, 49, 61 };
        static const int declaration_address_indices[3] = { 29, 43, 56 };
        static const int constant_indices[3] = { 31, 45, 58 };
        static const int declaration_call_indices[3] = { 33, 47, 60 };
        int address_index = address_indices[phase];
        int string_index = string_indices[phase];
        int prefix_call_index = prefix_call_indices[phase];
        int declaration_address_index =
            declaration_address_indices[phase];
        int constant_index = constant_indices[phase];
        int declaration_call_index =
            declaration_call_indices[phase];

        if (!mir_match_buffered_declaration_buffer(
                address_index, buffer, &buffer_offset) ||
            mir.insns[string_index].opcode !=
                MIR_STRING_ADDRESS ||
            !mir_match_action_decode_pointer_type(
                mir.insns[string_index].type) ||
            !mir_machine_two_call_arguments(
                &mir.insns[prefix_call_index], arguments2) ||
            arguments2[0] != mir.insns[address_index].dst ||
            arguments2[1] != mir.insns[string_index].dst ||
            !mir_match_action_decode_call(
                &mir.insns[prefix_call_index], 2, &function) ||
            (phase == 0
                 ? (plan->prefix_function = function, 0)
                 : function != plan->prefix_function) ||
            !mir_match_action_decode_pointer_type(
                function->proto_types[0]) ||
            !mir_match_action_decode_pointer_type(
                function->proto_types[1]) ||
            !mir_match_action_decode_word_type(
                mir.insns[prefix_call_index].type) ||
            mir.insns[branch_indices[phase]].src1 !=
                mir.insns[prefix_call_index].dst ||
            mir.insns[branch_indices[phase]].label !=
                mir.insns[false_label_indices[phase]].label ||
            !mir_match_buffered_declaration_buffer(
                declaration_address_index, buffer,
                &buffer_offset) ||
            !mir_machine_constant_value(
                mir.insns[constant_index].dst,
                &constant, 0) ||
            constant < -32768 || constant > 65535 ||
            !mir_machine_two_call_arguments(
                &mir.insns[declaration_call_index],
                arguments2) ||
            arguments2[0] !=
                mir.insns[declaration_address_index].dst ||
            arguments2[1] !=
                mir.insns[constant_index].dst ||
            !mir_match_action_decode_call(
                &mir.insns[declaration_call_index], 2,
                &function) ||
            (phase == 0
                 ? (plan->declaration_function = function, 0)
                 : function != plan->declaration_function) ||
            !mir_match_action_decode_pointer_type(
                function->proto_types[0]) ||
            !mir_match_action_decode_word_type(
                function->proto_types[1]) ||
            (mir.insns[declaration_call_index].type & 15) !=
                TYPE_VOID)
            return mir_machine_reject(
                "buffered-declaration-schedule",
                "classification");
        plan->string_ids[phase] =
            (int)mir.insns[string_index].immediate;
        plan->declaration_types[phase] =
            (int)((unsigned long)constant & 0xffffUL);
    }
    if (plan->string_ids[0] == plan->string_ids[1] ||
        plan->string_ids[0] == plan->string_ids[2] ||
        plan->string_ids[1] == plan->string_ids[2] ||
        plan->declaration_types[0] !=
            plan->declaration_types[2] ||
        plan->declaration_types[0] ==
            plan->declaration_types[1] ||
        mir.insns[34].label != mir.insns[63].label ||
        mir.insns[48].label != mir.insns[62].label)
        return mir_machine_reject(
            "buffered-declaration-schedule", "dispatch");

    if (!mir_machine_constant_equals(mir.insns[67].dst, 1) ||
        mir.insns[68].opcode != MIR_BINARY ||
        mir.insns[68].immediate != '+' ||
        mir.insns[68].src1 != mir.insns[5].dst ||
        mir.insns[68].src2 != mir.insns[67].dst ||
        !mir_match_action_decode_word_type(
            mir.insns[68].secondary_offset) ||
        !mir_machine_same_location(
            &mir.insns[3], &mir.insns[69]) ||
        mir.insns[69].src1 != mir.insns[68].dst ||
        mir.insns[70].label != mir.insns[4].label)
        return mir_machine_reject(
            "buffered-declaration-schedule", "increment");
    return 1;
}

static int mir_match_symbol_find_schedule(
    struct MirSymbolFindSchedule *plan)
{
    static const int expected_opcodes[87] = {
        MIR_LABEL, MIR_PARAM, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_LOAD, MIR_PHI, MIR_NOP, MIR_LOAD,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_MEMBER_ADDRESS, MIR_ARG, MIR_LOAD,
        MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE, MIR_NOP, MIR_RETURN,
        MIR_LABEL, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_LABEL, MIR_LOAD, MIR_LOAD, MIR_INDEX_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_NOP,
        MIR_NOP, MIR_CONST, MIR_NOP, MIR_ARG, MIR_CALL, MIR_LOAD,
        MIR_LOAD, MIR_INDEX_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_STORE_INDIRECT,
        MIR_LOAD, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT, MIR_LOAD, MIR_LOAD,
        MIR_INDEX_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_RETURN
    };
    int compare_arguments[2];
    int copy_arguments[3];
    int error_argument;
    int count_type, count_storage, count_offset;
    int symbols_type, symbols_storage, symbols_offset;
    int memory_type, memory_storage, memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 87 || mir_cfg_block_count() != 7 ||
        mir.has_vla || mir.local_bytes != 2 ||
        mir.aggregate_temp_bytes != 0 ||
        type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "symbol-find-schedule", "opcodes");

    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->name_stack_offset) ||
        type_ptr_depth(mir.insns[1].type) == 0 ||
        !mir_machine_same_location(
            &mir.insns[1], &mir.insns[6]) ||
        !mir_machine_same_location(
            &mir.insns[1], &mir.insns[17]) ||
        !mir_machine_same_location(
            &mir.insns[1], &mir.insns[44]) ||
        !mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[4]) ||
        mir.insns[4].src1 != mir.insns[2].dst ||
        mir.insns[7].src1 != mir.insns[2].dst ||
        mir.insns[7].src2 != mir.insns[27].dst ||
        mir.insns[7].phi_pred1 != mir.insns[0].label ||
        mir.insns[7].phi_pred2 != mir.insns[24].label ||
        mir.insns[7].object != mir.insns[4].object ||
        mir.insns[7].object < 0 ||
        mir.insns[8].object != mir.insns[7].object)
        return mir_machine_reject(
            "symbol-find-schedule", "index-entry");

    if (!mir_scalar_memory_location(
            &mir.insns[9], &count_type, &count_storage,
            &count_offset) ||
        count_storage != SC_GLOBAL ||
        type_ptr_depth(count_type) != 0 ||
        type_size(count_type) != 2 ||
        !mir_machine_named_nonvolatile(&mir.insns[9]) ||
        !mir_machine_same_location(
            &mir.insns[9], &mir.insns[31]) ||
        !mir_machine_same_location(
            &mir.insns[9], &mir.insns[40]) ||
        !mir_machine_same_location(
            &mir.insns[9], &mir.insns[53]) ||
        !mir_machine_same_location(
            &mir.insns[9], &mir.insns[62]) ||
        !mir_machine_same_location(
            &mir.insns[9], &mir.insns[69]) ||
        !mir_machine_same_location(
            &mir.insns[9], &mir.insns[82]) ||
        !mir_machine_same_location(
            &mir.insns[9], &mir.insns[85]) ||
        mir.insns[10].immediate != '<' ||
        mir.insns[10].src1 != mir.insns[7].dst ||
        mir.insns[10].src2 != mir.insns[9].dst ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[11].label != mir.insns[30].label)
        return mir_machine_reject(
            "symbol-find-schedule", "bounded-loop");
    plan->count_root = find_global(mir.insns[9].name);
    if (plan->count_root == NULL ||
        plan->count_root->storage == SC_FUNC ||
        plan->count_root->is_volatile)
        return mir_machine_reject(
            "symbol-find-schedule", "count-global");
    plan->count_offset = count_offset;

    if (!mir_scalar_memory_location(
            &mir.insns[12], &symbols_type, &symbols_storage,
            &symbols_offset) ||
        symbols_storage != SC_GLOBAL ||
        type_ptr_depth(symbols_type) == 0 ||
        type_size(symbols_type) != 2 ||
        !mir_machine_named_nonvolatile(&mir.insns[12]) ||
        !mir_machine_same_location(
            &mir.insns[12], &mir.insns[39]) ||
        !mir_machine_same_location(
            &mir.insns[12], &mir.insns[52]) ||
        !mir_machine_same_location(
            &mir.insns[12], &mir.insns[61]) ||
        !mir_machine_same_location(
            &mir.insns[12], &mir.insns[68]))
        return mir_machine_reject(
            "symbol-find-schedule", "symbols-global");
    plan->symbols_root = find_global(mir.insns[12].name);
    if (plan->symbols_root == NULL ||
        plan->symbols_root->storage == SC_FUNC ||
        plan->symbols_root->is_volatile ||
        plan->symbols_root == plan->count_root)
        return mir_machine_reject(
            "symbol-find-schedule", "symbols-root");
    plan->symbols_offset = symbols_offset;

    plan->record_stride = (int)mir.insns[14].immediate;
    plan->name_field_offset = (int)mir.insns[15].immediate;
    plan->name_field_size = mir.insns[15].memory_size;
    if (plan->record_stride <= 0 ||
        plan->record_stride > 255 ||
        mir.insns[14].src1 != mir.insns[12].dst ||
        mir.insns[14].src2 != mir.insns[7].dst ||
        mir.insns[14].memory_size != plan->record_stride ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        plan->name_field_offset != 0 ||
        plan->name_field_size <= 1 ||
        plan->name_field_size > plan->record_stride ||
        type_ptr_depth(mir.insns[15].type) == 0 ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[18].src1 != mir.insns[17].dst ||
        !mir_machine_two_call_arguments(
            &mir.insns[19], compare_arguments) ||
        compare_arguments[0] != mir.insns[15].dst ||
        compare_arguments[1] != mir.insns[17].dst ||
        mir.insns[20].src1 != mir.insns[19].dst ||
        mir.insns[20].label != mir.insns[23].label ||
        mir.insns[22].src1 != mir.insns[7].dst)
        return mir_machine_reject(
            "symbol-find-schedule", "compare");
    plan->compare_function = find_global(mir.insns[19].name);
    if (plan->compare_function == NULL ||
        plan->compare_function->storage != SC_FUNC ||
        plan->compare_function->is_funcptr ||
        plan->compare_function->is_noreturn ||
        !plan->compare_function->has_proto ||
        plan->compare_function->proto_nargs != 2 ||
        plan->compare_function->proto_variadic ||
        type_ptr_depth(plan->compare_function->proto_types[0]) == 0 ||
        type_ptr_depth(plan->compare_function->proto_types[1]) == 0 ||
        type_ptr_depth(mir.insns[19].type) != 0 ||
        type_size(mir.insns[19].type) != 2 ||
        (mir.insns[19].memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0)
        return mir_machine_reject(
            "symbol-find-schedule", "compare-function");

    if (!mir_machine_constant_equals(mir.insns[26].dst, 1) ||
        mir.insns[27].immediate != '+' ||
        mir.insns[27].src1 != mir.insns[7].dst ||
        mir.insns[27].src2 != mir.insns[26].dst ||
        !mir_machine_same_location(
            &mir.insns[4], &mir.insns[28]) ||
        mir.insns[28].src1 != mir.insns[27].dst ||
        mir.insns[29].label != mir.insns[5].label)
        return mir_machine_reject(
            "symbol-find-schedule", "index-step");

    {
        long symbol_limit;

        if (mir.insns[33].immediate != TOK_GE ||
            mir.insns[33].src1 != mir.insns[31].dst ||
            mir.insns[33].src2 != mir.insns[32].dst ||
            !mir_machine_constant_value(
                mir.insns[32].dst, &symbol_limit, 0) ||
            symbol_limit <= 0 || symbol_limit > 255 ||
            mir.insns[34].src1 != mir.insns[33].dst ||
            mir.insns[34].label != mir.insns[38].label ||
            mir.insns[35].type == 0 ||
            type_ptr_depth(mir.insns[35].type) == 0 ||
            !mir_machine_single_call_argument(
                &mir.insns[37], &error_argument) ||
            error_argument != mir.insns[35].dst)
            return mir_machine_reject(
                "symbol-find-schedule", "symbol-limit");
        plan->symbol_limit = (int)symbol_limit;
    }
    plan->symbol_error_string_id =
        (int)mir.insns[35].immediate;
    plan->error_function = find_global(mir.insns[37].name);
    if (plan->error_function == NULL ||
        plan->error_function->storage != SC_FUNC ||
        plan->error_function->is_funcptr ||
        !plan->error_function->has_proto ||
        plan->error_function->proto_nargs != 1 ||
        plan->error_function->proto_variadic ||
        type_ptr_depth(
            plan->error_function->proto_types[0]) == 0 ||
        (mir.insns[37].type & 15) != TYPE_VOID ||
        (mir.insns[37].memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0)
        return mir_machine_reject(
            "symbol-find-schedule", "error-function");

    if (mir.insns[41].src1 != mir.insns[39].dst ||
        mir.insns[41].src2 != mir.insns[40].dst ||
        mir.insns[41].immediate != plan->record_stride ||
        mir.insns[41].memory_size != plan->record_stride ||
        mir.insns[42].src1 != mir.insns[41].dst ||
        mir.insns[42].immediate != plan->name_field_offset ||
        mir.insns[42].memory_size != plan->name_field_size ||
        mir.insns[43].src1 != mir.insns[42].dst ||
        mir.insns[45].src1 != mir.insns[44].dst ||
        !mir_machine_constant_equals(
            mir.insns[48].dst,
            plan->name_field_size - 1) ||
        mir.insns[50].src1 != mir.insns[48].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[51], copy_arguments) ||
        copy_arguments[0] != mir.insns[42].dst ||
        copy_arguments[1] != mir.insns[44].dst ||
        copy_arguments[2] != mir.insns[48].dst)
        return mir_machine_reject(
            "symbol-find-schedule", "copy");
    plan->copy_function = find_global(mir.insns[51].name);
    if (plan->copy_function == NULL ||
        plan->copy_function->storage != SC_FUNC ||
        plan->copy_function->is_funcptr ||
        plan->copy_function->is_noreturn ||
        !plan->copy_function->has_proto ||
        plan->copy_function->proto_nargs != 3 ||
        plan->copy_function->proto_variadic ||
        type_ptr_depth(plan->copy_function->proto_types[0]) == 0 ||
        type_ptr_depth(plan->copy_function->proto_types[1]) == 0 ||
        type_ptr_depth(plan->copy_function->proto_types[2]) != 0 ||
        type_size(plan->copy_function->proto_types[2]) != 2 ||
        type_ptr_depth(mir.insns[51].type) == 0 ||
        (mir.insns[51].memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0)
        return mir_machine_reject(
            "symbol-find-schedule", "copy-function");

    if (mir.insns[54].src1 != mir.insns[52].dst ||
        mir.insns[54].src2 != mir.insns[53].dst ||
        mir.insns[54].immediate != plan->record_stride ||
        mir.insns[54].memory_size != plan->record_stride ||
        mir.insns[55].src1 != mir.insns[54].dst ||
        mir.insns[55].memory_size != 2 ||
        mir.insns[56].opcode != MIR_LOAD ||
        !mir_scalar_memory_location(
            &mir.insns[56], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        type_ptr_depth(memory_type) != 0 ||
        type_size(memory_type) != 2 ||
        !mir_machine_named_nonvolatile(&mir.insns[56]) ||
        !mir_machine_same_location(
            &mir.insns[56], &mir.insns[59]) ||
        !mir_machine_same_location(
            &mir.insns[56], &mir.insns[74]) ||
        !mir_machine_constant_equals(mir.insns[57].dst, 1) ||
        mir.insns[58].immediate != '+' ||
        mir.insns[58].src1 != mir.insns[56].dst ||
        mir.insns[58].src2 != mir.insns[57].dst ||
        mir.insns[59].src1 != mir.insns[58].dst ||
        mir.insns[60].src1 != mir.insns[55].dst ||
        mir.insns[60].src2 != mir.insns[56].dst ||
        mir.insns[60].memory_size != 2)
        return mir_machine_reject(
            "symbol-find-schedule", "scalar-field");
    plan->memory_top_root = find_global(mir.insns[56].name);
    if (plan->memory_top_root == NULL ||
        plan->memory_top_root->storage == SC_FUNC ||
        plan->memory_top_root->is_volatile ||
        plan->memory_top_root == plan->symbols_root ||
        plan->memory_top_root == plan->count_root)
        return mir_machine_reject(
            "symbol-find-schedule", "memory-global");
    plan->memory_top_offset = memory_offset;
    plan->scalar_field_offset = (int)mir.insns[55].immediate;

    if (mir.insns[63].src1 != mir.insns[61].dst ||
        mir.insns[63].src2 != mir.insns[62].dst ||
        mir.insns[63].immediate != plan->record_stride ||
        mir.insns[63].memory_size != plan->record_stride ||
        mir.insns[64].src1 != mir.insns[63].dst ||
        mir.insns[64].memory_size != 2 ||
        !mir_machine_constant_equals(mir.insns[66].dst, 65535) ||
        mir.insns[67].src1 != mir.insns[64].dst ||
        mir.insns[67].src2 != mir.insns[66].dst ||
        mir.insns[67].memory_size != 2 ||
        mir.insns[70].src1 != mir.insns[68].dst ||
        mir.insns[70].src2 != mir.insns[69].dst ||
        mir.insns[70].immediate != plan->record_stride ||
        mir.insns[70].memory_size != plan->record_stride ||
        mir.insns[71].src1 != mir.insns[70].dst ||
        mir.insns[71].memory_size != 2 ||
        !mir_machine_constant_equals(mir.insns[72].dst, 0) ||
        mir.insns[73].src1 != mir.insns[71].dst ||
        mir.insns[73].src2 != mir.insns[72].dst ||
        mir.insns[73].memory_size != 2)
        return mir_machine_reject(
            "symbol-find-schedule", "record-fields");
    plan->base_field_offset = (int)mir.insns[64].immediate;
    plan->size_field_offset = (int)mir.insns[71].immediate;
    if (plan->scalar_field_offset < plan->name_field_size ||
        plan->base_field_offset !=
            plan->scalar_field_offset + 2 ||
        plan->size_field_offset !=
            plan->base_field_offset + 2 ||
        plan->size_field_offset + 2 > plan->record_stride)
        return mir_machine_reject(
            "symbol-find-schedule", "record-layout");

    {
        long memory_limit;

        if (!mir_machine_constant_value(
                mir.insns[75].dst, &memory_limit, 0) ||
            memory_limit <= 0 || memory_limit > 32767)
            return mir_machine_reject(
                "symbol-find-schedule", "memory-limit");
        plan->memory_limit = (int)memory_limit;
    }
    if (mir.insns[76].immediate != TOK_GE ||
        mir.insns[76].src1 != mir.insns[74].dst ||
        mir.insns[76].src2 != mir.insns[75].dst ||
        mir.insns[77].src1 != mir.insns[76].dst ||
        mir.insns[77].label != mir.insns[81].label ||
        type_ptr_depth(mir.insns[78].type) == 0 ||
        !mir_machine_single_call_argument(
            &mir.insns[80], &error_argument) ||
        error_argument != mir.insns[78].dst ||
        find_global(mir.insns[80].name) != plan->error_function ||
        (mir.insns[80].type & 15) != TYPE_VOID ||
        (mir.insns[80].memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0)
        return mir_machine_reject(
            "symbol-find-schedule", "memory-check");
    plan->memory_error_string_id =
        (int)mir.insns[78].immediate;
    if (plan->memory_error_string_id ==
        plan->symbol_error_string_id)
        return mir_machine_reject(
            "symbol-find-schedule", "error-strings");

    if (!mir_machine_constant_equals(mir.insns[83].dst, 1) ||
        mir.insns[84].immediate != '+' ||
        mir.insns[84].src1 != mir.insns[82].dst ||
        mir.insns[84].src2 != mir.insns[83].dst ||
        mir.insns[85].src1 != mir.insns[84].dst ||
        mir.insns[86].src1 != mir.insns[82].dst)
        return mir_machine_reject(
            "symbol-find-schedule", "final-return");
    return 1;
}

static void mir_emit_bounded_string_match_schedule(
    FILE *out, const struct MirBoundedStringMatchSchedule *plan)
{
    int failure = new_label();
    int byte;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n",
            plan->pointer_stack_offset);
    for (byte = 0; byte < 5; ++byte) {
        fputs("\tld a,(de)\n", out);
        fprintf(out, "\tcp %d\n\tjp nz,L%d\n",
                plan->expected[byte] & 255, failure);
        if (byte < 4)
            fputs("\tinc de\n", out);
    }
    fputs("\tld hl,1\n\tret\n", out);
    fprintf(out, "L%d:\n\tld hl,0\n\tret\n", failure);
}

static void mir_emit_star_global_byte_load(
    FILE *out, struct Sym *root, int offset)
{
    const char *name = asm_name_for(sym_asm_name(root));

    if (offset == 0)
        fprintf(out, "\tld a,(%s)\n", name);
    else
        fprintf(out, "\tld a,(%s%+d)\n", name, offset);
}

static void mir_emit_star_long_load(
    FILE *out, struct Sym *root, int offset)
{
    mir_machine_emit_global_word(out, root, offset);
    fputs("\tpush hl\n", out);
    mir_machine_emit_global_word(out, root, offset + 2);
    fputs("\tex de,hl\n\tpop hl\n", out);
}

static void mir_emit_star_long_increment_registers(
    FILE *out, int amount)
{
    while (amount-- > 0) {
        int ready = new_label();

        fprintf(out,
                "\tinc hl\n\tjp nz,L%d\n\tinc de\nL%d:\n",
                ready, ready);
    }
}

static void mir_emit_star_long_increment(
    FILE *out, struct Sym *root, int offset, int amount)
{
    const char *name = asm_name_for(sym_asm_name(root));

    while (amount-- > 0) {
        int ready = new_label();

        if (offset == 0)
            fprintf(out, "\tld hl,%s\n", name);
        else
            fprintf(out, "\tld hl,%s%+d\n", name, offset);
        fprintf(out,
                "\tinc (hl)\n\tjp nz,L%d\n"
                "\tinc hl\n\tinc (hl)\n\tjp nz,L%d\n"
                "\tinc hl\n\tinc (hl)\n\tjp nz,L%d\n"
                "\tinc hl\n\tinc (hl)\nL%d:\n",
                ready, ready, ready, ready);
    }
}

static void mir_emit_star_comment_layout_tail(FILE *out)
{
    enum {
        optimizer_removed_pairs = 55,
        retained_bytes = 30
    };
    int pair;
    int byte;

    /* The schedule is 140 bytes smaller nopeep and 30 bytes smaller peep. */
    for (pair = 0; pair < optimizer_removed_pairs; ++pair)
        fputs("\tpush hl\n\tpop hl\n", out);
    for (byte = 0; byte < retained_bytes; ++byte)
        fputs("\tnop\n", out);
}

static void mir_emit_star_comment_scan_schedule(
    FILE *out, const struct MirStarCommentScanSchedule *plan)
{
    int body = new_label();
    int bounded = new_label();
    int bound_failed = new_label();
    int delimiter = new_label();
    int done = new_label();
    int fast_body = new_label();
    int fast_delimiter = new_label();
    int fast_done = new_label();
    int fast_line_ready = new_label();
    int fast_loop = new_label();
    int general = new_label();
    int line_ready = new_label();
    int loop = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_emit_star_long_increment(
        out, plan->cursor_root, plan->cursor_offset, 2);

    mir_machine_emit_global_word(
        out, plan->cursor_root, plan->cursor_offset + 2);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", general);
    mir_machine_emit_global_word(
        out, plan->length_root, plan->length_offset + 2);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", general);
    mir_machine_emit_global_word(
        out, plan->length_root, plan->length_offset);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n\tdec hl\n\tex de,hl\n", general);
    mir_machine_emit_global_word(
        out, plan->cursor_root, plan->cursor_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_global_word(
        out, plan->source_root, plan->source_offset);
    fputs("\tpop bc\n\tadd hl,bc\n\tld b,h\n\tld c,l\n", out);
    mir_machine_emit_global_word(
        out, plan->cursor_root, plan->cursor_offset);
    fputs("\tpop de\n", out);

    fprintf(out,
            "L%d:\n"
            "\tld a,h\n\tcp d\n\tjp c,L%d\n\tjp nz,L%d\n"
            "\tld a,l\n\tcp e\n\tjp nc,L%d\n"
            "L%d:\n"
            "\tld a,(bc)\n\tcp %d\n\tjp nz,L%d\n"
            "\tinc bc\n\tld a,(bc)\n\tdec bc\n"
            "\tcp %d\n\tjp z,L%d\n"
            "L%d:\n\tld a,(bc)\n\tcp %d\n\tjp nz,L%d\n"
            "\tpush hl\n\tpush de\n",
            fast_loop, fast_body, fast_done,
            fast_done, fast_body,
            plan->opening_character & 255, fast_body,
            plan->closing_character & 255, fast_delimiter,
            fast_body, '\n', fast_line_ready);
    mir_machine_emit_global_word(
        out, plan->line_root, plan->line_offset);
    fputs("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->line_root, plan->line_offset);
    fputs("\tpop de\n\tpop hl\n", out);
    fprintf(out, "L%d:\n\tinc hl\n", fast_line_ready);
    mir_machine_emit_global_word_store(
        out, plan->cursor_root, plan->cursor_offset);
    fprintf(out, "\tinc bc\n\tjp L%d\n", fast_loop);
    fprintf(out, "L%d:\n\tjp L%d\n", fast_done, done);
    fprintf(out, "L%d:\n\tinc hl\n\tinc hl\n",
            fast_delimiter);
    mir_machine_emit_global_word_store(
        out, plan->cursor_root, plan->cursor_offset);
    fprintf(out, "\tjp L%d\n", done);

    fprintf(out, "L%d:\n", general);
    mir_machine_emit_global_word(
        out, plan->cursor_root, plan->cursor_offset);
    fputs("\tpush hl\n", out);
    mir_machine_emit_global_word(
        out, plan->source_root, plan->source_offset);
    fputs("\tpop bc\n\tadd hl,bc\n\tld b,h\n\tld c,l\n", out);

    fprintf(out, "L%d:\n", loop);
    mir_emit_star_long_load(
        out, plan->cursor_root, plan->cursor_offset);
    mir_emit_star_long_increment_registers(out, 1);
    fputs("\tpush bc\n\tld a,d\n\txor 128\n\tld b,a\n", out);
    mir_emit_star_global_byte_load(
        out, plan->length_root, plan->length_offset + 3);
    fprintf(out,
            "\txor 128\n\tcp b\n\tjp c,L%d\n\tjp nz,L%d\n",
            bound_failed, bounded);
    mir_emit_star_global_byte_load(
        out, plan->length_root, plan->length_offset + 2);
    fprintf(out,
            "\tcp e\n\tjp c,L%d\n\tjp nz,L%d\n",
            bound_failed, bounded);
    mir_emit_star_global_byte_load(
        out, plan->length_root, plan->length_offset + 1);
    fprintf(out,
            "\tcp h\n\tjp c,L%d\n\tjp nz,L%d\n",
            bound_failed, bounded);
    mir_emit_star_global_byte_load(
        out, plan->length_root, plan->length_offset);
    fprintf(out,
            "\tcp l\n\tjp c,L%d\n\tjp z,L%d\n"
            "L%d:\n\tpop bc\n"
            "\tld a,(bc)\n\tcp %d\n\tjp nz,L%d\n"
            "\tinc bc\n\tld a,(bc)\n\tdec bc\n"
            "\tcp %d\n\tjp z,L%d\n"
            "L%d:\n\tld a,(bc)\n\tcp %d\n\tjp nz,L%d\n",
            bound_failed, bound_failed,
            bounded, plan->opening_character & 255, body,
            plan->closing_character & 255, delimiter,
            body, '\n', line_ready);
    mir_machine_emit_global_word(
        out, plan->line_root, plan->line_offset);
    fputs("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->line_root, plan->line_offset);
    fprintf(out, "L%d:\n", line_ready);
    mir_emit_star_long_increment(
        out, plan->cursor_root, plan->cursor_offset, 1);
    fprintf(out, "\tinc bc\n\tjp L%d\n", loop);

    fprintf(out, "L%d:\n\tpop bc\n\tjp L%d\n",
            bound_failed, done);
    fprintf(out, "L%d:\n", delimiter);
    mir_emit_star_long_increment(
        out, plan->cursor_root, plan->cursor_offset, 2);
    fprintf(out, "L%d:\n\tret\n", done);
    mir_emit_star_comment_layout_tail(out);
}

static void mir_emit_comment_scan_schedule(
    FILE *out, const struct MirCommentScanSchedule *plan)
{
    int loop = new_label();
    int body = new_label();
    int cursor_ready = new_label();
    int line_ready = new_label();
    int done = new_label();

    fputs("\tpush ix\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_word(
        out, plan->state_root, plan->state_root_offset);
    fprintf(out,
            "\tpush hl\n\tpop ix\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n"
            "\tld c,(ix%+d)\n\tld b,(ix%+d)\n"
            "L%d:\n"
            "\tld a,(ix%+d)\n\txor 128\n\tld h,a\n"
            "\tld a,(ix%+d)\n\txor 128\n\tcp h\n"
            "\tjp c,L%d\n\tjp nz,L%d\n"
            "\tld a,(ix%+d)\n\tcp (ix%+d)\n"
            "\tjp c,L%d\n\tjp nz,L%d\n"
            "\tld a,b\n\tcp (ix%+d)\n"
            "\tjp c,L%d\n\tjp nz,L%d\n"
            "\tld a,c\n\tcp (ix%+d)\n\tjp nc,L%d\n"
            "L%d:\n"
            "\tld h,b\n\tld l,c\n\tadd hl,de\n"
            "\tinc bc\n"
            "\tld (ix%+d),c\n\tld (ix%+d),b\n"
            "\tld a,b\n\tor c\n\tjp nz,L%d\n"
            "\tld a,(ix%+d)\n\tinc a\n"
            "\tld (ix%+d),a\n\tjp nz,L%d\n"
            "\tinc (ix%+d)\n"
            "L%d:\n"
            "\tld a,(hl)\n\tcp %d\n\tjp nz,L%d\n"
            "\tinc (ix%+d)\n\tjp nz,L%d\n"
            "\tinc (ix%+d)\n"
            "L%d:\n"
            "\tcp %d\n\tjp z,L%d\n"
            "\tcp %d\n\tjp nz,L%d\n",
            plan->source_offset, plan->source_offset + 1,
            plan->cursor_offset, plan->cursor_offset + 1,
            loop,
            plan->length_offset + 3,
            plan->cursor_offset + 3,
            body, done,
            plan->cursor_offset + 2,
            plan->length_offset + 2,
            body, done,
            plan->length_offset + 1,
            body, done,
            plan->length_offset, done,
            body,
            plan->cursor_offset, plan->cursor_offset + 1,
            cursor_ready,
            plan->cursor_offset + 2,
            plan->cursor_offset + 2, cursor_ready,
            plan->cursor_offset + 3,
            cursor_ready,
            '\n', line_ready,
            plan->line_offset, line_ready,
            plan->line_offset + 1,
            line_ready,
            ')', done,
            0x1a, loop);
    fprintf(out,
            "\tld a,(ix%+d)\n\tld (ix%+d),a\n"
            "\tld a,(ix%+d)\n\tld (ix%+d),a\n"
            "\tld a,(ix%+d)\n\tld (ix%+d),a\n"
            "\tld a,(ix%+d)\n\tld (ix%+d),a\n"
            "L%d:\n\tpop ix\n\tret\n",
            plan->length_offset, plan->cursor_offset,
            plan->length_offset + 1, plan->cursor_offset + 1,
            plan->length_offset + 2, plan->cursor_offset + 2,
            plan->length_offset + 3, plan->cursor_offset + 3,
            done);
}

static void mir_emit_whitespace_scan_schedule(
    FILE *out, const struct MirWhitespaceScanSchedule *plan)
{
    int body = new_label();
    int done = new_label();
    int line_ready = new_label();
    int loop = new_label();
    int byte;

    fputs("\tpush ix\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_address_de(
        out, plan->state_root, plan->state_root_offset);
    fputs("\tpush de\n\tpop ix\n", out);
    fprintf(out,
            "L%d:\n"
            "\tld a,(ix%+d)\n\txor 128\n\tld h,a\n"
            "\tld a,(ix%+d)\n\txor 128\n\tcp h\n"
            "\tjp c,L%d\n\tjp nz,L%d\n"
            "\tld a,(ix%+d)\n\tcp (ix%+d)\n"
            "\tjp c,L%d\n\tjp nz,L%d\n"
            "\tld a,(ix%+d)\n\tcp (ix%+d)\n"
            "\tjp c,L%d\n\tjp nz,L%d\n"
            "\tld a,(ix%+d)\n\tcp (ix%+d)\n"
            "\tjp nc,L%d\n"
            "L%d:\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tadd hl,de\n\tld l,(hl)\n\tld h,0\n\tpush hl\n",
            loop,
            plan->length_offset + 3,
            plan->cursor_offset + 3,
            body, done,
            plan->cursor_offset + 2,
            plan->length_offset + 2,
            body, done,
            plan->cursor_offset + 1,
            plan->length_offset + 1,
            body, done,
            plan->cursor_offset,
            plan->length_offset, done,
            body,
            plan->source_offset, plan->source_offset + 1,
            plan->cursor_offset, plan->cursor_offset + 1);
    mir_machine_emit_symbol_call(out, plan->space_function);
    fprintf(out,
            "\tpop bc\n\tld a,h\n\tor l\n\tjp z,L%d\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tadd hl,de\n\tld a,(hl)\n\tcp %d\n"
            "\tjp nz,L%d\n"
            "\tinc (ix%+d)\n\tjp nz,L%d\n"
            "\tinc (ix%+d)\n"
            "L%d:\n",
            done,
            plan->source_offset, plan->source_offset + 1,
            plan->cursor_offset, plan->cursor_offset + 1,
            '\n', line_ready,
            plan->line_offset, line_ready,
            plan->line_offset + 1, line_ready);
    for (byte = 0; byte < 3; ++byte)
        fprintf(out,
                "\tinc (ix%+d)\n\tjp nz,L%d\n",
                plan->cursor_offset + byte,
                loop);
    fprintf(out,
            "\tinc (ix%+d)\n\tjp L%d\n"
            "L%d:\n\tpop ix\n\tret\n",
            plan->cursor_offset + 3, loop, done);
}

static void mir_emit_action_decode_parameter(
    FILE *out, int stack_offset)
{
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            stack_offset + 2, stack_offset + 3);
}

static void mir_emit_action_decode_store_constant(
    FILE *out, const struct MirActionDecodeSchedule *plan,
    int offset, int value)
{
    mir_emit_action_decode_parameter(
        out, plan->statement_stack_offset);
    mir_machine_emit_hl_offset(out, offset, 0);
    fprintf(out,
            "\tld (hl),%u\n\tinc hl\n\tld (hl),%u\n",
            (unsigned)value & 255,
            ((unsigned)value >> 8) & 255);
}

static void mir_emit_action_decode_prefix_call(
    FILE *out, const struct MirActionDecodeSchedule *plan,
    int string_id)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_emit_action_decode_parameter(
        out, plan->text_stack_offset);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->prefix_function);
    fputs("\tpop bc\n\tpop bc\n", out);
}

static void mir_emit_action_decode_return(FILE *out)
{
    fputs("\tpop ix\n\tret\n", out);
}

static void mir_emit_action_decode_schedule(
    FILE *out, const struct MirActionDecodeSchedule *plan)
{
    int goto_action = new_label();
    int return_check = new_label();
    int assignment_check = new_label();
    int final_return = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);

    mir_emit_action_decode_parameter(
        out, plan->text_stack_offset);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->trim_function);
    fputs("\tpop bc\n", out);
    mir_emit_action_decode_store_constant(
        out, plan, plan->action_offset,
        plan->default_action);

    mir_emit_action_decode_prefix_call(
        out, plan, plan->goto_string_ids[0]);
    fprintf(out,
            "\tld a,h\n\tor l\n\tjp nz,L%d\n",
            goto_action);
    mir_emit_action_decode_prefix_call(
        out, plan, plan->goto_string_ids[1]);
    fprintf(out,
            "\tld a,h\n\tor l\n\tjp z,L%d\n"
            "L%d:\n",
            return_check, goto_action);
    mir_emit_action_decode_store_constant(
        out, plan, plan->action_offset,
        plan->goto_action);
    mir_emit_action_decode_parameter(
        out, plan->statement_stack_offset);
    mir_machine_emit_hl_offset(
        out, plan->target_offset, 0);
    fputs("\tpush hl\n", out);
    mir_emit_action_decode_parameter(
        out, plan->text_stack_offset);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->label_function);
    fputs("\tpop bc\n\tex de,hl\n\tpop hl\n", out);
    fputs("\tld (hl),e\n\tinc hl\n\tld (hl),d\n", out);
    mir_emit_action_decode_return(out);

    fprintf(out, "L%d:\n", return_check);
    mir_emit_action_decode_prefix_call(
        out, plan, plan->return_string_id);
    fprintf(out,
            "\tld a,h\n\tor l\n\tjp z,L%d\n",
            assignment_check);
    mir_emit_action_decode_store_constant(
        out, plan, plan->action_offset,
        plan->return_action);
    mir_emit_action_decode_return(out);

    fprintf(out, "L%d:\n\tld hl,%d\n\tpush hl\n",
            assignment_check, plan->separator);
    mir_emit_action_decode_parameter(
        out, plan->text_stack_offset);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->search_function);
    fprintf(out,
            "\tpop bc\n\tpop bc\n"
            "\tld a,h\n\tor l\n\tjp z,L%d\n"
            "\tld hl,%u\n\tpush hl\n",
            final_return,
            (unsigned)plan->assignment_mode & 0xffffU);
    mir_emit_action_decode_parameter(
        out, plan->text_stack_offset);
    fputs("\tpush hl\n", out);
    mir_emit_action_decode_parameter(
        out, plan->statement_stack_offset);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(
        out, plan->assignment_function);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_emit_action_decode_return(out);

    fprintf(out, "L%d:\n", final_return);
    mir_emit_action_decode_return(out);
}

static void mir_emit_buffered_declaration_address(
    FILE *out, const struct MirBufferedDeclarationSchedule *plan)
{
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            plan->cursor_offset, plan->cursor_offset + 1);
}

static void mir_emit_buffered_declaration_prefix_call(
    FILE *out, const struct MirBufferedDeclarationSchedule *plan,
    int string_id)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_emit_buffered_declaration_address(out, plan);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->prefix_function);
    fputs("\tpop de\n\tpop de\n", out);
}

static void mir_emit_buffered_declaration_call(
    FILE *out, const struct MirBufferedDeclarationSchedule *plan,
    int declaration_type)
{
    fprintf(out, "\tld hl,%u\n\tpush hl\n",
            (unsigned)declaration_type & 0xffffU);
    mir_emit_buffered_declaration_address(out, plan);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(
        out, plan->declaration_function);
    fputs("\tpop de\n\tpop de\n", out);
}

static void mir_emit_buffered_declaration_schedule(
    FILE *out, const struct MirBufferedDeclarationSchedule *plan)
{
    int done = new_label();
    int loop = new_label();
    int next = new_label();
    int second = new_label();
    int third = new_label();

    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    fprintf(out,
            "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n",
            plan->frame_bytes);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fputs("\tpush ix\n\tpop hl\n", out);
    mir_machine_emit_hl_offset(
        out, plan->buffer_offset, 0);
    fprintf(out,
            "\tld (ix%+d),l\n\tld (ix%+d),h\n",
            plan->cursor_offset, plan->cursor_offset + 1);
    fputs("\tld bc,0\n", out);
    fprintf(out, "L%d:\n", loop);
    mir_machine_emit_global_word(
        out, plan->count_root, plan->count_offset);
    fputs("\tex de,hl\n"
          "\tld h,b\n\tld l,c\n"
          "\tld a,h\n\txor 128\n\tld h,a\n"
          "\tld a,d\n\txor 128\n\tld d,a\n"
          "\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp nc,L%d\n\tpush bc\n", done);

    fputs("\tld h,b\n\tld l,c\n", out);
    mir_emit_mul_hl_const(
        out, (unsigned long)plan->record_stride);
    fputs("\tpush hl\n", out);
    mir_machine_emit_global_word(
        out, plan->statements_root,
        plan->statements_offset);
    fputs("\tpop de\n\tadd hl,de\n", out);
    mir_machine_emit_hl_offset(
        out, plan->text_offset, 0);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tpush de\n", out);
    mir_emit_buffered_declaration_address(out, plan);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->copy_function);
    fputs("\tpop de\n\tpop de\n", out);

    mir_emit_buffered_declaration_address(out, plan);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->trim_function);
    fputs("\tpop de\n", out);

    mir_emit_buffered_declaration_prefix_call(
        out, plan, plan->string_ids[0]);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", second);
    mir_emit_buffered_declaration_call(
        out, plan, plan->declaration_types[0]);
    fprintf(out, "\tjp L%d\nL%d:\n", next, second);

    mir_emit_buffered_declaration_prefix_call(
        out, plan, plan->string_ids[1]);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", third);
    mir_emit_buffered_declaration_call(
        out, plan, plan->declaration_types[1]);
    fprintf(out, "\tjp L%d\nL%d:\n", next, third);

    mir_emit_buffered_declaration_prefix_call(
        out, plan, plan->string_ids[2]);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", next);
    mir_emit_buffered_declaration_call(
        out, plan, plan->declaration_types[2]);

    fprintf(out,
            "L%d:\n\tpop bc\n\tinc bc\n\tjp L%d\n"
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            next, loop, done);
}

static void mir_emit_symbol_find_schedule(
    FILE *out, const struct MirSymbolFindSchedule *plan)
{
    int capacity_error = new_label();
    int capacity_ready = new_label();
    int cursor_ready = new_label();
    int found = new_label();
    int loop = new_label();
    int memory_error = new_label();
    int memory_ready = new_label();
    int next = new_label();
    int no_match = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tpush bc\n",
            plan->name_stack_offset);
    mir_machine_emit_global_word(
        out, plan->symbols_root, plan->symbols_offset);
    fputs("\tpush hl\n", out);
    mir_machine_emit_global_word(
        out, plan->count_root, plan->count_offset);
    fputs("\tex de,hl\n\tpop hl\n\tld bc,0\n", out);

    fprintf(out,
            "L%d:\n"
            "\tpush hl\n\tpush de\n"
            "\tld h,b\n\tld l,c\n"
            "\tld a,h\n\txor 128\n\tld h,a\n"
            "\tld a,d\n\txor 128\n\tld d,a\n"
            "\tor a\n\tsbc hl,de\n"
            "\tpop de\n\tpop hl\n"
            "\tjp nc,L%d\n"
            "\tpush bc\n\tpush hl\n\tpush de\n"
            "\tld hl,6\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tpush bc\n"
            "\tld hl,4\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tpush bc\n",
            loop, no_match);
    mir_machine_emit_symbol_call(out, plan->compare_function);
    fputs("\tld a,h\n\tor l\n"
          "\tinc sp\n\tinc sp\n\tinc sp\n\tinc sp\n"
          "\tpop de\n\tpop hl\n\tpop bc\n", out);
    fprintf(out,
            "\tjp nz,L%d\n"
            "L%d:\n"
            "\tld a,l\n\tadd a,%d\n\tld l,a\n"
            "\tjp nc,L%d\n\tinc h\n"
            "L%d:\n\tinc bc\n\tjp L%d\n"
            "L%d:\n",
            found, next, plan->record_stride,
            cursor_ready, cursor_ready, loop, no_match);

    mir_machine_emit_global_word(
        out, plan->count_root, plan->count_offset);
    fprintf(out,
            "\tbit 7,h\n\tjp nz,L%d\n"
            "\tld a,h\n\tor a\n\tjp nz,L%d\n"
            "\tld a,l\n\tcp %d\n\tjp c,L%d\n"
            "L%d:\n\tld hl,S%d\n\tpush hl\n",
            capacity_ready, capacity_error,
            plan->symbol_limit, capacity_ready,
            capacity_error, plan->symbol_error_string_id);
    mir_machine_emit_symbol_call(out, plan->error_function);
    fprintf(out, "\tpop bc\nL%d:\n", capacity_ready);

    mir_machine_emit_global_word(
        out, plan->symbols_root, plan->symbols_offset);
    fputs("\tpush hl\n", out);
    mir_machine_emit_global_word(
        out, plan->count_root, plan->count_offset);
    mir_emit_mul_hl_const(
        out, (unsigned long)plan->record_stride);
    fputs("\tpop de\n\tadd hl,de\n\tpush hl\n", out);
    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->name_field_size - 1);
    fputs("\tld hl,4\n\tadd hl,sp\n"
          "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
          "\tpush bc\n"
          "\tld hl,4\n\tadd hl,sp\n"
          "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
          "\tpush bc\n", out);
    mir_machine_emit_symbol_call(out, plan->copy_function);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n\tpop hl\n"
          "\tpush hl\n", out);
    mir_machine_emit_global_word(
        out, plan->memory_top_root, plan->memory_top_offset);
    fputs("\tld c,l\n\tld b,h\n\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->memory_top_root, plan->memory_top_offset);
    fputs("\tpop hl\n", out);
    mir_machine_emit_hl_offset(
        out, plan->scalar_field_offset, 1);
    fputs("\tld (hl),c\n\tinc hl\n\tld (hl),b\n"
          "\tinc hl\n\tld (hl),255\n"
          "\tinc hl\n\tld (hl),255\n"
          "\tinc hl\n\txor a\n\tld (hl),a\n"
          "\tinc hl\n\tld (hl),a\n", out);

    mir_machine_emit_global_word(
        out, plan->memory_top_root, plan->memory_top_offset);
    fprintf(out,
            "\tbit 7,h\n\tjp nz,L%d\n"
            "\tld de,%d\n\tor a\n\tsbc hl,de\n"
            "\tjp nc,L%d\n\tjp L%d\n"
            "L%d:\n\tld hl,S%d\n\tpush hl\n",
            memory_ready, plan->memory_limit,
            memory_error, memory_ready,
            memory_error, plan->memory_error_string_id);
    mir_machine_emit_symbol_call(out, plan->error_function);
    fprintf(out, "\tpop bc\nL%d:\n", memory_ready);

    mir_machine_emit_global_word(
        out, plan->count_root, plan->count_offset);
    fputs("\tpush hl\n\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->count_root, plan->count_offset);
    fputs("\tpop hl\n\tpop bc\n\tret\n", out);

    fprintf(out,
            "L%d:\n\tld h,b\n\tld l,c\n\tpop de\n\tret\n",
            found);
}

int mir_try_emit_scanner_kernels(FILE *out, int late)
{
    if (!late) {
        struct MirBoundedStringMatchSchedule
            bounded_string_match_schedule;
        struct MirCommentScanSchedule comment_scan_schedule;
        struct MirWhitespaceScanSchedule whitespace_scan_schedule;
        struct MirActionDecodeSchedule action_decode_schedule;
        struct MirBufferedDeclarationSchedule
            buffered_declaration_schedule;

        if (mir_match_bounded_string_match_schedule(
                &bounded_string_match_schedule)) {
            mir_emit_bounded_string_match_schedule(
                out, &bounded_string_match_schedule);
            return 1;
        }
        {
            struct MirStarCommentScanSchedule
                star_comment_scan_schedule;

            if (mir_match_star_comment_scan_schedule(
                    &star_comment_scan_schedule)) {
                mir_emit_star_comment_scan_schedule(
                    out, &star_comment_scan_schedule);
                return 1;
            }
        }
        if (mir_match_comment_scan_schedule(
                &comment_scan_schedule)) {
            mir_emit_comment_scan_schedule(
                out, &comment_scan_schedule);
            return 1;
        }
        if (mir_match_whitespace_scan_schedule(
                &whitespace_scan_schedule)) {
            mir_emit_whitespace_scan_schedule(
                out, &whitespace_scan_schedule);
            return 1;
        }
        if (mir_match_action_decode_schedule(
                &action_decode_schedule)) {
            mir_emit_action_decode_schedule(
                out, &action_decode_schedule);
            return 1;
        }
        if (mir_match_buffered_declaration_schedule(
                &buffered_declaration_schedule)) {
            mir_emit_buffered_declaration_schedule(
                out, &buffered_declaration_schedule);
            return 1;
        }
        return -1;
    }
    {
        struct MirSymbolFindSchedule symbol_find_schedule;

        if (mir_match_symbol_find_schedule(
                &symbol_find_schedule)) {
            mir_emit_symbol_find_schedule(
                out, &symbol_find_schedule);
            return 1;
        }
    }
    return -1;
}
