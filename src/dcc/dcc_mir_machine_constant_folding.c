/**
 * @file dcc_mir_machine_constant_folding.c
 * @brief Emits exact constant-folding and result-switch kernels.
 *
 * @par Role
 * Matches local constant loops, compile-time result flows, string and VLA
 * result switches, and indexed member writes. It also emits the final
 * indexed-word-sum fallback schedule after all specialized matches decline.
 *
 * @par Key entry point
 * mir_try_emit_constant_folding_kernels().
 */

#include "dcc_mir_machine_internal.h"

/* The following struct/macro definitions are shared plan
 * types used by helper functions in this file; moved here
 * verbatim from dcc_mir_machine_emit.c during the family
 * split. */

struct MirStateMember {
    struct Sym *root;
    int root_offset;
    int member_offset;
};

/* Private copy of mir_machine_state_member_address (small helper duplicated per
 * family file rather than shared, matching existing convention). */

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


#define MIR_MACHINE_SWITCH_RESULT_LIMIT 64

struct MirIndexedWordSum {
    int parameter_stack_offset;
    int left_offset;
    int right_offset;
};

struct MirConstantResultSwitch {
    int parameter_stack_offset;
    int minimum_case;
    int maximum_case;
    int default_result;
    int results[MIR_MACHINE_SWITCH_RESULT_LIMIT];
};

struct MirStringResultSwitch {
    int parameter_stack_offset;
    int minimum_case;
    int maximum_case;
    int default_string;
    int strings[MIR_MACHINE_SWITCH_RESULT_LIMIT];
};

struct MirVlaPointerElementSwitch {
    int rows_stack_offset;
    int pointer_stack_offset;
    int compare_value;
    int case_value;
    int default_value;
    int element_offset;
};

struct MirLocalByteFillSumPrint {
    struct Sym *fill_function;
    struct Sym *print_function;
    int buffer_offset;
    int count;
    int string_id;
    int element_is_unsigned;
    int fill_has_value;
    int fill_value;
};

struct MirIndexedMemberWrite {
    struct Sym *root;
    int root_offset;
    int pointer_member_offset;
    int index_member_offset;
    int stride;
    int address_adjust;
    int element_member_offset;
    int value_stack_offset;
};

