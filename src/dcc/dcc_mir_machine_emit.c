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

enum MirByteMemoryStackKind {
    MIR_BYTE_MEMORY_STACK_PUSH = 1,
    MIR_BYTE_MEMORY_STACK_POP = 2,
    MIR_BYTE_MEMORY_STACK_PUSH_WORD = 3
};

struct MirByteMemoryStack {
    int kind;
    struct Sym *memory_root;
    int memory_offset;
    struct Sym *cursor_root;
    int cursor_offset;
    int value_stack_offset;
};

struct MirFixedArrayReduction {
    int parameter_stack_offset;
    int element_width;
    int element_is_unsigned;
};

struct MirWideMemberUpdate {
    int pointer_stack_offset;
    int value_stack_offset;
    int member_offset;
    int operation;
};

struct MirSignedMemberProduct {
    int pointer_stack_offset;
    int left_member_offset;
    int right_member_offset;
    unsigned long scale;
};

struct MirWideNarrowDivision {
    int wide_stack_offset;
    int narrow_stack_offset;
    int operation;
    int is_unsigned;
};

struct MirAggregateSumField {
    int offset;
    int width;
    int is_unsigned;
};

struct MirAggregateFieldSum {
    int parameter_stack_offset;
    int field_count;
    struct MirAggregateSumField fields[4];
};

struct MirConstantChecks {
    struct Sym *function;
    int count;
    int strings[16];
    long actual[16];
    long expected[16];
};

struct MirConstantPrints {
    struct Sym *function;
    int count;
    int strings[16];
    long values[16];
};

struct MirPointerDifferencePrints {
    struct Sym *function;
    int count;
    int strings[16];
    struct Sym *left[16];
    struct Sym *right[16];
    long right_constant[16];
};

struct MirByteComparisonPrint {
    struct Sym *function;
    int left_stack_offset;
    int right_stack_offset;
    int is_unsigned;
    int string_id;
};

struct MirConstantBufferCallPrint {
    struct Sym *pack_function;
    char print_name[64];
    int string_id;
    unsigned char bytes[4];
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

struct MirSignedMemberSquareScaleDiv {
    int pointer_stack_offset;
    int member_offset;
    unsigned long scale;
    unsigned long divisor;
};

struct MirSignedMemberScalePair {
    int pointer_stack_offset;
    int value_stack_offset;
    int member_offsets[2];
    unsigned long divisor;
};

static void mir_machine_emit_hl_offset(
    FILE *out, int offset, int preserve_bc);
static int mir_machine_named_nonvolatile(
    const struct MirInsn *insn);
static int mir_machine_constant_equals(
    int value, long expected);
static int mir_machine_parameter_value_offset(
    int value, int *stack_offset);
static void mir_machine_emit_global_address_de(
    FILE *out, struct Sym *symbol, int offset);
static void mir_machine_emit_global_word(
    FILE *out, struct Sym *symbol, int offset);

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
    char name[64];
};

static int mir_machine_convert_integer(
    long value, int type, long *result)
{
    int width = type_size(type);
    unsigned long bits;
    unsigned long mask;
    unsigned long sign;

    if (type_ptr_depth(type) != 0 || type_is_float(type) ||
        (type & 15) == TYPE_BOOL ||
        (width != 1 && width != 2 && width != 4))
        return 0;
    mask = width == 1 ? 0xffUL :
           width == 2 ? 0xffffUL : 0xffffffffUL;
    sign = width == 1 ? 0x80UL :
           width == 2 ? 0x8000UL : 0x80000000UL;
    bits = (unsigned long)value & mask;
    if ((type & TYPE_UNSIGNED) == 0 && (bits & sign) != 0)
        bits |= ~mask;
    *result = (long)bits;
    return 1;
}

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
            signed_value = signed_lhs / signed_rhs;
            break;
        default:
            return 0;
        }
        bits = (unsigned long long)signed_value;
    }
    bits &= mask;
    if (is_unsigned)
        *result = (long)(unsigned long)bits;
    else if ((bits & sign) != 0)
        *result = (long)((long long)bits - (long long)modulus);
    else
        *result = (long)bits;
    return 1;
}

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
           !strcmp(left->name, right->name) &&
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

static int mir_machine_unobservable_local_store(
    const struct MirInsn *store)
{
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    if (store == NULL || store->opcode != MIR_STORE ||
        !mir_machine_named_nonvolatile(store) ||
        !mir_scalar_memory_location(
            store, &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_LOCAL)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_ADDRESS &&
            !strcmp(mir.insns[instruction].name, store->name))
            return 0;
    return 1;
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
        long converted;

        if (!mir_machine_convert_integer(
                definition->immediate, definition->type,
                &converted))
            return 0;
        form->kind = MIR_MACHINE_FORM_INTEGER;
        form->value = converted;
        form->storage = 0;
        form->offset = 0;
        form->pointer_terms = 0;
        form->name[0] = 0;
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
        snprintf(form->name, sizeof(form->name), "%s",
                 definition->name);
        return 1;
    }
    if (definition->opcode == MIR_LOAD) {
        const struct MirInsn *stored =
            mir_machine_resolve_local_alias(value);

        if (stored == NULL &&
            getenv("DCC_MIR_POINTER_REPORT") != NULL)
            fprintf(stderr,
                    "; MIR pointer function=%s value=%d reject=alias\n",
                    mir.name, value);
        return stored != NULL &&
               mir_machine_pointer_form(
                   stored->dst, definition_index,
                   form, depth + 1);
    }
    if (definition->opcode == MIR_UNARY &&
        definition->immediate == 0) {
        struct MirMachineForm source;
        const struct MirInsn *source_definition =
            mir_definition(definition->src1);

        if (!mir_machine_pointer_form(
                definition->src1, definition_index,
                &source, depth + 1))
            return 0;
        if (source.kind == MIR_MACHINE_FORM_POINTER) {
            if (source_definition == NULL ||
                type_ptr_depth(source_definition->type) == 0 ||
                type_ptr_depth(definition->type) !=
                    type_ptr_depth(source_definition->type) ||
                type_size(definition->type) != 2 ||
                type_size(source_definition->type) != 2)
                return 0;
            *form = source;
            return 1;
        }
        *form = source;
        return mir_machine_convert_integer(
            source.value, definition->type, &form->value);
    }
    if (definition->opcode == MIR_INDEX_ADDRESS) {
        struct MirMachineForm base;
        struct MirMachineForm index;
        long scaled;

        if (definition->immediate <= 0 ||
            !mir_machine_pointer_form(
                definition->src1, definition_index,
                &base, depth + 1) ||
            !mir_machine_pointer_form(
                definition->src2, definition_index,
                &index, depth + 1) ||
            base.kind != MIR_MACHINE_FORM_POINTER ||
            index.kind != MIR_MACHINE_FORM_INTEGER ||
            !mir_machine_fold_integer_binary(
                '*', index.value, definition->immediate,
                TYPE_INT, &scaled))
            return 0;
        *form = base;
        form->value += scaled;
        form->pointer_terms += index.pointer_terms;
        return 1;
    }
    if (definition->opcode == MIR_BINARY &&
        (definition->immediate == '+' ||
         definition->immediate == '-' ||
         definition->immediate == '*' ||
         definition->immediate == '/')) {
        struct MirMachineForm left;
        struct MirMachineForm right;

        if (!mir_machine_pointer_form(
                definition->src1, definition_index,
                &left, depth + 1) ||
            !mir_machine_pointer_form(
                definition->src2, definition_index,
                &right, depth + 1)) {
            if (getenv("DCC_MIR_POINTER_REPORT") != NULL)
                fprintf(stderr,
                        "; MIR pointer function=%s value=%d "
                        "op=%ld reject=operand\n",
                        mir.name, value, definition->immediate);
            return 0;
        }
        if (left.kind == MIR_MACHINE_FORM_INTEGER &&
            right.kind == MIR_MACHINE_FORM_INTEGER) {
            long result;

            if (!mir_machine_fold_integer_binary(
                    (int)definition->immediate,
                    left.value, right.value,
                    definition->type, &result))
                return 0;
            form->kind = MIR_MACHINE_FORM_INTEGER;
            form->value = result;
            form->storage = 0;
            form->offset = 0;
            form->pointer_terms =
                left.pointer_terms + right.pointer_terms;
            form->name[0] = 0;
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
        if ((definition->immediate == '+' ||
             definition->immediate == '-') &&
            left.kind == MIR_MACHINE_FORM_POINTER &&
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
            left.offset == right.offset &&
            !strcmp(left.name, right.name)) {
            form->kind = MIR_MACHINE_FORM_INTEGER;
            form->value = left.value - right.value;
            form->storage = 0;
            form->offset = 0;
            form->pointer_terms =
                left.pointer_terms + right.pointer_terms;
            form->name[0] = 0;
            return 1;
        }
    }
    if (getenv("DCC_MIR_POINTER_REPORT") != NULL)
        fprintf(stderr,
                "; MIR pointer function=%s value=%d opcode=%d "
                "reject=form\n",
                mir.name, value, definition->opcode);
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
        case MIR_UNARY:
        case MIR_BINARY:
            break;
        case MIR_LOAD:
            if (!mir_machine_named_nonvolatile(insn))
                return 0;
            break;
        case MIR_STORE:
            if (!mir_machine_unobservable_local_store(insn))
                return 0;
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

static int mir_machine_six_call_arguments(
    const struct MirInsn *call, int arguments[6])
{
    int count = 0;
    int instruction;
    int argument;

    for (argument = 0; argument < 6; ++argument)
        arguments[argument] = -1;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *arg = &mir.insns[instruction];
        int index;

        if (arg->opcode != MIR_ARG ||
            arg->secondary_offset != call->secondary_offset)
            continue;
        index = (int)arg->immediate;
        if (index < 0 || index >= 6 || arguments[index] >= 0)
            return 0;
        arguments[index] = arg->src1;
        ++count;
    }
    return count == 6;
}

static int mir_match_constant_checks(struct MirConstantChecks *plan)
{
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
        case MIR_CONST:
        case MIR_STRING_ADDRESS:
        case MIR_ADDRESS:
        case MIR_UNARY:
        case MIR_BINARY:
        case MIR_INDEX_ADDRESS:
        case MIR_ARG:
            break;
        case MIR_LOAD:
            if (!mir_machine_named_nonvolatile(insn))
                return 0;
            break;
        case MIR_STORE:
            if (!mir_machine_unobservable_local_store(insn))
                return 0;
            break;
        case MIR_CALL:
            {
                int arguments[3];
                const struct MirInsn *string;
                struct MirMachineForm actual;
                struct MirMachineForm expected;
                struct Sym *function;

                if (plan->count >= 16)
                    return mir_machine_reject(
                        "constant-checks", "count");
                if (!mir_machine_three_call_arguments(
                        insn, arguments))
                    return mir_machine_reject(
                        "constant-checks", "arguments");
                string = mir_definition(arguments[0]);
                function = find_global(insn->name);
                if (string == NULL ||
                    string->opcode != MIR_STRING_ADDRESS ||
                    (insn->type & 15) != TYPE_VOID ||
                    function == NULL ||
                    function->proto_nargs != 3 ||
                    type_ptr_depth(function->proto_types[0]) == 0 ||
                    type_size(function->proto_types[0]) != 2 ||
                    type_ptr_depth(function->proto_types[1]) != 0 ||
                    type_size(function->proto_types[1]) != 2 ||
                    type_ptr_depth(function->proto_types[2]) != 0 ||
                    type_size(function->proto_types[2]) != 2 ||
                    (plan->function != NULL &&
                     plan->function != function))
                    return mir_machine_reject(
                        "constant-checks", "call");
                if (!mir_machine_pointer_form(
                        arguments[1], instruction,
                        &actual, 0) ||
                    actual.kind != MIR_MACHINE_FORM_INTEGER)
                    return mir_machine_reject(
                        "constant-checks", "actual");
                if (!mir_machine_pointer_form(
                        arguments[2], instruction,
                        &expected, 0) ||
                    expected.kind != MIR_MACHINE_FORM_INTEGER)
                    return mir_machine_reject(
                        "constant-checks", "expected");
                plan->function = function;
                plan->strings[plan->count] =
                    (int)string->immediate;
                plan->actual[plan->count] =
                    actual.value & 0xffffL;
                plan->expected[plan->count] =
                    expected.value & 0xffffL;
                ++plan->count;
            }
            break;
        default:
            return mir_machine_reject(
                "constant-checks", "opcode");
        }
    }
    return plan->count > 0 && plan->function != NULL;
}

