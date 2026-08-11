/* dcc_mir_machine_emit.c - Z80 emission from scheduled MIR templates. */

#include "dcc_mir_internal.h"

struct MirIndexedWordSum {
    int parameter_stack_offset;
    int left_offset;
    int right_offset;
};

struct MirNestedRowStore {
    struct Sym *root;
    int root_pointer_offset;
    int row_stride;
    int value_member_offset;
    int count_member_offset;
    int element_stride;
    int index_stack_offset;
    int value_stack_offset;
};

struct MirFlatArrayChecks {
    struct Sym *check_function;
    int parameter_stack_offset;
    int width;
    int is_unsigned;
    int count;
    int offsets[16];
    int strings[16];
};

enum MirFixedMutationKind {
    MIR_FIXED_MUTATION_SET = 1,
    MIR_FIXED_MUTATION_ADD = 2
};

struct MirFixedMutation {
    int kind;
    int offset;
    int width;
    unsigned long value;
};

struct MirFixedParamMutations {
    int parameter_stack_offset;
    int count;
    struct MirFixedMutation mutations[8];
};

struct MirGlobalAppendStore {
    int parameter_stack_offset;
    int field_offset;
    int width;
};

struct MirGlobalAppend {
    struct Sym *root;
    int array_offset;
    int count_offset;
    int element_stride;
    int store_count;
    struct MirGlobalAppendStore stores[8];
};

struct MirNestedAppendStore {
    int parameter_stack_offset;
    int array_member_offset;
    int element_stride;
    int width;
};

struct MirNestedAppend {
    struct Sym *root;
    int root_pointer_offset;
    int row_stride;
    int row_index_value;
    int row_index_stack_offset;
    int count_member_offset;
    int store_count;
    struct MirNestedAppendStore stores[8];
};

enum MirIndexedStackKind {
    MIR_INDEXED_STACK_PUSH = 1,
    MIR_INDEXED_STACK_POP = 2
};

struct MirIndexedStack {
    int kind;
    struct Sym *root;
    int root_offset;
    int base_member_offset;
    int index_member_offset;
    int element_width;
    int value_stack_offset;
};

enum MirPointerStackKind {
    MIR_POINTER_STACK_PUSH = 1,
    MIR_POINTER_STACK_POP = 2
};

struct MirPointerStack {
    int kind;
    struct Sym *root;
    int root_offset;
    int pointer_member_offset;
    int value_stack_offset;
};

static void mir_machine_emit_hl_offset(
    FILE *out, int offset, int preserve_bc);

static int mir_machine_reject(const char *template_name, const char *reason)
{
    if (getenv("DCC_MIR_MACHINE_REPORT") != NULL)
        fprintf(stderr,
                "; MIR machine function=%s template=%s reject=%s\n",
                mir.name, template_name, reason);
    return 0;
}

enum MirMachineFormKind {
    MIR_MACHINE_FORM_INTEGER = 1,
    MIR_MACHINE_FORM_POINTER = 2
};

struct MirMachineForm {
    int kind;
    long value;
    int storage;
    int offset;
    int pointer_terms;
};

static int mir_machine_same_location(
    const struct MirInsn *left, const struct MirInsn *right)
{
    int left_type, left_storage, left_offset;
    int right_type, right_storage, right_offset;

    return mir_scalar_memory_location(
               left, &left_type, &left_storage, &left_offset) &&
           mir_scalar_memory_location(
               right, &right_type, &right_storage, &right_offset) &&
           left_storage == right_storage &&
           left_offset == right_offset &&
           type_size(left_type) == type_size(right_type);
}

static const struct MirInsn *mir_machine_resolve_local_alias(int value)
{
    const struct MirInsn *definition = mir_definition(value);
    int definition_index;
    int instruction;
    const struct MirInsn *stored = NULL;

    if (definition == NULL || definition->opcode != MIR_LOAD)
        return definition;
    definition_index = (int)(definition - mir.insns);
    for (instruction = 0; instruction < definition_index; ++instruction) {
        const struct MirInsn *candidate = &mir.insns[instruction];

        if (candidate->opcode == MIR_ADDRESS &&
            !strcmp(candidate->name, definition->name))
            return NULL;
        if (candidate->opcode == MIR_STORE &&
            mir_machine_same_location(candidate, definition))
            stored = candidate;
    }
    return stored != NULL ? mir_definition(stored->src1) : NULL;
}

static int mir_machine_pointer_form(
    int value, int before, struct MirMachineForm *form, int depth)
{
    const struct MirInsn *definition;
    int definition_index;

    if (depth > 32)
        return 0;
    definition = mir_definition(value);
    if (definition == NULL)
        return 0;
    definition_index = (int)(definition - mir.insns);
    if (definition_index >= before)
        return 0;
    if (definition->opcode == MIR_CONST) {
        form->kind = MIR_MACHINE_FORM_INTEGER;
        form->value = definition->immediate;
        form->storage = 0;
        form->offset = 0;
        form->pointer_terms = 0;
        return 1;
    }
    if (definition->opcode == MIR_ADDRESS) {
        int memory_type;
        int memory_storage;
        int memory_offset;

        if (!mir_scalar_memory_location(
                definition, &memory_type, &memory_storage,
                &memory_offset) ||
            (memory_storage != SC_LOCAL &&
             memory_storage != SC_GLOBAL) ||
            mir_declared_is_vla_object(definition->name))
            return 0;
        form->kind = MIR_MACHINE_FORM_POINTER;
        form->value = 0;
        form->storage = memory_storage;
        form->offset = memory_offset;
        form->pointer_terms = 1;
        return 1;
    }
    if (definition->opcode == MIR_LOAD) {
        int instruction;
        const struct MirInsn *stored = NULL;

        for (instruction = 0; instruction < definition_index; ++instruction) {
            const struct MirInsn *candidate = &mir.insns[instruction];

            if (candidate->opcode == MIR_CALL ||
                candidate->opcode == MIR_CALL_AGGREGATE ||
                candidate->opcode == MIR_STORE_INDIRECT)
                return 0;
            if (candidate->opcode == MIR_STORE &&
                mir_machine_same_location(candidate, definition))
                stored = candidate;
        }
        return stored != NULL &&
               mir_machine_pointer_form(
                   stored->src1, (int)(stored - mir.insns),
                   form, depth + 1);
    }
    if (definition->opcode == MIR_UNARY &&
        definition->immediate == 0)
        return mir_machine_pointer_form(
            definition->src1, definition_index, form, depth + 1);
    if (definition->opcode == MIR_BINARY &&
        (definition->immediate == '+' ||
         definition->immediate == '-')) {
        struct MirMachineForm left;
        struct MirMachineForm right;

        if (!mir_machine_pointer_form(
                definition->src1, definition_index,
                &left, depth + 1) ||
            !mir_machine_pointer_form(
                definition->src2, definition_index,
                &right, depth + 1))
            return 0;
        if (left.kind == MIR_MACHINE_FORM_INTEGER &&
            right.kind == MIR_MACHINE_FORM_INTEGER) {
            form->kind = MIR_MACHINE_FORM_INTEGER;
            form->value = definition->immediate == '+'
                ? left.value + right.value
                : left.value - right.value;
            form->storage = 0;
            form->offset = 0;
            form->pointer_terms =
                left.pointer_terms + right.pointer_terms;
            return 1;
        }
        if (definition->immediate == '+' &&
            left.kind == MIR_MACHINE_FORM_INTEGER &&
            right.kind == MIR_MACHINE_FORM_POINTER) {
            *form = right;
            form->value += left.value;
            form->pointer_terms += left.pointer_terms;
            return 1;
        }
        if (left.kind == MIR_MACHINE_FORM_POINTER &&
            right.kind == MIR_MACHINE_FORM_INTEGER) {
            *form = left;
            form->value += definition->immediate == '+'
                ? right.value : -right.value;
            form->pointer_terms += right.pointer_terms;
            return 1;
        }
        if (definition->immediate == '-' &&
            left.kind == MIR_MACHINE_FORM_POINTER &&
            right.kind == MIR_MACHINE_FORM_POINTER &&
            left.storage == right.storage &&
            left.offset == right.offset) {
            form->kind = MIR_MACHINE_FORM_INTEGER;
            form->value = left.value - right.value;
            form->storage = 0;
            form->offset = 0;
            form->pointer_terms =
                left.pointer_terms + right.pointer_terms;
            return 1;
        }
    }
    return 0;
}