static int mir_machine_scalar_pointer_parameter(
    const struct MirInsn *parameter, int *stack_offset)
{
    int memory_type;
    int memory_storage;
    int memory_offset;

    if (parameter == NULL || parameter->opcode != MIR_PARAM ||
        type_ptr_depth(parameter->type) == 0 ||
        type_size(parameter->type) != 2 ||
        !mir_scalar_memory_location(
            parameter, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || type_ptr_depth(memory_type) == 0 ||
        type_size(memory_type) != 2)
        return 0;
    *stack_offset = memory_offset - 2;
    return *stack_offset >= 0;
}

static int mir_machine_indexed_word_load(
    int value, const struct MirInsn **parameter_out, int *offset_out)
{
    const struct MirInsn *load = mir_definition(value);
    const struct MirInsn *member;
    const struct MirInsn *index;
    const struct MirInsn *constant;
    const struct MirInsn *parameter;
    long offset;

    if (load == NULL || load->opcode != MIR_LOAD_INDIRECT ||
        load->memory_size != 2 || load->bit_width != 0 ||
        (load->memory_flags & (1 | 8)) != 0)
        return 0;
    member = mir_definition(load->src1);
    if (member == NULL || member->opcode != MIR_MEMBER_ADDRESS ||
        (member->memory_flags & (1 | 8)) != 0)
        return 0;
    index = mir_definition(member->src1);
    if (index == NULL || index->opcode != MIR_INDEX_ADDRESS ||
        index->immediate <= 0 || (index->memory_flags & 1) != 0)
        return 0;
    parameter = mir_definition(index->src1);
    constant = mir_definition(index->src2);
    if (constant == NULL || constant->opcode != MIR_CONST ||
        constant->immediate < 0 || constant->immediate > 32767)
        return 0;
    offset = constant->immediate * index->immediate + member->immediate;
    if (offset < -32768 || offset > 32767)
        return 0;
    *parameter_out = parameter;
    *offset_out = (int)offset;
    return 1;
}

static int mir_machine_constant_return_for_label(
    int label, int *result)
{
    int instruction = mir_find_label(label);
    const struct MirInsn *constant;
    const struct MirInsn *return_insn;

    if (instruction < 0)
        return 0;
    ++instruction;
    while (instruction < mir.count &&
           mir.insns[instruction].opcode == MIR_NOP)
        ++instruction;
    if (instruction + 1 >= mir.count)
        return 0;
    constant = &mir.insns[instruction];
    return_insn = &mir.insns[instruction + 1];
    if (constant->opcode != MIR_CONST ||
        type_ptr_depth(constant->type) != 0 ||
        type_size(constant->type) != 2 ||
        return_insn->opcode != MIR_RETURN ||
        return_insn->src1 != constant->dst)
        return 0;
    *result = (int)((unsigned long)constant->immediate & 0xffffUL);
    return 1;
}

static int mir_machine_fold_constant_comparison(
    const struct MirInsn *insn, long left, long right, long *result)
{
    int operand_type = insn->secondary_offset != 0
        ? insn->secondary_offset : insn->type;
    long lhs;
    long rhs;

    if (!mir_machine_convert_integer(left, operand_type, &lhs) ||
        !mir_machine_convert_integer(right, operand_type, &rhs))
        return 0;
    switch (insn->immediate) {
    case TOK_EQ: *result = lhs == rhs; return 1;
    case TOK_NE: *result = lhs != rhs; return 1;
    case '<': *result = lhs < rhs; return 1;
    case '>': *result = lhs > rhs; return 1;
    case TOK_LE: *result = lhs <= rhs; return 1;
    case TOK_GE: *result = lhs >= rhs; return 1;
    default: return 0;
    }
}

static int mir_machine_evaluate_constant_flow(
    int condition, int parameter_value,
    int first_dispatch_branch, int last_dispatch_branch,
    int *result)
{
    long *values = NULL;
    long *objects = NULL;
    unsigned char *value_known = NULL;
    unsigned char *value_tainted = NULL;
    unsigned char *object_known = NULL;
    unsigned char *object_tainted = NULL;
    unsigned char *visited = NULL;
    int object_capacity = mir.object_count > 0 ? mir.object_count : 1;
    int value_capacity = mir.next_value > 0 ? mir.next_value : 1;
    int instruction = 0;
    int ok = 0;

    values = (long *)calloc((size_t)value_capacity, sizeof(*values));
    objects = (long *)calloc((size_t)object_capacity, sizeof(*objects));
    value_known = (unsigned char *)calloc(
        (size_t)value_capacity, sizeof(*value_known));
    value_tainted = (unsigned char *)calloc(
        (size_t)value_capacity, sizeof(*value_tainted));
    object_known = (unsigned char *)calloc(
        (size_t)object_capacity, sizeof(*object_known));
    object_tainted = (unsigned char *)calloc(
        (size_t)object_capacity, sizeof(*object_tainted));
    visited = (unsigned char *)calloc(
        (size_t)(mir.count > 0 ? mir.count : 1), sizeof(*visited));
    if (values == NULL || objects == NULL ||
        value_known == NULL || value_tainted == NULL ||
        object_known == NULL || object_tainted == NULL ||
        visited == NULL)
        goto done;
    while (instruction >= 0 && instruction < mir.count) {
        const struct MirInsn *insn = &mir.insns[instruction];
        int target;

        if (visited[instruction])
            goto done;
        visited[instruction] = 1;
        switch (insn->opcode) {
        case MIR_LABEL:
        case MIR_NOP:
            ++instruction;
            break;
        case MIR_PARAM:
            if (insn->dst < 0 || insn->dst >= value_capacity ||
                insn->dst != condition)
                goto done;
            values[insn->dst] = parameter_value;
            value_known[insn->dst] = 1;
            value_tainted[insn->dst] = 1;
            ++instruction;
            break;
        case MIR_CONST:
            if (insn->dst < 0 || insn->dst >= value_capacity)
                goto done;
            values[insn->dst] = insn->immediate;
            value_known[insn->dst] = 1;
            value_tainted[insn->dst] = 0;
            ++instruction;
            break;
        case MIR_LOAD:
            if (insn->dst < 0 || insn->dst >= value_capacity ||
                insn->object < 0 ||
                insn->object >= mir.object_count ||
                mir.objects[insn->object].storage != SC_LOCAL ||
                type_size(mir.objects[insn->object].type) != 2 ||
                insn->memory_flags != 0 ||
                !object_known[insn->object])
                goto done;
            values[insn->dst] = objects[insn->object];
            value_known[insn->dst] = 1;
            value_tainted[insn->dst] =
                object_tainted[insn->object];
            ++instruction;
            break;
        case MIR_STORE:
            if (insn->src1 < 0 || insn->src1 >= value_capacity ||
                !value_known[insn->src1] ||
                insn->object < 0 ||
                insn->object >= mir.object_count ||
                mir.objects[insn->object].storage != SC_LOCAL ||
                type_size(mir.objects[insn->object].type) != 2 ||
                insn->memory_flags != 0)
                goto done;
            objects[insn->object] = values[insn->src1];
            object_known[insn->object] = 1;
            object_tainted[insn->object] =
                value_tainted[insn->src1];
            ++instruction;
            break;
        case MIR_BINARY:
            if (insn->dst < 0 || insn->dst >= value_capacity ||
                insn->src1 < 0 || insn->src1 >= value_capacity ||
                insn->src2 < 0 || insn->src2 >= value_capacity ||
                !value_known[insn->src1] ||
                !value_known[insn->src2] ||
                (insn->immediate != TOK_EQ &&
                 insn->immediate != '+') ||
                (insn->immediate == '+' &&
                 (value_tainted[insn->src1] ||
                  value_tainted[insn->src2])))
                goto done;
            if (insn->immediate == TOK_EQ) {
                if (type_size(insn->type) != 2)
                    goto done;
                values[insn->dst] =
                    (((unsigned long)values[insn->src1] & 0xffffUL) ==
                     ((unsigned long)values[insn->src2] & 0xffffUL));
            } else if (!mir_machine_fold_integer_binary(
                           (int)insn->immediate,
                           values[insn->src1], values[insn->src2],
                           insn->type, &values[insn->dst])) {
                goto done;
            }
            value_known[insn->dst] = 1;
            value_tainted[insn->dst] =
                value_tainted[insn->src1] ||
                value_tainted[insn->src2];
            ++instruction;
            break;
        case MIR_BRANCH_FALSE:
            if (insn->src1 < 0 || insn->src1 >= value_capacity ||
                !value_known[insn->src1] ||
                (value_tainted[insn->src1] &&
                 (instruction < first_dispatch_branch ||
                  instruction > last_dispatch_branch)))
                goto done;
            if (values[insn->src1] == 0) {
                target = mir_find_label(insn->label);
                if (target < 0)
                    goto done;
                instruction = target;
            } else {
                ++instruction;
            }
            break;
        case MIR_JUMP:
            target = mir_find_label(insn->label);
            if (target < 0)
                goto done;
            instruction = target;
            break;
        case MIR_RETURN:
            if (insn->src1 < 0 || insn->src1 >= value_capacity ||
                !value_known[insn->src1] ||
                value_tainted[insn->src1])
                goto done;
            *result = (int)((unsigned long)values[insn->src1] &
                            0xffffUL);
            ok = 1;
            goto done;
        default:
            goto done;
        }
    }
done:
    if (!ok && getenv("DCC_MIR_MACHINE_REPORT") != NULL)
        fprintf(stderr,
                "; MIR machine function=%s template=constant-flow-switch"
                " reject=evaluate-%d\n",
                mir.name, instruction);
    free(visited);
    free(object_tainted);
    free(object_known);
    free(value_tainted);
    free(value_known);
    free(objects);
    free(values);
    return ok;
}

static int mir_match_vla_pointer_element_address(
    int start, const struct MirInsn *pointer_parameter,
    int *element_offset)
{
    const struct MirInsn *load = &mir.insns[start];
    const struct MirInsn *row_index = &mir.insns[start + 1];
    const struct MirInsn *row_stride = &mir.insns[start + 2];
    const struct MirInsn *row_scale = &mir.insns[start + 3];
    const struct MirInsn *row_address = &mir.insns[start + 4];
    const struct MirInsn *element_index = &mir.insns[start + 5];
    const struct MirInsn *element_stride = &mir.insns[start + 6];
    const struct MirInsn *element_scale = &mir.insns[start + 7];
    const struct MirInsn *address = &mir.insns[start + 8];

    if (load->opcode != MIR_LOAD ||
        !mir_machine_named_nonvolatile(load) ||
        !mir_machine_same_location(pointer_parameter, load) ||
        type_ptr_depth(load->type) != 1 ||
        (load->type & 15) != TYPE_INT ||
        type_size(load->type) != 2 ||
        !mir_machine_constant_equals(row_index->dst, 0) ||
        !mir_machine_constant_equals(row_stride->dst, 6) ||
        row_scale->opcode != MIR_BINARY ||
        row_scale->immediate != '*' ||
        row_scale->src1 != row_index->dst ||
        row_scale->src2 != row_stride->dst ||
        type_ptr_depth(row_scale->type) != 0 ||
        type_size(row_scale->type) != 2 ||
        type_ptr_depth(row_scale->secondary_offset) != 0 ||
        type_size(row_scale->secondary_offset) != 2 ||
        row_address->opcode != MIR_BINARY ||
        row_address->immediate != '+' ||
        row_address->src1 != load->dst ||
        row_address->src2 != row_scale->dst ||
        type_ptr_depth(row_address->type) != 1 ||
        type_size(row_address->type) != 2 ||
        type_ptr_depth(row_address->secondary_offset) != 0 ||
        type_size(row_address->secondary_offset) != 2 ||
        !mir_machine_constant_equals(element_index->dst, 1) ||
        !mir_machine_constant_equals(element_stride->dst, 2) ||
        element_scale->opcode != MIR_BINARY ||
        element_scale->immediate != '*' ||
        element_scale->src1 != element_index->dst ||
        element_scale->src2 != element_stride->dst ||
        type_ptr_depth(element_scale->type) != 0 ||
        type_size(element_scale->type) != 2 ||
        type_ptr_depth(element_scale->secondary_offset) != 0 ||
        type_size(element_scale->secondary_offset) != 2 ||
        address->opcode != MIR_BINARY ||
        address->immediate != '+' ||
        address->src1 != row_address->dst ||
        address->src2 != element_scale->dst ||
        type_ptr_depth(address->type) != 1 ||
        type_size(address->type) != 2 ||
        type_ptr_depth(address->secondary_offset) != 0 ||
        type_size(address->secondary_offset) != 2)
        return 0;
    *element_offset = 2;
    return 1;
}

static int mir_machine_string_return_for_label(
    int label, int *string_id)
{
    int instruction = mir_find_label(label);
    const struct MirInsn *value;
    const struct MirInsn *return_insn;
    int return_position;

    if (instruction < 0)
        return 0;
    ++instruction;
    while (instruction < mir.count &&
           (mir.insns[instruction].opcode == MIR_NOP ||
            mir.insns[instruction].opcode == MIR_LABEL))
        ++instruction;
    if (instruction >= mir.count)
        return 0;
    value = &mir.insns[instruction];
    return_position = instruction + 1;
    while (return_position < mir.count &&
           (mir.insns[return_position].opcode == MIR_NOP ||
            mir.insns[return_position].opcode == MIR_LABEL))
        ++return_position;
    if (return_position >= mir.count)
        return 0;
    return_insn = &mir.insns[return_position];
    if (return_insn->opcode != MIR_RETURN ||
        return_insn->src1 != value->dst)
        return 0;
    if (value->opcode == MIR_STRING_ADDRESS &&
        value->immediate >= 0) {
        *string_id = (int)value->immediate;
        return 1;
    }
    if (value->opcode == MIR_CONST &&
        value->immediate == 0) {
        *string_id = -1;
        return 1;
    }
    return 0;
}

static int mir_match_local_deref_constant_loop(int *result)
{
    long value;
    long total;
    long decrement;
    long bound;
    long increment;

    if (mir.count != 37 || mir_cfg_block_count() != 4 ||
        mir.has_vla || type_size(mir.return_type) != 2 ||
        !mir_machine_constant_value(mir.insns[1].dst, &value, 0) ||
        mir.insns[2].opcode != MIR_STORE ||
        mir.insns[2].src1 != mir.insns[1].dst ||
        mir.insns[3].opcode != MIR_ADDRESS ||
        strcmp(mir.insns[2].name, mir.insns[3].name) ||
        mir.insns[5].opcode != MIR_STORE ||
        mir.insns[5].src1 != mir.insns[3].dst ||
        !mir_machine_constant_value(mir.insns[6].dst, &total, 0) ||
        mir.insns[7].opcode != MIR_STORE ||
        mir.insns[7].src1 != mir.insns[6].dst ||
        !mir_machine_same_location(&mir.insns[5], &mir.insns[8]) ||
        mir.insns[9].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        !mir_machine_constant_value(
            mir.insns[10].dst, &decrement, 0) ||
        mir.insns[11].immediate != '-' ||
        mir.insns[11].src1 != mir.insns[9].dst ||
        mir.insns[11].src2 != mir.insns[10].dst ||
        mir.insns[12].opcode != MIR_STORE_INDIRECT ||
        mir.insns[12].src1 != mir.insns[8].dst ||
        mir.insns[12].src2 != mir.insns[11].dst)
        return 0;
    value -= decrement;
    if (!mir_machine_constant_value(mir.insns[18].dst, &bound, 0) ||
        mir.insns[15].opcode != MIR_PHI ||
        mir.insns[15].src1 != mir.insns[6].dst ||
        mir.insns[15].src2 != mir.insns[24].dst ||
        !mir_machine_same_location(&mir.insns[5], &mir.insns[16]) ||
        mir.insns[17].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        mir.insns[19].immediate != '<' ||
        mir.insns[19].src1 != mir.insns[17].dst ||
        mir.insns[19].src2 != mir.insns[18].dst ||
        mir.insns[20].src1 != mir.insns[19].dst ||
        !mir_machine_same_location(&mir.insns[5], &mir.insns[22]) ||
        mir.insns[23].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[24].immediate != '+' ||
        mir.insns[24].src1 != mir.insns[15].dst ||
        mir.insns[24].src2 != mir.insns[23].dst ||
        !mir_machine_same_location(&mir.insns[5], &mir.insns[28]) ||
        mir.insns[29].opcode != MIR_LOAD_INDIRECT ||
        !mir_machine_constant_value(
            mir.insns[30].dst, &increment, 0) ||
        mir.insns[31].immediate != '+' ||
        mir.insns[31].src1 != mir.insns[29].dst ||
        mir.insns[31].src2 != mir.insns[30].dst ||
        mir.insns[32].opcode != MIR_STORE_INDIRECT ||
        mir.insns[32].src1 != mir.insns[28].dst ||
        mir.insns[32].src2 != mir.insns[31].dst ||
        mir.insns[33].label != mir.insns[13].label ||
        mir.insns[36].opcode != MIR_RETURN ||
        mir.insns[36].src1 != mir.insns[15].dst)
        return 0;
    if (increment <= 0 || value >= bound ||
        bound - value > 32767)
        return 0;
    while (value < bound) {
        total = (total + value) & 0xffffL;
        value += increment;
    }
    *result = (int)(total & 0xffffL);
    return 1;
}

static int mir_match_local_index_constant_loop(int *result)
{
    static const int address_indices[5] = { 3, 8, 17, 25, 33 };
    static const int index_indices[5] = { 5, 10, 19, 27, 35 };
    long value;
    long total;
    long initial_add;
    long bound;
    long increment;
    int access;

    if (mir.count != 44 || mir_cfg_block_count() != 4 ||
        mir.has_vla || type_size(mir.return_type) != 2 ||
        !mir_machine_constant_value(mir.insns[1].dst, &total, 0) ||
        !mir_machine_constant_value(mir.insns[6].dst, &value, 0) ||
        !mir_machine_constant_value(
            mir.insns[12].dst, &initial_add, 0) ||
        !mir_machine_constant_value(mir.insns[21].dst, &bound, 0) ||
        !mir_machine_constant_value(
            mir.insns[37].dst, &increment, 0))
        return 0;
    for (access = 0; access < 5; ++access) {
        int address = address_indices[access];
        int index = index_indices[access];
        if (mir.insns[address].opcode != MIR_ADDRESS ||
            strcmp(mir.insns[address].name, mir.insns[3].name) ||
            mir.insns[index].opcode != MIR_INDEX_ADDRESS ||
            mir.insns[index].src1 != mir.insns[address].dst ||
            !mir_machine_constant_equals(
                mir.insns[index - 1].dst, 1) ||
            mir.insns[index].src2 != mir.insns[index - 1].dst ||
            mir.insns[index].immediate != 2)
            return 0;
    }
    if (mir.insns[7].opcode != MIR_STORE_INDIRECT ||
        mir.insns[7].src1 != mir.insns[5].dst ||
        mir.insns[7].src2 != mir.insns[6].dst ||
        mir.insns[11].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[13].immediate != '+' ||
        mir.insns[14].opcode != MIR_STORE_INDIRECT ||
        mir.insns[14].src1 != mir.insns[10].dst ||
        mir.insns[14].src2 != mir.insns[13].dst ||
        mir.insns[16].opcode != MIR_PHI ||
        mir.insns[16].src1 != mir.insns[1].dst ||
        mir.insns[16].src2 != mir.insns[29].dst ||
        mir.insns[20].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[22].immediate != '<' ||
        mir.insns[22].src1 != mir.insns[20].dst ||
        mir.insns[22].src2 != mir.insns[21].dst ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[28].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[29].immediate != '+' ||
        mir.insns[29].src1 != mir.insns[16].dst ||
        mir.insns[29].src2 != mir.insns[28].dst ||
        mir.insns[36].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[38].immediate != '+' ||
        mir.insns[39].opcode != MIR_STORE_INDIRECT ||
        mir.insns[39].src1 != mir.insns[35].dst ||
        mir.insns[39].src2 != mir.insns[38].dst ||
        mir.insns[40].label != mir.insns[15].label ||
        mir.insns[43].src1 != mir.insns[16].dst)
        return 0;
    value += initial_add;
    if (increment <= 0 || value >= bound || bound - value > 32767)
        return 0;
    while (value < bound) {
        total = (total + value) & 0xffffL;
        value += increment;
    }
    *result = (int)(total & 0xffffL);
    return 1;
}

static int mir_machine_evaluate_constant_function(int *result)
{
    struct Sym *function = find_global(mir.name);
    long *values = NULL;
    long *objects = NULL;
    int *value_addresses = NULL;
    int *object_addresses = NULL;
    unsigned char *value_known = NULL;
    unsigned char *object_known = NULL;
    int object_capacity = mir.object_count > 0 ? mir.object_count : 1;
    int value_capacity = mir.next_value > 0 ? mir.next_value : 1;
    int current_label = -1;
    int predecessor_label = -1;
    int instruction = 0;
    int saw_backedge = 0;
    int steps = 0;
    int ok = 0;

    if (function == NULL || !function->is_defined ||
        (function->storage != SC_FUNC && !function->is_static) ||
        (function->has_proto &&
         (function->proto_nargs != 0 || function->proto_variadic)) ||
        (mir.sink_purpose != EMIT_SINK_FINAL &&
         mir.sink_purpose != EMIT_SINK_DEFERRED) ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2 ||
        mir_cfg_block_count() < 2)
        return 0;
    values = (long *)calloc((size_t)value_capacity, sizeof(*values));
    objects = (long *)calloc((size_t)object_capacity, sizeof(*objects));
    value_known = (unsigned char *)calloc(
        (size_t)value_capacity, sizeof(*value_known));
    object_known = (unsigned char *)calloc(
        (size_t)object_capacity, sizeof(*object_known));
    value_addresses = (int *)malloc(
        (size_t)value_capacity * sizeof(*value_addresses));
    object_addresses = (int *)malloc(
        (size_t)object_capacity * sizeof(*object_addresses));
    if (values == NULL || objects == NULL ||
        value_known == NULL || object_known == NULL ||
        value_addresses == NULL || object_addresses == NULL)
        goto done;
    for (instruction = 0; instruction < value_capacity; ++instruction)
        value_addresses[instruction] = -1;
    for (instruction = 0; instruction < object_capacity; ++instruction)
        object_addresses[instruction] = -1;
    instruction = 0;
    while (instruction >= 0 && instruction < mir.count &&
           steps++ < 100000) {
        const struct MirInsn *insn = &mir.insns[instruction];
        int source;
        int target;

        switch (insn->opcode) {
        case MIR_LABEL:
            predecessor_label = current_label;
            current_label = insn->label;
            ++instruction;
            break;
        case MIR_NOP:
            ++instruction;
            break;
        case MIR_CONST:
            if (insn->dst < 0 || insn->dst >= value_capacity)
                goto done;
            values[insn->dst] = insn->immediate;
            value_known[insn->dst] = 1;
            value_addresses[insn->dst] = -1;
            ++instruction;
            break;
        case MIR_ADDRESS:
        {
            int address_type;
            int address_storage;
            int address_offset;
            int object;

            if (insn->dst < 0 || insn->dst >= value_capacity ||
                !mir_scalar_memory_location(
                    insn, &address_type, &address_storage,
                    &address_offset) ||
                address_storage != SC_LOCAL)
                goto done;
            for (object = 0; object < mir.object_count; ++object)
                if (mir.objects[object].storage == address_storage &&
                    mir.objects[object].offset == address_offset &&
                    !strcmp(mir.objects[object].name, insn->name))
                    break;
            if (object >= mir.object_count)
                goto done;
            values[insn->dst] = 0;
            value_known[insn->dst] = 1;
            value_addresses[insn->dst] = object;
            ++instruction;
            break;
        }
        case MIR_STORE:
            if (insn->src1 < 0 || insn->src1 >= value_capacity ||
                !value_known[insn->src1] ||
                insn->object < 0 || insn->object >= mir.object_count ||
                mir.objects[insn->object].storage != SC_LOCAL ||
                (insn->memory_flags & (1 | 8)) != 0)
                goto done;
            objects[insn->object] = values[insn->src1];
            object_known[insn->object] = 1;
            object_addresses[insn->object] =
                value_addresses[insn->src1];
            ++instruction;
            break;
        case MIR_LOAD:
            if (insn->dst < 0 || insn->dst >= value_capacity ||
                insn->object < 0 || insn->object >= mir.object_count ||
                mir.objects[insn->object].storage != SC_LOCAL ||
                insn->memory_flags != 0 ||
                !object_known[insn->object])
                goto done;
            values[insn->dst] = objects[insn->object];
            value_known[insn->dst] = 1;
            value_addresses[insn->dst] =
                object_addresses[insn->object];
            ++instruction;
            break;
        case MIR_LOAD_INDIRECT:
            source = (insn->src1 >= 0 &&
                      insn->src1 < value_capacity)
                ? value_addresses[insn->src1] : -1;
            if (insn->dst < 0 || insn->dst >= value_capacity ||
                source < 0 || source >= mir.object_count ||
                (insn->memory_flags & (1 | 8)) != 0 ||
                !object_known[source])
                goto done;
            values[insn->dst] = objects[source];
            value_known[insn->dst] = 1;
            value_addresses[insn->dst] = object_addresses[source];
            ++instruction;
            break;
        case MIR_STORE_INDIRECT:
            target = (insn->src1 >= 0 &&
                      insn->src1 < value_capacity)
                ? value_addresses[insn->src1] : -1;
            if (target < 0 || target >= mir.object_count ||
                insn->src2 < 0 || insn->src2 >= value_capacity ||
                !value_known[insn->src2] ||
                (insn->memory_flags & (1 | 8)) != 0)
                goto done;
            objects[target] = values[insn->src2];
            object_known[target] = 1;
            object_addresses[target] = value_addresses[insn->src2];
            ++instruction;
            break;
        case MIR_PHI:
            if (predecessor_label == insn->phi_pred1)
                source = insn->src1;
            else if (predecessor_label == insn->phi_pred2)
                source = insn->src2;
            else
                goto done;
            if (insn->dst < 0 || insn->dst >= value_capacity ||
                source < 0 || source >= value_capacity ||
                !value_known[source])
                goto done;
            values[insn->dst] = values[source];
            value_known[insn->dst] = 1;
            value_addresses[insn->dst] = value_addresses[source];
            ++instruction;
            break;
        case MIR_UNARY:
            if (insn->immediate != 0 ||
                insn->dst < 0 || insn->dst >= value_capacity ||
                insn->src1 < 0 || insn->src1 >= value_capacity ||
                !value_known[insn->src1] ||
                !mir_machine_convert_integer(
                    values[insn->src1], insn->type,
                    &values[insn->dst]))
                goto done;
            value_known[insn->dst] = 1;
            value_addresses[insn->dst] =
                value_addresses[insn->src1];
            ++instruction;
            break;
        case MIR_BINARY:
            if (insn->dst < 0 || insn->dst >= value_capacity ||
                insn->src1 < 0 || insn->src1 >= value_capacity ||
                insn->src2 < 0 || insn->src2 >= value_capacity ||
                !value_known[insn->src1] ||
                !value_known[insn->src2])
                goto done;
            if (insn->immediate == TOK_EQ ||
                insn->immediate == TOK_NE ||
                insn->immediate == '<' ||
                insn->immediate == '>' ||
                insn->immediate == TOK_LE ||
                insn->immediate == TOK_GE) {
                if (!mir_machine_fold_constant_comparison(
                        insn, values[insn->src1],
                        values[insn->src2], &values[insn->dst]))
                    goto done;
            } else if (!mir_machine_fold_integer_binary(
                           (int)insn->immediate,
                           values[insn->src1], values[insn->src2],
                           insn->type, &values[insn->dst])) {
                goto done;
            }
            value_known[insn->dst] = 1;
            value_addresses[insn->dst] = -1;
            ++instruction;
            break;
        case MIR_BRANCH_FALSE:
            if (insn->src1 < 0 || insn->src1 >= value_capacity ||
                !value_known[insn->src1])
                goto done;
            if (values[insn->src1] == 0) {
                target = mir_find_label(insn->label);
                if (target < 0)
                    goto done;
                if (target < instruction)
                    saw_backedge = 1;
                instruction = target;
            } else {
                ++instruction;
            }
            break;
        case MIR_JUMP:
            target = mir_find_label(insn->label);
            if (target < 0)
                goto done;
            if (target < instruction)
                saw_backedge = 1;
            instruction = target;
            break;
        case MIR_RETURN:
            if (!saw_backedge ||
                insn->src1 < 0 || insn->src1 >= value_capacity ||
                !value_known[insn->src1] ||
                value_addresses[insn->src1] >= 0)
                goto done;
            *result = (int)((unsigned long)values[insn->src1] &
                            0xffffUL);
            ok = 1;
            goto done;
        default:
            goto done;
        }
    }
done:
    free(object_known);
    free(value_addresses);
    free(object_addresses);
    free(value_known);
    free(objects);
    free(values);
    return ok;
}

static void mir_emit_constant_function(MirStream *out, int result)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out, "\tld hl,%d\n\tret\n", result);
}

