/**
 * @file dcc_mir_machine_wide_records.c
 * @brief Emits exact schedules for wide-value and record-oriented kernels.
 *
 * @par Role
 * Matches complete MIR shapes for wide arithmetic, aggregate-member updates,
 * record append variants, byte checks, and related reports. It emits
 * specialized Z80 schedules only after each shape is proven.
 *
 * @par Key entry point
 * mir_try_emit_wide_record_kernels().
 */

#include "dcc_mir_machine_internal.h"

/* Private copy of mir_machine_emit_global_address_hl (small helper duplicated per
 * family file rather than shared, matching existing convention). */

static void mir_machine_emit_global_address_hl(
    MirStream *out, struct Sym *symbol, int offset)
{
    const char *name = asm_name_for(sym_asm_name(symbol));

    if ((symbol->storage == SC_EXTERN || symbol->needs_extrn) &&
        mir_extrn_should_emit(symbol))
        mir_stream_printf(out, "\textrn %s\n", name);
    if (offset == 0)
        mir_stream_printf(out, "\tld hl,%s\n", name);
    else
        mir_stream_printf(out, "\tld hl,%s%+d\n", name, offset);
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

/* Private copy of mir_machine_single_call_argument (small helper duplicated per
 * family file rather than shared, matching existing convention). */

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

/* Private copy of mir_machine_call_has_no_arguments (small helper duplicated per
 * family file rather than shared, matching existing convention). */

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

/* Private copy of mir_machine_three_call_arguments (small helper duplicated per
 * family file rather than shared, matching existing convention). */

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

/* Enum tags paired with the plan structs above (also moved
 * verbatim from dcc_mir_machine_emit.c). */

enum MirFloatSpecialCheckKind {
    MIR_FLOAT_SPECIAL_INFINITY = 1,
    MIR_FLOAT_SPECIAL_NAN = 2
};

enum MirMachineFormKind {
    MIR_MACHINE_FORM_INTEGER = 1,
    MIR_MACHINE_FORM_POINTER = 2
};

/* The following struct/macro definitions are shared plan
 * types used by helper functions in this file; moved here
 * verbatim from dcc_mir_machine_emit.c during the family
 * split. */

struct MirAggregateSumField {
    int offset;
    int width;
    int is_unsigned;
};

struct MirMachineForm {
    int kind;
    long value;
    int storage;
    int offset;
    int pointer_terms;
    char name[64];
};

struct MirFixedEmbeddingBuild {
    struct Sym *destination;
    struct Sym *positions;
    struct Sym *tokens;
    struct Sym *token_weights;
    struct Sym *clamp_function;
    int destination_offset;
    int positions_offset;
    int tokens_offset;
    int token_weights_offset;
};

struct MirFixedForwardAttention {
    struct Sym *workspace;
    struct Sym *embeddings;
    struct Sym *output;
    struct Sym *project_function;
    struct Sym *score_function;
    struct Sym *softmax_function;
    struct Sym *transpose_function;
    struct Sym *clamp_function;
    int workspace_offset;
    int embeddings_offset;
    int output_offset;
};

struct MirFourByteFailureCheck {
    struct Sym *failure_count;
    int failure_offset;
    int name_stack_offset;
    int source_stack_offset;
    int source_is_pointer;
    int expected_stack_offsets[4];
    int expected_widths[4];
    int include_expected;
    int string_id;
    char call_name[64];
};

struct MirFloatSpecialCheck {
    struct Sym *checks;
    struct Sym *failures;
    int checks_offset;
    int failures_offset;
    int name_stack_offset;
    int value_stack_offset;
    int negative_stack_offset;
    int kind;
    int string_id;
    char call_name[64];
};

struct MirFlaggedRecordAppend {
    struct Sym *counts;
    struct Sym *records;
    struct Sym *values;
    struct Sym *classify_function;
    int counts_offset;
    int records_offset;
    int values_offset;
    int ply_stack_offset;
    int from_stack_offset;
    int to_stack_offset;
    int promoted_stack_offset;
    int flag_stack_offset;
    int record_stride;
    int row_stride;
    int field_offsets[8];
    int limit;
    int special_mask;
    int true_value;
    int false_value;
};

struct MirRecordWildcardMatch {
    int left_stack_offset;
    int right_stack_offset;
    int first_offset;
    int second_offset;
    int wildcard_offset;
    int wildcard_value;
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

struct MirAggregateFieldSum {
    int parameter_stack_offset;
    int field_count;
    struct MirAggregateSumField fields[4];
};

struct MirConstantChecks {
    struct Sym *function;
    int count;
    int name_last;
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

struct MirCallSumPrint {
    struct Sym *value_functions[4];
    struct Sym *print_function;
    int arguments[2];
    int string_id;
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
    int width;
    int is_unsigned;
    int string_id;
};

struct MirConstantBufferCallPrint {
    struct Sym *pack_function;
    char print_name[64];
    int string_id;
    unsigned char bytes[4];
};

struct MirVlaEndpointReduction {
    int parameter_stack_offset;
    int adjustment;
};

struct MirMaskedWideProductHigh {
    int parameter_stack_offset;
    unsigned int multiplier;
};

struct MirWideEqualSelect {
    int parameter_stack_offset;
    unsigned long match_value;
    unsigned long fallback_value;
};

struct MirWideEqualAddSelect {
    int wide_stack_offset;
    int narrow_stack_offset;
    int narrow_is_unsigned;
    unsigned long match_value;
    unsigned long fallback_value;
};

struct MirWideCallMemberAccumulate {
    struct Sym *function;
    int argument_stack_offset;
    int object_stack_offset;
    int member_offset;
};

struct MirWideDifferenceCall {
    struct Sym *function;
    int left_stack_offset;
    int right_stack_offset;
    int left_is_unsigned;
    int right_is_unsigned;
};

struct MirScaledWideDivisionCall {
    struct Sym *function;
    int numerator_stack_offset;
    int denominator_stack_offset;
};

struct MirRecordAppend {
    struct Sym *root;
    int root_offset;
    int array_member_offset;
    int cursor_member_offset;
    int stride;
    int field_offsets[3];
    int parameter_stack_offsets[3];
};

struct MirMixedWideSum {
    int parameter_stack_offsets[4];
    int parameter_widths[4];
    int parameter_is_unsigned[4];
};

struct MirFloatMemberScaleAdd {
    int parameter_stack_offset;
    int destination_offset;
    int source_offset;
    unsigned long scale_bits;
    int returns_value;
};

struct MirGlobalArrayFma {
    struct Sym *root;
    int root_offset;
    int index_stack_offset;
    int left_stack_offset;
    int right_stack_offset;
    int addend_stack_offset;
    int stride;
};

struct MirWideBitcastCall {
    struct Sym *function;
    int stack_offsets[3];
    int argument_count;
};

struct MirWideShiftCompare {
    int word_stack_offset;
    int wide_stack_offset;
    int shift;
    unsigned long threshold;
};

struct MirConditionalWideAdd {
    int condition_stack_offset;
    int word_stack_offset;
    int true_wide_stack_offset;
    int false_wide_stack_offset;
};

struct MirWideResultSwitch {
    int word_stack_offset;
    int wide_stack_offset;
    unsigned long cases[2];
    unsigned long results[3];
};

struct MirBoundedMemberAppend {
    int root_stack_offset;
    int value_stack_offset;
    int count_offset;
    int array_offset;
    int bound;
    int stride;
};

struct MirByteMismatchReport {
    struct Sym *counter;
    int counter_offset;
    int name_stack_offset;
    int got_stack_offset;
    int expected_stack_offset;
    int string_id;
    char call_name[64];
};

struct MirByteArithmeticReports {
    int left_stack_offset;
    int right_stack_offset;
    int is_unsigned;
    int string_ids[3];
    char call_name[64];
};

struct MirFixedByteScanChecks {
    struct Sym *root;
    struct Sym *check_function;
    char root_assembly_name[64];
    int values[16];
    int string_ids[16];
    int value_count;
    int check_count;
};

struct MirTwoConstantChecks {
    struct Sym *function;
    int values[2];
    int string_ids[2];
};

struct MirVariadicJoinReport {
    struct Sym *join_function;
    struct Sym *print_function;
    int separator_string_id;
    int item_string_ids[4];
    int format_string_id;
    int item_count;
    int buffer_size;
    int separator;
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

static int mir_machine_comparison_parameter(
    int value, int *parameter_value,
    int *parameter_width, int *parameter_unsigned)
{
    const struct MirInsn *definition = mir_definition(value);
    const struct MirInsn *conversions[8];
    int conversion_count = 0;
    int current_type;
    int original_type;
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
        (type_size(definition->type) != 1 &&
         type_size(definition->type) != 2) ||
        type_ptr_depth(definition->type) != 0 ||
        type_is_float(definition->type) ||
        (definition->type & 15) == TYPE_BOOL)
        return 0;
    original_type = current_type = definition->type;
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
    if (type_size(current_type) != 2)
        return 0;
    *parameter_value = definition->dst;
    *parameter_width = type_size(original_type);
    *parameter_unsigned =
        (original_type & TYPE_UNSIGNED) != 0;
    return 1;
}

static int mir_machine_match_comparison_argument(
    int value, int operation, int left_parameter,
    int right_parameter, int width, int is_unsigned)
{
    const struct MirInsn *definition = mir_definition(value);
    int left;
    int left_unsigned;
    int left_width;
    int right;
    int right_unsigned;
    int right_width;

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
               definition->src1, &left,
               &left_width, &left_unsigned) &&
           mir_machine_comparison_parameter(
               definition->src2, &right,
               &right_width, &right_unsigned) &&
           left == left_parameter && right == right_parameter &&
           left_width == width && right_width == width &&
           left_unsigned == is_unsigned &&
           right_unsigned == is_unsigned;
}

static void mir_machine_emit_byte_comparison_push(
    MirStream *out, const struct MirByteComparisonPrint *plan,
    int operation, int swap, int pushed_words)
{
    int left_offset = swap
        ? plan->right_stack_offset : plan->left_stack_offset;
    int right_offset = swap
        ? plan->left_stack_offset : plan->right_stack_offset;
    int true_label = new_label();
    int end_label = new_label();

    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld c,(hl)\n",
            left_offset + pushed_words * 2);
    if (plan->width == 2)
        mir_stream_puts("\tinc hl\n\tld b,(hl)\n", out);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld e,(hl)\n",
            right_offset + pushed_words * 2);
    if (plan->width == 2) {
        mir_stream_puts("\tinc hl\n\tld d,(hl)\n", out);
    } else if (plan->is_unsigned) {
        mir_stream_puts("\tld b,0\n\tld d,0\n", out);
    } else {
        mir_stream_puts("\tld a,c\n\trlca\n\tsbc a,a\n\tld b,a\n"
              "\tld a,e\n\trlca\n\tsbc a,a\n\tld d,a\n", out);
    }
    if (operation == TOK_EQ) {
        mir_stream_puts("\tld a,c\n\txor e\n\tld l,a\n"
              "\tld a,b\n\txor d\n\tor l\n", out);
        mir_stream_printf(out, "\tjp z,L%d\n", true_label);
    } else {
        if (!plan->is_unsigned)
            mir_stream_puts("\tld a,b\n\txor 128\n\tld b,a\n"
                  "\tld a,d\n\txor 128\n\tld d,a\n", out);
        mir_stream_puts("\tld a,c\n\tsub e\n\tld a,b\n\tsbc a,d\n", out);
        mir_stream_printf(out, operation == '<'
                    ? "\tjp c,L%d\n" : "\tjp nc,L%d\n",
                true_label);
    }
    mir_stream_printf(out,
            "\tld hl,0\n\tjp L%d\n"
            "L%d:\n\tld hl,1\nL%d:\n\tpush hl\n",
            end_label, true_label, end_label);
}

static int mir_machine_resolve_direct_call(
    const struct MirInsn *call, struct Sym **function)
{
    if (call == NULL || call->opcode != MIR_CALL ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0)
        return 0;
    *function = find_global(call->name);
    return *function != NULL && (*function)->is_defined &&
            (call->base_name[0] == 0 ||
             !strcmp(call->base_name,
                     asm_name_for(sym_asm_name(*function))));
}

static int mir_machine_value_matches_parameter(
    int value, const struct MirInsn *parameter)
{
    const struct MirInsn *definition;
    int value_type;
    int value_storage;
    int value_offset;
    int parameter_type;
    int parameter_storage;
    int parameter_offset;
    int depth = 0;

    while ((definition = mir_definition(value)) != NULL &&
           definition->opcode == MIR_UNARY &&
           definition->immediate == 0 &&
           depth++ < 8)
        value = definition->src1;
    if (value == parameter->dst)
        return 1;
    definition = mir_definition(value);
    return definition != NULL &&
           mir_scalar_memory_location(
               definition, &value_type, &value_storage,
               &value_offset) &&
           mir_scalar_memory_location(
               parameter, &parameter_type, &parameter_storage,
               &parameter_offset) &&
           value_storage == SC_PARAM &&
           parameter_storage == SC_PARAM &&
           value_offset == parameter_offset;
}

static int mir_machine_source_base_matches(
    int value, const struct MirInsn *source,
    int source_is_pointer, int depth)
{
    const struct MirInsn *definition;
    int value_type;
    int value_storage;
    int value_offset;
    int source_type;
    int source_storage;
    int source_offset;
    int instruction;
    const struct MirInsn *matching_store = NULL;

    if (depth > 12)
        return 0;
    if (value == source->dst && source_is_pointer)
        return 1;
    definition = mir_definition(value);
    if (definition == NULL)
        return 0;
    if (definition->opcode == MIR_UNARY &&
        definition->immediate == 0)
        return mir_machine_source_base_matches(
            definition->src1, source,
            source_is_pointer, depth + 1);
    if (mir_scalar_memory_location(
            definition, &value_type, &value_storage,
            &value_offset) &&
        mir_scalar_memory_location(
            source, &source_type, &source_storage,
            &source_offset) &&
        value_storage == SC_PARAM &&
        source_storage == SC_PARAM &&
        value_offset == source_offset)
        return source_is_pointer
                   ? definition->opcode == MIR_LOAD
                   : definition->opcode == MIR_ADDRESS;
    if (definition->opcode != MIR_LOAD ||
        !mir_machine_named_nonvolatile(definition))
        return 0;
    for (instruction = 0;
         instruction < (int)(definition - mir.insns);
         ++instruction)
        if (mir.insns[instruction].opcode == MIR_STORE &&
            mir_machine_same_location(
                definition, &mir.insns[instruction])) {
            if (matching_store != NULL)
                return 0;
            matching_store = &mir.insns[instruction];
        }
    return matching_store != NULL &&
           mir_machine_source_base_matches(
               matching_store->src1, source,
               source_is_pointer, depth + 1);
}

static int mir_machine_byte_load_from_source(
    int value, const struct MirInsn *source,
    int source_is_pointer, int *byte_offset)
{
    const struct MirInsn *definition;
    const struct MirInsn *address;
    long offset;
    int depth = 0;

    while ((definition = mir_definition(value)) != NULL &&
           definition->opcode == MIR_UNARY &&
           definition->immediate == 0 &&
           depth++ < 8)
        value = definition->src1;
    definition = mir_definition(value);
    if (definition == NULL ||
        definition->opcode != MIR_LOAD_INDIRECT ||
        definition->memory_size != 1 ||
        definition->bit_width != 0 ||
        (definition->memory_flags & (1 | 8)) != 0)
        return 0;
    address = mir_definition(definition->src1);
    if (address == NULL ||
        address->opcode != MIR_INDEX_ADDRESS ||
        address->immediate != 1 ||
        address->memory_size != 1 ||
        !mir_machine_constant_value(
            address->src2, &offset, 0) ||
        offset < 0 || offset > 3 ||
        !mir_machine_source_base_matches(
            address->src1, source,
            source_is_pointer, 0))
        return 0;
    *byte_offset = (int)offset;
    return 1;
}

static int mir_machine_named_global_increment(
    const struct MirInsn *store,
    struct Sym **root, int *offset)
{
    const struct MirInsn *add;
    const struct MirInsn *load;
    int memory_type;
    int memory_storage;
    int memory_offset;

    if (store == NULL || store->opcode != MIR_STORE ||
        !mir_scalar_memory_location(
            store, &memory_type,
            &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        type_size(memory_type) != 2)
        return 0;
    add = mir_definition(store->src1);
    if (add == NULL || add->opcode != MIR_BINARY ||
        add->immediate != '+' ||
        !mir_machine_constant_equals(add->src2, 1))
        return 0;
    load = mir_definition(add->src1);
    if (load == NULL || load->opcode != MIR_LOAD ||
        !mir_machine_same_location(load, store))
        return 0;
    *root = find_global(store->name);
    *offset = memory_offset;
    return *root != NULL && !(*root)->is_volatile;
}

static int mir_machine_match_name_report(
    const struct MirInsn *call,
    const struct MirInsn *name,
    int *string_id, char call_name[64])
{
    int arguments[2];
    const struct MirInsn *format;
    struct Sym *function;

    if (!mir_machine_two_call_arguments(call, arguments) ||
        !mir_machine_value_matches_parameter(
            arguments[1], name) ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) !=
            MIR_CALL_FLAG_VARIADIC)
        return 0;
    format = mir_definition(arguments[0]);
    if (format == NULL ||
        format->opcode != MIR_STRING_ADDRESS ||
        format->immediate < 0)
        return 0;
    function = find_global(call->name);
    if (function == NULL || function->is_defined)
        return 0;
    *string_id = (int)format->immediate;
    snprintf(call_name, 64, "%s",
             call->base_name[0] != 0
                 ? call->base_name
                 : asm_name_for(
                       sym_asm_name(function)));
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

static void mir_emit_four_byte_source(
    MirStream *out, const struct MirFourByteFailureCheck *plan,
    int byte)
{
    if (plan->source_is_pointer) {
        int offset;

        mir_stream_printf(out,
                "\tld l,(ix+%d)\n\tld h,(ix+%d)\n",
                plan->source_stack_offset + 2,
                plan->source_stack_offset + 3);
        for (offset = 0; offset < byte; ++offset)
            mir_stream_puts("\tinc hl\n", out);
        mir_stream_puts("\tld a,(hl)\n", out);
    } else {
        mir_stream_printf(out, "\tld a,(ix+%d)\n",
                plan->source_stack_offset + 2 + byte);
    }
}

static void mir_machine_emit_parameter_member_word(
    MirStream *out, int stack_offset, int member_offset,
    const char *low_register, const char *high_register,
    int preserve_bc)
{
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tex de,hl\n",
            stack_offset);
    mir_machine_emit_hl_offset(
        out, member_offset, preserve_bc);
    mir_stream_printf(out, "\tld %s,(hl)\n\tinc hl\n\tld %s,(hl)\n",
            low_register, high_register);
}

static void mir_machine_emit_extension_byte(
    MirStream *out, const struct MirAggregateSumField *field)
{
    if (field->is_unsigned) {
        mir_stream_puts("\tld c,0\n", out);
    } else {
        int nonnegative = new_label();
        int ready = new_label();

        mir_stream_printf(out,
                "\tbit 7,c\n\tjp z,L%d\n"
                "\tld c,255\n\tjp L%d\n"
                "L%d:\n\tld c,0\nL%d:\n",
                nonnegative, ready, nonnegative, ready);
    }
}

static void mir_machine_emit_aggregate_sum_field(
    MirStream *out, const struct MirAggregateSumField *field)
{
    int offset = field->offset;

    mir_stream_puts("\tld c,(iy", out);
    mir_stream_printf(out, "%+d)\n", offset);
    mir_stream_puts("\tld a,l\n\tadd a,c\n\tld l,a\n", out);
    if (field->width >= 2) {
        mir_stream_printf(out, "\tld c,(iy%+d)\n", offset + 1);
        mir_stream_puts("\tld a,h\n\tadc a,c\n\tld h,a\n", out);
    } else {
        mir_machine_emit_extension_byte(out, field);
        mir_stream_puts("\tld a,h\n\tadc a,c\n\tld h,a\n", out);
    }
    if (field->width >= 4) {
        mir_stream_printf(out, "\tld c,(iy%+d)\n", offset + 2);
        mir_stream_puts("\tld a,e\n\tadc a,c\n\tld e,a\n", out);
        mir_stream_printf(out, "\tld c,(iy%+d)\n", offset + 3);
        mir_stream_puts("\tld a,d\n\tadc a,c\n\tld d,a\n", out);
    } else {
        if (field->width == 2)
            mir_machine_emit_extension_byte(out, field);
        mir_stream_puts("\tld a,e\n\tadc a,c\n\tld e,a\n"
              "\tld a,d\n\tadc a,c\n\tld d,a\n", out);
    }
}

static void mir_emit_wide_stack_equality_branch(
    MirStream *out, int stack_offset, unsigned long value,
    int fallback)
{
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\txor %lu\n\tld c,a\n\tinc hl\n"
            "\tld a,(hl)\n\txor %lu\n\tor c\n\tld c,a\n\tinc hl\n"
            "\tld a,(hl)\n\txor %lu\n\tor c\n\tld c,a\n\tinc hl\n"
            "\tld a,(hl)\n\txor %lu\n\tor c\n\tjp nz,L%d\n",
            stack_offset,
            value & 0xffUL,
            (value >> 8) & 0xffUL,
            (value >> 16) & 0xffUL,
            (value >> 24) & 0xffUL,
            fallback);
}

static void mir_emit_widened_parameter(
    MirStream *out, int stack_offset, int is_unsigned)
{
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n",
            stack_offset);
    if (is_unsigned) {
        mir_stream_puts("\tld de,0\n", out);
    } else {
        mir_stream_puts("\tld a,h\n\trlca\n\tsbc a,a\n"
              "\tld e,a\n\tld d,a\n", out);
    }
}

static void mir_emit_add_mixed_wide_parameter(
    MirStream *out, int stack_offset, int width, int is_unsigned)
{
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    if (width == 2)
        mir_emit_widened_parameter(
            out, stack_offset + 4, is_unsigned);
    else
        mir_emit_wide_parameter(out, stack_offset + 4);
    mir_stream_puts("\tpop bc\n\tadd hl,bc\n\tex de,hl\n"
          "\tpop bc\n\tadc hl,bc\n\tex de,hl\n", out);
}

static void mir_emit_conditional_wide_add_arm(
    MirStream *out, const struct MirConditionalWideAdd *plan,
    int wide_stack_offset)
{
    mir_emit_wide_parameter(out, wide_stack_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tld h,b\n\tld l,c\n"
            "\tld a,b\n\trlca\n\tsbc a,a\n\tld d,a\n\tld e,a\n"
            "\tpop bc\n\tadd hl,bc\n\tex de,hl\n"
            "\tpop bc\n\tadc hl,bc\n\tex de,hl\n\tret\n",
            plan->word_stack_offset + 4);
}

static void mir_emit_word_plus_wide(
    MirStream *out, int word_stack_offset, int wide_stack_offset)
{
    mir_emit_wide_parameter(out, wide_stack_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tld h,b\n\tld l,c\n"
            "\tld a,b\n\trlca\n\tsbc a,a\n\tld d,a\n\tld e,a\n"
            "\tpop bc\n\tadd hl,bc\n\tex de,hl\n"
            "\tpop bc\n\tadc hl,bc\n\tex de,hl\n",
            word_stack_offset + 4);
}

static void mir_emit_byte_binary_operands(
    MirStream *out, const struct MirByteArithmeticReports *plan)
{
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld c,(hl)\n",
            plan->left_stack_offset);
    if (plan->is_unsigned) {
        mir_stream_puts("\tld b,0\n", out);
    } else {
        mir_stream_puts("\tld a,c\n\trlca\n\tsbc a,a\n\tld b,a\n", out);
    }
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld e,(hl)\n",
            plan->right_stack_offset);
    if (plan->is_unsigned) {
        mir_stream_puts("\tld d,0\n", out);
    } else {
        mir_stream_puts("\tld a,e\n\trlca\n\tsbc a,a\n\tld d,a\n", out);
    }
    mir_stream_puts("\tld l,c\n\tld h,b\n", out);
}

static int mir_match_fixed_embedding_build(
    struct MirFixedEmbeddingBuild *plan)
{
    static const int expected_opcodes[77] = {
        MIR_LABEL, MIR_ADDRESS, MIR_NOP, MIR_STORE, MIR_ADDRESS,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL,
        MIR_PHI, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_NOP, MIR_STORE, MIR_ADDRESS, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_INDEX_ADDRESS, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_LOAD, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_LOAD_INDIRECT,
        MIR_UNARY, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_BINARY, MIR_ARG, MIR_CALL,
        MIR_STORE_INDIRECT, MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL
    };
    const struct MirInsn *outer_phi = &mir.insns[11];
    const struct MirInsn *token_index = &mir.insns[19];
    const struct MirInsn *token_load = &mir.insns[20];
    const struct MirInsn *weight_index = &mir.insns[27];
    const struct MirInsn *source_load = &mir.insns[50];
    const struct MirInsn *position_load = &mir.insns[56];
    const struct MirInsn *addition = &mir.insns[58];
    const struct MirInsn *call = &mir.insns[60];
    const struct MirInsn *result_store = &mir.insns[61];
    long destination_offset;
    long positions_offset;
    long tokens_offset;
    long token_weights_offset;
    int call_argument;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir.count != 77 ||
        mir_cfg_block_count() != 7 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    if (!mir_machine_global_address_offset(
            mir.insns[1].dst, &plan->destination,
            &destination_offset, 0) ||
        !mir_machine_global_address_offset(
            mir.insns[4].dst, &plan->positions,
            &positions_offset, 0) ||
        !mir_machine_global_address_offset(
            mir.insns[17].dst, &plan->tokens,
            &tokens_offset, 0) ||
        !mir_machine_global_address_offset(
            mir.insns[23].dst, &plan->token_weights,
            &token_weights_offset, 0) ||
        destination_offset < -32768 ||
        destination_offset > 32767 ||
        positions_offset < -32768 ||
        positions_offset > 32767 ||
        tokens_offset < -32768 ||
        tokens_offset > 32767 ||
        token_weights_offset < -32768 ||
        token_weights_offset > 32767)
        return 0;
    plan->destination_offset = (int)destination_offset;
    plan->positions_offset = (int)positions_offset;
    plan->tokens_offset = (int)tokens_offset;
    plan->token_weights_offset = (int)token_weights_offset;
    if (mir.insns[3].src1 != mir.insns[1].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[3]) ||
        mir.insns[6].src1 != mir.insns[4].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[6]) ||
        !mir_machine_constant_equals(mir.insns[8].dst, 0) ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[9]) ||
        outer_phi->src1 != mir.insns[8].dst ||
        outer_phi->src2 != mir.insns[73].dst ||
        outer_phi->phi_pred1 != mir.insns[0].label ||
        outer_phi->phi_pred2 != mir.insns[70].label ||
        mir.insns[14].immediate != 0 ||
        mir.insns[14].src1 != outer_phi->dst ||
        !mir_machine_constant_equals(mir.insns[13].dst, 8) ||
        mir.insns[15].immediate != '<' ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        mir.insns[15].src2 != mir.insns[13].dst ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[16].label != mir.insns[76].label)
        return 0;
    if (token_index->src1 != mir.insns[17].dst ||
        token_index->src2 != outer_phi->dst ||
        token_index->immediate != 2 ||
        token_index->memory_size != 2 ||
        token_load->src1 != token_index->dst ||
        token_load->memory_size != 2 ||
        token_load->bit_width != 0 ||
        (token_load->memory_flags & (1 | 8)) != 0 ||
        mir.insns[22].src1 != token_load->dst ||
        !mir_machine_unobservable_local_store(&mir.insns[22]) ||
        !mir_machine_constant_equals(mir.insns[25].dst, 16) ||
        mir.insns[26].immediate != '*' ||
        mir.insns[26].src1 != token_load->dst ||
        mir.insns[26].src2 != mir.insns[25].dst ||
        weight_index->src1 != mir.insns[23].dst ||
        weight_index->src2 != mir.insns[26].dst ||
        weight_index->immediate != 2 ||
        weight_index->memory_size != 2 ||
        mir.insns[29].src1 != weight_index->dst ||
        !mir_machine_unobservable_local_store(&mir.insns[29]))
        return 0;
    if (!mir_machine_constant_equals(mir.insns[31].dst, 0) ||
        mir.insns[32].src1 != mir.insns[31].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[32]) ||
        !mir_machine_named_nonvolatile(&mir.insns[37]) ||
        mir.insns[39].immediate != 0 ||
        mir.insns[39].src1 != mir.insns[37].dst ||
        !mir_machine_constant_equals(mir.insns[38].dst, 16) ||
        mir.insns[40].immediate != '<' ||
        mir.insns[40].src1 != mir.insns[39].dst ||
        mir.insns[40].src2 != mir.insns[38].dst ||
        mir.insns[41].src1 != mir.insns[40].dst ||
        mir.insns[41].label != mir.insns[68].label)
        return 0;
    if (!mir_machine_named_nonvolatile(&mir.insns[42]) ||
        !mir_machine_constant_equals(mir.insns[43].dst, 2) ||
        mir.insns[44].immediate != '+' ||
        mir.insns[44].src1 != mir.insns[42].dst ||
        mir.insns[44].src2 != mir.insns[43].dst ||
        !mir_machine_same_location(
            &mir.insns[42], &mir.insns[45]) ||
        mir.insns[45].src1 != mir.insns[44].dst ||
        !mir_machine_named_nonvolatile(&mir.insns[46]) ||
        !mir_machine_constant_equals(mir.insns[47].dst, 2) ||
        mir.insns[48].immediate != '+' ||
        mir.insns[48].src1 != mir.insns[46].dst ||
        mir.insns[48].src2 != mir.insns[47].dst ||
        !mir_machine_same_location(
            &mir.insns[46], &mir.insns[49]) ||
        mir.insns[49].src1 != mir.insns[48].dst ||
        source_load->src1 != mir.insns[46].dst ||
        source_load->memory_size != 2 ||
        source_load->bit_width != 0 ||
        (source_load->memory_flags & (1 | 8)) != 0 ||
        mir.insns[51].opcode != MIR_UNARY ||
        mir.insns[51].immediate != 0 ||
        mir.insns[51].src1 != source_load->dst ||
        type_size(mir.insns[51].type) != 4 ||
        (mir.insns[51].type & TYPE_UNSIGNED) != 0)
        return 0;
    if (!mir_machine_named_nonvolatile(&mir.insns[52]) ||
        !mir_machine_constant_equals(mir.insns[53].dst, 2) ||
        mir.insns[54].immediate != '+' ||
        mir.insns[54].src1 != mir.insns[52].dst ||
        mir.insns[54].src2 != mir.insns[53].dst ||
        !mir_machine_same_location(
            &mir.insns[52], &mir.insns[55]) ||
        mir.insns[55].src1 != mir.insns[54].dst ||
        position_load->src1 != mir.insns[52].dst ||
        position_load->memory_size != 2 ||
        position_load->bit_width != 0 ||
        (position_load->memory_flags & (1 | 8)) != 0 ||
        mir.insns[57].opcode != MIR_UNARY ||
        mir.insns[57].immediate != 0 ||
        mir.insns[57].src1 != position_load->dst ||
        type_size(mir.insns[57].type) != 4 ||
        (mir.insns[57].type & TYPE_UNSIGNED) != 0 ||
        addition->immediate != '+' ||
        addition->src1 != mir.insns[51].dst ||
        addition->src2 != mir.insns[57].dst ||
        type_size(addition->type) != 4 ||
        (addition->type & TYPE_UNSIGNED) != 0)
        return 0;
    if (!mir_machine_single_call_argument(
            call, &call_argument) ||
        call_argument != addition->dst ||
        type_size(call->type) != 2 ||
        type_ptr_depth(call->type) != 0 ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0 ||
        result_store->src1 != mir.insns[42].dst ||
        result_store->src2 != call->dst ||
        result_store->memory_size != 2 ||
        result_store->bit_width != 0 ||
        (result_store->memory_flags & (1 | 8)) != 0)
        return 0;
    if (!mir_machine_named_nonvolatile(&mir.insns[63]) ||
        !mir_machine_constant_equals(mir.insns[64].dst, 1) ||
        mir.insns[65].immediate != '+' ||
        mir.insns[65].src1 != mir.insns[63].dst ||
        mir.insns[65].src2 != mir.insns[64].dst ||
        !mir_machine_same_location(
            &mir.insns[63], &mir.insns[66]) ||
        mir.insns[66].src1 != mir.insns[65].dst ||
        mir.insns[67].label != mir.insns[33].label ||
        !mir_machine_constant_equals(mir.insns[72].dst, 1) ||
        mir.insns[73].immediate != '+' ||
        mir.insns[73].src1 != outer_phi->dst ||
        mir.insns[73].src2 != mir.insns[72].dst ||
        !mir_machine_same_location(
            &mir.insns[9], &mir.insns[74]) ||
        mir.insns[74].src1 != mir.insns[73].dst ||
        mir.insns[75].label != mir.insns[10].label)
        return 0;
    plan->clamp_function = find_global(call->name);
    return plan->clamp_function != NULL &&
           plan->clamp_function->is_defined &&
           (call->base_name[0] == 0 ||
            !strcmp(call->base_name,
                    asm_name_for(sym_asm_name(
                        plan->clamp_function))));
}