static int mir_match_constant_prints(struct MirConstantPrints *plan)
{
    int instruction;
    int return_count = 0;
    int return_position = -1;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
        case MIR_CONST:
        case MIR_STRING_ADDRESS:
        case MIR_ADDRESS:
        case MIR_UNARY:
        case MIR_BINARY:
        case MIR_INDEX_ADDRESS:
        case MIR_ARG:
            break;
        case MIR_LOAD:
            if (!mir_machine_named_nonvolatile(insn))
                return 0;
            break;
        case MIR_STORE:
            if (!mir_machine_unobservable_local_store(insn))
                return 0;
            break;
        case MIR_CALL:
            {
                int arguments[2];
                const struct MirInsn *string;
                const struct MirInsn *numeric;
                struct MirMachineForm value;
                struct Sym *function;

                if (plan->count >= 16 ||
                    !mir_machine_two_call_arguments(
                        insn, arguments))
                    return 0;
                string = mir_definition(arguments[0]);
                numeric = mir_definition(arguments[1]);
                function = find_global(insn->name);
                if (string == NULL ||
                    string->opcode != MIR_STRING_ADDRESS ||
                    numeric == NULL ||
                    type_size(numeric->type) != 2 ||
                    type_is_float(numeric->type) ||
                    strcmp(insn->name, "printf") ||
                    (insn->memory_flags &
                     MIR_CALL_FLAG_FORMAT_RUNTIME) != 0 ||
                    function == NULL ||
                    (plan->function != NULL &&
                     plan->function != function) ||
                    !mir_machine_pointer_form(
                        arguments[1], instruction, &value, 0) ||
                    value.kind != MIR_MACHINE_FORM_INTEGER)
                    return 0;
                plan->function = function;
                plan->strings[plan->count] =
                    (int)string->immediate;
                plan->values[plan->count] =
                    value.value & 0xffffL;
                ++plan->count;
            }
            break;
        case MIR_RETURN:
            if (++return_count != 1 ||
                !mir_machine_constant_equals(
                    insn->src1, 0))
                return 0;
            return_position = instruction;
            break;
        default:
            return 0;
        }
    }
    if (plan->count == 0 || plan->function == NULL ||
        return_count != 1)
        return 0;
    for (instruction = return_position + 1;
         instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != MIR_NOP)
            return 0;
    return 1;
}

static int mir_match_pointer_difference_prints(
    struct MirPointerDifferencePrints *plan)
{
    int instruction;
    int return_count = 0;
    int return_position = -1;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
        case MIR_CONST:
        case MIR_STRING_ADDRESS:
        case MIR_ADDRESS:
        case MIR_ARG:
            break;
        case MIR_LOAD:
            if (!mir_machine_named_nonvolatile(insn))
                return 0;
            break;
        case MIR_BINARY:
            if (insn->immediate != '-' ||
                type_size(insn->type) != 2)
                return 0;
            break;
        case MIR_CALL:
            {
                int arguments[2];
                const struct MirInsn *string;
                const struct MirInsn *difference;
                const struct MirInsn *left;
                const struct MirInsn *right;
                struct Sym *function;
                struct Sym *left_symbol;
                struct Sym *right_symbol = NULL;
                long right_constant = 0;

                if (plan->count >= 16 ||
                    !mir_machine_two_call_arguments(
                        insn, arguments))
                    return 0;
                string = mir_definition(arguments[0]);
                difference = mir_definition(arguments[1]);
                function = find_global(insn->name);
                if (string == NULL ||
                    string->opcode != MIR_STRING_ADDRESS ||
                    difference == NULL ||
                    difference->opcode != MIR_BINARY ||
                    difference->immediate != '-' ||
                    type_size(difference->type) != 2 ||
                    strcmp(insn->name, "printf") ||
                    (insn->memory_flags &
                     MIR_CALL_FLAG_FORMAT_RUNTIME) != 0 ||
                    function == NULL ||
                    (plan->function != NULL &&
                     plan->function != function))
                    return 0;
                left = mir_definition(difference->src1);
                right = mir_definition(difference->src2);
                left_symbol = left != NULL &&
                              left->opcode == MIR_LOAD
                    ? find_global(left->name) : NULL;
                if (left_symbol == NULL ||
                    left_symbol->is_volatile ||
                    type_size(left_symbol->type) != 2)
                    return 0;
                if (right != NULL &&
                    right->opcode == MIR_ADDRESS) {
                    right_symbol = find_global(right->name);
                    if (right_symbol == NULL)
                        return 0;
                } else if (right == NULL ||
                           right->opcode != MIR_CONST ||
                           type_size(right->type) != 2) {
                    return 0;
                } else {
                    right_constant =
                        right->immediate & 0xffffL;
                }
                plan->function = function;
                plan->strings[plan->count] =
                    (int)string->immediate;
                plan->left[plan->count] = left_symbol;
                plan->right[plan->count] = right_symbol;
                plan->right_constant[plan->count] =
                    right_constant;
                ++plan->count;
            }
            break;
        case MIR_RETURN:
            if (++return_count != 1 ||
                !mir_machine_constant_equals(insn->src1, 0))
                return 0;
            return_position = instruction;
            break;
        default:
            return 0;
        }
    }
    if (plan->count == 0 || plan->function == NULL ||
        return_count != 1)
        return 0;
    for (instruction = return_position + 1;
         instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != MIR_NOP)
            return 0;
    return 1;
}

static int mir_machine_comparison_parameter(
    int value, int *parameter_value)
{
    const struct MirInsn *definition = mir_definition(value);
    const struct MirInsn *conversions[8];
    int conversion_count = 0;
    int current_type;
    int conversion;

    while (definition != NULL &&
           definition->opcode == MIR_UNARY &&
           definition->immediate == 0) {
        if (conversion_count >= 8)
            return 0;
        conversions[conversion_count++] = definition;
        value = definition->src1;
        definition = mir_definition(value);
    }
    if (definition == NULL || definition->opcode != MIR_PARAM ||
        type_size(definition->type) != 1 ||
        type_ptr_depth(definition->type) != 0 ||
        type_is_float(definition->type) ||
        (definition->type & 15) == TYPE_BOOL)
        return 0;
    current_type = definition->type;
    for (conversion = conversion_count - 1;
         conversion >= 0; --conversion) {
        int target_type = conversions[conversion]->type;
        int source_width = type_size(current_type);
        int target_width = type_size(target_type);

        if (type_ptr_depth(target_type) != 0 ||
            type_is_float(target_type) ||
            (target_type & 15) == TYPE_BOOL)
            return 0;
        if (source_width == 1 && target_width == 1) {
            if (target_type != current_type)
                return 0;
        } else if (source_width == 1 && target_width == 2) {
            if ((target_type & 15) != TYPE_INT ||
                (target_type & TYPE_UNSIGNED) != 0)
                return 0;
        } else if (source_width == 2 && target_width == 2) {
            if (target_type != current_type)
                return 0;
        } else {
            return 0;
        }
        current_type = target_type;
    }
    if (type_size(current_type) != 2 ||
        (current_type & TYPE_UNSIGNED) != 0)
        return 0;
    *parameter_value = definition->dst;
    return 1;
}

static int mir_machine_match_comparison_argument(
    int value, int operation, int left_parameter,
    int right_parameter)
{
    const struct MirInsn *definition = mir_definition(value);
    int left;
    int right;

    while (definition != NULL &&
           definition->opcode == MIR_UNARY &&
           definition->immediate == 0) {
        if (type_ptr_depth(definition->type) != 0 ||
            type_is_float(definition->type) ||
            type_size(definition->type) > 2)
            return 0;
        value = definition->src1;
        definition = mir_definition(value);
    }
    return definition != NULL &&
           definition->opcode == MIR_BINARY &&
           definition->immediate == operation &&
           mir_machine_comparison_parameter(
               definition->src1, &left) &&
           mir_machine_comparison_parameter(
               definition->src2, &right) &&
           left == left_parameter && right == right_parameter;
}

static int mir_match_byte_comparison_print(
    struct MirByteComparisonPrint *plan)
{
    const struct MirInsn *parameters[2] = { NULL, NULL };
    const struct MirInsn *call = NULL;
    int parameter_count = 0;
    int call_count = 0;
    int store_count = 0;
    int binary_count = 0;
    int instruction;
    int arguments[6];
    const struct MirInsn *string;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
        case MIR_STRING_ADDRESS:
        case MIR_ARG:
        case MIR_UNARY:
            break;
        case MIR_PARAM:
            if (parameter_count >= 2 ||
                type_size(insn->type) != 1 ||
                type_is_float(insn->type) ||
                type_ptr_depth(insn->type) != 0 ||
                (insn->type & 15) == TYPE_BOOL)
                return 0;
            parameters[parameter_count++] = insn;
            break;
        case MIR_BINARY:
            ++binary_count;
            break;
        case MIR_STORE:
            if (!mir_machine_unobservable_local_store(insn))
                return 0;
            ++store_count;
            break;
        case MIR_CALL:
            if (++call_count != 1 ||
                strcmp(insn->name, "printf") ||
                (insn->memory_flags &
                 MIR_CALL_FLAG_FORMAT_RUNTIME) != 0)
                return 0;
            call = insn;
            break;
        default:
            return 0;
        }
    }
    if (parameter_count != 2 || binary_count != 5 ||
        store_count != 5 || call_count != 1 || call == NULL ||
        ((parameters[0]->type & TYPE_UNSIGNED) != 0) !=
            ((parameters[1]->type & TYPE_UNSIGNED) != 0) ||
        !mir_machine_six_call_arguments(call, arguments))
        return 0;
    string = mir_definition(arguments[0]);
    plan->function = find_global(call->name);
    if (string == NULL || string->opcode != MIR_STRING_ADDRESS ||
        plan->function == NULL ||
        (call->base_name[0] != 0 &&
         strcmp(call->base_name,
                asm_name_for(sym_asm_name(plan->function)))) ||
        !mir_machine_match_comparison_argument(
            arguments[1], '<', parameters[0]->dst,
            parameters[1]->dst) ||
        !mir_machine_match_comparison_argument(
            arguments[2], TOK_LE, parameters[0]->dst,
            parameters[1]->dst) ||
        !mir_machine_match_comparison_argument(
            arguments[3], TOK_EQ, parameters[0]->dst,
            parameters[1]->dst) ||
        !mir_machine_match_comparison_argument(
            arguments[4], TOK_GE, parameters[0]->dst,
            parameters[1]->dst) ||
        !mir_machine_match_comparison_argument(
            arguments[5], '>', parameters[0]->dst,
            parameters[1]->dst) ||
        !mir_machine_parameter_value_offset(
            parameters[0]->dst, &plan->left_stack_offset) ||
        !mir_machine_parameter_value_offset(
            parameters[1]->dst, &plan->right_stack_offset))
        return 0;
    plan->is_unsigned =
        (parameters[0]->type & TYPE_UNSIGNED) != 0;
    plan->string_id = (int)string->immediate;
    return plan->function != NULL;
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