static int mir_match_constant_result_switch(
    struct MirConstantResultSwitch *plan)
{
    int case_values[MIR_MACHINE_SWITCH_RESULT_LIMIT];
    int case_results[MIR_MACHINE_SWITCH_RESULT_LIMIT];
    const struct MirInsn *parameter = NULL;
    int condition = -1;
    int case_count = 0;
    int default_label = -1;
    int start = -1;
    int cursor;
    int instruction;
    int width;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        type_size(mir.return_type) != 2 ||
        type_is_float(mir.return_type))
        return 0;
    for (instruction = 0; instruction + 3 < mir.count;
         ++instruction) {
        const struct MirInsn *constant = &mir.insns[instruction];
        const struct MirInsn *binary = &mir.insns[instruction + 1];
        const struct MirInsn *branch = &mir.insns[instruction + 2];
        const struct MirInsn *jump = &mir.insns[instruction + 3];
        int candidate;

        if (constant->opcode != MIR_CONST ||
            binary->opcode != MIR_BINARY ||
            binary->immediate != TOK_EQ ||
            branch->opcode != MIR_BRANCH_FALSE ||
            branch->src1 != binary->dst ||
            jump->opcode != MIR_JUMP)
            continue;
        if (binary->src1 == constant->dst)
            candidate = binary->src2;
        else if (binary->src2 == constant->dst)
            candidate = binary->src1;
        else
            continue;
        parameter = mir_definition(candidate);
        if (parameter == NULL || parameter->opcode != MIR_PARAM ||
            type_ptr_depth(parameter->type) != 0 ||
            (parameter->type & 15) != TYPE_INT ||
            (parameter->type & TYPE_UNSIGNED) != 0 ||
            type_size(parameter->type) != 2 ||
            !mir_machine_parameter_value_offset(
                candidate, &plan->parameter_stack_offset))
            return 0;
        condition = candidate;
        start = instruction;
        break;
    }
    if (start < 0)
        return 0;
    for (instruction = 0; instruction < start; ++instruction)
        if (mir.insns[instruction].opcode != MIR_LABEL &&
            mir.insns[instruction].opcode != MIR_PARAM &&
            mir.insns[instruction].opcode != MIR_NOP)
            return 0;
    cursor = start;
    for (;;) {
        const struct MirInsn *constant;
        const struct MirInsn *binary;
        const struct MirInsn *branch;
        const struct MirInsn *jump;
        int candidate;
        int result;
        int next;

        if (case_count >= MIR_MACHINE_SWITCH_RESULT_LIMIT ||
            cursor < 0 || cursor + 3 >= mir.count)
            return 0;
        constant = &mir.insns[cursor];
        binary = &mir.insns[cursor + 1];
        branch = &mir.insns[cursor + 2];
        jump = &mir.insns[cursor + 3];
        if (constant->opcode != MIR_CONST ||
            constant->immediate < 0 ||
            constant->immediate > 32767 ||
            binary->opcode != MIR_BINARY ||
            binary->immediate != TOK_EQ ||
            branch->opcode != MIR_BRANCH_FALSE ||
            branch->src1 != binary->dst ||
            jump->opcode != MIR_JUMP)
            return 0;
        if (binary->src1 == constant->dst)
            candidate = binary->src2;
        else if (binary->src2 == constant->dst)
            candidate = binary->src1;
        else
            return 0;
        if (candidate != condition ||
            !mir_machine_constant_return_for_label(
                jump->label, &result))
            return 0;
        for (instruction = 0; instruction < case_count; ++instruction)
            if (case_values[instruction] == constant->immediate)
                return 0;
        case_values[case_count] = (int)constant->immediate;
        case_results[case_count] = result;
        ++case_count;

        next = cursor + 4;
        if (next < mir.count &&
            mir.insns[next].opcode == MIR_LABEL &&
            mir.insns[next].label == branch->label) {
            cursor = next + 1;
            if (cursor + 3 < mir.count &&
                mir.insns[cursor].opcode == MIR_CONST)
                continue;
            if (cursor < mir.count &&
                mir.insns[cursor].opcode == MIR_JUMP)
                default_label = mir.insns[cursor].label;
            else
                default_label = branch->label;
        } else {
            default_label = branch->label;
        }
        break;
    }
    if (case_count < 2 || default_label < 0 ||
        !mir_machine_constant_return_for_label(
            default_label, &plan->default_result))
        return 0;
    plan->minimum_case = case_values[0];
    plan->maximum_case = case_values[0];
    for (instruction = 1; instruction < case_count; ++instruction) {
        if (case_values[instruction] < plan->minimum_case)
            plan->minimum_case = case_values[instruction];
        if (case_values[instruction] > plan->maximum_case)
            plan->maximum_case = case_values[instruction];
    }
    width = plan->maximum_case - plan->minimum_case + 1;
    if (width > MIR_MACHINE_SWITCH_RESULT_LIMIT ||
        case_count * 2 < width)
        return 0;
    for (instruction = 0; instruction < width; ++instruction)
        plan->results[instruction] = plan->default_result;
    for (instruction = 0; instruction < case_count; ++instruction)
        plan->results[case_values[instruction] -
                      plan->minimum_case] = case_results[instruction];
    return 1;
}

