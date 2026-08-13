/* dcc_mir_machine_aggregate_checks.c - strict aggregate check schedules. */

#include "dcc_mir_machine_internal.h"

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
static void mir_machine_emit_global_address_hl(
    FILE *out, struct Sym *symbol, int offset)
{
    const char *name = asm_name_for(sym_asm_name(symbol));

    if ((symbol->storage == SC_EXTERN || symbol->needs_extrn) &&
        mir_extrn_should_emit(symbol))
        fprintf(out, "\textrn %s\n", name);
    if (offset == 0)
        fprintf(out, "\tld hl,%s\n", name);
    else
        fprintf(out, "\tld hl,%s%+d\n", name, offset);
}

enum MirAggregateCheckValueKind {
    MIR_AGGREGATE_CHECK_CONSTANT = 1,
    MIR_AGGREGATE_CHECK_LOCAL = 2,
    MIR_AGGREGATE_CHECK_GLOBAL = 3
};

struct MirAggregateCheckValue {
    int kind;
    int width;
    int is_unsigned;
    int local_offset;
    struct Sym *root;
    int root_offset;
    unsigned long value;
};

struct MirAggregateCheck {
    struct Sym *function;
    int string_id;
    int width;
    unsigned long expected;
    struct MirAggregateCheckValue actual;
};

struct MirAggregateInitStore {
    int offset;
    int width;
    unsigned long value;
};

struct MirAggregateMultidimChecks {
    int kind;
    struct Sym *print_function;
    struct Sym *check_word_function;
    struct Sym *check_wide_function;
    int heading_string_id;
    int summary_string_id;
    char heading_call_name[64];
    char summary_call_name[64];
    struct {
        struct Sym *board_root;
        struct Sym *cells_root;
        struct Sym *board_fill_function;
        struct Sym *board_weight_function;
        struct Sym *tile_function;
        struct Sym *cells_fill_function;
        struct Sym *cells_checksum_function;
        int strings[6];
        int rows;
        int columns;
        int board_row_stride;
        int board_column_stride;
        int board_weight_offset;
        int cells_stride;
        int cells_tag_offset;
        int cells_index;
        int w_ptr_offset;
        int w_struct_offset;
        int tile_offset;
        int row_offset;
        int column_offset;
        unsigned long weight_expected;
        unsigned long tile_weight_expected;
        unsigned long tile_char_expected;
        unsigned long checksum_expected;
        unsigned long tag_expected;
    } multidim;
    struct {
        struct MirAggregateInitStore stores[43];
        struct MirAggregateCheck checks[17];
        int check_count;
        int strings[5];
        int lg_offset;
        int lu_offset;
        int lb_offset;
        int lop_offset;
        int sum_offset;
        int row_offset;
        int column_offset;
        int index_offset;
        int row_count;
        int column_count;
        int row_stride;
        int column_stride;
        int first_member_offset;
        int second_member_offset;
        unsigned long first_addend;
        unsigned long second_addend;
        unsigned long sum_expected;
    } size2;
};

static int mir_aggregate_direct_function(
    int instruction, struct Sym **function_out)
{
    const struct MirInsn *call;

    if (instruction < 0 || instruction >= mir.count ||
        mir.insns[instruction].opcode != MIR_CALL)
        return 0;
    call = &mir.insns[instruction];
    *function_out = find_global(call->name);
    if (*function_out == NULL ||
        (*function_out)->is_funcptr || (*function_out)->is_noreturn ||
        ((call->memory_flags & MIR_CALL_FLAG_VARIADIC) == 0 &&
         call->base_name[0] != 0 &&
         strcmp(call->base_name,
                asm_name_for(sym_asm_name(*function_out)))))
        return 0;
    return 1;
}

static int mir_aggregate_fixed_address(
    int value, int before, struct MirAggregateCheckValue *result, int depth)
{
    const struct MirInsn *definition;
    int definition_index;

    if (depth > 16 ||
        (definition = mir_definition(value)) == NULL)
        return 0;
    definition_index = (int)(definition - mir.insns);
    if (definition_index >= before)
        return 0;
    if (definition->opcode == MIR_ADDRESS) {
        int type;
        int storage;
        int offset;

        if (!mir_scalar_memory_location(
                definition, &type, &storage, &offset) ||
            (storage != SC_LOCAL && storage != SC_GLOBAL))
            return 0;
        memset(result, 0, sizeof(*result));
        if (storage == SC_LOCAL) {
            result->kind = MIR_AGGREGATE_CHECK_LOCAL;
            result->local_offset = offset;
        } else {
            result->kind = MIR_AGGREGATE_CHECK_GLOBAL;
            result->root = find_global(definition->name);
            result->root_offset = offset;
            if (result->root == NULL || !result->root->is_defined ||
                result->root->is_volatile)
                return 0;
        }
        return 1;
    }
    if (definition->opcode == MIR_MEMBER_ADDRESS) {
        if (!mir_aggregate_fixed_address(
                definition->src1, definition_index,
                result, depth + 1))
            return 0;
        if (result->kind == MIR_AGGREGATE_CHECK_LOCAL)
            result->local_offset += (int)definition->immediate;
        else
            result->root_offset += (int)definition->immediate;
        return 1;
    }
    if (definition->opcode == MIR_INDEX_ADDRESS) {
        long index;
        long adjustment;

        if (definition->immediate <= 0 ||
            !mir_machine_constant_value(
                definition->src2, &index, 0) ||
            index < 0 ||
            !mir_machine_fold_integer_binary(
                '*', index, definition->immediate,
                TYPE_INT, &adjustment) ||
            adjustment < 0 || adjustment > 32767 ||
            !mir_aggregate_fixed_address(
                definition->src1, definition_index,
                result, depth + 1))
            return 0;
        if (result->kind == MIR_AGGREGATE_CHECK_LOCAL)
            result->local_offset += (int)adjustment;
        else
            result->root_offset += (int)adjustment;
        return 1;
    }
    return 0;
}