static int mir_match_constant_buffer_call_print(
    struct MirConstantBufferCallPrint *plan)
{
    const struct MirInsn *pack_call = NULL;
    const struct MirInsn *print_call = NULL;
    char root_name[64] = "";
    int root_offset = 0;
    int stores = 0;
    unsigned seen = 0;
    int calls = 0;
    int return_count = 0;
    int returned = 0;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (returned && insn->opcode != MIR_NOP)
            return 0;
        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
        case MIR_CONST:
        case MIR_ADDRESS:
        case MIR_INDEX_ADDRESS:
        case MIR_ARG:
        case MIR_STRING_ADDRESS:
            break;
        case MIR_STORE_INDIRECT:
            {
                const struct MirInsn *index =
                    mir_definition(insn->src1);
                const struct MirInsn *root;
                const struct MirInsn *subscript;
                const struct MirInsn *value =
                    mir_definition(insn->src2);
                int memory_type;
                int memory_storage;
                int memory_offset;
                int lane;

                if (calls != 0 || insn->memory_size != 1 ||
                    insn->bit_width != 0 ||
                    (insn->memory_flags & (1 | 8)) != 0 ||
                    index == NULL ||
                    index->opcode != MIR_INDEX_ADDRESS ||
                    index->immediate != 1 ||
                    value == NULL || value->opcode != MIR_CONST)
                    return 0;
                root = mir_definition(index->src1);
                subscript = mir_definition(index->src2);
                if (root == NULL || root->opcode != MIR_ADDRESS ||
                    subscript == NULL ||
                    subscript->opcode != MIR_CONST ||
                    !mir_scalar_memory_location(
                        root, &memory_type, &memory_storage,
                        &memory_offset) ||
                    memory_storage != SC_LOCAL ||
                    subscript->immediate < 0 ||
                    subscript->immediate >= 4)
                    return 0;
                lane = (int)subscript->immediate;
                if ((seen & (1U << lane)) != 0)
                    return 0;
                if (stores == 0) {
                    int declared;

                    for (declared = 0;
                         declared < mir.declared_count; ++declared)
                        if (!strcmp(
                                mir.declared_names[declared],
                                root->name))
                            break;
                    if (declared == mir.declared_count ||
                        mir.declared_is_volatile[declared])
                        return 0;
                    snprintf(root_name, sizeof(root_name), "%s",
                             root->name);
                    root_offset = memory_offset;
                } else if (strcmp(root_name, root->name) ||
                           root_offset != memory_offset) {
                    return 0;
                }
                plan->bytes[lane] =
                    (unsigned char)value->immediate;
                seen |= 1U << lane;
                ++stores;
            }
            break;
        case MIR_CALL:
            if (stores != 4)
                return 0;
            if (++calls == 1)
                pack_call = insn;
            else if (calls == 2)
                print_call = insn;
            else
                return 0;
            break;
        case MIR_RETURN:
            if (calls != 2 || ++return_count != 1 ||
                !mir_machine_constant_equals(insn->src1, 0))
                return 0;
            returned = 1;
            break;
        default:
            return 0;
        }
    }
    if (stores != 4 || calls != 2 ||
        return_count != 1 || pack_call == NULL ||
        print_call == NULL || seen != 15U)
        return 0;
    {
        int pack_argument;
        int print_arguments[2];
        const struct MirInsn *pack_address;
        const struct MirInsn *print_string;

        if (!mir_machine_single_call_argument(
                pack_call, &pack_argument) ||
            !mir_machine_two_call_arguments(
                print_call, print_arguments))
            return 0;
        pack_address = mir_definition(pack_argument);
        print_string = mir_definition(print_arguments[0]);
        if (pack_address == NULL ||
            pack_address->opcode != MIR_ADDRESS ||
            strcmp(pack_address->name, root_name) ||
            type_size(pack_call->type) != 4 ||
            type_is_float(pack_call->type) ||
            print_string == NULL ||
            print_string->opcode != MIR_STRING_ADDRESS ||
            print_arguments[1] != pack_call->dst ||
            strcmp(print_call->base_name, "_pflng") ||
            (print_call->memory_flags &
             MIR_CALL_FLAG_FORMAT_RUNTIME) != 0)
            return 0;
        plan->pack_function = find_global(pack_call->name);
        if (plan->pack_function == NULL)
            return 0;
        snprintf(plan->print_name,
                 sizeof(plan->print_name), "%s",
                 print_call->base_name);
        plan->string_id = (int)print_string->immediate;
    }
    return 1;
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

static void mir_emit_constant_checks(
    FILE *out, const struct MirConstantChecks *plan)
{
    int check;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (check = 0; check < plan->count; ++check) {
        fprintf(out,
                "\tld hl,%ld\n\tpush hl\n"
                "\tld hl,%ld\n\tpush hl\n"
                "\tld hl,S%d\n\tpush hl\n",
                plan->expected[check],
                plan->actual[check],
                plan->strings[check]);
        mir_machine_emit_symbol_call(out, plan->function);
        fputs("\tpop bc\n\tpop bc\n\tpop bc\n", out);
    }
    fputs("\tret\n", out);
}

static void mir_emit_constant_prints(
    FILE *out, const struct MirConstantPrints *plan)
{
    int call;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (call = 0; call < plan->count; ++call) {
        fprintf(out,
                "\tld hl,%ld\n\tpush hl\n"
                "\tld hl,S%d\n\tpush hl\n",
                plan->values[call], plan->strings[call]);
        mir_machine_emit_symbol_call(out, plan->function);
        fputs("\tpop bc\n\tpop bc\n", out);
    }
    fputs("\tld hl,0\n\tret\n", out);
}

static void mir_emit_pointer_difference_prints(
    FILE *out, const struct MirPointerDifferencePrints *plan)
{
    int call;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (call = 0; call < plan->count; ++call) {
        mir_machine_emit_global_word(
            out, plan->left[call], 0);
        if (plan->right[call] != NULL)
            mir_machine_emit_global_address_de(
                out, plan->right[call], 0);
        else
            fprintf(out, "\tld de,%ld\n",
                    plan->right_constant[call]);
        fputs("\tor a\n\tsbc hl,de\n\tpush hl\n", out);
        fprintf(out, "\tld hl,S%d\n\tpush hl\n",
                plan->strings[call]);
        mir_machine_emit_symbol_call(out, plan->function);
        fputs("\tpop bc\n\tpop bc\n", out);
    }
    fputs("\tld hl,0\n\tret\n", out);
}

static void mir_machine_emit_byte_comparison_push(
    FILE *out, const struct MirByteComparisonPrint *plan,
    int operation, int swap, int pushed_words)
{
    int left_offset = swap
        ? plan->right_stack_offset : plan->left_stack_offset;
    int right_offset = swap
        ? plan->left_stack_offset : plan->right_stack_offset;
    int true_label = new_label();
    int end_label = new_label();

    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld c,(hl)\n"
            "\tld hl,%d\n\tadd hl,sp\n\tld e,(hl)\n",
            left_offset + pushed_words * 2,
            right_offset + pushed_words * 2);
    if (plan->is_unsigned) {
        fputs("\tld b,0\n\tld d,0\n", out);
    } else {
        fputs("\tld a,c\n\trlca\n\tsbc a,a\n\tld b,a\n"
              "\tld a,e\n\trlca\n\tsbc a,a\n\tld d,a\n", out);
    }
    if (operation == TOK_EQ) {
        fputs("\tld a,c\n\txor e\n\tld l,a\n"
              "\tld a,b\n\txor d\n\tor l\n", out);
        fprintf(out, "\tjp z,L%d\n", true_label);
    } else {
        if (!plan->is_unsigned)
            fputs("\tld a,b\n\txor 128\n\tld b,a\n"
                  "\tld a,d\n\txor 128\n\tld d,a\n", out);
        fputs("\tld a,c\n\tsub e\n\tld a,b\n\tsbc a,d\n", out);
        fprintf(out, operation == '<'
                    ? "\tjp c,L%d\n" : "\tjp nc,L%d\n",
                true_label);
    }
    fprintf(out,
            "\tld hl,0\n\tjp L%d\n"
            "L%d:\n\tld hl,1\nL%d:\n\tpush hl\n",
            end_label, true_label, end_label);
}

static void mir_emit_byte_comparison_print(
    FILE *out, const struct MirByteComparisonPrint *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_byte_comparison_push(
        out, plan, '<', 1, 0);
    mir_machine_emit_byte_comparison_push(
        out, plan, TOK_GE, 0, 1);
    mir_machine_emit_byte_comparison_push(
        out, plan, TOK_EQ, 0, 2);
    mir_machine_emit_byte_comparison_push(
        out, plan, TOK_GE, 1, 3);
    mir_machine_emit_byte_comparison_push(
        out, plan, '<', 0, 4);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->string_id);
    mir_machine_emit_symbol_call(out, plan->function);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tpop bc\n\tpop bc\n\tpop bc\n\tret\n", out);
}

static void mir_emit_constant_buffer_call_print(
    FILE *out, const struct MirConstantBufferCallPrint *plan)
{
    unsigned int first =
        (unsigned int)plan->bytes[0] |
        ((unsigned int)plan->bytes[1] << 8);
    unsigned int second =
        (unsigned int)plan->bytes[2] |
        ((unsigned int)plan->bytes[3] << 8);

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%u\n\tpush hl\n"
            "\tld hl,%u\n\tpush hl\n"
            "\tld hl,0\n\tadd hl,sp\n\tpush hl\n",
            second, first);
    mir_machine_emit_symbol_call(out, plan->pack_function);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tpush de\n\tpush hl\n", out);
    fprintf(out,
            "\tld hl,S%d\n\tpush hl\n"
            "\textrn %s\n\tcall %s\n"
            "\tpop bc\n\tpop bc\n\tpop bc\n"
            "\tld hl,0\n\tret\n",
            plan->string_id,
            plan->print_name, plan->print_name);
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
    int memory_type;
    int memory_storage;
    int memory_offset;

    if (definition != NULL && definition->opcode == MIR_UNARY &&
        definition->immediate == 0) {
        if ((definition->type & 15) == TYPE_BOOL)
            return 0;
        value = definition->src1;
        definition = mir_definition(value);
    }
    if (definition == NULL || definition->opcode != MIR_PARAM ||
        !mir_scalar_memory_location(
            definition, &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_PARAM ||
        (type_size(memory_type) != 1 &&
         type_size(memory_type) != 2))
        return 0;
    *stack_offset = memory_offset - 2;
    return *stack_offset >= 0;
}