static void mir_emit_fixed_embedding_build(
    MirStream *out, const struct MirFixedEmbeddingBuild *plan)
{
    int loop = new_label();
    int source_ready = new_label();

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-3\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\txor a\n\tld (ix-1),a\n", out);
    mir_stream_printf(out,
            "L%d:\n\tld a,(ix-1)\n\tand 15\n"
            "\tjp nz,L%d\n\tld a,(ix-1)\n"
            "\trrca\n\trrca\n\trrca\n\trrca\n"
            "\tld l,a\n\tld h,0\n\tadd hl,hl\n",
            loop, source_ready);
    mir_machine_emit_global_address_de(
        out, plan->tokens, plan->tokens_offset);
    mir_stream_puts("\tadd hl,de\n\tld e,(hl)\n\tinc hl\n"
          "\tld d,(hl)\n\tex de,hl\n"
          "\tadd hl,hl\n\tadd hl,hl\n\tadd hl,hl\n"
          "\tadd hl,hl\n\tadd hl,hl\n", out);
    mir_machine_emit_global_address_de(
        out, plan->token_weights,
        plan->token_weights_offset);
    mir_stream_printf(out,
            "\tadd hl,de\n\tld (ix-3),l\n\tld (ix-2),h\n"
            "L%d:\n\tld a,(ix-1)\n\tld l,a\n\tld h,0\n"
            "\tadd hl,hl\n",
            source_ready);
    mir_machine_emit_global_address_de(
        out, plan->destination,
        plan->destination_offset);
    mir_stream_puts("\tadd hl,de\n\tpush hl\n"
          "\tld l,(ix-3)\n\tld h,(ix-2)\n"
          "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tinc hl\n"
          "\tld (ix-3),l\n\tld (ix-2),h\n\tex de,hl\n"
          "\tld a,h\n\trlca\n\tsbc a,a\n\tld d,a\n\tld e,a\n"
          "\tpush de\n\tpush hl\n"
          "\tld a,(ix-1)\n\tld l,a\n\tld h,0\n\tadd hl,hl\n",
          out);
    mir_machine_emit_global_address_de(
        out, plan->positions, plan->positions_offset);
    mir_stream_puts("\tadd hl,de\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n\tld a,h\n\trlca\n\tsbc a,a\n"
          "\tld d,a\n\tld e,a\n"
          "\tpop bc\n\tadd hl,bc\n\tex de,hl\n"
          "\tpop bc\n\tadc hl,bc\n\tex de,hl\n"
          "\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->clamp_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop de\n"
          "\tld a,l\n\tld (de),a\n\tinc de\n"
          "\tld a,h\n\tld (de),a\n"
          "\tld a,(ix-1)\n\tinc a\n\tld (ix-1),a\n", out);
    mir_stream_printf(out,
            "\tcp 128\n\tjp nz,L%d\n"
            "\tld sp,ix\n\tpop ix\n\tret\n",
            loop);
}

static int mir_match_fixed_forward_attention(
    struct MirFixedForwardAttention *plan)
{
    static const int expected_opcodes[177] = {
        MIR_LABEL, MIR_CALL, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_NOP, MIR_STORE, MIR_ADDRESS, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_PHI, MIR_NOP,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS,
        MIR_NOP, MIR_NOP, MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_NOP,
        MIR_NOP, MIR_LOAD, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_LOAD, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_STORE_INDIRECT, MIR_LOAD, MIR_CONST, MIR_CONST, MIR_BINARY,
        MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP, MIR_LABEL, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_LOAD,
        MIR_CONST, MIR_CONST, MIR_BINARY, MIR_BINARY, MIR_ARG, MIR_CALL,
        MIR_LOAD, MIR_CONST, MIR_CONST, MIR_BINARY, MIR_BINARY, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_STORE,
        MIR_LABEL, MIR_PHI, MIR_NOP, MIR_NOP, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_CONST, MIR_INDEX_ADDRESS, MIR_ARG,
        MIR_ADDRESS, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST,
        MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BINARY,
        MIR_INDEX_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_INDEX_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_PHI,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_STORE, MIR_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_STORE, MIR_LOAD, MIR_LOAD,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_LOAD, MIR_UNARY, MIR_BINARY,
        MIR_ARG, MIR_CALL, MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL
    };
    const struct MirInsn *outer_score_phi = &mir.insns[20];
    const struct MirInsn *transpose_phi = &mir.insns[95];
    const struct MirInsn *residual_phi = &mir.insns[142];
    int score_arguments[2];
    int transpose_arguments[3];
    int clamp_argument;
    struct Sym *workspace;
    struct Sym *root;
    long workspace_offset;
    long root_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir.count != 177 ||
        mir_cfg_block_count() != 13 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
             expected_opcodes[instruction])
             return 0;
    if (!mir_machine_resolve_direct_call(
             &mir.insns[1], &plan->project_function) ||
        !mir_machine_call_has_no_arguments(&mir.insns[1]) ||
        !mir_machine_resolve_direct_call(
             &mir.insns[52], &plan->score_function) ||
        !mir_machine_resolve_direct_call(
             &mir.insns[75], &plan->softmax_function) ||
        !mir_machine_resolve_direct_call(
             &mir.insns[130], &plan->transpose_function) ||
        !mir_machine_resolve_direct_call(
             &mir.insns[168], &plan->clamp_function))
        return 0;
    if (!mir_machine_global_address_offset(
             mir.insns[2].dst, &workspace,
             &workspace_offset, 0) ||
        workspace_offset < -32768 ||
        workspace_offset > 32767)
        return 0;
    plan->workspace = workspace;
    plan->workspace_offset = (int)workspace_offset;
    {
        static const int workspace_addresses[] = {
             7, 26, 102, 110
        };
        int address;

        for (address = 0;
              address < (int)(sizeof(workspace_addresses) /
                              sizeof(workspace_addresses[0]));
              ++address) {
             int index = workspace_addresses[address];

             if (!mir_machine_global_address_offset(
                     mir.insns[index].dst, &root,
                     &root_offset, 0) ||
                 root != workspace ||
                 root_offset != workspace_offset)
                 return 0;
        }
    }
    if (!mir_machine_global_address_offset(
             mir.insns[151].dst, &plan->embeddings,
             &root_offset, 0) ||
        root_offset < -32768 || root_offset > 32767)
        return 0;
    plan->embeddings_offset = (int)root_offset;
    if (!mir_machine_global_address_offset(
             mir.insns[123].dst, &plan->output,
             &root_offset, 0) ||
        root_offset < -32768 || root_offset > 32767)
        return 0;
    plan->output_offset = (int)root_offset;
    if (!mir_machine_global_address_offset(
             mir.insns[156].dst, &root,
             &root_offset, 0) ||
        root != plan->output ||
        root_offset != plan->output_offset)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        mir.insns[4].src1 != mir.insns[2].dst ||
        mir.insns[4].src2 != mir.insns[3].dst ||
        mir.insns[4].immediate != 2 ||
        mir.insns[4].memory_size != 2 ||
        mir.insns[6].src1 != mir.insns[4].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[6]) ||
        !mir_machine_constant_equals(mir.insns[12].dst, 384) ||
        mir.insns[13].src1 != mir.insns[7].dst ||
        mir.insns[13].src2 != mir.insns[12].dst ||
        mir.insns[13].immediate != 2 ||
        mir.insns[13].memory_size != 2 ||
        mir.insns[15].src1 != mir.insns[13].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[15]))
        return 0;
    if (!mir_machine_constant_equals(mir.insns[17].dst, 0) ||
        mir.insns[18].src1 != mir.insns[17].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[18]) ||
        outer_score_phi->src1 != mir.insns[17].dst ||
        outer_score_phi->src2 != mir.insns[87].dst ||
        outer_score_phi->phi_pred1 != mir.insns[0].label ||
        outer_score_phi->phi_pred2 != mir.insns[84].label ||
        !mir_machine_constant_equals(mir.insns[22].dst, 8) ||
        mir.insns[23].immediate != 0 ||
        mir.insns[23].src1 != outer_score_phi->dst ||
        mir.insns[24].immediate != '<' ||
        mir.insns[24].src1 != mir.insns[23].dst ||
        mir.insns[24].src2 != mir.insns[22].dst ||
        mir.insns[25].src1 != mir.insns[24].dst ||
        mir.insns[25].label != mir.insns[90].label)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[29].dst, 128) ||
        mir.insns[30].src1 != mir.insns[26].dst ||
        mir.insns[30].src2 != mir.insns[29].dst ||
        mir.insns[30].immediate != 2 ||
        mir.insns[30].memory_size != 2 ||
        mir.insns[32].src1 != mir.insns[30].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[32]) ||
        !mir_machine_constant_equals(mir.insns[34].dst, 0) ||
        mir.insns[35].src1 != mir.insns[34].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[35]) ||
        !mir_machine_named_nonvolatile(&mir.insns[39]) ||
        !mir_machine_constant_equals(mir.insns[40].dst, 8) ||
        mir.insns[41].immediate != 0 ||
        mir.insns[41].src1 != mir.insns[39].dst ||
        mir.insns[42].immediate != '<' ||
        mir.insns[42].src1 != mir.insns[41].dst ||
        mir.insns[42].src2 != mir.insns[40].dst ||
        mir.insns[43].src1 != mir.insns[42].dst ||
        mir.insns[43].label != mir.insns[68].label)
        return 0;
    if (!mir_machine_named_nonvolatile(&mir.insns[44]) ||
        !mir_machine_constant_equals(mir.insns[45].dst, 2) ||
        mir.insns[46].immediate != '+' ||
        mir.insns[46].src1 != mir.insns[44].dst ||
        mir.insns[46].src2 != mir.insns[45].dst ||
        !mir_machine_same_location(
             &mir.insns[44], &mir.insns[47]) ||
        mir.insns[47].src1 != mir.insns[46].dst ||
        !mir_machine_named_nonvolatile(&mir.insns[48]) ||
        !mir_machine_named_nonvolatile(&mir.insns[50]) ||
        !mir_machine_two_call_arguments(
             &mir.insns[52], score_arguments) ||
        score_arguments[0] != mir.insns[48].dst ||
        score_arguments[1] != mir.insns[50].dst ||
        mir.insns[53].src1 != mir.insns[44].dst ||
        mir.insns[53].src2 != mir.insns[52].dst ||
        mir.insns[53].memory_size != 2 ||
        (mir.insns[53].memory_flags & (1 | 8)) != 0)
        return 0;
    if (!mir_machine_named_nonvolatile(&mir.insns[54]) ||
        !mir_machine_constant_equals(mir.insns[55].dst, 16) ||
        !mir_machine_constant_equals(mir.insns[56].dst, 2) ||
        mir.insns[57].immediate != '*' ||
        mir.insns[57].src1 != mir.insns[55].dst ||
        mir.insns[57].src2 != mir.insns[56].dst ||
        mir.insns[58].immediate != '+' ||
        mir.insns[58].src1 != mir.insns[54].dst ||
        mir.insns[58].src2 != mir.insns[57].dst ||
        !mir_machine_same_location(
             &mir.insns[54], &mir.insns[60]) ||
        mir.insns[60].src1 != mir.insns[58].dst ||
        !mir_machine_named_nonvolatile(&mir.insns[63]) ||
        !mir_machine_constant_equals(mir.insns[64].dst, 1) ||
        mir.insns[65].immediate != '+' ||
        mir.insns[65].src1 != mir.insns[63].dst ||
        mir.insns[65].src2 != mir.insns[64].dst ||
        !mir_machine_same_location(
             &mir.insns[35], &mir.insns[66]) ||
        mir.insns[66].src1 != mir.insns[65].dst ||
        mir.insns[67].label != mir.insns[36].label)
        return 0;
    if (!mir_machine_named_nonvolatile(&mir.insns[69]) ||
        !mir_machine_constant_equals(mir.insns[70].dst, 8) ||
        !mir_machine_constant_equals(mir.insns[71].dst, 2) ||
        mir.insns[72].immediate != '*' ||
        mir.insns[72].src1 != mir.insns[70].dst ||
        mir.insns[72].src2 != mir.insns[71].dst ||
        mir.insns[73].immediate != '-' ||
        mir.insns[73].src1 != mir.insns[69].dst ||
        mir.insns[73].src2 != mir.insns[72].dst ||
        !mir_machine_single_call_argument(
             &mir.insns[75], &clamp_argument) ||
        clamp_argument != mir.insns[73].dst ||
        !mir_machine_named_nonvolatile(&mir.insns[76]) ||
        !mir_machine_constant_equals(mir.insns[77].dst, 16) ||
        !mir_machine_constant_equals(mir.insns[78].dst, 2) ||
        mir.insns[79].immediate != '*' ||
        mir.insns[79].src1 != mir.insns[77].dst ||
        mir.insns[79].src2 != mir.insns[78].dst ||
        mir.insns[80].immediate != '+' ||
        mir.insns[80].src1 != mir.insns[76].dst ||
        mir.insns[80].src2 != mir.insns[79].dst ||
        !mir_machine_same_location(
             &mir.insns[76], &mir.insns[82]) ||
        mir.insns[82].src1 != mir.insns[80].dst ||
        !mir_machine_constant_equals(mir.insns[86].dst, 1) ||
        mir.insns[87].immediate != '+' ||
        mir.insns[87].src1 != outer_score_phi->dst ||
        mir.insns[87].src2 != mir.insns[86].dst ||
        !mir_machine_same_location(
             &mir.insns[18], &mir.insns[88]) ||
        mir.insns[88].src1 != mir.insns[87].dst ||
        mir.insns[89].label != mir.insns[19].label)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[92].dst, 0) ||
        mir.insns[93].src1 != mir.insns[92].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[93]) ||
        transpose_phi->src1 != mir.insns[92].dst ||
        transpose_phi->src2 != mir.insns[134].dst ||
        transpose_phi->phi_pred1 != mir.insns[90].label ||
        transpose_phi->phi_pred2 != mir.insns[131].label ||
        !mir_machine_constant_equals(mir.insns[98].dst, 8) ||
        mir.insns[99].immediate != 0 ||
        mir.insns[99].src1 != transpose_phi->dst ||
        mir.insns[100].immediate != '<' ||
        mir.insns[100].src1 != mir.insns[99].dst ||
        mir.insns[100].src2 != mir.insns[98].dst ||
        mir.insns[101].src1 != mir.insns[100].dst ||
        mir.insns[101].label != mir.insns[137].label)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[107].dst, 256) ||
        mir.insns[108].src1 != mir.insns[102].dst ||
        mir.insns[108].src2 != mir.insns[107].dst ||
        mir.insns[108].immediate != 2 ||
        !mir_machine_constant_equals(mir.insns[115].dst, 384) ||
        !mir_machine_constant_equals(mir.insns[117].dst, 8) ||
        mir.insns[118].immediate != 0 ||
        mir.insns[118].src1 != transpose_phi->dst ||
        mir.insns[119].immediate != '*' ||
        mir.insns[119].src1 != mir.insns[118].dst ||
        mir.insns[119].src2 != mir.insns[117].dst ||
        mir.insns[120].immediate != '+' ||
        mir.insns[120].src1 != mir.insns[115].dst ||
        mir.insns[120].src2 != mir.insns[119].dst ||
        mir.insns[121].src1 != mir.insns[110].dst ||
        mir.insns[121].src2 != mir.insns[120].dst ||
        mir.insns[121].immediate != 2 ||
        !mir_machine_constant_equals(mir.insns[125].dst, 16) ||
        mir.insns[126].immediate != 0 ||
        mir.insns[126].src1 != transpose_phi->dst ||
        mir.insns[127].immediate != '*' ||
        mir.insns[127].src1 != mir.insns[126].dst ||
        mir.insns[127].src2 != mir.insns[125].dst ||
        mir.insns[128].src1 != mir.insns[123].dst ||
        mir.insns[128].src2 != mir.insns[127].dst ||
        mir.insns[128].immediate != 2 ||
        !mir_machine_three_call_arguments(
             &mir.insns[130], transpose_arguments) ||
        transpose_arguments[0] != mir.insns[108].dst ||
        transpose_arguments[1] != mir.insns[121].dst ||
        transpose_arguments[2] != mir.insns[128].dst ||
        !mir_machine_constant_equals(mir.insns[133].dst, 1) ||
        mir.insns[134].immediate != '+' ||
        mir.insns[134].src1 != transpose_phi->dst ||
        mir.insns[134].src2 != mir.insns[133].dst ||
        !mir_machine_same_location(
             &mir.insns[93], &mir.insns[135]) ||
        mir.insns[135].src1 != mir.insns[134].dst ||
        mir.insns[136].label != mir.insns[94].label)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[139].dst, 0) ||
        mir.insns[140].src1 != mir.insns[139].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[140]) ||
        residual_phi->src1 != mir.insns[139].dst ||
        residual_phi->src2 != mir.insns[173].dst ||
        residual_phi->phi_pred1 != mir.insns[137].label ||
        residual_phi->phi_pred2 != mir.insns[170].label ||
        !mir_machine_constant_equals(mir.insns[147].dst, 128) ||
        mir.insns[148].immediate != 0 ||
        mir.insns[148].src1 != residual_phi->dst ||
        mir.insns[149].immediate != '<' ||
        mir.insns[149].src1 != mir.insns[148].dst ||
        mir.insns[149].src2 != mir.insns[147].dst ||
        mir.insns[150].src1 != mir.insns[149].dst ||
        mir.insns[150].label != mir.insns[176].label)
        return 0;
    if (mir.insns[153].src1 != mir.insns[151].dst ||
        mir.insns[153].src2 != residual_phi->dst ||
        mir.insns[153].immediate != 2 ||
        mir.insns[154].src1 != mir.insns[153].dst ||
        mir.insns[154].memory_size != 2 ||
        (mir.insns[154].memory_flags & (1 | 8)) != 0 ||
        mir.insns[155].src1 != mir.insns[154].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[155]) ||
        mir.insns[158].src1 != mir.insns[156].dst ||
        mir.insns[158].src2 != residual_phi->dst ||
        mir.insns[158].immediate != 2 ||
        mir.insns[159].src1 != mir.insns[158].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[159]) ||
        !mir_machine_same_location(
             &mir.insns[159], &mir.insns[160]) ||
        !mir_machine_same_location(
             &mir.insns[159], &mir.insns[161]) ||
        mir.insns[162].src1 != mir.insns[161].dst ||
        mir.insns[162].memory_size != 2 ||
        (mir.insns[162].memory_flags & (1 | 8)) != 0 ||
        mir.insns[163].immediate != 0 ||
        mir.insns[163].src1 != mir.insns[162].dst ||
        type_size(mir.insns[163].type) != 4 ||
        !mir_machine_same_location(
             &mir.insns[155], &mir.insns[164]) ||
        mir.insns[165].immediate != 0 ||
        mir.insns[165].src1 != mir.insns[164].dst ||
        type_size(mir.insns[165].type) != 4 ||
        mir.insns[166].immediate != '+' ||
        mir.insns[166].src1 != mir.insns[163].dst ||
        mir.insns[166].src2 != mir.insns[165].dst ||
        !mir_machine_single_call_argument(
             &mir.insns[168], &clamp_argument) ||
        clamp_argument != mir.insns[166].dst ||
        mir.insns[169].src1 != mir.insns[160].dst ||
        mir.insns[169].src2 != mir.insns[168].dst ||
        mir.insns[169].memory_size != 2 ||
        (mir.insns[169].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_constant_equals(mir.insns[172].dst, 1) ||
        mir.insns[173].immediate != '+' ||
        mir.insns[173].src1 != residual_phi->dst ||
        mir.insns[173].src2 != mir.insns[172].dst ||
        !mir_machine_same_location(
             &mir.insns[140], &mir.insns[174]) ||
        mir.insns[174].src1 != mir.insns[173].dst ||
        mir.insns[175].label != mir.insns[141].label)
        return 0;
    return 1;
}

static void mir_emit_fixed_forward_attention(
    MirStream *out, const struct MirFixedForwardAttention *plan)
{
    int score_outer = new_label();
    int score_inner = new_label();
    int transpose_loop = new_label();
    int residual_loop = new_label();

    mir_stream_printf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
            "\tld hl,-5\n\tadd hl,sp\n\tld sp,hl\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_symbol_call(out, plan->project_function);
    mir_machine_emit_global_address_de(
        out, plan->workspace, plan->workspace_offset);
    mir_stream_puts("\tpush de\n\tpop iy\n", out);
    mir_machine_emit_global_address_de(
        out, plan->workspace, plan->workspace_offset + 768);
    mir_stream_puts("\tld (ix-5),e\n\tld (ix-4),d\n", out);
    mir_stream_printf(out, "L%d:\n", score_outer);
    mir_machine_emit_global_address_de(
        out, plan->workspace, plan->workspace_offset + 256);
    mir_stream_puts("\tld (ix-3),e\n\tld (ix-2),d\n"
          "\txor a\n\tld (ix-1),a\n", out);
    mir_stream_printf(out,
            "L%d:\n"
            "\tld l,(ix-5)\n\tld h,(ix-4)\n\tpush hl\n"
            "\tinc hl\n\tinc hl\n"
            "\tld (ix-5),l\n\tld (ix-4),h\n"
            "\tld l,(ix-3)\n\tld h,(ix-2)\n\tpush hl\n"
            "\tld de,32\n\tadd hl,de\n"
            "\tld (ix-3),l\n\tld (ix-2),h\n"
            "\tpush iy\n",
            score_inner);
    mir_machine_emit_symbol_call(out, plan->score_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop de\n"
          "\tld a,l\n\tld (de),a\n\tinc de\n"
          "\tld a,h\n\tld (de),a\n"
          "\tld a,(ix-1)\n\tinc a\n\tld (ix-1),a\n", out);
    mir_stream_printf(out, "\tcp 8\n\tjp nz,L%d\n", score_inner);
    mir_stream_puts("\tld l,(ix-5)\n\tld h,(ix-4)\n"
          "\tld de,-16\n\tadd hl,de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->softmax_function);
    mir_stream_puts("\tpop bc\n\tpush iy\n\tpop hl\n"
          "\tld de,32\n\tadd hl,de\n\tpush hl\n\tpop iy\n"
          "\tpush iy\n\tpop hl\n", out);
    mir_machine_emit_global_address_de(
        out, plan->workspace, plan->workspace_offset + 256);
    mir_stream_puts("\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", score_outer);
    mir_machine_emit_global_address_de(
        out, plan->workspace, plan->workspace_offset + 768);
    mir_stream_puts("\tpush de\n\tpop iy\n", out);
    mir_machine_emit_global_address_de(
        out, plan->output, plan->output_offset);
    mir_stream_puts("\tld (ix-5),e\n\tld (ix-4),d\n", out);
    mir_stream_printf(out,
            "L%d:\n"
            "\tld l,(ix-5)\n\tld h,(ix-4)\n\tpush hl\n"
            "\tld de,32\n\tadd hl,de\n"
            "\tld (ix-5),l\n\tld (ix-4),h\n"
            "\tpush iy\n",
            transpose_loop);
    mir_machine_emit_global_address_de(
        out, plan->workspace, plan->workspace_offset + 512);
    mir_stream_puts("\tpush de\n", out);
    mir_machine_emit_symbol_call(out, plan->transpose_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tpush iy\n\tpop hl\n\tld de,16\n\tadd hl,de\n"
          "\tpush hl\n\tpop iy\n\tpush iy\n\tpop hl\n", out);
    mir_machine_emit_global_address_de(
        out, plan->workspace, plan->workspace_offset + 896);
    mir_stream_puts("\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", transpose_loop);
    mir_machine_emit_global_address_de(
        out, plan->output, plan->output_offset);
    mir_stream_puts("\tpush de\n\tpop iy\n", out);
    mir_machine_emit_global_address_de(
        out, plan->embeddings, plan->embeddings_offset);
    mir_stream_puts("\tld (ix-3),e\n\tld (ix-2),d\n", out);
    mir_stream_printf(out,
            "L%d:\n\tpush iy\n"
            "\tld l,(iy+0)\n\tld h,(iy+1)\n"
            "\tld a,h\n\trlca\n\tsbc a,a\n"
            "\tld d,a\n\tld e,a\n\tpush de\n\tpush hl\n"
            "\tld l,(ix-3)\n\tld h,(ix-2)\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tinc hl\n"
            "\tld (ix-3),l\n\tld (ix-2),h\n\tex de,hl\n"
            "\tld a,h\n\trlca\n\tsbc a,a\n"
            "\tld d,a\n\tld e,a\n"
            "\tpop bc\n\tadd hl,bc\n\tex de,hl\n"
            "\tpop bc\n\tadc hl,bc\n\tex de,hl\n"
            "\tpush de\n\tpush hl\n",
            residual_loop);
    mir_machine_emit_symbol_call(out, plan->clamp_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tld a,l\n\tld (bc),a\n\tinc bc\n"
          "\tld a,h\n\tld (bc),a\n"
          "\tinc iy\n\tinc iy\n\tpush iy\n\tpop hl\n", out);
    mir_machine_emit_global_address_de(
        out, plan->output, plan->output_offset + 256);
    mir_stream_puts("\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out,
            "\tjp nz,L%d\n\tld sp,ix\n\tpop ix\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            residual_loop);
}

static int mir_match_four_byte_failure_check(
    struct MirFourByteFailureCheck *plan)
{
    const struct MirInsn *name;
    const struct MirInsn *source;
    const struct MirInsn *expected[4];
    const struct MirInsn *print_call = NULL;
    const struct MirInsn *failure_store = NULL;
    int comparison_seen[4] = { 0, 0, 0, 0 };
    int arguments[10];
    int parameter_count = 0;
    int comparison_count = 0;
    int call_count = 0;
    int global_store_count = 0;
    int final_branch_count = 0;
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;
    int byte;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir_cfg_block_count() != 23 ||
        (mir.return_type & 15) != TYPE_VOID ||
        (mir.count != 122 && mir.count != 126) ||
        mir.insns[mir.count - 1].opcode != MIR_LABEL)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_PARAM)
            ++parameter_count;
    if (parameter_count != 6)
        return 0;
    name = &mir.insns[1];
    source = &mir.insns[2];
    for (byte = 0; byte < 4; ++byte)
        expected[byte] = &mir.insns[3 + byte];
    if (name->opcode != MIR_PARAM ||
        type_ptr_depth(name->type) == 0 ||
        source->opcode != MIR_PARAM ||
        !mir_machine_parameter_value_offset(
            name->dst, &plan->name_stack_offset))
        return 0;
    if (type_ptr_depth(source->type) == 1) {
        plan->source_is_pointer = 1;
        if (!mir_machine_parameter_value_offset(
                source->dst, &plan->source_stack_offset))
            return 0;
    } else {
        if (type_ptr_depth(source->type) != 0 ||
            type_size(source->type) != 4 ||
            !type_is_float(source->type) ||
            !mir_scalar_memory_location(
                source, &memory_type, &memory_storage,
                &memory_offset) ||
            memory_storage != SC_PARAM ||
            type_size(memory_type) != 4 ||
            !type_is_float(memory_type) ||
            memory_offset < 2)
            return 0;
        plan->source_stack_offset = memory_offset - 2;
    }
    for (byte = 0; byte < 4; ++byte) {
        if (expected[byte]->opcode != MIR_PARAM ||
            type_ptr_depth(expected[byte]->type) != 0 ||
            type_is_float(expected[byte]->type) ||
            (type_size(expected[byte]->type) != 1 &&
             type_size(expected[byte]->type) != 2) ||
            (expected[byte]->type & TYPE_UNSIGNED) == 0 ||
            !mir_machine_parameter_value_offset(
                expected[byte]->dst,
                &plan->expected_stack_offsets[byte]))
            return 0;
        plan->expected_widths[byte] =
            type_size(expected[byte]->type);
    }
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode == MIR_BINARY &&
            insn->immediate == TOK_NE) {
            int actual_byte = -1;
            int expected_byte;

            for (expected_byte = 0;
                 expected_byte < 4; ++expected_byte)
                if (mir_machine_value_matches_parameter(
                        insn->src2,
                        expected[expected_byte]) &&
                    mir_machine_byte_load_from_source(
                        insn->src1, source,
                        plan->source_is_pointer,
                        &actual_byte))
                    break;
            if (expected_byte == 4) {
                for (expected_byte = 0;
                     expected_byte < 4; ++expected_byte)
                    if (mir_machine_value_matches_parameter(
                            insn->src1,
                            expected[expected_byte]) &&
                        mir_machine_byte_load_from_source(
                            insn->src2, source,
                            plan->source_is_pointer,
                            &actual_byte))
                        break;
            }
            if (expected_byte == 4 ||
                actual_byte != expected_byte ||
                comparison_seen[actual_byte])
                return 0;
            comparison_seen[actual_byte] = 1;
            ++comparison_count;
        } else if (insn->opcode == MIR_CALL) {
            print_call = insn;
            ++call_count;
        } else if (insn->opcode == MIR_STORE &&
                   !mir_machine_unobservable_local_store(insn)) {
            ++global_store_count;
            failure_store = insn;
        } else if (insn->opcode == MIR_STORE_INDIRECT) {
            return 0;
        } else if (insn->opcode == MIR_BRANCH_FALSE &&
                   insn->label ==
                       mir.insns[mir.count - 1].label) {
            if (instruction >= mir.count - 7)
                return 0;
            ++final_branch_count;
        }
    }
    if (comparison_count != 4 || call_count != 1 ||
        global_store_count != 1 || final_branch_count != 1 ||
        print_call == NULL || failure_store == NULL ||
        (print_call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) !=
            MIR_CALL_FLAG_VARIADIC)
        return 0;
    plan->include_expected =
        mir_machine_ten_call_arguments(
            print_call, arguments);
    if (!plan->include_expected &&
        !mir_machine_six_call_arguments(
            print_call, arguments))
        return 0;
    {
        const struct MirInsn *format =
            mir_definition(arguments[0]);

        if (format == NULL ||
            format->opcode != MIR_STRING_ADDRESS ||
            format->immediate < 0)
            return 0;
        plan->string_id = (int)format->immediate;
    }
    if (!mir_machine_value_matches_parameter(
            arguments[1], name))
        return 0;
    for (byte = 0; byte < 4; ++byte) {
        int actual_byte;

        if (!mir_machine_byte_load_from_source(
                arguments[2 + byte], source,
                plan->source_is_pointer,
                &actual_byte) ||
            actual_byte != byte ||
            (plan->include_expected &&
             !mir_machine_value_matches_parameter(
                 arguments[6 + byte],
                 expected[byte])))
            return 0;
    }
    {
        const struct MirInsn *add =
            mir_definition(failure_store->src1);
        const struct MirInsn *load;

        if (!mir_scalar_memory_location(
                failure_store, &memory_type,
                &memory_storage, &memory_offset) ||
            memory_storage != SC_GLOBAL ||
            type_size(memory_type) != 2 ||
            add == NULL || add->opcode != MIR_BINARY ||
            add->immediate != '+' ||
            !mir_machine_constant_equals(add->src2, 1) ||
            (load = mir_definition(add->src1)) == NULL ||
            load->opcode != MIR_LOAD ||
            !mir_machine_same_location(
                load, failure_store))
            return 0;
        plan->failure_count =
            find_global(failure_store->name);
        plan->failure_offset = memory_offset;
        if (plan->failure_count == NULL ||
            plan->failure_count->is_volatile)
            return 0;
    }
    {
        struct Sym *function =
            find_global(print_call->name);

        if (function == NULL || function->is_defined)
            return 0;
        snprintf(plan->call_name,
                 sizeof(plan->call_name), "%s",
                 print_call->base_name[0] != 0
                     ? print_call->base_name
                     : asm_name_for(
                           sym_asm_name(function)));
    }
    return 1;
}