static int mir_aggregate_check_value(
    int value, int before, struct MirAggregateCheckValue *result)
{
    const struct MirInsn *definition;
    long constant;

    definition = mir_definition(value);
    while (definition != NULL &&
           definition->opcode == MIR_UNARY &&
           definition->immediate == 0)
        definition = mir_definition(definition->src1);
    if (definition == NULL)
        return 0;
    if (mir_machine_constant_value(
            definition->dst, &constant, 0)) {
        memset(result, 0, sizeof(*result));
        result->kind = MIR_AGGREGATE_CHECK_CONSTANT;
        result->width = type_size(definition->type);
        result->is_unsigned =
            (definition->type & TYPE_UNSIGNED) != 0;
        result->value = (unsigned long)constant;
        return result->width == 1 || result->width == 2 ||
            result->width == 4;
    }
    if (definition->opcode != MIR_LOAD_INDIRECT ||
        (definition->memory_flags & (1 | 8)) != 0 ||
        definition->bit_width != 0 ||
        (definition->memory_size != 1 &&
         definition->memory_size != 2 &&
         definition->memory_size != 4) ||
        !mir_aggregate_fixed_address(
            definition->src1, before, result, 0))
        return 0;
    result->width = definition->memory_size;
    result->is_unsigned =
        (definition->type & TYPE_UNSIGNED) != 0;
    return 1;
}

static int mir_aggregate_match_check(
    int instruction, struct MirAggregateCheck *check,
    struct Sym **word_function, struct Sym **wide_function)
{
    const struct MirInsn *call;
    const struct MirInsn *string;
    const struct MirInsn *actual_definition;
    struct Sym *function;
    int arguments[3];
    long expected;
    int width;

    if (instruction < 0 || instruction >= mir.count)
        return 0;
    call = &mir.insns[instruction];
    if (!mir_aggregate_direct_function(instruction, &function) ||
        !mir_machine_three_call_arguments(call, arguments) ||
        (string = mir_definition(arguments[0])) == NULL ||
        string->opcode != MIR_STRING_ADDRESS ||
        (actual_definition = mir_definition(arguments[1])) == NULL ||
        !mir_machine_constant_value(arguments[2], &expected, 0))
        return 0;
    width = type_size(actual_definition->type);
    if (width != 2 && width != 4)
        return 0;
    if (width == 2) {
        if (*word_function == NULL)
            *word_function = function;
        else if (*word_function != function)
            return 0;
    } else {
        if (*wide_function == NULL)
            *wide_function = function;
        else if (*wide_function != function)
            return 0;
    }
    memset(check, 0, sizeof(*check));
    check->function = function;
    check->string_id = (int)string->immediate;
    check->width = width;
    check->expected = (unsigned long)expected;
    return mir_aggregate_check_value(
        arguments[1], instruction, &check->actual);
}

static int mir_aggregate_local_location(
    const struct MirInsn *insn, int width, int *offset_out)
{
    int type;
    int storage;
    int offset;

    if (insn == NULL || !mir_scalar_memory_location(
            insn, &type, &storage, &offset) ||
        storage != SC_LOCAL || insn->memory_size != width ||
    (insn->memory_flags & (1 | 8)) != 0)
        return 0;
    *offset_out = offset;
    return 1;
}

static int mir_match_multidim_aggregate_checks(
    struct MirAggregateMultidimChecks *plan)
{
    static const int call_indices[14] = {
        3, 7, 13, 22, 81, 89, 100, 117, 118, 121, 125, 136, 141, 143
    };
    static const int string_indices[8] = {
        1, 16, 75, 92, 101, 119, 126, 137
    };
    int opcode_counts[MIR_RETURN + 1];
    struct Sym *functions[14];
    struct Sym *board_roots[4];
    struct Sym *cells_root;
    long root_offsets[4];
    long cells_offset;
    int instruction;
    int index;
    long constant;
    int arguments[3];