static int mir_match_affine_pointer_constant_return(long *value_out)
{
    const struct MirInsn *return_insn = NULL;
    struct MirMachineForm form;
    int return_count = 0;
    int instruction;

    if (mir.has_vla || mir_cfg_block_count() != 1 ||
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
        case MIR_ADDRESS:
        case MIR_LOAD:
        case MIR_STORE:
        case MIR_UNARY:
        case MIR_BINARY:
            break;
        case MIR_RETURN:
            return_insn = insn;
            ++return_count;
            break;
        default:
            return 0;
        }
    }
    if (return_count != 1 || return_insn == NULL ||
        !mir_machine_pointer_form(
            return_insn->src1, (int)(return_insn - mir.insns),
            &form, 0) ||
        form.kind != MIR_MACHINE_FORM_INTEGER ||
        form.pointer_terms < 2)
        return 0;
    *value_out = form.value & 0xffffL;
    return 1;
}

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

struct MirRowMemberAddress {
    struct Sym *root;
    int root_pointer_offset;
    int row_stride;
    int member_offset;
    int index_value;
};

static int mir_machine_row_member_address(
    int value, struct MirRowMemberAddress *address)
{
    const struct MirInsn *member = mir_definition(value);
    const struct MirInsn *row_index;
    const struct MirInsn *rows_load;
    const struct MirInsn *rows_member;
    const struct MirInsn *root;

    if (member == NULL || member->opcode != MIR_MEMBER_ADDRESS ||
        (member->memory_flags & (1 | 8)) != 0)
        return 0;
    row_index = mir_definition(member->src1);
    if (row_index == NULL || row_index->opcode != MIR_INDEX_ADDRESS ||
        row_index->immediate <= 0 ||
        (row_index->memory_flags & 1) != 0)
        return 0;
    rows_load = mir_definition(row_index->src1);
    if (rows_load == NULL || rows_load->opcode != MIR_LOAD_INDIRECT ||
        rows_load->memory_size != 2 ||
        (rows_load->memory_flags & (1 | 8)) != 0)
        return 0;
    rows_member = mir_definition(rows_load->src1);
    if (rows_member == NULL ||
        rows_member->opcode != MIR_MEMBER_ADDRESS ||
        (rows_member->memory_flags & (1 | 8)) != 0)
        return 0;
    root = mir_machine_resolve_local_alias(rows_member->src1);
    if (root == NULL || root->opcode != MIR_ADDRESS)
        return 0;
    address->root = find_global(root->name);
    if (address->root == NULL || !address->root->is_defined ||
        address->root->is_volatile)
        return 0;
    address->root_pointer_offset = (int)rows_member->immediate;
    address->row_stride = (int)row_index->immediate;
    address->member_offset = (int)member->immediate;
    address->index_value = row_index->src2;
    return 1;
}

static int mir_machine_same_row(
    const struct MirRowMemberAddress *left,
    const struct MirRowMemberAddress *right)
{
    return left->root == right->root &&
           left->root_pointer_offset == right->root_pointer_offset &&
           left->row_stride == right->row_stride &&
           left->index_value == right->index_value;
}

static int mir_machine_parameter_offset(
    int value, int *stack_offset)
{
    const struct MirInsn *parameter = mir_definition(value);
    int memory_type;
    int memory_storage;
    int memory_offset;

    if (parameter == NULL || parameter->opcode != MIR_PARAM ||
        !mir_scalar_memory_location(
            parameter, &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_PARAM || type_size(memory_type) != 2)
        return 0;
    *stack_offset = memory_offset - 2;
    return *stack_offset >= 0;
}

static int mir_machine_parameter_address(
    int value, int *stack_offset, long *offset, int depth)
{
    const struct MirInsn *definition;

    if (depth > 32)
        return 0;
    definition = mir_definition(value);
    if (definition == NULL)
        return 0;
    if (definition->opcode == MIR_LOAD) {
        int memory_type, memory_storage, memory_offset;

        if (!mir_scalar_memory_location(
                definition, &memory_type, &memory_storage,
                &memory_offset))
            return 0;
        if (memory_storage == SC_PARAM &&
            type_ptr_depth(memory_type) > 0 &&
            type_size(memory_type) == 2) {
            *stack_offset = memory_offset - 2;
            *offset = 0;
            return *stack_offset >= 0;
        }
        definition = mir_machine_resolve_local_alias(value);
        if (definition == NULL)
            return 0;
        value = definition->dst;
    }
    if (definition->opcode == MIR_PARAM) {
        if (!mir_machine_parameter_offset(value, stack_offset))
            return 0;
        *offset = 0;
        return 1;
    }
    if (definition->opcode == MIR_UNARY &&
        definition->immediate == 0)
        return mir_machine_parameter_address(
            definition->src1, stack_offset, offset, depth + 1);
    if (definition->opcode == MIR_INDEX_ADDRESS) {
        const struct MirInsn *constant = mir_definition(definition->src2);
        long base_offset;

        if (constant == NULL || constant->opcode != MIR_CONST ||
            definition->immediate <= 0 ||
            !mir_machine_parameter_address(
                definition->src1, stack_offset,
                &base_offset, depth + 1))
            return 0;
        *offset = base_offset +
            constant->immediate * definition->immediate;
        return *offset >= -32768 && *offset <= 32767;
    }
    if (definition->opcode == MIR_MEMBER_ADDRESS) {
        long base_offset;

        if (!mir_machine_parameter_address(
                definition->src1, stack_offset,
                &base_offset, depth + 1))
            return 0;
        *offset = base_offset + definition->immediate;
        return *offset >= -32768 && *offset <= 32767;
    }
    return 0;
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

static int mir_machine_flat_load(
    int value, int *stack_offset, long *offset,
    int *width, int *is_unsigned)
{
    const struct MirInsn *load = mir_definition(value);

    if (load != NULL && load->opcode == MIR_UNARY &&
        load->immediate == 0)
        load = mir_definition(load->src1);
    if (load == NULL || load->opcode != MIR_LOAD_INDIRECT ||
        (load->memory_size != 1 && load->memory_size != 2 &&
         load->memory_size != 4) ||
        (load->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_parameter_address(
            load->src1, stack_offset, offset, 0))
        return 0;
    *width = load->memory_size;
    *is_unsigned = (load->type & TYPE_UNSIGNED) != 0;
    return 1;
}

static int mir_match_flat_array_checks(struct MirFlatArrayChecks *plan)
{
    int instruction;
    int call_count = 0;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
        case MIR_PARAM:
        case MIR_CONST:
        case MIR_STRING_ADDRESS:
        case MIR_LOAD:
        case MIR_UNARY:
        case MIR_ARG:
        case MIR_INDEX_ADDRESS:
        case MIR_MEMBER_ADDRESS:
        case MIR_LOAD_INDIRECT:
            if (insn->opcode == MIR_LOAD &&
                (insn->memory_flags & 1) != 0)
                return 0;
            break;
        case MIR_STORE:
            {
                int memory_type, memory_storage, memory_offset;
                int parameter_stack_offset;
                long pointer_offset;

                if (!mir_scalar_memory_location(
                        insn, &memory_type, &memory_storage,
                        &memory_offset) ||
                    memory_storage != SC_LOCAL ||
                    type_ptr_depth(memory_type) == 0 ||
                    !mir_machine_parameter_address(
                        insn->src1, &parameter_stack_offset,
                        &pointer_offset, 0))
                    return 0;
                (void)parameter_stack_offset;
                (void)pointer_offset;
            }
            break;
        case MIR_CALL:
            {
                const struct MirInsn *string;
                int arguments[3];
                int actual_stack, expected_stack;
                int actual_width, expected_width;
                int actual_unsigned, expected_unsigned;
                long actual_offset, expected_offset;

                if (call_count >= 16 ||
                    !mir_machine_three_call_arguments(insn, arguments))
                    return 0;
                string = mir_definition(arguments[0]);
                if (string == NULL ||
                    string->opcode != MIR_STRING_ADDRESS ||
                    !mir_machine_flat_load(
                        arguments[1], &actual_stack, &actual_offset,
                        &actual_width, &actual_unsigned) ||
                    !mir_machine_flat_load(
                        arguments[2], &expected_stack, &expected_offset,
                        &expected_width, &expected_unsigned) ||
                    actual_stack != expected_stack ||
                    actual_offset != expected_offset ||
                    actual_width != expected_width ||
                    actual_unsigned != expected_unsigned ||
                    (actual_width < 4 &&
                     (actual_offset < -128 ||
                      actual_offset + actual_width - 1 > 127)))
                    return 0;
                if (call_count == 0) {
                    plan->check_function = find_global(insn->name);
                    plan->parameter_stack_offset = actual_stack;
                    plan->width = actual_width;
                    plan->is_unsigned = actual_unsigned;
                    if (plan->check_function == NULL ||
                        !plan->check_function->is_defined)
                        return 0;
                } else if (plan->check_function != find_global(insn->name) ||
                           plan->parameter_stack_offset != actual_stack ||
                           plan->width != actual_width ||
                           plan->is_unsigned != actual_unsigned) {
                    return 0;
                }
                plan->offsets[call_count] = (int)actual_offset;
                plan->strings[call_count] = (int)string->immediate;
                ++call_count;
            }
            break;
        default:
            return 0;
        }
    }
    plan->count = call_count;
    return call_count >= 2;
}

static void mir_machine_emit_symbol_call(
    FILE *out, struct Sym *symbol)
{
    const char *name = asm_name_for(sym_asm_name(symbol));

    if ((symbol->storage == SC_EXTERN || symbol->needs_extrn) &&
        mir_extrn_should_emit(symbol))
        fprintf(out, "\textrn %s\n", name);
    fprintf(out, "\tcall %s\n", name);
}

static void mir_emit_flat_array_checks(
    FILE *out, const struct MirFlatArrayChecks *plan)
{
    int check;

    fprintf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tpush de\n\tpop iy\n",
            plan->parameter_stack_offset + 2);
    for (check = 0; check < plan->count; ++check) {
        if (plan->width == 2) {
            fprintf(out,
                    "\tld l,(iy%+d)\n\tld h,(iy%+d)\n",
                    plan->offsets[check],
                    plan->offsets[check] + 1);
        } else if (plan->width == 4) {
            if (plan->offsets[check] >= -128 &&
                plan->offsets[check] + 3 <= 127) {
                fprintf(out,
                        "\tld l,(iy%+d)\n\tld h,(iy%+d)\n"
                        "\tld e,(iy%+d)\n\tld d,(iy%+d)\n",
                        plan->offsets[check],
                        plan->offsets[check] + 1,
                        plan->offsets[check] + 2,
                        plan->offsets[check] + 3);
            } else {
                fputs("\tpush iy\n\tpop hl\n", out);
                fprintf(out,
                        "\tld de,%d\n\tadd hl,de\n"
                        "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
                        "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
                        "\tld l,c\n\tld h,b\n",
                        plan->offsets[check]);
            }
        } else if (plan->is_unsigned) {
            fprintf(out,
                    "\tld l,(iy%+d)\n\tld h,0\n",
                    plan->offsets[check]);
        } else {
            fprintf(out,
                    "\tld l,(iy%+d)\n"
                    "\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n",
                    plan->offsets[check]);
        }
        if (plan->width == 4)
            fputs("\tpush de\n\tpush hl\n\tpush de\n\tpush hl\n", out);
        else
            fputs("\tpush hl\n\tpush hl\n", out);
        fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->strings[check]);
        mir_machine_emit_symbol_call(out, plan->check_function);
        fputs("\tpop bc\n\tpop bc\n\tpop bc\n", out);
        if (plan->width == 4)
            fputs("\tpop bc\n\tpop bc\n", out);
    }
    fputs("\tpop iy\n\tret\n", out);
}