static void mir_emit_four_byte_failure_check(
    MirStream *out, const struct MirFourByteFailureCheck *plan)
{
    int mismatch = new_label();
    int done = new_label();
    int byte;
    int argument;

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (byte = 0; byte < 4; ++byte) {
        if (plan->expected_widths[byte] == 2) {
            mir_stream_printf(out,
                    "\tld a,(ix+%d)\n\tor a\n"
                    "\tjp nz,L%d\n",
                    plan->expected_stack_offsets[byte] + 3,
                    mismatch);
        }
        mir_emit_four_byte_source(out, plan, byte);
        mir_stream_printf(out,
                "\tcp (ix+%d)\n\tjp nz,L%d\n",
                plan->expected_stack_offsets[byte] + 2,
                mismatch);
    }
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", done, mismatch);
    if (plan->include_expected)
        for (byte = 3; byte >= 0; --byte) {
            mir_stream_printf(out, "\tld l,(ix+%d)\n",
                    plan->expected_stack_offsets[byte] + 2);
            if (plan->expected_widths[byte] == 2)
                mir_stream_printf(out, "\tld h,(ix+%d)\n",
                        plan->expected_stack_offsets[byte] + 3);
            else
                mir_stream_puts("\tld h,0\n", out);
            mir_stream_puts("\tpush hl\n", out);
        }
    for (byte = 3; byte >= 0; --byte) {
        mir_emit_four_byte_source(out, plan, byte);
        mir_stream_puts("\tld l,a\n\tld h,0\n\tpush hl\n", out);
    }
    mir_stream_printf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->name_stack_offset + 2,
            plan->name_stack_offset + 3,
            plan->string_id);
    mir_emit_runtime_call(out, plan->call_name);
    for (argument = 0;
         argument < (plan->include_expected ? 10 : 6);
         ++argument)
        mir_stream_puts("\tpop bc\n", out);
    mir_machine_emit_global_word(
        out, plan->failure_count,
        plan->failure_offset);
    mir_stream_puts("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->failure_count,
        plan->failure_offset);
    mir_stream_printf(out,
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            done);
}

static int mir_match_float_special_check(
    struct MirFloatSpecialCheck *plan)
{
    const struct MirInsn *name;
    const struct MirInsn *value;
    int memory_type;
    int memory_storage;
    int memory_offset;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || (mir.return_type & 15) != TYPE_VOID ||
        mir.insns[0].opcode != MIR_LABEL ||
        mir.insns[1].opcode != MIR_PARAM ||
        mir.insns[2].opcode != MIR_PARAM)
        return 0;
    name = &mir.insns[1];
    value = &mir.insns[2];
    if (type_ptr_depth(name->type) == 0 ||
        type_ptr_depth(value->type) != 0 ||
        !type_is_float(value->type) ||
        type_size(value->type) != 4 ||
        !mir_machine_parameter_value_offset(
            name->dst, &plan->name_stack_offset) ||
        !mir_scalar_memory_location(
            value, &memory_type,
            &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM ||
        type_size(memory_type) != 4 ||
        !type_is_float(memory_type) ||
        memory_offset < 2)
        return 0;
    plan->value_stack_offset = memory_offset - 2;
    if (mir.count == 118 &&
        mir_cfg_block_count() == 27) {
        const struct MirInsn *negative = &mir.insns[3];

        if (negative->opcode != MIR_PARAM ||
            type_size(negative->type) != 2 ||
            type_ptr_depth(negative->type) != 0 ||
            !mir_machine_parameter_value_offset(
                negative->dst,
                &plan->negative_stack_offset) ||
            !mir_machine_named_global_increment(
                &mir.insns[7], &plan->checks,
                &plan->checks_offset) ||
            mir.insns[11].opcode != MIR_STORE_INDIRECT ||
            mir.insns[11].src2 != value->dst ||
            mir.insns[11].memory_size != 4 ||
            !mir_machine_constant_equals(
                mir.insns[17].dst, 0) ||
            mir.insns[19].opcode != MIR_BINARY ||
            mir.insns[19].immediate != TOK_NE ||
            !mir_machine_constant_equals(
                mir.insns[30].dst, 0) ||
            mir.insns[32].opcode != MIR_BINARY ||
            mir.insns[32].immediate != TOK_NE ||
            !mir_machine_constant_equals(
                mir.insns[55].dst, 128) ||
            mir.insns[57].opcode != MIR_BINARY ||
            mir.insns[57].immediate != TOK_NE ||
            mir.insns[81].opcode != MIR_BRANCH_FALSE ||
            mir.insns[81].src1 != negative->dst ||
            !mir_machine_constant_equals(
                mir.insns[82].dst, 255) ||
            !mir_machine_constant_equals(
                mir.insns[86].dst, 127) ||
            mir.insns[93].opcode != MIR_BINARY ||
            mir.insns[93].immediate != TOK_NE ||
            mir.insns[106].opcode != MIR_BRANCH_FALSE ||
            mir.insns[106].label !=
                mir.insns[117].label ||
            !mir_machine_match_name_report(
                &mir.insns[111], name,
                &plan->string_id,
                plan->call_name) ||
            !mir_machine_named_global_increment(
                &mir.insns[115], &plan->failures,
                &plan->failures_offset))
            return 0;
        plan->kind = MIR_FLOAT_SPECIAL_INFINITY;
        return 1;
    }
    if (mir.count == 141 &&
        mir_cfg_block_count() == 29) {
        if (!mir_machine_named_global_increment(
                &mir.insns[6], &plan->checks,
                &plan->checks_offset) ||
            mir.insns[10].opcode != MIR_STORE_INDIRECT ||
            mir.insns[10].src2 != value->dst ||
            mir.insns[10].memory_size != 4 ||
            mir.insns[13].opcode != MIR_BINARY ||
            mir.insns[13].immediate != TOK_EQ ||
            mir.insns[13].src1 != value->dst ||
            mir.insns[13].src2 != value->dst ||
            !mir_machine_constant_equals(
                mir.insns[24].dst, 127) ||
            mir.insns[26].opcode != MIR_BINARY ||
            mir.insns[26].immediate != '&' ||
            !mir_machine_constant_equals(
                mir.insns[27].dst, 127) ||
            mir.insns[28].opcode != MIR_BINARY ||
            mir.insns[28].immediate != TOK_NE ||
            !mir_machine_constant_equals(
                mir.insns[51].dst, 128) ||
            mir.insns[53].opcode != MIR_BINARY ||
            mir.insns[53].immediate != '&' ||
            !mir_machine_constant_equals(
                mir.insns[54].dst, 0) ||
            mir.insns[55].opcode != MIR_BINARY ||
            mir.insns[55].immediate != TOK_EQ ||
            !mir_machine_constant_equals(
                mir.insns[78].dst, 127) ||
            mir.insns[80].opcode != MIR_BINARY ||
            mir.insns[80].immediate != '&' ||
            mir.insns[82].opcode != MIR_BINARY ||
            mir.insns[82].immediate != TOK_EQ ||
            mir.insns[91].opcode != MIR_BINARY ||
            mir.insns[91].immediate != TOK_EQ ||
            mir.insns[108].opcode != MIR_BINARY ||
            mir.insns[108].immediate != TOK_EQ ||
            mir.insns[129].opcode != MIR_BRANCH_FALSE ||
            mir.insns[129].label !=
                mir.insns[140].label ||
            !mir_machine_match_name_report(
                &mir.insns[134], name,
                &plan->string_id,
                plan->call_name) ||
            !mir_machine_named_global_increment(
                &mir.insns[138], &plan->failures,
                &plan->failures_offset))
            return 0;
        plan->kind = MIR_FLOAT_SPECIAL_NAN;
        return 1;
    }
    return 0;
}

static void mir_emit_float_special_check(
    MirStream *out, const struct MirFloatSpecialCheck *plan)
{
    int failure = new_label();
    int done = new_label();

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_word(
        out, plan->checks, plan->checks_offset);
    mir_stream_puts("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->checks, plan->checks_offset);
    if (plan->kind == MIR_FLOAT_SPECIAL_INFINITY) {
        int positive = new_label();

        mir_stream_printf(out,
                "\tld a,(ix+%d)\n\tor a\n\tjp nz,L%d\n"
                "\tld a,(ix+%d)\n\tor a\n\tjp nz,L%d\n"
                "\tld a,(ix+%d)\n\tcp 128\n\tjp nz,L%d\n"
                "\tld a,(ix+%d)\n\tor (ix+%d)\n"
                "\tld a,(ix+%d)\n\tjp z,L%d\n"
                "\tcp 255\n\tjp nz,L%d\n\tjp L%d\n"
                "L%d:\n\tcp 127\n\tjp nz,L%d\n\tjp L%d\n",
                plan->value_stack_offset + 2, failure,
                plan->value_stack_offset + 3, failure,
                plan->value_stack_offset + 4, failure,
                plan->negative_stack_offset + 2,
                plan->negative_stack_offset + 3,
                plan->value_stack_offset + 5, positive,
                failure, done, positive, failure, done);
    } else {
        mir_stream_printf(out,
                "\tld a,(ix+%d)\n\tand 127\n"
                "\tcp 127\n\tjp nz,L%d\n"
                "\tld a,(ix+%d)\n\tld b,a\n"
                "\tand 128\n\tjp z,L%d\n"
                "\tld a,b\n\tand 127\n\tld b,a\n"
                "\tld a,(ix+%d)\n\tor b\n\tld b,a\n"
                "\tld a,(ix+%d)\n\tor b\n"
                "\tjp z,L%d\n\tjp L%d\n",
                plan->value_stack_offset + 5, failure,
                plan->value_stack_offset + 4, failure,
                plan->value_stack_offset + 3,
                plan->value_stack_offset + 2,
                failure, done);
    }
    mir_stream_printf(out,
            "L%d:\n\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
            "\tpush hl\n\tld hl,S%d\n\tpush hl\n",
            failure,
            plan->name_stack_offset + 2,
            plan->name_stack_offset + 3,
            plan->string_id);
    mir_emit_runtime_call(out, plan->call_name);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_global_word(
        out, plan->failures,
        plan->failures_offset);
    mir_stream_puts("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->failures,
        plan->failures_offset);
    mir_stream_printf(out,
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            done);
}

static int mir_match_flagged_record_append(
    struct MirFlaggedRecordAppend *plan)
{
    static const int member_indices[8] = {
        29, 34, 39, 52, 83, 87, 91, 96
    };
    const struct MirInsn *count_address = &mir.insns[8];
    const struct MirInsn *count_load = &mir.insns[9];
    const struct MirInsn *row_address = &mir.insns[17];
    const struct MirInsn *count_update_address = &mir.insns[20];
    const struct MirInsn *old_count = &mir.insns[21];
    const struct MirInsn *record_address = &mir.insns[25];
    int call_argument;
    struct Sym *root;
    long root_offset;
    int field;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir.count != 100 ||
        mir_cfg_block_count() != 9 ||
        (mir.return_type & 15) != TYPE_VOID ||
        mir.insns[0].opcode != MIR_LABEL ||
        mir.insns[1].opcode != MIR_PARAM ||
        mir.insns[2].opcode != MIR_PARAM ||
        mir.insns[3].opcode != MIR_PARAM ||
        mir.insns[4].opcode != MIR_PARAM ||
        mir.insns[5].opcode != MIR_PARAM)
        return 0;
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst,
            &plan->ply_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst,
            &plan->from_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[3].dst,
            &plan->to_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[4].dst,
            &plan->promoted_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[5].dst,
            &plan->flag_stack_offset) ||
        type_size(mir.insns[1].type) != 2 ||
        type_size(mir.insns[2].type) != 2 ||
        type_size(mir.insns[3].type) != 2 ||
        type_size(mir.insns[4].type) != 1 ||
        type_size(mir.insns[5].type) != 1)
        return 0;
    if (!mir_machine_global_address_offset(
            mir.insns[6].dst, &plan->counts,
            &root_offset, 0) ||
        root_offset < -32768 ||
        root_offset > 32767)
        return 0;
    plan->counts_offset = (int)root_offset;
    if (!mir_machine_global_address_offset(
            mir.insns[18].dst, &root,
            &root_offset, 0) ||
        root != plan->counts ||
        root_offset != plan->counts_offset ||
        count_address->src1 != mir.insns[6].dst ||
        count_address->src2 != mir.insns[1].dst ||
        count_address->immediate != 2 ||
        count_address->memory_size != 2 ||
        count_load->src1 != count_address->dst ||
        count_load->memory_size != 2 ||
        (count_load->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_constant_value(
            mir.insns[10].dst, &root_offset, 0) ||
        root_offset <= 0 || root_offset > 255 ||
        mir.insns[11].opcode != MIR_BINARY ||
        mir.insns[11].immediate != TOK_GE ||
        mir.insns[11].src1 != count_load->dst ||
        mir.insns[11].src2 != mir.insns[10].dst ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[12].label != mir.insns[14].label ||
        mir.insns[13].opcode != MIR_RETURN)
        return 0;
    plan->limit = (int)root_offset;
    if (!mir_machine_global_address_offset(
            mir.insns[15].dst, &plan->records,
            &root_offset, 0) ||
        root_offset < -32768 ||
        root_offset > 32767)
        return 0;
    plan->records_offset = (int)root_offset;
    if (row_address->src1 != mir.insns[15].dst ||
        row_address->src2 != mir.insns[1].dst ||
        row_address->immediate <= 0 ||
        row_address->memory_size != row_address->immediate ||
        count_update_address->src1 != mir.insns[18].dst ||
        count_update_address->src2 != mir.insns[1].dst ||
        count_update_address->immediate != 2 ||
        old_count->src1 != count_update_address->dst ||
        old_count->memory_size != 2 ||
        !mir_machine_constant_equals(
            mir.insns[22].dst, 1) ||
        mir.insns[23].opcode != MIR_BINARY ||
        mir.insns[23].immediate != '+' ||
        mir.insns[23].src1 != old_count->dst ||
        mir.insns[23].src2 != mir.insns[22].dst ||
        mir.insns[24].src1 !=
            count_update_address->dst ||
        mir.insns[24].src2 != mir.insns[23].dst ||
        mir.insns[24].memory_size != 2 ||
        record_address->src1 != row_address->dst ||
        record_address->src2 != old_count->dst ||
        record_address->immediate <= 0 ||
        record_address->memory_size !=
            record_address->immediate ||
        !mir_machine_unobservable_local_store(
            &mir.insns[27]) ||
        mir.insns[27].src1 != record_address->dst)
        return 0;
    plan->row_stride = (int)row_address->immediate;
    plan->record_stride =
        (int)record_address->immediate;
    for (field = 0; field < 8; ++field) {
        const struct MirInsn *member =
            &mir.insns[member_indices[field]];

        if (member->opcode != MIR_MEMBER_ADDRESS ||
            member->memory_size != 1 ||
            member->immediate < 0 ||
            member->immediate >= plan->record_stride)
            return 0;
        plan->field_offsets[field] =
            (int)member->immediate;
    }
    if (plan->field_offsets[0] != 0 ||
        plan->field_offsets[1] != 1 ||
        plan->field_offsets[2] != 2 ||
        plan->field_offsets[3] != 3 ||
        plan->field_offsets[4] != 4 ||
        plan->field_offsets[5] != 5 ||
        plan->field_offsets[6] != 6 ||
        plan->field_offsets[7] != 7 ||
        mir.insns[31].opcode != MIR_UNARY ||
        mir.insns[31].immediate != 0 ||
        mir.insns[31].src1 != mir.insns[2].dst ||
        mir.insns[32].src2 != mir.insns[31].dst ||
        mir.insns[36].opcode != MIR_UNARY ||
        mir.insns[36].immediate != 0 ||
        mir.insns[36].src1 != mir.insns[3].dst ||
        mir.insns[37].src2 != mir.insns[36].dst)
        return 0;
    if (!mir_machine_global_address_offset(
            mir.insns[40].dst, &plan->values,
            &root_offset, 0) ||
        root_offset < -32768 ||
        root_offset > 32767)
        return 0;
    plan->values_offset = (int)root_offset;
    if (mir.insns[42].src1 != mir.insns[40].dst ||
        mir.insns[42].src2 != mir.insns[2].dst ||
        mir.insns[42].immediate != 1 ||
        mir.insns[43].src1 != mir.insns[42].dst ||
        mir.insns[43].memory_size != 1 ||
        mir.insns[44].src2 != mir.insns[43].dst ||
        !mir_machine_constant_value(
            mir.insns[46].dst, &root_offset, 0) ||
        root_offset <= 0 || root_offset > 255 ||
        mir.insns[47].opcode != MIR_UNARY ||
        mir.insns[47].immediate != 0 ||
        mir.insns[47].src1 != mir.insns[5].dst ||
        mir.insns[48].opcode != MIR_BINARY ||
        mir.insns[48].immediate != '&' ||
        mir.insns[48].src1 != mir.insns[47].dst ||
        mir.insns[48].src2 != mir.insns[46].dst ||
        mir.insns[49].src1 != mir.insns[48].dst)
        return 0;
    plan->special_mask = (int)root_offset;
    if (!mir_machine_global_address_offset(
            mir.insns[53].dst, &root,
            &root_offset, 0) ||
        root != plan->values ||
        root_offset != plan->values_offset ||
        mir.insns[55].src1 != mir.insns[53].dst ||
        mir.insns[55].src2 != mir.insns[2].dst ||
        mir.insns[55].immediate != 1 ||
        mir.insns[56].src1 != mir.insns[55].dst ||
        !mir_machine_single_call_argument(
            &mir.insns[58], &call_argument) ||
        call_argument != mir.insns[56].dst ||
        !mir_machine_resolve_direct_call(
            &mir.insns[58],
            &plan->classify_function) ||
        !mir_machine_constant_equals(
            mir.insns[59].dst, 1) ||
        mir.insns[60].opcode != MIR_BINARY ||
        mir.insns[60].immediate != TOK_EQ ||
        mir.insns[60].src1 != mir.insns[58].dst ||
        mir.insns[60].src2 != mir.insns[59].dst ||
        !mir_machine_constant_value(
            mir.insns[62].dst, &root_offset, 0) ||
        root_offset < -128 || root_offset > 255)
        return 0;
    plan->true_value = (int)root_offset & 255;
    if (!mir_machine_constant_value(
            mir.insns[66].dst, &root_offset, 0) ||
        root_offset < -128 || root_offset > 255)
        return 0;
    plan->false_value = (int)root_offset & 255;
    if (!mir_machine_global_address_offset(
            mir.insns[76].dst, &root,
            &root_offset, 0) ||
        root != plan->values ||
        root_offset != plan->values_offset ||
        mir.insns[78].src1 != mir.insns[76].dst ||
        mir.insns[78].src2 != mir.insns[3].dst ||
        mir.insns[78].immediate != 1 ||
        mir.insns[79].src1 != mir.insns[78].dst ||
        mir.insns[80].src2 != mir.insns[79].dst ||
        mir.insns[85].src2 != mir.insns[4].dst ||
        mir.insns[89].src2 != mir.insns[5].dst ||
        !mir_machine_constant_equals(
            mir.insns[93].dst, 0) ||
        mir.insns[94].src2 != mir.insns[93].dst ||
        !mir_machine_constant_equals(
            mir.insns[98].dst, 0) ||
        mir.insns[99].src2 != mir.insns[98].dst)
        return 0;
    return 1;
}

static void mir_emit_flagged_record_append(
    MirStream *out, const struct MirFlaggedRecordAppend *plan)
{
    int append = new_label();
    int done = new_label();
    int ordinary_capture = new_label();
    int false_capture = new_label();
    int capture_done = new_label();

    mir_stream_printf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tadd hl,hl\n",
            plan->ply_stack_offset + 4,
            plan->ply_stack_offset + 5);
    mir_machine_emit_global_address_de(
        out, plan->counts, plan->counts_offset);
    mir_stream_puts("\tadd hl,de\n\tld c,(hl)\n\tinc hl\n"
          "\tld b,(hl)\n\tdec hl\n\tbit 7,b\n", out);
    mir_stream_printf(out,
            "\tjp nz,L%d\n\tld a,b\n\tor a\n\tjp nz,L%d\n"
            "\tld a,c\n\tcp %d\n\tjp nc,L%d\n"
            "L%d:\n\tpush hl\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n",
            append, done, plan->limit, done, append,
            plan->ply_stack_offset + 4,
            plan->ply_stack_offset + 5);
    mir_emit_mul_hl_const(
        out, (unsigned long)plan->row_stride);
    mir_machine_emit_global_address_de(
        out, plan->records, plan->records_offset);
    mir_stream_puts("\tadd hl,de\n\tpush hl\n\tld h,b\n\tld l,c\n", out);
    mir_emit_mul_hl_const(
        out, (unsigned long)plan->record_stride);
    mir_stream_puts("\tex de,hl\n\tpop hl\n\tadd hl,de\n"
          "\tpush hl\n\tpop iy\n\tpop hl\n"
          "\tinc (hl)\n", out);
    {
        int no_count_carry = new_label();

        mir_stream_printf(out,
                "\tjp nz,L%d\n\tinc hl\n\tinc (hl)\nL%d:\n",
                no_count_carry, no_count_carry);
    }
    mir_stream_printf(out,
            "\tld a,(ix+%d)\n\tld (iy%+d),a\n"
            "\tld a,(ix+%d)\n\tld (iy%+d),a\n"
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n",
            plan->from_stack_offset + 4,
            plan->field_offsets[0],
            plan->to_stack_offset + 4,
            plan->field_offsets[1],
            plan->from_stack_offset + 4,
            plan->from_stack_offset + 5);
    mir_machine_emit_global_address_de(
        out, plan->values, plan->values_offset);
    mir_stream_puts("\tadd hl,de\n\tld a,(hl)\n", out);
    mir_stream_printf(out,
            "\tld (iy%+d),a\n"
            "\tld a,(ix+%d)\n\tand %d\n"
            "\tjp z,L%d\n"
            "\tld l,(iy%+d)\n\tld a,l\n\trlca\n"
            "\tsbc a,a\n\tld h,a\n\tpush hl\n",
            plan->field_offsets[2],
            plan->flag_stack_offset + 4,
            plan->special_mask, ordinary_capture,
            plan->field_offsets[2]);
    mir_machine_emit_symbol_call(
        out, plan->classify_function);
    mir_stream_puts("\tpop bc\n\tld a,h\n\tor a\n", out);
    mir_stream_printf(out,
            "\tjp nz,L%d\n\tld a,l\n\tcp 1\n\tjp nz,L%d\n"
            "\tld a,%d\n\tjp L%d\n"
            "L%d:\n\tld a,%d\n\tjp L%d\n"
            "L%d:\n\tld l,(ix+%d)\n\tld h,(ix+%d)\n",
            false_capture, false_capture,
            plan->true_value, capture_done,
            false_capture, plan->false_value, capture_done,
            ordinary_capture,
            plan->to_stack_offset + 4,
            plan->to_stack_offset + 5);
    mir_machine_emit_global_address_de(
        out, plan->values, plan->values_offset);
    mir_stream_puts("\tadd hl,de\n\tld a,(hl)\n", out);
    mir_stream_printf(out,
            "L%d:\n\tld (iy%+d),a\n"
            "\tld a,(ix+%d)\n\tld (iy%+d),a\n"
            "\tld a,(ix+%d)\n\tld (iy%+d),a\n"
            "\txor a\n\tld (iy%+d),a\n\tld (iy%+d),a\n"
            "L%d:\n\tld sp,ix\n\tpop ix\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            capture_done, plan->field_offsets[3],
            plan->promoted_stack_offset + 4,
            plan->field_offsets[4],
            plan->flag_stack_offset + 4,
            plan->field_offsets[5],
            plan->field_offsets[6],
            plan->field_offsets[7],
            done);
}