    memset(plan, 0, sizeof(*plan));
    memset(opcode_counts, 0, sizeof(opcode_counts));
    if (mir.count != 144 || mir_cfg_block_count() != 7 ||
        mir.local_bytes != 13 || mir.has_vla ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode < 0 || insn->opcode > MIR_RETURN)
            return 0;
        ++opcode_counts[insn->opcode];
    }
    if (opcode_counts[MIR_LABEL] != 7 ||
        opcode_counts[MIR_STRING_ADDRESS] != 8 ||
        opcode_counts[MIR_ARG] != 28 ||
        opcode_counts[MIR_CALL] != 14 ||
        opcode_counts[MIR_ADDRESS] != 5 ||
        opcode_counts[MIR_NOP] != 23 ||
        opcode_counts[MIR_MEMBER_ADDRESS] != 6 ||
        opcode_counts[MIR_CONST] != 17 ||
        opcode_counts[MIR_STORE] != 8 ||
        opcode_counts[MIR_LOAD] != 10 ||
        opcode_counts[MIR_BINARY] != 5 ||
        opcode_counts[MIR_BRANCH_FALSE] != 2 ||
        opcode_counts[MIR_INDEX_ADDRESS] != 3 ||
        opcode_counts[MIR_LOAD_INDIRECT] != 4 ||
        opcode_counts[MIR_UNARY] != 2 ||
        opcode_counts[MIR_JUMP] != 2)
        return 0;
    for (index = 0; index < 14; ++index)
        if (!mir_aggregate_direct_function(
                call_indices[index], &functions[index]))
            return 0;
    if (functions[3] != functions[4] ||
        functions[3] != functions[10] ||
        functions[6] != functions[7] ||
        functions[6] != functions[11] ||
        functions[9] != functions[12])
        return 0;
    for (index = 0; index < 8; ++index) {
        const struct MirInsn *string =
            &mir.insns[string_indices[index]];

        if (string->opcode != MIR_STRING_ADDRESS)
            return 0;
        if (index == 0)
            plan->heading_string_id = (int)string->immediate;
        else if (index == 7)
            plan->summary_string_id = (int)string->immediate;
        else
            plan->multidim.strings[index - 1] =
                (int)string->immediate;
    }
    for (index = 0; index < 4; ++index) {
        int address_index =
            index == 0 ? 4 : index == 1 ? 8 :
            index == 2 ? 48 : 82;

        if (!mir_machine_global_address_offset(
                mir.insns[address_index].dst,
                &board_roots[index], &root_offsets[index], 0) ||
            board_roots[index]->is_volatile)
            return 0;
    }
    if (board_roots[0] != board_roots[1] ||
        board_roots[0] != board_roots[2] ||
        board_roots[0] != board_roots[3] ||
        root_offsets[0] != root_offsets[1] ||
        root_offsets[0] != root_offsets[2] ||
        root_offsets[0] != root_offsets[3] ||
        root_offsets[0] != 0 ||
        !mir_machine_global_address_offset(
            mir.insns[128].dst, &cells_root, &cells_offset, 0) ||
        cells_offset != 0 ||
        cells_root == board_roots[0] || cells_root->is_volatile)
        return 0;
    if (!mir_aggregate_local_location(
            &mir.insns[15], 4, &plan->multidim.w_ptr_offset) ||
        !mir_aggregate_local_location(
            &mir.insns[25], 4, &plan->multidim.w_struct_offset) ||
        !mir_aggregate_local_location(
            &mir.insns[59], 4, &index) ||
        index != plan->multidim.w_struct_offset ||
        !mir_aggregate_local_location(
            &mir.insns[29], 1, &plan->multidim.row_offset) ||
        !mir_aggregate_local_location(
            &mir.insns[71], 1, &index) ||
        index != plan->multidim.row_offset ||
        !mir_aggregate_local_location(
            &mir.insns[39], 2, &plan->multidim.column_offset) ||
        !mir_aggregate_local_location(
            &mir.insns[64], 2, &index) ||
        index != plan->multidim.column_offset ||
        !mir_aggregate_local_location(
            &mir.insns[91], 2, &plan->multidim.tile_offset))
        return 0;
    if (mir.insns[51].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[53].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[54].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[130].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[131].opcode != MIR_MEMBER_ADDRESS ||
        !mir_machine_constant_value(
            mir.insns[11].dst, &constant, 0) ||
        constant <= 0 || constant > 127)
        return 0;
    plan->multidim.rows = (int)constant;
    if (!mir_machine_constant_value(
            mir.insns[44].dst, &constant, 0) ||
        constant <= 0 || constant > 127)
        return 0;
    plan->multidim.columns = (int)constant;
    plan->multidim.board_row_stride =
        (int)mir.insns[51].immediate;
    plan->multidim.board_column_stride =
        (int)mir.insns[53].immediate;
    plan->multidim.board_weight_offset =
        (int)mir.insns[54].immediate;
    plan->multidim.cells_stride =
        (int)mir.insns[130].immediate;
    plan->multidim.cells_tag_offset =
        (int)mir.insns[131].immediate;
    if (plan->multidim.board_row_stride <= 0 ||
        plan->multidim.board_column_stride <= 0 ||
        plan->multidim.board_row_stride !=
            plan->multidim.board_column_stride *
            plan->multidim.columns ||
        plan->multidim.cells_stride <= 0)
        return 0;
    if (!mir_machine_three_call_arguments(
            &mir.insns[22], arguments) ||
        !mir_machine_constant_value(arguments[2], &constant, 0))
        return 0;
    plan->multidim.weight_expected = (unsigned long)constant;
    if (!mir_machine_three_call_arguments(
            &mir.insns[100], arguments) ||
        !mir_machine_constant_value(arguments[2], &constant, 0))
        return 0;
    plan->multidim.tile_weight_expected = (unsigned long)constant;
    if (!mir_machine_three_call_arguments(
            &mir.insns[117], arguments) ||
        !mir_machine_constant_value(arguments[2], &constant, 0))
        return 0;
    plan->multidim.tile_char_expected = (unsigned long)constant;
    if (!mir_machine_three_call_arguments(
            &mir.insns[125], arguments) ||
        !mir_machine_constant_value(arguments[2], &constant, 0))
        return 0;
    plan->multidim.checksum_expected = (unsigned long)constant;
    if (!mir_machine_three_call_arguments(
            &mir.insns[136], arguments) ||
        !mir_machine_constant_value(arguments[2], &constant, 0) ||
        !mir_machine_constant_value(
            mir.insns[129].dst, &cells_offset, 0))
        return 0;
    plan->multidim.tag_expected = (unsigned long)constant;
    plan->multidim.cells_index = (int)cells_offset;
    if (!mir_machine_single_call_argument(
            &mir.insns[3], &index) ||
        index != mir.insns[1].dst ||
        !mir_machine_single_call_argument(
            &mir.insns[7], &index) ||
        index != mir.insns[4].dst ||
        !mir_machine_two_call_arguments(
            &mir.insns[13], arguments) ||
        arguments[0] != mir.insns[9].dst ||
        arguments[1] != mir.insns[11].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[22], arguments) ||
        arguments[0] != mir.insns[16].dst ||
        arguments[1] != mir.insns[13].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[81], arguments) ||
        arguments[0] != mir.insns[75].dst ||
        arguments[1] != mir.insns[77].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[89], arguments) ||
        arguments[0] != mir.insns[82].dst ||
        arguments[1] != mir.insns[85].dst ||
        arguments[2] != mir.insns[87].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[100], arguments) ||
        arguments[0] != mir.insns[92].dst ||
        arguments[1] != mir.insns[96].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[117], arguments) ||
        arguments[0] != mir.insns[101].dst ||
        arguments[1] != mir.insns[106].dst ||
        !mir_machine_call_has_no_arguments(&mir.insns[118]) ||
        !mir_machine_call_has_no_arguments(&mir.insns[121]) ||
        !mir_machine_three_call_arguments(
            &mir.insns[125], arguments) ||
        arguments[0] != mir.insns[119].dst ||
        arguments[1] != mir.insns[121].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[136], arguments) ||
        arguments[0] != mir.insns[126].dst ||
        arguments[1] != mir.insns[132].dst ||
        !mir_machine_call_has_no_arguments(&mir.insns[141]) ||
        !mir_machine_three_call_arguments(
            &mir.insns[143], arguments) ||
        arguments[0] != mir.insns[137].dst ||
        arguments[1] != mir.insns[13].dst ||
        arguments[2] != mir.insns[141].dst ||
        mir.insns[15].src1 != mir.insns[13].dst ||
        !mir_machine_constant_equals(mir.insns[25].src1, 0) ||
        !mir_machine_constant_equals(mir.insns[29].src1, 0) ||
        !mir_machine_constant_equals(mir.insns[39].src1, 0) ||
        mir.insns[59].src1 != mir.insns[57].dst ||
        mir.insns[64].src1 != mir.insns[63].dst ||
        mir.insns[71].src1 != mir.insns[70].dst ||
        mir.insns[91].src1 != mir.insns[89].dst)
        return 0;
    plan->kind = 1;
    plan->print_function = functions[0];
    if (functions[13] != plan->print_function)
        return 0;
    plan->check_wide_function = functions[3];
    plan->check_word_function = functions[6];
    plan->multidim.board_root = board_roots[0];
    plan->multidim.cells_root = cells_root;
    plan->multidim.board_fill_function = functions[1];
    plan->multidim.board_weight_function = functions[2];
    plan->multidim.tile_function = functions[5];
    plan->multidim.cells_fill_function = functions[8];
    plan->multidim.cells_checksum_function = functions[9];
    snprintf(plan->heading_call_name,
             sizeof(plan->heading_call_name), "%s",
             mir.insns[3].base_name[0] != 0
                 ? mir.insns[3].base_name
                 : asm_name_for(sym_asm_name(functions[0])));
    snprintf(plan->summary_call_name,
             sizeof(plan->summary_call_name), "%s",
             mir.insns[143].base_name[0] != 0
                 ? mir.insns[143].base_name
                 : asm_name_for(sym_asm_name(functions[13])));
    return plan->check_word_function != plan->check_wide_function &&
        plan->multidim.board_fill_function !=
            plan->multidim.board_weight_function &&
        plan->multidim.cells_fill_function !=
            plan->multidim.cells_checksum_function;
}