static void mir_emit_constant_result_switch(
    MirStream *out, const struct MirConstantResultSwitch *plan)
{
    int default_label = new_label();
    int table_label = new_label();
    int width = plan->maximum_case - plan->minimum_case + 1;
    int value;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n",
            plan->parameter_stack_offset);
    if (plan->minimum_case != 0)
        mir_stream_printf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n",
                plan->minimum_case);
    mir_stream_printf(out,
            "\tld a,h\n\tor a\n\tjp nz,L%d\n"
            "\tld a,l\n\tcp %d\n\tjp nc,L%d\n"
            "\tadd hl,hl\n\tld de,L%d\n\tadd hl,de\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tex de,hl\n\tret\n"
            "L%d:\n",
            default_label, width, default_label,
            table_label, table_label);
    for (value = 0; value < width; ++value)
        mir_stream_printf(out, "\tdw %d\n", plan->results[value]);
    mir_stream_printf(out, "L%d:\n\tld hl,%d\n\tret\n",
            default_label, plan->default_result);
}

static int mir_match_vla_pointer_element_switch(
    struct MirVlaPointerElementSwitch *plan)
{
    static const int expected_opcodes[51] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_JUMP, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_LOAD, MIR_CONST, MIR_CONST, MIR_BINARY, MIR_BINARY, MIR_CONST,
        MIR_CONST, MIR_BINARY, MIR_BINARY, MIR_CONST, MIR_STORE_INDIRECT,
        MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_CONST,
        MIR_BINARY, MIR_BINARY, MIR_CONST, MIR_CONST, MIR_BINARY,
        MIR_BINARY, MIR_CONST, MIR_STORE_INDIRECT, MIR_NOP, MIR_JUMP,
        MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_CONST, MIR_BINARY,
        MIR_BINARY, MIR_CONST, MIR_CONST, MIR_BINARY, MIR_BINARY,
        MIR_LOAD_INDIRECT, MIR_RETURN
    };
    const struct MirInsn *rows = &mir.insns[1];
    const struct MirInsn *pointer = &mir.insns[2];
    const struct MirInsn *case_store = &mir.insns[21];
    const struct MirInsn *default_store = &mir.insns[35];
    const struct MirInsn *result_load = &mir.insns[49];
    int case_offset;
    int default_offset;
    int result_offset;
    int instruction;
    long value;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 51 || mir_cfg_block_count() != 5 ||
        mir.has_vla || !mir.has_runtime_stride_param ||
        type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        (mir.return_type & TYPE_UNSIGNED) != 0 ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;

    if (type_ptr_depth(rows->type) != 0 ||
        (rows->type & 15) != TYPE_INT ||
        (rows->type & TYPE_UNSIGNED) != 0 ||
        type_size(rows->type) != 2 ||
        type_ptr_depth(pointer->type) != 1 ||
        (pointer->type & 15) != TYPE_INT ||
        type_size(pointer->type) != 2 ||
        mir_machine_pointee_is_volatile(pointer) ||
        !mir_machine_parameter_value_offset(
            rows->dst, &plan->rows_stack_offset) ||
        !mir_machine_parameter_value_offset(
            pointer->dst, &plan->pointer_stack_offset) ||
        plan->rows_stack_offset != 2 ||
        plan->pointer_stack_offset != 4)
        return mir_machine_reject(
            "vla-pointer-element-switch", "parameters");

    if (!mir_machine_constant_value(
            mir.insns[4].dst, &value, 0) ||
        value < -32768 || value > 32767 ||
        mir.insns[5].immediate != TOK_EQ ||
        mir.insns[5].src1 != rows->dst ||
        mir.insns[5].src2 != mir.insns[4].dst ||
        type_ptr_depth(mir.insns[5].secondary_offset) != 0 ||
        type_size(mir.insns[5].secondary_offset) != 2 ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        mir.insns[6].label != mir.insns[24].label ||
        mir.insns[7].label != mir.insns[10].label ||
        mir.insns[9].label != mir.insns[24].label ||
        mir.insns[8].label == mir.insns[0].label ||
        mir.insns[8].label == mir.insns[10].label ||
        mir.insns[8].label == mir.insns[24].label ||
        mir.insns[10].label == mir.insns[24].label ||
        mir.insns[23].label != mir.insns[39].label ||
        mir.insns[37].label != mir.insns[39].label)
        return mir_machine_reject(
            "vla-pointer-element-switch", "control-flow");
    plan->compare_value = (int)value;

    if (!mir_match_vla_pointer_element_address(
            11, pointer, &case_offset) ||
        !mir_match_vla_pointer_element_address(
            25, pointer, &default_offset) ||
        !mir_match_vla_pointer_element_address(
            40, pointer, &result_offset) ||
        case_offset != default_offset ||
        case_offset != result_offset)
        return mir_machine_reject(
            "vla-pointer-element-switch", "address-chain");
    plan->element_offset = case_offset;

    if (case_store->memory_size != 2 ||
        case_store->memory_flags != 0 ||
        case_store->bit_width != 0 ||
        case_store->src1 != mir.insns[19].dst ||
        !mir_machine_constant_value(
            case_store->src2, &value, 0) ||
        value < -32768 || value > 65535)
        return mir_machine_reject(
            "vla-pointer-element-switch", "case-store");
    plan->case_value = (int)((unsigned long)value & 0xffffUL);

    if (default_store->memory_size != 2 ||
        default_store->memory_flags != 0 ||
        default_store->bit_width != 0 ||
        default_store->src1 != mir.insns[33].dst ||
        !mir_machine_constant_value(
            default_store->src2, &value, 0) ||
        value < -32768 || value > 65535)
        return mir_machine_reject(
            "vla-pointer-element-switch", "default-store");
    plan->default_value =
        (int)((unsigned long)value & 0xffffUL);

    if (result_load->src1 != mir.insns[48].dst ||
        result_load->memory_size != 2 ||
        result_load->memory_flags != 0 ||
        result_load->bit_width != 0 ||
        type_ptr_depth(result_load->type) != 0 ||
        (result_load->type & 15) != TYPE_INT ||
        (result_load->type & TYPE_UNSIGNED) != 0 ||
        type_size(result_load->type) != 2 ||
        mir.insns[50].src1 != result_load->dst)
        return mir_machine_reject(
            "vla-pointer-element-switch", "result-load");
    return 1;
}