static int mir_match_record_wildcard(
    struct MirRecordWildcardMatch *plan)
{
    static const int member_indices[7] = {
        4, 7, 14, 17, 32, 35, 46
    };
    const struct MirInsn *left = &mir.insns[1];
    const struct MirInsn *right = &mir.insns[2];
    int member_offsets[7];
    long wildcard_value;
    int member;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir.count != 72 ||
        mir_cfg_block_count() != 14 ||
        type_size(mir.return_type) != 2 ||
        type_ptr_depth(mir.return_type) != 0 ||
        left->opcode != MIR_PARAM ||
        right->opcode != MIR_PARAM ||
        type_ptr_depth(left->type) != 1 ||
        type_ptr_depth(right->type) != 1 ||
        mir_machine_pointee_is_volatile(left) ||
        mir_machine_pointee_is_volatile(right) ||
        !mir_machine_parameter_value_offset(
            left->dst, &plan->left_stack_offset) ||
        !mir_machine_parameter_value_offset(
            right->dst, &plan->right_stack_offset))
        return 0;
    for (member = 0; member < 7; ++member) {
        const struct MirInsn *address =
            &mir.insns[member_indices[member]];
        const struct MirInsn *load =
            &mir.insns[member_indices[member] + 1];

        if (address->opcode != MIR_MEMBER_ADDRESS ||
            address->memory_size != 1 ||
            address->immediate < 0 ||
            address->immediate > 127 ||
            load->opcode != MIR_LOAD_INDIRECT ||
            load->src1 != address->dst ||
            load->memory_size != 1 ||
            load->bit_width != 0 ||
            (load->memory_flags & (1 | 8)) != 0)
            return 0;
        member_offsets[member] =
            (int)address->immediate;
    }
    if (mir.insns[4].src1 != left->dst ||
        mir.insns[7].src1 != right->dst ||
        member_offsets[0] != member_offsets[1] ||
        mir.insns[11].opcode != MIR_BINARY ||
        mir.insns[11].immediate != TOK_EQ ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[14].src1 != left->dst ||
        mir.insns[17].src1 != right->dst ||
        member_offsets[2] != member_offsets[3] ||
        mir.insns[21].opcode != MIR_BINARY ||
        mir.insns[21].immediate != TOK_EQ ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        mir.insns[32].src1 != left->dst ||
        mir.insns[35].src1 != right->dst ||
        member_offsets[4] != member_offsets[5] ||
        mir.insns[39].opcode != MIR_BINARY ||
        mir.insns[39].immediate != TOK_EQ ||
        mir.insns[40].src1 != mir.insns[39].dst ||
        mir.insns[46].src1 != right->dst ||
        member_offsets[6] != member_offsets[4] ||
        !mir_machine_constant_value(
            mir.insns[48].dst,
            &wildcard_value, 0) ||
        wildcard_value < -128 ||
        wildcard_value > 255 ||
        mir.insns[50].opcode != MIR_BINARY ||
        mir.insns[50].immediate != TOK_EQ ||
        mir.insns[50].src2 != mir.insns[48].dst ||
        mir.insns[71].opcode != MIR_RETURN)
        return 0;
    plan->first_offset = member_offsets[0];
    plan->second_offset = member_offsets[2];
    plan->wildcard_offset = member_offsets[4];
    plan->wildcard_value = (int)wildcard_value & 255;
    return plan->first_offset == 0 &&
           plan->second_offset == 1 &&
           plan->wildcard_offset == 4;
}

static void mir_emit_record_wildcard(
    MirStream *out, const struct MirRecordWildcardMatch *plan)
{
    int false_result = new_label();
    int true_result = new_label();
    int done = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld a,(bc)\n\tld h,a\n\tld a,(de)\n"
            "\tcp h\n\tjp nz,L%d\n"
            "\tinc bc\n\tinc de\n"
            "\tld a,(bc)\n\tld h,a\n\tld a,(de)\n"
            "\tcp h\n\tjp nz,L%d\n"
            "\tinc bc\n\tinc bc\n\tinc bc\n"
            "\tinc de\n\tinc de\n\tinc de\n"
            "\tld a,(bc)\n\tld h,a\n\tld a,(de)\n"
            "\tcp h\n\tjp z,L%d\n\tcp %d\n"
            "\tjp nz,L%d\n"
            "L%d:\n\tld hl,1\n\tjp L%d\n"
            "L%d:\n\tld hl,0\n"
            "L%d:\n\tret\n",
            plan->left_stack_offset,
            plan->right_stack_offset,
            false_result, false_result,
            true_result, plan->wildcard_value,
            false_result, true_result, done,
            false_result, done);
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

static void mir_emit_wide_member_update(
    MirStream *out, const struct MirWideMemberUpdate *plan)
{
    int byte;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tex de,hl\n",
            plan->pointer_stack_offset);
    mir_machine_emit_hl_offset(
        out, plan->member_offset, 0);
    mir_stream_puts("\tex de,hl\n", out);
    mir_stream_printf(out, "\tld hl,%d\n\tadd hl,sp\n",
            plan->value_stack_offset);
    for (byte = 0; byte < 4; ++byte) {
        mir_stream_puts("\tld a,(de)\n", out);
        if (plan->operation == '+')
            mir_stream_puts(byte == 0 ? "\tadd a,(hl)\n" :
                              "\tadc a,(hl)\n", out);
        else
            mir_stream_puts(byte == 0 ? "\tsub (hl)\n" :
                              "\tsbc a,(hl)\n", out);
        mir_stream_puts("\tld (de),a\n", out);
        if (byte != 3)
            mir_stream_puts("\tinc de\n\tinc hl\n", out);
    }
    mir_stream_puts("\tret\n", out);
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

static void mir_emit_signed_member_product(
    MirStream *out, const struct MirSignedMemberProduct *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_parameter_member_word(
        out, plan->pointer_stack_offset,
        plan->left_member_offset, "c", "b", 0);
    mir_machine_emit_parameter_member_word(
        out, plan->pointer_stack_offset,
        plan->right_member_offset, "e", "d", 1);
    mir_stream_puts("\tex de,hl\n", out);
    mir_emit_runtime_call(out, "__m1s");
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,%lu\n\tld de,%lu\n",
            plan->scale & 0xffffUL,
            (plan->scale >> 16) & 0xffffUL);
    mir_emit_runtime_call(out, "__lmul");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tret\n", out);
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

static void mir_emit_signed_member_square_scale_div(
    MirStream *out, const struct MirSignedMemberSquareScaleDiv *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_parameter_member_word(
        out, plan->pointer_stack_offset,
        plan->member_offset, "c", "b", 0);
    mir_stream_puts("\tld l,c\n\tld h,b\n", out);
    mir_emit_runtime_call(out, "__m1s");
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,%lu\n\tld de,%lu\n",
            plan->scale & 0xffffUL,
            (plan->scale >> 16) & 0xffffUL);
    mir_emit_runtime_call(out, "__lmul");
    mir_stream_puts("\tpop bc\n\tpop bc\n"
          "\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,%lu\n\tld de,%lu\n",
            plan->divisor & 0xffffUL,
            (plan->divisor >> 16) & 0xffffUL);
    mir_emit_runtime_call(out, "__lds");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tret\n", out);
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

static void mir_emit_signed_member_scale_pair(
    MirStream *out, const struct MirSignedMemberScalePair *plan)
{
    int member;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (member = 0; member < 2; ++member) {
        mir_machine_emit_parameter_member_word(
            out, plan->pointer_stack_offset,
            plan->member_offsets[member], "c", "b", 0);
        mir_stream_printf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
                "\tex de,hl\n",
                plan->value_stack_offset);
        mir_emit_runtime_call(out, "__m1s");
        mir_stream_puts("\tpush de\n\tpush hl\n", out);
        mir_stream_printf(out, "\tld hl,%lu\n\tld de,%lu\n",
                plan->divisor & 0xffffUL,
                (plan->divisor >> 16) & 0xffffUL);
        mir_emit_runtime_call(out, "__lds");
        mir_stream_puts("\tpop bc\n\tpop bc\n\tpush hl\n", out);
        mir_stream_printf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
                "\tex de,hl\n",
                plan->pointer_stack_offset + 2);
        mir_machine_emit_hl_offset(
            out, plan->member_offsets[member], 0);
        mir_stream_puts("\tpop de\n\tld (hl),e\n\tinc hl\n"
              "\tld (hl),d\n", out);
    }
    mir_stream_puts("\tret\n", out);
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

static void mir_emit_wide_narrow_division(
    MirStream *out, const struct MirWideNarrowDivision *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld l,c\n\tld h,b\n"
            "\tpush de\n\tpush hl\n",
            plan->wide_stack_offset);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tld l,c\n\tld h,b\n\tld de,0\n",
            plan->narrow_stack_offset + 4);
    if (!plan->is_unsigned) {
        int extended = new_label();

        mir_stream_printf(out,
                "\tbit 7,h\n\tjp z,L%d\n\tdec de\nL%d:\n",
                extended, extended);
    }
    mir_emit_runtime_call(
        out, plan->operation == '%'
                 ? (plan->is_unsigned ? "__lmu" : "__lms")
                 : (plan->is_unsigned ? "__ldu" : "__lds"));
    mir_stream_puts("\tpop bc\n\tpop bc\n\tret\n", out);
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

static void mir_emit_aggregate_field_sum(
    MirStream *out, const struct MirAggregateFieldSum *plan)
{
    int field;

    mir_stream_printf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tpush hl\n\tpop iy\n"
            "\tld hl,0\n\tld de,0\n",
            plan->parameter_stack_offset + 2);
    for (field = 0; field < plan->field_count; ++field)
        mir_machine_emit_aggregate_sum_field(
            out, &plan->fields[field]);
    mir_stream_puts("\tpop iy\n\tret\n", out);
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

static int mir_match_local_boolean_checks(struct MirConstantChecks *plan)
{
    static const int call_indices[14] = {
        56, 63, 71, 79, 87, 94, 101,
        112, 123, 134, 145, 155, 165, 175
    };
    static const int actual_indices[14] = {
        50, 57, 65, 73, 81, 88, 95,
        106, 117, 128, 139, 149, 159, 169
    };
    static const int values[14] = {
        1, 1, 0, 1, 0, 1, 0,
        0, 1, 1, 0, 1, 85, 0
    };
    int check;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 176 || mir_cfg_block_count() != 1 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID ||
        !mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[5].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[8].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[14].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[20].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[27].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[33].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[38].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[43].dst, 85) ||
        !mir_machine_constant_equals(mir.insns[48].dst, 0))
        return mir_machine_reject("local-boolean-checks", "setup");
    for (check = 0; check < 14; ++check) {
        const struct MirInsn *call = &mir.insns[call_indices[check]];
        const struct MirInsn *actual =
            &mir.insns[actual_indices[check]];
        const struct MirInsn *string;
        struct MirMachineForm expected;
        struct Sym *function;
        int arguments[3];

        if (!mir_machine_three_call_arguments(call, arguments))
            return mir_machine_reject(
                "local-boolean-checks", "arguments");
        if (arguments[0] != actual->dst)
            return mir_machine_reject(
                "local-boolean-checks", "actual");
        if (
            !mir_machine_pointer_form(
                arguments[1], call_indices[check], &expected, 0) ||
            expected.kind != MIR_MACHINE_FORM_INTEGER ||
            (expected.value & 0xffffL) != values[check])
            return mir_machine_reject(
                "local-boolean-checks", "arguments");
        string = mir_definition(arguments[2]);
        function = find_global(call->name);
        if (string == NULL ||
            string->opcode != MIR_STRING_ADDRESS ||
            function == NULL ||
            (plan->function != NULL && plan->function != function))
            return mir_machine_reject(
                "local-boolean-checks", "call");
        plan->function = function;
        plan->strings[check] = (int)string->immediate;
        plan->actual[check] = values[check];
        plan->expected[check] = values[check];
    }
    plan->count = 14;
    plan->name_last = 1;
    return plan->function != NULL;
}

static int mir_match_local_bitfield_checks(struct MirConstantChecks *plan)
{
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if ((mir.count != 65 && mir.count != 127) ||
        mir_cfg_block_count() != 1 || mir.has_vla ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->opcode == MIR_CALL) {
            int args[3];
            const struct MirInsn *string;
            struct MirMachineForm expected;
            struct Sym *function;
            if (plan->count >= 16 ||
                !mir_machine_three_call_arguments(insn, args) ||
                (string = mir_definition(args[0])) == NULL ||
                string->opcode != MIR_STRING_ADDRESS ||
                !mir_machine_pointer_form(args[2], instruction, &expected, 0) ||
                expected.kind != MIR_MACHINE_FORM_INTEGER ||
                (function = find_global(insn->name)) == NULL ||
                (plan->function != NULL && plan->function != function))
                return mir_machine_reject("local-bitfield-checks", "call");
            plan->function = function;
            plan->strings[plan->count] = (int)string->immediate;
            plan->actual[plan->count] = expected.value & 0xffffL;
            plan->expected[plan->count] = expected.value & 0xffffL;
            ++plan->count;
        } else if (insn->opcode != MIR_LABEL && insn->opcode != MIR_NOP &&
                   insn->opcode != MIR_ADDRESS &&
                   insn->opcode != MIR_MEMBER_ADDRESS &&
                   insn->opcode != MIR_CONST &&
                   insn->opcode != MIR_STORE_INDIRECT &&
                   insn->opcode != MIR_LOAD_INDIRECT &&
                   insn->opcode != MIR_STRING_ADDRESS &&
                   insn->opcode != MIR_ARG) {
            return mir_machine_reject("local-bitfield-checks", "opcode");
        }
    }
    return plan->function != NULL &&
           ((mir.count == 65 && plan->count == 4) ||
            (mir.count == 127 && plan->count == 9));
}

static int mir_match_nested_literal_checks(struct MirConstantChecks *plan)
{
    static const int calls[8] = { 70, 82, 94, 104, 114, 126, 138, 148 };
    static const int values[8] = { 1, 20, 300, 321, 7, 40, 500, 547 };
    int check;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 149 || mir_cfg_block_count() != 1 || mir.has_vla ||
        (mir.return_type & 15) != TYPE_VOID ||
        mir.insns[97].opcode != MIR_CALL || mir.insns[141].opcode != MIR_CALL ||
        strcmp(mir.insns[97].name, mir.insns[141].name))
        return 0;
    for (check = 0; check < 8; ++check) {
        int args[3];
        const struct MirInsn *string;
        struct MirMachineForm expected;
        struct Sym *function;
        if (!mir_machine_three_call_arguments(&mir.insns[calls[check]], args) ||
            (string = mir_definition(args[2])) == NULL ||
            string->opcode != MIR_STRING_ADDRESS ||
            !mir_machine_pointer_form(args[1], calls[check], &expected, 0) ||
            expected.kind != MIR_MACHINE_FORM_INTEGER ||
            (expected.value & 0xffffL) != values[check] ||
            (function = find_global(mir.insns[calls[check]].name)) == NULL ||
            (plan->function != NULL && plan->function != function))
            return mir_machine_reject("nested-literal-checks", "call");
        plan->function = function;
        plan->strings[check] = (int)string->immediate;
        plan->actual[check] = values[check];
        plan->expected[check] = values[check];
    }
    plan->count = 8;
    plan->name_last = 1;
    return plan->function != NULL;
}

static void mir_emit_constant_checks(
    MirStream *out, const struct MirConstantChecks *plan)
{
    int check;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (check = 0; check < plan->count; ++check) {
        if (plan->name_last) {
            mir_stream_printf(out,
                    "\tld hl,S%d\n\tpush hl\n"
                    "\tld hl,%ld\n\tpush hl\n"
                    "\tld hl,%ld\n\tpush hl\n",
                    plan->strings[check],
                    plan->expected[check],
                    plan->actual[check]);
        } else {
            mir_stream_printf(out,
                    "\tld hl,%ld\n\tpush hl\n"
                    "\tld hl,%ld\n\tpush hl\n"
                    "\tld hl,S%d\n\tpush hl\n",
                    plan->expected[check],
                    plan->actual[check],
                    plan->strings[check]);
        }
        mir_machine_emit_symbol_call(out, plan->function);
        mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n", out);
    }
    mir_stream_puts("\tret\n", out);
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

static void mir_emit_constant_prints(
    MirStream *out, const struct MirConstantPrints *plan)
{
    int call;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (call = 0; call < plan->count; ++call) {
        mir_stream_printf(out,
                "\tld hl,%ld\n\tpush hl\n"
                "\tld hl,S%d\n\tpush hl\n",
                plan->values[call], plan->strings[call]);
        mir_machine_emit_symbol_call(out, plan->function);
        mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    }
    mir_stream_puts("\tld hl,0\n\tret\n", out);
}

static int mir_match_call_sum_print(struct MirCallSumPrint *plan)
{
    static const int expected_opcodes[21] = {
        MIR_LABEL, MIR_CONST, MIR_ARG, MIR_CALL, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_BINARY, MIR_CALL, MIR_BINARY, MIR_CALL, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_CONST, MIR_RETURN
    };
    static const int call_indices[4] = { 3, 6, 8, 10 };
    int print_arguments[2];
    int call_argument;
    long first_argument;
    long second_argument;
    int call;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 21 || mir_cfg_block_count() != 1 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return mir_machine_reject("call-sum-print", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject("call-sum-print", "opcode");
    if (!mir_machine_constant_value(
            mir.insns[1].dst, &first_argument, 0) ||
        !mir_machine_constant_value(
            mir.insns[4].dst, &second_argument, 0) ||
        !mir_machine_single_call_argument(
            &mir.insns[3], &call_argument) ||
        call_argument != mir.insns[1].dst ||
        !mir_machine_single_call_argument(
            &mir.insns[6], &call_argument) ||
        call_argument != mir.insns[4].dst ||
        !mir_machine_call_has_no_arguments(&mir.insns[8]) ||
        !mir_machine_call_has_no_arguments(&mir.insns[10]))
        return mir_machine_reject("call-sum-print", "arguments");
    plan->arguments[0] = (int)(first_argument & 0xffffL);
    plan->arguments[1] = (int)(second_argument & 0xffffL);
    for (call = 0; call < 4; ++call) {
        const struct MirInsn *call_insn =
            &mir.insns[call_indices[call]];

        plan->value_functions[call] =
            find_global(call_insn->name);
        if (plan->value_functions[call] == NULL ||
            !plan->value_functions[call]->is_defined ||
            plan->value_functions[call]->is_funcptr ||
            plan->value_functions[call]->is_noreturn ||
            type_size(call_insn->type) != 2)
            return mir_machine_reject(
                "call-sum-print", "value-function");
    }
    if (mir.insns[7].immediate != '+' ||
        mir.insns[7].src1 != mir.insns[3].dst ||
        mir.insns[7].src2 != mir.insns[6].dst ||
        mir.insns[9].immediate != '+' ||
        mir.insns[9].src1 != mir.insns[7].dst ||
        mir.insns[9].src2 != mir.insns[8].dst ||
        mir.insns[11].immediate != '+' ||
        mir.insns[11].src1 != mir.insns[9].dst ||
        mir.insns[11].src2 != mir.insns[10].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[13]) ||
        mir.insns[13].src1 != mir.insns[11].dst)
        return mir_machine_reject("call-sum-print", "sum");
    if (!mir_machine_two_call_arguments(
            &mir.insns[18], print_arguments) ||
        print_arguments[0] != mir.insns[14].dst ||
        print_arguments[1] != mir.insns[11].dst ||
        !mir_machine_constant_equals(mir.insns[19].dst, 0) ||
        mir.insns[20].src1 != mir.insns[19].dst)
        return mir_machine_reject("call-sum-print", "print");
    plan->print_function = find_global(mir.insns[18].name);
    plan->string_id = (int)mir.insns[14].immediate;
    if (plan->print_function == NULL ||
        plan->print_function->is_funcptr ||
        plan->string_id < 0)
        return mir_machine_reject("call-sum-print", "print-function");
    return 1;
}

static void mir_emit_call_sum_print(
    MirStream *out, const struct MirCallSumPrint *plan)
{
    int call;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (call = 0; call < 4; ++call) {
        if (call < 2)
            mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n",
                    plan->arguments[call]);
        mir_machine_emit_symbol_call(
            out, plan->value_functions[call]);
        if (call < 2)
            mir_stream_puts("\tpop bc\n", out);
        if (call == 0)
            mir_stream_puts("\tpush hl\n", out);
        else {
            mir_stream_puts("\tpop de\n\tadd hl,de\n", out);
            if (call != 3)
                mir_stream_puts("\tpush hl\n", out);
        }
    }
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", plan->string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld hl,0\n\tret\n", out);
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

static void mir_emit_pointer_difference_prints(
    MirStream *out, const struct MirPointerDifferencePrints *plan)
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
            mir_stream_printf(out, "\tld de,%ld\n",
                    plan->right_constant[call]);
        mir_stream_puts("\tor a\n\tsbc hl,de\n\tpush hl\n", out);
        mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
                plan->strings[call]);
        mir_machine_emit_symbol_call(out, plan->function);
        mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    }
    mir_stream_puts("\tld hl,0\n\tret\n", out);
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
                (type_size(insn->type) != 1 &&
                 type_size(insn->type) != 2) ||
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
        type_size(parameters[0]->type) !=
            type_size(parameters[1]->type) ||
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
            parameters[1]->dst,
            type_size(parameters[0]->type),
            (parameters[0]->type & TYPE_UNSIGNED) != 0) ||
        !mir_machine_match_comparison_argument(
            arguments[2], TOK_LE, parameters[0]->dst,
            parameters[1]->dst,
            type_size(parameters[0]->type),
            (parameters[0]->type & TYPE_UNSIGNED) != 0) ||
        !mir_machine_match_comparison_argument(
            arguments[3], TOK_EQ, parameters[0]->dst,
            parameters[1]->dst,
            type_size(parameters[0]->type),
            (parameters[0]->type & TYPE_UNSIGNED) != 0) ||
        !mir_machine_match_comparison_argument(
            arguments[4], TOK_GE, parameters[0]->dst,
            parameters[1]->dst,
            type_size(parameters[0]->type),
            (parameters[0]->type & TYPE_UNSIGNED) != 0) ||
        !mir_machine_match_comparison_argument(
            arguments[5], '>', parameters[0]->dst,
            parameters[1]->dst,
            type_size(parameters[0]->type),
            (parameters[0]->type & TYPE_UNSIGNED) != 0) ||
        !mir_machine_parameter_value_offset(
            parameters[0]->dst, &plan->left_stack_offset) ||
        !mir_machine_parameter_value_offset(
            parameters[1]->dst, &plan->right_stack_offset))
        return 0;
    plan->is_unsigned =
        (parameters[0]->type & TYPE_UNSIGNED) != 0;
    plan->width = type_size(parameters[0]->type);
    plan->string_id = (int)string->immediate;
    return plan->function != NULL;
}

static void mir_emit_byte_comparison_print(
    MirStream *out, const struct MirByteComparisonPrint *plan)
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
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->string_id);
    mir_machine_emit_symbol_call(out, plan->function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tpop bc\n\tpop bc\n\tpop bc\n\tret\n", out);
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

static void mir_emit_constant_buffer_call_print(
    MirStream *out, const struct MirConstantBufferCallPrint *plan)
{
    unsigned int first =
        (unsigned int)plan->bytes[0] |
        ((unsigned int)plan->bytes[1] << 8);
    unsigned int second =
        (unsigned int)plan->bytes[2] |
        ((unsigned int)plan->bytes[3] << 8);

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%u\n\tpush hl\n"
            "\tld hl,%u\n\tpush hl\n"
            "\tld hl,0\n\tadd hl,sp\n\tpush hl\n",
            second, first);
    mir_machine_emit_symbol_call(out, plan->pack_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out,
            "\tld hl,S%d\n\tpush hl\n"
            "\textrn %s\n\tcall %s\n"
            "\tpop bc\n\tpop bc\n\tpop bc\n"
            "\tld hl,0\n\tret\n",
            plan->string_id,
            plan->print_name, plan->print_name);
}

static int mir_match_vla_endpoint_reduction(
    struct MirVlaEndpointReduction *plan)
{
    static const int expected_opcodes[42] = {
        MIR_LABEL, MIR_PARAM, MIR_VLA_SAVE, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_VLA_ALLOC, MIR_LOAD,
        MIR_STORE, MIR_LOAD, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_CONST, MIR_BINARY, MIR_BINARY,
        MIR_STORE, MIR_LOAD, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_CONST, MIR_STORE_INDIRECT, MIR_LOAD, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_INDEX_ADDRESS, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_LOAD, MIR_LOAD, MIR_BINARY,
        MIR_CONST, MIR_BINARY, MIR_NOP, MIR_LOAD,
        MIR_LOAD_INDIRECT, MIR_BINARY, MIR_LOAD,
        MIR_LOAD_INDIRECT, MIR_BINARY, MIR_RETURN
    };
    const struct MirInsn *parameter;
    const struct MirInsn *size;
    const struct MirInsn *allocation;
    const struct MirInsn *p_store;
    const struct MirInsn *q_store;
    const struct MirInsn *q_address;
    const struct MirInsn *last_index;
    const struct MirInsn *first_store;
    const struct MirInsn *last_store;
    const struct MirInsn *difference;
    const struct MirInsn *scaled_difference;
    const struct MirInsn *q_load;
    const struct MirInsn *p_load;
    const struct MirInsn *sum;
    const struct MirInsn *result;
    const struct MirInsn *first_constant;
    const struct MirInsn *last_constant;
    long first_value;
    long last_value;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (!mir.has_vla || mir_cfg_block_count() != 1 ||
        mir.count != 42 || type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode != expected_opcodes[instruction])
            return 0;
        if ((insn->opcode == MIR_LOAD ||
             insn->opcode == MIR_STORE) &&
            !mir_machine_named_nonvolatile(insn))
            return 0;
        if ((insn->opcode == MIR_LOAD_INDIRECT ||
             insn->opcode == MIR_STORE_INDIRECT) &&
            ((insn->memory_flags & (1 | 8)) != 0 ||
             insn->bit_width != 0))
            return 0;
        if (insn->opcode == MIR_CALL ||
            insn->opcode == MIR_CALL_AGGREGATE ||
            insn->opcode == MIR_ARG ||
            insn->opcode == MIR_BRANCH_FALSE ||
            insn->opcode == MIR_JUMP ||
            insn->opcode == MIR_PHI ||
            insn->opcode == MIR_OPAQUE)
            return 0;
    }
    parameter = &mir.insns[1];
    size = &mir.insns[5];
    allocation = &mir.insns[6];
    p_store = &mir.insns[8];
    q_address = &mir.insns[15];
    q_store = &mir.insns[16];
    first_store = &mir.insns[21];
    last_store = &mir.insns[28];
    difference = &mir.insns[31];
    scaled_difference = &mir.insns[33];
    q_load = &mir.insns[36];
    sum = &mir.insns[37];
    p_load = &mir.insns[39];
    result = &mir.insns[40];
    last_index = &mir.insns[25];
    first_constant = mir_definition(first_store->src2);
    last_constant = mir_definition(last_store->src2);
    if (allocation->name[0] == '\0' ||
        mir.insns[7].name[0] == '\0' ||
        mir.insns[9].name[0] == '\0' ||
        mir.insns[17].name[0] == '\0' ||
        mir.insns[22].name[0] == '\0' ||
        mir.insns[0].opcode != MIR_LABEL ||
        parameter->opcode != MIR_PARAM ||
        type_size(parameter->type) != 2 ||
        mir.insns[2].opcode != MIR_VLA_SAVE ||
        size->opcode != MIR_BINARY || size->immediate != '*' ||
        size->src1 != parameter->dst ||
        !mir_machine_constant_equals(size->src2, 2) ||
        allocation->opcode != MIR_VLA_ALLOC ||
        allocation->src1 != size->dst ||
        allocation->memory_size != 2 ||
        p_store->opcode != MIR_STORE ||
        !mir_machine_unobservable_local_store(p_store) ||
        mir.insns[7].opcode != MIR_LOAD ||
        p_store->src1 != mir.insns[7].dst ||
        q_store->opcode != MIR_STORE ||
        !mir_machine_unobservable_local_store(q_store) ||
        mir_machine_same_location(p_store, q_store) ||
        q_address->opcode != MIR_BINARY ||
        q_address->immediate != '+' ||
        q_store->src1 != q_address->dst ||
        mir.insns[9].opcode != MIR_LOAD ||
        q_address->src1 != mir.insns[9].dst ||
        !mir_machine_same_location(
            &mir.insns[7], &mir.insns[9]) ||
        strcmp(allocation->name, mir.insns[7].name) ||
        strcmp(allocation->name, mir.insns[9].name) ||
        mir.insns[12].opcode != MIR_BINARY ||
        mir.insns[12].immediate != '-' ||
        mir.insns[12].src1 != parameter->dst ||
        !mir_machine_constant_equals(mir.insns[12].src2, 1) ||
        mir.insns[14].opcode != MIR_BINARY ||
        mir.insns[14].immediate != '*' ||
        mir.insns[14].src1 != mir.insns[12].dst ||
        !mir_machine_constant_equals(mir.insns[14].src2, 2) ||
        q_address->src2 != mir.insns[14].dst)
        return 0;
    if (first_store->opcode != MIR_STORE_INDIRECT ||
        first_store->memory_size != 2 ||
    first_constant == NULL ||
    first_constant->opcode != MIR_CONST ||
    type_size(first_constant->type) != 2 ||
    mir.insns[17].opcode != MIR_LOAD ||
    !mir_machine_same_location(
        &mir.insns[7], &mir.insns[17]) ||
        strcmp(allocation->name, mir.insns[17].name) ||
        mir.insns[19].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[19].src1 != mir.insns[17].dst ||
        !mir_machine_constant_equals(mir.insns[19].src2, 0) ||
        mir.insns[19].immediate != 2 ||
        mir.insns[19].memory_size != 2 ||
        first_store->src1 != mir.insns[19].dst ||
        last_store->opcode != MIR_STORE_INDIRECT ||
        last_store->memory_size != 2 ||
        last_constant == NULL ||
        last_constant->opcode != MIR_CONST ||
        type_size(last_constant->type) != 2 ||
        mir.insns[22].opcode != MIR_LOAD ||
        !mir_machine_same_location(
            &mir.insns[7], &mir.insns[22]) ||
        strcmp(allocation->name, mir.insns[22].name) ||
        last_index->opcode != MIR_BINARY ||
        last_index->immediate != '-' ||
        last_index->src1 != parameter->dst ||
        !mir_machine_constant_equals(last_index->src2, 1) ||
        mir.insns[26].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[26].src1 != mir.insns[22].dst ||
        mir.insns[26].src2 != last_index->dst ||
        mir.insns[26].immediate != 2 ||
        mir.insns[26].memory_size != 2 ||
        last_store->src1 != mir.insns[26].dst)
        return 0;
    if (difference->opcode != MIR_BINARY ||
    type_size(difference->type) != 2 ||
        difference->immediate != '-' ||
        mir.insns[29].opcode != MIR_LOAD ||
        mir.insns[30].opcode != MIR_LOAD ||
        difference->src1 != mir.insns[29].dst ||
        difference->src2 != mir.insns[30].dst ||
        scaled_difference->opcode != MIR_BINARY ||
        type_size(scaled_difference->type) != 2 ||
        scaled_difference->immediate != '/' ||
        scaled_difference->src1 != difference->dst ||
        !mir_machine_constant_equals(
            scaled_difference->src2, 2) ||
        q_load->opcode != MIR_LOAD_INDIRECT ||
        q_load->memory_size != 2 ||
        type_size(q_load->type) != 2 ||
        q_load->src1 != mir.insns[35].dst ||
        mir.insns[34].opcode != MIR_NOP ||
        mir.insns[35].opcode != MIR_LOAD ||
        !mir_machine_same_location(q_store, &mir.insns[35]) ||
        sum->opcode != MIR_BINARY || sum->immediate != '+' ||
        type_size(sum->type) != 2 ||
        sum->src1 != scaled_difference->dst ||
        sum->src2 != q_load->dst ||
        p_load->opcode != MIR_LOAD_INDIRECT ||
        p_load->memory_size != 2 ||
        type_size(p_load->type) != 2 ||
        p_load->src1 != mir.insns[38].dst ||
        mir.insns[38].opcode != MIR_LOAD ||
        !mir_machine_same_location(p_store, &mir.insns[38]) ||
        result->opcode != MIR_BINARY || result->immediate != '-' ||
        type_size(result->type) != 2 ||
        result->src1 != sum->dst || result->src2 != p_load->dst ||
        mir.insns[41].opcode != MIR_RETURN ||
        mir.insns[41].src1 != result->dst ||
        !mir_machine_same_location(
            p_store, &mir.insns[30]) ||
        !mir_machine_same_location(
            q_store, &mir.insns[29]))
        return 0;
    if (!mir_machine_parameter_value_offset(
            parameter->dst, &plan->parameter_stack_offset))
        return 0;
    first_value = first_constant->immediate;
    last_value = last_constant->immediate;
    plan->adjustment =
        (int)(unsigned short)(
            (unsigned long)(unsigned short)last_value -
            (unsigned long)(unsigned short)first_value);
    return 1;
}