static int mir_machine_transparent_pointer_unary(
    const struct MirInsn *unary)
{
    const struct MirInsn *source;

    if (unary == NULL || unary->opcode != MIR_UNARY ||
        unary->immediate != 0 ||
        type_ptr_depth(unary->type) == 0 ||
        type_size(unary->type) != 2)
        return 0;
    source = mir_definition(unary->src1);
    return source != NULL &&
           type_ptr_depth(source->type) ==
               type_ptr_depth(unary->type) &&
           type_size(source->type) == 2;
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

static int mir_machine_global_address_offset(
    int value, struct Sym **root_out, long *offset_out, int depth)
{
    const struct MirInsn *definition;
    int memory_type;
    int memory_storage;
    int memory_offset;

    if (depth > 16)
        return 0;
    definition = mir_definition(value);
    if (definition == NULL)
        return 0;
    if (mir_machine_transparent_pointer_unary(definition))
        return mir_machine_global_address_offset(
            definition->src1, root_out, offset_out, depth + 1);
    if (definition->opcode == MIR_ADDRESS) {
        struct Sym *root;

        if (!mir_scalar_memory_location(
                definition, &memory_type, &memory_storage,
                &memory_offset) ||
            memory_storage != SC_GLOBAL)
            return 0;
        root = find_global(definition->name);
        if (root == NULL || !root->is_defined || root->is_volatile)
            return 0;
        *root_out = root;
        *offset_out = memory_offset;
        return 1;
    }
    if (definition->opcode == MIR_BINARY &&
        definition->immediate == '+') {
        long constant;
        long base_offset;

        if (mir_machine_global_address_offset(
                definition->src1, root_out, &base_offset,
                depth + 1) &&
            mir_machine_constant_value(
                definition->src2, &constant, 0)) {
            *offset_out = base_offset + constant;
            return *offset_out >= -32768 && *offset_out <= 32767;
        }
        if (mir_machine_constant_value(
                definition->src1, &constant, 0) &&
            mir_machine_global_address_offset(
                definition->src2, root_out, &base_offset,
                depth + 1)) {
            *offset_out = base_offset + constant;
            return *offset_out >= -32768 && *offset_out <= 32767;
        }
    }
    return 0;
}

static int mir_machine_byte_cursor_update(
    const struct MirInsn *store, int operation,
    struct Sym **root_out, int *offset_out,
    int *old_value, int *new_value,
    const struct MirInsn **load_out)
{
    const struct MirInsn *binary;
    const struct MirInsn *load;
    const struct MirInsn *one;
    struct Sym *store_root;
    struct Sym *load_root;
    int store_offset;
    int load_offset;

    if (store == NULL || store->opcode != MIR_STORE_INDIRECT ||
        store->memory_size != 1 || store->bit_width != 0 ||
        (store->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_global_member(
            store->src1, &store_root, &store_offset))
        return 0;
    binary = mir_definition(store->src2);
    if (binary == NULL || binary->opcode != MIR_BINARY ||
        binary->immediate != operation)
        return 0;
    load = mir_definition(binary->src1);
    one = mir_definition(binary->src2);
    if (load == NULL || load->opcode != MIR_LOAD_INDIRECT ||
        load->memory_size != 1 || load->bit_width != 0 ||
        (load->type & TYPE_UNSIGNED) == 0 ||
        (load->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_global_member(
            load->src1, &load_root, &load_offset) ||
        load_root != store_root || load_offset != store_offset ||
        one == NULL || one->opcode != MIR_CONST ||
        one->immediate != 1)
        return 0;
    *root_out = store_root;
    *offset_out = store_offset;
    *old_value = load->dst;
    *new_value = binary->dst;
    if (load_out != NULL)
        *load_out = load;
    return 1;
}

static int mir_machine_byte_stack_address(
    int value, int cursor_value, struct Sym **root_out,
    int *offset_out)
{
    const struct MirInsn *address = mir_definition(value);
    long offset;

    if (mir_machine_transparent_pointer_unary(address)) {
        value = address->src1;
        address = mir_definition(value);
    }
    if (address == NULL || address->opcode != MIR_BINARY ||
        address->immediate != '+')
        return 0;
    if (address->src1 == cursor_value &&
        mir_machine_global_address_offset(
            address->src2, root_out, &offset, 0)) {
        *offset_out = (int)offset;
        return 1;
    }
    if (address->src2 == cursor_value &&
        mir_machine_global_address_offset(
            address->src1, root_out, &offset, 0)) {
        *offset_out = (int)offset;
        return 1;
    }
    return 0;
}

static int mir_match_byte_memory_stack(
    struct MirByteMemoryStack *plan)
{
    const struct MirInsn *cursor_store = NULL;
    const struct MirInsn *element_store = NULL;
    const struct MirInsn *return_insn = NULL;
    const struct MirInsn *element_load;
    int old_cursor = -1;
    int new_cursor = -1;
    int parameter_count = 0;
    int store_count = 0;
    int return_count = 0;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir_cfg_block_count() != 1)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
        case MIR_CONST:
        case MIR_ADDRESS:
        case MIR_MEMBER_ADDRESS:
        case MIR_UNARY:
        case MIR_BINARY:
            break;
        case MIR_PARAM:
            ++parameter_count;
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
                cursor_store = insn;
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
        return_count == 0 && cursor_store != NULL &&
        element_store != NULL &&
        (mir.return_type & 15) == TYPE_VOID) {
        plan->kind = MIR_BYTE_MEMORY_STACK_PUSH;
        if (!mir_machine_byte_cursor_update(
                cursor_store, '-', &plan->cursor_root,
                &plan->cursor_offset, &old_cursor,
                &new_cursor, NULL) ||
            !mir_machine_byte_stack_address(
                element_store->src1, old_cursor,
                &plan->memory_root, &plan->memory_offset) ||
            cursor_store >= element_store ||
            element_store->memory_size != 1 ||
            !mir_machine_parameter_value_offset(
                element_store->src2,
                &plan->value_stack_offset))
            return 0;
    } else if (store_count == 1 && parameter_count == 0 &&
               return_count == 1 && cursor_store != NULL &&
               return_insn != NULL &&
               type_size(mir.return_type) == 1 &&
               (mir.return_type & TYPE_UNSIGNED) != 0) {
        plan->kind = MIR_BYTE_MEMORY_STACK_POP;
        element_load = mir_definition(return_insn->src1);
        if (!mir_machine_byte_cursor_update(
                cursor_store, '+', &plan->cursor_root,
                &plan->cursor_offset, &old_cursor,
                &new_cursor, NULL) ||
            element_load == NULL ||
            element_load->opcode != MIR_LOAD_INDIRECT ||
            element_load->memory_size != 1 ||
            element_load->bit_width != 0 ||
            (element_load->memory_flags & (1 | 8)) != 0 ||
            !mir_machine_byte_stack_address(
                element_load->src1, new_cursor,
                &plan->memory_root, &plan->memory_offset) ||
            cursor_store >= element_load)
            return 0;
    } else {
        return 0;
    }
    return plan->memory_root != NULL &&
           plan->cursor_root != NULL;
}

static int mir_match_word_memory_stack_push(
    struct MirByteMemoryStack *plan)
{
    const struct MirInsn *cursor_stores[2] = { NULL, NULL };
    const struct MirInsn *element_store = NULL;
    const struct MirInsn *second_load = NULL;
    struct Sym *first_cursor_root;
    struct Sym *second_cursor_root;
    int first_cursor_offset;
    int second_cursor_offset;
    int first_old;
    int first_new;
    int second_old;
    int second_new;
    int cursor_store_count = 0;
    int parameter_count = 0;
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
        case MIR_CONST:
        case MIR_ADDRESS:
        case MIR_MEMBER_ADDRESS:
        case MIR_UNARY:
        case MIR_BINARY:
            break;
        case MIR_PARAM:
            ++parameter_count;
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
            if (mir_definition(insn->src1) != NULL &&
                mir_definition(insn->src1)->opcode ==
                    MIR_MEMBER_ADDRESS) {
                if (cursor_store_count >= 2)
                    return 0;
                cursor_stores[cursor_store_count++] = insn;
            } else {
                if (element_store != NULL)
                    return 0;
                element_store = insn;
            }
            break;
        default:
            return 0;
        }
    }
    if (parameter_count != 1 || cursor_store_count != 2 ||
        element_store == NULL ||
        !mir_machine_byte_cursor_update(
            cursor_stores[0], '-', &first_cursor_root,
            &first_cursor_offset, &first_old, &first_new, NULL) ||
        !mir_machine_byte_cursor_update(
            cursor_stores[1], '-', &second_cursor_root,
            &second_cursor_offset, &second_old, &second_new,
            &second_load) ||
        first_cursor_root != second_cursor_root ||
        first_cursor_offset != second_cursor_offset ||
        cursor_stores[0] >= element_store ||
        element_store >= second_load ||
        second_load >= cursor_stores[1] ||
        element_store->memory_size != 2 ||
        !mir_machine_byte_stack_address(
            element_store->src1, first_new,
            &plan->memory_root, &plan->memory_offset) ||
        !mir_machine_parameter_value_offset(
            element_store->src2, &plan->value_stack_offset))
        return 0;
    plan->kind = MIR_BYTE_MEMORY_STACK_PUSH_WORD;
    plan->cursor_root = first_cursor_root;
    plan->cursor_offset = first_cursor_offset;
    return 1;
}

static int mir_machine_named_nonvolatile(const struct MirInsn *insn)
{
    int declared;
    struct Sym *global;

    if (insn == NULL || (insn->memory_flags & (1 | 8)) != 0)
        return 0;
    for (declared = 0; declared < mir.declared_count; ++declared)
        if (!strcmp(mir.declared_names[declared], insn->name))
            return !mir.declared_is_volatile[declared];
    global = find_global(insn->name);
    return global != NULL && !global->is_volatile;
}

static int mir_machine_value_object(int value)
{
    const struct MirInsn *definition = mir_definition(value);

    if (definition == NULL)
        return -1;
    if (definition->opcode == MIR_UNARY &&
        definition->immediate == 0) {
        const struct MirInsn *source =
            mir_definition(definition->src1);
        int source_width;
        int target_width;

        if (source == NULL ||
            type_ptr_depth(source->type) != 0 ||
            type_ptr_depth(definition->type) != 0 ||
            type_is_float(source->type) ||
            type_is_float(definition->type) ||
            (source->type & 15) == TYPE_BOOL ||
            (definition->type & 15) == TYPE_BOOL)
            return -1;
        source_width = type_size(source->type);
        target_width = type_size(definition->type);
        if (target_width != source_width &&
            !(source_width == 1 && target_width == 2))
            return -1;
        return mir_machine_value_object(definition->src1);
    }
    if (definition->opcode != MIR_LOAD &&
        definition->opcode != MIR_PHI &&
        definition->opcode != MIR_CONST)
        return -1;
    return definition->object;
}

static int mir_machine_constant_equals(int value, long expected)
{
    const struct MirInsn *constant = mir_definition(value);

    return constant != NULL && constant->opcode == MIR_CONST &&
           constant->immediate == expected;
}

static int mir_machine_reduction_operand(
    int value, const struct MirInsn *element_load,
    int result_width, int *is_unsigned)
{
    const struct MirInsn *conversions[8];
    int conversion_count = 0;
    int current_type;
    int current_width;
    int conversion;

    while (value != element_load->dst) {
        const struct MirInsn *definition = mir_definition(value);

        if (definition == NULL || definition->opcode != MIR_UNARY ||
            definition->immediate != 0 || conversion_count >= 8)
            return 0;
        conversions[conversion_count++] = definition;
        value = definition->src1;
    }
    current_type = element_load->type;
    current_width = type_size(current_type);
    if (type_ptr_depth(current_type) != 0 ||
        type_is_float(current_type) ||
        (current_type & 15) == TYPE_BOOL)
        return 0;
    *is_unsigned = (current_type & TYPE_UNSIGNED) != 0;
    for (conversion = conversion_count - 1;
         conversion >= 0; --conversion) {
        int target_type = conversions[conversion]->type;
        int target_width = type_size(target_type);

        if (type_ptr_depth(target_type) != 0 ||
            type_is_float(target_type) ||
            (target_type & 15) == TYPE_BOOL)
            return 0;
        if (target_width == current_width) {
            current_type = target_type;
            if (current_width == 1)
                *is_unsigned =
                    (current_type & TYPE_UNSIGNED) != 0;
        } else if (current_width == 1 &&
                   target_width == 2) {
            current_type = target_type;
            current_width = target_width;
        } else {
            return 0;
        }
    }
    return current_width == result_width;
}

static int mir_machine_find_branch_for_value(
    int value, int *branch_position)
{
    int instruction;
    int found = 0;

    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_BRANCH_FALSE &&
            mir.insns[instruction].src1 == value) {
            *branch_position = instruction;
            ++found;
        }
    return found == 1;
}

static int mir_machine_find_header_before(int position, int *label)
{
    int instruction;

    for (instruction = position - 1; instruction >= 0; --instruction)
        if (mir.insns[instruction].opcode == MIR_LABEL) {
            *label = mir.insns[instruction].label;
            return 1;
        }
    return 0;
}

static int mir_match_fixed_array_reduction(
    struct MirFixedArrayReduction *plan)
{
    const struct MirInsn *parameter = NULL;
    const struct MirInsn *element_load = NULL;
    const struct MirInsn *return_insn = NULL;
    const struct MirInsn *sum_update_store = NULL;
    const struct MirInsn *sum_init_store = NULL;
    const struct MirInsn *outer_phi = NULL;
    const struct MirInsn *previous_index = NULL;
    int index_objects[6];
    int init_positions[6];
    int compare_positions[6];
    int branch_positions[6];
    int increment_positions[6];
    int jump_positions[6];
    int exit_positions[6];
    int header_labels[6];
    int init_values[6];
    int increment_values[6];
    int sum_object = -1;
    int sum_init_position = -1;
    int sum_update_position = -1;
    int parameter_count = 0;
    int index_count = 0;
    int label_count = 0;
    int phi_count = 0;
    int binary_count = 0;
    int branch_count = 0;
    int jump_count = 0;
    int return_count = 0;
    int load_indirect_count = 0;
    int store_count = 0;
    int instruction;
    int loop;