static int mir_match_size2_aggregate_checks(
    struct MirAggregateMultidimChecks *plan)
{
    static const int check_indices[17] = {
        224, 235, 245, 256, 266, 277, 289, 299, 310,
        320, 331, 341, 352, 363, 373, 384, 395
    };
    int opcode_counts[MIR_RETURN + 1];
    struct Sym *function;
    const struct MirInsn *string;
    int instruction;
    int store;
    int check;
    int offset;
    int type;
    int storage;
    long constant;
    long expected;
    int arguments[3];

    memset(plan, 0, sizeof(*plan));
    memset(opcode_counts, 0, sizeof(opcode_counts));
    if (mir.count != 401 || mir_cfg_block_count() != 7 ||
        mir.local_bytes != 86 || mir.has_vla ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode < 0 || insn->opcode > MIR_RETURN)
            return 0;
        ++opcode_counts[insn->opcode];
    }
    if (opcode_counts[MIR_LABEL] != 7 ||
        opcode_counts[MIR_CONST] != 101 ||
        opcode_counts[MIR_STORE] != 50 ||
        opcode_counts[MIR_NOP] != 37 ||
        opcode_counts[MIR_STRING_ADDRESS] != 24 ||
        opcode_counts[MIR_ARG] != 69 ||
        opcode_counts[MIR_CALL] != 24 ||
        opcode_counts[MIR_BINARY] != 18 ||
        opcode_counts[MIR_PHI] != 1 ||
        opcode_counts[MIR_BRANCH_FALSE] != 2 ||
        opcode_counts[MIR_LOAD] != 9 ||
        opcode_counts[MIR_ADDRESS] != 13 ||
        opcode_counts[MIR_INDEX_ADDRESS] != 16 ||
        opcode_counts[MIR_MEMBER_ADDRESS] != 14 ||
        opcode_counts[MIR_LOAD_INDIRECT] != 13 ||
        opcode_counts[MIR_UNARY] != 1 ||
        opcode_counts[MIR_JUMP] != 2)
        return 0;
    for (store = 0; store < 43; ++store) {
        const struct MirInsn *value = &mir.insns[store * 2 + 1];
        const struct MirInsn *write = &mir.insns[store * 2 + 2];

        if (value->opcode != MIR_CONST ||
            write->opcode != MIR_STORE ||
            write->src1 != value->dst ||
            write->memory_flags != 0 ||
            !mir_scalar_memory_location(
                write, &type, &storage, &offset) ||
            storage != SC_LOCAL ||
            (write->memory_size != 1 &&
             write->memory_size != 2 &&
             write->memory_size != 4))
            return 0;
        plan->size2.stores[store].offset = offset;
        plan->size2.stores[store].width = write->memory_size;
        plan->size2.stores[store].value =
            (unsigned long)value->immediate;
    }
    if (!mir_aggregate_direct_function(
            92, &plan->print_function) ||
        !mir_aggregate_direct_function(400, &function) ||
        function != plan->print_function ||
        !mir_aggregate_direct_function(
            102, &plan->check_word_function) ||
        !mir_aggregate_direct_function(112, &function) ||
        function != plan->check_word_function ||
        !mir_aggregate_direct_function(164, &function) ||
        function != plan->check_word_function ||
        !mir_aggregate_direct_function(
            180, &plan->check_wide_function) ||
        !mir_aggregate_direct_function(214, &function) ||
        function != plan->check_wide_function ||
        plan->check_word_function == plan->check_wide_function)
        return 0;
    string = mir_definition(mir.insns[91].src1);
    if (string == NULL || string->opcode != MIR_STRING_ADDRESS)
        return 0;
    plan->heading_string_id = (int)string->immediate;
    if (!mir_machine_three_call_arguments(
            &mir.insns[102], arguments) ||
        !mir_machine_constant_value(arguments[1], &constant, 0) ||
    !mir_machine_constant_value(arguments[2], &expected, 0) ||
    constant != expected || constant <= 0 || constant > 127)
        return 0;
    plan->size2.row_count = (int)constant;
    string = mir_definition(arguments[0]);
    if (string == NULL || string->opcode != MIR_STRING_ADDRESS)
        return 0;
    plan->size2.strings[0] = (int)string->immediate;
    if (!mir_machine_three_call_arguments(
            &mir.insns[112], arguments) ||
        !mir_machine_constant_value(arguments[1], &constant, 0) ||
    !mir_machine_constant_value(arguments[2], &expected, 0) ||
    constant != expected || constant <= 0 || constant > 127)
        return 0;
    plan->size2.column_count = (int)constant /
        plan->size2.row_count;
    if (plan->size2.column_count <= 0 ||
        plan->size2.row_count *
            plan->size2.column_count != constant)
        return 0;
    string = mir_definition(arguments[0]);
    if (string == NULL || string->opcode != MIR_STRING_ADDRESS)
        return 0;
    plan->size2.strings[1] = (int)string->immediate;
    if (!mir_aggregate_local_location(
            &mir.insns[89], 4, &plan->size2.sum_offset) ||
        !mir_aggregate_local_location(
            &mir.insns[121], 2, &plan->size2.row_offset) ||
        !mir_aggregate_local_location(
            &mir.insns[133], 2, &plan->size2.column_offset) ||
        !mir_aggregate_local_location(
            &mir.insns[149], 2, &plan->size2.index_offset) ||
        !mir_scalar_memory_location(
            &mir.insns[152], &type, &storage,
            &plan->size2.lg_offset) ||
        storage != SC_LOCAL ||
        !mir_scalar_memory_location(
            &mir.insns[302], &type, &storage,
            &plan->size2.lu_offset) ||
        storage != SC_LOCAL ||
        !mir_scalar_memory_location(
            &mir.insns[323], &type, &storage,
            &plan->size2.lb_offset) ||
        storage != SC_LOCAL ||
        !mir_scalar_memory_location(
            &mir.insns[376], &type, &storage,
            &plan->size2.lop_offset) ||
        storage != SC_LOCAL)
        return 0;
    if (!mir_machine_single_call_argument(
            &mir.insns[92], &offset) ||
        offset != mir.insns[90].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[164], arguments) ||
        arguments[0] != mir.insns[150].dst ||
        arguments[1] != mir.insns[158].dst ||
        arguments[2] != mir.insns[162].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[180], arguments) ||
        arguments[0] != mir.insns[165].dst ||
        arguments[1] != mir.insns[173].dst ||
        arguments[2] != mir.insns[178].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[214], arguments) ||
        arguments[0] != mir.insns[208].dst ||
        arguments[1] != mir.insns[210].dst ||
        !mir_machine_two_call_arguments(
            &mir.insns[400], arguments) ||
        arguments[0] != mir.insns[396].dst ||
        arguments[1] != mir.insns[398].dst ||
        mir.insns[89].src1 != mir.insns[88].dst ||
        mir.insns[121].src1 != mir.insns[119].dst ||
        mir.insns[133].src1 != mir.insns[131].dst ||
        mir.insns[149].src1 != mir.insns[148].dst ||
        mir.insns[191].src1 != mir.insns[189].dst ||
        mir.insns[197].src1 != mir.insns[196].dst ||
        mir.insns[205].src1 != mir.insns[204].dst)
        return 0;
    if (mir.insns[154].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[156].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[157].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[172].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[154].immediate <= 0 ||
        mir.insns[156].immediate <= 0 ||
        mir.insns[154].immediate !=
            mir.insns[169].immediate ||
        mir.insns[156].immediate !=
            mir.insns[171].immediate ||
        mir.insns[157].immediate < 0 ||
        mir.insns[172].immediate < 0)
        return 0;
    plan->size2.row_stride = (int)mir.insns[154].immediate;
    plan->size2.column_stride = (int)mir.insns[156].immediate;
    plan->size2.first_member_offset =
        (int)mir.insns[157].immediate;
    plan->size2.second_member_offset =
        (int)mir.insns[172].immediate;
    if (plan->size2.row_stride !=
            plan->size2.column_stride *
            plan->size2.column_count ||
        !mir_machine_constant_value(
            mir.insns[161].dst, &constant, 0))
        return 0;
    plan->size2.first_addend = (unsigned long)constant;
    if (!mir_machine_constant_value(
            mir.insns[176].dst, &constant, 0))
        return 0;
    plan->size2.second_addend = (unsigned long)constant;
    if (!mir_machine_three_call_arguments(
            &mir.insns[164], arguments) ||
        (string = mir_definition(arguments[0])) == NULL ||
        string->opcode != MIR_STRING_ADDRESS)
        return 0;
    plan->size2.strings[2] = (int)string->immediate;
    if (!mir_machine_three_call_arguments(
            &mir.insns[180], arguments) ||
        (string = mir_definition(arguments[0])) == NULL ||
        string->opcode != MIR_STRING_ADDRESS)
        return 0;
    plan->size2.strings[3] = (int)string->immediate;
    if (!mir_machine_three_call_arguments(
            &mir.insns[214], arguments) ||
        !mir_machine_constant_value(arguments[2], &constant, 0))
        return 0;
    string = mir_definition(arguments[0]);
    if (string == NULL || string->opcode != MIR_STRING_ADDRESS)
        return 0;
    plan->size2.strings[4] = (int)string->immediate;
    plan->size2.sum_expected = (unsigned long)constant;
    for (check = 0; check < 17; ++check) {
        if (!mir_aggregate_match_check(
                check_indices[check],
                &plan->size2.checks[check],
                &plan->check_word_function,
                &plan->check_wide_function))
            return 0;
    }
    string = mir_definition(mir.insns[397].src1);
    if (string == NULL || string->opcode != MIR_STRING_ADDRESS ||
        mir.insns[398].opcode != MIR_LOAD ||
        (mir.insns[398].memory_flags & (1 | 8)) != 0 ||
        type_size(mir.insns[398].type) != 4 ||
        !mir_scalar_memory_location(
            &mir.insns[398], &type, &storage, &offset) ||
        storage != SC_LOCAL ||
        offset != plan->size2.sum_offset)
        return 0;
    plan->summary_string_id = (int)string->immediate;
    snprintf(plan->heading_call_name,
             sizeof(plan->heading_call_name), "%s",
             mir.insns[92].base_name[0] != 0
                 ? mir.insns[92].base_name
                 : asm_name_for(sym_asm_name(plan->print_function)));
    snprintf(plan->summary_call_name,
             sizeof(plan->summary_call_name), "%s",
             mir.insns[400].base_name[0] != 0
                 ? mir.insns[400].base_name
                 : asm_name_for(sym_asm_name(plan->print_function)));
    plan->size2.check_count = 17;
    plan->kind = 2;
    return 1;
}