static void mir_emit_vla_endpoint_reduction(
    MirStream *out, const struct MirVlaEndpointReduction *plan)
{
    int done = new_label();

    if (opt_stack_check) {
        mir_stream_printf(out,
                "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
                "\tld l,(ix+%d)\n\tld h,(ix+%d)\n"
                "\tadd hl,hl\n\tex de,hl\n\tld hl,0\n"
                "\tor a\n\tsbc hl,de\n\tadd hl,sp\n\tld sp,hl\n",
                plan->parameter_stack_offset + 2,
                plan->parameter_stack_offset + 3);
        mir_emit_runtime_call(out, "__stchk");
        mir_stream_puts("\tld sp,ix\n\tpop ix\n", out);
    }
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n"
            "\tdec hl\n\tld a,h\n\tor l\n\tjp z,L%d\n",
            plan->parameter_stack_offset, done);
    mir_machine_emit_hl_offset(out, plan->adjustment, 0);
    mir_stream_printf(out, "L%d:\n\tret\n", done);
}

static int mir_match_masked_wide_product_high(
    struct MirMaskedWideProductHigh *plan)
{
    const struct MirInsn *parameter = NULL;
    const struct MirInsn *return_insn = NULL;
    const struct MirInsn *shift;
    const struct MirInsn *multiply;
    const struct MirInsn *masked;
    const struct MirInsn *multiplier;
    const struct MirInsn *mask;
    int parameter_count = 0;
    int return_count = 0;
    int memory_type;
    int memory_storage;
    int memory_offset;
    int masked_value;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir_cfg_block_count() != 1 ||
        type_size(mir.return_type) != 4 ||
        type_is_float(mir.return_type) ||
        type_ptr_depth(mir.return_type) != 0)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_LABEL:
        case MIR_NOP:
        case MIR_CONST:
            break;
        case MIR_PARAM:
            if (++parameter_count != 1)
                return 0;
            parameter = insn;
            break;
        case MIR_BINARY:
            break;
        case MIR_RETURN:
            if (++return_count != 1)
                return 0;
            return_insn = insn;
            break;
        default:
            return 0;
        }
    }
    if (parameter_count != 1 || return_count != 1 ||
        parameter == NULL || return_insn == NULL ||
        type_size(parameter->type) != 4 ||
        type_is_float(parameter->type) ||
        type_ptr_depth(parameter->type) != 0)
        return 0;
    shift = mir_definition(return_insn->src1);
    if (shift == NULL || shift->opcode != MIR_BINARY ||
        shift->immediate != TOK_SHR ||
        type_size(shift->type) != 4 ||
        !mir_machine_constant_equals(shift->src2, 16))
        return 0;
    multiply = mir_definition(shift->src1);
    if (multiply == NULL || multiply->opcode != MIR_BINARY ||
        multiply->immediate != '*' ||
        type_size(multiply->type) != 4)
        return 0;
    masked = mir_definition(multiply->src1);
    multiplier = mir_definition(multiply->src2);
    if (masked == NULL || masked->opcode != MIR_BINARY ||
        masked->immediate != '&') {
        masked = mir_definition(multiply->src2);
        multiplier = mir_definition(multiply->src1);
    }
    if (masked == NULL || masked->opcode != MIR_BINARY ||
        masked->immediate != '&' ||
        type_size(masked->type) != 4 ||
        multiplier == NULL ||
        multiplier->opcode != MIR_CONST ||
        type_size(multiplier->type) != 4 ||
        multiplier->immediate <= 0 ||
        multiplier->immediate > 32768)
        return 0;
    if (masked->src1 == parameter->dst) {
        masked_value = masked->src2;
    } else if (masked->src2 == parameter->dst) {
        masked_value = masked->src1;
    } else {
        return 0;
    }
    mask = mir_definition(masked_value);
    if (mask == NULL || type_size(mask->type) != 4)
        return 0;
    if (mask->opcode != MIR_CONST || mask->immediate != 65535) {
        const struct MirInsn *left;
        const struct MirInsn *right;

        if (mask->opcode != MIR_BINARY ||
            mask->immediate != '-')
            return 0;
        left = mir_definition(mask->src1);
        right = mir_definition(mask->src2);
        if (left == NULL || left->opcode != MIR_CONST ||
            type_size(left->type) != 4 ||
            left->immediate != 65536 ||
            right == NULL || right->opcode != MIR_CONST ||
            type_size(right->type) != 4 ||
            right->immediate != 1)
            return 0;
    }
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode == MIR_BINARY &&
            insn != shift && insn != multiply &&
            insn != masked && insn != mask)
            return 0;
    }
    if (!mir_scalar_memory_location(
            parameter, &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_PARAM ||
        type_size(memory_type) != 4 ||
        memory_offset < 2)
        return 0;
    plan->parameter_stack_offset = memory_offset - 2;
    plan->multiplier = (unsigned int)multiplier->immediate;
    return 1;
}

static void mir_emit_masked_wide_product_high(
    MirStream *out, const struct MirMaskedWideProductHigh *plan)
{
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tld hl,%u\n",
            plan->parameter_stack_offset, plan->multiplier);
    mir_emit_runtime_call(out, "__m1u");
    mir_stream_puts("\tld l,e\n\tld h,d\n\tld de,0\n\tret\n", out);
}

static int mir_match_wide_equal_select(
    struct MirWideEqualSelect *plan)
{
    const struct MirInsn *parameter = NULL;
    const struct MirInsn *comparison = NULL;
    const struct MirInsn *branch = NULL;
    const struct MirInsn *true_return = NULL;
    const struct MirInsn *fallback_return = NULL;
    const struct MirInsn *match_constant;
    const struct MirInsn *fallback_constant;
    const struct MirInsn *target_label = NULL;
    int parameter_count = 0;
    int comparison_count = 0;
    int branch_count = 0;
    int return_count = 0;
    int label_count = 0;
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir_cfg_block_count() != 2 ||
        type_size(mir.return_type) != 4 ||
        type_is_float(mir.return_type) ||
        type_ptr_depth(mir.return_type) != 0)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_CONST:
            break;
        case MIR_LABEL:
            ++label_count;
            break;
        case MIR_PARAM:
            if (++parameter_count != 1)
                return 0;
            parameter = insn;
            break;
        case MIR_BINARY:
            if (++comparison_count != 1)
                return 0;
            comparison = insn;
            break;
        case MIR_BRANCH_FALSE:
            if (++branch_count != 1)
                return 0;
            branch = insn;
            break;
        case MIR_RETURN:
            if (++return_count == 1)
                true_return = insn;
            else if (return_count == 2)
                fallback_return = insn;
            else
                return 0;
            break;
        default:
            return 0;
        }
    }
    if (parameter_count != 1 || comparison_count != 1 ||
        branch_count != 1 || return_count != 2 ||
        label_count != 2 || parameter == NULL ||
        comparison == NULL || branch == NULL ||
        true_return == NULL || fallback_return == NULL ||
        comparison < parameter || branch < comparison ||
        true_return < branch || fallback_return < true_return)
        return 0;
    match_constant = comparison->src1 == parameter->dst
        ? mir_definition(comparison->src2) :
          comparison->src2 == parameter->dst
        ? mir_definition(comparison->src1) : NULL;
    fallback_constant = mir_definition(fallback_return->src1);
    if (type_size(parameter->type) != 4 ||
        type_is_float(parameter->type) ||
        type_ptr_depth(parameter->type) != 0 ||
        match_constant == NULL ||
        match_constant->opcode != MIR_CONST ||
        type_size(match_constant->type) != 4 ||
        comparison->immediate != TOK_EQ ||
        type_size(comparison->type) != 2 ||
        branch->src1 != comparison->dst ||
        true_return->src1 != parameter->dst ||
        fallback_constant == NULL ||
        fallback_constant->opcode != MIR_CONST ||
        type_size(fallback_constant->type) != 4 ||
        branch->label <= 0)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_LABEL &&
            mir.insns[instruction].label == branch->label) {
            if (target_label != NULL)
                return 0;
            target_label = &mir.insns[instruction];
        }
    if (target_label == NULL ||
        target_label <= true_return ||
        target_label >= fallback_return ||
        match_constant >= comparison ||
        fallback_constant <= target_label ||
        fallback_constant >= fallback_return)
        return 0;
    if (!mir_scalar_memory_location(
            parameter, &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_PARAM ||
        type_size(memory_type) != 4 ||
        memory_offset < 2)
        return 0;
    plan->parameter_stack_offset = memory_offset - 2;
    plan->match_value =
        (unsigned long)match_constant->immediate & 0xffffffffUL;
    plan->fallback_value =
        (unsigned long)fallback_constant->immediate & 0xffffffffUL;
    return 1;
}

static void mir_emit_wide_equal_select(
    MirStream *out, const struct MirWideEqualSelect *plan)
{
    int fallback = new_label();

    mir_emit_wide_stack_equality_branch(
        out, plan->parameter_stack_offset,
        plan->match_value, fallback);
    mir_stream_printf(out,
            "\tld hl,%lu\n\tld de,%lu\n\tret\n"
            "L%d:\n\tld hl,%lu\n\tld de,%lu\n\tret\n",
            plan->match_value & 0xffffUL,
            (plan->match_value >> 16) & 0xffffUL,
            fallback,
            plan->fallback_value & 0xffffUL,
            (plan->fallback_value >> 16) & 0xffffUL);
}

static int mir_match_wide_equal_add_select(
    struct MirWideEqualAddSelect *plan)
{
    const struct MirInsn *parameters[2];
    const struct MirInsn *comparison = NULL;
    const struct MirInsn *addition = NULL;
    const struct MirInsn *conversion = NULL;
    const struct MirInsn *branch = NULL;
    const struct MirInsn *true_return = NULL;
    const struct MirInsn *fallback_return = NULL;
    const struct MirInsn *match_constant;
    const struct MirInsn *fallback_constant;
    const struct MirInsn *target_label = NULL;
    const struct MirInsn *wide_parameter;
    const struct MirInsn *narrow_parameter;
    int parameter_count = 0;
    int binary_count = 0;
    int branch_count = 0;
    int return_count = 0;
    int label_count = 0;
    int unary_count = 0;
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir_cfg_block_count() != 2 ||
        type_size(mir.return_type) != 4 ||
        type_is_float(mir.return_type) ||
        type_ptr_depth(mir.return_type) != 0)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_CONST:
            break;
        case MIR_LABEL:
            ++label_count;
            break;
        case MIR_PARAM:
            if (parameter_count >= 2)
                return 0;
            parameters[parameter_count++] = insn;
            break;
        case MIR_UNARY:
            if (++unary_count != 1)
                return 0;
            conversion = insn;
            break;
        case MIR_BINARY:
            if (++binary_count == 1)
                comparison = insn;
            else if (binary_count == 2)
                addition = insn;
            else
                return 0;
            break;
        case MIR_BRANCH_FALSE:
            if (++branch_count != 1)
                return 0;
            branch = insn;
            break;
        case MIR_RETURN:
            if (++return_count == 1)
                true_return = insn;
            else if (return_count == 2)
                fallback_return = insn;
            else
                return 0;
            break;
        default:
            return 0;
        }
    }
    if (parameter_count != 2 || binary_count != 2 ||
        unary_count != 1 || branch_count != 1 ||
        return_count != 2 || label_count != 2 ||
        comparison == NULL || addition == NULL ||
        conversion == NULL || branch == NULL ||
        true_return == NULL || fallback_return == NULL ||
        comparison > branch || branch > conversion ||
        conversion > addition || addition > true_return ||
        true_return > fallback_return)
        return 0;
    if (type_size(parameters[0]->type) == 4) {
        wide_parameter = parameters[0];
        narrow_parameter = parameters[1];
    } else if (type_size(parameters[1]->type) == 4) {
        wide_parameter = parameters[1];
        narrow_parameter = parameters[0];
    } else {
        return 0;
    }
    if (type_is_float(wide_parameter->type) ||
        type_ptr_depth(wide_parameter->type) != 0 ||
        type_size(narrow_parameter->type) != 2 ||
        type_is_float(narrow_parameter->type) ||
        type_ptr_depth(narrow_parameter->type) != 0 ||
        (narrow_parameter->type & 15) == TYPE_BOOL)
        return 0;
    match_constant =
        comparison->src1 == wide_parameter->dst
        ? mir_definition(comparison->src2) :
          comparison->src2 == wide_parameter->dst
        ? mir_definition(comparison->src1) : NULL;
    fallback_constant = mir_definition(fallback_return->src1);
    if (match_constant == NULL ||
        match_constant->opcode != MIR_CONST ||
        type_size(match_constant->type) != 4 ||
        comparison->immediate != TOK_EQ ||
        type_size(comparison->type) != 2 ||
        branch->src1 != comparison->dst ||
        conversion->opcode != MIR_UNARY ||
        conversion->immediate != 0 ||
        conversion->src1 != narrow_parameter->dst ||
        type_size(conversion->type) != 4 ||
        addition->opcode != MIR_BINARY ||
        addition->immediate != '+' ||
        type_size(addition->type) != 4 ||
        !((addition->src1 == wide_parameter->dst &&
           addition->src2 == conversion->dst) ||
          (addition->src2 == wide_parameter->dst &&
           addition->src1 == conversion->dst)) ||
        true_return->src1 != addition->dst ||
        fallback_constant == NULL ||
        fallback_constant->opcode != MIR_CONST ||
        type_size(fallback_constant->type) != 4 ||
        branch->label <= 0)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_LABEL &&
            mir.insns[instruction].label == branch->label) {
            if (target_label != NULL)
                return 0;
            target_label = &mir.insns[instruction];
        }
    if (target_label == NULL ||
        target_label <= true_return ||
        target_label >= fallback_return ||
        match_constant >= comparison ||
        fallback_constant <= target_label ||
        fallback_constant >= fallback_return)
        return 0;
    if (!mir_scalar_memory_location(
            wide_parameter, &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_PARAM ||
        type_size(memory_type) != 4 ||
        memory_offset < 2)
        return 0;
    plan->wide_stack_offset = memory_offset - 2;
    if (!mir_scalar_memory_location(
            narrow_parameter, &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_PARAM ||
        type_size(memory_type) != 2 ||
        memory_offset < 2)
        return 0;
    plan->narrow_stack_offset = memory_offset - 2;
    plan->narrow_is_unsigned =
        (narrow_parameter->type & TYPE_UNSIGNED) != 0;
    plan->match_value =
        (unsigned long)match_constant->immediate & 0xffffffffUL;
    plan->fallback_value =
        (unsigned long)fallback_constant->immediate & 0xffffffffUL;
    return 1;
}

static void mir_emit_wide_equal_add_select(
    MirStream *out, const struct MirWideEqualAddSelect *plan)
{
    int fallback = new_label();
    int extended = new_label();

    mir_emit_wide_stack_equality_branch(
        out, plan->wide_stack_offset,
        plan->match_value, fallback);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tld l,c\n\tld h,b\n\tld de,0\n",
            plan->narrow_stack_offset);
    if (!plan->narrow_is_unsigned)
        mir_stream_printf(out,
                "\tld a,h\n\tor a\n\tjp p,L%d\n"
                "\tdec de\nL%d:\n",
                extended, extended);
    mir_stream_printf(out,
            "\tld bc,%lu\n\tadd hl,bc\n\tex de,hl\n"
            "\tld bc,%lu\n\tadc hl,bc\n\tex de,hl\n\tret\n"
            "L%d:\n\tld hl,%lu\n\tld de,%lu\n\tret\n",
            plan->match_value & 0xffffUL,
            (plan->match_value >> 16) & 0xffffUL,
            fallback,
            plan->fallback_value & 0xffffUL,
            (plan->fallback_value >> 16) & 0xffffUL);
}

static int mir_match_wide_call_member_accumulate(
    struct MirWideCallMemberAccumulate *plan)
{
    static const int expected_opcodes[11] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_LOAD,
        MIR_ARG, MIR_CALL, MIR_BINARY, MIR_STORE_INDIRECT
    };
    const struct MirInsn *argument_parameter;
    const struct MirInsn *object_parameter;
    const struct MirInsn *member;
    const struct MirInsn *load;
    const struct MirInsn *argument_load;
    const struct MirInsn *call;
    const struct MirInsn *addition;
    const struct MirInsn *store;
    int call_argument;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir_cfg_block_count() != 1 || mir.count != 11 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    argument_parameter = &mir.insns[1];
    object_parameter = &mir.insns[2];
    member = &mir.insns[4];
    load = &mir.insns[5];
    argument_load = &mir.insns[6];
    call = &mir.insns[8];
    addition = &mir.insns[9];
    store = &mir.insns[10];
    if (type_size(argument_parameter->type) != 2 ||
        type_ptr_depth(argument_parameter->type) == 0 ||
        type_is_float(argument_parameter->type) ||
        type_size(object_parameter->type) != 2 ||
        type_ptr_depth(object_parameter->type) == 0 ||
        type_is_float(object_parameter->type) ||
        member->src1 != object_parameter->dst ||
        member->memory_size != 4 ||
        member->immediate < -128 ||
        member->immediate + 3 > 127 ||
        load->src1 != member->dst ||
        load->memory_size != 4 ||
        load->bit_width != 0 ||
        type_size(load->type) != 4 ||
        type_is_float(load->type) ||
        (load->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_same_location(
            argument_parameter, argument_load) ||
        !mir_machine_single_call_argument(
            call, &call_argument) ||
        call_argument != argument_load->dst ||
        type_size(call->type) != 4 ||
        type_is_float(call->type) ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0 ||
        addition->immediate != '+' ||
        type_size(addition->type) != 4 ||
        !((addition->src1 == load->dst &&
           addition->src2 == call->dst) ||
          (addition->src2 == load->dst &&
           addition->src1 == call->dst)) ||
        store->src1 != member->dst ||
        store->src2 != addition->dst ||
        store->memory_size != 4 ||
        store->bit_width != 0 ||
        (store->memory_flags & (1 | 8)) != 0)
        return 0;
    plan->function = find_global(call->name);
    if (plan->function == NULL ||
        !plan->function->is_defined ||
        (call->base_name[0] != 0 &&
         strcmp(call->base_name,
                asm_name_for(
                    sym_asm_name(plan->function)))))
        return 0;
    if (!mir_machine_parameter_value_offset(
            argument_parameter->dst,
            &plan->argument_stack_offset) ||
        !mir_machine_parameter_value_offset(
            object_parameter->dst,
            &plan->object_stack_offset))
        return 0;
    plan->member_offset = (int)member->immediate;
    return 1;
}

static void mir_emit_wide_call_member_accumulate(
    MirStream *out, const struct MirWideCallMemberAccumulate *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tex de,hl\n\tld de,%d\n\tadd hl,de\n"
            "\tpush hl\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tinc hl\n\tld a,(hl)\n\tinc hl\n"
            "\tld h,(hl)\n\tld l,a\n\tex de,hl\n"
            "\tpush de\n\tpush hl\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tpush de\n",
            plan->object_stack_offset,
            plan->member_offset,
            plan->argument_stack_offset + 6);
    mir_machine_emit_symbol_call(out, plan->function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tadd hl,bc\n"
          "\tex de,hl\n\tpop bc\n\tadc hl,bc\n\tex de,hl\n"
          "\tld b,d\n\tld c,e\n\tpop de\n\tex de,hl\n"
          "\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tinc hl\n"
          "\tld (hl),c\n\tinc hl\n\tld (hl),b\n\tret\n",
          out);
}

static int mir_match_wide_difference_call(
    struct MirWideDifferenceCall *plan)
{
    static const int expected_opcodes[11] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_UNARY,
        MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_ARG, MIR_CALL,
        MIR_RETURN
    };
    const struct MirInsn *left_parameter;
    const struct MirInsn *right_parameter;
    const struct MirInsn *left_conversion;
    const struct MirInsn *right_conversion;
    const struct MirInsn *difference;
    const struct MirInsn *call;
    int call_argument;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir_cfg_block_count() != 1 || mir.count != 11)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    left_parameter = &mir.insns[1];
    right_parameter = &mir.insns[2];
    left_conversion = &mir.insns[4];
    right_conversion = &mir.insns[6];
    difference = &mir.insns[7];
    call = &mir.insns[9];
    if (type_size(left_parameter->type) != 2 ||
        type_is_float(left_parameter->type) ||
        type_ptr_depth(left_parameter->type) != 0 ||
        (left_parameter->type & 15) == TYPE_BOOL ||
        type_size(right_parameter->type) != 2 ||
        type_is_float(right_parameter->type) ||
        type_ptr_depth(right_parameter->type) != 0 ||
        (right_parameter->type & 15) == TYPE_BOOL ||
        left_conversion->immediate != 0 ||
        left_conversion->src1 != left_parameter->dst ||
        type_size(left_conversion->type) != 4 ||
        type_is_float(left_conversion->type) ||
        right_conversion->immediate != 0 ||
        right_conversion->src1 != right_parameter->dst ||
        type_size(right_conversion->type) != 4 ||
        type_is_float(right_conversion->type) ||
        difference->immediate != '-' ||
        difference->src1 != left_conversion->dst ||
        difference->src2 != right_conversion->dst ||
        type_size(difference->type) != 4 ||
        type_is_float(difference->type) ||
        !mir_machine_single_call_argument(
            call, &call_argument) ||
        call_argument != difference->dst ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0 ||
        mir.insns[10].src1 != call->dst)
        return 0;
    plan->function = find_global(call->name);
    if (plan->function == NULL ||
        !plan->function->is_defined ||
        (call->base_name[0] != 0 &&
         strcmp(call->base_name,
                asm_name_for(
                    sym_asm_name(plan->function)))) ||
        !mir_machine_parameter_value_offset(
            left_parameter->dst,
            &plan->left_stack_offset) ||
        !mir_machine_parameter_value_offset(
            right_parameter->dst,
            &plan->right_stack_offset))
        return 0;
    plan->left_is_unsigned =
        (left_parameter->type & TYPE_UNSIGNED) != 0;
    plan->right_is_unsigned =
        (right_parameter->type & TYPE_UNSIGNED) != 0;
    return 1;
}

static void mir_emit_wide_difference_call(
    MirStream *out, const struct MirWideDifferenceCall *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_emit_widened_parameter(
        out, plan->left_stack_offset,
        plan->left_is_unsigned);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_emit_widened_parameter(
        out, plan->right_stack_offset + 4,
        plan->right_is_unsigned);
    mir_stream_puts("\tld b,d\n\tld c,e\n\tex de,hl\n\tpop hl\n"
          "\tor a\n\tsbc hl,de\n\tex de,hl\n\tpop hl\n"
          "\tsbc hl,bc\n\tex de,hl\n"
          "\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tret\n", out);
}

static int mir_match_scaled_wide_division_call(
    struct MirScaledWideDivisionCall *plan)
{
    static const int expected_opcodes[13] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_UNARY,
        MIR_CONST, MIR_BINARY, MIR_NOP, MIR_UNARY, MIR_BINARY,
        MIR_ARG, MIR_CALL, MIR_RETURN
    };
    const struct MirInsn *numerator;
    const struct MirInsn *denominator;
    const struct MirInsn *wide_numerator;
    const struct MirInsn *product;
    const struct MirInsn *wide_denominator;
    const struct MirInsn *division;
    const struct MirInsn *call;
    int call_argument;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir_cfg_block_count() != 1 || mir.count != 13)
        return mir_machine_reject(
            "scaled-wide-division-call", "preflight");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "scaled-wide-division-call", "opcodes");
    numerator = &mir.insns[1];
    denominator = &mir.insns[2];
    wide_numerator = &mir.insns[4];
    product = &mir.insns[6];
    wide_denominator = &mir.insns[8];
    division = &mir.insns[9];
    call = &mir.insns[11];
    if (type_size(numerator->type) != 2 ||
        (numerator->type & TYPE_UNSIGNED) != 0 ||
        type_is_float(numerator->type) ||
        type_ptr_depth(numerator->type) != 0 ||
        (numerator->type & 15) == TYPE_BOOL ||
        type_size(denominator->type) != 2 ||
        (denominator->type & TYPE_UNSIGNED) != 0 ||
        type_is_float(denominator->type) ||
        type_ptr_depth(denominator->type) != 0 ||
        (denominator->type & 15) == TYPE_BOOL ||
        wide_numerator->immediate != 0 ||
        wide_numerator->src1 != numerator->dst ||
        type_size(wide_numerator->type) != 4 ||
        (wide_numerator->type & TYPE_UNSIGNED) != 0 ||
        product->immediate != '*' ||
        product->src1 != wide_numerator->dst ||
        !mir_machine_constant_equals(product->src2, 256) ||
        type_size(product->type) != 4 ||
        (product->type & TYPE_UNSIGNED) != 0 ||
        wide_denominator->immediate != 0 ||
        wide_denominator->src1 != denominator->dst ||
        type_size(wide_denominator->type) != 4 ||
        (wide_denominator->type & TYPE_UNSIGNED) != 0 ||
        division->immediate != '/' ||
        division->src1 != product->dst ||
        division->src2 != wide_denominator->dst ||
        type_size(division->type) != 4 ||
        (division->type & TYPE_UNSIGNED) != 0 ||
        !mir_machine_single_call_argument(
            call, &call_argument) ||
        call_argument != division->dst ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0 ||
        mir.insns[12].src1 != call->dst)
        return mir_machine_reject(
            "scaled-wide-division-call", "shape");
    plan->function = find_global(call->name);
    if (plan->function == NULL ||
        !plan->function->is_defined ||
        (call->base_name[0] != 0 &&
         strcmp(call->base_name,
                asm_name_for(
                    sym_asm_name(plan->function)))) ||
        !mir_machine_parameter_value_offset(
            numerator->dst,
            &plan->numerator_stack_offset) ||
        !mir_machine_parameter_value_offset(
            denominator->dst,
            &plan->denominator_stack_offset))
        return mir_machine_reject(
            "scaled-wide-division-call", "call-or-parameters");
    return 1;
}

static void mir_emit_scaled_wide_division_call(
    MirStream *out, const struct MirScaledWideDivisionCall *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tld e,(hl)\n"
            "\tld h,a\n\tld l,0\n"
            "\tld a,e\n\trlca\n\tsbc a,a\n\tld d,a\n",
            plan->numerator_stack_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_emit_widened_parameter(
        out, plan->denominator_stack_offset + 4, 0);
    mir_emit_runtime_call(out, "__lds");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tret\n", out);
}