static int mir_machine_constant_bits(
    int value, int width, unsigned long *bits)
{
    const struct MirInsn *constant = mir_definition(value);

    if (constant == NULL ||
        (constant->opcode != MIR_CONST &&
         constant->opcode != MIR_FLOAT_CONST) ||
        type_size(constant->type) != width)
        return 0;
    *bits = (unsigned long)constant->immediate;
    if (width == 1)
        *bits &= 0xffUL;
    else if (width == 2)
        *bits &= 0xffffUL;
    return 1;
}

static int mir_machine_match_fixed_mutation(
    const struct MirInsn *store, struct MirFixedMutation *mutation,
    int *parameter_stack_offset)
{
    const struct MirInsn *value;
    int stack_offset;
    long offset;
    unsigned long bits;

    if (store == NULL || store->opcode != MIR_STORE_INDIRECT ||
        (store->memory_size != 1 && store->memory_size != 2 &&
         store->memory_size != 4) ||
        store->bit_width != 0 ||
        (store->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_parameter_address(
            store->src1, &stack_offset, &offset, 0) ||
        offset < -32768 || offset > 32767)
        return 0;
    if (*parameter_stack_offset < 0)
        *parameter_stack_offset = stack_offset;
    else if (*parameter_stack_offset != stack_offset)
        return 0;
    value = mir_definition(store->src2);
    if (mir_machine_constant_bits(
            store->src2, store->memory_size, &bits)) {
        mutation->kind = MIR_FIXED_MUTATION_SET;
    } else if (value != NULL && value->opcode == MIR_BINARY &&
               value->immediate == '+') {
        const struct MirInsn *load = mir_definition(value->src1);
        int load_stack;
        long load_offset;

        if (load == NULL || load->opcode != MIR_LOAD_INDIRECT ||
            load->memory_size != store->memory_size ||
            load->bit_width != 0 ||
            (load->memory_flags & (1 | 8)) != 0 ||
            !mir_machine_parameter_address(
                load->src1, &load_stack, &load_offset, 0) ||
            load_stack != stack_offset || load_offset != offset ||
            !mir_machine_constant_bits(
                value->src2, store->memory_size, &bits))
            return 0;
        mutation->kind = MIR_FIXED_MUTATION_ADD;
    } else {
        return 0;
    }
    mutation->offset = (int)offset;
    mutation->width = store->memory_size;
    mutation->value = bits;
    return 1;
}

static int mir_match_fixed_param_mutations(
    struct MirFixedParamMutations *plan)
{
    int instruction;

    memset(plan, 0, sizeof(*plan));
    plan->parameter_stack_offset = -1;
    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
        case MIR_PARAM:
        case MIR_CONST:
        case MIR_FLOAT_CONST:
        case MIR_INDEX_ADDRESS:
        case MIR_BINARY:
            break;
        case MIR_LOAD:
        case MIR_LOAD_INDIRECT:
            if ((insn->memory_flags & (1 | 8)) != 0)
                return 0;
            break;
        case MIR_STORE_INDIRECT:
            if (plan->count >= 8 ||
                !mir_machine_match_fixed_mutation(
                    insn, &plan->mutations[plan->count],
                    &plan->parameter_stack_offset))
                return 0;
            ++plan->count;
            break;
        default:
            return 0;
        }
    }
    return plan->count > 0 && plan->parameter_stack_offset >= 0;
}

static void mir_machine_emit_parameter_address(
    FILE *out, int stack_offset, int offset)
{
    fprintf(out, "\tld hl,%d\n\tadd hl,sp\n", stack_offset);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n", out);
    mir_machine_emit_hl_offset(out, offset, 0);
}

static void mir_machine_emit_store_hl_to_bc(FILE *out)
{
    fputs("\tld a,l\n\tld (bc),a\n\tinc bc\n"
          "\tld a,h\n\tld (bc),a\n", out);
}