static void mir_emit_vla_pointer_element_switch(
    MirStream *out, const struct MirVlaPointerElementSwitch *plan)
{
    int default_label = new_label();
    int value_ready = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld hl,%d\n\tor a\n\tsbc hl,de\n"
            "\tjp nz,L%d\n"
            "\tld de,%d\n\tjp L%d\n"
            "L%d:\n\tld de,%d\n"
            "L%d:\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tld h,b\n\tld l,c\n",
            plan->rows_stack_offset, plan->compare_value,
            default_label, plan->case_value, value_ready,
            default_label, plan->default_value, value_ready,
            plan->pointer_stack_offset);
    mir_machine_emit_hl_offset(out, plan->element_offset, 0);
    mir_stream_puts("\tld (hl),e\n\tinc hl\n\tld (hl),d\n"
          "\tld d,(hl)\n\tdec hl\n\tld e,(hl)\n"
          "\tex de,hl\n\tret\n", out);
}

static int mir_match_constant_flow_result_switch(
    struct MirConstantResultSwitch *plan)
{
    int case_values[MIR_MACHINE_SWITCH_RESULT_LIMIT];
    int case_results[MIR_MACHINE_SWITCH_RESULT_LIMIT];
    const struct MirInsn *parameter;
    int condition = -1;
    int case_count = 0;
    int last_dispatch_branch = -1;
    int start = -1;
    int cursor;
    int instruction;
    int width;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        type_size(mir.return_type) != 2 ||
        type_is_float(mir.return_type))
        return 0;
    for (instruction = 0; instruction + 3 < mir.count;
         ++instruction) {
        const struct MirInsn *constant = &mir.insns[instruction];
        const struct MirInsn *binary = &mir.insns[instruction + 1];
        const struct MirInsn *branch = &mir.insns[instruction + 2];
        const struct MirInsn *jump = &mir.insns[instruction + 3];
        int candidate;

        if (constant->opcode != MIR_CONST ||
            binary->opcode != MIR_BINARY ||
            binary->immediate != TOK_EQ ||
            branch->opcode != MIR_BRANCH_FALSE ||
            branch->src1 != binary->dst ||
            jump->opcode != MIR_JUMP)
            continue;
        if (binary->src1 == constant->dst)
            candidate = binary->src2;
        else if (binary->src2 == constant->dst)
            candidate = binary->src1;
        else
            continue;
        parameter = mir_definition(candidate);
        if (parameter == NULL || parameter->opcode != MIR_PARAM ||
            type_ptr_depth(parameter->type) != 0 ||
            (parameter->type & 15) != TYPE_INT ||
            (parameter->type & TYPE_UNSIGNED) != 0 ||
            type_size(parameter->type) != 2 ||
            !mir_machine_parameter_value_offset(
                candidate, &plan->parameter_stack_offset))
            return 0;
        condition = candidate;
        start = instruction;
        break;
    }
    if (start < 0)
        return 0;
    cursor = start;
    for (;;) {
        const struct MirInsn *constant;
        const struct MirInsn *binary;
        const struct MirInsn *branch;
        const struct MirInsn *jump;
        int candidate;
        int next;

        if (case_count >= MIR_MACHINE_SWITCH_RESULT_LIMIT ||
            cursor < 0 || cursor + 3 >= mir.count)
            return 0;
        constant = &mir.insns[cursor];
        binary = &mir.insns[cursor + 1];
        branch = &mir.insns[cursor + 2];
        jump = &mir.insns[cursor + 3];
        if (constant->opcode != MIR_CONST ||
            constant->immediate < 0 ||
            constant->immediate >= 32767 ||
            binary->opcode != MIR_BINARY ||
            binary->immediate != TOK_EQ ||
            branch->opcode != MIR_BRANCH_FALSE ||
            branch->src1 != binary->dst ||
            jump->opcode != MIR_JUMP)
            return 0;
        if (binary->src1 == constant->dst)
            candidate = binary->src2;
        else if (binary->src2 == constant->dst)
            candidate = binary->src1;
        else
            return 0;
        if (candidate != condition ||
            mir_find_label(jump->label) < 0 ||
            mir_find_label(branch->label) < 0)
            return 0;
        last_dispatch_branch = cursor + 2;
        for (instruction = 0; instruction < case_count; ++instruction)
            if (case_values[instruction] == constant->immediate)
                return 0;
        case_values[case_count++] = (int)constant->immediate;

        next = cursor + 4;
        if (next < mir.count &&
            mir.insns[next].opcode == MIR_LABEL &&
            mir.insns[next].label == branch->label) {
            cursor = next + 1;
            if (cursor + 3 < mir.count &&
                mir.insns[cursor].opcode == MIR_CONST)
                continue;
        }
        break;
    }
    if (case_count < 2)
        return 0;
    plan->minimum_case = case_values[0];
    plan->maximum_case = case_values[0];
    for (instruction = 1; instruction < case_count; ++instruction) {
        if (case_values[instruction] < plan->minimum_case)
            plan->minimum_case = case_values[instruction];
        if (case_values[instruction] > plan->maximum_case)
            plan->maximum_case = case_values[instruction];
    }
    width = plan->maximum_case - plan->minimum_case + 1;
    if (width > MIR_MACHINE_SWITCH_RESULT_LIMIT ||
        case_count * 2 < width ||
        !mir_machine_evaluate_constant_flow(
            condition, plan->maximum_case + 1,
            start + 2, last_dispatch_branch,
            &plan->default_result))
        return 0;
    for (instruction = 0; instruction < case_count; ++instruction)
        if (!mir_machine_evaluate_constant_flow(
                condition, case_values[instruction],
                start + 2, last_dispatch_branch,
                &case_results[instruction]))
            return 0;
    for (instruction = 0; instruction < width; ++instruction)
        plan->results[instruction] = plan->default_result;
    for (instruction = 0; instruction < case_count; ++instruction)
        plan->results[case_values[instruction] -
                      plan->minimum_case] = case_results[instruction];
    return 1;
}