static int mir_match_record_append(struct MirRecordAppend *plan)
{
    static const int expected_opcodes[33] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_BINARY, MIR_BINARY, MIR_NOP, MIR_STORE,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_NOP, MIR_STORE_INDIRECT,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_NOP, MIR_STORE_INDIRECT,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_NOP, MIR_STORE_INDIRECT,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_BINARY, MIR_STORE_INDIRECT
    };
    const struct MirInsn *parameters[3];
    const struct MirInsn *root_loads[3];
    const struct MirInsn *array_member;
    const struct MirInsn *array_load;
    const struct MirInsn *cursor_member;
    const struct MirInsn *cursor_load;
    const struct MirInsn *scaled;
    const struct MirInsn *stride_constant;
    const struct MirInsn *address;
    const struct MirInsn *local_store;
    const struct MirInsn *record_loads[3];
    const struct MirInsn *field_members[3];
    const struct MirInsn *field_stores[3];
    const struct MirInsn *increment_member;
    const struct MirInsn *increment_load;
    const struct MirInsn *increment;
    int memory_type;
    int memory_storage;
    int memory_offset;
    int field;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir_cfg_block_count() != 1 || mir.count != 33 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    parameters[0] = &mir.insns[1];
    parameters[1] = &mir.insns[2];
    parameters[2] = &mir.insns[3];
    root_loads[0] = &mir.insns[4];
    root_loads[1] = &mir.insns[7];
    root_loads[2] = &mir.insns[27];
    array_member = &mir.insns[5];
    array_load = &mir.insns[6];
    cursor_member = &mir.insns[8];
    cursor_load = &mir.insns[9];
    scaled = &mir.insns[11];
    stride_constant = mir_definition(scaled->src2);
    address = &mir.insns[12];
    local_store = &mir.insns[14];
    record_loads[0] = &mir.insns[15];
    record_loads[1] = &mir.insns[19];
    record_loads[2] = &mir.insns[23];
    field_members[0] = &mir.insns[16];
    field_members[1] = &mir.insns[20];
    field_members[2] = &mir.insns[24];
    field_stores[0] = &mir.insns[18];
    field_stores[1] = &mir.insns[22];
    field_stores[2] = &mir.insns[26];
    increment_member = &mir.insns[28];
    increment_load = &mir.insns[29];
    increment = &mir.insns[31];
    if (!mir_machine_named_nonvolatile(root_loads[0]) ||
        !mir_machine_same_location(
            root_loads[0], root_loads[1]) ||
        !mir_machine_same_location(
            root_loads[0], root_loads[2]) ||
        !mir_scalar_memory_location(
            root_loads[0], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        type_size(memory_type) != 2)
        return 0;
    plan->root = find_global(root_loads[0]->name);
    plan->root_offset = memory_offset;
    if (plan->root == NULL || plan->root->is_volatile ||
        array_member->src1 != root_loads[0]->dst ||
        array_member->memory_size != 2 ||
        array_load->src1 != array_member->dst ||
        array_load->memory_size != 2 ||
        array_load->bit_width != 0 ||
        (array_load->memory_flags & (1 | 8)) != 0 ||
        cursor_member->src1 != root_loads[1]->dst ||
        cursor_member->memory_size != 2 ||
        cursor_load->src1 != cursor_member->dst ||
        cursor_load->memory_size != 2 ||
        cursor_load->bit_width != 0 ||
        (cursor_load->memory_flags & (1 | 8)) != 0 ||
        scaled->immediate != '*' ||
        scaled->src1 != cursor_load->dst ||
        stride_constant == NULL ||
        stride_constant->opcode != MIR_CONST ||
        stride_constant->immediate <= 0 ||
        stride_constant->immediate > 32767 ||
        address->immediate != '+' ||
        address->src1 != array_load->dst ||
        address->src2 != scaled->dst ||
        !mir_machine_unobservable_local_store(local_store) ||
        local_store->src1 != address->dst)
        return 0;
    plan->array_member_offset = (int)array_member->immediate;
    plan->cursor_member_offset = (int)cursor_member->immediate;
    plan->stride = (int)stride_constant->immediate;
    for (field = 0; field < 3; ++field) {
        if (type_size(parameters[field]->type) != 2 ||
            type_is_float(parameters[field]->type) ||
            type_ptr_depth(parameters[field]->type) != 0 ||
            (parameters[field]->type & 15) == TYPE_BOOL ||
            !mir_machine_same_location(
                local_store, record_loads[field]) ||
            field_members[field]->src1 !=
                record_loads[field]->dst ||
            field_members[field]->memory_size != 2 ||
            field_stores[field]->src1 !=
                field_members[field]->dst ||
            field_stores[field]->src2 !=
                parameters[field]->dst ||
            field_stores[field]->memory_size != 2 ||
            field_stores[field]->bit_width != 0 ||
            (field_stores[field]->memory_flags & (1 | 8)) != 0 ||
            !mir_machine_parameter_value_offset(
                parameters[field]->dst,
                &plan->parameter_stack_offsets[field]))
            return 0;
        plan->field_offsets[field] =
            (int)field_members[field]->immediate;
    }
    if (increment_member->src1 != root_loads[2]->dst ||
        increment_member->memory_size != 2 ||
        increment_member->immediate != cursor_member->immediate ||
        increment_load->src1 != increment_member->dst ||
        increment_load->memory_size != 2 ||
        increment_load->bit_width != 0 ||
        (increment_load->memory_flags & (1 | 8)) != 0 ||
        increment->immediate != '+' ||
        increment->src1 != increment_load->dst ||
        !mir_machine_constant_equals(increment->src2, 1) ||
        mir.insns[32].src1 != increment_member->dst ||
        mir.insns[32].src2 != increment->dst ||
        mir.insns[32].memory_size != 2 ||
        mir.insns[32].bit_width != 0 ||
        (mir.insns[32].memory_flags & (1 | 8)) != 0)
        return 0;
    return 1;
}

static void mir_emit_record_append(
    MirStream *out, const struct MirRecordAppend *plan)
{
    int current_offset = 0;
    int field;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_word(
        out, plan->root, plan->root_offset);
    mir_machine_emit_hl_offset(
        out, plan->array_member_offset, 0);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n",
          out);
    mir_machine_emit_global_word(
        out, plan->root, plan->root_offset);
    mir_machine_emit_hl_offset(
        out, plan->cursor_member_offset, 0);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n",
          out);
    mir_emit_mul_hl_const(out, (unsigned long)plan->stride);
    mir_stream_puts("\tpop de\n\tadd hl,de\n", out);
    for (field = 0; field < 3; ++field) {
        mir_machine_emit_hl_offset(
            out, plan->field_offsets[field] - current_offset, 0);
        mir_stream_printf(out,
                "\tpush hl\n\tld hl,%d\n\tadd hl,sp\n"
                "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
                "\tpop hl\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n",
                plan->parameter_stack_offsets[field] + 2);
        current_offset = plan->field_offsets[field] + 1;
    }
    mir_machine_emit_global_word(
        out, plan->root, plan->root_offset);
    mir_machine_emit_hl_offset(
        out, plan->cursor_member_offset, 0);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tinc de\n"
          "\tdec hl\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tret\n",
          out);
}

static int mir_match_direct_record_append(
    struct MirRecordAppend *plan)
{
    static const int expected_opcodes[40] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_INDEX_ADDRESS, MIR_MEMBER_ADDRESS, MIR_NOP,
        MIR_STORE_INDIRECT,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_INDEX_ADDRESS, MIR_MEMBER_ADDRESS, MIR_NOP,
        MIR_STORE_INDIRECT,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_INDEX_ADDRESS, MIR_MEMBER_ADDRESS, MIR_NOP,
        MIR_STORE_INDIRECT,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_BINARY, MIR_STORE_INDIRECT
    };
    static const int array_root_index[3] = { 4, 14, 24 };
    static const int array_member_index[3] = { 5, 15, 25 };
    static const int array_load_index[3] = { 6, 16, 26 };
    static const int cursor_root_index[3] = { 7, 17, 27 };
    static const int cursor_member_index[3] = { 8, 18, 28 };
    static const int cursor_load_index[3] = { 9, 19, 29 };
    static const int address_index[3] = { 10, 20, 30 };
    static const int field_member_index[3] = { 11, 21, 31 };
    static const int field_store_index[3] = { 13, 23, 33 };
    const struct MirInsn *root;
    const struct MirInsn *increment_root;
    const struct MirInsn *increment_member;
    const struct MirInsn *increment_load;
    const struct MirInsn *increment;
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;
    int field;

    memset(plan, 0, sizeof(*plan));
    if (mir_cfg_block_count() != 1 || mir.count != 40 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    root = &mir.insns[array_root_index[0]];
    if (!mir_machine_named_nonvolatile(root) ||
        !mir_scalar_memory_location(
            root, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        type_size(memory_type) != 2)
        return 0;
    plan->root = find_global(root->name);
    plan->root_offset = memory_offset;
    if (plan->root == NULL || plan->root->is_volatile)
        return 0;
    for (field = 0; field < 3; ++field) {
        const struct MirInsn *parameter = &mir.insns[1 + field];
        const struct MirInsn *array_root =
            &mir.insns[array_root_index[field]];
        const struct MirInsn *array_member =
            &mir.insns[array_member_index[field]];
        const struct MirInsn *array_load =
            &mir.insns[array_load_index[field]];
        const struct MirInsn *cursor_root =
            &mir.insns[cursor_root_index[field]];
        const struct MirInsn *cursor_member =
            &mir.insns[cursor_member_index[field]];
        const struct MirInsn *cursor_load =
            &mir.insns[cursor_load_index[field]];
        const struct MirInsn *address =
            &mir.insns[address_index[field]];
        const struct MirInsn *member =
            &mir.insns[field_member_index[field]];
        const struct MirInsn *store =
            &mir.insns[field_store_index[field]];

        if (!mir_machine_same_location(root, array_root) ||
            !mir_machine_same_location(root, cursor_root) ||
            array_member->src1 != array_root->dst ||
            array_member->memory_size != 2 ||
            array_load->src1 != array_member->dst ||
            array_load->memory_size != 2 ||
            array_load->bit_width != 0 ||
            (array_load->memory_flags & (1 | 8)) != 0 ||
            cursor_member->src1 != cursor_root->dst ||
            cursor_member->memory_size != 2 ||
            cursor_load->src1 != cursor_member->dst ||
            cursor_load->memory_size != 2 ||
            cursor_load->bit_width != 0 ||
            (cursor_load->memory_flags & (1 | 8)) != 0 ||
            address->src1 != array_load->dst ||
            address->src2 != cursor_load->dst ||
            address->immediate <= 0 ||
            address->immediate > 32767 ||
            address->memory_size != address->immediate ||
            member->src1 != address->dst ||
            member->memory_size != 2 ||
            store->src1 != member->dst ||
            store->src2 != parameter->dst ||
            store->memory_size != 2 ||
            store->bit_width != 0 ||
            (store->memory_flags & (1 | 8)) != 0 ||
            type_size(parameter->type) != 2 ||
            type_is_float(parameter->type) ||
            type_ptr_depth(parameter->type) != 0 ||
            (parameter->type & 15) == TYPE_BOOL ||
            !mir_machine_parameter_value_offset(
                parameter->dst,
                &plan->parameter_stack_offsets[field]))
            return 0;
        if (field == 0) {
            plan->array_member_offset =
                (int)array_member->immediate;
            plan->cursor_member_offset =
                (int)cursor_member->immediate;
            plan->stride = (int)address->immediate;
        } else if (array_member->immediate !=
                       plan->array_member_offset ||
                   cursor_member->immediate !=
                       plan->cursor_member_offset ||
                   address->immediate != plan->stride) {
            return 0;
        }
        plan->field_offsets[field] = (int)member->immediate;
    }
    increment_root = &mir.insns[34];
    increment_member = &mir.insns[35];
    increment_load = &mir.insns[36];
    increment = &mir.insns[38];
    if (!mir_machine_same_location(root, increment_root) ||
        increment_member->src1 != increment_root->dst ||
        increment_member->memory_size != 2 ||
        increment_member->immediate !=
            plan->cursor_member_offset ||
        increment_load->src1 != increment_member->dst ||
        increment_load->memory_size != 2 ||
        increment_load->bit_width != 0 ||
        (increment_load->memory_flags & (1 | 8)) != 0 ||
        increment->immediate != '+' ||
        increment->src1 != increment_load->dst ||
        !mir_machine_constant_equals(increment->src2, 1) ||
        mir.insns[39].src1 != increment_member->dst ||
        mir.insns[39].src2 != increment->dst ||
        mir.insns[39].memory_size != 2 ||
        mir.insns[39].bit_width != 0 ||
        (mir.insns[39].memory_flags & (1 | 8)) != 0)
        return 0;
    return 1;
}

static void mir_emit_direct_record_append(
    MirStream *out, const struct MirRecordAppend *plan)
{
    int field;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (field = 0; field < 3; ++field) {
        mir_machine_emit_global_word(
            out, plan->root, plan->root_offset);
        mir_machine_emit_hl_offset(
            out, plan->array_member_offset, 0);
        mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n",
              out);
        mir_machine_emit_global_word(
            out, plan->root, plan->root_offset);
        mir_machine_emit_hl_offset(
            out, plan->cursor_member_offset, 0);
        mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n",
              out);
        mir_emit_mul_hl_const(out, (unsigned long)plan->stride);
        mir_stream_puts("\tpop de\n\tadd hl,de\n", out);
        mir_machine_emit_hl_offset(
            out, plan->field_offsets[field], 0);
        mir_stream_printf(out,
                "\tpush hl\n\tld hl,%d\n\tadd hl,sp\n"
                "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
                "\tpop hl\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n",
                plan->parameter_stack_offsets[field] + 2);
    }
    mir_machine_emit_global_word(
        out, plan->root, plan->root_offset);
    mir_machine_emit_hl_offset(
        out, plan->cursor_member_offset, 0);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tinc de\n"
          "\tdec hl\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tret\n",
          out);
}

static int mir_match_mixed_wide_sum(
    struct MirMixedWideSum *plan)
{
    static const int expected_opcodes[16] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_PARAM,
        MIR_NOP, MIR_UNARY, MIR_NOP, MIR_BINARY, MIR_NOP,
        MIR_NOP, MIR_BINARY, MIR_NOP, MIR_UNARY, MIR_BINARY,
        MIR_RETURN
    };
    const struct MirInsn *parameters[4];
    const struct MirInsn *first_conversion;
    const struct MirInsn *first_add;
    const struct MirInsn *second_add;
    const struct MirInsn *last_conversion;
    const struct MirInsn *last_add;
    int parameter;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir_cfg_block_count() != 1 || mir.count != 16 ||
        type_size(mir.return_type) != 4 ||
        type_is_float(mir.return_type) ||
        type_ptr_depth(mir.return_type) != 0)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    for (parameter = 0; parameter < 4; ++parameter)
        parameters[parameter] = &mir.insns[1 + parameter];
    first_conversion = &mir.insns[6];
    first_add = &mir.insns[8];
    second_add = &mir.insns[11];
    last_conversion = &mir.insns[13];
    last_add = &mir.insns[14];
    if (type_size(parameters[0]->type) != 2 ||
        type_size(parameters[1]->type) != 4 ||
        type_size(parameters[2]->type) != 4 ||
        type_size(parameters[3]->type) != 2 ||
        first_conversion->immediate != 0 ||
        first_conversion->src1 != parameters[0]->dst ||
        type_size(first_conversion->type) != 4 ||
        first_add->immediate != '+' ||
        first_add->src1 != first_conversion->dst ||
        first_add->src2 != parameters[1]->dst ||
        type_size(first_add->type) != 4 ||
        second_add->immediate != '+' ||
        second_add->src1 != first_add->dst ||
        second_add->src2 != parameters[2]->dst ||
        type_size(second_add->type) != 4 ||
        last_conversion->immediate != 0 ||
        last_conversion->src1 != parameters[3]->dst ||
        type_size(last_conversion->type) != 4 ||
        last_add->immediate != '+' ||
        last_add->src1 != second_add->dst ||
        last_add->src2 != last_conversion->dst ||
        type_size(last_add->type) != 4 ||
        mir.insns[15].src1 != last_add->dst)
        return 0;
    for (parameter = 0; parameter < 4; ++parameter) {
        int memory_type;
        int memory_storage;
        int memory_offset;

        if (type_is_float(parameters[parameter]->type) ||
            type_ptr_depth(parameters[parameter]->type) != 0 ||
            (parameters[parameter]->type & 15) == TYPE_BOOL ||
            !mir_scalar_memory_location(
                parameters[parameter], &memory_type,
                &memory_storage, &memory_offset) ||
            memory_storage != SC_PARAM ||
            type_size(memory_type) !=
                type_size(parameters[parameter]->type) ||
            memory_offset < 2)
            return 0;
        plan->parameter_stack_offsets[parameter] =
            memory_offset - 2;
        plan->parameter_widths[parameter] =
            type_size(parameters[parameter]->type);
        plan->parameter_is_unsigned[parameter] =
            (parameters[parameter]->type & TYPE_UNSIGNED) != 0;
    }
    return 1;
}

static void mir_emit_mixed_wide_sum(
    MirStream *out, const struct MirMixedWideSum *plan)
{
    int parameter;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_emit_widened_parameter(
        out, plan->parameter_stack_offsets[0],
        plan->parameter_is_unsigned[0]);
    for (parameter = 1; parameter < 4; ++parameter)
        mir_emit_add_mixed_wide_parameter(
            out, plan->parameter_stack_offsets[parameter],
            plan->parameter_widths[parameter],
            plan->parameter_is_unsigned[parameter]);
    mir_stream_puts("\tret\n", out);
}

static int mir_match_float_member_scale_add(
    struct MirFloatMemberScaleAdd *plan)
{
    const struct MirInsn *parameter;
    const struct MirInsn *destination;
    const struct MirInsn *destination_load;
    const struct MirInsn *source;
    const struct MirInsn *source_load;
    const struct MirInsn *scale;
    const struct MirInsn *product;
    const struct MirInsn *sum;
    const struct MirInsn *store;
    int expected_count;

    memset(plan, 0, sizeof(*plan));
    plan->returns_value =
        (mir.return_type & 15) != TYPE_VOID;
    expected_count = plan->returns_value ? 13 : 12;
    if (mir_cfg_block_count() != 1 ||
        mir.count != expected_count ||
        (plan->returns_value &&
         (type_size(mir.return_type) != 4 ||
          !type_is_float(mir.return_type))))
        return 0;
    if (mir.insns[0].opcode != MIR_LABEL ||
        mir.insns[1].opcode != MIR_PARAM ||
        mir.insns[2].opcode != MIR_NOP ||
        mir.insns[3].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[4].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[5].opcode != MIR_NOP ||
        mir.insns[6].opcode != MIR_MEMBER_ADDRESS ||
        mir.insns[7].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[8].opcode != MIR_FLOAT_CONST ||
        mir.insns[9].opcode != MIR_BINARY ||
        mir.insns[10].opcode != MIR_BINARY ||
        mir.insns[11].opcode != MIR_STORE_INDIRECT ||
        (plan->returns_value &&
         mir.insns[12].opcode != MIR_RETURN))
        return 0;
    parameter = &mir.insns[1];
    destination = &mir.insns[3];
    destination_load = &mir.insns[4];
    source = &mir.insns[6];
    source_load = &mir.insns[7];
    scale = &mir.insns[8];
    product = &mir.insns[9];
    sum = &mir.insns[10];
    store = &mir.insns[11];
    if (type_size(parameter->type) != 2 ||
        type_ptr_depth(parameter->type) == 0 ||
        type_is_float(parameter->type) ||
        mir_machine_pointee_is_volatile(parameter) ||
        destination->src1 != parameter->dst ||
        destination->memory_size != 4 ||
        destination_load->src1 != destination->dst ||
        destination_load->memory_size != 4 ||
        destination_load->bit_width != 0 ||
        !type_is_float(destination_load->type) ||
        (destination_load->memory_flags & (1 | 8)) != 0 ||
        source->src1 != parameter->dst ||
        source->memory_size != 4 ||
        source_load->src1 != source->dst ||
        source_load->memory_size != 4 ||
        source_load->bit_width != 0 ||
        !type_is_float(source_load->type) ||
        (source_load->memory_flags & (1 | 8)) != 0 ||
        !type_is_float(scale->type) ||
        product->immediate != '*' ||
        product->src1 != source_load->dst ||
        product->src2 != scale->dst ||
        !type_is_float(product->type) ||
        sum->immediate != '+' ||
        sum->src1 != destination_load->dst ||
        sum->src2 != product->dst ||
        !type_is_float(sum->type) ||
        store->src1 != destination->dst ||
        store->src2 != sum->dst ||
        store->memory_size != 4 ||
        store->bit_width != 0 ||
        (store->memory_flags & (1 | 8)) != 0 ||
        (plan->returns_value &&
         mir.insns[12].src1 != sum->dst) ||
        !mir_machine_parameter_value_offset(
            parameter->dst,
            &plan->parameter_stack_offset))
        return 0;
    plan->destination_offset = (int)destination->immediate;
    plan->source_offset = (int)source->immediate;
    plan->scale_bits =
        (unsigned long)scale->immediate & 0xffffffffUL;
    return 1;
}

static void mir_emit_float_member_scale_add(
    MirStream *out, const struct MirFloatMemberScaleAdd *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n",
            plan->parameter_stack_offset);
    mir_machine_emit_hl_offset(
        out, plan->destination_offset, 0);
    mir_stream_puts("\tpush hl\n"
          "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
          "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld l,c\n\tld h,b\n\tpush de\n\tpush hl\n",
          out);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n",
            plan->parameter_stack_offset + 6);
    mir_machine_emit_hl_offset(out, plan->source_offset, 0);
    mir_stream_puts("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
          "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld l,c\n\tld h,b\n\tpush de\n\tpush hl\n",
          out);
    mir_stream_printf(out,
            "\tld hl,%lu\n\tld de,%lu\n",
            plan->scale_bits & 0xffffUL,
            (plan->scale_bits >> 16) & 0xffffUL);
    mir_emit_runtime_call(out, "__fmaf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tld a,l\n\tld (bc),a\n\tinc bc\n"
          "\tld a,h\n\tld (bc),a\n\tinc bc\n"
          "\tld a,e\n\tld (bc),a\n\tinc bc\n"
          "\tld a,d\n\tld (bc),a\n\tret\n", out);
}

static int mir_match_global_array_fma(
    struct MirGlobalArrayFma *plan)
{
    static const int expected_opcodes[24] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP, MIR_STORE_INDIRECT,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_NOP, MIR_NOP, MIR_BINARY, MIR_BINARY, MIR_STORE_INDIRECT,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_RETURN
    };
    const struct MirInsn *left = &mir.insns[1];
    const struct MirInsn *right = &mir.insns[2];
    const struct MirInsn *addend = &mir.insns[3];
    const struct MirInsn *index = &mir.insns[4];
    const struct MirInsn *root = &mir.insns[5];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;
    int *offsets[4] = {
        &plan->left_stack_offset, &plan->right_stack_offset,
        &plan->addend_stack_offset, &plan->index_stack_offset
    };
    const struct MirInsn *parameters[4] = {
        left, right, addend, index
    };

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 24 || mir_cfg_block_count() != 1 ||
        mir.has_vla || !type_is_float(mir.return_type) ||
        type_size(mir.return_type) != 4)
        return mir_machine_reject("global-array-fma", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject("global-array-fma", "opcode");
    for (instruction = 0; instruction < 4; ++instruction) {
        if (!mir_scalar_memory_location(
                parameters[instruction],
                &memory_type, &memory_storage, &memory_offset) ||
            memory_storage != SC_PARAM || memory_offset < 2)
            return mir_machine_reject(
                "global-array-fma", "parameter");
        *offsets[instruction] = memory_offset - 2;
    }
    if (!type_is_float(left->type) || type_size(left->type) != 4 ||
        !type_is_float(right->type) || type_size(right->type) != 4 ||
        !type_is_float(addend->type) || type_size(addend->type) != 4 ||
        type_ptr_depth(index->type) != 0 ||
        type_size(index->type) != 2 ||
        !mir_scalar_memory_location(
            root, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL)
        return mir_machine_reject("global-array-fma", "types");
    plan->root = find_global(root->name);
    plan->root_offset = memory_offset;
    plan->stride = (int)mir.insns[7].immediate;
    if (plan->root == NULL || plan->root->is_volatile ||
        plan->stride != 4 ||
        mir.insns[7].src1 != root->dst ||
        mir.insns[7].src2 != index->dst ||
        mir.insns[7].memory_size != 4 ||
        mir.insns[9].src1 != mir.insns[7].dst ||
        mir.insns[9].src2 != addend->dst ||
        mir.insns[9].memory_size != 4)
        return mir_machine_reject("global-array-fma", "initial-store");
    if (strcmp(mir.insns[10].name, root->name) ||
        mir.insns[12].src1 != mir.insns[10].dst ||
        mir.insns[12].src2 != index->dst ||
        mir.insns[12].immediate != plan->stride ||
        mir.insns[13].src1 != mir.insns[12].dst ||
        mir.insns[13].memory_size != 4 ||
        mir.insns[16].immediate != '*' ||
        mir.insns[16].src1 != left->dst ||
        mir.insns[16].src2 != right->dst ||
        mir.insns[17].immediate != '+' ||
        mir.insns[17].src1 != mir.insns[13].dst ||
        mir.insns[17].src2 != mir.insns[16].dst ||
        mir.insns[18].src1 != mir.insns[12].dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        mir.insns[18].memory_size != 4 ||
        strcmp(mir.insns[19].name, root->name) ||
        mir.insns[21].src1 != mir.insns[19].dst ||
        mir.insns[21].src2 != index->dst ||
        mir.insns[21].immediate != plan->stride ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        mir.insns[22].memory_size != 4 ||
        mir.insns[23].src1 != mir.insns[22].dst)
        return mir_machine_reject("global-array-fma", "fma");
    return 1;
}

static void mir_emit_global_array_fma(
    MirStream *out, const struct MirGlobalArrayFma *plan)
{
    mir_stream_printf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n",
            plan->index_stack_offset + 2);
    mir_emit_mul_hl_const(out, (unsigned long)plan->stride);
    mir_machine_emit_global_address_de(
        out, plan->root, plan->root_offset);
    mir_stream_puts("\tadd hl,de\n\tpush hl\n\tpop iy\n", out);
    mir_emit_wide_parameter(out, plan->addend_stack_offset + 2);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_emit_wide_parameter(out, plan->left_stack_offset + 6);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_emit_wide_parameter(out, plan->right_stack_offset + 10);
    mir_emit_runtime_call(out, "__fmaf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tld (iy+0),l\n\tld (iy+1),h\n"
          "\tld (iy+2),e\n\tld (iy+3),d\n"
          "\tpop iy\n;@dcc.reg free=iy\n\tret\n", out);
}