static int mir_match_aggregate_multidim_checks(
    struct MirAggregateMultidimChecks *plan)
{
    if (mir_match_multidim_aggregate_checks(plan) ||
        mir_match_size2_aggregate_checks(plan))
        return 1;
    return 0;
}

static void mir_aggregate_cleanup(FILE *out, int words)
{
    while (words-- > 0)
        fputs("\tpop bc\n", out);
}

static void mir_aggregate_emit_format_call(
    FILE *out, struct Sym *function, const char *call_name)
{
    if (!strcmp(call_name,
                asm_name_for(sym_asm_name(function))))
        mir_machine_emit_symbol_call(out, function);
    else
        fprintf(out, "\tcall %s\n", call_name);
}

static void mir_aggregate_emit_ix_store(
    FILE *out, int offset, int width, unsigned long value)
{
    if (width == 1) {
        fprintf(out, "\tld (ix%+d),%lu\n",
                offset, value & 0xffUL);
    } else if (width == 2) {
        fprintf(out,
                "\tld (ix%+d),%lu\n"
                "\tld (ix%+d),%lu\n",
                offset, value & 0xffUL,
                offset + 1, (value >> 8) & 0xffUL);
    } else {
        mir_machine_emit_float_bits(out, value);
        mir_machine_emit_ix_wide_store(out, offset);
    }
}