static void mir_machine_emit_fixed_mutation(
    FILE *out, const struct MirFixedParamMutations *plan,
    const struct MirFixedMutation *mutation)
{
    mir_machine_emit_parameter_address(
        out, plan->parameter_stack_offset, mutation->offset);
    if (mutation->kind == MIR_FIXED_MUTATION_SET) {
        if (mutation->width == 1) {
            fprintf(out, "\tld (hl),%lu\n", mutation->value & 0xffUL);
        } else if (mutation->width == 2) {
            fprintf(out,
                    "\tld (hl),%lu\n\tinc hl\n\tld (hl),%lu\n",
                    mutation->value & 0xffUL,
                    (mutation->value >> 8) & 0xffUL);
        } else {
            int byte;
            for (byte = 0; byte < 4; ++byte) {
                fprintf(out, "\tld (hl),%lu\n",
                        (mutation->value >> (byte * 8)) & 0xffUL);
                if (byte != 3)
                    fputs("\tinc hl\n", out);
            }
        }
        return;
    }
    fputs("\tpush hl\n", out);
    if (mutation->width == 1) {
        fputs("\tpop hl\n\tld a,(hl)\n", out);
        fprintf(out, "\tadd a,%lu\n\tld (hl),a\n",
                mutation->value & 0xffUL);
    } else if (mutation->width == 2) {
        fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n", out);
        fprintf(out, "\tld hl,%lu\n\tadd hl,de\n",
                mutation->value & 0xffffUL);
        fputs("\tpop bc\n", out);
        mir_machine_emit_store_hl_to_bc(out);
    } else if (mutation->width == 4) {
        fputs("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
              "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
              "\tld l,c\n\tld h,b\n", out);
        fprintf(out,
                "\tld bc,%lu\n\tadd hl,bc\n\tex de,hl\n"
                "\tld bc,%lu\n\tadc hl,bc\n\tex de,hl\n"
                "\tpop bc\n",
                mutation->value & 0xffffUL,
                (mutation->value >> 16) & 0xffffUL);
        fputs("\tld a,l\n\tld (bc),a\n\tinc bc\n"
              "\tld a,h\n\tld (bc),a\n\tinc bc\n"
              "\tld a,e\n\tld (bc),a\n\tinc bc\n"
              "\tld a,d\n\tld (bc),a\n", out);
    }
}

static void mir_emit_fixed_param_mutations(
    FILE *out, const struct MirFixedParamMutations *plan)
{
    int mutation;

    for (mutation = 0; mutation < plan->count; ++mutation)
        mir_machine_emit_fixed_mutation(
            out, plan, &plan->mutations[mutation]);
    fputs("\tret\n", out);
}

static int mir_machine_global_member(
    int value, struct Sym **root_out, int *offset_out)
{
    const struct MirInsn *member = mir_definition(value);
    const struct MirInsn *root;
    struct Sym *symbol;

    if (member == NULL || member->opcode != MIR_MEMBER_ADDRESS ||
        (member->memory_flags & (1 | 8)) != 0)
        return 0;
    root = mir_machine_resolve_local_alias(member->src1);
    if (root == NULL || root->opcode != MIR_ADDRESS)
        return 0;
    symbol = find_global(root->name);
    if (symbol == NULL || !symbol->is_defined || symbol->is_volatile)
        return 0;
    *root_out = symbol;
    *offset_out = (int)member->immediate;
    return 1;
}

static int mir_machine_parameter_value_offset(
    int value, int *stack_offset)
{
    const struct MirInsn *definition = mir_definition(value);

    if (definition != NULL && definition->opcode == MIR_UNARY &&
        definition->immediate == 0) {
        value = definition->src1;
        definition = mir_definition(value);
    }
    return definition != NULL && definition->opcode == MIR_PARAM &&
           mir_machine_parameter_offset(value, stack_offset);
}

static int mir_machine_match_global_append_store(
    const struct MirInsn *store, struct MirGlobalAppend *plan,
    struct MirGlobalAppendStore *append_store)
{
    const struct MirInsn *destination;
    const struct MirInsn *index;
    const struct MirInsn *array_member;
    const struct MirInsn *count_load;
    struct Sym *array_root;
    struct Sym *count_root;
    int array_offset;
    int count_offset;
    int field_offset = 0;

    if (store == NULL || store->opcode != MIR_STORE_INDIRECT ||
        (store->memory_size != 1 && store->memory_size != 2) ||
        store->bit_width != 0 ||
        (store->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_parameter_value_offset(
            store->src2, &append_store->parameter_stack_offset))
        return 0;
    destination = mir_definition(store->src1);
    if (destination != NULL &&
        destination->opcode == MIR_MEMBER_ADDRESS) {
        if ((destination->memory_flags & (1 | 8)) != 0)
            return 0;
        field_offset = (int)destination->immediate;
        destination = mir_definition(destination->src1);
    }
    if (destination == NULL ||
        destination->opcode != MIR_INDEX_ADDRESS ||
        (destination->memory_flags & 1) != 0 ||
        (destination->immediate != 1 &&
         destination->immediate != 2 &&
         destination->immediate != 4 &&
         destination->immediate != 8))
        return 0;
    index = destination;
    array_member = mir_definition(index->src1);
    count_load = mir_definition(index->src2);
    if (array_member == NULL ||
        array_member->opcode != MIR_MEMBER_ADDRESS ||
        count_load == NULL ||
        count_load->opcode != MIR_LOAD_INDIRECT ||
        count_load->memory_size != 2 ||
        count_load->bit_width != 0 ||
        (count_load->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_global_member(
            array_member->dst, &array_root, &array_offset) ||
        !mir_machine_global_member(
            count_load->src1, &count_root, &count_offset) ||
        array_root != count_root)
        return 0;
    if (plan->root == NULL) {
        plan->root = array_root;
        plan->array_offset = array_offset;
        plan->count_offset = count_offset;
        plan->element_stride = (int)index->immediate;
    } else if (plan->root != array_root ||
               plan->array_offset != array_offset ||
               plan->count_offset != count_offset ||
               plan->element_stride != (int)index->immediate) {
        return 0;
    }
    append_store->field_offset = field_offset;
    append_store->width = store->memory_size;
    return 1;
}

static int mir_machine_match_global_increment(
    const struct MirInsn *store, struct Sym **root_out, int *offset_out)
{
    const struct MirInsn *add;
    const struct MirInsn *load;
    const struct MirInsn *one;
    struct Sym *store_root;
    struct Sym *load_root;
    int store_offset;
    int load_offset;

    if (store == NULL || store->opcode != MIR_STORE_INDIRECT ||
        store->memory_size != 2 ||
        store->bit_width != 0 ||
        (store->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_global_member(
            store->src1, &store_root, &store_offset))
        return 0;
    add = mir_definition(store->src2);
    if (add == NULL || add->opcode != MIR_BINARY ||
        add->immediate != '+')
        return 0;
    load = mir_definition(add->src1);
    one = mir_definition(add->src2);
    if (load == NULL || load->opcode != MIR_LOAD_INDIRECT ||
        load->memory_size != 2 ||
        load->bit_width != 0 ||
        (load->memory_flags & (1 | 8)) != 0 ||
        one == NULL || one->opcode != MIR_CONST ||
        one->immediate != 1 ||
        !mir_machine_global_member(
            load->src1, &load_root, &load_offset) ||
        load_root != store_root || load_offset != store_offset)
        return 0;
    *root_out = store_root;
    *offset_out = store_offset;
    return 1;
}

static int mir_match_global_append(struct MirGlobalAppend *plan)
{
    struct Sym *increment_root = NULL;
    int increment_offset = 0;
    int increment_count = 0;
    int seen_increment = 0;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
        case MIR_PARAM:
        case MIR_CONST:
        case MIR_ADDRESS:
        case MIR_MEMBER_ADDRESS:
        case MIR_INDEX_ADDRESS:
        case MIR_UNARY:
        case MIR_BINARY:
            break;
        case MIR_LOAD_INDIRECT:
            if ((insn->memory_flags & (1 | 8)) != 0)
                return 0;
            break;
        case MIR_STORE_INDIRECT:
            if (mir_machine_match_global_increment(
                    insn, &increment_root, &increment_offset)) {
                ++increment_count;
                seen_increment = 1;
            } else {
                if (seen_increment || plan->store_count >= 8 ||
                    !mir_machine_match_global_append_store(
                        insn, plan, &plan->stores[plan->store_count]))
                    return 0;
                ++plan->store_count;
            }
            break;
        default:
            return 0;
        }
    }
    return plan->root != NULL && plan->store_count > 0 &&
           increment_count == 1 && increment_root == plan->root &&
           increment_offset == plan->count_offset;
}

static int mir_machine_match_nested_append_store(
    const struct MirInsn *store, struct MirNestedAppend *plan,
    struct MirNestedAppendStore *append_store)
{
    const struct MirInsn *destination;
    const struct MirInsn *array_member;
    const struct MirInsn *count_load;
    struct MirRowMemberAddress array_address;
    struct MirRowMemberAddress count_address;