    memset(plan, 0, sizeof(*plan));
    for (loop = 0; loop < 6; ++loop) {
        index_objects[loop] = -1;
        init_positions[loop] = -1;
        compare_positions[loop] = -1;
        branch_positions[loop] = -1;
        increment_positions[loop] = -1;
        jump_positions[loop] = -1;
        exit_positions[loop] = -1;
        header_labels[loop] = -1;
        init_values[loop] = -1;
        increment_values[loop] = -1;
    }
    if (mir.has_vla || mir_cfg_block_count() != 19 ||
        type_ptr_depth(mir.return_type) != 0 ||
        type_is_float(mir.return_type))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
            break;
        case MIR_LABEL:
            ++label_count;
            break;
        case MIR_PARAM:
            ++parameter_count;
            parameter = insn;
            break;
        case MIR_CONST:
            if (insn->immediate < 0 || insn->immediate > 2)
                return 0;
            break;
        case MIR_LOAD:
            if (!mir_machine_named_nonvolatile(insn))
                return 0;
            break;
        case MIR_STORE:
            if (!mir_machine_unobservable_local_store(insn))
                return 0;
            ++store_count;
            break;
        case MIR_PHI:
            ++phi_count;
            outer_phi = insn;
            break;
        case MIR_BINARY:
            ++binary_count;
            break;
        case MIR_BRANCH_FALSE:
            ++branch_count;
            break;
        case MIR_JUMP:
            ++jump_count;
            break;
        case MIR_INDEX_ADDRESS:
            if (index_count >= 6 ||
                (index_count == 0
                     ? parameter == NULL ||
                       insn->src1 != parameter->dst
                     : previous_index == NULL ||
                       insn->src1 != previous_index->dst)) {
                return 0;
            }
            index_objects[index_count] =
                mir_machine_value_object(insn->src2);
            if (index_objects[index_count] < 0)
                return 0;
            previous_index = insn;
            ++index_count;
            break;
        case MIR_LOAD_INDIRECT:
            if (element_load != NULL || insn->bit_width != 0 ||
                (insn->memory_flags & (1 | 8)) != 0)
                return 0;
            element_load = insn;
            ++load_indirect_count;
            break;
        case MIR_UNARY:
            if (insn->immediate != 0)
                return 0;
            break;
        case MIR_RETURN:
            ++return_count;
            return_insn = insn;
            break;
        default:
            return 0;
        }
    }
    if (parameter_count != 1 || parameter == NULL ||
        type_ptr_depth(parameter->type) == 0 ||
        !mir_machine_parameter_offset(
            parameter->dst, &plan->parameter_stack_offset) ||
        index_count != 6 || previous_index == NULL ||
        element_load == NULL || load_indirect_count != 1 ||
        element_load->src1 != previous_index->dst ||
        (element_load->memory_size != 1 &&
         element_load->memory_size != 2 &&
         element_load->memory_size != 4) ||
        return_count != 1 || return_insn == NULL ||
        label_count != 19 || phi_count != 1 ||
        binary_count != 13 || branch_count != 6 ||
        jump_count != 6 || store_count != 14)
        return 0;
    plan->element_width = element_load->memory_size;
    plan->element_is_unsigned = 0;
    if ((plan->element_width == 1 &&
         type_size(mir.return_type) != 2) ||
        (plan->element_width != 1 &&
         type_size(mir.return_type) != plan->element_width))
        return 0;
    previous_index = NULL;
    index_count = 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode != MIR_INDEX_ADDRESS)
            continue;
        if (insn->immediate !=
            plan->element_width * (1 << (5 - index_count)))
            return 0;
        previous_index = insn;
        ++index_count;
    }
    {
        const struct MirInsn *return_value =
            mir_definition(return_insn->src1);
        if (return_value == NULL ||
            return_value->opcode != MIR_LOAD ||
            !mir_machine_named_nonvolatile(return_value))
            return 0;
        sum_object = return_value->object;
    }
    if (sum_object < 0)
        return 0;
    for (loop = 0; loop < 6; ++loop) {
        int other;

        if (index_objects[loop] < 0 ||
            index_objects[loop] == sum_object)
            return 0;
        for (other = 0; other < loop; ++other)
            if (index_objects[other] == index_objects[loop])
                return 0;
    }
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode == MIR_STORE) {
            const struct MirInsn *value = mir_definition(insn->src1);
            int object = insn->object;
            int object_loop = -1;

            for (loop = 0; loop < 6; ++loop)
                if (index_objects[loop] == object)
                    object_loop = loop;
            if (object == sum_object) {
                if (mir_machine_constant_equals(insn->src1, 0)) {
                    if (sum_init_store != NULL)
                        return 0;
                    sum_init_store = insn;
                    sum_init_position = instruction;
                } else {
                    const struct MirInsn *left;
                    const struct MirInsn *right;

                    if (sum_update_store != NULL || value == NULL ||
                        value->opcode != MIR_BINARY ||
                        value->immediate != '+')
                        return 0;
                    left = mir_definition(value->src1);
                    right = mir_definition(value->src2);
                    if (left == NULL || left->opcode != MIR_LOAD ||
                        left->object != sum_object ||
                        !mir_machine_reduction_operand(
                            value->src2, element_load,
                            type_size(mir.return_type),
                            &plan->element_is_unsigned)) {
                        if (right == NULL ||
                            right->opcode != MIR_LOAD ||
                            right->object != sum_object ||
                            !mir_machine_reduction_operand(
                                value->src1, element_load,
                                type_size(mir.return_type),
                                &plan->element_is_unsigned))
                            return 0;
                    }
                    sum_update_store = insn;
                    sum_update_position = instruction;
                }
            } else if (object_loop >= 0) {
                if (mir_machine_constant_equals(insn->src1, 0)) {
                    if (init_positions[object_loop] >= 0)
                        return 0;
                    init_positions[object_loop] = instruction;
                    init_values[object_loop] = insn->src1;
                } else {
                    const struct MirInsn *add = value;

                    if (increment_positions[object_loop] >= 0 ||
                        add == NULL || add->opcode != MIR_BINARY ||
                        add->immediate != '+' ||
                        mir_machine_value_object(add->src1) != object ||
                        !mir_machine_constant_equals(add->src2, 1))
                        return 0;
                    increment_positions[object_loop] = instruction;
                    increment_values[object_loop] = add->dst;
                }
            } else {
                return 0;
            }
        } else if (insn->opcode == MIR_BINARY &&
                   insn->immediate == '<') {
            int object = mir_machine_value_object(insn->src1);

            for (loop = 0; loop < 6; ++loop)
                if (index_objects[loop] == object)
                    break;
            if (loop == 6 ||
                compare_positions[loop] >= 0 ||
                !mir_machine_constant_equals(insn->src2, 2) ||
                !mir_machine_find_branch_for_value(
                    insn->dst, &branch_positions[loop]) ||
                !mir_machine_find_header_before(
                    instruction, &header_labels[loop]))
                return 0;
            compare_positions[loop] = instruction;
        }
    }
    if (sum_init_store == NULL || sum_update_store == NULL ||
        sum_init_position < 0 || sum_update_position < 0 ||
        outer_phi == NULL ||
        outer_phi->object != index_objects[0] ||
        outer_phi->src1 != init_values[0] ||
        outer_phi->src2 != increment_values[0])
        return 0;
    for (loop = 0; loop < 6; ++loop) {
        int jump;

        if (init_positions[loop] < 0 ||
            compare_positions[loop] < 0 ||
            branch_positions[loop] != compare_positions[loop] + 1 ||
            increment_positions[loop] < 0)
            return 0;
        jump = increment_positions[loop] + 1;
        if (jump >= mir.count ||
            mir.insns[jump].opcode != MIR_JUMP ||
            mir.insns[jump].label != header_labels[loop])
            return 0;
        jump_positions[loop] = jump;
        exit_positions[loop] =
            mir_find_label(mir.insns[branch_positions[loop]].label);
        if (exit_positions[loop] <= jump_positions[loop])
            return 0;
        if (loop == 0) {
            if (init_positions[loop] >= compare_positions[loop])
                return 0;
        } else if (branch_positions[loop - 1] >=
                       init_positions[loop] ||
                   init_positions[loop] >=
                       compare_positions[loop]) {
            return 0;
        }
    }
    if (sum_init_position >= init_positions[0] ||
        sum_init_position >= compare_positions[0])
        return 0;
    if (branch_positions[5] >= sum_update_position ||
        sum_update_position >= increment_positions[5])
        return 0;
    for (loop = 5; loop > 0; --loop)
        if (exit_positions[loop] >= increment_positions[loop - 1])
            return 0;
    return exit_positions[0] <
           (int)(return_insn - mir.insns);
}

static int mir_machine_wide_parameter_offset(
    int value, int *stack_offset)
{
    const struct MirInsn *parameter = mir_definition(value);
    int memory_type;
    int memory_storage;
    int memory_offset;

    if (parameter == NULL || parameter->opcode != MIR_PARAM ||
        type_size(parameter->type) != 4 ||
        type_is_float(parameter->type) ||
        !mir_scalar_memory_location(
            parameter, &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_PARAM ||
        type_size(memory_type) != 4 ||
        type_is_float(memory_type))
        return 0;
    *stack_offset = memory_offset - 2;
    return *stack_offset >= 0;
}

static int mir_machine_pointee_is_volatile(
    const struct MirInsn *parameter)
{
    int declared;

    if (parameter == NULL || parameter->opcode != MIR_PARAM)
        return 1;
    for (declared = 0; declared < mir.declared_count; ++declared)
        if (!strcmp(
                mir.declared_names[declared], parameter->name))
            return mir.declared_pointee_is_volatile[declared];
    return 1;
}

static int mir_match_wide_member_update(
    struct MirWideMemberUpdate *plan)
{
    const struct MirInsn *store = NULL;
    const struct MirInsn *member;
    const struct MirInsn *load;
    const struct MirInsn *binary;
    const struct MirInsn *pointer_parameter;
    int parameter_count = 0;
    int load_count = 0;
    int binary_count = 0;
    int store_count = 0;
    int instruction;
    long member_offset;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
        case MIR_MEMBER_ADDRESS:
            break;
        case MIR_PARAM:
            ++parameter_count;
            break;
        case MIR_LOAD_INDIRECT:
            if (insn->memory_size != 4 ||
                insn->bit_width != 0 ||
                type_is_float(insn->type) ||
                (insn->memory_flags & (1 | 8)) != 0)
                return 0;
            ++load_count;
            break;
        case MIR_BINARY:
            ++binary_count;
            break;
        case MIR_STORE_INDIRECT:
            if (insn->memory_size != 4 ||
                insn->bit_width != 0 ||
                (insn->memory_flags & (1 | 8)) != 0)
                return 0;
            ++store_count;
            store = insn;
            break;
        default:
            return 0;
        }
    }
    if (parameter_count != 2 || load_count != 1 ||
        binary_count != 1 || store_count != 1 ||
        store == NULL)
        return 0;
    member = mir_definition(store->src1);
    binary = mir_definition(store->src2);
    if (member == NULL || member->opcode != MIR_MEMBER_ADDRESS ||
        member->bit_width != 0 ||
        (member->memory_flags & (1 | 8)) != 0 ||
        binary == NULL || binary->opcode != MIR_BINARY ||
        (binary->immediate != '+' && binary->immediate != '-') ||
        type_size(binary->type) != 4 ||
        type_is_float(binary->type))
        return 0;
    load = mir_definition(binary->src1);
    pointer_parameter = mir_definition(member->src1);
    if (load == NULL || load->opcode != MIR_LOAD_INDIRECT ||
        load->src1 != member->dst ||
        load->memory_size != 4 || load->bit_width != 0 ||
        type_is_float(load->type) ||
        (load->memory_flags & (1 | 8)) != 0 ||
        mir_machine_pointee_is_volatile(pointer_parameter) ||
        !mir_machine_parameter_address(
            member->src1, &plan->pointer_stack_offset,
            &member_offset, 0) ||
        member_offset != 0 ||
        !mir_machine_wide_parameter_offset(
            binary->src2, &plan->value_stack_offset))
        return 0;
    if (member->immediate < -32768 ||
        member->immediate > 32767)
        return 0;
    plan->member_offset = (int)member->immediate;
    plan->operation = (int)binary->immediate;
    return 1;
}

static int mir_machine_parameter_alias_offset(
    int value, int *stack_offset, int depth)
{
    const struct MirInsn *definition;
    int memory_type;
    int memory_storage;
    int memory_offset;

    if (depth > 8)
        return 0;
    definition = mir_definition(value);
    if (definition == NULL)
        return 0;
    if (definition->opcode == MIR_PARAM)
        return mir_machine_parameter_offset(
            value, stack_offset);
    if (definition->opcode == MIR_LOAD) {
        const struct MirInsn *alias;

        if (mir_scalar_memory_location(
                definition, &memory_type, &memory_storage,
                &memory_offset) &&
            memory_storage == SC_PARAM &&
            type_ptr_depth(memory_type) > 0) {
            *stack_offset = memory_offset - 2;
            return *stack_offset >= 0;
        }
        alias = mir_machine_resolve_local_alias(value);
        return alias != NULL &&
               mir_machine_parameter_alias_offset(
                   alias->dst, stack_offset, depth + 1);
    }
    if (mir_machine_transparent_pointer_unary(definition))
        return mir_machine_parameter_alias_offset(
            definition->src1, stack_offset, depth + 1);
    return 0;
}

static int mir_machine_signed_member_load(
    int value, int *pointer_stack_offset, int *member_offset)
{
    const struct MirInsn *widen = mir_definition(value);
    const struct MirInsn *load;
    const struct MirInsn *member;
    int instruction;

    if (widen == NULL || widen->opcode != MIR_UNARY ||
        widen->immediate != 0 ||
        type_size(widen->type) != 4 ||
        type_is_float(widen->type))
        return 0;
    load = mir_definition(widen->src1);
    if (load == NULL || load->opcode != MIR_LOAD_INDIRECT ||
        load->memory_size != 2 || load->bit_width != 0 ||
        type_size(load->type) != 2 ||
        type_ptr_depth(load->type) != 0 ||
        type_is_float(load->type) ||
        (load->type & TYPE_UNSIGNED) != 0 ||
        (load->memory_flags & (1 | 8)) != 0 ||
        (member = mir_definition(load->src1)) == NULL ||
        member->opcode != MIR_MEMBER_ADDRESS ||
        member->bit_width != 0 ||
        (member->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_parameter_alias_offset(
            member->src1, pointer_stack_offset, 0) ||
        member->immediate < -32768 ||
        member->immediate > 32767)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *parameter = &mir.insns[instruction];
        int stack_offset;

        if (parameter->opcode == MIR_PARAM &&
            mir_machine_parameter_offset(
                parameter->dst, &stack_offset) &&
            stack_offset == *pointer_stack_offset &&
            mir_machine_pointee_is_volatile(parameter))
            return 0;
    }
    *member_offset = (int)member->immediate;
    return 1;
}