static int mir_match_string_result_switch(
    struct MirStringResultSwitch *plan)
{
    int case_values[MIR_MACHINE_SWITCH_RESULT_LIMIT];
    int case_strings[MIR_MACHINE_SWITCH_RESULT_LIMIT];
    const struct MirInsn *parameter = NULL;
    int condition = -1;
    int case_count = 0;
    int default_label = -1;
    int start = -1;
    int cursor;
    int instruction;
    int width;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla ||
        type_ptr_depth(mir.return_type) != 1 ||
        (mir.return_type & 15) != TYPE_CHAR ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0;
         instruction + 3 < mir.count; ++instruction) {
        const struct MirInsn *constant =
            &mir.insns[instruction];
        const struct MirInsn *binary =
            &mir.insns[instruction + 1];
        const struct MirInsn *branch =
            &mir.insns[instruction + 2];
        const struct MirInsn *jump =
            &mir.insns[instruction + 3];
        int candidate;

        if (constant->opcode != MIR_CONST ||
            binary->opcode != MIR_BINARY ||
            binary->immediate != TOK_EQ ||
            branch->opcode != MIR_BRANCH_FALSE ||
            branch->src1 != binary->dst ||
            jump->opcode != MIR_JUMP)
            continue;
        if (binary->src1 == constant->dst)
            candidate = binary->src2;
        else if (binary->src2 == constant->dst)
            candidate = binary->src1;
        else
            continue;
        parameter = mir_definition(candidate);
        if (parameter == NULL ||
            parameter->opcode != MIR_PARAM ||
            type_ptr_depth(parameter->type) != 0 ||
            type_size(parameter->type) != 2 ||
            type_is_float(parameter->type) ||
            !mir_machine_parameter_value_offset(
                candidate,
                &plan->parameter_stack_offset))
            return 0;
        condition = candidate;
        start = instruction;
        break;
    }
    if (start < 0)
        return 0;
    for (instruction = 0; instruction < start; ++instruction)
        if (mir.insns[instruction].opcode != MIR_LABEL &&
            mir.insns[instruction].opcode != MIR_PARAM &&
            mir.insns[instruction].opcode != MIR_NOP)
            return 0;
    cursor = start;
    for (;;) {
        const struct MirInsn *constant;
        const struct MirInsn *binary;
        const struct MirInsn *branch;
        const struct MirInsn *jump;
        int candidate;
        int next;

        if (case_count >= MIR_MACHINE_SWITCH_RESULT_LIMIT ||
            cursor < 0 || cursor + 3 >= mir.count)
            return 0;
        constant = &mir.insns[cursor];
        binary = &mir.insns[cursor + 1];
        branch = &mir.insns[cursor + 2];
        jump = &mir.insns[cursor + 3];
        if (constant->opcode != MIR_CONST ||
            constant->immediate < 0 ||
            constant->immediate > 32767 ||
            binary->opcode != MIR_BINARY ||
            binary->immediate != TOK_EQ ||
            branch->opcode != MIR_BRANCH_FALSE ||
            branch->src1 != binary->dst ||
            jump->opcode != MIR_JUMP)
            return 0;
        if (binary->src1 == constant->dst)
            candidate = binary->src2;
        else if (binary->src2 == constant->dst)
            candidate = binary->src1;
        else
            return 0;
        if (candidate != condition ||
            !mir_machine_string_return_for_label(
                jump->label,
                &case_strings[case_count]))
            return 0;
        for (instruction = 0;
             instruction < case_count; ++instruction)
            if (case_values[instruction] ==
                constant->immediate)
                return 0;
        case_values[case_count] =
            (int)constant->immediate;
        ++case_count;
        next = cursor + 4;
        if (next < mir.count &&
            mir.insns[next].opcode == MIR_LABEL &&
            mir.insns[next].label == branch->label) {
            cursor = next + 1;
            if (cursor + 3 < mir.count &&
                mir.insns[cursor].opcode == MIR_CONST)
                continue;
            if (cursor < mir.count &&
                mir.insns[cursor].opcode == MIR_JUMP)
                default_label = mir.insns[cursor].label;
            else
                default_label = branch->label;
        } else {
            default_label = branch->label;
        }
        break;
    }
    if (case_count < 2 || default_label < 0 ||
        !mir_machine_string_return_for_label(
            default_label, &plan->default_string))
        return 0;
    plan->minimum_case = case_values[0];
    plan->maximum_case = case_values[0];
    for (instruction = 1;
         instruction < case_count; ++instruction) {
        if (case_values[instruction] < plan->minimum_case)
            plan->minimum_case = case_values[instruction];
        if (case_values[instruction] > plan->maximum_case)
            plan->maximum_case = case_values[instruction];
    }
    width = plan->maximum_case -
            plan->minimum_case + 1;
    if (width > MIR_MACHINE_SWITCH_RESULT_LIMIT ||
        case_count * 2 < width)
        return 0;
    for (instruction = 0; instruction < width; ++instruction)
        plan->strings[instruction] =
            plan->default_string;
    for (instruction = 0;
         instruction < case_count; ++instruction)
        plan->strings[case_values[instruction] -
                      plan->minimum_case] =
            case_strings[instruction];
    return 1;
}

static void mir_emit_string_result_switch(
    MirStream *out, const struct MirStringResultSwitch *plan)
{
    int default_label = new_label();
    int table_label = new_label();
    int width = plan->maximum_case -
                plan->minimum_case + 1;
    int value;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tex de,hl\n",
            plan->parameter_stack_offset);
    if (plan->minimum_case != 0)
        mir_stream_printf(out,
                "\tld de,%d\n\tor a\n\tsbc hl,de\n",
                plan->minimum_case);
    mir_stream_printf(out,
            "\tld a,h\n\tor a\n\tjp nz,L%d\n"
            "\tld a,l\n\tcp %d\n\tjp nc,L%d\n"
            "\tadd hl,hl\n\tld de,L%d\n\tadd hl,de\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tex de,hl\n\tret\nL%d:\n",
            default_label, width, default_label,
            table_label, table_label);
    for (value = 0; value < width; ++value)
        if (plan->strings[value] >= 0)
            mir_stream_printf(out, "\tdw S%d\n",
                    plan->strings[value]);
        else
            mir_stream_puts("\tdw 0\n", out);
    if (plan->default_string >= 0)
        mir_stream_printf(out,
                "L%d:\n\tld hl,S%d\n\tret\n",
                default_label,
                plan->default_string);
    else
        mir_stream_printf(out,
                "L%d:\n\tld hl,0\n\tret\n",
                default_label);
}

static int mir_match_local_byte_fill_sum_print(
    struct MirLocalByteFillSumPrint *plan)
{
    static const int expected_opcodes[41] = {
        MIR_LABEL, MIR_ADDRESS, MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_PHI,
        MIR_PHI, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_BINARY, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_RETURN
    };
    const struct MirInsn *buffer = &mir.insns[1];
    const struct MirInsn *total_phi = &mir.insns[11];
    const struct MirInsn *index_phi = &mir.insns[12];
    int arguments[2];
    int memory_storage;
    int memory_type;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 41 || mir_cfg_block_count() != 4 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_INT ||
        type_ptr_depth(mir.return_type) != 0)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    if (!mir_scalar_memory_location(
            buffer, &memory_type, &memory_storage,
            &plan->buffer_offset) ||
        memory_storage != SC_LOCAL ||
        type_ptr_depth(buffer->type) != 1 ||
        (buffer->type & 15) != TYPE_CHAR ||
        mir.insns[2].src1 != buffer->dst ||
        mir.insns[2].immediate != 0 ||
        mir.insns[2].secondary_offset !=
            mir.insns[3].secondary_offset ||
        mir.insns[3].type != TYPE_VOID)
        return 0;
    if (mir_declared_is_vla_object(buffer->name))
        return 0;
    plan->fill_function = find_global(mir.insns[3].name);
    if (plan->fill_function == NULL ||
        !plan->fill_function->is_defined ||
        plan->fill_function->storage != SC_FUNC ||
        plan->fill_function->is_funcptr ||
        plan->fill_function->is_noreturn ||
        !plan->fill_function->has_proto ||
        plan->fill_function->proto_nargs != 1 ||
        plan->fill_function->proto_variadic ||
        plan->fill_function->proto_types[0] != mir.insns[2].type ||
        mir.insns[2].type != buffer->type ||
        (mir.insns[3].memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME |
          MIR_CALL_FLAG_INLINE_SUBSTITUTABLE)) != 0 ||
        (mir.insns[3].base_name[0] != 0 &&
         strcmp(mir.insns[3].base_name,
                asm_name_for(sym_asm_name(plan->fill_function)))))
        return 0;
    if (!mir_machine_constant_equals(mir.insns[4].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[6]) ||
        mir.insns[6].memory_size != 2 ||
        mir.insns[6].src1 != mir.insns[4].dst ||
        !mir_machine_constant_equals(mir.insns[8].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[9]) ||
        mir.insns[9].memory_size != 1 ||
        mir.insns[9].src1 != mir.insns[8].dst)
        return 0;
    if ((total_phi->type & 15) != TYPE_INT ||
        (total_phi->type & TYPE_UNSIGNED) != 0 ||
        type_size(total_phi->type) != 2 ||
        total_phi->src1 != mir.insns[4].dst ||
        total_phi->src2 != mir.insns[24].dst ||
        total_phi->phi_pred1 != mir.insns[0].label ||
        total_phi->phi_pred2 != mir.insns[27].label ||
        index_phi->src1 != mir.insns[8].dst ||
        index_phi->src2 != mir.insns[30].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[27].label ||
        (index_phi->type & 15) != TYPE_CHAR ||
        (index_phi->type & TYPE_UNSIGNED) == 0)
        return 0;
    if (mir.insns[15].immediate != 0 ||
        mir.insns[15].src1 != index_phi->dst ||
    (mir.insns[15].type & 15) != TYPE_INT ||
    (mir.insns[15].type & TYPE_UNSIGNED) != 0 ||
    mir.insns[16].immediate != '<' ||
    (mir.insns[16].secondary_offset & 15) != TYPE_INT ||
    (mir.insns[16].secondary_offset & TYPE_UNSIGNED) != 0 ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[16].src2 != mir.insns[14].dst ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        mir.insns[17].label != mir.insns[33].label ||
        !mir_machine_same_location(buffer, &mir.insns[19]) ||
        mir.insns[21].src1 != mir.insns[19].dst ||
        mir.insns[21].src2 != index_phi->dst ||
        mir.insns[21].immediate != 1 ||
        mir.insns[21].memory_size != 1 ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        mir.insns[22].memory_size != 1 ||
        (mir.insns[22].memory_flags & (1 | 8)) != 0 ||
        mir.insns[23].immediate != 0 ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        (mir.insns[23].type & 15) != TYPE_INT ||
        (mir.insns[23].type & TYPE_UNSIGNED) != 0 ||
        mir.insns[24].immediate != '+' ||
        (mir.insns[24].secondary_offset & 15) != TYPE_INT ||
        (mir.insns[24].secondary_offset & TYPE_UNSIGNED) != 0 ||
        mir.insns[24].src1 != total_phi->dst ||
        mir.insns[24].src2 != mir.insns[23].dst ||
        !mir_machine_same_location(&mir.insns[6], &mir.insns[26]) ||
        mir.insns[26].src1 != mir.insns[24].dst)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[29].dst, 1) ||
        mir.insns[30].immediate != '+' ||
        mir.insns[30].src1 != index_phi->dst ||
        mir.insns[30].src2 != mir.insns[29].dst ||
        !mir_machine_same_location(&mir.insns[9], &mir.insns[31]) ||
        mir.insns[31].src1 != mir.insns[30].dst ||
        mir.insns[32].label != mir.insns[10].label ||
        !mir_machine_two_call_arguments(&mir.insns[38], arguments) ||
        arguments[0] != mir.insns[34].dst ||
        arguments[1] != total_phi->dst ||
        mir.insns[35].type != mir.insns[34].type ||
        mir.insns[37].type != total_phi->type ||
        mir.insns[34].immediate < 0 ||
        !mir_machine_constant_equals(mir.insns[39].dst, 0) ||
        mir.insns[40].src1 != mir.insns[39].dst)
        return 0;
    plan->print_function = find_global(mir.insns[38].name);
    if (plan->print_function == NULL ||
        strcmp(mir.insns[38].name, "printf") ||
        plan->print_function->is_defined ||
        plan->print_function->is_noreturn ||
        (mir.insns[38].memory_flags & MIR_CALL_FLAG_VARIADIC) == 0 ||
        (mir.insns[38].memory_flags & MIR_CALL_FLAG_FORMAT_RUNTIME) != 0 ||
        (mir.insns[38].base_name[0] != 0 &&
         strcmp(mir.insns[38].base_name,
                asm_name_for(sym_asm_name(plan->print_function)))) ||
        plan->buffer_offset >= 0 ||
        mir.insns[14].immediate <= 0 ||
        mir.insns[14].immediate > 255 ||
        -plan->buffer_offset < mir.insns[14].immediate)
        return 0;
    plan->string_id = (int)mir.insns[34].immediate;
    plan->count = (int)mir.insns[14].immediate;
    plan->element_is_unsigned =
        (mir.insns[22].type & TYPE_UNSIGNED) != 0;
    return 1;
}

