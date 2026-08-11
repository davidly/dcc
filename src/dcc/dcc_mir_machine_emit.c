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

    if (!mir_match_indexed_word_sum(&indexed_word_sum))
        return 0;
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_emit_indexed_word_sum(out, &indexed_word_sum);
    return 1;
}