static int mir_match_signed_member_product(
    struct MirSignedMemberProduct *plan)
{
    const struct MirInsn *return_insn = NULL;
    const struct MirInsn *scaled;
    const struct MirInsn *multiply;
    const struct MirInsn *scale;
    const struct MirInsn *parameter = NULL;
    int parameter_count = 0;
    int load_count = 0;
    int store_count = 0;
    int member_count = 0;
    int load_indirect_count = 0;
    int unary_count = 0;
    int binary_count = 0;
    int return_count = 0;
    int instruction;
    int right_stack_offset;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        type_size(mir.return_type) != 4 ||
        type_is_float(mir.return_type) ||
        mir.count != 17)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
        case MIR_CONST:
            break;
        case MIR_PARAM:
            ++parameter_count;
            parameter = insn;
            break;
        case MIR_LOAD:
            if (!mir_machine_named_nonvolatile(insn))
                return 0;
            ++load_count;
            break;
        case MIR_STORE:
            if (!mir_machine_unobservable_local_store(insn))
                return 0;
            ++store_count;
            break;
        case MIR_MEMBER_ADDRESS:
            if (insn->bit_width != 0 ||
                (insn->memory_flags & (1 | 8)) != 0)
                return 0;
            ++member_count;
            break;
        case MIR_LOAD_INDIRECT:
            if (insn->memory_size != 2 ||
                insn->bit_width != 0 ||
                (insn->memory_flags & (1 | 8)) != 0)
                return 0;
            ++load_indirect_count;
            break;
        case MIR_UNARY:
            ++unary_count;
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
    if (parameter_count != 1 || parameter == NULL ||
        type_ptr_depth(parameter->type) == 0 ||
        mir_machine_pointee_is_volatile(parameter) ||
        load_count != 3 || store_count != 1 ||
        member_count != 2 || load_indirect_count != 2 ||
        unary_count != 2 || binary_count != 2 ||
        return_count != 1 || return_insn == NULL)
        return 0;
    scaled = mir_definition(return_insn->src1);
    if (scaled == NULL || scaled->opcode != MIR_BINARY ||
        scaled->immediate != '*' ||
        type_size(scaled->type) != 4 ||
        type_is_float(scaled->type))
        return 0;
    multiply = mir_definition(scaled->src1);
    scale = mir_definition(scaled->src2);
    if (multiply == NULL || multiply->opcode != MIR_BINARY ||
        multiply->immediate != '*' ||
        type_size(multiply->type) != 4 ||
        type_is_float(multiply->type) ||
        scale == NULL || scale->opcode != MIR_CONST ||
        type_size(scale->type) != 4)
        return 0;
    if (!mir_machine_signed_member_load(
            multiply->src1, &plan->pointer_stack_offset,
            &plan->left_member_offset) ||
        !mir_machine_signed_member_load(
            multiply->src2, &right_stack_offset,
            &plan->right_member_offset) ||
        right_stack_offset != plan->pointer_stack_offset)
        return 0;
    plan->scale = (unsigned long)scale->immediate;
    return 1;
}

static int mir_match_signed_member_square_scale_div(
    struct MirSignedMemberSquareScaleDiv *plan)
{
    const struct MirInsn *return_insn = NULL;
    const struct MirInsn *division;
    const struct MirInsn *scaled;
    const struct MirInsn *inner_multiply;
    const struct MirInsn *scale;
    const struct MirInsn *divisor;
    int first_member_value;
    int second_member_value;
    int member_count = 0;
    int parameter_count = 0;
    int load_count = 0;
    int store_count = 0;
    int address_count = 0;
    int member_address_count = 0;
    int load_indirect_count = 0;
    int unary_count = 0;
    int binary_count = 0;
    int return_count = 0;
    int instruction;
    int right_stack_offset;
    int right_member_offset;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        type_size(mir.return_type) != 4 ||
        type_is_float(mir.return_type))
        return 0;
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
                return 0;
            ++load_count;
            break;
        case MIR_STORE:
            if (!mir_machine_unobservable_local_store(insn))
                return 0;
            ++store_count;
            break;
        case MIR_ADDRESS:
            ++address_count;
            break;
        case MIR_MEMBER_ADDRESS:
            ++member_address_count;
            break;
        case MIR_LOAD_INDIRECT:
            if (member_count >= 2)
                return 0;
            ++member_count;
            ++load_indirect_count;
            break;
        case MIR_UNARY:
            ++unary_count;
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
    if (parameter_count != 1 || load_count != 3 ||
        store_count != 1 || address_count != 0 ||
        member_address_count != 2 || load_indirect_count != 2 ||
        unary_count != 2 || binary_count != 3 ||
        return_count != 1 || return_insn == NULL)
        return 0;
    division = mir_definition(return_insn->src1);
    if (division == NULL || division->opcode != MIR_BINARY ||
        division->immediate != '/' ||
        type_size(division->type) != 4 ||
        type_is_float(division->type) ||
        (division->type & TYPE_UNSIGNED) != 0)
        return 0;
    scaled = mir_definition(division->src1);
    divisor = mir_definition(division->src2);
    if (scaled == NULL || scaled->opcode != MIR_BINARY ||
        scaled->immediate != '*' ||
        (scaled->type & TYPE_UNSIGNED) != 0 ||
        divisor == NULL || divisor->opcode != MIR_CONST ||
        type_size(divisor->type) != 4 ||
        type_is_float(divisor->type) ||
        (divisor->type & TYPE_UNSIGNED) != 0)
        return 0;
    inner_multiply = mir_definition(scaled->src1);
    second_member_value = scaled->src2;
    if (inner_multiply == NULL ||
        inner_multiply->opcode != MIR_BINARY ||
        inner_multiply->immediate != '*') {
        inner_multiply = mir_definition(scaled->src2);
        second_member_value = scaled->src1;
    }
    if (inner_multiply == NULL ||
        inner_multiply->opcode != MIR_BINARY ||
        inner_multiply->immediate != '*' ||
        (inner_multiply->type & TYPE_UNSIGNED) != 0)
        return 0;
    scale = mir_definition(inner_multiply->src1);
    first_member_value = inner_multiply->src2;
    if (scale == NULL || scale->opcode != MIR_CONST) {
        scale = mir_definition(inner_multiply->src2);
        first_member_value = inner_multiply->src1;
    }
    if (scale == NULL || scale->opcode != MIR_CONST ||
        (scale->type & TYPE_UNSIGNED) != 0 ||
        !mir_machine_signed_member_load(
            first_member_value,
            &plan->pointer_stack_offset,
            &plan->member_offset) ||
        !mir_machine_signed_member_load(
            second_member_value,
            &right_stack_offset, &right_member_offset) ||
        right_stack_offset != plan->pointer_stack_offset ||
        right_member_offset != plan->member_offset)
        return 0;
    plan->scale = (unsigned long)scale->immediate;
    plan->divisor = (unsigned long)divisor->immediate;
    if (plan->divisor == 0)
        return 0;
    return 1;
}

static int mir_machine_signed_parameter_widen(
    int value, int *stack_offset)
{
    const struct MirInsn *widen = mir_definition(value);
    const struct MirInsn *parameter;

    if (widen == NULL || widen->opcode != MIR_UNARY ||
        widen->immediate != 0 ||
        type_size(widen->type) != 4 ||
        type_is_float(widen->type) ||
        (widen->type & TYPE_UNSIGNED) != 0)
        return 0;
    parameter = mir_definition(widen->src1);
    return parameter != NULL &&
           parameter->opcode == MIR_PARAM &&
           type_size(parameter->type) == 2 &&
           (parameter->type & TYPE_UNSIGNED) == 0 &&
           mir_machine_parameter_offset(
               parameter->dst, stack_offset);
}

static int mir_machine_member_scale_store(
    const struct MirInsn *store, int *pointer_stack_offset,
    int *value_stack_offset, int *member_offset,
    unsigned long *divisor_out)
{
    const struct MirInsn *destination;
    const struct MirInsn *narrow;
    const struct MirInsn *division;
    const struct MirInsn *multiply;
    const struct MirInsn *divisor;
    int source_pointer_offset;
    int source_member_offset;
    int value_offset;

    if (store == NULL || store->opcode != MIR_STORE_INDIRECT ||
        store->memory_size != 2 || store->bit_width != 0 ||
        (store->memory_flags & (1 | 8)) != 0)
        return 0;
    destination = mir_definition(store->src1);
    narrow = mir_definition(store->src2);
    if (destination == NULL ||
        destination->opcode != MIR_MEMBER_ADDRESS ||
        destination->bit_width != 0 ||
        (destination->memory_flags & (1 | 8)) != 0 ||
        narrow == NULL || narrow->opcode != MIR_UNARY ||
        narrow->immediate != 0 ||
        type_size(narrow->type) != 2 ||
        (narrow->type & TYPE_UNSIGNED) != 0)
        return 0;
    division = mir_definition(narrow->src1);
    if (division == NULL || division->opcode != MIR_BINARY ||
        division->immediate != '/' ||
        type_size(division->type) != 4 ||
        type_is_float(division->type) ||
        (division->type & TYPE_UNSIGNED) != 0)
        return 0;
    multiply = mir_definition(division->src1);
    divisor = mir_definition(division->src2);
    if (multiply == NULL || multiply->opcode != MIR_BINARY ||
        multiply->immediate != '*' ||
        type_size(multiply->type) != 4 ||
        type_is_float(multiply->type) ||
        (multiply->type & TYPE_UNSIGNED) != 0 ||
        divisor == NULL || divisor->opcode != MIR_CONST ||
        type_size(divisor->type) != 4 ||
        (divisor->type & TYPE_UNSIGNED) != 0)
        return 0;
    if (!mir_machine_signed_member_load(
            multiply->src1, &source_pointer_offset,
            &source_member_offset) ||
        !mir_machine_signed_parameter_widen(
            multiply->src2, &value_offset)) {
        if (!mir_machine_signed_member_load(
                multiply->src2, &source_pointer_offset,
                &source_member_offset) ||
            !mir_machine_signed_parameter_widen(
                multiply->src1, &value_offset))
            return 0;
    }
    if (!mir_machine_parameter_alias_offset(
            destination->src1, pointer_stack_offset, 0) ||
        *pointer_stack_offset != source_pointer_offset ||
        destination->immediate != source_member_offset)
        return 0;
    *value_stack_offset = value_offset;
    *member_offset = source_member_offset;
    *divisor_out = (unsigned long)divisor->immediate;
    return *divisor_out != 0;
}

static int mir_match_signed_member_scale_pair(
    struct MirSignedMemberScalePair *plan)
{
    const struct MirInsn *stores[2] = { NULL, NULL };
    int store_count = 0;
    int parameter_count = 0;
    int load_count = 0;
    int named_store_count = 0;
    int member_count = 0;
    int load_indirect_count = 0;
    int unary_count = 0;
    int binary_count = 0;
    int constant_count = 0;
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
            break;
        case MIR_PARAM: ++parameter_count; break;
        case MIR_CONST: ++constant_count; break;
        case MIR_LOAD:
            if (!mir_machine_named_nonvolatile(insn))
                return 0;
            ++load_count;
            break;
        case MIR_STORE:
            if (!mir_machine_unobservable_local_store(insn))
                return 0;
            ++named_store_count;
            break;
        case MIR_MEMBER_ADDRESS: ++member_count; break;
        case MIR_LOAD_INDIRECT: ++load_indirect_count; break;
        case MIR_UNARY: ++unary_count; break;
        case MIR_BINARY: ++binary_count; break;
        case MIR_STORE_INDIRECT:
            if (store_count >= 2)
                return 0;
            stores[store_count++] = insn;
            break;
        default:
            return 0;
        }
    }
    if (parameter_count != 2 || load_count != 5 ||
        named_store_count != 1 || member_count != 4 ||
        load_indirect_count != 2 || unary_count != 6 ||
        binary_count != 4 || constant_count != 2 ||
        store_count != 2)
        return 0;
    for (instruction = 0; instruction < 2; ++instruction) {
        int pointer_offset;
        int value_offset;
        unsigned long divisor;

        if (!mir_machine_member_scale_store(
                stores[instruction], &pointer_offset,
                &value_offset, &plan->member_offsets[instruction],
                &divisor))
            return 0;
        if (instruction == 0) {
            plan->pointer_stack_offset = pointer_offset;
            plan->value_stack_offset = value_offset;
            plan->divisor = divisor;
        } else if (plan->pointer_stack_offset != pointer_offset ||
                   plan->value_stack_offset != value_offset ||
                   plan->divisor != divisor) {
            return 0;
        }
    }
    return plan->member_offsets[0] !=
           plan->member_offsets[1];
}