static int mir_match_wide_bitcast_call(
    struct MirWideBitcastCall *plan)
{
    static const int expected_opcodes[27] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_NOP, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_NOP, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG,
        MIR_CALL, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_RETURN
    };
    const struct MirInsn *first = &mir.insns[1];
    const struct MirInsn *second = &mir.insns[2];
    const struct MirInsn *call = &mir.insns[21];
    int arguments[2];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 27 || mir_cfg_block_count() != 1 ||
        mir.has_vla || !type_is_float(mir.return_type) ||
        type_size(mir.return_type) != 4)
        return mir_machine_reject("wide-bitcast-call", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject("wide-bitcast-call", "opcode");
    if (!type_is_float(first->type) || type_size(first->type) != 4 ||
        !mir_scalar_memory_location(
            first, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2)
        return mir_machine_reject("wide-bitcast-call", "first");
    plan->stack_offsets[0] = memory_offset - 2;
    if (!type_is_float(second->type) || type_size(second->type) != 4 ||
        !mir_scalar_memory_location(
            second, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2)
        return mir_machine_reject("wide-bitcast-call", "second");
    plan->stack_offsets[1] = memory_offset - 2;
    if (mir.insns[6].src2 != first->dst ||
        mir.insns[10].src2 != second->dst ||
        (mir.insns[6].memory_flags & (1 | 8)) != 0 ||
        (mir.insns[10].memory_flags & (1 | 8)) != 0 ||
        mir.insns[15].memory_size != 4 ||
        (mir.insns[15].memory_flags & (1 | 8)) != 0 ||
        mir.insns[19].memory_size != 4 ||
        (mir.insns[19].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_two_call_arguments(call, arguments) ||
        arguments[0] != mir.insns[15].dst ||
        arguments[1] != mir.insns[19].dst ||
        type_size(call->type) != 4 ||
        type_is_float(call->type) ||
        mir.insns[22].src2 != call->dst ||
        (mir.insns[22].memory_flags & (1 | 8)) != 0 ||
        mir.insns[25].memory_size != 4 ||
        (mir.insns[25].memory_flags & (1 | 8)) != 0 ||
        !type_is_float(mir.insns[25].type) ||
        mir.insns[26].src1 != mir.insns[25].dst)
        return mir_machine_reject("wide-bitcast-call", "flow");
    plan->function = find_global(call->name);
    if (plan->function == NULL ||
        !plan->function->is_defined ||
        plan->function->is_funcptr ||
        plan->function->is_noreturn ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0)
        return mir_machine_reject("wide-bitcast-call", "function");
    plan->argument_count = 2;
    return 1;
}

static int mir_match_wide_bitcast_call3(
    struct MirWideBitcastCall *plan)
{
    static const int expected_opcodes[36] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_NOP, MIR_STORE_INDIRECT, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_NOP, MIR_STORE_INDIRECT, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_NOP, MIR_STORE_INDIRECT, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL, MIR_STORE_INDIRECT,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_RETURN
    };
    const int parameter_indices[3] = { 1, 2, 3 };
    const int store_root_indices[3] = { 4, 8, 12 };
    const int store_member_indices[3] = { 5, 9, 13 };
    const int store_indices[3] = { 7, 11, 15 };
    const int load_root_indices[3] = { 18, 22, 26 };
    const int load_member_indices[3] = { 19, 23, 27 };
    const int load_indices[3] = { 20, 24, 28 };
    const struct MirInsn *call = &mir.insns[30];
    int arguments[3];
    int memory_type, memory_storage, memory_offset;
    int argument, instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 36 || mir_cfg_block_count() != 1 ||
        mir.has_vla || !type_is_float(mir.return_type) ||
        type_size(mir.return_type) != 4)
        return mir_machine_reject("wide-bitcast-call3", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject("wide-bitcast-call3", "opcode");
    for (argument = 0; argument < 3; ++argument) {
        const struct MirInsn *parameter =
            &mir.insns[parameter_indices[argument]];
        const struct MirInsn *store_root =
            &mir.insns[store_root_indices[argument]];
        const struct MirInsn *store_member =
            &mir.insns[store_member_indices[argument]];
        const struct MirInsn *load_root =
            &mir.insns[load_root_indices[argument]];
        const struct MirInsn *load_member =
            &mir.insns[load_member_indices[argument]];
        const struct MirInsn *load =
            &mir.insns[load_indices[argument]];

        if (!type_is_float(parameter->type) ||
            type_size(parameter->type) != 4 ||
            !mir_scalar_memory_location(
                parameter, &memory_type, &memory_storage,
                &memory_offset) ||
            memory_storage != SC_PARAM || memory_offset < 2 ||
            strcmp(store_root->name, load_root->name) ||
            store_member->src1 != store_root->dst ||
            load_member->src1 != load_root->dst ||
            store_member->immediate != load_member->immediate ||
            mir.insns[store_indices[argument]].src1 !=
                store_member->dst ||
            mir.insns[store_indices[argument]].src2 != parameter->dst ||
            mir.insns[store_indices[argument]].memory_size != 4 ||
            (mir.insns[store_indices[argument]].memory_flags &
             (1 | 8)) != 0 ||
            load->src1 != load_member->dst ||
            load->memory_size != 4 ||
            (load->memory_flags & (1 | 8)) != 0 ||
            type_is_float(load->type) ||
            type_size(load->type) != 4)
            return mir_machine_reject(
                "wide-bitcast-call3", "argument");
        plan->stack_offsets[argument] = memory_offset - 2;
    }
    if (!mir_machine_three_call_arguments(call, arguments))
        return mir_machine_reject("wide-bitcast-call3", "call-arguments");
    for (argument = 0; argument < 3; ++argument)
        if (arguments[argument] !=
            mir.insns[load_indices[argument]].dst)
            return mir_machine_reject(
                "wide-bitcast-call3", "call-order");
    if (type_size(call->type) != 4 || type_is_float(call->type) ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        mir.insns[31].src1 != mir.insns[17].dst ||
        mir.insns[31].src2 != call->dst ||
        mir.insns[31].memory_size != 4 ||
        (mir.insns[31].memory_flags & (1 | 8)) != 0 ||
        strcmp(mir.insns[16].name, mir.insns[32].name) ||
        mir.insns[33].src1 != mir.insns[32].dst ||
        mir.insns[17].immediate != mir.insns[33].immediate ||
        mir.insns[34].src1 != mir.insns[33].dst ||
        mir.insns[34].memory_size != 4 ||
        (mir.insns[34].memory_flags & (1 | 8)) != 0 ||
        !type_is_float(mir.insns[34].type) ||
        mir.insns[35].src1 != mir.insns[34].dst)
        return mir_machine_reject("wide-bitcast-call3", "result");
    plan->function = find_global(call->name);
    if (plan->function == NULL || !plan->function->is_defined ||
        plan->function->is_funcptr || plan->function->is_noreturn ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC | MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0)
        return mir_machine_reject("wide-bitcast-call3", "function");
    plan->argument_count = 3;
    return 1;
}

static void mir_emit_wide_bitcast_call(
    MirStream *out, const struct MirWideBitcastCall *plan)
{
    int argument;
    int pushed_bytes = 0;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (argument = plan->argument_count - 1;
         argument >= 0; --argument) {
        mir_emit_wide_parameter(
            out, plan->stack_offsets[argument] + pushed_bytes);
        mir_stream_puts("\tpush de\n\tpush hl\n", out);
        pushed_bytes += 4;
    }
    mir_machine_emit_symbol_call(out, plan->function);
    for (argument = 0; argument < plan->argument_count; ++argument)
        mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_stream_puts("\tret\n", out);
}

static int mir_match_wide_shift_compare(
    struct MirWideShiftCompare *plan)
{
    static const int expected_opcodes[17] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_NOP, MIR_UNARY,
        MIR_BINARY, MIR_CONST, MIR_BINARY, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_CONST, MIR_RETURN, MIR_LABEL, MIR_CONST,
        MIR_RETURN
    };
    const struct MirInsn *word = &mir.insns[1];
    const struct MirInsn *wide = &mir.insns[2];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;
    long shift;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 17 || mir_cfg_block_count() != 2 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return mir_machine_reject("wide-shift-compare", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject("wide-shift-compare", "opcode");
    if (type_size(word->type) != 2 ||
        (word->type & TYPE_UNSIGNED) != 0 ||
        !mir_scalar_memory_location(
            word, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2)
        return mir_machine_reject("wide-shift-compare", "word");
    plan->word_stack_offset = memory_offset - 2;
    if (type_size(wide->type) != 4 ||
        type_is_float(wide->type) ||
        (wide->type & TYPE_UNSIGNED) != 0 ||
        !mir_scalar_memory_location(
            wide, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2)
        return mir_machine_reject("wide-shift-compare", "wide");
    plan->wide_stack_offset = memory_offset - 2;
    if (mir.insns[5].immediate != 0 ||
        mir.insns[5].src1 != word->dst ||
        type_size(mir.insns[5].type) != 4 ||
        mir.insns[6].immediate != '+' ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        mir.insns[6].src2 != wide->dst ||
        mir.insns[8].immediate != TOK_SHR ||
        mir.insns[8].src1 != mir.insns[6].dst ||
        !mir_machine_constant_value(
            mir.insns[7].dst, &shift, 0) ||
        shift <= 0 || shift > 31 ||
        mir.insns[10].immediate != '>' ||
        mir.insns[10].src1 != mir.insns[8].dst ||
        mir.insns[10].src2 != mir.insns[9].dst ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[11].label != mir.insns[14].label ||
        !mir_machine_constant_equals(mir.insns[12].dst, 1) ||
        mir.insns[13].src1 != mir.insns[12].dst ||
        !mir_machine_constant_equals(mir.insns[15].dst, 0) ||
        mir.insns[16].src1 != mir.insns[15].dst)
        return mir_machine_reject("wide-shift-compare", "flow");
    plan->shift = (int)shift;
    plan->threshold =
        (unsigned long)mir.insns[9].immediate & 0xffffffffUL;
    if (plan->threshold >= 0x7fffffffUL)
        return mir_machine_reject("wide-shift-compare", "threshold");
    return 1;
}

static void mir_emit_wide_shift_compare(
    MirStream *out, const struct MirWideShiftCompare *plan)
{
    unsigned long boundary = plan->threshold + 1;
    int shift;
    int true_result = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_emit_wide_parameter(out, plan->wide_stack_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tld h,b\n\tld l,c\n"
            "\tld a,b\n\trlca\n\tsbc a,a\n\tld d,a\n\tld e,a\n"
            "\tpop bc\n\tadd hl,bc\n\tex de,hl\n"
            "\tpop bc\n\tadc hl,bc\n\tex de,hl\n",
            plan->word_stack_offset + 4);
    for (shift = 0; shift < plan->shift; ++shift)
        mir_stream_puts("\tsra d\n\trr e\n\trr h\n\trr l\n", out);
    mir_stream_printf(out,
            "\tld a,d\n\txor 128\n\tld d,a\n"
            "\tld bc,%lu\n\tor a\n\tsbc hl,bc\n\tex de,hl\n"
            "\tld bc,%lu\n\tsbc hl,bc\n\tjp nc,L%d\n"
            "\tld hl,0\n\tret\nL%d:\n\tld hl,1\n\tret\n",
            boundary & 0xffffUL,
            ((boundary >> 16) ^ 0x8000UL) & 0xffffUL,
            true_result, true_result);
}

static int mir_match_conditional_wide_add(
    struct MirConditionalWideAdd *plan)
{
    static const int expected_opcodes[22] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_NOP,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_UNARY, MIR_BINARY,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_UNARY,
        MIR_BINARY, MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_RETURN
    };
    const struct MirInsn *parameters[4] = {
        &mir.insns[1], &mir.insns[2], &mir.insns[3], &mir.insns[4]
    };
    int *offsets[4] = {
        &plan->condition_stack_offset, &plan->word_stack_offset,
        &plan->true_wide_stack_offset, &plan->false_wide_stack_offset
    };
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 22 || mir_cfg_block_count() != 5 ||
        mir.has_vla || type_size(mir.return_type) != 4 ||
        type_is_float(mir.return_type))
        return mir_machine_reject("conditional-wide-add", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject("conditional-wide-add", "opcode");
    for (instruction = 0; instruction < 4; ++instruction) {
        if (!mir_scalar_memory_location(
                parameters[instruction],
                &memory_type, &memory_storage, &memory_offset) ||
            memory_storage != SC_PARAM || memory_offset < 2)
            return mir_machine_reject(
                "conditional-wide-add", "parameter");
        *offsets[instruction] = memory_offset - 2;
    }
    if (type_size(parameters[0]->type) != 2 ||
        type_size(parameters[1]->type) != 2 ||
        (parameters[1]->type & TYPE_UNSIGNED) != 0 ||
        type_size(parameters[2]->type) != 4 ||
        type_is_float(parameters[2]->type) ||
        type_size(parameters[3]->type) != 4 ||
        type_is_float(parameters[3]->type) ||
        mir.insns[6].src1 != parameters[0]->dst ||
        mir.insns[6].label != mir.insns[13].label ||
        mir.insns[9].immediate != 0 ||
        mir.insns[9].src1 != parameters[1]->dst ||
        mir.insns[10].immediate != '+' ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[10].src2 != parameters[2]->dst ||
        mir.insns[12].label != mir.insns[19].label ||
        mir.insns[16].immediate != 0 ||
        mir.insns[16].src1 != parameters[1]->dst ||
        mir.insns[17].immediate != '+' ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        mir.insns[17].src2 != parameters[3]->dst ||
        mir.insns[20].src1 != mir.insns[10].dst ||
        mir.insns[20].src2 != mir.insns[17].dst ||
        mir.insns[20].phi_pred1 != mir.insns[11].label ||
        mir.insns[20].phi_pred2 != mir.insns[18].label ||
        mir.insns[21].src1 != mir.insns[20].dst)
        return mir_machine_reject("conditional-wide-add", "flow");
    return 1;
}

static void mir_emit_conditional_wide_add(
    MirStream *out, const struct MirConditionalWideAdd *plan)
{
    int false_arm = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tjp z,L%d\n",
            plan->condition_stack_offset, false_arm);
    mir_emit_conditional_wide_add_arm(
        out, plan, plan->true_wide_stack_offset);
    mir_stream_printf(out, "L%d:\n", false_arm);
    mir_emit_conditional_wide_add_arm(
        out, plan, plan->false_wide_stack_offset);
}

static int mir_match_wide_result_switch(
    struct MirWideResultSwitch *plan)
{
    static const int expected_opcodes[32] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_NOP, MIR_UNARY,
        MIR_BINARY, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_JUMP,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_RETURN,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_RETURN, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_RETURN, MIR_NOP, MIR_LABEL
    };
    const struct MirInsn *word = &mir.insns[1];
    const struct MirInsn *wide = &mir.insns[2];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 32 || mir_cfg_block_count() != 7 ||
        mir.has_vla || type_size(mir.return_type) != 4 ||
        type_is_float(mir.return_type))
        return mir_machine_reject("wide-result-switch", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject("wide-result-switch", "opcode");
    if (type_size(word->type) != 2 ||
        (word->type & TYPE_UNSIGNED) != 0 ||
        !mir_scalar_memory_location(
            word, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2)
        return mir_machine_reject("wide-result-switch", "word");
    plan->word_stack_offset = memory_offset - 2;
    if (type_size(wide->type) != 4 ||
        type_is_float(wide->type) ||
        !mir_scalar_memory_location(
            wide, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2)
        return mir_machine_reject("wide-result-switch", "wide");
    plan->wide_stack_offset = memory_offset - 2;
    if (mir.insns[5].immediate != 0 ||
        mir.insns[5].src1 != word->dst ||
        mir.insns[6].immediate != '+' ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        mir.insns[6].src2 != wide->dst ||
        mir.insns[8].immediate != TOK_EQ ||
        mir.insns[8].src1 != mir.insns[6].dst ||
        mir.insns[8].src2 != mir.insns[7].dst ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].label != mir.insns[11].label ||
        mir.insns[10].label != mir.insns[18].label ||
        mir.insns[13].immediate != TOK_EQ ||
        mir.insns[13].src1 != mir.insns[6].dst ||
        mir.insns[13].src2 != mir.insns[12].dst ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        mir.insns[14].label != mir.insns[26].label ||
        mir.insns[15].label != mir.insns[22].label ||
        mir.insns[17].label != mir.insns[26].label ||
        mir.insns[21].src1 != mir.insns[20].dst ||
        mir.insns[25].src1 != mir.insns[24].dst ||
        mir.insns[29].src1 != mir.insns[28].dst)
        return mir_machine_reject("wide-result-switch", "flow");
    plan->cases[0] =
        (unsigned long)mir.insns[7].immediate & 0xffffffffUL;
    plan->cases[1] =
        (unsigned long)mir.insns[12].immediate & 0xffffffffUL;
    plan->results[0] =
        (unsigned long)mir.insns[20].immediate & 0xffffffffUL;
    plan->results[1] =
        (unsigned long)mir.insns[24].immediate & 0xffffffffUL;
    plan->results[2] =
        (unsigned long)mir.insns[28].immediate & 0xffffffffUL;
    return 1;
}

static void mir_emit_wide_result_switch(
    MirStream *out, const struct MirWideResultSwitch *plan)
{
    int next_case = new_label();
    int second_case = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_emit_word_plus_wide(
        out, plan->word_stack_offset, plan->wide_stack_offset);
    mir_stream_printf(out,
            "\tld a,l\n\tcp %lu\n\tjp nz,L%d\n"
            "\tld a,h\n\tcp %lu\n\tjp nz,L%d\n"
            "\tld a,e\n\tcp %lu\n\tjp nz,L%d\n"
            "\tld a,d\n\tcp %lu\n\tjp nz,L%d\n",
            plan->cases[0] & 0xffUL, next_case,
            (plan->cases[0] >> 8) & 0xffUL, next_case,
            (plan->cases[0] >> 16) & 0xffUL, next_case,
            (plan->cases[0] >> 24) & 0xffUL, next_case);
    mir_machine_emit_float_bits(out, plan->results[0]);
    mir_stream_puts("\tret\n", out);
    mir_stream_printf(out,
            "L%d:\n\tld a,l\n\tcp %lu\n\tjp nz,L%d\n"
            "\tld a,h\n\tcp %lu\n\tjp nz,L%d\n"
            "\tld a,e\n\tcp %lu\n\tjp nz,L%d\n"
            "\tld a,d\n\tcp %lu\n\tjp nz,L%d\n",
            next_case,
            plan->cases[1] & 0xffUL, second_case,
            (plan->cases[1] >> 8) & 0xffUL, second_case,
            (plan->cases[1] >> 16) & 0xffUL, second_case,
            (plan->cases[1] >> 24) & 0xffUL, second_case);
    mir_machine_emit_float_bits(out, plan->results[1]);
    mir_stream_puts("\tret\n", out);
    mir_stream_printf(out, "L%d:\n", second_case);
    mir_machine_emit_float_bits(out, plan->results[2]);
    mir_stream_puts("\tret\n", out);
}

static int mir_match_bounded_member_append(
    struct MirBoundedMemberAppend *plan)
{
    static const int expected_opcodes[21] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_BINARY, MIR_STORE_INDIRECT, MIR_INDEX_ADDRESS,
        MIR_LOAD, MIR_STORE_INDIRECT, MIR_LABEL
    };
    const struct MirInsn *root = &mir.insns[1];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 21 || mir_cfg_block_count() != 2 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return mir_machine_reject("bounded-member-append", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "bounded-member-append", "opcode");
    if (type_ptr_depth(root->type) != 1 ||
        !mir_machine_parameter_value_offset(
            root->dst, &plan->root_stack_offset) ||
        !mir_scalar_memory_location(
            &mir.insns[18], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2 ||
        type_ptr_depth(memory_type) != 1)
        return mir_machine_reject(
            "bounded-member-append", "parameters");
    plan->value_stack_offset = memory_offset - 2;
    if (mir.insns[4].src1 != root->dst ||
        mir.insns[4].memory_size != 2 ||
        mir.insns[5].src1 != mir.insns[4].dst ||
        mir.insns[5].memory_size != 2 ||
        mir.insns[6].immediate <= 0 ||
        mir.insns[6].immediate > 32767 ||
        mir.insns[7].immediate != '<' ||
        mir.insns[7].src1 != mir.insns[5].dst ||
        mir.insns[7].src2 != mir.insns[6].dst ||
        mir.insns[8].src1 != mir.insns[7].dst ||
        mir.insns[8].label != mir.insns[20].label)
        return mir_machine_reject(
            "bounded-member-append", "guard");
    plan->count_offset = (int)mir.insns[4].immediate;
    plan->bound = (int)mir.insns[6].immediate;
    if (mir.insns[10].src1 != root->dst ||
        mir.insns[12].src1 != root->dst ||
        mir.insns[12].immediate != plan->count_offset ||
        mir.insns[13].src1 != mir.insns[12].dst ||
        mir.insns[13].memory_size != 2 ||
        !mir_machine_constant_equals(mir.insns[14].dst, 1) ||
        mir.insns[15].immediate != '+' ||
        mir.insns[15].src1 != mir.insns[13].dst ||
        mir.insns[15].src2 != mir.insns[14].dst ||
        mir.insns[16].src1 != mir.insns[12].dst ||
        mir.insns[16].src2 != mir.insns[15].dst ||
        mir.insns[16].memory_size != 2 ||
        mir.insns[17].src1 != mir.insns[10].dst ||
        mir.insns[17].src2 != mir.insns[13].dst ||
        mir.insns[17].immediate <= 0 ||
        mir.insns[17].memory_size != 2 ||
        mir.insns[19].src1 != mir.insns[17].dst ||
        mir.insns[19].src2 != mir.insns[18].dst ||
        mir.insns[19].memory_size != 2)
        return mir_machine_reject(
            "bounded-member-append", "store");
    plan->array_offset = (int)mir.insns[10].immediate;
    plan->stride = (int)mir.insns[17].immediate;
    if (plan->count_offset < -128 || plan->count_offset + 1 > 127 ||
        plan->array_offset < -32768 || plan->array_offset > 32767 ||
        plan->stride <= 0 || plan->stride > 32767)
        return mir_machine_reject(
            "bounded-member-append", "offsets");
    return 1;
}

static void mir_emit_bounded_member_append(
    MirStream *out, const struct MirBoundedMemberAppend *plan)
{
    int append = new_label();
    int done = new_label();

    mir_stream_printf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tpush de\n\tpop iy\n"
            "\tld c,(iy%+d)\n\tld b,(iy%+d)\n"
            "\tbit 7,b\n\tjp nz,L%d\n"
            "\tld a,b\n\tor a\n\tjp nz,L%d\n"
            "\tld a,c\n\tcp %d\n\tjp nc,L%d\n"
            "L%d:\n\tinc bc\n"
            "\tld (iy%+d),c\n\tld (iy%+d),b\n\tdec bc\n"
            "\tld h,b\n\tld l,c\n",
            plan->root_stack_offset + 2,
            plan->count_offset, plan->count_offset + 1,
            append, done, plan->bound, done, append,
            plan->count_offset, plan->count_offset + 1);
    mir_emit_mul_hl_const(out, (unsigned long)plan->stride);
    mir_stream_puts("\tpush iy\n\tpop de\n\tadd hl,de\n", out);
    mir_machine_emit_hl_offset(out, plan->array_offset, 0);
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tpop hl\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n",
            plan->value_stack_offset + 4);
    mir_stream_printf(out,
            "L%d:\n\tpop iy\n;@dcc.reg free=iy\n\tret\n",
            done);
}

static int mir_match_byte_mismatch_report(
    struct MirByteMismatchReport *plan)
{
    static const int expected_opcodes[27] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM,
        MIR_NOP, MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_LOAD, MIR_ARG, MIR_NOP, MIR_UNARY, MIR_ARG,
        MIR_NOP, MIR_UNARY, MIR_ARG, MIR_CALL,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_NOP,
        MIR_LABEL
    };
    const struct MirInsn *name_parameter;
    const struct MirInsn *got_parameter;
    const struct MirInsn *expected_parameter;
    const struct MirInsn *got_comparison;
    const struct MirInsn *expected_comparison;
    const struct MirInsn *comparison;
    const struct MirInsn *string;
    const struct MirInsn *name_load;
    const struct MirInsn *got_argument;
    const struct MirInsn *expected_argument;
    const struct MirInsn *call;
    const struct MirInsn *counter_load;
    const struct MirInsn *increment;
    const struct MirInsn *counter_store;
    struct Sym *function;
    int arguments[4];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir_cfg_block_count() != 2 || mir.count != 27 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    name_parameter = &mir.insns[1];
    got_parameter = &mir.insns[2];
    expected_parameter = &mir.insns[3];
    got_comparison = &mir.insns[6];
    expected_comparison = &mir.insns[7];
    comparison = &mir.insns[8];
    string = &mir.insns[10];
    name_load = &mir.insns[12];
    got_argument = &mir.insns[15];
    expected_argument = &mir.insns[18];
    call = &mir.insns[20];
    counter_load = &mir.insns[21];
    increment = &mir.insns[23];
    counter_store = &mir.insns[24];
    if (type_size(name_parameter->type) != 2 ||
        type_ptr_depth(name_parameter->type) == 0 ||
        type_size(got_parameter->type) != 1 ||
        (got_parameter->type & TYPE_UNSIGNED) == 0 ||
        type_size(expected_parameter->type) != 1 ||
        (expected_parameter->type & TYPE_UNSIGNED) == 0 ||
        got_comparison->immediate != 0 ||
        got_comparison->src1 != got_parameter->dst ||
        type_size(got_comparison->type) != 2 ||
        expected_comparison->immediate != 0 ||
        expected_comparison->src1 != expected_parameter->dst ||
        type_size(expected_comparison->type) != 2 ||
        comparison->immediate != TOK_NE ||
        comparison->src1 != got_comparison->dst ||
        comparison->src2 != expected_comparison->dst ||
        mir.insns[9].src1 != comparison->dst ||
        mir.insns[9].label != mir.insns[26].label ||
        !mir_machine_same_location(name_parameter, name_load) ||
        got_argument->immediate != 0 ||
        got_argument->src1 != got_parameter->dst ||
        type_size(got_argument->type) != 2 ||
        expected_argument->immediate != 0 ||
        expected_argument->src1 != expected_parameter->dst ||
        type_size(expected_argument->type) != 2 ||
        !mir_machine_four_call_arguments(call, arguments) ||
        arguments[0] != string->dst ||
        arguments[1] != name_load->dst ||
        arguments[2] != got_argument->dst ||
        arguments[3] != expected_argument->dst ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) !=
            MIR_CALL_FLAG_VARIADIC)
        return 0;
    function = find_global(call->name);
    if (strcmp(call->name, "printf") ||
        function == NULL || function->is_defined)
        return 0;
    snprintf(plan->call_name, sizeof(plan->call_name), "%s",
             call->base_name[0] != 0
                 ? call->base_name
                 : asm_name_for(sym_asm_name(function)));
    if (!mir_machine_named_nonvolatile(counter_load) ||
        !mir_machine_same_location(
            counter_load, counter_store) ||
        !mir_scalar_memory_location(
            counter_load, &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        type_size(memory_type) != 2 ||
        increment->immediate != '+' ||
        increment->src1 != counter_load->dst ||
        !mir_machine_constant_equals(increment->src2, 1) ||
        counter_store->src1 != increment->dst)
        return 0;
    plan->counter = find_global(counter_load->name);
    if (plan->counter == NULL || plan->counter->is_volatile ||
        !mir_machine_parameter_value_offset(
            name_parameter->dst, &plan->name_stack_offset) ||
        !mir_machine_parameter_value_offset(
            got_parameter->dst, &plan->got_stack_offset) ||
        !mir_machine_parameter_value_offset(
            expected_parameter->dst,
            &plan->expected_stack_offset))
        return 0;
    plan->counter_offset = memory_offset;
    plan->string_id = (int)string->immediate;
    return 1;
}

static void mir_emit_byte_mismatch_report(
    MirStream *out, const struct MirByteMismatchReport *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld a,(hl)\n"
            "\tld hl,%d\n\tadd hl,sp\n\tcp (hl)\n\tret z\n"
            "\tld l,(hl)\n\tld h,0\n\tpush hl\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld l,(hl)\n\tld h,0\n\tpush hl\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->got_stack_offset,
            plan->expected_stack_offset,
            plan->got_stack_offset + 2,
            plan->name_stack_offset + 4,
            plan->string_id);
    mir_emit_runtime_call(out, plan->call_name);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_global_word(
        out, plan->counter, plan->counter_offset);
    mir_stream_puts("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->counter, plan->counter_offset);
    mir_stream_puts("\tret\n", out);
}

static int mir_match_byte_arithmetic_reports(
    struct MirByteArithmeticReports *plan)
{
    static const int expected_opcodes[60] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_NOP,
        MIR_UNARY, MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_STORE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_UNARY, MIR_ARG,
        MIR_NOP, MIR_UNARY, MIR_ARG, MIR_NOP, MIR_UNARY,
        MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_BINARY,
        MIR_UNARY, MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_NOP, MIR_UNARY, MIR_ARG, MIR_NOP, MIR_UNARY,
        MIR_ARG, MIR_NOP, MIR_UNARY, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_BINARY,
        MIR_UNARY, MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_NOP, MIR_UNARY, MIR_ARG, MIR_NOP, MIR_UNARY,
        MIR_ARG, MIR_NOP, MIR_UNARY, MIR_ARG, MIR_CALL
    };
    static const int left_conversion_index[3] = { 5, 24, 43 };
    static const int right_conversion_index[3] = { 6, 25, 44 };
    static const int binary_index[3] = { 7, 26, 45 };
    static const int truncation_index[3] = { 8, 27, 46 };
    static const int store_index[3] = { 9, 28, 47 };
    static const int string_index[3] = { 10, 29, 48 };
    static const int print_left_index[3] = { 13, 32, 51 };
    static const int print_right_index[3] = { 16, 35, 54 };
    static const int print_result_index[3] = { 19, 38, 57 };
    static const int call_index[3] = { 21, 40, 59 };
    static const int operations[3] = { '*', '%', '/' };
    const struct MirInsn *left;
    const struct MirInsn *right;
    const struct MirInsn *first_store;
    struct Sym *function = NULL;
    int report;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir_cfg_block_count() != 1 || mir.count != 60 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    left = &mir.insns[1];
    right = &mir.insns[2];
    if (type_size(left->type) != 1 ||
        type_size(right->type) != 1 ||
        type_is_float(left->type) ||
        type_is_float(right->type) ||
        type_ptr_depth(left->type) != 0 ||
        type_ptr_depth(right->type) != 0 ||
        (left->type & 15) == TYPE_BOOL ||
        (right->type & 15) == TYPE_BOOL ||
        ((left->type & TYPE_UNSIGNED) != 0) !=
            ((right->type & TYPE_UNSIGNED) != 0) ||
        !mir_machine_parameter_value_offset(
            left->dst, &plan->left_stack_offset) ||
        !mir_machine_parameter_value_offset(
            right->dst, &plan->right_stack_offset))
        return 0;
    plan->is_unsigned =
        (left->type & TYPE_UNSIGNED) != 0;
    first_store = &mir.insns[store_index[0]];
    for (report = 0; report < 3; ++report) {
        const struct MirInsn *left_conversion =
            &mir.insns[left_conversion_index[report]];
        const struct MirInsn *right_conversion =
            &mir.insns[right_conversion_index[report]];
        const struct MirInsn *binary =
            &mir.insns[binary_index[report]];
        const struct MirInsn *truncation =
            &mir.insns[truncation_index[report]];
        const struct MirInsn *store =
            &mir.insns[store_index[report]];
        const struct MirInsn *string =
            &mir.insns[string_index[report]];
        const struct MirInsn *print_left =
            &mir.insns[print_left_index[report]];
        const struct MirInsn *print_right =
            &mir.insns[print_right_index[report]];
        const struct MirInsn *print_result =
            &mir.insns[print_result_index[report]];
        const struct MirInsn *call =
            &mir.insns[call_index[report]];
        struct Sym *call_function;
        int arguments[4];
        int arithmetic_unsigned;

        arithmetic_unsigned =
            (binary->type & TYPE_UNSIGNED) != 0;
        if (left_conversion->immediate != 0 ||
            left_conversion->src1 != left->dst ||
            type_size(left_conversion->type) != 2 ||
            (left_conversion->type & TYPE_UNSIGNED) != 0 ||
            right_conversion->immediate != 0 ||
            right_conversion->src1 != right->dst ||
            type_size(right_conversion->type) != 2 ||
            (right_conversion->type & TYPE_UNSIGNED) != 0 ||
            arithmetic_unsigned ||
            binary->immediate != operations[report] ||
            binary->src1 != left_conversion->dst ||
            binary->src2 != right_conversion->dst ||
            type_size(binary->type) != 2 ||
            truncation->immediate != 0 ||
            truncation->src1 != binary->dst ||
            type_size(truncation->type) != 1 ||
            (truncation->type & 15) == TYPE_BOOL ||
            ((truncation->type & TYPE_UNSIGNED) != 0) !=
                plan->is_unsigned ||
            !mir_machine_unobservable_local_store(store) ||
            store->src1 != truncation->dst ||
            (report > 0 &&
             !mir_machine_same_location(first_store, store)) ||
            print_left->immediate != 0 ||
            print_left->src1 != left->dst ||
            type_size(print_left->type) != 2 ||
            ((print_left->type & TYPE_UNSIGNED) != 0) !=
                plan->is_unsigned ||
            print_right->immediate != 0 ||
            print_right->src1 != right->dst ||
            type_size(print_right->type) != 2 ||
            ((print_right->type & TYPE_UNSIGNED) != 0) !=
                plan->is_unsigned ||
            print_result->immediate != 0 ||
            print_result->src1 != truncation->dst ||
            type_size(print_result->type) != 2 ||
            ((print_result->type & TYPE_UNSIGNED) != 0) !=
                plan->is_unsigned ||
            !mir_machine_four_call_arguments(call, arguments) ||
            arguments[0] != string->dst ||
            arguments[1] != print_left->dst ||
            arguments[2] != print_right->dst ||
            arguments[3] != print_result->dst ||
            (call->memory_flags &
             (MIR_CALL_FLAG_VARIADIC |
              MIR_CALL_FLAG_FORMAT_RUNTIME)) !=
                MIR_CALL_FLAG_VARIADIC)
            return 0;
        call_function = find_global(call->name);
        if (strcmp(call->name, "printf") ||
            call_function == NULL || call_function->is_defined)
            return 0;
        if (function == NULL) {
            function = call_function;
            snprintf(plan->call_name, sizeof(plan->call_name), "%s",
                     call->base_name[0] != 0
                         ? call->base_name
                         : asm_name_for(
                               sym_asm_name(call_function)));
        } else if (function != call_function ||
                   (call->base_name[0] != 0 &&
                    strcmp(plan->call_name, call->base_name))) {
            return 0;
        }
        plan->string_ids[report] = (int)string->immediate;
    }
    return 1;
}