    if (store == NULL || store->opcode != MIR_STORE_INDIRECT ||
        (store->memory_size != 1 && store->memory_size != 2) ||
        store->bit_width != 0 ||
        (store->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_parameter_value_offset(
            store->src2, &append_store->parameter_stack_offset))
        return 0;
    destination = mir_definition(store->src1);
    if (destination == NULL ||
        destination->opcode != MIR_INDEX_ADDRESS ||
        (destination->memory_flags & 1) != 0 ||
        (destination->immediate != 1 &&
         destination->immediate != 2 &&
         destination->immediate != 4 &&
         destination->immediate != 8))
        return 0;
    array_member = mir_definition(destination->src1);
    count_load = mir_definition(destination->src2);
    if (array_member == NULL ||
        array_member->opcode != MIR_MEMBER_ADDRESS ||
        count_load == NULL ||
        count_load->opcode != MIR_LOAD_INDIRECT ||
        count_load->memory_size != 2 ||
        count_load->bit_width != 0 ||
        (count_load->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_row_member_address(
            array_member->dst, &array_address) ||
        !mir_machine_row_member_address(
            count_load->src1, &count_address) ||
        !mir_machine_same_row(&array_address, &count_address))
        return 0;
    if (plan->root == NULL) {
        plan->root = array_address.root;
        plan->root_pointer_offset =
            array_address.root_pointer_offset;
        plan->row_stride = array_address.row_stride;
        plan->row_index_value = array_address.index_value;
        plan->count_member_offset = count_address.member_offset;
    } else if (plan->root != array_address.root ||
               plan->root_pointer_offset !=
                   array_address.root_pointer_offset ||
               plan->row_stride != array_address.row_stride ||
               plan->row_index_value !=
                   array_address.index_value ||
               plan->count_member_offset !=
                   count_address.member_offset) {
        return 0;
    }
    append_store->array_member_offset =
        array_address.member_offset;
    append_store->element_stride = (int)destination->immediate;
    append_store->width = store->memory_size;
    return 1;
}

static int mir_machine_match_nested_increment(
    const struct MirInsn *store, struct MirRowMemberAddress *address)
{
    const struct MirInsn *add;
    const struct MirInsn *load;
    const struct MirInsn *one;

    if (store == NULL || store->opcode != MIR_STORE_INDIRECT ||
        store->memory_size != 2 ||
        store->bit_width != 0 ||
        (store->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_row_member_address(store->src1, address))
        return 0;
    add = mir_definition(store->src2);
    if (add == NULL || add->opcode != MIR_BINARY ||
        add->immediate != '+')
        return 0;
    load = mir_definition(add->src1);
    one = mir_definition(add->src2);
    return load != NULL && load->opcode == MIR_LOAD_INDIRECT &&
           load->src1 == store->src1 &&
           load->memory_size == 2 &&
           load->bit_width == 0 &&
           (load->memory_flags & (1 | 8)) == 0 &&
           one != NULL && one->opcode == MIR_CONST &&
           one->immediate == 1;
}

static int mir_machine_local_pointer_alias(
    const struct MirInsn *insn)
{
    int memory_type;
    int memory_storage;
    int memory_offset;
    int declared;

    if (!mir_scalar_memory_location(
            insn, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_LOCAL ||
        type_ptr_depth(memory_type) == 0 ||
        type_size(memory_type) != 2 ||
        (insn->memory_flags & (1 | 8)) != 0)
        return 0;
    for (declared = 0; declared < mir.declared_count; ++declared)
        if (!strcmp(mir.declared_names[declared], insn->name))
            break;
    if (declared == mir.declared_count ||
        mir.declared_is_volatile[declared])
        return 0;
    if (insn->opcode == MIR_LOAD)
        return 1;
    if (insn->opcode == MIR_STORE) {
        const struct MirInsn *source = mir_definition(insn->src1);
        return source != NULL && source->opcode == MIR_ADDRESS;
    }
    return 0;
}

static int mir_match_nested_append(struct MirNestedAppend *plan)
{
    struct MirRowMemberAddress increment_address;
    int increment_count = 0;
    int seen_increment = 0;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    memset(&increment_address, 0, sizeof(increment_address));
    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
        case MIR_PARAM:
        case MIR_CONST:
        case MIR_ADDRESS:
        case MIR_MEMBER_ADDRESS:
        case MIR_INDEX_ADDRESS:
        case MIR_UNARY:
        case MIR_BINARY:
            break;
        case MIR_LOAD:
        case MIR_STORE:
            if (!mir_machine_local_pointer_alias(insn))
                return 0;
            break;
        case MIR_LOAD_INDIRECT:
            if ((insn->memory_flags & (1 | 8)) != 0)
                return 0;
            break;
        case MIR_STORE_INDIRECT:
            if (mir_machine_match_nested_increment(
                    insn, &increment_address)) {
                ++increment_count;
                seen_increment = 1;
            } else {
                if (seen_increment || plan->store_count >= 8 ||
                    !mir_machine_match_nested_append_store(
                        insn, plan,
                        &plan->stores[plan->store_count]))
                    return 0;
                ++plan->store_count;
            }
            break;
        default:
            return 0;
        }
    }
    if (plan->root == NULL || plan->store_count == 0 ||
        increment_count != 1 ||
        increment_address.root != plan->root ||
        increment_address.root_pointer_offset !=
            plan->root_pointer_offset ||
        increment_address.row_stride != plan->row_stride ||
        increment_address.index_value != plan->row_index_value ||
        increment_address.member_offset !=
            plan->count_member_offset ||
        !mir_machine_parameter_offset(
            increment_address.index_value,
            &plan->row_index_stack_offset))
        return 0;
    return 1;
}

struct MirStateMember {
    struct Sym *root;
    int root_offset;
    int member_offset;
};

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

static int mir_machine_index_update(
    const struct MirInsn *store, int operation,
    struct MirStateMember *member_out, int *old_value,
    int *new_value)
{
    const struct MirInsn *binary;
    const struct MirInsn *load;
    const struct MirInsn *one;
    struct MirStateMember loaded_member;

    if (store == NULL || store->opcode != MIR_STORE_INDIRECT ||
        store->memory_size != 2 || store->bit_width != 0 ||
        (store->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_state_member_address(
            store->src1, member_out))
        return 0;
    binary = mir_definition(store->src2);
    if (binary == NULL || binary->opcode != MIR_BINARY ||
        binary->immediate != operation)
        return 0;
    load = mir_definition(binary->src1);
    one = mir_definition(binary->src2);
    if (load == NULL || load->opcode != MIR_LOAD_INDIRECT ||
        load->memory_size != 2 || load->bit_width != 0 ||
        (load->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_state_member_address(
            load->src1, &loaded_member) ||
        !mir_machine_same_state_member(
            member_out, &loaded_member) ||
        one == NULL || one->opcode != MIR_CONST ||
        one->immediate != 1)
        return 0;
    *old_value = load->dst;
    *new_value = binary->dst;
    return 1;
}

static int mir_machine_indexed_stack_address(
    int value, struct MirStateMember *base_member,
    int *index_value, int *element_width)
{
    const struct MirInsn *index = mir_definition(value);
    const struct MirInsn *base_load;

    if (index == NULL || index->opcode != MIR_INDEX_ADDRESS ||
        index->immediate != 2 ||
        (index->memory_flags & 1) != 0)
        return 0;
    base_load = mir_definition(index->src1);
    if (base_load == NULL ||
        base_load->opcode != MIR_LOAD_INDIRECT ||
        base_load->memory_size != 2 ||
        base_load->bit_width != 0 ||
        type_ptr_depth(base_load->type) == 0 ||
        (base_load->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_state_member_address(
            base_load->src1, base_member))
        return 0;
    *index_value = index->src2;
    *element_width = (int)index->immediate;
    return 1;
}

static int mir_match_indexed_stack(struct MirIndexedStack *plan)
{
    const struct MirInsn *index_store = NULL;
    const struct MirInsn *element_store = NULL;
    const struct MirInsn *return_insn = NULL;
    const struct MirInsn *element_load;
    struct MirStateMember index_member;
    struct MirStateMember base_member;
    int old_index = -1;
    int new_index = -1;
    int address_index = -1;
    int parameter_count = 0;
    int store_count = 0;
    int return_count = 0;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    memset(&index_member, 0, sizeof(index_member));
    memset(&base_member, 0, sizeof(base_member));
    if (mir.has_vla || mir_cfg_block_count() != 1)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
        case MIR_CONST:
        case MIR_MEMBER_ADDRESS:
        case MIR_INDEX_ADDRESS:
        case MIR_BINARY:
            break;
        case MIR_PARAM:
            ++parameter_count;
            break;
        case MIR_LOAD:
            if ((insn->memory_flags & (1 | 8)) != 0)
                return 0;
            break;
        case MIR_LOAD_INDIRECT:
            if ((insn->memory_flags & (1 | 8)) != 0 ||
                insn->bit_width != 0)
                return 0;
            break;
        case MIR_STORE_INDIRECT:
            if ((insn->memory_flags & (1 | 8)) != 0 ||
                insn->bit_width != 0)
                return 0;
            ++store_count;
            if (mir_definition(insn->src1) != NULL &&
                mir_definition(insn->src1)->opcode ==
                    MIR_INDEX_ADDRESS)
                element_store = insn;
            else
                index_store = insn;
            break;
        case MIR_RETURN:
            ++return_count;
            return_insn = insn;
            break;
        default:
            return 0;
        }
    }
    if (store_count == 2 && parameter_count == 1 &&
        return_count == 0 && element_store != NULL &&
        index_store != NULL &&
        (mir.return_type & 15) == TYPE_VOID) {
        plan->kind = MIR_INDEXED_STACK_PUSH;
        if (!mir_machine_index_update(
                index_store, '+', &index_member,
                &old_index, &new_index) ||
            !mir_machine_indexed_stack_address(
                element_store->src1, &base_member,
                &address_index, &plan->element_width) ||
            address_index != old_index ||
            index_store >= element_store ||
            element_store->memory_size != plan->element_width ||
            !mir_machine_parameter_value_offset(
                element_store->src2,
                &plan->value_stack_offset))
            return 0;
    } else if (store_count == 1 && parameter_count == 0 &&
               return_count == 1 && index_store != NULL &&
               return_insn != NULL &&
               type_size(mir.return_type) == 2) {
        plan->kind = MIR_INDEXED_STACK_POP;
        element_load = mir_definition(return_insn->src1);
        if (!mir_machine_index_update(
                index_store, '-', &index_member,
                &old_index, &new_index) ||
            element_load == NULL ||
            element_load->opcode != MIR_LOAD_INDIRECT ||
            element_load->bit_width != 0 ||
            (element_load->memory_flags & (1 | 8)) != 0 ||
            !mir_machine_indexed_stack_address(
                element_load->src1, &base_member,
                &address_index, &plan->element_width) ||
            address_index != new_index ||
            index_store >= element_load ||
            element_load->memory_size != plan->element_width ||
            type_size(mir.return_type) != plan->element_width)
            return 0;
    } else {
        return 0;
    }
    if (index_member.root != base_member.root ||
        index_member.root_offset != base_member.root_offset)
        return 0;
    plan->root = index_member.root;
    plan->root_offset = index_member.root_offset;
    plan->base_member_offset = base_member.member_offset;
    plan->index_member_offset = index_member.member_offset;
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
            memory_offset != plan->root_offset ||
            find_global(insn->name) != plan->root)
            return 0;
    }
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

static int mir_machine_pointer_update(
    const struct MirInsn *store, int operation,
    struct MirStateMember *member_out,
    const struct MirInsn **load_out)
{
    const struct MirInsn *binary;
    const struct MirInsn *load;
    struct MirStateMember loaded_member;
    long amount;

    if (store == NULL || store->opcode != MIR_STORE_INDIRECT ||
        store->memory_size != 2 || store->bit_width != 0 ||
        (store->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_state_member_address(
            store->src1, member_out))
        return 0;
    binary = mir_definition(store->src2);
    if (binary == NULL || binary->opcode != MIR_BINARY ||
        binary->immediate != operation)
        return 0;
    load = mir_definition(binary->src1);
    if (load == NULL || load->opcode != MIR_LOAD_INDIRECT ||
        load->memory_size != 2 || load->bit_width != 0 ||
        type_ptr_depth(load->type) == 0 ||
        (load->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_state_member_address(
            load->src1, &loaded_member) ||
        !mir_machine_same_state_member(
            member_out, &loaded_member) ||
        !mir_machine_constant_value(
            binary->src2, &amount, 0) ||
        amount != 2)
        return 0;
    *load_out = load;
    return 1;
}

static int mir_machine_pointer_member_load(
    int value, struct MirStateMember *member_out)
{
    const struct MirInsn *load = mir_definition(value);

    return load != NULL && load->opcode == MIR_LOAD_INDIRECT &&
           load->memory_size == 2 && load->bit_width == 0 &&
           type_ptr_depth(load->type) > 0 &&
           (load->memory_flags & (1 | 8)) == 0 &&
           mir_machine_state_member_address(
               load->src1, member_out);
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

static int mir_match_pointer_stack(struct MirPointerStack *plan)
{
    const struct MirInsn *pointer_store = NULL;
    const struct MirInsn *element_store = NULL;
    const struct MirInsn *return_insn = NULL;
    const struct MirInsn *element_load;
    const struct MirInsn *update_load = NULL;
    const struct MirInsn *data_pointer_load;
    struct MirStateMember update_member;
    struct MirStateMember data_member;
    int parameter_count = 0;
    int store_count = 0;
    int return_count = 0;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    memset(&update_member, 0, sizeof(update_member));
    memset(&data_member, 0, sizeof(data_member));
    if (mir.has_vla || mir_cfg_block_count() != 1)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
        case MIR_CONST:
        case MIR_MEMBER_ADDRESS:
        case MIR_UNARY:
        case MIR_BINARY:
            break;
        case MIR_PARAM:
            ++parameter_count;
            break;
        case MIR_LOAD:
            if ((insn->memory_flags & (1 | 8)) != 0)
                return 0;
            break;
        case MIR_LOAD_INDIRECT:
            if ((insn->memory_flags & (1 | 8)) != 0 ||
                insn->bit_width != 0)
                return 0;
            break;
        case MIR_STORE_INDIRECT:
            if ((insn->memory_flags & (1 | 8)) != 0 ||
                insn->bit_width != 0)
                return 0;
            ++store_count;
            if (mir_definition(insn->src1) != NULL &&
                mir_definition(insn->src1)->opcode ==
                    MIR_MEMBER_ADDRESS)
                pointer_store = insn;
            else
                element_store = insn;
            break;
        case MIR_RETURN:
            ++return_count;
            return_insn = insn;
            break;
        default:
            return 0;
        }
    }
    if (store_count == 2 && parameter_count == 1 &&
        return_count == 0 && pointer_store != NULL &&
        element_store != NULL &&
        (mir.return_type & 15) == TYPE_VOID) {
        plan->kind = MIR_POINTER_STACK_PUSH;
        if (!mir_machine_pointer_update(
                pointer_store, '+', &update_member,
                &update_load) ||
            !mir_machine_pointer_member_load(
                element_store->src1, &data_member) ||
            !mir_machine_same_state_member(
                &update_member, &data_member) ||
            element_store >= update_load ||
            update_load >= pointer_store ||
            element_store->memory_size != 2 ||
            !mir_machine_parameter_value_offset(
                element_store->src2,
                &plan->value_stack_offset))
            return 0;
    } else if (store_count == 1 && parameter_count == 0 &&
               return_count == 1 && pointer_store != NULL &&
               return_insn != NULL &&
               type_size(mir.return_type) == 2) {
        plan->kind = MIR_POINTER_STACK_POP;
        element_load = mir_definition(return_insn->src1);
        if (!mir_machine_pointer_update(
                pointer_store, '-', &update_member,
                &update_load) ||
            element_load == NULL ||
            element_load->opcode != MIR_LOAD_INDIRECT ||
            element_load->memory_size != 2 ||
            element_load->bit_width != 0 ||
            (element_load->memory_flags & (1 | 8)) != 0 ||
            !mir_machine_pointer_member_load(
                element_load->src1, &data_member) ||
            !mir_machine_same_state_member(
                &update_member, &data_member) ||
            (data_pointer_load =
                mir_definition(element_load->src1)) == NULL ||
            pointer_store >= data_pointer_load ||
            data_pointer_load >= element_load)
            return 0;
    } else {
        return 0;
    }
    plan->root = update_member.root;
    plan->root_offset = update_member.root_offset;
    plan->pointer_member_offset = update_member.member_offset;
    return mir_machine_only_root_loads(
        plan->root, plan->root_offset);
}

static int mir_match_nested_row_store(struct MirNestedRowStore *plan)
{
    const struct MirInsn *value_store = NULL;
    const struct MirInsn *count_store = NULL;
    const struct MirInsn *destination;
    const struct MirInsn *count_load;
    const struct MirInsn *increment;
    const struct MirInsn *one;
    struct MirRowMemberAddress value_member;
    struct MirRowMemberAddress loaded_count_member;
    struct MirRowMemberAddress stored_count_member;
    int store_count = 0;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        (mir.return_type & 15) != TYPE_VOID)
        return mir_machine_reject("nested-row-store", "preflight");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
        case MIR_PARAM:
        case MIR_CONST:
        case MIR_ADDRESS:
        case MIR_LOAD:
        case MIR_STORE:
        case MIR_MEMBER_ADDRESS:
        case MIR_LOAD_INDIRECT:
        case MIR_INDEX_ADDRESS:
        case MIR_BINARY:
            break;
        case MIR_STORE_INDIRECT:
            if (++store_count > 2)
                return mir_machine_reject(
                    "nested-row-store", "store-count");
            if (mir_definition(insn->src2) != NULL &&
                mir_definition(insn->src2)->opcode == MIR_BINARY)
                count_store = insn;
            else
                value_store = insn;
            break;
        default:
            return mir_machine_reject("nested-row-store", "opcode");
        }
    }
    if (store_count != 2 || value_store == NULL || count_store == NULL)
        return mir_machine_reject("nested-row-store", "stores");
    destination = mir_definition(value_store->src1);
    if (destination == NULL ||
        destination->opcode != MIR_INDEX_ADDRESS ||
        destination->immediate <= 0 ||
        destination->memory_size != 2 ||
        !mir_machine_row_member_address(
            destination->src1, &value_member))
        return mir_machine_reject("nested-row-store", "destination");
    count_load = mir_definition(destination->src2);
    if (count_load == NULL || count_load->opcode != MIR_LOAD_INDIRECT ||
        count_load->memory_size != 2)
        return mir_machine_reject("nested-row-store", "count-load");
    if (!mir_machine_row_member_address(
            count_load->src1, &loaded_count_member) ||
        !mir_machine_same_row(&value_member, &loaded_count_member))
        return mir_machine_reject(
            "nested-row-store", "loaded-count-row");
    if (!mir_machine_row_member_address(
            count_store->src1, &stored_count_member) ||
        !mir_machine_same_row(&value_member, &stored_count_member) ||
        stored_count_member.member_offset !=
            loaded_count_member.member_offset)
        return mir_machine_reject(
            "nested-row-store", "stored-count-row");
    increment = mir_definition(count_store->src2);
    if (increment == NULL || increment->opcode != MIR_BINARY ||
        increment->immediate != '+' ||
        increment->src1 < 0 || increment->src2 < 0)
        return mir_machine_reject("nested-row-store", "increment");
    one = mir_definition(increment->src2);
    if (one == NULL || one->opcode != MIR_CONST ||
        one->immediate != 1)
        return mir_machine_reject("nested-row-store", "increment-one");
    {
        const struct MirInsn *increment_source =
            mir_definition(increment->src1);
        if (increment_source == NULL ||
            increment_source->opcode != MIR_LOAD_INDIRECT ||
            increment_source->src1 != count_store->src1 ||
            increment_source->memory_size != 2)
            return mir_machine_reject(
                "nested-row-store", "increment-source");
    }
    if (!mir_machine_parameter_offset(
            value_member.index_value, &plan->index_stack_offset) ||
        !mir_machine_parameter_offset(
            value_store->src2, &plan->value_stack_offset))
        return mir_machine_reject("nested-row-store", "parameters");
    plan->root = value_member.root;
    plan->root_pointer_offset = value_member.root_pointer_offset;
    plan->row_stride = value_member.row_stride;
    plan->value_member_offset = value_member.member_offset;
    plan->count_member_offset = loaded_count_member.member_offset;
    plan->element_stride = (int)destination->immediate;
    return 1;
}

static void mir_machine_emit_global_word(
    FILE *out, struct Sym *symbol, int offset)
{
    const char *name = asm_name_for(sym_asm_name(symbol));

    if ((symbol->storage == SC_EXTERN || symbol->needs_extrn) &&
        mir_extrn_should_emit(symbol))
        fprintf(out, "\textrn %s\n", name);
    if (offset == 0)
        fprintf(out, "\tld hl,(%s)\n", name);
    else
        fprintf(out, "\tld hl,(%s%+d)\n", name, offset);
}

static void mir_machine_emit_global_address_de(
    FILE *out, struct Sym *symbol, int offset)
{
    const char *name = asm_name_for(sym_asm_name(symbol));

    if ((symbol->storage == SC_EXTERN || symbol->needs_extrn) &&
        mir_extrn_should_emit(symbol))
        fprintf(out, "\textrn %s\n", name);
    if (offset == 0)
        fprintf(out, "\tld de,%s\n", name);
    else
        fprintf(out, "\tld de,%s%+d\n", name, offset);
}

static void mir_machine_emit_global_word_store(
    FILE *out, struct Sym *symbol, int offset)
{
    const char *name = asm_name_for(sym_asm_name(symbol));

    if (offset == 0)
        fprintf(out, "\tld (%s),hl\n", name);
    else
        fprintf(out, "\tld (%s%+d),hl\n", name, offset);
}

static void mir_machine_emit_bc_offset(FILE *out, int offset)
{
    if (offset == 0)
        return;
    if (offset >= -4 && offset <= 4) {
        const char *instruction = offset > 0 ? "\tinc bc\n" : "\tdec bc\n";
        int count = offset > 0 ? offset : -offset;

        while (count-- > 0)
            fputs(instruction, out);
        return;
    }
    fprintf(out, "\tld hl,%d\n\tadd hl,bc\n"
                 "\tld b,h\n\tld c,l\n", offset & 0xffff);
}

static void mir_machine_emit_parameter_store_to_bc(
    FILE *out, const struct MirGlobalAppendStore *store)
{
    fprintf(out, "\tld hl,%d\n\tadd hl,sp\n",
            store->parameter_stack_offset);
    if (store->width == 1) {
        fputs("\tld a,(hl)\n\tld (bc),a\n", out);
    } else {
        fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
              "\tld a,e\n\tld (bc),a\n\tinc bc\n"
              "\tld a,d\n\tld (bc),a\n", out);
    }
}