static void mir_aggregate_emit_value(
    FILE *out, const struct MirAggregateCheckValue *value)
{
    if (value->kind == MIR_AGGREGATE_CHECK_CONSTANT) {
        if (value->width == 4)
            mir_machine_emit_float_bits(out, value->value);
        else
            fprintf(out, "\tld hl,%lu\n",
                    value->value & 0xffffUL);
        return;
    }
    if (value->kind == MIR_AGGREGATE_CHECK_LOCAL) {
        if (value->width == 4) {
            mir_machine_emit_ix_wide_load(
                out, value->local_offset);
        } else if (value->width == 2) {
            fprintf(out,
                    "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
                    value->local_offset,
                    value->local_offset + 1);
        } else {
            fprintf(out, "\tld l,(ix%+d)\n",
                    value->local_offset);
            if (value->is_unsigned)
                fputs("\tld h,0\n", out);
            else
                fputs("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n",
                      out);
        }
        return;
    }
    mir_machine_emit_global_address_hl(
        out, value->root, value->root_offset);
    if (value->width == 4) {
        fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
              "\tinc hl\n\tld a,(hl)\n\tinc hl\n"
              "\tld h,(hl)\n\tld l,a\n\tex de,hl\n", out);
    } else if (value->width == 2) {
        fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
              "\tex de,hl\n", out);
    } else {
        fputs("\tld l,(hl)\n", out);
        if (value->is_unsigned)
            fputs("\tld h,0\n", out);
        else
            fputs("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n",
                  out);
    }
}

static void mir_aggregate_emit_check(
    FILE *out, const struct MirAggregateCheck *check)
{
    if (check->width == 4) {
        mir_machine_emit_float_bits(out, check->expected);
        fputs("\tpush de\n\tpush hl\n", out);
        mir_aggregate_emit_value(out, &check->actual);
        fputs("\tpush de\n\tpush hl\n", out);
    } else {
        fprintf(out, "\tld hl,%lu\n\tpush hl\n",
                check->expected & 0xffffUL);
        mir_aggregate_emit_value(out, &check->actual);
        fputs("\tpush hl\n", out);
    }
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            check->string_id);
    mir_machine_emit_symbol_call(out, check->function);
    mir_aggregate_cleanup(out, check->width == 4 ? 5 : 3);
}

static void mir_aggregate_emit_wide_check(
    FILE *out, struct Sym *function, int string_id,
    unsigned long expected, int local_offset)
{
    mir_machine_emit_float_bits(out, expected);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, local_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_machine_emit_symbol_call(out, function);
    mir_aggregate_cleanup(out, 5);
}