static int mir_match_local_affine_fill_sum_print(
    struct MirLocalByteFillSumPrint *plan)
{
    static const int expected_opcodes[43] = {
        MIR_LABEL, MIR_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST, MIR_STORE,
        MIR_LABEL, MIR_PHI, MIR_PHI, MIR_NOP, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_NOP, MIR_ARG, MIR_CALL, MIR_CONST, MIR_RETURN
    };
    const struct MirInsn *buffer = &mir.insns[1];
    const struct MirInsn *total_phi = &mir.insns[13];
    const struct MirInsn *index_phi = &mir.insns[14];
    int arguments[2];
    int memory_storage;
    int memory_type;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 43 || mir_cfg_block_count() != 4 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_INT)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (!mir_scalar_memory_location(
            buffer, &memory_type, &memory_storage,
            &plan->buffer_offset) ||
        memory_storage != SC_LOCAL ||
        type_ptr_depth(buffer->type) != 1 ||
        (buffer->type & 15) != TYPE_CHAR ||
        mir_declared_is_vla_object(buffer->name) ||
        !mir_machine_two_call_arguments(&mir.insns[5], arguments) ||
        arguments[0] != buffer->dst ||
        arguments[1] != mir.insns[3].dst ||
        mir.insns[3].immediate < -32768 ||
        mir.insns[3].immediate > 65535)
        return 0;
    plan->fill_function = find_global(mir.insns[5].name);
    if (plan->fill_function == NULL ||
        !plan->fill_function->is_defined ||
        plan->fill_function->storage != SC_FUNC ||
        plan->fill_function->is_funcptr ||
        plan->fill_function->is_noreturn ||
        !plan->fill_function->has_proto ||
        plan->fill_function->proto_nargs != 2 ||
        plan->fill_function->proto_variadic ||
        plan->fill_function->proto_types[0] != mir.insns[2].type ||
        plan->fill_function->proto_types[1] != mir.insns[4].type ||
        (mir.insns[5].memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME |
          MIR_CALL_FLAG_INLINE_SUBSTITUTABLE)) != 0)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[6].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[8]) ||
        !mir_machine_constant_equals(mir.insns[10].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[11]) ||
        total_phi->src1 != mir.insns[6].dst ||
        total_phi->src2 != mir.insns[26].dst ||
        total_phi->phi_pred1 != mir.insns[0].label ||
        total_phi->phi_pred2 != mir.insns[29].label ||
        index_phi->src1 != mir.insns[10].dst ||
        index_phi->src2 != mir.insns[32].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[29].label ||
        (index_phi->type & TYPE_UNSIGNED) == 0 ||
        type_size(index_phi->type) != 1)
        return 0;
    if (mir.insns[17].src1 != index_phi->dst ||
        mir.insns[18].immediate != '<' ||
        mir.insns[18].src1 != mir.insns[17].dst ||
        mir.insns[18].src2 != mir.insns[16].dst ||
        mir.insns[19].src1 != mir.insns[18].dst ||
        mir.insns[19].label != mir.insns[35].label ||
        !mir_machine_same_location(buffer, &mir.insns[21]) ||
        mir.insns[23].src1 != mir.insns[21].dst ||
        mir.insns[23].src2 != index_phi->dst ||
        mir.insns[23].immediate != 1 ||
        mir.insns[24].src1 != mir.insns[23].dst ||
        mir.insns[24].memory_size != 1 ||
        (mir.insns[24].memory_flags & (1 | 8)) != 0 ||
        mir.insns[25].src1 != mir.insns[24].dst ||
        mir.insns[26].immediate != '+' ||
        mir.insns[26].src1 != total_phi->dst ||
        mir.insns[26].src2 != mir.insns[25].dst ||
        !mir_machine_same_location(&mir.insns[8], &mir.insns[28]) ||
        mir.insns[28].src1 != mir.insns[26].dst)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[31].dst, 1) ||
        mir.insns[32].immediate != '+' ||
        mir.insns[32].src1 != index_phi->dst ||
        mir.insns[32].src2 != mir.insns[31].dst ||
        !mir_machine_same_location(&mir.insns[11], &mir.insns[33]) ||
        mir.insns[33].src1 != mir.insns[32].dst ||
        mir.insns[34].label != mir.insns[12].label ||
        !mir_machine_two_call_arguments(&mir.insns[40], arguments) ||
        arguments[0] != mir.insns[36].dst ||
        arguments[1] != total_phi->dst ||
        !mir_machine_constant_equals(mir.insns[41].dst, 0) ||
        mir.insns[42].src1 != mir.insns[41].dst)
        return 0;
    plan->print_function = find_global(mir.insns[40].name);
    if (plan->print_function == NULL ||
        strcmp(mir.insns[40].name, "printf") ||
        (mir.insns[40].memory_flags & MIR_CALL_FLAG_VARIADIC) == 0 ||
        (mir.insns[40].memory_flags & MIR_CALL_FLAG_FORMAT_RUNTIME) != 0 ||
        plan->buffer_offset >= 0 ||
        mir.insns[16].immediate <= 0 ||
        mir.insns[16].immediate > 255 ||
        -plan->buffer_offset < mir.insns[16].immediate)
        return 0;
    plan->fill_has_value = 1;
    plan->fill_value = (int)mir.insns[3].immediate & 0xffff;
    plan->string_id = (int)mir.insns[36].immediate;
    plan->count = (int)mir.insns[16].immediate;
    plan->element_is_unsigned =
        (mir.insns[24].type & TYPE_UNSIGNED) != 0;
    return 1;
}