static void mir_emit_global_append(
    FILE *out, const struct MirGlobalAppend *plan)
{
    int current_offset = 0;
    int store;

    mir_machine_emit_global_word(out, plan->root, plan->count_offset);
    for (store = 1; store < plan->element_stride; store <<= 1)
        fputs("\tadd hl,hl\n", out);
    mir_machine_emit_global_address_de(
        out, plan->root, plan->array_offset);
    fputs("\tadd hl,de\n\tld c,l\n\tld b,h\n", out);
    for (store = 0; store < plan->store_count; ++store) {
        const struct MirGlobalAppendStore *append_store =
            &plan->stores[store];

        mir_machine_emit_bc_offset(
            out, append_store->field_offset - current_offset);
        mir_machine_emit_parameter_store_to_bc(out, append_store);
        current_offset = append_store->field_offset +
            (append_store->width == 2 ? 1 : 0);
    }
    mir_machine_emit_global_word(out, plan->root, plan->count_offset);
    fputs("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->root, plan->count_offset);
    fputs("\tret\n", out);
}

static void mir_emit_nested_append(
    FILE *out, const struct MirNestedAppend *plan)
{
    int done = new_label();
    int store;

    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld l,e\n\tld h,d\n",
            plan->row_index_stack_offset);
    mir_emit_mul_hl_const(out, (unsigned long)plan->row_stride);
    fputs("\tld c,l\n\tld b,h\n", out);
    mir_machine_emit_global_word(
        out, plan->root, plan->root_pointer_offset);
    fputs("\tadd hl,bc\n\tld c,l\n\tld b,h\n", out);
    for (store = 0; store < plan->store_count; ++store) {
        const struct MirNestedAppendStore *append_store =
            &plan->stores[store];
        struct MirGlobalAppendStore parameter_store;

        fputs("\tld l,c\n\tld h,b\n", out);
        mir_machine_emit_hl_offset(
            out, plan->count_member_offset, 1);
        fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
              "\tld l,e\n\tld h,d\n", out);
        mir_emit_mul_hl_const(
            out, (unsigned long)append_store->element_stride);
        mir_machine_emit_hl_offset(
            out, append_store->array_member_offset, 1);
        fputs("\tadd hl,bc\n\tpush bc\n"
              "\tld c,l\n\tld b,h\n", out);
        parameter_store.parameter_stack_offset =
            append_store->parameter_stack_offset + 2;
        parameter_store.field_offset = 0;
        parameter_store.width = append_store->width;
        mir_machine_emit_parameter_store_to_bc(
            out, &parameter_store);
        fputs("\tpop bc\n", out);
    }
    fputs("\tld l,c\n\tld h,b\n", out);
    mir_machine_emit_hl_offset(
        out, plan->count_member_offset, 0);
    fputs("\tinc (hl)\n", out);
    fprintf(out, "\tjp nz, L%d\n\tinc hl\n\tinc (hl)\nL%d:\n\tret\n",
            done, done);
}