static void mir_aggregate_emit_word_check(
    FILE *out, struct Sym *function, int string_id,
    unsigned long expected)
{
    fprintf(out, "\tld hl,%lu\n\tpush hl\n",
            expected & 0xffffUL);
    fputs("\tpush de\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_machine_emit_symbol_call(out, function);
    mir_aggregate_cleanup(out, 3);
}

static void mir_aggregate_scale_hl(FILE *out, int factor)
{
    int add;

    if (factor <= 1)
        return;
    if (factor == 2) {
        fputs("\tadd hl,hl\n", out);
        return;
    }
    fputs("\tld d,h\n\tld e,l\n", out);
    if (factor == 3) {
        fputs("\tadd hl,hl\n\tadd hl,de\n", out);
        return;
    }
    if (factor == 4) {
        fputs("\tadd hl,hl\n\tadd hl,hl\n", out);
        return;
    }
    if (factor == 6) {
        fputs("\tadd hl,hl\n\tadd hl,de\n\tadd hl,hl\n", out);
        return;
    }
    if (factor == 12) {
        fputs("\tadd hl,hl\n\tadd hl,de\n"
              "\tadd hl,hl\n\tadd hl,hl\n", out);
        return;
    }
    for (add = 1; add < factor; ++add)
        fputs("\tadd hl,de\n", out);
}

static void mir_emit_multidim_aggregate_checks(
    FILE *out, const struct MirAggregateMultidimChecks *plan)
{
    const int loop_row = label_id++;
    const int loop_column = label_id++;
    const int column_incremented = label_id++;
    const int next_row = label_id++;
    const int done = label_id++;
    int cells_offset =
        plan->multidim.cells_index *
            plan->multidim.cells_stride +
        plan->multidim.cells_tag_offset;

    fprintf(out,
            "%s\n"
            "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n",
            MIR_EXACT_KERNEL_MARKER, mir.local_bytes);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->heading_string_id);
    mir_aggregate_emit_format_call(
        out, plan->print_function, plan->heading_call_name);
    fputs("\tpop bc\n", out);

    mir_machine_emit_global_address_hl(
        out, plan->multidim.board_root, 0);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(
        out, plan->multidim.board_fill_function);
    fputs("\tpop bc\n", out);

    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->multidim.rows);
    mir_machine_emit_global_address_hl(
        out, plan->multidim.board_root, 0);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(
        out, plan->multidim.board_weight_function);
    fputs("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(
        out, plan->multidim.w_ptr_offset);
    mir_aggregate_emit_wide_check(
        out, plan->check_wide_function,
        plan->multidim.strings[0],
        plan->multidim.weight_expected,
        plan->multidim.w_ptr_offset);

    mir_aggregate_emit_ix_store(
        out, plan->multidim.w_struct_offset, 4, 0);
    fprintf(out, "\tld (ix%+d),0\nL%d:\n"
            "\tld (ix%+d),0\n\tld (ix%+d),0\nL%d:\n",
            plan->multidim.row_offset, loop_row,
            plan->multidim.column_offset,
            plan->multidim.column_offset + 1,
            loop_column);
    mir_machine_emit_global_address_hl(
        out, plan->multidim.board_root, 0);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld l,(ix%+d)\n\tld h,0\n",
            plan->multidim.row_offset);
    mir_aggregate_scale_hl(
        out, plan->multidim.board_row_stride);
    fputs("\tex de,hl\n\tpop hl\n\tadd hl,de\n\tpush hl\n",
          out);
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            plan->multidim.column_offset,
            plan->multidim.column_offset + 1);
    mir_aggregate_scale_hl(
        out, plan->multidim.board_column_stride);
    fputs("\tex de,hl\n\tpop hl\n\tadd hl,de\n", out);
    mir_machine_emit_hl_offset(
        out, plan->multidim.board_weight_offset, 0);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n\tld a,h\n\trlca\n\tsbc a,a\n"
          "\tld d,a\n\tld e,a\n\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(
        out, plan->multidim.w_struct_offset);
    fputs("\tpop bc\n\tadd hl,bc\n\tex de,hl\n"
          "\tpop bc\n\tadc hl,bc\n\tex de,hl\n", out);
    mir_machine_emit_ix_wide_store(
        out, plan->multidim.w_struct_offset);
    fprintf(out,
            "\tinc (ix%+d)\n\tjp nz,L%d\n"
            "\tinc (ix%+d)\nL%d:\n"
            "\tld a,(ix%+d)\n\tor a\n\tjp nz,L%d\n"
            "\tld a,(ix%+d)\n\tcp %d\n\tjp c,L%d\n"
            "L%d:\n\tinc (ix%+d)\n"
            "\tld a,(ix%+d)\n\tcp %d\n\tjp c,L%d\n"
            "\tjp L%d\n",
            plan->multidim.column_offset, column_incremented,
            plan->multidim.column_offset + 1, column_incremented,
            plan->multidim.column_offset + 1, next_row,
            plan->multidim.column_offset,
            plan->multidim.columns, loop_column,
            next_row, plan->multidim.row_offset,
            plan->multidim.row_offset,
            plan->multidim.rows, loop_row, done);
    fprintf(out, "L%d:\n", done);
    mir_aggregate_emit_wide_check(
        out, plan->check_wide_function,
        plan->multidim.strings[1],
        plan->multidim.weight_expected,
        plan->multidim.w_struct_offset);

    fprintf(out,
            "\tld hl,3\n\tpush hl\n"
            "\tld hl,2\n\tpush hl\n");
    mir_machine_emit_global_address_hl(
        out, plan->multidim.board_root, 0);
    fputs("\tpush hl\n", out);
    mir_machine_emit_symbol_call(
        out, plan->multidim.tile_function);
    mir_aggregate_cleanup(out, 3);
    fprintf(out,
            "\tld (ix%+d),l\n\tld (ix%+d),h\n",
            plan->multidim.tile_offset,
            plan->multidim.tile_offset + 1);
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            plan->multidim.tile_offset,
            plan->multidim.tile_offset + 1);
    mir_machine_emit_hl_offset(
        out, plan->multidim.board_weight_offset, 0);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n", out);
    mir_aggregate_emit_word_check(
        out, plan->check_word_function,
        plan->multidim.strings[2],
        plan->multidim.tile_weight_expected);
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld l,(hl)\n\tld a,l\n\trlca\n"
            "\tsbc a,a\n\tld h,a\n\tex de,hl\n",
            plan->multidim.tile_offset,
            plan->multidim.tile_offset + 1);
    mir_aggregate_emit_word_check(
        out, plan->check_word_function,
        plan->multidim.strings[3],
        plan->multidim.tile_char_expected);

    mir_machine_emit_symbol_call(
        out, plan->multidim.cells_fill_function);
    mir_machine_emit_float_bits(
        out, plan->multidim.checksum_expected);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(
        out, plan->multidim.cells_checksum_function);
    fputs("\tpush de\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->multidim.strings[4]);
    mir_machine_emit_symbol_call(
        out, plan->check_wide_function);
    mir_aggregate_cleanup(out, 5);

    mir_machine_emit_global_address_hl(
        out, plan->multidim.cells_root, cells_offset);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n", out);
    mir_aggregate_emit_word_check(
        out, plan->check_word_function,
        plan->multidim.strings[5],
        plan->multidim.tag_expected);

    mir_machine_emit_symbol_call(
        out, plan->multidim.cells_checksum_function);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(
        out, plan->multidim.w_ptr_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->summary_string_id);
    mir_aggregate_emit_format_call(
        out, plan->print_function, plan->summary_call_name);
    mir_aggregate_cleanup(out, 5);
    fputs("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static void mir_size2_emit_element_address(
    FILE *out, const struct MirAggregateMultidimChecks *plan)
{
    fputs("\tpush ix\n\tpop hl\n", out);
    mir_machine_emit_hl_offset(
        out, plan->size2.lg_offset, 0);
    fputs("\tpush hl\n", out);
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            plan->size2.row_offset,
            plan->size2.row_offset + 1);
    mir_aggregate_scale_hl(out, plan->size2.row_stride);
    fputs("\tex de,hl\n\tpop hl\n\tadd hl,de\n\tpush hl\n",
          out);
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            plan->size2.column_offset,
            plan->size2.column_offset + 1);
    mir_aggregate_scale_hl(out, plan->size2.column_stride);
    fputs("\tex de,hl\n\tpop hl\n\tadd hl,de\n", out);
}