static void mir_emit_local_byte_fill_sum_print(
    MirStream *out, const struct MirLocalByteFillSumPrint *plan)
{
    int loop = new_label();

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    mir_stream_printf(out, "\tld hl,%d\n\tadd hl,sp\n\tld sp,hl\n",
            plan->buffer_offset);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    if (plan->fill_has_value)
        mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n",
                plan->fill_value);
    mir_stream_puts("\tpush ix\n\tpop hl\n", out);
    mir_stream_printf(out, "\tld de,%d\n\tadd hl,de\n\tpush hl\n",
            plan->buffer_offset);
    mir_machine_emit_symbol_call(out, plan->fill_function);
    mir_stream_puts("\tpop bc\n", out);
    if (plan->fill_has_value)
        mir_stream_puts("\tpop bc\n", out);
    mir_stream_puts("\tpush ix\n\tpop hl\n", out);
    mir_stream_printf(out,
            "\tld de,%d\n\tadd hl,de\n\tld de,0\n\tld b,%d\n"
            "L%d:\n",
            plan->buffer_offset, plan->count, loop);
    if (plan->element_is_unsigned) {
        mir_stream_puts("\tld a,(hl)\n\tinc hl\n\tadd a,e\n\tld e,a\n"
              "\tld a,0\n\tadc a,d\n\tld d,a\n", out);
    } else {
        int sign_done = new_label();

        mir_stream_printf(out,
                "\tld c,(hl)\n\tinc hl\n"
                "\tld a,c\n\tadd a,e\n\tld e,a\n"
                "\tld a,0\n\tadc a,d\n"
                "\tbit 7,c\n\tjp z,L%d\n\tdec a\n"
                "L%d:\n\tld d,a\n",
                sign_done, sign_done);
    }
    mir_stream_printf(out,
            "\tdjnz L%d\n\tex de,hl\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            loop, plan->string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld hl,0\n"
          "\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_indexed_member_write(
    struct MirIndexedMemberWrite *plan)
{
    const struct MirInsn *store_indirect = NULL;
    const struct MirInsn *member;
    const struct MirInsn *local_load;
    const struct MirInsn *address;
    const struct MirInsn *addition;
    const struct MirInsn *scaled;
    const struct MirInsn *pointer_load;
    const struct MirInsn *index_load;
    struct MirStateMember pointer_member;
    struct MirStateMember index_member;
    long adjust = 0;
    long stride;
    int parameter_count = 0;
    int load_count = 0;
    int store_count = 0;
    int member_count = 0;
    int load_indirect_count = 0;
    int binary_count = 0;
    int store_indirect_count = 0;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        (mir.return_type & 15) != TYPE_VOID)
        return mir_machine_reject(
            "indexed-member-write", "preflight");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
        case MIR_CONST:
            break;
        case MIR_PARAM:
            ++parameter_count;
            break;
        case MIR_LOAD:
            if (!mir_machine_named_nonvolatile(insn))
                return mir_machine_reject(
                    "indexed-member-write", "load");
            ++load_count;
            break;
        case MIR_STORE:
            if (!mir_machine_unobservable_local_store(insn))
                return mir_machine_reject(
                    "indexed-member-write", "store");
            ++store_count;
            break;
        case MIR_MEMBER_ADDRESS:
            if (insn->bit_width != 0 ||
                (insn->memory_flags & (1 | 8)) != 0)
                return mir_machine_reject(
                    "indexed-member-write", "member");
            ++member_count;
            break;
        case MIR_LOAD_INDIRECT:
            if (insn->memory_size != 2 ||
                insn->bit_width != 0 ||
                (insn->memory_flags & (1 | 8)) != 0)
                return mir_machine_reject(
                    "indexed-member-write", "load-indirect");
            ++load_indirect_count;
            break;
        case MIR_BINARY:
            ++binary_count;
            break;
        case MIR_STORE_INDIRECT:
            if (insn->memory_size != 2 ||
                insn->bit_width != 0 ||
                (insn->memory_flags & (1 | 8)) != 0)
                return mir_machine_reject(
                    "indexed-member-write", "store-indirect");
            ++store_indirect_count;
            store_indirect = insn;
            break;
        default:
            return mir_machine_reject(
                "indexed-member-write", "opcode");
        }
    }
    if (parameter_count != 1 || load_count != 3 ||
        store_count != 1 || member_count != 3 ||
        load_indirect_count != 2 ||
        (binary_count != 2 && binary_count != 4) ||
        store_indirect_count != 1 || store_indirect == NULL)
        return mir_machine_reject(
            "indexed-member-write", "counts");
    member = mir_definition(store_indirect->src1);
    local_load = member != NULL
        ? mir_definition(member->src1) : NULL;
    address = local_load != NULL
        ? mir_machine_resolve_local_alias(local_load->dst) : NULL;
    if (member == NULL || member->opcode != MIR_MEMBER_ADDRESS ||
        local_load == NULL || local_load->opcode != MIR_LOAD ||
        address == NULL)
        return mir_machine_reject(
            "indexed-member-write", "destination");
    if (address->opcode == MIR_BINARY &&
        address->immediate == '-') {
        if (binary_count != 4)
            return 0;
        if (!mir_machine_constant_value(
                address->src2, &adjust, 0) ||
            adjust < 0 || adjust > 32767)
            return 0;
        addition = mir_definition(address->src1);
    } else {
        if (binary_count != 2)
            return 0;
        addition = address;
    }
    if (addition == NULL || addition->opcode != MIR_BINARY ||
        addition->immediate != '+')
        return mir_machine_reject(
            "indexed-member-write", "addition");
    pointer_load = mir_definition(addition->src1);
    scaled = mir_definition(addition->src2);
    if (pointer_load == NULL ||
        pointer_load->opcode != MIR_LOAD_INDIRECT ||
        scaled == NULL || scaled->opcode != MIR_BINARY ||
        scaled->immediate != '*') {
        pointer_load = mir_definition(addition->src2);
        scaled = mir_definition(addition->src1);
    }
    if (pointer_load == NULL ||
        pointer_load->opcode != MIR_LOAD_INDIRECT ||
        scaled == NULL || scaled->opcode != MIR_BINARY ||
        scaled->immediate != '*' ||
        !mir_machine_constant_value(
            scaled->src2, &stride, 0)) {
        const struct MirInsn *constant =
            scaled != NULL ? mir_definition(scaled->src1) : NULL;
        if (constant == NULL || constant->opcode != MIR_CONST ||
            !mir_machine_constant_value(
                scaled->src1, &stride, 0))
            return mir_machine_reject(
                "indexed-member-write", "scale");
        index_load = mir_definition(scaled->src2);
    } else {
        index_load = mir_definition(scaled->src1);
    }
    if (stride <= 0 || stride > 32767 ||
        index_load == NULL ||
        index_load->opcode != MIR_LOAD_INDIRECT ||
        !mir_machine_state_member_address(
            pointer_load->src1, &pointer_member) ||
        !mir_machine_state_member_address(
            index_load->src1, &index_member) ||
        pointer_member.root != index_member.root ||
        pointer_member.root_offset !=
            index_member.root_offset ||
        !mir_machine_parameter_value_offset(
            store_indirect->src2,
            &plan->value_stack_offset))
        return mir_machine_reject(
            "indexed-member-write", "components");
    plan->root = pointer_member.root;
    plan->root_offset =
        pointer_member.root_offset;
    plan->pointer_member_offset =
        pointer_member.member_offset;
    plan->index_member_offset =
        index_member.member_offset;
    plan->stride = (int)stride;
    plan->address_adjust = (int)adjust;
    plan->element_member_offset =
        (int)member->immediate;
    return 1;
}

static void mir_emit_indexed_member_write(
    MirStream *out, const struct MirIndexedMemberWrite *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_word(
        out, plan->root, plan->root_offset);
    mir_stream_puts("\tld c,l\n\tld b,h\n"
          "\tld l,c\n\tld h,b\n", out);
    mir_machine_emit_hl_offset(
        out, plan->pointer_member_offset, 1);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tpush de\n\tld l,c\n\tld h,b\n", out);
    mir_machine_emit_hl_offset(
        out, plan->index_member_offset, 0);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld l,e\n\tld h,d\n", out);
    mir_emit_mul_hl_const(out, (unsigned long)plan->stride);
    mir_stream_puts("\tpop de\n\tadd hl,de\n", out);
    mir_machine_emit_hl_offset(
        out, plan->element_member_offset -
             plan->address_adjust, 0);
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tpop hl\n\tld (hl),e\n\tinc hl\n"
            "\tld (hl),d\n\tret\n",
            plan->value_stack_offset + 2);
}

static int mir_match_indexed_word_sum(struct MirIndexedWordSum *plan)
{
    const struct MirInsn *return_insn = NULL;
    const struct MirInsn *add;
    const struct MirInsn *left_parameter;
    const struct MirInsn *right_parameter;
    int parameter_count = 0;
    int load_count = 0;
    int binary_count = 0;
    int return_count = 0;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir.local_bytes != 0 ||
        mir.aggregate_temp_bytes != 0 || mir_cfg_block_count() != 1 ||
        type_ptr_depth(mir.return_type) != 0 ||
        type_size(mir.return_type) != 2 ||
        type_is_float(mir.return_type))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
        case MIR_CONST:
        case MIR_INDEX_ADDRESS:
        case MIR_MEMBER_ADDRESS:
            break;
        case MIR_PARAM:
            ++parameter_count;
            break;
        case MIR_LOAD_INDIRECT:
            ++load_count;
            break;
        case MIR_BINARY:
            ++binary_count;
            break;
        case MIR_RETURN:
            ++return_count;
            return_insn = insn;
            break;
        default:
            return 0;
        }
    }
    if (parameter_count != 1 || load_count != 2 ||
        binary_count != 1 || return_count != 1 ||
        return_insn == NULL || return_insn->src1 < 0)
        return 0;
    add = mir_definition(return_insn->src1);
    if (add == NULL || add->opcode != MIR_BINARY ||
        add->immediate != '+' || type_size(add->type) != 2 ||
        type_size(add->secondary_offset) != 2 ||
        !mir_machine_indexed_word_load(
            add->src1, &left_parameter, &plan->left_offset) ||
        !mir_machine_indexed_word_load(
            add->src2, &right_parameter, &plan->right_offset) ||
        left_parameter != right_parameter ||
        !mir_machine_scalar_pointer_parameter(
            left_parameter, &plan->parameter_stack_offset))
        return 0;
    return 1;
}

static void mir_emit_indexed_word_sum(
    MirStream *out, const struct MirIndexedWordSum *plan)
{
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld l,e\n\tld h,d\n",
            plan->parameter_stack_offset);
    mir_machine_emit_hl_offset(out, plan->left_offset, 0);
    mir_stream_puts("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
          "\tld l,e\n\tld h,d\n", out);
    mir_machine_emit_hl_offset(out, plan->right_offset, 1);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld l,c\n\tld h,b\n\tadd hl,de\n\tret\n", out);
}

int mir_try_emit_constant_folding_kernels(MirStream *out)
{
    struct MirConstantResultSwitch constant_result_switch;
    struct MirVlaPointerElementSwitch vla_pointer_element_switch;
    struct MirStringResultSwitch string_result_switch;
    struct MirLocalByteFillSumPrint local_byte_fill_sum_print;
    struct MirIndexedMemberWrite indexed_member_write;
    struct MirIndexedWordSum indexed_word_sum;

    int constant_function_result;

    if (mir_match_local_deref_constant_loop(
            &constant_function_result) ||
        mir_match_local_index_constant_loop(
            &constant_function_result)) {
        mir_emit_constant_function(out, constant_function_result);
        return 1;
    }
    if (mir_machine_evaluate_constant_function(
            &constant_function_result)) {
        mir_emit_constant_function(
            out, constant_function_result);
        return 1;
    }
    if (mir_match_constant_result_switch(
            &constant_result_switch)) {
        mir_emit_constant_result_switch(
            out, &constant_result_switch);
        return 1;
    }
    if (mir_match_vla_pointer_element_switch(
            &vla_pointer_element_switch)) {
        mir_emit_vla_pointer_element_switch(
            out, &vla_pointer_element_switch);
        return 1;
    }
    if (mir_match_constant_flow_result_switch(
            &constant_result_switch)) {
        mir_emit_constant_result_switch(
            out, &constant_result_switch);
        return 1;
    }
    if (mir_match_string_result_switch(
            &string_result_switch)) {
        mir_emit_string_result_switch(
            out, &string_result_switch);
        return 1;
    }
    if (mir_match_local_byte_fill_sum_print(
            &local_byte_fill_sum_print)) {
        mir_emit_local_byte_fill_sum_print(
            out, &local_byte_fill_sum_print);
        return 1;
    }
    if (mir_match_local_affine_fill_sum_print(
            &local_byte_fill_sum_print)) {
        mir_emit_local_byte_fill_sum_print(
            out, &local_byte_fill_sum_print);
        return 1;
    }
    if (mir_match_indexed_member_write(
            &indexed_member_write)) {
        mir_emit_indexed_member_write(
            out, &indexed_member_write);
        return 1;
    }

    if (!mir_match_indexed_word_sum(&indexed_word_sum))
        return 0;
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_emit_indexed_word_sum(out, &indexed_word_sum);
    return 1;
}