static void mir_machine_emit_indexed_stack_base(
    FILE *out, const struct MirIndexedStack *plan)
{
    mir_machine_emit_global_word(
        out, plan->root, plan->root_offset);
    fputs("\tld c,l\n\tld b,h\n"
          "\tld l,c\n\tld h,b\n", out);
    mir_machine_emit_hl_offset(
        out, plan->base_member_offset, 1);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tpush de\n\tld l,c\n\tld h,b\n", out);
    mir_machine_emit_hl_offset(
        out, plan->index_member_offset, 0);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n", out);
}

static void mir_emit_indexed_stack(
    FILE *out, const struct MirIndexedStack *plan)
{
    if (plan->kind == MIR_INDEXED_STACK_PUSH) {
        fprintf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
                "\tpush de\n",
                plan->value_stack_offset);
    }
    mir_machine_emit_indexed_stack_base(out, plan);
    if (plan->kind == MIR_INDEXED_STACK_PUSH)
        fputs("\tinc de\n", out);
    else
        fputs("\tdec de\n", out);
    fputs("\tld (hl),d\n\tdec hl\n\tld (hl),e\n", out);
    if (plan->kind == MIR_INDEXED_STACK_PUSH)
        fputs("\tdec de\n", out);
    fputs("\tld l,e\n\tld h,d\n\tadd hl,hl\n"
          "\tpop de\n\tadd hl,de\n", out);
    if (plan->kind == MIR_INDEXED_STACK_PUSH) {
        fputs("\tpop de\n\tld (hl),e\n\tinc hl\n"
              "\tld (hl),d\n\tret\n", out);
    } else {
        fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
              "\tex de,hl\n\tret\n", out);
    }
}