static void mir_emit_size2_aggregate_checks(
    FILE *out, const struct MirAggregateMultidimChecks *plan)
{
    const int row_loop = label_id++;
    const int column_loop = label_id++;
    const int column_incremented = label_id++;
    const int next_row = label_id++;
    const int row_incremented = label_id++;
    const int after_loop = label_id++;
    int store;
    int check;

    fprintf(out,
            "%s\n"
            "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n",
            MIR_EXACT_KERNEL_MARKER, mir.local_bytes);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (store = 0; store < 43; ++store)
        mir_aggregate_emit_ix_store(
            out, plan->size2.stores[store].offset,
            plan->size2.stores[store].width,
            plan->size2.stores[store].value);
    mir_aggregate_emit_ix_store(
        out, plan->size2.sum_offset, 4, 0);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->heading_string_id);
    mir_aggregate_emit_format_call(
        out, plan->print_function, plan->heading_call_name);
    fputs("\tpop bc\n", out);

    fprintf(out,
            "\tld hl,%d\n\tpush hl\n"
            "\tld hl,%d\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->size2.row_count, plan->size2.row_count,
            plan->size2.strings[0]);
    mir_machine_emit_symbol_call(
        out, plan->check_word_function);
    mir_aggregate_cleanup(out, 3);
    fprintf(out,
            "\tld hl,%d\n\tpush hl\n"
            "\tld hl,%d\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->size2.row_count * plan->size2.column_count,
            plan->size2.row_count * plan->size2.column_count,
            plan->size2.strings[1]);
    mir_machine_emit_symbol_call(
        out, plan->check_word_function);
    mir_aggregate_cleanup(out, 3);

    mir_aggregate_emit_ix_store(
        out, plan->size2.row_offset, 2, 0);
    fprintf(out, "L%d:\n", row_loop);
    mir_aggregate_emit_ix_store(
        out, plan->size2.column_offset, 2, 0);
    fprintf(out, "L%d:\n", column_loop);
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            plan->size2.row_offset,
            plan->size2.row_offset + 1);
    mir_aggregate_scale_hl(out, plan->size2.column_count);
    fputs("\tpush hl\n", out);
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tex de,hl\n\tpop hl\n\tadd hl,de\n"
            "\tld (ix%+d),l\n\tld (ix%+d),h\n",
            plan->size2.column_offset,
            plan->size2.column_offset + 1,
            plan->size2.index_offset,
            plan->size2.index_offset + 1);

    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld de,%lu\n\tadd hl,de\n\tpush hl\n",
            plan->size2.index_offset,
            plan->size2.index_offset + 1,
            plan->size2.first_addend & 0xffffUL);
    mir_size2_emit_element_address(out, plan);
    mir_machine_emit_hl_offset(
        out, plan->size2.first_member_offset, 0);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->size2.strings[2]);
    mir_machine_emit_symbol_call(
        out, plan->check_word_function);
    mir_aggregate_cleanup(out, 3);

    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld de,%lu\n\tadd hl,de\n"
            "\tld a,h\n\trlca\n\tsbc a,a\n"
            "\tld d,a\n\tld e,a\n"
            "\tpush de\n\tpush hl\n",
            plan->size2.index_offset,
            plan->size2.index_offset + 1,
            plan->size2.second_addend & 0xffffUL);
    mir_size2_emit_element_address(out, plan);
    mir_machine_emit_hl_offset(
        out, plan->size2.second_member_offset, 0);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tinc hl\n\tld a,(hl)\n\tinc hl\n"
          "\tld h,(hl)\n\tld l,a\n\tex de,hl\n"
          "\tpush de\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->size2.strings[3]);
    mir_machine_emit_symbol_call(
        out, plan->check_wide_function);
    mir_aggregate_cleanup(out, 5);

    mir_size2_emit_element_address(out, plan);
    mir_machine_emit_hl_offset(
        out, plan->size2.second_member_offset, 0);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tinc hl\n\tld a,(hl)\n\tinc hl\n"
          "\tld h,(hl)\n\tld l,a\n\tex de,hl\n"
          "\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, plan->size2.sum_offset);
    fputs("\tpop bc\n\tadd hl,bc\n\tex de,hl\n"
          "\tpop bc\n\tadc hl,bc\n\tex de,hl\n", out);
    mir_machine_emit_ix_wide_store(out, plan->size2.sum_offset);

    fprintf(out,
            "\tinc (ix%+d)\n\tjp nz,L%d\n"
            "\tinc (ix%+d)\nL%d:\n"
            "\tld a,(ix%+d)\n\tor a\n\tjp nz,L%d\n"
            "\tld a,(ix%+d)\n\tcp %d\n\tjp c,L%d\n",
            plan->size2.column_offset, column_incremented,
            plan->size2.column_offset + 1, column_incremented,
            plan->size2.column_offset + 1, next_row,
            plan->size2.column_offset,
            plan->size2.column_count, column_loop);
    fprintf(out,
            "L%d:\n\tinc (ix%+d)\n\tjp nz,L%d\n"
            "\tinc (ix%+d)\nL%d:\n"
            "\tld a,(ix%+d)\n\tor a\n\tjp nz,L%d\n"
            "\tld a,(ix%+d)\n\tcp %d\n\tjp c,L%d\n",
            next_row, plan->size2.row_offset, row_incremented,
            plan->size2.row_offset + 1, row_incremented,
            plan->size2.row_offset + 1, after_loop,
            plan->size2.row_offset,
            plan->size2.row_count, row_loop);

    fprintf(out, "L%d:\n", after_loop);
    mir_aggregate_emit_wide_check(
        out, plan->check_wide_function, plan->size2.strings[4],
        plan->size2.sum_expected, plan->size2.sum_offset);
    for (check = 0; check < plan->size2.check_count; ++check)
        mir_aggregate_emit_check(out, &plan->size2.checks[check]);
    mir_machine_emit_ix_wide_load(out, plan->size2.sum_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->summary_string_id);
    mir_aggregate_emit_format_call(
        out, plan->print_function, plan->summary_call_name);
    mir_aggregate_cleanup(out, 3);
    fputs("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static void mir_emit_aggregate_multidim_checks(
    FILE *out, const struct MirAggregateMultidimChecks *plan)
{
    if (plan->kind == 1)
        mir_emit_multidim_aggregate_checks(out, plan);
    else
        mir_emit_size2_aggregate_checks(out, plan);
}

int mir_try_emit_aggregate_checks(FILE *out)
{
    struct MirAggregateMultidimChecks plan;

    if (!mir_match_aggregate_multidim_checks(&plan))
        return -1;
    mir_emit_aggregate_multidim_checks(out, &plan);
    return 1;
}