static int mir_match_wide_narrow_division(
    struct MirWideNarrowDivision *plan)
{
    const struct MirInsn *wide_parameter = NULL;
    const struct MirInsn *narrow_parameter = NULL;
    const struct MirInsn *widen = NULL;
    const struct MirInsn *binary = NULL;
    const struct MirInsn *return_insn = NULL;
    int parameter_count = 0;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        mir.count != 8 || type_size(mir.return_type) != 4 ||
        type_is_float(mir.return_type))
        return mir_machine_reject(
            "wide-narrow-division", "preflight");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
            break;
        case MIR_PARAM:
            ++parameter_count;
            if (type_size(insn->type) == 4)
                wide_parameter = insn;
            else if (type_size(insn->type) == 2)
                narrow_parameter = insn;
            else
                return mir_machine_reject(
                    "wide-narrow-division", "parameter-width");
            break;
        case MIR_UNARY:
            if (widen != NULL)
                return mir_machine_reject(
                    "wide-narrow-division", "unary-count");
            widen = insn;
            break;
        case MIR_BINARY:
            if (binary != NULL)
                return mir_machine_reject(
                    "wide-narrow-division", "binary-count");
            binary = insn;
            break;
        case MIR_RETURN:
            if (return_insn != NULL)
                return mir_machine_reject(
                    "wide-narrow-division", "return-count");
            return_insn = insn;
            break;
        default:
            return mir_machine_reject(
                "wide-narrow-division", "opcode");
        }
    }
    if (parameter_count != 2 ||
        wide_parameter == NULL || narrow_parameter == NULL ||
        widen == NULL || widen->immediate != 0 ||
        widen->src1 != narrow_parameter->dst ||
        type_size(widen->type) != 4 ||
        type_is_float(widen->type) ||
        ((widen->type & TYPE_UNSIGNED) != 0) !=
            ((narrow_parameter->type & TYPE_UNSIGNED) != 0) ||
        binary == NULL ||
        (binary->immediate != '/' && binary->immediate != '%') ||
        binary->src1 != wide_parameter->dst ||
        binary->src2 != widen->dst ||
        type_size(binary->type) != 4 ||
        type_is_float(binary->type) ||
        ((binary->type & TYPE_UNSIGNED) != 0) !=
            ((wide_parameter->type & TYPE_UNSIGNED) != 0) ||
        ((binary->type & TYPE_UNSIGNED) != 0) !=
            ((widen->type & TYPE_UNSIGNED) != 0) ||
        return_insn == NULL ||
        return_insn->src1 != binary->dst)
        return mir_machine_reject(
            "wide-narrow-division", "shape");
    if (!mir_machine_wide_parameter_offset(
            wide_parameter->dst, &plan->wide_stack_offset))
        return mir_machine_reject(
            "wide-narrow-division", "wide-parameter");
    if (!mir_machine_parameter_offset(
            narrow_parameter->dst, &plan->narrow_stack_offset))
        return mir_machine_reject(
            "wide-narrow-division", "narrow-parameter");
    plan->operation = (int)binary->immediate;
    plan->is_unsigned =
        (binary->type & TYPE_UNSIGNED) != 0;
    if (getenv("DCC_MIR_MACHINE_REPORT") != NULL)
        fprintf(stderr,
                "; MIR machine function=%s "
                "template=wide-narrow-division accept\n",
                mir.name);
    return 1;
}

static int mir_machine_aggregate_sum_leaf(
    int value, struct MirAggregateFieldSum *plan)
{
    const struct MirInsn *conversions[8];
    const struct MirInsn *load;
    const struct MirInsn *member;
    const struct MirInsn *root;
    int conversion_count = 0;
    int current_type;
    int current_width;
    int is_unsigned;
    int conversion;
    int memory_type;
    int memory_storage;
    int memory_offset;

    while ((load = mir_definition(value)) != NULL &&
           load->opcode == MIR_UNARY) {
        if (load->immediate != 0 || conversion_count >= 8)
            return 0;
        conversions[conversion_count++] = load;
        value = load->src1;
    }
    load = mir_definition(value);
    if (load == NULL || load->opcode != MIR_LOAD_INDIRECT ||
        (load->memory_size != 1 && load->memory_size != 2 &&
         load->memory_size != 4) ||
        load->bit_width != 0 ||
        type_ptr_depth(load->type) != 0 ||
        type_is_float(load->type) ||
        (load->type & 15) == TYPE_BOOL ||
        (load->memory_flags & (1 | 8)) != 0)
        return 0;
    current_type = load->type;
    current_width = type_size(current_type);
    is_unsigned = (current_type & TYPE_UNSIGNED) != 0;
    for (conversion = conversion_count - 1;
         conversion >= 0; --conversion) {
        int target_type = conversions[conversion]->type;
        int target_width = type_size(target_type);

        if (type_ptr_depth(target_type) != 0 ||
            type_is_float(target_type) ||
            (target_type & 15) == TYPE_BOOL ||
            target_width < current_width ||
            (target_width != current_width && target_width != 4))
            return 0;
        if (target_width == current_width) {
            current_type = target_type;
            is_unsigned =
                (current_type & TYPE_UNSIGNED) != 0;
        } else {
            current_type = target_type;
            current_width = target_width;
        }
    }
    if (current_width != 4 || plan->field_count >= 4)
        return 0;
    member = mir_definition(load->src1);
    root = member != NULL
        ? mir_definition(member->src1) : NULL;
    if (member == NULL || member->opcode != MIR_MEMBER_ADDRESS ||
        member->bit_width != 0 ||
        (member->memory_flags & (1 | 8)) != 0 ||
        root == NULL || root->opcode != MIR_ADDRESS ||
        !mir_scalar_memory_location(
            root, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM ||
        member->immediate < -128 ||
        member->immediate + load->memory_size - 1 > 127)
        return 0;
    if (plan->field_count == 0)
        plan->parameter_stack_offset = memory_offset - 2;
    else if (plan->parameter_stack_offset != memory_offset - 2)
        return 0;
    if (plan->parameter_stack_offset < 0)
        return 0;
    plan->fields[plan->field_count].offset =
        (int)member->immediate;
    plan->fields[plan->field_count].width =
        load->memory_size;
    plan->fields[plan->field_count].is_unsigned =
        is_unsigned;
    ++plan->field_count;
    return 1;
}

static int mir_machine_collect_aggregate_sum(
    int value, struct MirAggregateFieldSum *plan, int depth)
{
    const struct MirInsn *definition;

    if (depth > 8)
        return 0;
    definition = mir_definition(value);
    if (definition != NULL && definition->opcode == MIR_BINARY &&
        definition->immediate == '+' &&
        type_size(definition->type) == 4 &&
        !type_is_float(definition->type))
        return mir_machine_collect_aggregate_sum(
                   definition->src1, plan, depth + 1) &&
               mir_machine_collect_aggregate_sum(
                   definition->src2, plan, depth + 1);
    return mir_machine_aggregate_sum_leaf(value, plan);
}

static int mir_match_aggregate_field_sum(
    struct MirAggregateFieldSum *plan)
{
    const struct MirInsn *return_insn = NULL;
    int parameter_count = 0;
    int address_count = 0;
    int member_count = 0;
    int load_count = 0;
    int binary_count = 0;
    int return_count = 0;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        type_size(mir.return_type) != 4 ||
        type_is_float(mir.return_type))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
            break;
        case MIR_PARAM:
            ++parameter_count;
            break;
        case MIR_ADDRESS:
            ++address_count;
            break;
        case MIR_MEMBER_ADDRESS:
            ++member_count;
            break;
        case MIR_LOAD_INDIRECT:
            ++load_count;
            break;
        case MIR_UNARY:
            if (insn->immediate != 0)
                return 0;
            break;
        case MIR_BINARY:
            if (insn->immediate != '+')
                return 0;
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
    return parameter_count == 1 &&
           address_count == 3 && member_count == 3 &&
           load_count == 3 && binary_count == 2 &&
           return_count == 1 && return_insn != NULL &&
           mir_machine_collect_aggregate_sum(
               return_insn->src1, plan, 0) &&
           plan->field_count == 3;
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
        case MIR_MEMBER_ADDRESS:
        case MIR_LOAD_INDIRECT:
        case MIR_INDEX_ADDRESS:
        case MIR_BINARY:
            break;
        case MIR_LOAD:
            if (!mir_machine_named_nonvolatile(insn))
                return mir_machine_reject(
                    "nested-row-store", "load");
            break;
        case MIR_STORE:
            if (!mir_machine_unobservable_local_store(insn))
                return mir_machine_reject(
                    "nested-row-store", "store");
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

static void mir_machine_emit_global_byte_a(
    FILE *out, struct Sym *symbol, int offset, int is_store)
{
    const char *name = asm_name_for(sym_asm_name(symbol));

    if ((symbol->storage == SC_EXTERN || symbol->needs_extrn) &&
        mir_extrn_should_emit(symbol))
        fprintf(out, "\textrn %s\n", name);
    if (offset == 0)
        fprintf(out, is_store ? "\tld (%s),a\n" : "\tld a,(%s)\n",
                name);
    else
        fprintf(out, is_store
                    ? "\tld (%s%+d),a\n"
                    : "\tld a,(%s%+d)\n",
                name, offset);
}

static void mir_emit_byte_memory_stack(
    FILE *out, const struct MirByteMemoryStack *plan)
{
    mir_machine_emit_global_byte_a(
        out, plan->cursor_root, plan->cursor_offset, 0);
    fputs("\tld e,a\n", out);
    if (plan->kind == MIR_BYTE_MEMORY_STACK_PUSH ||
        plan->kind == MIR_BYTE_MEMORY_STACK_PUSH_WORD)
        fputs("\tdec a\n", out);
    else
        fputs("\tinc a\n", out);
    mir_machine_emit_global_byte_a(
        out, plan->cursor_root, plan->cursor_offset, 1);
    if (plan->kind == MIR_BYTE_MEMORY_STACK_POP ||
        plan->kind == MIR_BYTE_MEMORY_STACK_PUSH_WORD)
        fputs("\tld e,a\n", out);
    fputs("\tld d,0\n", out);
    mir_machine_emit_global_address_hl(
        out, plan->memory_root, plan->memory_offset);
    fputs("\tadd hl,de\n", out);
    if (plan->kind == MIR_BYTE_MEMORY_STACK_PUSH) {
        fputs("\tpush hl\n", out);
        fprintf(out, "\tld hl,%d\n\tadd hl,sp\n"
                     "\tld a,(hl)\n\tpop hl\n"
                     "\tld (hl),a\n\tret\n",
                plan->value_stack_offset + 2);
    } else if (plan->kind == MIR_BYTE_MEMORY_STACK_POP) {
        fputs("\tld a,(hl)\n\tld l,a\n\tld h,0\n\tret\n", out);
    } else {
        fputs("\tpush hl\n", out);
        fprintf(out, "\tld hl,%d\n\tadd hl,sp\n"
                     "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
                     "\tpop hl\n\tld (hl),e\n\tinc hl\n"
                     "\tld (hl),d\n",
                plan->value_stack_offset + 2);
        mir_machine_emit_global_byte_a(
            out, plan->cursor_root, plan->cursor_offset, 0);
        fputs("\tdec a\n", out);
        mir_machine_emit_global_byte_a(
            out, plan->cursor_root, plan->cursor_offset, 1);
        fputs("\tret\n", out);
    }
}

static void mir_emit_fixed_array_reduction(
    FILE *out, const struct MirFixedArrayReduction *plan)
{
    int loop = new_label();

    if (plan->element_width == 4) {
        fprintf(out,
                ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
                "\tpush iy\n",
                mir.name);
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
        fprintf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
                "\tpush de\n\tpop iy\n"
                "\tld hl,0\n\tld de,0\n\tld b,64\n"
                "L%d:\n"
                "\tld c,(iy+0)\n\tld a,l\n\tadd a,c\n\tld l,a\n"
                "\tld c,(iy+1)\n\tld a,h\n\tadc a,c\n\tld h,a\n"
                "\tld c,(iy+2)\n\tld a,e\n\tadc a,c\n\tld e,a\n"
                "\tld c,(iy+3)\n\tld a,d\n\tadc a,c\n\tld d,a\n"
                "\tinc iy\n\tinc iy\n\tinc iy\n\tinc iy\n"
                "\tdjnz L%d\n\tpop iy\n\tret\n",
                plan->parameter_stack_offset + 2, loop, loop);
        return;
    }
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tex de,hl\n\tld de,0\n\tld b,64\n"
            "L%d:\n",
            plan->parameter_stack_offset, loop);
    if (plan->element_width == 2) {
        fputs("\tld a,(hl)\n\tadd a,e\n\tld e,a\n\tinc hl\n"
              "\tld a,(hl)\n\tadc a,d\n\tld d,a\n\tinc hl\n",
              out);
    } else if (plan->element_is_unsigned) {
        fputs("\tld a,(hl)\n\tinc hl\n"
              "\tadd a,e\n\tld e,a\n"
              "\tld a,0\n\tadc a,d\n\tld d,a\n", out);
    } else {
        int sign_done = new_label();

        fprintf(out,
                "\tld c,(hl)\n\tinc hl\n"
                "\tld a,c\n\tadd a,e\n\tld e,a\n"
                "\tld a,0\n\tadc a,d\n"
                "\tbit 7,c\n\tjp z,L%d\n\tdec a\n"
                "L%d:\n\tld d,a\n",
                sign_done, sign_done);
    }
    fprintf(out, "\tdjnz L%d\n\tex de,hl\n\tret\n", loop);
}