static void mir_emit_pointer_stack(
    FILE *out, const struct MirPointerStack *plan)
{
    mir_machine_emit_global_word(
        out, plan->root, plan->root_offset);
    mir_machine_emit_hl_offset(
        out, plan->pointer_member_offset, 0);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n", out);
    if (plan->kind == MIR_POINTER_STACK_PUSH) {
        fprintf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
                "\tld a,c\n\tld (de),a\n\tinc de\n"
                "\tld a,b\n\tld (de),a\n",
                plan->value_stack_offset);
        mir_machine_emit_global_word(
            out, plan->root, plan->root_offset);
        mir_machine_emit_hl_offset(
            out, plan->pointer_member_offset, 0);
        fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
              "\tinc de\n\tinc de\n"
              "\tld (hl),d\n\tdec hl\n"
              "\tld (hl),e\n\tret\n", out);
    } else {
        fputs("\tdec de\n\tdec de\n"
              "\tld (hl),d\n\tdec hl\n\tld (hl),e\n", out);
        mir_machine_emit_global_word(
            out, plan->root, plan->root_offset);
        mir_machine_emit_hl_offset(
            out, plan->pointer_member_offset, 0);
        fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
              "\tex de,hl\n\tld e,(hl)\n\tinc hl\n"
              "\tld d,(hl)\n\tex de,hl\n\tret\n", out);
    }
}

static void mir_emit_nested_row_store(
    FILE *out, const struct MirNestedRowStore *plan)
{
    int done = new_label();

    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tpush de\n"
            "\tld l,c\n\tld h,b\n",
            plan->index_stack_offset,
            plan->value_stack_offset);
    mir_emit_mul_hl_const(
        out, (unsigned long)plan->row_stride);
    fputs("\tld c,l\n\tld b,h\n", out);
    mir_machine_emit_global_word(
        out, plan->root, plan->root_pointer_offset);
    fputs("\tadd hl,bc\n\tld c,l\n\tld b,h\n", out);
    mir_machine_emit_hl_offset(
        out, plan->count_member_offset, 1);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld l,e\n\tld h,d\n", out);
    mir_emit_mul_hl_const(
        out, (unsigned long)plan->element_stride);
    mir_machine_emit_hl_offset(
        out, plan->value_member_offset, 1);
    fputs("\tadd hl,bc\n\tpop de\n"
          "\tld (hl),e\n\tinc hl\n\tld (hl),d\n"
          "\tld l,c\n\tld h,b\n", out);
    mir_machine_emit_hl_offset(
        out, plan->count_member_offset, 1);
    fputs("\tinc (hl)\n", out);
    fprintf(out, "\tjp nz, L%d\n\tinc hl\n\tinc (hl)\nL%d:\n\tret\n",
            done, done);
}

static void mir_machine_emit_hl_offset(
    FILE *out, int offset, int preserve_bc)
{
    int count;

    if (offset >= -8 && offset <= 8) {
        const char *operation = offset < 0 ? "\tdec hl\n" : "\tinc hl\n";
        for (count = 0; count < (offset < 0 ? -offset : offset); ++count)
            fputs(operation, out);
        return;
    }
    if (preserve_bc)
        fputs("\tpush bc\n", out);
    fprintf(out, "\tld bc,%d\n\tadd hl,bc\n", offset);
    if (preserve_bc)
        fputs("\tpop bc\n", out);
}

static void mir_emit_indexed_word_sum(
    FILE *out, const struct MirIndexedWordSum *plan)
{
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld l,e\n\tld h,d\n",
            plan->parameter_stack_offset);
    mir_machine_emit_hl_offset(out, plan->left_offset, 0);
    fputs("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
          "\tld l,e\n\tld h,d\n", out);
    mir_machine_emit_hl_offset(out, plan->right_offset, 1);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld l,c\n\tld h,b\n\tadd hl,de\n\tret\n", out);
}

int mir_try_emit_scheduled_machine_cfg(FILE *out)
{
    struct MirIndexedWordSum indexed_word_sum;
    struct MirNestedRowStore nested_row_store;
    struct MirFlatArrayChecks flat_array_checks;
    struct MirFixedParamMutations fixed_param_mutations;
    struct MirGlobalAppend global_append;
    struct MirNestedAppend nested_append;
    struct MirIndexedStack indexed_stack;
    struct MirPointerStack pointer_stack;
    long constant;

    if (mir_match_affine_pointer_constant_return(&constant)) {
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
        fprintf(out, "\tld hl,%ld\n\tret\n", constant);
        return 1;
    }
    if (mir_match_nested_row_store(&nested_row_store)) {
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
        mir_emit_nested_row_store(out, &nested_row_store);
        return 1;
    }
    if (mir_match_flat_array_checks(&flat_array_checks)) {
        mir_emit_flat_array_checks(out, &flat_array_checks);
        return 1;
    }
    if (mir_match_fixed_param_mutations(&fixed_param_mutations)) {
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
        mir_emit_fixed_param_mutations(out, &fixed_param_mutations);
        return 1;
    }
    if (mir_match_global_append(&global_append)) {
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
        mir_emit_global_append(out, &global_append);
        return 1;
    }
    if (mir_match_nested_append(&nested_append)) {
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
        mir_emit_nested_append(out, &nested_append);
        return 1;
    }
    if (mir_match_indexed_stack(&indexed_stack)) {
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
        mir_emit_indexed_stack(out, &indexed_stack);
        return 1;
    }
    if (mir_match_pointer_stack(&pointer_stack)) {
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
        mir_emit_pointer_stack(out, &pointer_stack);
        return 1;
    }

    if (!mir_match_indexed_word_sum(&indexed_word_sum))
        return 0;
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_emit_indexed_word_sum(out, &indexed_word_sum);
    return 1;
}