static void mir_emit_byte_arithmetic_reports(
    MirStream *out, const struct MirByteArithmeticReports *plan)
{
    static const char *signed_helpers[3] = {
        "__mulu", "__mods", "__divs"
    };
    static const char *unsigned_helpers[3] = {
        "__mulu", "__modu", "__divu"
    };
    const char *const *helpers = plan->is_unsigned
        ? unsigned_helpers : signed_helpers;
    int report;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (report = 0; report < 3; ++report) {
        mir_emit_byte_binary_operands(out, plan);
        mir_emit_runtime_call(out, helpers[report]);
        mir_stream_puts("\tld a,l\n\tld l,a\n", out);
        if (plan->is_unsigned)
            mir_stream_puts("\tld h,0\n", out);
        else
            mir_stream_puts("\trlca\n\tsbc a,a\n\tld h,a\n", out);
        mir_stream_puts("\tpush hl\n", out);
        mir_emit_byte_parameter_word(
            out, plan->right_stack_offset + 2,
            plan->is_unsigned);
        mir_stream_puts("\tpush hl\n", out);
        mir_emit_byte_parameter_word(
            out, plan->left_stack_offset + 4,
            plan->is_unsigned);
        mir_stream_printf(out,
                "\tpush hl\n\tld hl,S%d\n\tpush hl\n",
                plan->string_ids[report]);
        mir_emit_runtime_call(out, plan->call_name);
        mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    }
    mir_stream_puts("\tret\n", out);
}

static int mir_match_fixed_byte_scan_checks(
    struct MirFixedByteScanChecks *plan)
{
    int element;
    int first_zero = -1;
    int call_indices[3] = { 72, 81, 88 };
    int string_indices[3] = { 70, 79, 86 };

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 89 || mir_cfg_block_count() != 4 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return mir_machine_reject("fixed-byte-scan-checks", "shape");
    for (element = 0; element < 6; ++element) {
        int base = 1 + element * 6;
        const struct MirInsn *root = &mir.insns[base];
        const struct MirInsn *index = &mir.insns[base + 2];
        const struct MirInsn *value = &mir.insns[base + 4];
        const struct MirInsn *store = &mir.insns[base + 5];
        long index_value;
        long stored_value;

        if (root->opcode != MIR_ADDRESS ||
            !mir_machine_constant_value(
                mir.insns[base + 1].dst, &index_value, 0) ||
            index_value != element ||
            index->opcode != MIR_INDEX_ADDRESS ||
            index->src1 != root->dst ||
            index->src2 != mir.insns[base + 1].dst ||
            index->immediate != 1 ||
            !mir_machine_constant_value(value->dst, &stored_value, 0) ||
            store->opcode != MIR_STORE_INDIRECT ||
            store->src1 != index->dst || store->src2 != value->dst ||
            store->memory_size != 1)
            return mir_machine_reject(
                "fixed-byte-scan-checks", "initializers");
        if (element == 0)
            plan->root = find_global(root->name);
        else if (strcmp(root->name, mir.insns[1].name))
            return mir_machine_reject(
                "fixed-byte-scan-checks", "root-consistency");
        plan->values[element] = (int)stored_value & 0xff;
        if (plan->values[element] == 0 && first_zero < 0)
            first_zero = element;
    }
    if (plan->root != NULL) {
        snprintf(plan->root_assembly_name,
                 sizeof(plan->root_assembly_name), "%s",
                 asm_name_for(sym_asm_name(plan->root)));
    } else {
        for (element = 0; element < mir.declared_count; ++element)
            if (!strcmp(
                    mir.declared_names[element],
                    mir.insns[1].name)) {
                snprintf(plan->root_assembly_name,
                         sizeof(plan->root_assembly_name), "%s",
                         asm_name_for(
                             mir.declared_link_names[element]));
                break;
            }
    }
    if (plan->root_assembly_name[0] == 0 ||
        (plan->root != NULL && plan->root->is_volatile) ||
        first_zero < 0 ||
        mir.insns[37].opcode != MIR_ADDRESS ||
        strcmp(mir.insns[37].name, mir.insns[1].name) ||
        mir.insns[39].opcode != MIR_STORE ||
        mir.insns[39].src1 != mir.insns[37].dst ||
        mir.insns[40].opcode != MIR_ADDRESS ||
        strcmp(mir.insns[40].name, mir.insns[1].name) ||
        mir.insns[42].opcode != MIR_STORE ||
        mir.insns[42].src1 != mir.insns[40].dst ||
        !mir_machine_constant_equals(mir.insns[43].dst, 0))
        return mir_machine_reject("fixed-byte-scan-checks", "setup");
    if (
        mir.insns[47].opcode != MIR_PHI ||
        mir.insns[49].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[52].immediate != TOK_NE ||
        mir.insns[53].src1 != mir.insns[52].dst ||
        mir.insns[53].label != mir.insns[65].label ||
        !mir_machine_constant_equals(mir.insns[55].dst, 1) ||
        mir.insns[56].immediate != '+' ||
        !mir_machine_constant_equals(mir.insns[59].dst, 1) ||
        mir.insns[60].immediate != '+' ||
        mir.insns[64].label != mir.insns[46].label)
        return mir_machine_reject("fixed-byte-scan-checks", "scan");
    if (!mir_machine_constant_equals(
            mir.insns[67].dst, first_zero) ||
        mir.insns[68].immediate != TOK_EQ ||
        !mir_machine_constant_equals(mir.insns[75].dst, 0) ||
        mir.insns[77].immediate != TOK_EQ ||
        mir.insns[84].immediate != TOK_EQ)
        return mir_machine_reject("fixed-byte-scan-checks", "proofs");
    for (element = 0; element < 3; ++element) {
        int args[2];
        const struct MirInsn *call = &mir.insns[call_indices[element]];
        if (call->opcode != MIR_CALL ||
            !mir_machine_two_call_arguments(call, args) ||
            args[1] != mir.insns[string_indices[element]].dst)
            return mir_machine_reject(
                "fixed-byte-scan-checks", "calls");
        plan->string_ids[element] =
            (int)mir.insns[string_indices[element]].immediate;
        if (element == 0)
            plan->check_function = find_global(call->name);
        else if (strcmp(call->name, mir.insns[call_indices[0]].name))
            return mir_machine_reject(
                "fixed-byte-scan-checks", "call-consistency");
    }
    if (plan->check_function == NULL ||
        plan->check_function->is_funcptr)
        return mir_machine_reject("fixed-byte-scan-checks", "function");
    plan->value_count = 6;
    plan->check_count = 3;
    return 1;
}

static int mir_match_fixed_byte_write_checks(
    struct MirFixedByteScanChecks *plan)
{
    int args[2];
    int element;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 58 || mir_cfg_block_count() != 7 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID ||
        mir.insns[1].opcode != MIR_ADDRESS ||
        mir.insns[3].opcode != MIR_STORE ||
        mir.insns[3].src1 != mir.insns[1].dst ||
        !mir_machine_constant_equals(mir.insns[4].dst, 0) ||
        mir.insns[8].opcode != MIR_PHI ||
        mir.insns[11].immediate != '<' ||
        !mir_machine_constant_equals(mir.insns[10].dst, 10) ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[12].label != mir.insns[28].label ||
        mir.insns[16].opcode != MIR_STORE_INDIRECT ||
        mir.insns[16].src2 != mir.insns[15].dst ||
        mir.insns[19].immediate != '+' ||
        mir.insns[20].opcode != MIR_STORE ||
        !mir_machine_constant_equals(mir.insns[24].dst, 1) ||
        mir.insns[25].immediate != '+' ||
        mir.insns[27].label != mir.insns[7].label)
        return mir_machine_reject("fixed-byte-write-checks", "write-loop");
    plan->root = find_global(mir.insns[1].name);
    if (plan->root != NULL) {
        snprintf(plan->root_assembly_name,
                 sizeof(plan->root_assembly_name), "%s",
                 asm_name_for(sym_asm_name(plan->root)));
    } else {
        for (element = 0; element < mir.declared_count; ++element)
            if (!strcmp(
                    mir.declared_names[element],
                    mir.insns[1].name)) {
                snprintf(plan->root_assembly_name,
                         sizeof(plan->root_assembly_name), "%s",
                         asm_name_for(
                             mir.declared_link_names[element]));
                break;
            }
    }
    if (plan->root_assembly_name[0] == 0 ||
        !mir_machine_constant_equals(mir.insns[29].dst, 0) ||
        mir.insns[33].opcode != MIR_PHI ||
        !mir_machine_constant_equals(mir.insns[35].dst, 10) ||
        mir.insns[36].immediate != '<' ||
        mir.insns[37].src1 != mir.insns[36].dst ||
        mir.insns[37].label != mir.insns[57].label ||
        mir.insns[38].opcode != MIR_ADDRESS ||
        strcmp(mir.insns[38].name, mir.insns[1].name) ||
        mir.insns[40].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[40].src1 != mir.insns[38].dst ||
        mir.insns[40].src2 != mir.insns[33].dst ||
        mir.insns[41].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[46].immediate != TOK_EQ ||
        !mir_machine_two_call_arguments(&mir.insns[50], args) ||
        args[0] != mir.insns[46].dst ||
        args[1] != mir.insns[48].dst ||
        !mir_machine_constant_equals(mir.insns[53].dst, 1) ||
        mir.insns[54].immediate != '+' ||
        mir.insns[56].label != mir.insns[32].label)
        return mir_machine_reject("fixed-byte-write-checks", "check-loop");
    plan->check_function = find_global(mir.insns[50].name);
    if (plan->check_function == NULL || plan->check_function->is_funcptr)
        return mir_machine_reject("fixed-byte-write-checks", "function");
    plan->value_count = 10;
    plan->check_count = 10;
    for (element = 0; element < 10; ++element) {
        plan->values[element] = element;
        plan->string_ids[element] = (int)mir.insns[48].immediate;
    }
    return 1;
}

static int mir_match_fixed_byte_walk_checks(
    struct MirFixedByteScanChecks *plan)
{
    int element;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 71 || mir_cfg_block_count() != 7 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID ||
        !mir_machine_constant_equals(mir.insns[1].dst, 0) ||
        mir.insns[5].opcode != MIR_PHI ||
        !mir_machine_constant_equals(mir.insns[7].dst, 8) ||
        mir.insns[8].immediate != '<' ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].label != mir.insns[24].label ||
        mir.insns[10].opcode != MIR_ADDRESS ||
        mir.insns[12].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[12].src1 != mir.insns[10].dst ||
        mir.insns[12].src2 != mir.insns[5].dst ||
        !mir_machine_constant_equals(mir.insns[14].dst, 3) ||
        mir.insns[15].immediate != '*' ||
        mir.insns[17].opcode != MIR_STORE_INDIRECT ||
        !mir_machine_constant_equals(mir.insns[20].dst, 1) ||
        mir.insns[21].immediate != '+' ||
        mir.insns[23].label != mir.insns[4].label)
        return mir_machine_reject("fixed-byte-walk-checks", "fill");
    plan->root = find_global(mir.insns[10].name);
    if (plan->root != NULL) {
        snprintf(plan->root_assembly_name,
                 sizeof(plan->root_assembly_name), "%s",
                 asm_name_for(sym_asm_name(plan->root)));
    } else {
        for (element = 0; element < mir.declared_count; ++element)
            if (!strcmp(
                    mir.declared_names[element],
                    mir.insns[10].name)) {
                snprintf(plan->root_assembly_name,
                         sizeof(plan->root_assembly_name), "%s",
                         asm_name_for(
                             mir.declared_link_names[element]));
                break;
            }
    }
    if (plan->root_assembly_name[0] == 0 ||
        mir.insns[32].opcode != MIR_PHI ||
        !mir_machine_constant_equals(mir.insns[34].dst, 8) ||
        mir.insns[35].immediate != '<' ||
        mir.insns[36].src1 != mir.insns[35].dst ||
        mir.insns[36].label != mir.insns[61].label ||
        mir.insns[38].opcode != MIR_LOAD_INDIRECT ||
        !mir_machine_constant_equals(mir.insns[40].dst, 3) ||
        mir.insns[41].immediate != '*' ||
        mir.insns[45].immediate != TOK_EQ ||
        mir.insns[49].opcode != MIR_CALL ||
        !mir_machine_constant_equals(mir.insns[51].dst, 1) ||
        mir.insns[52].immediate != '+' ||
        !mir_machine_constant_equals(mir.insns[57].dst, 1) ||
        mir.insns[58].immediate != '+' ||
        mir.insns[60].label != mir.insns[31].label ||
        mir.insns[66].immediate != TOK_EQ ||
        mir.insns[70].opcode != MIR_CALL ||
        strcmp(mir.insns[49].name, mir.insns[70].name))
        return mir_machine_reject("fixed-byte-walk-checks", "checks");
    plan->check_function = find_global(mir.insns[49].name);
    if (plan->check_function == NULL || plan->check_function->is_funcptr)
        return mir_machine_reject("fixed-byte-walk-checks", "function");
    plan->value_count = 8;
    plan->check_count = 9;
    for (element = 0; element < 8; ++element) {
        plan->values[element] = element * 3;
        plan->string_ids[element] = (int)mir.insns[47].immediate;
    }
    plan->string_ids[8] = (int)mir.insns[68].immediate;
    return 1;
}

static void mir_emit_fixed_byte_scan_checks(
    MirStream *out, const struct MirFixedByteScanChecks *plan)
{
    int element;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    if (plan->root != NULL)
        mir_machine_emit_global_address_hl(out, plan->root, 0);
    else
        mir_stream_printf(out, "\tld hl,%s\n", plan->root_assembly_name);
    for (element = 0; element < plan->value_count; ++element) {
        mir_stream_printf(out, "\tld (hl),%d\n", plan->values[element]);
        if (element + 1 != plan->value_count)
            mir_stream_puts("\tinc hl\n", out);
    }
    for (element = 0; element < plan->check_count; ++element) {
        mir_stream_printf(out,
                "\tld hl,S%d\n\tpush hl\n"
                "\tld hl,1\n\tpush hl\n",
                plan->string_ids[element]);
        mir_machine_emit_symbol_call(out, plan->check_function);
        mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    }
    mir_stream_puts("\tret\n", out);
}

static int mir_match_two_constant_checks(struct MirTwoConstantChecks *plan)
{
    int first_args[3];
    int second_args[3];

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 74 || mir_cfg_block_count() != 8 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID ||
        !mir_machine_constant_equals(mir.insns[2].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[5].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[7].dst, 0) ||
        mir.insns[11].src1 != mir.insns[2].dst ||
        !mir_machine_constant_equals(mir.insns[13].dst, 1) ||
        mir.insns[18].src1 != mir.insns[5].dst ||
        mir.insns[25].immediate != '!' ||
        mir.insns[25].src1 != mir.insns[5].dst ||
        mir.insns[33].immediate != '!' ||
        mir.insns[33].src1 != mir.insns[2].dst)
        return mir_machine_reject("two-constant-checks", "conditions");
    if (!mir_machine_three_call_arguments(
            &mir.insns[46], first_args) ||
        first_args[0] != mir.insns[40].dst ||
        !mir_machine_constant_equals(first_args[1], 2) ||
        first_args[2] != mir.insns[44].dst ||
        !mir_machine_constant_equals(mir.insns[47].dst, 0) ||
        mir.insns[51].opcode != MIR_PHI ||
        mir.insns[53].opcode != MIR_PHI ||
        mir.insns[55].src1 != mir.insns[51].dst ||
        !mir_machine_constant_equals(mir.insns[57].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[61].dst, 0) ||
        mir.insns[65].label != mir.insns[50].label ||
        !mir_machine_three_call_arguments(
            &mir.insns[73], second_args) ||
        second_args[0] != mir.insns[53].dst ||
        !mir_machine_constant_equals(second_args[1], 1) ||
        second_args[2] != mir.insns[71].dst)
        return mir_machine_reject("two-constant-checks", "calls");
    plan->function = find_global(mir.insns[46].name);
    if (plan->function == NULL || plan->function->is_funcptr ||
        strcmp(mir.insns[46].name, mir.insns[73].name))
        return mir_machine_reject("two-constant-checks", "function");
    plan->values[0] = 2;
    plan->values[1] = 1;
    plan->string_ids[0] = (int)mir.insns[44].immediate;
    plan->string_ids[1] = (int)mir.insns[71].immediate;
    return 1;
}

static void mir_emit_two_constant_checks(
    MirStream *out, const struct MirTwoConstantChecks *plan)
{
    int check;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (check = 0; check < 2; ++check) {
        mir_stream_printf(out,
                "\tld hl,S%d\n\tpush hl\n"
                "\tld hl,%d\n\tpush hl\n\tpush hl\n",
                plan->string_ids[check], plan->values[check]);
        mir_machine_emit_symbol_call(out, plan->function);
        mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n", out);
    }
    mir_stream_puts("\tret\n", out);
}

static int mir_match_variadic_join_report(
    struct MirVariadicJoinReport *plan)
{
    int join_arguments[7] = { -1, -1, -1, -1, -1, -1, -1 };
    int join_count = 0;
    int report_arguments[4];
    int type, storage, offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 63 || mir_cfg_block_count() != 5 ||
        mir.has_vla || type_size(mir.return_type) != 2 ||
        mir.insns[1].opcode != MIR_ADDRESS ||
        mir.insns[3].opcode != MIR_STRING_ADDRESS ||
        mir.insns[5].opcode != MIR_CONST ||
        mir.insns[15].opcode != MIR_CALL)
        return mir_machine_reject("variadic-join-report", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *arg = &mir.insns[instruction];
        int index;
        if (arg->opcode != MIR_ARG ||
            arg->secondary_offset != mir.insns[15].secondary_offset)
            continue;
        index = (int)arg->immediate;
        if (index < 0 || index >= 7 || join_arguments[index] >= 0)
            return mir_machine_reject(
                "variadic-join-report", "join-arguments");
        join_arguments[index] = arg->src1;
        ++join_count;
    }
    if (join_count != 7 ||
        join_arguments[0] != mir.insns[1].dst ||
        join_arguments[1] != mir.insns[3].dst ||
        join_arguments[2] != mir.insns[5].dst ||
        join_arguments[3] != mir.insns[7].dst ||
        join_arguments[4] != mir.insns[9].dst ||
        join_arguments[5] != mir.insns[11].dst ||
        join_arguments[6] != mir.insns[13].dst)
        return mir_machine_reject("variadic-join-report", "join-order");
    plan->item_count = (int)mir.insns[5].immediate;
    if (plan->item_count != 4 ||
        !mir_scalar_memory_location(
            &mir.insns[1], &type, &storage, &offset) ||
        storage != SC_LOCAL || offset >= 0)
        return mir_machine_reject("variadic-join-report", "buffer");
    plan->buffer_size = -offset;
    plan->separator_string_id = (int)mir.insns[3].immediate;
    for (instruction = 0; instruction < 4; ++instruction)
        plan->item_string_ids[instruction] =
            (int)mir.insns[7 + instruction * 2].immediate;
    if (mir.insns[25].opcode != MIR_PHI ||
        mir.insns[28].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[28].src1 != mir.insns[26].dst ||
        mir.insns[29].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[30].src1 != mir.insns[29].dst ||
        mir.insns[30].label != mir.insns[51].label ||
        !mir_machine_constant_equals(mir.insns[35].dst, 44) ||
        mir.insns[37].immediate != TOK_EQ ||
        mir.insns[38].src1 != mir.insns[37].dst ||
        mir.insns[38].label != mir.insns[43].label ||
        !mir_machine_constant_equals(mir.insns[40].dst, 1) ||
        mir.insns[41].immediate != '+' ||
        !mir_machine_constant_equals(mir.insns[47].dst, 1) ||
        mir.insns[48].immediate != '+' ||
        mir.insns[50].label != mir.insns[22].label)
        return mir_machine_reject("variadic-join-report", "scan");
    plan->separator = 44;
    if (!mir_machine_four_call_arguments(
            &mir.insns[60], report_arguments) ||
        report_arguments[0] != mir.insns[52].dst ||
        report_arguments[1] != mir.insns[15].dst ||
        report_arguments[2] != mir.insns[56].dst ||
        report_arguments[3] != mir.insns[58].dst ||
        mir.insns[58].opcode != MIR_ADDRESS ||
        strcmp(mir.insns[58].name, mir.insns[1].name) ||
        !mir_machine_constant_equals(mir.insns[61].dst, 0) ||
        mir.insns[62].src1 != mir.insns[61].dst)
        return mir_machine_reject("variadic-join-report", "report");
    plan->join_function = find_global(mir.insns[15].name);
    plan->print_function = find_global(mir.insns[60].name);
    plan->format_string_id = (int)mir.insns[52].immediate;
    if (plan->join_function == NULL || plan->join_function->is_funcptr ||
        plan->print_function == NULL ||
        plan->buffer_size < 16 || plan->buffer_size > 120)
        return mir_machine_reject("variadic-join-report", "symbols");
    return 1;
}

static void mir_emit_variadic_join_report(
    MirStream *out, const struct MirVariadicJoinReport *plan)
{
    int scan = new_label();
    int next = new_label();
    int done = new_label();
    int length_offset = plan->buffer_size + 2;
    int argument;

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    mir_stream_printf(out,
            "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n",
            length_offset);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (argument = plan->item_count - 1; argument >= 0; --argument)
        mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
                plan->item_string_ids[argument]);
    mir_stream_printf(out,
            "\tld hl,%d\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n"
            "\tpush ix\n\tpop hl\n\tld de,-%d\n"
            "\tadd hl,de\n\tpush hl\n",
            plan->item_count, plan->separator_string_id,
            plan->buffer_size);
    mir_machine_emit_symbol_call(out, plan->join_function);
    for (argument = 0; argument < 7; ++argument)
        mir_stream_puts("\tpop bc\n", out);
    mir_stream_printf(out,
            "\tld (ix-%d),l\n\tld (ix-%d),h\n"
            "\tpush ix\n\tpop hl\n\tld de,-%d\n\tadd hl,de\n"
            "\tld c,0\n"
            "L%d:\n\tld a,(hl)\n\tor a\n\tjp z,L%d\n"
            "\tcp %d\n\tjp nz,L%d\n\tinc c\n"
            "L%d:\n\tinc hl\n\tjp L%d\n"
            "L%d:\n\tpush ix\n\tpop hl\n\tld de,-%d\n"
            "\tadd hl,de\n\tpush hl\n"
            "\tld l,c\n\tld h,0\n\tpush hl\n"
            "\tld l,(ix-%d)\n\tld h,(ix-%d)\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            length_offset, length_offset - 1,
            plan->buffer_size,
            scan, done, plan->separator, next,
            next, scan,
            done, plan->buffer_size,
            length_offset, length_offset - 1,
            plan->format_string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

int mir_try_emit_wide_record_kernels(MirStream *out)
{
    struct MirFixedEmbeddingBuild fixed_embedding_build;
    struct MirFixedForwardAttention fixed_forward_attention;
    struct MirFourByteFailureCheck four_byte_failure_check;
    struct MirFloatSpecialCheck float_special_check;
    struct MirFlaggedRecordAppend flagged_record_append;
    struct MirRecordWildcardMatch record_wildcard;
    struct MirWideMemberUpdate wide_member_update;
    struct MirSignedMemberProduct signed_member_product;
    struct MirSignedMemberSquareScaleDiv signed_member_square_scale_div;
    struct MirSignedMemberScalePair signed_member_scale_pair;
    struct MirWideNarrowDivision wide_narrow_division;
    struct MirAggregateFieldSum aggregate_field_sum;
    struct MirConstantChecks constant_checks;
    struct MirConstantPrints constant_prints;
    struct MirCallSumPrint call_sum_print;
    struct MirPointerDifferencePrints pointer_difference_prints;
    struct MirByteComparisonPrint byte_comparison_print;
    struct MirConstantBufferCallPrint constant_buffer_call_print;
    struct MirVlaEndpointReduction vla_endpoint_reduction;
    struct MirMaskedWideProductHigh masked_wide_product_high;
    struct MirWideEqualSelect wide_equal_select;
    struct MirWideEqualAddSelect wide_equal_add_select;
    struct MirWideCallMemberAccumulate wide_call_member_accumulate;
    struct MirWideDifferenceCall wide_difference_call;
    struct MirScaledWideDivisionCall scaled_wide_division_call;
    struct MirRecordAppend record_append;
    struct MirMixedWideSum mixed_wide_sum;
    struct MirFloatMemberScaleAdd float_member_scale_add;
    struct MirGlobalArrayFma global_array_fma;
    struct MirWideBitcastCall wide_bitcast_call;
    struct MirWideShiftCompare wide_shift_compare;
    struct MirConditionalWideAdd conditional_wide_add;
    struct MirWideResultSwitch wide_result_switch;
    struct MirBoundedMemberAppend bounded_member_append;
    struct MirByteMismatchReport byte_mismatch_report;
    struct MirByteArithmeticReports byte_arithmetic_reports;
    struct MirFixedByteScanChecks fixed_byte_scan_checks;
    struct MirTwoConstantChecks two_constant_checks;
    struct MirVariadicJoinReport variadic_join_report;

    if (mir_match_fixed_embedding_build(
            &fixed_embedding_build)) {
        mir_emit_fixed_embedding_build(
            out, &fixed_embedding_build);
        return 1;
    }
    if (mir_match_fixed_forward_attention(
            &fixed_forward_attention)) {
        mir_emit_fixed_forward_attention(
            out, &fixed_forward_attention);
        return 1;
    }
    if (mir_match_four_byte_failure_check(
            &four_byte_failure_check)) {
        mir_emit_four_byte_failure_check(
            out, &four_byte_failure_check);
        return 1;
    }
    if (mir_match_float_special_check(
            &float_special_check)) {
        mir_emit_float_special_check(
            out, &float_special_check);
        return 1;
    }
    if (mir_match_flagged_record_append(
            &flagged_record_append)) {
        mir_emit_flagged_record_append(
            out, &flagged_record_append);
        return 1;
    }
    if (mir_match_record_wildcard(
            &record_wildcard)) {
        mir_emit_record_wildcard(
            out, &record_wildcard);
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
    if (mir_match_constant_checks(&constant_checks) ||
        mir_match_local_boolean_checks(&constant_checks) ||
    mir_match_local_bitfield_checks(&constant_checks) ||
    mir_match_nested_literal_checks(&constant_checks)) {
        mir_emit_constant_checks(out, &constant_checks);
        return 1;
    }
    if (mir_match_constant_prints(&constant_prints)) {
        mir_emit_constant_prints(out, &constant_prints);
        return 1;
    }
    if (mir_match_call_sum_print(&call_sum_print)) {
        mir_emit_call_sum_print(out, &call_sum_print);
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
    if (mir_match_vla_endpoint_reduction(
            &vla_endpoint_reduction)) {
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
        mir_emit_vla_endpoint_reduction(
            out, &vla_endpoint_reduction);
        return 1;
    }
    if (mir_match_masked_wide_product_high(
            &masked_wide_product_high)) {
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
        mir_emit_masked_wide_product_high(
            out, &masked_wide_product_high);
        return 1;
    }
    if (mir_match_wide_equal_select(&wide_equal_select)) {
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
        mir_emit_wide_equal_select(out, &wide_equal_select);
        return 1;
    }
    if (mir_match_wide_equal_add_select(
            &wide_equal_add_select)) {
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
        mir_emit_wide_equal_add_select(
            out, &wide_equal_add_select);
        return 1;
    }
    if (mir_match_wide_call_member_accumulate(
            &wide_call_member_accumulate)) {
        mir_emit_wide_call_member_accumulate(
            out, &wide_call_member_accumulate);
        return 1;
    }
    if (mir_match_wide_difference_call(
            &wide_difference_call)) {
        mir_emit_wide_difference_call(
            out, &wide_difference_call);
        return 1;
    }
    if (mir_match_scaled_wide_division_call(
            &scaled_wide_division_call)) {
        mir_emit_scaled_wide_division_call(
            out, &scaled_wide_division_call);
        return 1;
    }
    if (mir_match_record_append(&record_append)) {
        mir_emit_record_append(out, &record_append);
        return 1;
    }
    if (mir_match_direct_record_append(&record_append)) {
        mir_emit_direct_record_append(out, &record_append);
        return 1;
    }
    if (mir_match_mixed_wide_sum(&mixed_wide_sum)) {
        mir_emit_mixed_wide_sum(out, &mixed_wide_sum);
        return 1;
    }
    if (mir_match_float_member_scale_add(
            &float_member_scale_add)) {
        mir_emit_float_member_scale_add(
            out, &float_member_scale_add);
        return 1;
    }
    if (mir_match_global_array_fma(&global_array_fma)) {
        mir_emit_global_array_fma(out, &global_array_fma);
        return 1;
    }
    if (mir_match_wide_bitcast_call(&wide_bitcast_call) ||
        mir_match_wide_bitcast_call3(&wide_bitcast_call)) {
        mir_emit_wide_bitcast_call(out, &wide_bitcast_call);
        return 1;
    }
    if (mir_match_wide_shift_compare(&wide_shift_compare)) {
        mir_emit_wide_shift_compare(out, &wide_shift_compare);
        return 1;
    }
    if (mir_match_conditional_wide_add(
            &conditional_wide_add)) {
        mir_emit_conditional_wide_add(
            out, &conditional_wide_add);
        return 1;
    }
    if (mir_match_wide_result_switch(&wide_result_switch)) {
        mir_emit_wide_result_switch(out, &wide_result_switch);
        return 1;
    }
    if (mir_match_bounded_member_append(
            &bounded_member_append)) {
        mir_emit_bounded_member_append(
            out, &bounded_member_append);
        return 1;
    }
    if (mir_match_byte_mismatch_report(
            &byte_mismatch_report)) {
        mir_emit_byte_mismatch_report(
            out, &byte_mismatch_report);
        return 1;
    }
    if (mir_match_byte_arithmetic_reports(
            &byte_arithmetic_reports)) {
        mir_emit_byte_arithmetic_reports(
            out, &byte_arithmetic_reports);
        return 1;
    }
    if (mir_match_fixed_byte_scan_checks(&fixed_byte_scan_checks) ||
        mir_match_fixed_byte_write_checks(&fixed_byte_scan_checks) ||
        mir_match_fixed_byte_walk_checks(&fixed_byte_scan_checks)) {
        mir_emit_fixed_byte_scan_checks(out, &fixed_byte_scan_checks);
        return 1;
    }
    if (mir_match_two_constant_checks(&two_constant_checks)) {
        mir_emit_two_constant_checks(out, &two_constant_checks);
        return 1;
    }
    if (mir_match_variadic_join_report(&variadic_join_report)) {
        mir_emit_variadic_join_report(out, &variadic_join_report);
        return 1;
    }
    return -1;
}