static void mir_emit_wide_member_update(
    FILE *out, const struct MirWideMemberUpdate *plan)
{
    int byte;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tex de,hl\n",
            plan->pointer_stack_offset);
    mir_machine_emit_hl_offset(
        out, plan->member_offset, 0);
    fputs("\tex de,hl\n", out);
    fprintf(out, "\tld hl,%d\n\tadd hl,sp\n",
            plan->value_stack_offset);
    for (byte = 0; byte < 4; ++byte) {
        fputs("\tld a,(de)\n", out);
        if (plan->operation == '+')
            fputs(byte == 0 ? "\tadd a,(hl)\n" :
                              "\tadc a,(hl)\n", out);
        else
            fputs(byte == 0 ? "\tsub (hl)\n" :
                              "\tsbc a,(hl)\n", out);
        fputs("\tld (de),a\n", out);
        if (byte != 3)
            fputs("\tinc de\n\tinc hl\n", out);
    }
    fputs("\tret\n", out);
}

static void mir_machine_emit_parameter_member_word(
    FILE *out, int stack_offset, int member_offset,
    const char *low_register, const char *high_register,
    int preserve_bc)
{
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tex de,hl\n",
            stack_offset);
    mir_machine_emit_hl_offset(
        out, member_offset, preserve_bc);
    fprintf(out, "\tld %s,(hl)\n\tinc hl\n\tld %s,(hl)\n",
            low_register, high_register);
}

static void mir_emit_signed_member_product(
    FILE *out, const struct MirSignedMemberProduct *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_parameter_member_word(
        out, plan->pointer_stack_offset,
        plan->left_member_offset, "c", "b", 0);
    mir_machine_emit_parameter_member_word(
        out, plan->pointer_stack_offset,
        plan->right_member_offset, "e", "d", 1);
    fputs("\tex de,hl\n", out);
    mir_emit_runtime_call(out, "__m1s");
    fputs("\tpush de\n\tpush hl\n", out);
    fprintf(out, "\tld hl,%lu\n\tld de,%lu\n",
            plan->scale & 0xffffUL,
            (plan->scale >> 16) & 0xffffUL);
    mir_emit_runtime_call(out, "__lmul");
    fputs("\tpop bc\n\tpop bc\n\tret\n", out);
}

static void mir_emit_signed_member_square_scale_div(
    FILE *out, const struct MirSignedMemberSquareScaleDiv *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_parameter_member_word(
        out, plan->pointer_stack_offset,
        plan->member_offset, "c", "b", 0);
    fputs("\tld l,c\n\tld h,b\n", out);
    mir_emit_runtime_call(out, "__m1s");
    fputs("\tpush de\n\tpush hl\n", out);
    fprintf(out, "\tld hl,%lu\n\tld de,%lu\n",
            plan->scale & 0xffffUL,
            (plan->scale >> 16) & 0xffffUL);
    mir_emit_runtime_call(out, "__lmul");
    fputs("\tpop bc\n\tpop bc\n"
          "\tpush de\n\tpush hl\n", out);
    fprintf(out, "\tld hl,%lu\n\tld de,%lu\n",
            plan->divisor & 0xffffUL,
            (plan->divisor >> 16) & 0xffffUL);
    mir_emit_runtime_call(out, "__lds");
    fputs("\tpop bc\n\tpop bc\n\tret\n", out);
}

static void mir_emit_signed_member_scale_pair(
    FILE *out, const struct MirSignedMemberScalePair *plan)
{
    int member;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (member = 0; member < 2; ++member) {
        mir_machine_emit_parameter_member_word(
            out, plan->pointer_stack_offset,
            plan->member_offsets[member], "c", "b", 0);
        fprintf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
                "\tex de,hl\n",
                plan->value_stack_offset);
        mir_emit_runtime_call(out, "__m1s");
        fputs("\tpush de\n\tpush hl\n", out);
        fprintf(out, "\tld hl,%lu\n\tld de,%lu\n",
                plan->divisor & 0xffffUL,
                (plan->divisor >> 16) & 0xffffUL);
        mir_emit_runtime_call(out, "__lds");
        fputs("\tpop bc\n\tpop bc\n\tpush hl\n", out);
        fprintf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
                "\tex de,hl\n",
                plan->pointer_stack_offset + 2);
        mir_machine_emit_hl_offset(
            out, plan->member_offsets[member], 0);
        fputs("\tpop de\n\tld (hl),e\n\tinc hl\n"
              "\tld (hl),d\n", out);
    }
    fputs("\tret\n", out);
}

static void mir_emit_wide_narrow_division(
    FILE *out, const struct MirWideNarrowDivision *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld l,c\n\tld h,b\n"
            "\tpush de\n\tpush hl\n",
            plan->wide_stack_offset);
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tld l,c\n\tld h,b\n\tld de,0\n",
            plan->narrow_stack_offset + 4);
    if (!plan->is_unsigned) {
        int extended = new_label();

        fprintf(out,
                "\tbit 7,h\n\tjp z,L%d\n\tdec de\nL%d:\n",
                extended, extended);
    }
    mir_emit_runtime_call(
        out, plan->operation == '%'
                 ? (plan->is_unsigned ? "__lmu" : "__lms")
                 : (plan->is_unsigned ? "__ldu" : "__lds"));
    fputs("\tpop bc\n\tpop bc\n\tret\n", out);
}

static void mir_machine_emit_extension_byte(
    FILE *out, const struct MirAggregateSumField *field)
{
    if (field->is_unsigned) {
        fputs("\tld c,0\n", out);
    } else {
        int nonnegative = new_label();
        int ready = new_label();

        fprintf(out,
                "\tbit 7,c\n\tjp z,L%d\n"
                "\tld c,255\n\tjp L%d\n"
                "L%d:\n\tld c,0\nL%d:\n",
                nonnegative, ready, nonnegative, ready);
    }
}

static void mir_machine_emit_aggregate_sum_field(
    FILE *out, const struct MirAggregateSumField *field)
{
    int offset = field->offset;

    fputs("\tld c,(iy", out);
    fprintf(out, "%+d)\n", offset);
    fputs("\tld a,l\n\tadd a,c\n\tld l,a\n", out);
    if (field->width >= 2) {
        fprintf(out, "\tld c,(iy%+d)\n", offset + 1);
        fputs("\tld a,h\n\tadc a,c\n\tld h,a\n", out);
    } else {
        mir_machine_emit_extension_byte(out, field);
        fputs("\tld a,h\n\tadc a,c\n\tld h,a\n", out);
    }
    if (field->width >= 4) {
        fprintf(out, "\tld c,(iy%+d)\n", offset + 2);
        fputs("\tld a,e\n\tadc a,c\n\tld e,a\n", out);
        fprintf(out, "\tld c,(iy%+d)\n", offset + 3);
        fputs("\tld a,d\n\tadc a,c\n\tld d,a\n", out);
    } else {
        if (field->width == 2)
            mir_machine_emit_extension_byte(out, field);
        fputs("\tld a,e\n\tadc a,c\n\tld e,a\n"
              "\tld a,d\n\tadc a,c\n\tld d,a\n", out);
    }
}

static void mir_emit_aggregate_field_sum(
    FILE *out, const struct MirAggregateFieldSum *plan)
{
    int field;

    fprintf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tpush hl\n\tpop iy\n"
            "\tld hl,0\n\tld de,0\n",
            plan->parameter_stack_offset + 2);
    for (field = 0; field < plan->field_count; ++field)
        mir_machine_emit_aggregate_sum_field(
            out, &plan->fields[field]);
    fputs("\tpop iy\n\tret\n", out);
}

static void mir_emit_indexed_member_write(
    FILE *out, const struct MirIndexedMemberWrite *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_word(
        out, plan->root, plan->root_offset);
    fputs("\tld c,l\n\tld b,h\n"
          "\tld l,c\n\tld h,b\n", out);
    mir_machine_emit_hl_offset(
        out, plan->pointer_member_offset, 1);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tpush de\n\tld l,c\n\tld h,b\n", out);
    mir_machine_emit_hl_offset(
        out, plan->index_member_offset, 0);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld l,e\n\tld h,d\n", out);
    mir_emit_mul_hl_const(out, (unsigned long)plan->stride);
    fputs("\tpop de\n\tadd hl,de\n", out);
    mir_machine_emit_hl_offset(
        out, plan->element_member_offset -
             plan->address_adjust, 0);
    fputs("\tpush hl\n", out);
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tpop hl\n\tld (hl),e\n\tinc hl\n"
            "\tld (hl),d\n\tret\n",
            plan->value_stack_offset + 2);
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

int mir_try_emit_speculation_safe_machine_cfg(FILE *out)
{
    struct MirWideNarrowDivision division;
    struct MirIndexedMemberWrite write;

    if (mir_match_wide_narrow_division(&division)) {
        fprintf(out, "%s\n", MIR_EXACT_KERNEL_MARKER);
        fprintf(out, "%s\n", MIR_SPECULATION_SAFE_MARKER);
        mir_emit_wide_narrow_division(out, &division);
        return 1;
    }
    if (!mir_match_indexed_member_write(&write))
        return 0;
    fprintf(out, "%s\n", MIR_EXACT_KERNEL_MARKER);
    fprintf(out, "%s\n", MIR_SPECULATION_SAFE_MARKER);
    mir_emit_indexed_member_write(out, &write);
    return 1;
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
    struct MirByteMemoryStack byte_memory_stack;
    struct MirFixedArrayReduction fixed_array_reduction;
    struct MirWideMemberUpdate wide_member_update;
    struct MirSignedMemberProduct signed_member_product;
    struct MirSignedMemberSquareScaleDiv
        signed_member_square_scale_div;
    struct MirSignedMemberScalePair signed_member_scale_pair;
    struct MirWideNarrowDivision wide_narrow_division;
    struct MirAggregateFieldSum aggregate_field_sum;
    struct MirConstantChecks constant_checks;
    struct MirConstantPrints constant_prints;
    struct MirPointerDifferencePrints pointer_difference_prints;
    struct MirByteComparisonPrint byte_comparison_print;
    struct MirConstantBufferCallPrint constant_buffer_call_print;
    struct MirIndexedMemberWrite indexed_member_write;
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
    if (mir_match_byte_memory_stack(&byte_memory_stack)) {
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
        mir_emit_byte_memory_stack(out, &byte_memory_stack);
        return 1;
    }
    if (mir_match_word_memory_stack_push(&byte_memory_stack)) {
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
        mir_emit_byte_memory_stack(out, &byte_memory_stack);
        return 1;
    }
    if (mir_match_fixed_array_reduction(&fixed_array_reduction)) {
        mir_emit_fixed_array_reduction(out, &fixed_array_reduction);
        return 1;
    }
    if (mir_match_wide_member_update(&wide_member_update)) {
        mir_emit_wide_member_update(out, &wide_member_update);
        return 1;
    }
    if (mir_match_signed_member_product(
            &signed_member_product)) {
        mir_emit_signed_member_product(
            out, &signed_member_product);
        return 1;
    }
    if (mir_match_signed_member_square_scale_div(
            &signed_member_square_scale_div)) {
        mir_emit_signed_member_square_scale_div(
            out, &signed_member_square_scale_div);
        return 1;
    }
    if (mir_match_signed_member_scale_pair(
            &signed_member_scale_pair)) {
        mir_emit_signed_member_scale_pair(
            out, &signed_member_scale_pair);
        return 1;
    }
    if (mir_match_wide_narrow_division(
            &wide_narrow_division)) {
        mir_emit_wide_narrow_division(
            out, &wide_narrow_division);
        return 1;
    }
    if (mir_match_aggregate_field_sum(
            &aggregate_field_sum)) {
        mir_emit_aggregate_field_sum(
            out, &aggregate_field_sum);
        return 1;
    }
    if (mir_match_constant_checks(&constant_checks)) {
        mir_emit_constant_checks(out, &constant_checks);
        return 1;
    }
    if (mir_match_constant_prints(&constant_prints)) {
        mir_emit_constant_prints(out, &constant_prints);
        return 1;
    }
    if (mir_match_pointer_difference_prints(
            &pointer_difference_prints)) {
        mir_emit_pointer_difference_prints(
            out, &pointer_difference_prints);
        return 1;
    }
    if (mir_match_byte_comparison_print(
            &byte_comparison_print)) {
        mir_emit_byte_comparison_print(
            out, &byte_comparison_print);
        return 1;
    }
    if (mir_match_constant_buffer_call_print(
            &constant_buffer_call_print)) {
        mir_emit_constant_buffer_call_print(
            out, &constant_buffer_call_print);
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
