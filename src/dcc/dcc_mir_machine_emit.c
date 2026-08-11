/* dcc_mir_machine_emit.c - Z80 emission from scheduled MIR templates. */

#include "dcc_mir_internal.h"
#include <stdint.h>

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

struct MirGlobalByteChecks {
    struct Sym *check_function;
    struct Sym *symbols[16];
    int offsets[16];
    int expected[16];
    int strings[16];
    int is_unsigned[16];
    int count;
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

struct MirByteBitwiseReport {
    int left_stack_offset;
    int right_stack_offset;
    int is_unsigned;
    int string_id;
    int runtime_flags;
    char call_name[64];
};

struct MirVariadicSum {
    int count_stack_offset;
    int first_argument_stack_offset;
    int value_width;
};

struct MirGuardedGlobalPop {
    struct Sym *cursor;
    int cursor_offset;
    struct Sym *array;
    int array_offset;
    struct Sym *guard_function;
};

struct MirFloatMemberScalarCompare {
    int pointer_stack_offset;
    int scalar_stack_offset;
    int member_offset;
    int scalar_width;
    int member_is_left;
};

struct MirExactFloatMismatchReport {
    struct Sym *checks;
    int checks_offset;
    struct Sym *failures;
    int failures_offset;
    int name_stack_offset;
    int got_stack_offset;
    int want_stack_offset;
    int string_id;
    int runtime_flags;
    char call_name[64];
};

struct MirFloatToleranceReport {
    struct Sym *checks;
    int checks_offset;
    struct Sym *failures;
    int failures_offset;
    int name_stack_offset;
    int got_stack_offset;
    int want_stack_offset;
    unsigned long epsilon_bits;
    int string_id;
    char call_name[64];
};

struct MirFloatToleranceFailure {
    struct Sym *failures;
    struct Sym *print_function;
    int got_stack_offset;
    int want_stack_offset;
    int name_stack_offset;
    int failures_offset;
    int string_id;
    unsigned long epsilon_bits;
};

struct MirFloatByteReport {
    struct Sym *failures;
    int failures_offset;
    int name_stack_offset;
    int value_stack_offset;
    int expected_stack_offsets[4];
    int string_id;
    char call_name[64];
};

struct MirRelativeToleranceCall {
    struct Sym *function;
    int name_stack_offset;
    int got_stack_offset;
    int want_stack_offset;
    unsigned long scale_bits;
};

struct MirFixedFloatGridFill {
    struct Sym *root;
    int root_offset;
    int outer_bound;
    int inner_bound;
    int add_constant;
    unsigned long divisor_bits;
};

struct MirConstantFloatConditional {
    int condition_stack_offset;
    int true_is_float;
    int true_integer_width;
    unsigned long true_bits;
    int false_is_float;
    int false_integer_width;
    unsigned long false_bits;
};

struct MirConditionalGlobalFloatLoad {
    struct Sym *root;
    int condition_stack_offset;
    int true_offset;
    int false_offset;
};

struct MirReducedFloatPolynomial {
    struct Sym *remainder_function;
    int parameter_stack_offset;
    int sine_form;
    unsigned long one_bits;
    unsigned long two_pi_bits;
    unsigned long pi_bits;
    unsigned long half_pi_bits;
    unsigned long coefficients[3];
};

struct MirFloatTangentRational {
    struct Sym *remainder_function;
    int parameter_stack_offset;
    unsigned long pi_bits;
    unsigned long half_pi_bits;
    unsigned long quarter_pi_bits;
    unsigned long one_bits;
    unsigned long fifteen_bits;
    unsigned long six_bits;
};

struct MirRecursiveWideProduct {
    struct Sym *function;
    int parameter_stack_offset;
    int operation;
    int base_result;
};

struct MirRecursiveWideTreeSum {
    struct Sym *function;
    int parameter_stack_offset;
    int value_offset;
    int value_width;
    int value_is_unsigned;
    int value_first;
    int left_offset;
    int right_offset;
};

struct MirByteRotateFlags {
    struct Sym *state;
    int operation_stack_offset;
    int value_stack_offset;
    int carry_offset;
    int negative_offset;
    int zero_offset;
};

struct MirStatusUnpack {
    struct Sym *memory;
    int memory_offset;
    struct Sym *state;
    int stack_offset;
    int flag_offsets[6];
    int masks[6];
};

struct MirStatusPack {
    struct Sym *state;
    int flag_offsets[6];
    int masks[6];
    struct Sym *function;
};

struct MirByteMathFlags {
    int op_stack_offset;
    int rhs_stack_offset;
    struct Sym *state;
    int accumulator_offset;
    int negative_offset;
    int overflow_offset;
    int decimal_offset;
    int zero_offset;
    int carry_offset;
    struct Sym *compare_function;
    struct Sym *decimal_function;
};

struct MirByteRangeUnion {
    int stack_offset;
    int lower[3];
    int upper[3];
};

struct MirByteArraySum {
    int array_stack_offset;
    int count_stack_offset;
};

struct MirWraparoundBoolStep {
    int count_stack_offset;
    int current_stack_offset;
    int next_stack_offset;
};

struct MirFixedRowWordSum {
    int rows_stack_offset;
    int array_stack_offset;
};

struct MirFixedWideZero {
    int parameter_stack_offset;
    int count;
};

struct MirConstantByteFill {
    int parameter_stack_offset;
    int count;
    int value;
};

struct MirAffineByteFill {
    int pointer_stack_offset;
    int base_stack_offset;
    int count;
    int value_from_parameter;
    int initial_value;
    int step;
};

struct MirPalindromeScan {
    int parameter_stack_offset;
};

struct MirDynamicRowScan {
    struct Sym *table;
    int row_stride;
    int element_stride;
    int count;
};

struct MirByteMismatchScan {
    int pointer_stack_offset;
    int base_stack_offset;
    int count;
};

struct MirVariableByteStepSum {
    int first_stack_offset;
    int step_stack_offset;
    int bound;
    int double_step_value;
    int has_double_step;
};

struct MirFixedReverseWordCopy {
    struct Sym *source;
    struct Sym *destination;
    int source_offset;
    int destination_offset;
    int count;
};

struct MirFixedRandomWordFill {
    struct Sym *destination;
    struct Sym *random_function;
    struct Sym *finish_function;
    int destination_offset;
    int count;
    int modulus;
};

struct MirGlobalByteCopyState {
    struct Sym *source;
    struct Sym *destination;
    struct Sym *state[3];
    int source_offset;
    int destination_offset;
    int state_offsets[3];
    int state_widths[3];
    int state_values[3];
    int count;
};

struct MirFixedGlobalStrideCall {
    struct Sym *function;
    struct Sym *fixed;
    struct Sym *first;
    struct Sym *second;
    int fixed_offset;
    int first_offset;
    int second_offset;
    int first_stride;
    int second_stride;
    int count;
};

struct MirConstantLoopCheck {
    struct Sym *function;
    int string_id;
};

struct MirGlobalByteCountdown {
    struct Sym *value;
    int parameter_stack_offset;
};

struct MirConditionalStringReport {
    struct Sym *function;
    int name_stack_offset;
    int condition_stack_offset;
    int format_string_id;
    int true_string_id;
    int false_string_id;
};

struct MirWordRangeBool {
    int parameter_stack_offset;
    int lower;
    int upper;
};

struct MirAsciiUpper {
    int parameter_stack_offset;
    int width;
    int lower;
    int upper;
    int adjustment;
};

struct MirFixedWordArraySum {
    int parameter_stack_offset;
    int count;
    int pointer_is_volatile;
};

struct MirSliceWordSum {
    int parameter_stack_offset;
    int data_offset;
    int count_offset;
};

struct MirConditionalNullIdentity {
    int condition_stack_offset;
    int pointer_stack_offset;
};

struct MirWideConstantEqual {
    int parameter_stack_offset;
    unsigned long value;
};

struct MirFloatTruthOnce {
    int parameter_stack_offset;
};

struct MirNestedWordLongSelect {
    int first_condition_stack_offset;
    int second_condition_stack_offset;
    int third_stack_offset;
    int first_value;
    int second_value;
    int third_value;
    int third_is_parameter;
};

struct MirFloatIntTruth {
    int float_stack_offset;
    int int_stack_offset;
    int operation;
};

enum MirConditionalFloatLongKind {
    MIR_CONDITIONAL_FLOAT_ADD = 1,
    MIR_CONDITIONAL_FLOAT_CALL = 2
};

struct MirConditionalFloatLong {
    struct Sym *function;
    int condition_stack_offset;
    int argument_stack_offset;
    int true_value;
    unsigned long add_bits;
    int kind;
};

struct MirConditionalPointerFloatLong {
    struct Sym *true_root;
    struct Sym *false_pointer;
    int condition_stack_offset;
    int element_offset;
};

struct MirNestedMemberFloatLong {
    int first_condition_stack_offset;
    int second_condition_stack_offset;
    int pointer_stack_offset;
    int member_offset;
    int first_value;
    int second_value;
};

struct MirConditionalFloatCompareLong {
    int condition_stack_offset;
    int float_stack_offset;
    int true_value;
    int positive_value;
    int nonpositive_value;
};

struct MirConditionalBool {
    int condition_stack_offset;
    int true_value;
    int false_value;
    int result_type;
};

struct MirLogicalOrParameters {
    int first_stack_offset;
    int second_stack_offset;
};

struct MirClearedRecordAppend {
    struct Sym *root;
    struct Sym *clear_function;
    struct Sym *copy_function;
    int root_offset;
    int array_member_offset;
    int cursor_member_offset;
    int stride;
    int name_field_offset;
    int kind_field_offset;
    int value_field_offset;
    int name_stack_offset;
    int kind_stack_offset;
    int value_stack_offset;
};

struct MirRecordNameSearch {
    struct Sym *root;
    struct Sym *compare_function;
    int root_offset;
    int array_member_offset;
    int cursor_member_offset;
    int stride;
    int name_field_offset;
    int name_stack_offset;
};

struct MirSequentialUnaryReports {
    struct Sym *helper;
    struct Sym *print_function;
    int parameter_stack_offsets[5];
    int string_id;
};

struct MirNibbleAppend {
    int pointer_stack_offset;
    int value_stack_offset;
    int threshold;
    int low_adjustment;
    int high_adjustment;
};

struct MirVolatileFillWideConstant {
    int buffer_offset;
    int count;
    unsigned long result;
};

struct MirSingleSignedDivCheck {
    struct Sym *check_function;
    int numerator_stack_offset;
    int denominator_stack_offset;
    int expected_stack_offset;
    int label_stack_offset;
    int operation;
};

struct MirLocalIdentityArrayResult {
    struct Sym *escaped_pointer;
    int escaped_pointer_offset;
    int array_offset;
    int result;
};

#define MIR_MACHINE_SWITCH_RESULT_LIMIT 64

struct MirConstantResultSwitch {
    int parameter_stack_offset;
    int minimum_case;
    int maximum_case;
    int default_result;
    int results[MIR_MACHINE_SWITCH_RESULT_LIMIT];
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
static int mir_machine_name_nonvolatile(const char *name);
static int mir_machine_constant_equals(
    int value, long expected);
static int mir_machine_constant_value(
    int value, long *constant_out, int depth);
static int mir_machine_parameter_value_offset(
    int value, int *stack_offset);
static int mir_machine_pointee_is_volatile(
    const struct MirInsn *parameter);
static void mir_machine_emit_global_address_de(
    FILE *out, struct Sym *symbol, int offset);
static void mir_machine_emit_global_word(
    FILE *out, struct Sym *symbol, int offset);
static void mir_machine_emit_float_bits(
    FILE *out, unsigned long bits);
static void mir_machine_emit_global_byte_a(
    FILE *out, struct Sym *symbol, int offset, int is_store);
static int mir_machine_single_call_argument(
    const struct MirInsn *call, int *argument);

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

static int mir_match_local_constant_store_return(long *value_out)
{
    const struct MirInsn *store = NULL;
    const struct MirInsn *load = NULL;
    const struct MirInsn *return_insn = NULL;
    struct MirMachineForm store_address;
    struct MirMachineForm load_address;
    struct MirMachineForm value;
    int instruction;

    if (mir.has_vla || mir_cfg_block_count() > 2 ||
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
        case MIR_INDEX_ADDRESS:
        case MIR_UNARY:
        case MIR_BINARY:
            break;
        case MIR_JUMP:
            break;
        case MIR_STORE_INDIRECT:
            if (store != NULL || insn->memory_size != 2 ||
                insn->bit_width != 0 ||
                (insn->memory_flags & (1 | 8)) != 0)
                return 0;
            store = insn;
            break;
        case MIR_LOAD_INDIRECT:
            if (load != NULL || insn->memory_size != 2 ||
                insn->bit_width != 0 ||
                (insn->memory_flags & (1 | 8)) != 0)
                return 0;
            load = insn;
            break;
        case MIR_RETURN:
            if (return_insn != NULL)
                return 0;
            return_insn = insn;
            break;
        default:
            return 0;
        }
    }
    if (store == NULL || load == NULL || return_insn == NULL ||
        store >= load || load >= return_insn ||
        return_insn->src1 != load->dst ||
        !mir_machine_pointer_form(
            store->src1, (int)(store - mir.insns),
            &store_address, 0) ||
        !mir_machine_pointer_form(
            load->src1, (int)(load - mir.insns),
            &load_address, 0) ||
        store_address.kind != MIR_MACHINE_FORM_POINTER ||
        load_address.kind != MIR_MACHINE_FORM_POINTER ||
        store_address.storage != SC_LOCAL ||
        load_address.storage != SC_LOCAL ||
        store_address.offset != load_address.offset ||
        store_address.value != load_address.value ||
        strcmp(store_address.name, load_address.name) ||
        !mir_machine_name_nonvolatile(store_address.name) ||
        !mir_machine_pointer_form(
            store->src2, (int)(store - mir.insns),
            &value, 0) ||
        value.kind != MIR_MACHINE_FORM_INTEGER)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_JUMP) {
            int target;

            for (target = instruction + 1;
                 target < mir.count; ++target)
                if (mir.insns[target].opcode == MIR_LABEL &&
                    mir.insns[target].label ==
                        mir.insns[instruction].label)
                    break;
            if (target >= mir.count || &mir.insns[target] > store)
                return 0;
        }
    *value_out = value.value & 0xffffL;
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

static int mir_machine_four_call_arguments(
    const struct MirInsn *call, int arguments[4])
{
    int count = 0;
    int instruction;
    int argument;

    for (argument = 0; argument < 4; ++argument)
        arguments[argument] = -1;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *arg = &mir.insns[instruction];
        int index;

        if (arg->opcode != MIR_ARG ||
            arg->secondary_offset != call->secondary_offset)
            continue;
        index = (int)arg->immediate;
        if (index < 0 || index >= 4 || arguments[index] >= 0)
            return 0;
        arguments[index] = arg->src1;
        ++count;
    }
    return count == 4;
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

static int mir_machine_ten_call_arguments(
    const struct MirInsn *call, int arguments[10])
{
    int count = 0;
    int instruction;
    int argument;

    for (argument = 0; argument < 10; ++argument)
        arguments[argument] = -1;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *arg = &mir.insns[instruction];
        int index;

        if (arg->opcode != MIR_ARG ||
            arg->secondary_offset != call->secondary_offset)
            continue;
        index = (int)arg->immediate;
        if (index < 0 || index >= 10 || arguments[index] >= 0)
            return 0;
        arguments[index] = arg->src1;
        ++count;
    }
    return count == 10;
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
    if (plan->is_unsigned)
        return 0;
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

static int mir_match_byte_bitwise_report(
    struct MirByteBitwiseReport *plan)
{
    static const int expected_opcodes[45] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM,
        MIR_NOP, MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_BINARY,
        MIR_UNARY, MIR_STORE,
        MIR_NOP, MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_BINARY,
        MIR_UNARY, MIR_STORE,
        MIR_NOP, MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_BINARY,
        MIR_UNARY, MIR_STORE,
        MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_STORE,
        MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_STORE,
        MIR_STRING_ADDRESS, MIR_ARG,
        MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG,
        MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL
    };
    static const int left_conversion_index[3] = { 5, 12, 19 };
    static const int right_conversion_index[3] = { 6, 13, 20 };
    static const int binary_index[3] = { 7, 14, 21 };
    static const int truncation_index[5] = { 8, 15, 22, 26, 30 };
    static const int store_index[5] = { 9, 16, 23, 27, 31 };
    static const int operations[3] = { '&', '|', '^' };
    const struct MirInsn *left;
    const struct MirInsn *right;
    const struct MirInsn *string;
    const struct MirInsn *call;
    struct Sym *function;
    int arguments[6];
    int result;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir_cfg_block_count() != 1 || mir.count != 45 ||
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
    for (result = 0; result < 3; ++result) {
        const struct MirInsn *left_conversion =
            &mir.insns[left_conversion_index[result]];
        const struct MirInsn *right_conversion =
            &mir.insns[right_conversion_index[result]];
        const struct MirInsn *binary =
            &mir.insns[binary_index[result]];

        if (left_conversion->immediate != 0 ||
            left_conversion->src1 != left->dst ||
            type_size(left_conversion->type) != 2 ||
            (left_conversion->type & TYPE_UNSIGNED) != 0 ||
            right_conversion->immediate != 0 ||
            right_conversion->src1 != right->dst ||
            type_size(right_conversion->type) != 2 ||
            (right_conversion->type & TYPE_UNSIGNED) != 0 ||
            binary->immediate != operations[result] ||
            binary->src1 != left_conversion->dst ||
            binary->src2 != right_conversion->dst ||
            type_size(binary->type) != 2)
            return 0;
    }
    if (mir.insns[25].immediate != '~' ||
        mir.insns[25].src1 != left->dst ||
        type_size(mir.insns[25].type) != 2 ||
        mir.insns[29].immediate != '~' ||
        mir.insns[29].src1 != right->dst ||
        type_size(mir.insns[29].type) != 2)
        return 0;
    for (result = 0; result < 5; ++result) {
        const struct MirInsn *truncation =
            &mir.insns[truncation_index[result]];
        const struct MirInsn *store =
            &mir.insns[store_index[result]];
        int source = result < 3
            ? mir.insns[binary_index[result]].dst
            : mir.insns[result == 3 ? 25 : 29].dst;

        if (truncation->immediate != 0 ||
            truncation->src1 != source ||
            type_size(truncation->type) != 1 ||
            (truncation->type & 15) == TYPE_BOOL ||
            ((truncation->type & TYPE_UNSIGNED) != 0) !=
                plan->is_unsigned ||
            !mir_machine_unobservable_local_store(store) ||
            store->src1 != truncation->dst)
            return 0;
    }
    string = &mir.insns[32];
    call = &mir.insns[44];
    if (!mir_machine_six_call_arguments(call, arguments) ||
        arguments[0] != string->dst ||
        arguments[1] != mir.insns[8].dst ||
        arguments[2] != mir.insns[15].dst ||
        arguments[3] != mir.insns[22].dst ||
        arguments[4] != mir.insns[26].dst ||
        arguments[5] != mir.insns[30].dst ||
        (call->memory_flags &
         MIR_CALL_FLAG_VARIADIC) == 0 ||
        (call->memory_flags &
         MIR_CALL_FLAG_FORMAT_RUNTIME) !=
            MIR_CALL_FLAG_FORMAT_HEX)
        return 0;
    function = find_global(call->name);
    if (strcmp(call->name, "printf") ||
        function == NULL || function->is_defined)
        return 0;
    snprintf(plan->call_name, sizeof(plan->call_name), "%s",
             call->base_name[0] != 0
                 ? call->base_name
                 : asm_name_for(sym_asm_name(function)));
    plan->string_id = (int)string->immediate;
    plan->runtime_flags =
        call->memory_flags & MIR_CALL_FLAG_FORMAT_RUNTIME;
    return 1;
}

static int mir_match_variadic_sum(struct MirVariadicSum *plan)
{
    static const int expected_opcodes[32] = {
        MIR_LABEL, MIR_PARAM, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_VA_START, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_PHI, MIR_PHI, MIR_NOP, MIR_NOP,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_VA_ARG,
        MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL,
        MIR_VA_END, MIR_NOP, MIR_RETURN
    };
    const struct MirInsn *count;
    const struct MirInsn *initial_total;
    const struct MirInsn *total_store;
    const struct MirInsn *va_start;
    const struct MirInsn *initial_index;
    const struct MirInsn *index_store;
    const struct MirInsn *total_phi;
    const struct MirInsn *index_phi;
    const struct MirInsn *comparison;
    const struct MirInsn *va_arg;
    const struct MirInsn *sum;
    const struct MirInsn *loop_total_store;
    const struct MirInsn *increment;
    const struct MirInsn *loop_index_store;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir_cfg_block_count() != 4 || mir.count != 32)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    count = &mir.insns[1];
    initial_total = &mir.insns[2];
    total_store = &mir.insns[4];
    va_start = &mir.insns[5];
    initial_index = &mir.insns[6];
    index_store = &mir.insns[8];
    total_phi = &mir.insns[11];
    index_phi = &mir.insns[12];
    comparison = &mir.insns[15];
    va_arg = &mir.insns[18];
    sum = &mir.insns[19];
    loop_total_store = &mir.insns[21];
    increment = &mir.insns[25];
    loop_index_store = &mir.insns[26];
    plan->value_width = type_size(initial_total->type);
    if (type_size(count->type) != 2 ||
        (count->type & TYPE_UNSIGNED) != 0 ||
        type_is_float(count->type) ||
        type_ptr_depth(count->type) != 0 ||
        (plan->value_width != 2 && plan->value_width != 4) ||
        type_is_float(initial_total->type) ||
        initial_total->immediate != 0 ||
        !mir_machine_unobservable_local_store(total_store) ||
        total_store->src1 != initial_total->dst ||
        va_start->secondary_offset < 2 ||
        !mir_machine_named_nonvolatile(va_start) ||
        initial_index->immediate != 0 ||
        type_size(initial_index->type) != 2 ||
        !mir_machine_unobservable_local_store(index_store) ||
        index_store->src1 != initial_index->dst ||
        total_phi->src1 != initial_total->dst ||
        total_phi->phi_pred1 != mir.insns[0].label ||
        total_phi->phi_pred2 != mir.insns[22].label ||
        index_phi->src1 != initial_index->dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[22].label ||
        type_size(total_phi->type) != plan->value_width ||
        type_size(index_phi->type) != 2 ||
        comparison->immediate != '<' ||
        comparison->src1 != index_phi->dst ||
        comparison->src2 != count->dst ||
        type_size(comparison->type) != 2 ||
        mir.insns[16].src1 != comparison->dst ||
        mir.insns[16].label != mir.insns[28].label ||
        type_size(va_arg->type) != plan->value_width ||
        va_arg->secondary_offset != plan->value_width ||
        !mir_machine_named_nonvolatile(va_arg) ||
        strcmp(va_start->name, va_arg->name) ||
        strcmp(va_start->name, mir.insns[29].name) ||
        !mir_machine_named_nonvolatile(&mir.insns[29]) ||
        sum->immediate != '+' ||
        sum->src1 != total_phi->dst ||
        sum->src2 != va_arg->dst ||
        type_size(sum->type) != plan->value_width ||
        !mir_machine_same_location(
            total_store, loop_total_store) ||
        loop_total_store->src1 != sum->dst ||
        total_phi->src2 != sum->dst ||
        increment->immediate != '+' ||
        increment->src1 != index_phi->dst ||
        !mir_machine_constant_equals(increment->src2, 1) ||
        !mir_machine_same_location(
            index_store, loop_index_store) ||
        loop_index_store->src1 != increment->dst ||
        index_phi->src2 != increment->dst ||
        mir.insns[27].label != mir.insns[9].label ||
        mir.insns[31].src1 != total_phi->dst ||
        !mir_machine_parameter_value_offset(
            count->dst, &plan->count_stack_offset))
        return 0;
    plan->first_argument_stack_offset =
        va_start->secondary_offset - 2;
    return 1;
}

static int mir_match_guarded_global_pop(
    struct MirGuardedGlobalPop *plan)
{
    static const int expected_opcodes[15] = {
        MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_CALL, MIR_LABEL, MIR_ADDRESS,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_RETURN
    };
    const struct MirInsn *first_cursor;
    const struct MirInsn *comparison;
    const struct MirInsn *call;
    const struct MirInsn *array;
    const struct MirInsn *second_cursor;
    const struct MirInsn *decrement;
    const struct MirInsn *cursor_store;
    const struct MirInsn *address;
    const struct MirInsn *load;
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir_cfg_block_count() != 2 || mir.count != 15 ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    first_cursor = &mir.insns[1];
    comparison = &mir.insns[3];
    call = &mir.insns[5];
    array = &mir.insns[7];
    second_cursor = &mir.insns[8];
    decrement = &mir.insns[10];
    cursor_store = &mir.insns[11];
    address = &mir.insns[12];
    load = &mir.insns[13];
    if (!mir_machine_named_nonvolatile(first_cursor) ||
        !mir_machine_same_location(
            first_cursor, second_cursor) ||
        !mir_machine_same_location(
            first_cursor, cursor_store) ||
        !mir_scalar_memory_location(
            first_cursor, &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        type_size(memory_type) != 2 ||
        type_ptr_depth(memory_type) != 0 ||
        type_is_float(memory_type) ||
        (memory_type & TYPE_UNSIGNED) != 0 ||
        mir_type_uses_unsigned_comparison(
            comparison->secondary_offset) ||
        comparison->immediate != TOK_LE ||
        comparison->src1 != first_cursor->dst ||
        !mir_machine_constant_equals(comparison->src2, 0) ||
        mir.insns[4].src1 != comparison->dst ||
        mir.insns[4].label != mir.insns[6].label ||
        call->name[0] == '\0' ||
        array->name[0] == '\0' ||
        address->src1 != array->dst ||
        address->src2 != decrement->dst ||
        address->immediate != 2 ||
        address->memory_size != 2 ||
        decrement->immediate != '-' ||
        decrement->src1 != second_cursor->dst ||
        !mir_machine_constant_equals(decrement->src2, 1) ||
        cursor_store->src1 != decrement->dst ||
        load->src1 != address->dst ||
        load->memory_size != 2 ||
        load->bit_width != 0 ||
        (load->memory_flags & (1 | 8)) != 0 ||
        mir.insns[14].src1 != load->dst)
        return 0;
    plan->cursor = find_global(first_cursor->name);
    plan->array = find_global(array->name);
    plan->guard_function = find_global(call->name);
    if (plan->cursor == NULL || plan->cursor->is_volatile ||
        plan->array == NULL || plan->array->is_volatile ||
        plan->guard_function == NULL ||
        !plan->guard_function->is_defined)
        return 0;
    plan->cursor_offset = memory_offset;
    if (!mir_scalar_memory_location(
            array, &memory_type, &memory_storage,
            &plan->array_offset) ||
        memory_storage != SC_GLOBAL)
        return 0;
    return 1;
}

static int mir_match_float_member_scalar_compare(
    struct MirFloatMemberScalarCompare *plan)
{
    const struct MirInsn *parameters[2];
    const struct MirInsn *member = NULL;
    const struct MirInsn *member_load = NULL;
    const struct MirInsn *conversion = NULL;
    const struct MirInsn *comparison = NULL;
    const struct MirInsn *branch = NULL;
    const struct MirInsn *returns[2];
    const struct MirInsn *target = NULL;
    const struct MirInsn *pointer_parameter;
    const struct MirInsn *scalar_parameter;
    const struct MirInsn *true_constant;
    const struct MirInsn *false_constant;
    int parameter_count = 0;
    int label_count = 0;
    int nop_count = 0;
    int constant_count = 0;
    int return_count = 0;
    int instruction;
    int memory_type;
    int memory_storage;
    int memory_offset;
    int left;
    int right;

    memset(plan, 0, sizeof(*plan));
    if (mir_cfg_block_count() != 2 || mir.count != 15 ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_LABEL:
            ++label_count;
            break;
        case MIR_PARAM:
            if (parameter_count >= 2)
                return 0;
            parameters[parameter_count++] = insn;
            break;
        case MIR_NOP:
            ++nop_count;
            break;
        case MIR_MEMBER_ADDRESS:
            if (member != NULL)
                return 0;
            member = insn;
            break;
        case MIR_LOAD_INDIRECT:
            if (member_load != NULL)
                return 0;
            member_load = insn;
            break;
        case MIR_UNARY:
            if (conversion != NULL)
                return 0;
            conversion = insn;
            break;
        case MIR_BINARY:
            if (comparison != NULL)
                return 0;
            comparison = insn;
            break;
        case MIR_BRANCH_FALSE:
            if (branch != NULL)
                return 0;
            branch = insn;
            break;
        case MIR_CONST:
            ++constant_count;
            break;
        case MIR_RETURN:
            if (return_count >= 2)
                return 0;
            returns[return_count++] = insn;
            break;
        default:
            return 0;
        }
    }
    if (parameter_count != 2 || label_count != 2 ||
        nop_count != 2 || constant_count != 2 ||
        return_count != 2 || member == NULL ||
        member_load == NULL || conversion == NULL ||
        comparison == NULL || branch == NULL ||
        comparison > branch || branch > returns[0] ||
        returns[0] > returns[1])
        return 0;
    if (type_ptr_depth(parameters[0]->type) > 0) {
        pointer_parameter = parameters[0];
        scalar_parameter = parameters[1];
    } else if (type_ptr_depth(parameters[1]->type) > 0) {
        pointer_parameter = parameters[1];
        scalar_parameter = parameters[0];
    } else {
        return 0;
    }
    plan->scalar_width = type_size(scalar_parameter->type);
    if (type_size(pointer_parameter->type) != 2 ||
        mir_machine_pointee_is_volatile(pointer_parameter) ||
        (plan->scalar_width != 2 && plan->scalar_width != 4) ||
        type_ptr_depth(scalar_parameter->type) != 0 ||
        type_is_float(scalar_parameter->type) ||
        (scalar_parameter->type & TYPE_UNSIGNED) != 0 ||
        member->src1 != pointer_parameter->dst ||
        member->memory_size != 4 ||
        member_load->src1 != member->dst ||
        member_load->memory_size != 4 ||
        member_load->bit_width != 0 ||
        !type_is_float(member_load->type) ||
        (member_load->memory_flags & (1 | 8)) != 0 ||
        conversion->immediate != 0 ||
        conversion->src1 != scalar_parameter->dst ||
        !type_is_float(conversion->type) ||
        type_size(conversion->type) != 4)
        return 0;
    if (comparison->immediate == '<') {
        left = comparison->src1;
        right = comparison->src2;
    } else if (comparison->immediate == '>') {
        left = comparison->src2;
        right = comparison->src1;
    } else {
        return 0;
    }
    if (!((left == member_load->dst &&
           right == conversion->dst) ||
          (left == conversion->dst &&
           right == member_load->dst)) ||
        type_size(comparison->type) != 2 ||
        branch->src1 != comparison->dst)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_LABEL &&
            mir.insns[instruction].label == branch->label) {
            if (target != NULL)
                return 0;
            target = &mir.insns[instruction];
        }
    true_constant = mir_definition(returns[0]->src1);
    false_constant = mir_definition(returns[1]->src1);
    if (target == NULL || target <= returns[0] ||
        target >= returns[1] ||
        true_constant == NULL ||
        true_constant->opcode != MIR_CONST ||
        true_constant->immediate != 1 ||
        false_constant == NULL ||
        false_constant->opcode != MIR_CONST ||
        false_constant->immediate != 0 ||
        !mir_scalar_memory_location(
            pointer_parameter, &memory_type,
            &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2)
        return 0;
    plan->pointer_stack_offset = memory_offset - 2;
    if (!mir_scalar_memory_location(
            scalar_parameter, &memory_type,
            &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2)
        return 0;
    plan->scalar_stack_offset = memory_offset - 2;
    plan->member_offset = (int)member->immediate;
    plan->member_is_left = left == member_load->dst;
    return 1;
}

static int mir_match_exact_float_mismatch_report(
    struct MirExactFloatMismatchReport *plan)
{
    static const int expected_opcodes[31] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_LOAD, MIR_LOAD, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG,
        MIR_ADDRESS, MIR_NOP, MIR_LOAD_INDIRECT, MIR_ARG,
        MIR_ADDRESS, MIR_NOP, MIR_LOAD_INDIRECT, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_LABEL
    };
    const struct MirInsn *name;
    const struct MirInsn *got;
    const struct MirInsn *want;
    const struct MirInsn *checks_load;
    const struct MirInsn *checks_increment;
    const struct MirInsn *checks_store;
    const struct MirInsn *got_load;
    const struct MirInsn *want_load;
    const struct MirInsn *comparison;
    const struct MirInsn *failures_load;
    const struct MirInsn *failures_increment;
    const struct MirInsn *failures_store;
    const struct MirInsn *string;
    const struct MirInsn *name_load;
    const struct MirInsn *got_address;
    const struct MirInsn *got_bits;
    const struct MirInsn *want_address;
    const struct MirInsn *want_bits;
    const struct MirInsn *call;
    struct Sym *function;
    int arguments[4];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir_cfg_block_count() != 2 || mir.count != 31 ||
        (mir.return_type & 15) != TYPE_VOID)
        return mir_machine_reject(
            "exact-float-mismatch-report", "preflight");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "exact-float-mismatch-report", "opcodes");
    name = &mir.insns[1];
    got = &mir.insns[2];
    want = &mir.insns[3];
    checks_load = &mir.insns[4];
    checks_increment = &mir.insns[6];
    checks_store = &mir.insns[7];
    got_load = &mir.insns[8];
    want_load = &mir.insns[9];
    comparison = &mir.insns[10];
    failures_load = &mir.insns[12];
    failures_increment = &mir.insns[14];
    failures_store = &mir.insns[15];
    string = &mir.insns[16];
    name_load = &mir.insns[18];
    got_address = &mir.insns[20];
    got_bits = &mir.insns[22];
    want_address = &mir.insns[24];
    want_bits = &mir.insns[26];
    call = &mir.insns[28];
    if (type_size(name->type) != 2 ||
        type_ptr_depth(name->type) == 0 ||
        !type_is_float(got->type) ||
        type_size(got->type) != 4 ||
        !type_is_float(want->type) ||
        type_size(want->type) != 4 ||
        !mir_machine_same_location(got, got_load) ||
        !mir_machine_same_location(want, want_load) ||
        comparison->immediate != TOK_NE ||
        comparison->src1 != got_load->dst ||
        comparison->src2 != want_load->dst ||
        type_size(comparison->secondary_offset) != 4 ||
        mir.insns[11].src1 != comparison->dst ||
        mir.insns[11].label != mir.insns[30].label ||
        strcmp(got_address->name, got->name) ||
        got_bits->src1 != got_address->dst ||
        got_bits->memory_size != 4 ||
        got_bits->bit_width != 0 ||
        (got_bits->memory_flags & (1 | 8)) != 0 ||
        strcmp(want_address->name, want->name) ||
        want_bits->src1 != want_address->dst ||
        want_bits->memory_size != 4 ||
        want_bits->bit_width != 0 ||
        (want_bits->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_same_location(name, name_load) ||
        !mir_machine_four_call_arguments(call, arguments) ||
        arguments[0] != string->dst ||
        arguments[1] != name_load->dst ||
        arguments[2] != got_bits->dst ||
        arguments[3] != want_bits->dst)
        return mir_machine_reject(
            "exact-float-mismatch-report", "shape");
    if ((call->memory_flags & MIR_CALL_FLAG_VARIADIC) == 0 ||
        (call->memory_flags & MIR_CALL_FLAG_FORMAT_OCTAL) != 0)
        return mir_machine_reject(
            "exact-float-mismatch-report", "call-flags");
    if (!mir_machine_named_nonvolatile(checks_load) ||
        !mir_machine_same_location(checks_load, checks_store) ||
        !mir_scalar_memory_location(
            checks_load, &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
            type_size(memory_type) != 2 ||
            checks_increment->immediate != '+' ||
            checks_increment->src1 != checks_load->dst ||
            type_size(checks_increment->type) != 2 ||
            !mir_machine_constant_equals(checks_increment->src2, 1) ||
            checks_store->src1 != checks_increment->dst ||
            checks_store->memory_size != 2)
        return mir_machine_reject(
            "exact-float-mismatch-report", "checks-counter");
    plan->checks = find_global(checks_load->name);
    plan->checks_offset = memory_offset;
    if (!mir_machine_named_nonvolatile(failures_load) ||
        !mir_machine_same_location(
            failures_load, failures_store) ||
        !mir_scalar_memory_location(
            failures_load, &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
            type_size(memory_type) != 2 ||
            failures_increment->immediate != '+' ||
            failures_increment->src1 != failures_load->dst ||
            type_size(failures_increment->type) != 2 ||
            !mir_machine_constant_equals(
                failures_increment->src2, 1) ||
            failures_store->src1 != failures_increment->dst ||
            failures_store->memory_size != 2)
        return mir_machine_reject(
            "exact-float-mismatch-report", "failure-counter");
    plan->failures = find_global(failures_load->name);
    plan->failures_offset = memory_offset;
    function = find_global(call->name);
    if (plan->checks == NULL || plan->checks->is_volatile ||
        plan->failures == NULL || plan->failures->is_volatile ||
        strcmp(call->name, "printf") ||
        function == NULL || function->is_defined ||
        !mir_machine_parameter_value_offset(
            name->dst, &plan->name_stack_offset))
        return mir_machine_reject(
            "exact-float-mismatch-report", "symbols");
    if (!mir_scalar_memory_location(
            got, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2)
        return mir_machine_reject(
            "exact-float-mismatch-report", "got-parameter");
    plan->got_stack_offset = memory_offset - 2;
    if (!mir_scalar_memory_location(
            want, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2)
        return mir_machine_reject(
            "exact-float-mismatch-report", "want-parameter");
    plan->want_stack_offset = memory_offset - 2;
    snprintf(plan->call_name, sizeof(plan->call_name), "%s",
             call->base_name[0] != 0
                 ? call->base_name
                 : asm_name_for(sym_asm_name(function)));
    plan->runtime_flags =
        call->memory_flags & MIR_CALL_FLAG_FORMAT_RUNTIME;
    plan->string_id = (int)string->immediate;
    return 1;
}

static int mir_match_float_tolerance_report(
    struct MirFloatToleranceReport *plan)
{
    static const int expected_opcodes[31] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CALL,
        MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG,
        MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_LABEL
    };
    const struct MirInsn *name;
    const struct MirInsn *got;
    const struct MirInsn *want;
    const struct MirInsn *checks_load;
    const struct MirInsn *checks_increment;
    const struct MirInsn *checks_store;
    const struct MirInsn *difference;
    const struct MirInsn *fabs_call;
    const struct MirInsn *epsilon;
    const struct MirInsn *comparison;
    const struct MirInsn *failures_load;
    const struct MirInsn *failures_increment;
    const struct MirInsn *failures_store;
    const struct MirInsn *string;
    const struct MirInsn *name_load;
    const struct MirInsn *report_call;
    struct Sym *fabs_function;
    struct Sym *report_function;
    int fabs_argument;
    int report_arguments[4];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir_cfg_block_count() != 2 || mir.count != 31 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    name = &mir.insns[1];
    got = &mir.insns[2];
    want = &mir.insns[3];
    checks_load = &mir.insns[4];
    checks_increment = &mir.insns[6];
    checks_store = &mir.insns[7];
    difference = &mir.insns[10];
    fabs_call = &mir.insns[12];
    epsilon = &mir.insns[13];
    comparison = &mir.insns[14];
    failures_load = &mir.insns[16];
    failures_increment = &mir.insns[18];
    failures_store = &mir.insns[19];
    string = &mir.insns[20];
    name_load = &mir.insns[22];
    report_call = &mir.insns[28];
    if (type_size(name->type) != 2 ||
        type_ptr_depth(name->type) == 0 ||
        !type_is_float(got->type) ||
        type_size(got->type) != 4 ||
        !type_is_float(want->type) ||
        type_size(want->type) != 4 ||
        difference->immediate != '-' ||
        difference->src1 != got->dst ||
        difference->src2 != want->dst ||
        !type_is_float(difference->type) ||
        !mir_machine_single_call_argument(
            fabs_call, &fabs_argument) ||
        fabs_argument != difference->dst ||
        strcmp(fabs_call->name, "fabsf") ||
        !type_is_float(fabs_call->type) ||
        !type_is_float(epsilon->type) ||
        comparison->immediate != '>' ||
        comparison->src1 != fabs_call->dst ||
        comparison->src2 != epsilon->dst ||
        type_size(comparison->type) != 2 ||
        mir.insns[15].src1 != comparison->dst ||
        mir.insns[15].label != mir.insns[30].label ||
        !mir_machine_same_location(name, name_load) ||
        !mir_machine_four_call_arguments(
            report_call, report_arguments) ||
        report_arguments[0] != string->dst ||
        report_arguments[1] != name_load->dst ||
        report_arguments[2] != got->dst ||
        report_arguments[3] != want->dst ||
        (report_call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) !=
            MIR_CALL_FLAG_VARIADIC)
        return 0;
    fabs_function = find_global(fabs_call->name);
    report_function = find_global(report_call->name);
    if (fabs_function == NULL || fabs_function->is_defined ||
        (fabs_call->memory_flags & MIR_CALL_FLAG_VARIADIC) != 0 ||
        (fabs_call->base_name[0] != 0 &&
         strcmp(fabs_call->base_name,
                asm_name_for(sym_asm_name(fabs_function)))) ||
        strcmp(report_call->name, "printf") ||
        report_function == NULL || report_function->is_defined)
        return 0;
    if (!mir_machine_named_nonvolatile(checks_load) ||
        !mir_machine_same_location(checks_load, checks_store) ||
        !mir_scalar_memory_location(
            checks_load, &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        type_size(memory_type) != 2 ||
        checks_increment->immediate != '+' ||
        checks_increment->src1 != checks_load->dst ||
        type_size(checks_increment->type) != 2 ||
        !mir_machine_constant_equals(checks_increment->src2, 1) ||
        checks_store->src1 != checks_increment->dst ||
        checks_store->memory_size != 2)
        return 0;
    plan->checks = find_global(checks_load->name);
    plan->checks_offset = memory_offset;
    if (!mir_machine_named_nonvolatile(failures_load) ||
        !mir_machine_same_location(
            failures_load, failures_store) ||
        !mir_scalar_memory_location(
            failures_load, &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        type_size(memory_type) != 2 ||
        failures_increment->immediate != '+' ||
        failures_increment->src1 != failures_load->dst ||
        type_size(failures_increment->type) != 2 ||
        !mir_machine_constant_equals(
            failures_increment->src2, 1) ||
        failures_store->src1 != failures_increment->dst ||
        failures_store->memory_size != 2)
        return 0;
    plan->failures = find_global(failures_load->name);
    plan->failures_offset = memory_offset;
    if (plan->checks == NULL || plan->checks->is_volatile ||
        plan->failures == NULL || plan->failures->is_volatile ||
        !mir_machine_parameter_value_offset(
            name->dst, &plan->name_stack_offset))
        return 0;
    if (!mir_scalar_memory_location(
            got, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2)
        return 0;
    plan->got_stack_offset = memory_offset - 2;
    if (!mir_scalar_memory_location(
            want, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2)
        return 0;
    plan->want_stack_offset = memory_offset - 2;
    snprintf(plan->call_name, sizeof(plan->call_name), "%s",
             report_call->base_name[0] != 0
                 ? report_call->base_name
                 : asm_name_for(sym_asm_name(report_function)));
    plan->epsilon_bits =
        (unsigned long)epsilon->immediate & 0xffffffffUL;
    plan->string_id = (int)string->immediate;
    return 1;
}

static int mir_machine_phi_merge(
    int phi_index, int true_value_index, int false_value_index,
    int true_label_index, int false_label_index)
{
    const struct MirInsn *phi = &mir.insns[phi_index];

    return phi->opcode == MIR_PHI &&
           phi->src1 == mir.insns[true_value_index].dst &&
           phi->src2 == mir.insns[false_value_index].dst &&
           phi->phi_pred1 == mir.insns[true_label_index].label &&
           phi->phi_pred2 == mir.insns[false_label_index].label;
}

static int mir_machine_boolean_merge(
    int phi_index, int true_value_index, int false_value_index,
    int true_label_index, int false_label_index)
{
    return mir_machine_phi_merge(
               phi_index, true_value_index, false_value_index,
               true_label_index, false_label_index) &&
           mir_machine_constant_equals(
               mir.insns[true_value_index].dst, 1) &&
           mir_machine_constant_equals(
               mir.insns[false_value_index].dst, 0);
}

static int mir_match_float_tolerance_failure(
    struct MirFloatToleranceFailure *plan)
{
    static const int expected_opcodes[32] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_NOP,
        MIR_BINARY, MIR_STORE, MIR_NOP, MIR_FLOAT_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_UNARY, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_LOAD, MIR_FLOAT_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_NOP,
        MIR_LABEL
    };
    const struct MirInsn *got = &mir.insns[1];
    const struct MirInsn *want = &mir.insns[2];
    const struct MirInsn *name = &mir.insns[3];
    const struct MirInsn *diff_store = &mir.insns[7];
    const struct MirInsn *call = &mir.insns[25];
    const struct MirInsn *failures_load = &mir.insns[26];
    int arguments[2];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 32 || mir_cfg_block_count() != 3 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return mir_machine_reject(
            "float-tolerance-failure", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "float-tolerance-failure", "opcode");
    if (!type_is_float(got->type) || type_size(got->type) != 4 ||
        !mir_scalar_memory_location(
            got, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2)
        return mir_machine_reject(
            "float-tolerance-failure", "got");
    plan->got_stack_offset = memory_offset - 2;
    if (!type_is_float(want->type) || type_size(want->type) != 4 ||
        !mir_scalar_memory_location(
            want, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2)
        return mir_machine_reject(
            "float-tolerance-failure", "want");
    plan->want_stack_offset = memory_offset - 2;
    if (type_ptr_depth(name->type) != 1 ||
        !mir_scalar_memory_location(
            name, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2)
        return mir_machine_reject(
            "float-tolerance-failure", "name");
    plan->name_stack_offset = memory_offset - 2;
    if (mir.insns[6].immediate != '-' ||
        mir.insns[6].src1 != got->dst ||
        mir.insns[6].src2 != want->dst ||
        !mir_machine_unobservable_local_store(diff_store) ||
        diff_store->memory_size != 4 ||
        diff_store->src1 != mir.insns[6].dst ||
        mir.insns[10].immediate != '<' ||
        mir.insns[10].src1 != mir.insns[6].dst ||
        mir.insns[10].src2 != mir.insns[9].dst ||
        mir.insns[9].immediate != 0 ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[11].label != mir.insns[16].label ||
        mir.insns[13].immediate != '-' ||
        mir.insns[13].src1 != mir.insns[6].dst ||
        mir.insns[15].object != diff_store->object ||
        mir.insns[15].src1 != mir.insns[13].dst ||
        mir.insns[17].object != diff_store->object ||
        type_size(mir.insns[17].type) != 4 ||
        mir.insns[19].immediate != '>' ||
        mir.insns[19].src1 != mir.insns[17].dst ||
        mir.insns[19].src2 != mir.insns[18].dst ||
        mir.insns[20].src1 != mir.insns[19].dst ||
        mir.insns[20].label != mir.insns[31].label)
        return mir_machine_reject(
            "float-tolerance-failure", "comparison");
    plan->epsilon_bits =
        (unsigned long)mir.insns[18].immediate & 0xffffffffUL;
    if (!mir_machine_two_call_arguments(call, arguments) ||
        arguments[0] != mir.insns[21].dst ||
        arguments[1] != mir.insns[23].dst ||
        mir.insns[23].object != name->object ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) !=
            MIR_CALL_FLAG_VARIADIC)
        return mir_machine_reject(
            "float-tolerance-failure", "print-call");
    plan->print_function = find_global(call->name);
    plan->string_id = (int)mir.insns[21].immediate;
    if (plan->print_function == NULL ||
        plan->print_function->storage != SC_FUNC ||
        plan->print_function->is_funcptr ||
        plan->print_function->is_noreturn ||
        plan->string_id < 0)
        return mir_machine_reject(
            "float-tolerance-failure", "print-symbol");
    if (!mir_machine_named_nonvolatile(failures_load) ||
        !mir_scalar_memory_location(
            failures_load, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL || type_size(memory_type) != 2 ||
        !mir_machine_constant_equals(mir.insns[27].dst, 1) ||
        mir.insns[28].immediate != '+' ||
        mir.insns[28].src1 != failures_load->dst ||
        mir.insns[28].src2 != mir.insns[27].dst ||
        !mir_machine_same_location(
            failures_load, &mir.insns[29]) ||
        mir.insns[29].src1 != mir.insns[28].dst)
        return mir_machine_reject(
            "float-tolerance-failure", "failure-count");
    plan->failures = find_global(failures_load->name);
    plan->failures_offset = memory_offset;
    if (plan->failures == NULL ||
        plan->failures->is_volatile)
        return mir_machine_reject(
            "float-tolerance-failure", "failure-symbol");
    return 1;
}

static int mir_match_float_byte_report(
    struct MirFloatByteReport *plan)
{
    static const int expected_opcodes[134] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_PARAM,
        MIR_PARAM, MIR_PARAM, MIR_ADDRESS, MIR_NOP, MIR_UNARY,
        MIR_STORE, MIR_LOAD, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_NOP, MIR_UNARY, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_UNARY,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
        MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_NOP, MIR_UNARY, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_NOP, MIR_UNARY, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD,
        MIR_ARG, MIR_LOAD, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_LOAD, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_LOAD,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG,
        MIR_LOAD, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG, MIR_NOP,
        MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_NOP, MIR_LABEL
    };
    static const int hot_pointer_load_index[4] = {
        11, 24, 49, 74
    };
    static const int hot_index_index[4] = {
        13, 26, 51, 76
    };
    static const int hot_byte_index[4] = {
        14, 27, 52, 77
    };
    static const int expected_conversion_index[4] = {
        17, 30, 55, 80
    };
    static const int comparison_index[4] = {
        18, 31, 56, 81
    };
    static const int cold_pointer_load_index[4] = {
        99, 104, 109, 114
    };
    static const int cold_index_index[4] = {
        101, 106, 111, 116
    };
    static const int cold_byte_index[4] = {
        102, 107, 112, 117
    };
    const struct MirInsn *name;
    const struct MirInsn *value;
    const struct MirInsn *expected[4];
    const struct MirInsn *value_address;
    const struct MirInsn *pointer_conversion;
    const struct MirInsn *pointer_store;
    const struct MirInsn *string;
    const struct MirInsn *name_load;
    const struct MirInsn *call;
    const struct MirInsn *failures_load;
    const struct MirInsn *failures_increment;
    const struct MirInsn *failures_store;
    struct Sym *function;
    int arguments[10];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int byte;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir_cfg_block_count() != 23 || mir.count != 134 ||
        (mir.return_type & 15) != TYPE_VOID)
        return mir_machine_reject("float-byte-report", "preflight");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject("float-byte-report", "opcodes");
    name = &mir.insns[1];
    value = &mir.insns[2];
    for (byte = 0; byte < 4; ++byte)
        expected[byte] = &mir.insns[3 + byte];
    value_address = &mir.insns[7];
    pointer_conversion = &mir.insns[9];
    pointer_store = &mir.insns[10];
    if (type_size(name->type) != 2 ||
        type_ptr_depth(name->type) == 0 ||
        !type_is_float(value->type) ||
        type_size(value->type) != 4 ||
        strcmp(value_address->name, value->name) ||
        pointer_conversion->immediate != 0 ||
        pointer_conversion->src1 != value_address->dst ||
        type_ptr_depth(pointer_conversion->type) == 0 ||
        !mir_machine_unobservable_local_store(pointer_store) ||
        pointer_store->src1 != pointer_conversion->dst)
        return mir_machine_reject("float-byte-report", "setup");
    for (byte = 0; byte < 4; ++byte) {
        const struct MirInsn *pointer_load =
            &mir.insns[hot_pointer_load_index[byte]];
        const struct MirInsn *index =
            &mir.insns[hot_index_index[byte]];
        const struct MirInsn *actual =
            &mir.insns[hot_byte_index[byte]];
        const struct MirInsn *actual_conversion =
            &mir.insns[hot_byte_index[byte] + 2];
        const struct MirInsn *expected_conversion =
            &mir.insns[expected_conversion_index[byte]];
        const struct MirInsn *comparison =
            &mir.insns[comparison_index[byte]];
        const struct MirInsn *cold_pointer_load =
            &mir.insns[cold_pointer_load_index[byte]];
        const struct MirInsn *cold_index =
            &mir.insns[cold_index_index[byte]];
        const struct MirInsn *cold_actual =
            &mir.insns[cold_byte_index[byte]];

        if (type_size(expected[byte]->type) != 1 ||
            (expected[byte]->type & TYPE_UNSIGNED) == 0 ||
            !mir_machine_same_location(
                pointer_store, pointer_load) ||
            !mir_machine_constant_equals(index->src2, byte) ||
            index->src1 != pointer_load->dst ||
            index->immediate != 1 ||
            index->memory_size != 1 ||
            actual->src1 != index->dst ||
            actual->memory_size != 1 ||
            type_size(actual->type) != 1 ||
            (actual->type & TYPE_UNSIGNED) == 0 ||
            actual->bit_width != 0 ||
            (actual->memory_flags & (1 | 8)) != 0 ||
            actual_conversion->immediate != 0 ||
            actual_conversion->src1 != actual->dst ||
            type_size(actual_conversion->type) != 2 ||
            (actual_conversion->type & TYPE_UNSIGNED) != 0 ||
            expected_conversion->immediate != 0 ||
            expected_conversion->src1 != expected[byte]->dst ||
            type_size(expected_conversion->type) != 2 ||
            (expected_conversion->type & TYPE_UNSIGNED) != 0 ||
            comparison->immediate != TOK_NE ||
            comparison->src1 !=
                mir.insns[hot_byte_index[byte] + 2].dst ||
            comparison->src2 != expected_conversion->dst ||
            !mir_machine_same_location(
                pointer_store, cold_pointer_load) ||
            !mir_machine_constant_equals(
                cold_index->src2, byte) ||
            cold_index->src1 != cold_pointer_load->dst ||
            cold_index->immediate != 1 ||
            cold_index->memory_size != 1 ||
            cold_actual->src1 != cold_index->dst ||
            cold_actual->memory_size != 1 ||
            type_size(cold_actual->type) != 1 ||
            (cold_actual->type & TYPE_UNSIGNED) == 0 ||
            cold_actual->bit_width != 0 ||
            (cold_actual->memory_flags & (1 | 8)) != 0)
            return mir_machine_reject("float-byte-report", "bytes");
    }
    if (mir.insns[19].src1 != mir.insns[18].dst ||
        mir.insns[19].label != mir.insns[23].label ||
        mir.insns[22].label != mir.insns[42].label ||
        mir.insns[32].src1 != mir.insns[31].dst ||
        mir.insns[32].label != mir.insns[36].label ||
        mir.insns[35].label != mir.insns[38].label)
        return mir_machine_reject("float-byte-report", "control-01-edges");
    if (!mir_machine_boolean_merge(39, 34, 37, 33, 36))
        return mir_machine_reject("float-byte-report", "control-01-phi1");
    if (mir.insns[41].label != mir.insns[42].label ||
        mir.insns[44].src1 != mir.insns[43].dst ||
        mir.insns[44].label != mir.insns[48].label)
        return mir_machine_reject("float-byte-report", "control-01-join");
    if (!mir_machine_phi_merge(43, 21, 39, 20, 40))
        return mir_machine_reject("float-byte-report", "control-01-phi2");
    if (mir.insns[47].label != mir.insns[67].label ||
        mir.insns[57].src1 != mir.insns[56].dst ||
        mir.insns[57].label != mir.insns[61].label ||
        mir.insns[60].label != mir.insns[63].label ||
        !mir_machine_boolean_merge(64, 59, 62, 58, 61) ||
        mir.insns[66].label != mir.insns[67].label ||
        !mir_machine_phi_merge(68, 46, 64, 45, 65) ||
        mir.insns[69].src1 != mir.insns[68].dst ||
        mir.insns[69].label != mir.insns[73].label)
        return mir_machine_reject("float-byte-report", "control-2");
    if (mir.insns[72].label != mir.insns[92].label ||
        mir.insns[82].src1 != mir.insns[81].dst ||
        mir.insns[82].label != mir.insns[86].label ||
        mir.insns[85].label != mir.insns[88].label ||
        !mir_machine_boolean_merge(89, 84, 87, 83, 86) ||
        mir.insns[91].label != mir.insns[92].label ||
        !mir_machine_phi_merge(93, 71, 89, 70, 90) ||
        mir.insns[94].src1 != mir.insns[93].dst ||
        mir.insns[94].label != mir.insns[133].label)
        return mir_machine_reject("float-byte-report", "control-3");
    string = &mir.insns[95];
    name_load = &mir.insns[97];
    call = &mir.insns[127];
    if (!mir_machine_same_location(name, name_load) ||
        !mir_machine_ten_call_arguments(call, arguments) ||
        arguments[0] != string->dst ||
        arguments[1] != name_load->dst)
        return mir_machine_reject("float-byte-report", "call-prefix");
    for (byte = 0; byte < 4; ++byte)
        if (arguments[2 + byte] !=
                mir.insns[cold_byte_index[byte]].dst ||
            arguments[6 + byte] != expected[byte]->dst)
            return mir_machine_reject("float-byte-report", "call-arguments");
    if ((call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) !=
            MIR_CALL_FLAG_VARIADIC)
        return mir_machine_reject("float-byte-report", "call-flags");
    function = find_global(call->name);
    if (strcmp(call->name, "printf") ||
        function == NULL || function->is_defined)
        return mir_machine_reject("float-byte-report", "call-symbol");
    failures_load = &mir.insns[128];
    failures_increment = &mir.insns[130];
    failures_store = &mir.insns[131];
    if (!mir_machine_named_nonvolatile(failures_load) ||
        !mir_machine_same_location(
            failures_load, failures_store) ||
        !mir_scalar_memory_location(
            failures_load, &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        type_size(memory_type) != 2 ||
        failures_increment->immediate != '+' ||
        failures_increment->src1 != failures_load->dst ||
        type_size(failures_increment->type) != 2 ||
        !mir_machine_constant_equals(
            failures_increment->src2, 1) ||
        failures_store->src1 != failures_increment->dst ||
        failures_store->memory_size != 2)
        return mir_machine_reject("float-byte-report", "counter");
    plan->failures = find_global(failures_load->name);
    plan->failures_offset = memory_offset;
    if (plan->failures == NULL || plan->failures->is_volatile ||
        !mir_machine_parameter_value_offset(
            name->dst, &plan->name_stack_offset))
        return mir_machine_reject("float-byte-report", "symbols");
    if (!mir_scalar_memory_location(
            value, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2)
        return mir_machine_reject("float-byte-report", "value-parameter");
    plan->value_stack_offset = memory_offset - 2;
    for (byte = 0; byte < 4; ++byte) {
        if (!mir_machine_parameter_value_offset(
                expected[byte]->dst,
                &plan->expected_stack_offsets[byte]))
            return mir_machine_reject(
                "float-byte-report", "expected-parameters");
    }
    snprintf(plan->call_name, sizeof(plan->call_name), "%s",
             call->base_name[0] != 0
                 ? call->base_name
                 : asm_name_for(sym_asm_name(function)));
    plan->string_id = (int)string->immediate;
    return 1;
}

static int mir_match_relative_tolerance_call(
    struct MirRelativeToleranceCall *plan)
{
    static const int expected_opcodes[31] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM,
        MIR_NOP, MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_NOP, MIR_UNARY, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_LABEL, MIR_PHI,
        MIR_STORE, MIR_LOAD, MIR_ARG, MIR_NOP, MIR_ARG,
        MIR_NOP, MIR_ARG, MIR_FLOAT_CONST, MIR_FLOAT_CONST,
        MIR_NOP, MIR_BINARY, MIR_BINARY, MIR_ARG, MIR_CALL
    };
    const struct MirInsn *name;
    const struct MirInsn *got;
    const struct MirInsn *want;
    const struct MirInsn *zero;
    const struct MirInsn *comparison;
    const struct MirInsn *negation;
    const struct MirInsn *absolute_phi;
    const struct MirInsn *absolute_store;
    const struct MirInsn *name_load;
    const struct MirInsn *scale;
    const struct MirInsn *second_scale;
    const struct MirInsn *product;
    const struct MirInsn *sum;
    const struct MirInsn *call;
    int arguments[4];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir_cfg_block_count() != 5 || mir.count != 31 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    name = &mir.insns[1];
    got = &mir.insns[2];
    want = &mir.insns[3];
    zero = &mir.insns[5];
    comparison = &mir.insns[6];
    negation = &mir.insns[9];
    absolute_phi = &mir.insns[16];
    absolute_store = &mir.insns[17];
    name_load = &mir.insns[18];
    scale = &mir.insns[24];
    second_scale = &mir.insns[25];
    product = &mir.insns[27];
    sum = &mir.insns[28];
    call = &mir.insns[30];
    if (type_size(name->type) != 2 ||
        type_ptr_depth(name->type) == 0 ||
        !type_is_float(got->type) || type_size(got->type) != 4 ||
        !type_is_float(want->type) || type_size(want->type) != 4 ||
        !type_is_float(zero->type) || zero->immediate != 0 ||
        comparison->immediate != '<' ||
        comparison->src1 != want->dst ||
        comparison->src2 != zero->dst ||
        mir.insns[7].src1 != comparison->dst ||
        mir.insns[7].label != mir.insns[12].label ||
        negation->immediate != '-' ||
        negation->src1 != want->dst ||
        !type_is_float(negation->type) ||
        mir.insns[11].label != mir.insns[15].label ||
        absolute_phi->src1 != negation->dst ||
        absolute_phi->src2 != want->dst ||
        absolute_phi->phi_pred1 != mir.insns[10].label ||
        absolute_phi->phi_pred2 != mir.insns[14].label ||
        !type_is_float(absolute_phi->type) ||
        !mir_machine_unobservable_local_store(absolute_store) ||
        absolute_store->src1 != absolute_phi->dst ||
        !mir_machine_same_location(name, name_load) ||
        !type_is_float(scale->type) ||
        !type_is_float(second_scale->type) ||
        scale->immediate != second_scale->immediate ||
        product->immediate != '*' ||
        product->src1 != second_scale->dst ||
        product->src2 != absolute_phi->dst ||
        !type_is_float(product->type) ||
        sum->immediate != '+' ||
        sum->src1 != scale->dst ||
        sum->src2 != product->dst ||
        !type_is_float(sum->type) ||
        !mir_machine_four_call_arguments(call, arguments) ||
        arguments[0] != name_load->dst ||
        arguments[1] != got->dst ||
        arguments[2] != want->dst ||
        arguments[3] != sum->dst)
        return 0;
    plan->function = find_global(call->name);
    if (plan->function == NULL || !plan->function->is_defined ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0 ||
        (call->base_name[0] != 0 &&
         strcmp(call->base_name,
                asm_name_for(
                    sym_asm_name(plan->function)))) ||
        !mir_machine_parameter_value_offset(
            name->dst, &plan->name_stack_offset))
        return 0;
    if (!mir_scalar_memory_location(
            got, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2)
        return 0;
    plan->got_stack_offset = memory_offset - 2;
    if (!mir_scalar_memory_location(
            want, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2)
        return 0;
    plan->want_stack_offset = memory_offset - 2;
    plan->scale_bits =
        (unsigned long)scale->immediate & 0xffffffffUL;
    return 1;
}

static int mir_match_fixed_float_grid_fill(
    struct MirFixedFloatGridFill *plan)
{
    static const int expected_opcodes[50] = {
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL,
        MIR_PHI, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_NOP, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD, MIR_INDEX_ADDRESS, MIR_NOP, MIR_LOAD, MIR_UNARY,
        MIR_BINARY, MIR_CONST, MIR_BINARY, MIR_UNARY,
        MIR_FLOAT_CONST, MIR_BINARY, MIR_STORE_INDIRECT, MIR_LABEL,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL
    };
    const struct MirInsn *initial_outer;
    const struct MirInsn *outer_store;
    const struct MirInsn *outer_phi;
    const struct MirInsn *outer_bound;
    const struct MirInsn *outer_conversion;
    const struct MirInsn *outer_comparison;
    const struct MirInsn *initial_inner;
    const struct MirInsn *inner_store;
    const struct MirInsn *inner_load;
    const struct MirInsn *inner_bound;
    const struct MirInsn *inner_comparison;
    const struct MirInsn *root;
    const struct MirInsn *row_address;
    const struct MirInsn *element_index;
    const struct MirInsn *element_address;
    const struct MirInsn *expression_outer;
    const struct MirInsn *expression_inner;
    const struct MirInsn *first_sum;
    const struct MirInsn *add_constant;
    const struct MirInsn *second_sum;
    const struct MirInsn *conversion;
    const struct MirInsn *divisor;
    const struct MirInsn *division;
    const struct MirInsn *element_store;
    const struct MirInsn *inner_increment;
    const struct MirInsn *inner_loop_store;
    const struct MirInsn *outer_increment;
    const struct MirInsn *outer_loop_store;
    int memory_type;
    int memory_storage;
    int memory_offset;
    long maximum_add_constant;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir_cfg_block_count() != 7 || mir.count != 50 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    initial_outer = &mir.insns[2];
    outer_store = &mir.insns[3];
    outer_phi = &mir.insns[5];
    outer_bound = &mir.insns[7];
    outer_conversion = &mir.insns[8];
    outer_comparison = &mir.insns[9];
    initial_inner = &mir.insns[11];
    inner_store = &mir.insns[13];
    inner_load = &mir.insns[17];
    inner_bound = &mir.insns[18];
    inner_comparison = &mir.insns[19];
    root = &mir.insns[21];
    row_address = &mir.insns[23];
    element_index = &mir.insns[24];
    element_address = &mir.insns[25];
    expression_inner = &mir.insns[27];
    expression_outer = &mir.insns[28];
    first_sum = &mir.insns[29];
    add_constant = &mir.insns[30];
    second_sum = &mir.insns[31];
    conversion = &mir.insns[32];
    divisor = &mir.insns[33];
    division = &mir.insns[34];
    element_store = &mir.insns[35];
    inner_increment = &mir.insns[39];
    inner_loop_store = &mir.insns[40];
    outer_increment = &mir.insns[46];
    outer_loop_store = &mir.insns[47];
    if (initial_outer->immediate != 0 ||
        type_size(initial_outer->type) != 1 ||
        (initial_outer->type & TYPE_UNSIGNED) == 0 ||
        !mir_machine_unobservable_local_store(outer_store) ||
        outer_store->src1 != initial_outer->dst ||
        outer_phi->src1 != initial_outer->dst ||
        outer_phi->src2 != outer_increment->dst ||
        outer_phi->phi_pred1 != mir.insns[0].label ||
        outer_phi->phi_pred2 != mir.insns[43].label ||
        type_size(outer_phi->type) != 1 ||
        outer_conversion->immediate != 0 ||
        outer_conversion->src1 != outer_phi->dst ||
        type_size(outer_conversion->type) != 2 ||
        outer_comparison->immediate != '<' ||
        outer_comparison->src1 != outer_conversion->dst ||
        outer_comparison->src2 != outer_bound->dst ||
        mir.insns[10].src1 != outer_comparison->dst ||
        mir.insns[10].label != mir.insns[49].label ||
        initial_inner->immediate != 0 ||
        type_size(initial_inner->type) != 2 ||
        !mir_machine_unobservable_local_store(inner_store) ||
        inner_store->src1 != initial_inner->dst ||
        !mir_machine_same_location(inner_store, inner_load) ||
        inner_comparison->immediate != '<' ||
        inner_comparison->src1 != inner_load->dst ||
        inner_comparison->src2 != inner_bound->dst ||
        mir.insns[20].src1 != inner_comparison->dst ||
        mir.insns[20].label != mir.insns[42].label)
        return 0;
    plan->outer_bound = (int)outer_bound->immediate;
    plan->inner_bound = (int)inner_bound->immediate;
    if (outer_bound->opcode != MIR_CONST ||
        inner_bound->opcode != MIR_CONST ||
        plan->outer_bound <= 0 || plan->outer_bound > 255 ||
        plan->inner_bound <= 0 || plan->inner_bound > 255)
        return 0;
    plan->root = find_global(root->name);
    if (plan->root == NULL || plan->root->is_volatile ||
        !mir_scalar_memory_location(
            root, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        row_address->src1 != root->dst ||
        row_address->src2 != outer_phi->dst ||
        row_address->immediate !=
            plan->inner_bound * 4L ||
        element_index->opcode != MIR_LOAD ||
        !mir_machine_same_location(inner_store, element_index) ||
        element_address->src1 != row_address->dst ||
        element_address->src2 != element_index->dst ||
        element_address->immediate != 4 ||
        expression_outer->immediate != 0 ||
        expression_outer->src1 != outer_phi->dst ||
        expression_inner->opcode != MIR_LOAD ||
        !mir_machine_same_location(
            inner_store, expression_inner) ||
        first_sum->immediate != '+' ||
        first_sum->src1 != expression_outer->dst ||
        first_sum->src2 != expression_inner->dst ||
        add_constant->opcode != MIR_CONST ||
        second_sum->immediate != '+' ||
        second_sum->src1 != first_sum->dst ||
        second_sum->src2 != add_constant->dst ||
        conversion->immediate != 0 ||
        conversion->src1 != second_sum->dst ||
        !type_is_float(conversion->type) ||
        !type_is_float(divisor->type) ||
        division->immediate != '/' ||
        division->src1 != conversion->dst ||
        division->src2 != divisor->dst ||
        !type_is_float(division->type) ||
        element_store->src1 != element_address->dst ||
        element_store->src2 != division->dst ||
        element_store->memory_size != 4 ||
        element_store->bit_width != 0 ||
        (element_store->memory_flags & (1 | 8)) != 0)
        return 0;
    plan->root_offset = memory_offset;
    maximum_add_constant =
        255L - (plan->outer_bound - 1L) -
        (plan->inner_bound - 1L);
    if (add_constant->immediate < 0 ||
        add_constant->immediate > maximum_add_constant)
        return 0;
    plan->add_constant = (int)add_constant->immediate;
    if (inner_increment->immediate != '+' ||
        !mir_machine_same_location(
            inner_store, &mir.insns[37]) ||
        inner_increment->src1 != mir.insns[37].dst ||
        !mir_machine_constant_equals(inner_increment->src2, 1) ||
        !mir_machine_same_location(
            inner_store, inner_loop_store) ||
        inner_loop_store->src1 != inner_increment->dst ||
        mir.insns[41].label != mir.insns[14].label ||
        outer_increment->immediate != '+' ||
        outer_increment->src1 != outer_phi->dst ||
        !mir_machine_constant_equals(outer_increment->src2, 1) ||
        !mir_machine_same_location(
            outer_store, outer_loop_store) ||
        outer_loop_store->src1 != outer_increment->dst ||
        mir.insns[48].label != mir.insns[4].label)
        return 0;
    plan->divisor_bits =
        (unsigned long)divisor->immediate & 0xffffffffUL;
    return 1;
}

static int mir_match_constant_float_conditional(
    struct MirConstantFloatConditional *plan)
{
    const struct MirInsn *condition;
    const struct MirInsn *branch;
    const struct MirInsn *true_value;
    const struct MirInsn *false_value;
    const struct MirInsn *true_label;
    const struct MirInsn *false_label;
    const struct MirInsn *false_pred;
    const struct MirInsn *jump;
    const struct MirInsn *join;
    const struct MirInsn *phi;
    const struct MirInsn *conversion;
    const struct MirInsn *return_insn;
    const struct MirInsn *integer_constant;
    const struct MirInsn *integer_conversion;
    const struct MirInsn *float_constant;
    int integer_is_true;

    memset(plan, 0, sizeof(*plan));
    if (mir_cfg_block_count() != 5 || mir.count != 15 ||
        type_size(mir.return_type) != 4 ||
        type_is_float(mir.return_type) ||
        mir.insns[0].opcode != MIR_LABEL ||
        mir.insns[1].opcode != MIR_PARAM ||
        mir.insns[2].opcode != MIR_NOP ||
        mir.insns[3].opcode != MIR_BRANCH_FALSE ||
        mir.insns[11].opcode != MIR_LABEL ||
        mir.insns[12].opcode != MIR_PHI ||
        mir.insns[13].opcode != MIR_UNARY ||
        mir.insns[14].opcode != MIR_RETURN)
        return 0;
    condition = &mir.insns[1];
    branch = &mir.insns[3];
    join = &mir.insns[11];
    phi = &mir.insns[12];
    conversion = &mir.insns[13];
    return_insn = &mir.insns[14];
    if (type_size(condition->type) != 2)
        return 0;
    if (mir.insns[4].opcode == MIR_CONST) {
        integer_is_true = 1;
        integer_constant = &mir.insns[4];
        integer_conversion = &mir.insns[5];
        true_value = integer_conversion;
        true_label = &mir.insns[6];
        jump = &mir.insns[7];
        false_label = &mir.insns[8];
        float_constant = &mir.insns[9];
        false_value = float_constant;
        false_pred = &mir.insns[10];
    } else if (mir.insns[4].opcode == MIR_FLOAT_CONST) {
        integer_is_true = 0;
        float_constant = &mir.insns[4];
        true_value = float_constant;
        true_label = &mir.insns[5];
        jump = &mir.insns[6];
        false_label = &mir.insns[7];
        integer_constant = &mir.insns[8];
        integer_conversion = &mir.insns[9];
        false_value = integer_conversion;
        false_pred = &mir.insns[10];
    } else {
        return 0;
    }
    if (true_label->opcode != MIR_LABEL ||
        jump->opcode != MIR_JUMP ||
        false_label->opcode != MIR_LABEL ||
        false_pred->opcode != MIR_LABEL ||
        integer_constant->opcode != MIR_CONST ||
        (type_size(integer_constant->type) != 2 &&
         type_size(integer_constant->type) != 4) ||
        type_is_float(integer_constant->type) ||
        (integer_constant->type & TYPE_UNSIGNED) != 0 ||
        integer_conversion->opcode != MIR_UNARY ||
        integer_conversion->immediate != 0 ||
        integer_conversion->src1 != integer_constant->dst ||
        !type_is_float(integer_conversion->type) ||
        float_constant->opcode != MIR_FLOAT_CONST ||
        !type_is_float(float_constant->type) ||
        branch->src1 != condition->dst ||
        branch->label != false_label->label ||
        jump->label != join->label ||
        phi->src1 != true_value->dst ||
        phi->src2 != false_value->dst ||
        phi->phi_pred1 != true_label->label ||
        phi->phi_pred2 != false_pred->label ||
        !type_is_float(phi->type) ||
        conversion->immediate != 0 ||
        conversion->src1 != phi->dst ||
        type_size(conversion->type) != 4 ||
        type_is_float(conversion->type) ||
        (conversion->type & TYPE_UNSIGNED) != 0 ||
        return_insn->src1 != conversion->dst ||
        !mir_machine_parameter_value_offset(
            condition->dst, &plan->condition_stack_offset))
        return 0;
    plan->true_is_float = !integer_is_true;
    plan->false_is_float = integer_is_true;
    if (integer_is_true) {
        plan->true_integer_width =
            type_size(integer_constant->type);
        plan->true_bits =
            (unsigned long)integer_constant->immediate & 0xffffffffUL;
        plan->false_bits =
            (unsigned long)float_constant->immediate & 0xffffffffUL;
    } else {
        plan->true_bits =
            (unsigned long)float_constant->immediate & 0xffffffffUL;
        plan->false_integer_width =
            type_size(integer_constant->type);
        plan->false_bits =
            (unsigned long)integer_constant->immediate & 0xffffffffUL;
    }
    return 1;
}

static int mir_match_conditional_global_float_load(
    struct MirConditionalGlobalFloatLoad *plan)
{
    const struct MirInsn *condition;
    const struct MirInsn *branch;
    const struct MirInsn *true_root;
    const struct MirInsn *true_index;
    const struct MirInsn *true_member = NULL;
    const struct MirInsn *true_label;
    const struct MirInsn *jump;
    const struct MirInsn *false_label;
    const struct MirInsn *false_root;
    const struct MirInsn *false_index;
    const struct MirInsn *false_member = NULL;
    const struct MirInsn *false_pred;
    const struct MirInsn *join;
    const struct MirInsn *phi;
    const struct MirInsn *pointer_store;
    const struct MirInsn *pointer_load;
    const struct MirInsn *element_address;
    const struct MirInsn *element_load;
    const struct MirInsn *conversion;
    const struct MirInsn *return_insn;
    const struct MirInsn *true_row_constant;
    const struct MirInsn *false_row_constant;
    const struct MirInsn *final_index_constant;
    int has_member;
    int tail;
    long long true_offset;
    long long false_offset;
    int memory_type;
    int memory_storage;
    int memory_offset;

    memset(plan, 0, sizeof(*plan));
    has_member = mir.count == 26;
    if (mir_cfg_block_count() != 5 ||
        (!has_member && mir.count != 24) ||
        type_size(mir.return_type) != 4 ||
        mir.insns[0].opcode != MIR_LABEL ||
        mir.insns[1].opcode != MIR_PARAM ||
        mir.insns[2].opcode != MIR_NOP ||
        mir.insns[3].opcode != MIR_BRANCH_FALSE)
        return 0;
    condition = &mir.insns[1];
    branch = &mir.insns[3];
    true_root = &mir.insns[4];
    true_index = &mir.insns[6];
    if (has_member) {
        true_member = &mir.insns[7];
        true_label = &mir.insns[8];
        jump = &mir.insns[9];
        false_label = &mir.insns[10];
        false_root = &mir.insns[11];
        false_index = &mir.insns[13];
        false_member = &mir.insns[14];
        false_pred = &mir.insns[15];
        join = &mir.insns[16];
        phi = &mir.insns[17];
        tail = 18;
    } else {
        true_label = &mir.insns[7];
        jump = &mir.insns[8];
        false_label = &mir.insns[9];
        false_root = &mir.insns[10];
        false_index = &mir.insns[12];
        false_pred = &mir.insns[13];
        join = &mir.insns[14];
        phi = &mir.insns[15];
        tail = 16;
    }
    pointer_store = &mir.insns[tail + 1];
    pointer_load = &mir.insns[tail + 2];
    element_address = &mir.insns[tail + 4];
    element_load = &mir.insns[tail + 5];
    conversion = &mir.insns[tail + 6];
    return_insn = &mir.insns[tail + 7];
    true_row_constant = mir_definition(true_index->src2);
    false_row_constant = mir_definition(false_index->src2);
    final_index_constant = mir_definition(element_address->src2);
    if (mir.insns[tail].opcode != MIR_NOP ||
        pointer_store->opcode != MIR_STORE ||
        pointer_load->opcode != MIR_LOAD ||
        mir.insns[tail + 3].opcode != MIR_CONST ||
        element_address->opcode != MIR_INDEX_ADDRESS ||
        element_load->opcode != MIR_LOAD_INDIRECT ||
        conversion->opcode != MIR_UNARY ||
        return_insn->opcode != MIR_RETURN ||
        true_root->opcode != MIR_ADDRESS ||
        mir.insns[5].opcode != MIR_CONST ||
        true_index->opcode != MIR_INDEX_ADDRESS ||
        true_label->opcode != MIR_LABEL ||
        jump->opcode != MIR_JUMP ||
        false_label->opcode != MIR_LABEL ||
        false_root->opcode != MIR_ADDRESS ||
        mir.insns[has_member ? 12 : 11].opcode != MIR_CONST ||
        false_index->opcode != MIR_INDEX_ADDRESS ||
        false_pred->opcode != MIR_LABEL ||
        join->opcode != MIR_LABEL ||
        phi->opcode != MIR_PHI)
        return 0;
    if (has_member &&
        (true_member->opcode != MIR_MEMBER_ADDRESS ||
         false_member->opcode != MIR_MEMBER_ADDRESS ||
         true_member->src1 != true_index->dst ||
         false_member->src1 != false_index->dst ||
         true_member->immediate != false_member->immediate))
        return 0;
    if (strcmp(true_root->name, false_root->name) ||
        true_row_constant == NULL ||
        true_row_constant->opcode != MIR_CONST ||
        true_row_constant->immediate < 0 ||
        true_row_constant->immediate > 32767 ||
        false_row_constant == NULL ||
        false_row_constant->opcode != MIR_CONST ||
        false_row_constant->immediate < 0 ||
        false_row_constant->immediate > 32767 ||
        true_index->src1 != true_root->dst ||
        false_index->src1 != false_root->dst ||
        true_index->immediate <= 0 ||
        true_index->immediate != false_index->immediate ||
        branch->src1 != condition->dst ||
        branch->label != false_label->label ||
        jump->label != join->label ||
        phi->src1 !=
            (has_member ? true_member->dst : true_index->dst) ||
        phi->src2 !=
            (has_member ? false_member->dst : false_index->dst) ||
        phi->phi_pred1 != true_label->label ||
        phi->phi_pred2 != false_pred->label ||
        !mir_machine_unobservable_local_store(pointer_store) ||
        pointer_store->src1 != phi->dst ||
        !mir_machine_same_location(pointer_store, pointer_load) ||
        element_address->src1 != pointer_load->dst ||
        final_index_constant == NULL ||
        final_index_constant->opcode != MIR_CONST ||
        final_index_constant->immediate < 0 ||
        final_index_constant->immediate > 32767 ||
        element_address->immediate != 4 ||
        element_load->src1 != element_address->dst ||
        element_load->memory_size != 4 ||
        element_load->bit_width != 0 ||
        !type_is_float(element_load->type) ||
        (element_load->memory_flags & (1 | 8)) != 0 ||
        conversion->immediate != 0 ||
        conversion->src1 != element_load->dst ||
        type_size(conversion->type) != 4 ||
        type_is_float(conversion->type) ||
        (conversion->type & TYPE_UNSIGNED) != 0 ||
        (mir.return_type & TYPE_UNSIGNED) != 0 ||
        return_insn->src1 != conversion->dst ||
        type_size(condition->type) != 2 ||
        !mir_machine_parameter_value_offset(
            condition->dst, &plan->condition_stack_offset))
        return 0;
    plan->root = find_global(true_root->name);
    if (plan->root == NULL || plan->root->is_volatile ||
        !mir_scalar_memory_location(
            true_root, &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL)
        return 0;
    true_offset =
        (long long)memory_offset +
        (long long)true_row_constant->immediate *
            (long long)true_index->immediate +
        (has_member ? true_member->immediate : 0) +
        (long long)final_index_constant->immediate *
            (long long)element_address->immediate;
    false_offset =
        (long long)memory_offset +
        (long long)false_row_constant->immediate *
            (long long)false_index->immediate +
        (has_member ? false_member->immediate : 0) +
        (long long)final_index_constant->immediate *
            (long long)element_address->immediate;
    if (true_offset < -32768 || true_offset > 32767 ||
        false_offset < -32768 || false_offset > 32767)
        return 0;
    plan->true_offset = (int)true_offset;
    plan->false_offset = (int)false_offset;
    return 1;
}

static int mir_match_float_cosine_polynomial(
    struct MirReducedFloatPolynomial *plan)
{
    static const int expected_opcodes[89] = {
        MIR_LABEL, MIR_PARAM, MIR_FLOAT_CONST, MIR_STORE, MIR_NOP, MIR_ARG,
        MIR_FLOAT_CONST, MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_FLOAT_CONST, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_LOAD, MIR_FLOAT_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LOAD, MIR_FLOAT_CONST, MIR_BINARY, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_LOAD, MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_FLOAT_CONST, MIR_LOAD, MIR_BINARY, MIR_NOP,
        MIR_STORE, MIR_FLOAT_CONST, MIR_UNARY, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_FLOAT_CONST, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_FLOAT_CONST, MIR_UNARY,
        MIR_LOAD, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_FLOAT_CONST, MIR_UNARY,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_LABEL, MIR_LABEL, MIR_LOAD,
        MIR_LOAD, MIR_BINARY, MIR_STORE, MIR_LOAD, MIR_FLOAT_CONST, MIR_NOP,
        MIR_FLOAT_CONST, MIR_UNARY, MIR_NOP, MIR_FLOAT_CONST, MIR_NOP,
        MIR_FLOAT_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BINARY, MIR_BINARY, MIR_BINARY,
        MIR_BINARY, MIR_BINARY, MIR_BINARY, MIR_RETURN
    };
    static const int x_accesses[] = {
        10, 19, 21, 26, 30, 32, 38, 41, 49, 56, 59, 67, 68
    };
    const struct MirInsn *x = &mir.insns[1];
    const struct MirInsn *call = &mir.insns[8];
    const struct MirInsn *sign_store = &mir.insns[3];
    const struct MirInsn *x2_store = &mir.insns[70];
    int arguments[2];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 89 || mir_cfg_block_count() != 7 ||
        mir.has_vla || !type_is_float(mir.return_type) ||
        type_size(mir.return_type) != 4)
        return mir_machine_reject(
            "float-cosine-polynomial", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction]) {
            if (getenv("DCC_MIR_MACHINE_REPORT") != NULL)
                fprintf(stderr,
                        "; MIR machine function=%s"
                        " template=float-cosine-polynomial"
                        " opcode-index=%d expected=%d actual=%d\n",
                        mir.name, instruction,
                        expected_opcodes[instruction],
                        mir.insns[instruction].opcode);
            return mir_machine_reject(
                "float-cosine-polynomial", "opcode");
        }
    if (!type_is_float(x->type) || type_size(x->type) != 4 ||
        !mir_scalar_memory_location(
            x, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2 ||
        memory_offset > 125 || x->object < 0)
        return mir_machine_reject(
            "float-cosine-polynomial", "parameter");
    plan->parameter_stack_offset = memory_offset - 2;
    for (instruction = 0;
         instruction < (int)(sizeof(x_accesses) /
                             sizeof(x_accesses[0]));
         ++instruction) {
        const struct MirInsn *access =
            &mir.insns[x_accesses[instruction]];

        if (access->object != x->object ||
            (access->memory_flags & (1 | 8)) != 0)
            return mir_machine_reject(
                "float-cosine-polynomial", "parameter-access");
    }
    if (!mir_machine_unobservable_local_store(sign_store) ||
        sign_store->memory_size != 4 ||
        sign_store->src1 != mir.insns[2].dst ||
        sign_store->object < 0 ||
        mir.insns[45].object != sign_store->object ||
        mir.insns[63].object != sign_store->object ||
        mir.insns[71].object != sign_store->object ||
        (mir.insns[45].memory_flags & (1 | 8)) != 0 ||
        (mir.insns[63].memory_flags & (1 | 8)) != 0 ||
        (mir.insns[71].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_unobservable_local_store(x2_store) ||
        x2_store->memory_size != 4 ||
        x2_store->src1 != mir.insns[69].dst)
        return mir_machine_reject(
            "float-cosine-polynomial", "locals");
    if (!mir_machine_two_call_arguments(call, arguments) ||
        arguments[0] != x->dst ||
        arguments[1] != mir.insns[6].dst ||
        !type_is_float(call->type) ||
        type_size(call->type) != 4 ||
        call->memory_flags != 0)
        return mir_machine_reject(
            "float-cosine-polynomial", "call");
    plan->remainder_function = find_global(call->name);
    if (plan->remainder_function == NULL ||
        plan->remainder_function->storage != SC_FUNC ||
        plan->remainder_function->is_funcptr ||
        plan->remainder_function->is_noreturn)
        return mir_machine_reject(
            "float-cosine-polynomial", "call-symbol");
    plan->one_bits =
        (unsigned long)mir.insns[2].immediate & 0xffffffffUL;
    plan->two_pi_bits =
        (unsigned long)mir.insns[6].immediate & 0xffffffffUL;
    plan->pi_bits =
        (unsigned long)mir.insns[12].immediate & 0xffffffffUL;
    plan->half_pi_bits =
        (unsigned long)mir.insns[33].immediate & 0xffffffffUL;
    if (plan->one_bits !=
            ((unsigned long)mir.insns[42].immediate & 0xffffffffUL) ||
        plan->one_bits !=
            ((unsigned long)mir.insns[60].immediate & 0xffffffffUL) ||
        plan->one_bits !=
            ((unsigned long)mir.insns[72].immediate & 0xffffffffUL) ||
        plan->two_pi_bits !=
            ((unsigned long)mir.insns[16].immediate & 0xffffffffUL) ||
        plan->two_pi_bits !=
            ((unsigned long)mir.insns[27].immediate & 0xffffffffUL) ||
        plan->pi_bits !=
            ((unsigned long)mir.insns[22].immediate & 0xffffffffUL) ||
        plan->pi_bits !=
            ((unsigned long)mir.insns[37].immediate & 0xffffffffUL) ||
        plan->pi_bits !=
            ((unsigned long)mir.insns[54].immediate & 0xffffffffUL) ||
        plan->half_pi_bits !=
            ((unsigned long)mir.insns[50].immediate & 0xffffffffUL))
        return mir_machine_reject(
            "float-cosine-polynomial", "constants");
    if (mir.insns[23].immediate != '-' ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[43].immediate != '-' ||
        mir.insns[43].src1 != mir.insns[42].dst ||
        mir.insns[51].immediate != '-' ||
        mir.insns[51].src1 != mir.insns[50].dst ||
        mir.insns[55].immediate != '-' ||
        mir.insns[55].src1 != mir.insns[54].dst ||
        mir.insns[61].immediate != '-' ||
        mir.insns[61].src1 != mir.insns[60].dst)
        return mir_machine_reject(
            "float-cosine-polynomial", "negations");
    if (mir.insns[10].src1 != call->dst ||
        mir.insns[13].immediate != '>' ||
        mir.insns[13].src1 != call->dst ||
        mir.insns[13].src2 != mir.insns[12].dst ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        mir.insns[14].label != mir.insns[20].label ||
        mir.insns[17].immediate != '-' ||
        mir.insns[17].src1 != call->dst ||
        mir.insns[17].src2 != mir.insns[16].dst ||
        mir.insns[19].src1 != mir.insns[17].dst ||
        mir.insns[21].object != x->object ||
        mir.insns[24].immediate != '<' ||
        mir.insns[24].src1 != mir.insns[21].dst ||
        mir.insns[24].src2 != mir.insns[23].dst ||
        mir.insns[25].src1 != mir.insns[24].dst ||
        mir.insns[25].label != mir.insns[31].label ||
        mir.insns[28].immediate != '+' ||
        mir.insns[28].src1 != mir.insns[26].dst ||
        mir.insns[28].src2 != mir.insns[27].dst ||
        mir.insns[30].src1 != mir.insns[28].dst)
        return mir_machine_reject(
            "float-cosine-polynomial", "reduction");
    if (mir.insns[34].immediate != '>' ||
        mir.insns[34].src1 != mir.insns[32].dst ||
        mir.insns[34].src2 != mir.insns[33].dst ||
        mir.insns[35].src1 != mir.insns[34].dst ||
        mir.insns[35].label != mir.insns[48].label ||
        mir.insns[39].immediate != '-' ||
        mir.insns[39].src1 != mir.insns[37].dst ||
        mir.insns[39].src2 != mir.insns[38].dst ||
        mir.insns[41].src1 != mir.insns[39].dst ||
        mir.insns[45].src1 != mir.insns[43].dst ||
        mir.insns[47].label != mir.insns[66].label ||
        mir.insns[52].immediate != '<' ||
        mir.insns[52].src1 != mir.insns[49].dst ||
        mir.insns[52].src2 != mir.insns[51].dst ||
        mir.insns[53].src1 != mir.insns[52].dst ||
        mir.insns[53].label != mir.insns[65].label ||
        mir.insns[57].immediate != '-' ||
        mir.insns[57].src1 != mir.insns[55].dst ||
        mir.insns[57].src2 != mir.insns[56].dst ||
        mir.insns[59].src1 != mir.insns[57].dst ||
        mir.insns[63].src1 != mir.insns[61].dst)
        return mir_machine_reject(
            "float-cosine-polynomial", "quadrants");
    plan->coefficients[0] =
        ((unsigned long)mir.insns[74].immediate ^
         0x80000000UL) & 0xffffffffUL;
    plan->coefficients[1] =
        (unsigned long)mir.insns[77].immediate & 0xffffffffUL;
    plan->coefficients[2] =
        ((unsigned long)mir.insns[79].immediate ^
         0x80000000UL) & 0xffffffffUL;
    if (mir.insns[75].immediate != '-' ||
        mir.insns[75].src1 != mir.insns[74].dst ||
        mir.insns[80].immediate != '-' ||
        mir.insns[80].src1 != mir.insns[79].dst ||
        mir.insns[69].immediate != '*' ||
        mir.insns[69].src1 != mir.insns[67].dst ||
        mir.insns[69].src2 != mir.insns[68].dst ||
        mir.insns[81].immediate != '*' ||
        mir.insns[81].src1 != mir.insns[69].dst ||
        mir.insns[81].src2 != mir.insns[80].dst ||
        mir.insns[82].immediate != '+' ||
        mir.insns[82].src1 != mir.insns[77].dst ||
        mir.insns[82].src2 != mir.insns[81].dst ||
        mir.insns[83].immediate != '*' ||
        mir.insns[83].src1 != mir.insns[69].dst ||
        mir.insns[83].src2 != mir.insns[82].dst ||
        mir.insns[84].immediate != '+' ||
        mir.insns[84].src1 != mir.insns[75].dst ||
        mir.insns[84].src2 != mir.insns[83].dst ||
        mir.insns[85].immediate != '*' ||
        mir.insns[85].src1 != mir.insns[69].dst ||
        mir.insns[85].src2 != mir.insns[84].dst ||
        mir.insns[86].immediate != '+' ||
        mir.insns[86].src1 != mir.insns[72].dst ||
        mir.insns[86].src2 != mir.insns[85].dst ||
        mir.insns[87].immediate != '*' ||
        mir.insns[87].src1 != mir.insns[71].dst ||
        mir.insns[87].src2 != mir.insns[86].dst ||
        mir.insns[88].src1 != mir.insns[87].dst)
        return mir_machine_reject(
            "float-cosine-polynomial", "polynomial");
    return 1;
}

static int mir_match_float_sine_polynomial(
    struct MirReducedFloatPolynomial *plan)
{
    static const int expected_opcodes[81] = {
        MIR_LABEL, MIR_PARAM, MIR_FLOAT_CONST, MIR_STORE, MIR_NOP, MIR_ARG,
        MIR_FLOAT_CONST, MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_FLOAT_CONST, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_LOAD, MIR_FLOAT_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LOAD, MIR_FLOAT_CONST, MIR_BINARY, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_LOAD, MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_FLOAT_CONST, MIR_LOAD, MIR_BINARY, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_FLOAT_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_FLOAT_CONST,
        MIR_UNARY, MIR_LOAD, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_LABEL, MIR_LABEL, MIR_LOAD, MIR_LOAD, MIR_BINARY, MIR_STORE,
        MIR_LOAD, MIR_LOAD, MIR_NOP, MIR_BINARY, MIR_FLOAT_CONST,
        MIR_UNARY, MIR_NOP, MIR_FLOAT_CONST, MIR_NOP, MIR_FLOAT_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BINARY, MIR_BINARY, MIR_BINARY,
        MIR_BINARY, MIR_BINARY, MIR_RETURN
    };
    static const int x_accesses[] = {
        10, 19, 21, 26, 30, 32, 38, 41, 45, 52, 55, 59, 60, 63, 64
    };
    const struct MirInsn *x = &mir.insns[1];
    const struct MirInsn *call = &mir.insns[8];
    const struct MirInsn *sign_store = &mir.insns[3];
    const struct MirInsn *x2_store = &mir.insns[62];
    int arguments[2];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 81 || mir_cfg_block_count() != 7 ||
        mir.has_vla || !type_is_float(mir.return_type) ||
        type_size(mir.return_type) != 4)
        return mir_machine_reject(
            "float-sine-polynomial", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "float-sine-polynomial", "opcode");
    if (!type_is_float(x->type) || type_size(x->type) != 4 ||
        !mir_scalar_memory_location(
            x, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2 ||
        memory_offset > 125 || x->object < 0)
        return mir_machine_reject(
            "float-sine-polynomial", "parameter");
    plan->parameter_stack_offset = memory_offset - 2;
    plan->sine_form = 1;
    for (instruction = 0;
         instruction < (int)(sizeof(x_accesses) /
                             sizeof(x_accesses[0]));
         ++instruction) {
        const struct MirInsn *access =
            &mir.insns[x_accesses[instruction]];

        if (access->object != x->object ||
            (access->memory_flags & (1 | 8)) != 0)
            return mir_machine_reject(
                "float-sine-polynomial", "parameter-access");
    }
    if (!mir_machine_unobservable_local_store(sign_store) ||
        sign_store->memory_size != 4 ||
        sign_store->src1 != mir.insns[2].dst ||
        !mir_machine_unobservable_local_store(x2_store) ||
        x2_store->memory_size != 4 ||
        x2_store->src1 != mir.insns[61].dst)
        return mir_machine_reject(
            "float-sine-polynomial", "locals");
    if (!mir_machine_two_call_arguments(call, arguments) ||
        arguments[0] != x->dst ||
        arguments[1] != mir.insns[6].dst ||
        !type_is_float(call->type) ||
        type_size(call->type) != 4 ||
        call->memory_flags != 0)
        return mir_machine_reject(
            "float-sine-polynomial", "call");
    plan->remainder_function = find_global(call->name);
    if (plan->remainder_function == NULL ||
        plan->remainder_function->storage != SC_FUNC ||
        plan->remainder_function->is_funcptr ||
        plan->remainder_function->is_noreturn)
        return mir_machine_reject(
            "float-sine-polynomial", "call-symbol");
    plan->one_bits =
        (unsigned long)mir.insns[2].immediate & 0xffffffffUL;
    plan->two_pi_bits =
        (unsigned long)mir.insns[6].immediate & 0xffffffffUL;
    plan->pi_bits =
        (unsigned long)mir.insns[12].immediate & 0xffffffffUL;
    plan->half_pi_bits =
        (unsigned long)mir.insns[33].immediate & 0xffffffffUL;
    if (plan->two_pi_bits !=
            ((unsigned long)mir.insns[16].immediate & 0xffffffffUL) ||
        plan->two_pi_bits !=
            ((unsigned long)mir.insns[27].immediate & 0xffffffffUL) ||
        plan->pi_bits !=
            ((unsigned long)mir.insns[22].immediate & 0xffffffffUL) ||
        plan->pi_bits !=
            ((unsigned long)mir.insns[37].immediate & 0xffffffffUL) ||
        plan->pi_bits !=
            ((unsigned long)mir.insns[50].immediate & 0xffffffffUL) ||
        plan->half_pi_bits !=
            ((unsigned long)mir.insns[46].immediate & 0xffffffffUL))
        return mir_machine_reject(
            "float-sine-polynomial", "constants");
    if (mir.insns[23].immediate != '-' ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[47].immediate != '-' ||
        mir.insns[47].src1 != mir.insns[46].dst ||
        mir.insns[51].immediate != '-' ||
        mir.insns[51].src1 != mir.insns[50].dst)
        return mir_machine_reject(
            "float-sine-polynomial", "negations");
    if (mir.insns[10].src1 != call->dst ||
        mir.insns[13].immediate != '>' ||
        mir.insns[13].src1 != call->dst ||
        mir.insns[13].src2 != mir.insns[12].dst ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        mir.insns[14].label != mir.insns[20].label ||
        mir.insns[17].immediate != '-' ||
        mir.insns[17].src1 != call->dst ||
        mir.insns[17].src2 != mir.insns[16].dst ||
        mir.insns[19].src1 != mir.insns[17].dst ||
        mir.insns[24].immediate != '<' ||
        mir.insns[24].src1 != mir.insns[21].dst ||
        mir.insns[24].src2 != mir.insns[23].dst ||
        mir.insns[25].src1 != mir.insns[24].dst ||
        mir.insns[25].label != mir.insns[31].label ||
        mir.insns[28].immediate != '+' ||
        mir.insns[28].src1 != mir.insns[26].dst ||
        mir.insns[28].src2 != mir.insns[27].dst ||
        mir.insns[30].src1 != mir.insns[28].dst)
        return mir_machine_reject(
            "float-sine-polynomial", "reduction");
    if (mir.insns[34].immediate != '>' ||
        mir.insns[34].src1 != mir.insns[32].dst ||
        mir.insns[34].src2 != mir.insns[33].dst ||
        mir.insns[35].src1 != mir.insns[34].dst ||
        mir.insns[35].label != mir.insns[44].label ||
        mir.insns[39].immediate != '-' ||
        mir.insns[39].src1 != mir.insns[37].dst ||
        mir.insns[39].src2 != mir.insns[38].dst ||
        mir.insns[41].src1 != mir.insns[39].dst ||
        mir.insns[43].label != mir.insns[58].label ||
        mir.insns[48].immediate != '<' ||
        mir.insns[48].src1 != mir.insns[45].dst ||
        mir.insns[48].src2 != mir.insns[47].dst ||
        mir.insns[49].src1 != mir.insns[48].dst ||
        mir.insns[49].label != mir.insns[57].label ||
        mir.insns[53].immediate != '-' ||
        mir.insns[53].src1 != mir.insns[51].dst ||
        mir.insns[53].src2 != mir.insns[52].dst ||
        mir.insns[55].src1 != mir.insns[53].dst)
        return mir_machine_reject(
            "float-sine-polynomial", "quadrants");
    plan->coefficients[0] =
        ((unsigned long)mir.insns[67].immediate ^
         0x80000000UL) & 0xffffffffUL;
    plan->coefficients[1] =
        (unsigned long)mir.insns[70].immediate & 0xffffffffUL;
    plan->coefficients[2] =
        ((unsigned long)mir.insns[72].immediate ^
         0x80000000UL) & 0xffffffffUL;
    if (mir.insns[68].immediate != '-' ||
        mir.insns[68].src1 != mir.insns[67].dst ||
        mir.insns[73].immediate != '-' ||
        mir.insns[73].src1 != mir.insns[72].dst ||
        mir.insns[61].immediate != '*' ||
        mir.insns[61].src1 != mir.insns[59].dst ||
        mir.insns[61].src2 != mir.insns[60].dst ||
        mir.insns[66].immediate != '*' ||
        mir.insns[66].src1 != mir.insns[64].dst ||
        mir.insns[66].src2 != mir.insns[61].dst ||
        mir.insns[74].immediate != '*' ||
        mir.insns[74].src1 != mir.insns[61].dst ||
        mir.insns[74].src2 != mir.insns[73].dst ||
        mir.insns[75].immediate != '+' ||
        mir.insns[75].src1 != mir.insns[70].dst ||
        mir.insns[75].src2 != mir.insns[74].dst ||
        mir.insns[76].immediate != '*' ||
        mir.insns[76].src1 != mir.insns[61].dst ||
        mir.insns[76].src2 != mir.insns[75].dst ||
        mir.insns[77].immediate != '+' ||
        mir.insns[77].src1 != mir.insns[68].dst ||
        mir.insns[77].src2 != mir.insns[76].dst ||
        mir.insns[78].immediate != '*' ||
        mir.insns[78].src1 != mir.insns[66].dst ||
        mir.insns[78].src2 != mir.insns[77].dst ||
        mir.insns[79].immediate != '+' ||
        mir.insns[79].src1 != mir.insns[63].dst ||
        mir.insns[79].src2 != mir.insns[78].dst ||
        mir.insns[80].src1 != mir.insns[79].dst)
        return mir_machine_reject(
            "float-sine-polynomial", "polynomial");
    return 1;
}

static int mir_match_float_tangent_rational(
    struct MirFloatTangentRational *plan)
{
    static const int expected_opcodes[114] = {
        MIR_LABEL, MIR_PARAM, MIR_CONST, MIR_STORE, MIR_NOP, MIR_ARG,
        MIR_FLOAT_CONST, MIR_ARG, MIR_CALL, MIR_STORE, MIR_NOP,
        MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_NOP,
        MIR_FLOAT_CONST, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_FLOAT_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_FLOAT_CONST, MIR_BINARY, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_LABEL, MIR_LABEL, MIR_LOAD,
        MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_FLOAT_CONST, MIR_LOAD, MIR_BINARY, MIR_NOP, MIR_STORE,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_NOP, MIR_JUMP, MIR_LABEL,
        MIR_LOAD, MIR_FLOAT_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_FLOAT_CONST, MIR_UNARY, MIR_LOAD, MIR_BINARY, MIR_NOP,
        MIR_STORE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_NOP, MIR_LABEL,
        MIR_LABEL, MIR_LOAD, MIR_LOAD, MIR_BINARY, MIR_STORE, MIR_LOAD,
        MIR_FLOAT_CONST, MIR_NOP, MIR_BINARY, MIR_BINARY, MIR_STORE,
        MIR_FLOAT_CONST, MIR_FLOAT_CONST, MIR_NOP, MIR_BINARY, MIR_BINARY,
        MIR_STORE, MIR_NOP, MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_FLOAT_CONST, MIR_RETURN, MIR_LABEL, MIR_NOP, MIR_NOP,
        MIR_BINARY, MIR_STORE, MIR_LOAD, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_FLOAT_CONST,
        MIR_RETURN, MIR_LABEL, MIR_FLOAT_CONST, MIR_NOP, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_LABEL, MIR_PHI, MIR_RETURN
    };
    static const int reduced_accesses[] = {
        9, 19, 23, 28, 32, 36, 42, 45, 52, 59, 62, 69, 70, 73
    };
    const struct MirInsn *x = &mir.insns[1];
    const struct MirInsn *call = &mir.insns[8];
    const struct MirInsn *invert_store = &mir.insns[3];
    const struct MirInsn *reduced_store = &mir.insns[9];
    int arguments[2];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 114 || mir_cfg_block_count() != 12 ||
        mir.has_vla || !type_is_float(mir.return_type) ||
        type_size(mir.return_type) != 4)
        return mir_machine_reject(
            "float-tangent-rational", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "float-tangent-rational", "opcode");
    if (!type_is_float(x->type) || type_size(x->type) != 4 ||
        !mir_scalar_memory_location(
            x, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2 ||
        memory_offset > 125)
        return mir_machine_reject(
            "float-tangent-rational", "parameter");
    plan->parameter_stack_offset = memory_offset - 2;
    if (!mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_unobservable_local_store(invert_store) ||
        invert_store->memory_size != 2 ||
        invert_store->src1 != mir.insns[2].dst ||
        !mir_machine_unobservable_local_store(reduced_store) ||
        reduced_store->memory_size != 4 ||
        reduced_store->src1 != call->dst ||
        reduced_store->object < 0)
        return mir_machine_reject(
            "float-tangent-rational", "initializers");
    for (instruction = 0;
         instruction < (int)(sizeof(reduced_accesses) /
                             sizeof(reduced_accesses[0]));
         ++instruction) {
        const struct MirInsn *access =
            &mir.insns[reduced_accesses[instruction]];

        if (access->object != reduced_store->object ||
            (access->memory_flags & (1 | 8)) != 0)
            return mir_machine_reject(
                "float-tangent-rational", "reduced-access");
    }
    if (!mir_machine_two_call_arguments(call, arguments) ||
        arguments[0] != x->dst ||
        arguments[1] != mir.insns[6].dst ||
        !type_is_float(call->type) ||
        type_size(call->type) != 4 ||
        call->memory_flags != 0)
        return mir_machine_reject(
            "float-tangent-rational", "call");
    plan->remainder_function = find_global(call->name);
    if (plan->remainder_function == NULL ||
        plan->remainder_function->storage != SC_FUNC ||
        plan->remainder_function->is_funcptr ||
        plan->remainder_function->is_noreturn)
        return mir_machine_reject(
            "float-tangent-rational", "call-symbol");
    plan->pi_bits =
        (unsigned long)mir.insns[6].immediate & 0xffffffffUL;
    plan->half_pi_bits =
        (unsigned long)mir.insns[11].immediate & 0xffffffffUL;
    plan->quarter_pi_bits =
        (unsigned long)mir.insns[37].immediate & 0xffffffffUL;
    plan->fifteen_bits =
        (unsigned long)mir.insns[74].immediate & 0xffffffffUL;
    plan->six_bits =
        (unsigned long)mir.insns[80].immediate & 0xffffffffUL;
    plan->one_bits =
        (unsigned long)mir.insns[105].immediate & 0xffffffffUL;
    if (plan->pi_bits !=
            ((unsigned long)mir.insns[16].immediate & 0xffffffffUL) ||
        plan->pi_bits !=
            ((unsigned long)mir.insns[29].immediate & 0xffffffffUL) ||
        plan->half_pi_bits !=
            ((unsigned long)mir.insns[24].immediate & 0xffffffffUL) ||
        plan->half_pi_bits !=
            ((unsigned long)mir.insns[41].immediate & 0xffffffffUL) ||
        plan->half_pi_bits !=
            ((unsigned long)mir.insns[57].immediate & 0xffffffffUL) ||
        plan->quarter_pi_bits !=
            ((unsigned long)mir.insns[53].immediate & 0xffffffffUL) ||
        plan->fifteen_bits !=
            ((unsigned long)mir.insns[79].immediate & 0xffffffffUL) ||
        mir.insns[86].immediate != 0 ||
        mir.insns[89].immediate != 0 ||
        mir.insns[99].immediate != 0 ||
        mir.insns[102].immediate != 0)
        return mir_machine_reject(
            "float-tangent-rational", "constants");
    if (mir.insns[25].immediate != '-' ||
        mir.insns[25].src1 != mir.insns[24].dst ||
        mir.insns[54].immediate != '-' ||
        mir.insns[54].src1 != mir.insns[53].dst ||
        mir.insns[58].immediate != '-' ||
        mir.insns[58].src1 != mir.insns[57].dst)
        return mir_machine_reject(
            "float-tangent-rational", "negations");
    if (mir.insns[12].immediate != '>' ||
        mir.insns[12].src1 != call->dst ||
        mir.insns[12].src2 != mir.insns[11].dst ||
        mir.insns[13].src1 != mir.insns[12].dst ||
        mir.insns[13].label != mir.insns[22].label ||
        mir.insns[17].immediate != '-' ||
        mir.insns[17].src1 != call->dst ||
        mir.insns[17].src2 != mir.insns[16].dst ||
        mir.insns[19].src1 != mir.insns[17].dst ||
        mir.insns[21].label != mir.insns[35].label ||
        mir.insns[26].immediate != '<' ||
        mir.insns[26].src1 != call->dst ||
        mir.insns[26].src2 != mir.insns[25].dst ||
        mir.insns[27].src1 != mir.insns[26].dst ||
        mir.insns[27].label != mir.insns[34].label ||
        mir.insns[30].immediate != '+' ||
        mir.insns[30].src1 != call->dst ||
        mir.insns[30].src2 != mir.insns[29].dst ||
        mir.insns[32].src1 != mir.insns[30].dst)
        return mir_machine_reject(
            "float-tangent-rational", "period-reduction");
    if (mir.insns[38].immediate != '>' ||
        mir.insns[38].src1 != mir.insns[36].dst ||
        mir.insns[38].src2 != mir.insns[37].dst ||
        mir.insns[39].src1 != mir.insns[38].dst ||
        mir.insns[39].label != mir.insns[51].label ||
        mir.insns[43].immediate != '-' ||
        mir.insns[43].src1 != mir.insns[41].dst ||
        mir.insns[43].src2 != mir.insns[42].dst ||
        mir.insns[45].src1 != mir.insns[43].dst ||
        !mir_machine_constant_equals(mir.insns[46].dst, 1) ||
        mir.insns[48].object != invert_store->object ||
        mir.insns[48].src1 != mir.insns[46].dst ||
        mir.insns[50].label != mir.insns[68].label ||
        mir.insns[55].immediate != '<' ||
        mir.insns[55].src1 != mir.insns[52].dst ||
        mir.insns[55].src2 != mir.insns[54].dst ||
        mir.insns[56].src1 != mir.insns[55].dst ||
        mir.insns[56].label != mir.insns[67].label ||
        mir.insns[60].immediate != '-' ||
        mir.insns[60].src1 != mir.insns[58].dst ||
        mir.insns[60].src2 != mir.insns[59].dst ||
        mir.insns[62].src1 != mir.insns[60].dst ||
        !mir_machine_constant_equals(mir.insns[63].dst, 1) ||
        mir.insns[65].object != invert_store->object ||
        mir.insns[65].src1 != mir.insns[63].dst)
        return mir_machine_reject(
            "float-tangent-rational", "quadrants");
    if (mir.insns[71].immediate != '*' ||
        mir.insns[71].src1 != mir.insns[69].dst ||
        mir.insns[71].src2 != mir.insns[70].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[72]) ||
        mir.insns[72].src1 != mir.insns[71].dst ||
        mir.insns[76].immediate != '-' ||
        mir.insns[76].src1 != mir.insns[74].dst ||
        mir.insns[76].src2 != mir.insns[71].dst ||
        mir.insns[77].immediate != '*' ||
        mir.insns[77].src1 != mir.insns[73].dst ||
        mir.insns[77].src2 != mir.insns[76].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[78]) ||
        mir.insns[78].src1 != mir.insns[77].dst ||
        mir.insns[82].immediate != '*' ||
        mir.insns[82].src1 != mir.insns[80].dst ||
        mir.insns[82].src2 != mir.insns[71].dst ||
        mir.insns[83].immediate != '-' ||
        mir.insns[83].src1 != mir.insns[79].dst ||
        mir.insns[83].src2 != mir.insns[82].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[84]) ||
        mir.insns[84].src1 != mir.insns[83].dst)
        return mir_machine_reject(
            "float-tangent-rational", "rational");
    if (mir.insns[87].immediate != TOK_EQ ||
        mir.insns[87].src1 != mir.insns[83].dst ||
        mir.insns[87].src2 != mir.insns[86].dst ||
        mir.insns[88].src1 != mir.insns[87].dst ||
        mir.insns[88].label != mir.insns[91].label ||
        mir.insns[90].src1 != mir.insns[89].dst ||
        mir.insns[94].immediate != '/' ||
        mir.insns[94].src1 != mir.insns[77].dst ||
        mir.insns[94].src2 != mir.insns[83].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[95]) ||
        mir.insns[95].src1 != mir.insns[94].dst ||
        mir.insns[96].object != invert_store->object ||
        mir.insns[97].src1 != mir.insns[96].dst ||
        mir.insns[97].label != mir.insns[111].label ||
        mir.insns[100].immediate != TOK_EQ ||
        mir.insns[100].src1 != mir.insns[94].dst ||
        mir.insns[100].src2 != mir.insns[99].dst ||
        mir.insns[101].src1 != mir.insns[100].dst ||
        mir.insns[101].label != mir.insns[104].label ||
        mir.insns[103].src1 != mir.insns[102].dst ||
        mir.insns[107].immediate != '/' ||
        mir.insns[107].src1 != mir.insns[105].dst ||
        mir.insns[107].src2 != mir.insns[94].dst ||
        mir.insns[109].src1 != mir.insns[107].dst ||
        mir.insns[112].src1 != mir.insns[94].dst ||
        mir.insns[112].src2 != mir.insns[107].dst ||
        mir.insns[112].phi_pred1 != mir.insns[91].label ||
        mir.insns[112].phi_pred2 != mir.insns[104].label ||
        mir.insns[113].src1 != mir.insns[112].dst)
        return mir_machine_reject(
            "float-tangent-rational", "result");
    return 1;
}

static int mir_match_recursive_wide_product(
    struct MirRecursiveWideProduct *plan)
{
    static const int expected_opcodes[20] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_RETURN,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_ARG, MIR_CALL, MIR_BINARY, MIR_RETURN
    };
    const struct MirInsn *parameter;
    const struct MirInsn *comparison;
    const struct MirInsn *base_value;
    const struct MirInsn *decrement;
    const struct MirInsn *call;
    const struct MirInsn *product;
    int call_argument;
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir_cfg_block_count() != 2 || mir.count != 20 ||
        type_size(mir.return_type) != 4 ||
        type_is_float(mir.return_type))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    parameter = &mir.insns[1];
    comparison = &mir.insns[5];
    base_value = &mir.insns[8];
    decrement = &mir.insns[15];
    call = &mir.insns[17];
    product = &mir.insns[18];
    if (type_size(parameter->type) != 4 ||
        type_is_float(parameter->type) ||
        type_ptr_depth(parameter->type) != 0 ||
        (parameter->type & TYPE_UNSIGNED) != 0 ||
        comparison->immediate != TOK_EQ ||
        !mir_machine_constant_equals(comparison->src1, 0) ||
        comparison->src2 != parameter->dst ||
        mir.insns[6].src1 != comparison->dst ||
        mir.insns[6].label != mir.insns[10].label ||
        (base_value->immediate != 0 &&
         base_value->immediate != 1) ||
        type_size(base_value->type) != 4 ||
        mir.insns[9].src1 != base_value->dst ||
        decrement->immediate != '-' ||
        decrement->src1 != parameter->dst ||
        !mir_machine_constant_equals(decrement->src2, 1) ||
        !mir_machine_single_call_argument(
            call, &call_argument) ||
        call_argument != decrement->dst ||
        (product->immediate != '*' &&
         product->immediate != '+') ||
        product->src1 != parameter->dst ||
        product->src2 != call->dst ||
        type_size(product->type) != 4 ||
        mir.insns[19].src1 != product->dst)
        return 0;
    plan->function = find_global(call->name);
    if (plan->function == NULL || !plan->function->is_defined ||
        strcmp(call->name, mir.name) ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0 ||
        (call->base_name[0] != 0 &&
         strcmp(call->base_name,
                asm_name_for(
                    sym_asm_name(plan->function)))) ||
        !mir_scalar_memory_location(
            parameter, &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2)
        return 0;
    plan->parameter_stack_offset = memory_offset - 2;
    plan->operation = (int)product->immediate;
    plan->base_result = (int)base_value->immediate;
    return 1;
}

static int mir_match_recursive_wide_tree_sum(
    struct MirRecursiveWideTreeSum *plan)
{
    static const int expected_opcodes[26] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_RETURN, MIR_LABEL,
        MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL,
        MIR_BINARY, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_ARG, MIR_CALL, MIR_BINARY, MIR_RETURN
    };
    const struct MirInsn *parameter = &mir.insns[1];
    const struct MirInsn *value_member = &mir.insns[11];
    const struct MirInsn *left_member = &mir.insns[14];
    const struct MirInsn *right_member = &mir.insns[20];
    const struct MirInsn *left_call = &mir.insns[17];
    const struct MirInsn *right_call = &mir.insns[23];
    int left_argument;
    int right_argument;
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 26 || mir_cfg_block_count() != 2 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        type_size(mir.return_type) != 4 ||
        type_is_float(mir.return_type))
        return mir_machine_reject(
            "recursive-wide-tree-sum", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "recursive-wide-tree-sum", "opcode");
    if (type_ptr_depth(parameter->type) != 1 ||
        !mir_scalar_memory_location(
            parameter, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2 ||
        mir.insns[4].immediate != TOK_EQ ||
        mir.insns[4].src1 != parameter->dst ||
        !mir_machine_constant_equals(mir.insns[4].src2, 0) ||
        mir.insns[5].src1 != mir.insns[4].dst ||
        mir.insns[5].label != mir.insns[9].label ||
        !mir_machine_constant_equals(mir.insns[7].dst, 0) ||
        type_size(mir.insns[7].type) != 4 ||
        mir.insns[8].src1 != mir.insns[7].dst)
        return mir_machine_reject(
            "recursive-wide-tree-sum", "base-case");
    plan->parameter_stack_offset = memory_offset - 2;
    if (value_member->src1 != parameter->dst ||
        value_member->memory_size != 4 ||
        left_member->src1 != parameter->dst ||
        left_member->memory_size != 2 ||
        right_member->src1 != parameter->dst ||
        right_member->memory_size != 2 ||
        mir.insns[12].src1 != value_member->dst ||
        mir.insns[12].memory_size != 4 ||
        (mir.insns[12].memory_flags & (1 | 8)) != 0 ||
        mir.insns[15].src1 != left_member->dst ||
        mir.insns[15].memory_size != 2 ||
        (mir.insns[15].memory_flags & (1 | 8)) != 0 ||
        mir.insns[21].src1 != right_member->dst ||
        mir.insns[21].memory_size != 2 ||
        (mir.insns[21].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject(
            "recursive-wide-tree-sum", "members");
    plan->value_offset = (int)value_member->immediate;
    plan->value_width = 4;
    plan->value_first = 1;
    plan->left_offset = (int)left_member->immediate;
    plan->right_offset = (int)right_member->immediate;
    if (!mir_machine_single_call_argument(
            left_call, &left_argument) ||
        !mir_machine_single_call_argument(
            right_call, &right_argument) ||
        left_argument != mir.insns[15].dst ||
        right_argument != mir.insns[21].dst ||
        strcmp(left_call->name, mir.name) ||
        strcmp(right_call->name, mir.name) ||
        left_call->type != mir.return_type ||
        right_call->type != mir.return_type ||
        (left_call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0 ||
        (right_call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0)
        return mir_machine_reject(
            "recursive-wide-tree-sum", "calls");
    plan->function = find_global(left_call->name);
    if (plan->function == NULL ||
        !plan->function->is_defined ||
        plan->function != find_global(right_call->name) ||
        plan->function->is_funcptr ||
        plan->function->is_noreturn)
        return mir_machine_reject(
            "recursive-wide-tree-sum", "call-symbol");
    if (mir.insns[18].immediate != '+' ||
        mir.insns[18].src1 != mir.insns[12].dst ||
        mir.insns[18].src2 != left_call->dst ||
        type_size(mir.insns[18].type) != 4 ||
        mir.insns[24].immediate != '+' ||
        mir.insns[24].src1 != mir.insns[18].dst ||
        mir.insns[24].src2 != right_call->dst ||
        type_size(mir.insns[24].type) != 4 ||
        mir.insns[25].src1 != mir.insns[24].dst)
        return mir_machine_reject(
            "recursive-wide-tree-sum", "sum");
    return 1;
}

static int mir_match_recursive_word_member_tree_sum(
    struct MirRecursiveWideTreeSum *plan)
{
    static const int expected_opcodes[27] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_RETURN, MIR_LABEL,
        MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY,
        MIR_BINARY, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG,
        MIR_CALL, MIR_BINARY, MIR_RETURN
    };
    const struct MirInsn *parameter = &mir.insns[1];
    const struct MirInsn *left_member = &mir.insns[11];
    const struct MirInsn *value_member = &mir.insns[16];
    const struct MirInsn *right_member = &mir.insns[21];
    const struct MirInsn *left_call = &mir.insns[14];
    const struct MirInsn *right_call = &mir.insns[24];
    int left_argument;
    int right_argument;
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 27 || mir_cfg_block_count() != 2 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        type_size(mir.return_type) != 4 ||
        type_is_float(mir.return_type))
        return mir_machine_reject(
            "recursive-word-member-tree-sum", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "recursive-word-member-tree-sum", "opcode");
    if (type_ptr_depth(parameter->type) != 1 ||
        !mir_scalar_memory_location(
            parameter, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2 ||
        mir.insns[4].immediate != TOK_EQ ||
        mir.insns[4].src1 != parameter->dst ||
        !mir_machine_constant_equals(mir.insns[4].src2, 0) ||
        mir.insns[5].src1 != mir.insns[4].dst ||
        mir.insns[5].label != mir.insns[9].label ||
        !mir_machine_constant_equals(mir.insns[7].dst, 0) ||
        type_size(mir.insns[7].type) != 4 ||
        mir.insns[8].src1 != mir.insns[7].dst)
        return mir_machine_reject(
            "recursive-word-member-tree-sum", "base-case");
    plan->parameter_stack_offset = memory_offset - 2;
    if (left_member->src1 != parameter->dst ||
        left_member->memory_size != 2 ||
        value_member->src1 != parameter->dst ||
        value_member->memory_size != 2 ||
        right_member->src1 != parameter->dst ||
        right_member->memory_size != 2 ||
        mir.insns[12].src1 != left_member->dst ||
        mir.insns[12].memory_size != 2 ||
        (mir.insns[12].memory_flags & (1 | 8)) != 0 ||
        mir.insns[17].src1 != value_member->dst ||
        mir.insns[17].memory_size != 2 ||
        (mir.insns[17].memory_flags & (1 | 8)) != 0 ||
        mir.insns[18].immediate != 0 ||
        mir.insns[18].src1 != mir.insns[17].dst ||
        type_size(mir.insns[18].type) != 4 ||
        mir.insns[22].src1 != right_member->dst ||
        mir.insns[22].memory_size != 2 ||
        (mir.insns[22].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject(
            "recursive-word-member-tree-sum", "members");
    plan->left_offset = (int)left_member->immediate;
    plan->value_offset = (int)value_member->immediate;
    plan->right_offset = (int)right_member->immediate;
    plan->value_width = 2;
    plan->value_is_unsigned =
        (mir.insns[17].type & TYPE_UNSIGNED) != 0;
    if (!mir_machine_single_call_argument(
            left_call, &left_argument) ||
        !mir_machine_single_call_argument(
            right_call, &right_argument) ||
        left_argument != mir.insns[12].dst ||
        right_argument != mir.insns[22].dst ||
        strcmp(left_call->name, mir.name) ||
        strcmp(right_call->name, mir.name) ||
        left_call->type != mir.return_type ||
        right_call->type != mir.return_type)
        return mir_machine_reject(
            "recursive-word-member-tree-sum", "calls");
    plan->function = find_global(left_call->name);
    if (plan->function == NULL ||
        !plan->function->is_defined ||
        plan->function != find_global(right_call->name) ||
        plan->function->is_funcptr ||
        plan->function->is_noreturn)
        return mir_machine_reject(
            "recursive-word-member-tree-sum", "call-symbol");
    if (mir.insns[19].immediate != '+' ||
        mir.insns[19].src1 != left_call->dst ||
        mir.insns[19].src2 != mir.insns[18].dst ||
        type_size(mir.insns[19].type) != 4 ||
        mir.insns[25].immediate != '+' ||
        mir.insns[25].src1 != mir.insns[19].dst ||
        mir.insns[25].src2 != right_call->dst ||
        type_size(mir.insns[25].type) != 4 ||
        mir.insns[26].src1 != mir.insns[25].dst)
        return mir_machine_reject(
            "recursive-word-member-tree-sum", "sum");
    return 1;
}

static int mir_machine_global_byte_member(
    int root_index, int member_index,
    struct Sym **symbol_out, int *offset_out)
{
    const struct MirInsn *root = &mir.insns[root_index];
    const struct MirInsn *member = &mir.insns[member_index];
    struct Sym *symbol;
    int memory_type;
    int memory_storage;
    int memory_offset;

    if (root->opcode != MIR_ADDRESS ||
        member->opcode != MIR_MEMBER_ADDRESS ||
        member->src1 != root->dst ||
        member->memory_size != 1 ||
        !mir_scalar_memory_location(
            root, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL)
        return 0;
    symbol = find_global(root->name);
    if (symbol == NULL || symbol->is_volatile)
        return 0;
    *symbol_out = symbol;
    *offset_out = memory_offset + (int)member->immediate;
    return 1;
}

static int mir_machine_global_byte_access(
    int root_index, int member_index, int access_index,
    int opcode, struct Sym **symbol_out, int *offset_out)
{
    const struct MirInsn *member = &mir.insns[member_index];
    const struct MirInsn *access = &mir.insns[access_index];

    return mir_machine_global_byte_member(
               root_index, member_index, symbol_out, offset_out) &&
           member->bit_width == 0 &&
           (member->memory_flags & (1 | 8)) == 0 &&
           access->opcode == opcode &&
           access->src1 == member->dst &&
           access->memory_size == 1 &&
           access->bit_width == 0 &&
           (access->memory_flags & (1 | 8)) == 0;
}

static int mir_machine_same_global_byte_access(
    int root_index, int member_index, int access_index,
    int opcode, struct Sym *symbol, int offset)
{
    struct Sym *access_symbol;
    int access_offset;

    return mir_machine_global_byte_access(
               root_index, member_index, access_index, opcode,
               &access_symbol, &access_offset) &&
           access_symbol == symbol && access_offset == offset;
}

static int mir_match_byte_rotate_flags(
    struct MirByteRotateFlags *plan)
{
    static const int expected_opcodes[139] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_UNARY,
        MIR_NOP, MIR_STORE, MIR_CONST, MIR_NOP, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_NOP,
        MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_STORE_INDIRECT,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_UNARY, MIR_STORE,
        MIR_NOP, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_NOP, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_NOP, MIR_STORE, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_CONST, MIR_NOP, MIR_UNARY,
        MIR_BINARY, MIR_UNARY, MIR_STORE_INDIRECT, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_UNARY, MIR_STORE,
        MIR_NOP, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_NOP,
        MIR_STORE, MIR_LABEL, MIR_NOP, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_NOP, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_NOP, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_UNARY, MIR_STORE_INDIRECT, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_UNARY, MIR_STORE,
        MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_STORE,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_STORE_INDIRECT,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_UNARY,
        MIR_STORE, MIR_NOP, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_UNARY,
        MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP,
        MIR_LABEL, MIR_LABEL, MIR_LABEL, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_UNARY, MIR_STORE_INDIRECT, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD, MIR_UNARY, MIR_UNARY,
        MIR_STORE_INDIRECT, MIR_LOAD, MIR_RETURN
    };
    const struct MirInsn *operation;
    const struct MirInsn *value;
    struct Sym *state;
    struct Sym *member_state;
    int carry_offset;
    int member_offset;
    static const int boolean_member_index[8] = {
        17, 39, 44, 76, 92, 97, 124, 132
    };
    static const int boolean_value_index[8] = {
        22, 40, 49, 81, 93, 102, 129, 135
    };
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir_cfg_block_count() != 12 || mir.count != 139 ||
        type_size(mir.return_type) != 1)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    operation = &mir.insns[1];
    value = &mir.insns[2];
    for (instruction = 0; instruction < mir.count; ++instruction)
        if ((mir.insns[instruction].opcode == MIR_LOAD_INDIRECT ||
             mir.insns[instruction].opcode == MIR_STORE_INDIRECT) &&
            (mir.insns[instruction].memory_size != 1 ||
             mir.insns[instruction].bit_width != 0 ||
             (mir.insns[instruction].memory_flags & (1 | 8)) != 0))
            return 0;
    if (type_size(operation->type) != 1 ||
        (operation->type & TYPE_UNSIGNED) == 0 ||
        type_size(value->type) != 1 ||
        (value->type & TYPE_UNSIGNED) == 0 ||
        !mir_machine_parameter_value_offset(
            operation->dst, &plan->operation_stack_offset) ||
        !mir_machine_parameter_value_offset(
            value->dst, &plan->value_stack_offset) ||
        !mir_machine_constant_equals(mir.insns[6].src2, 224) ||
        mir.insns[6].immediate != '&' ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        mir.insns[5].src1 != operation->dst ||
        mir.insns[7].src1 != mir.insns[6].dst ||
        !mir_machine_same_location(operation, &mir.insns[9]) ||
        mir.insns[9].src1 != mir.insns[7].dst ||
        !mir_machine_constant_equals(mir.insns[13].src1, 0) ||
        mir.insns[13].immediate != TOK_EQ ||
        mir.insns[13].src2 != mir.insns[12].dst ||
        mir.insns[12].src1 != mir.insns[7].dst ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        mir.insns[14].label != mir.insns[31].label ||
        !mir_machine_constant_equals(mir.insns[35].src1, 32) ||
        mir.insns[35].immediate != TOK_EQ ||
        mir.insns[35].src2 != mir.insns[34].dst ||
        mir.insns[34].src1 != mir.insns[7].dst ||
        mir.insns[36].src1 != mir.insns[35].dst ||
        mir.insns[36].label != mir.insns[68].label ||
        !mir_machine_constant_equals(mir.insns[72].src1, 64) ||
        mir.insns[72].immediate != TOK_EQ ||
        mir.insns[72].src2 != mir.insns[71].dst ||
        mir.insns[71].src1 != mir.insns[7].dst ||
        mir.insns[73].src1 != mir.insns[72].dst ||
        mir.insns[73].label != mir.insns[90].label)
        return 0;
    if (!mir_machine_global_byte_member(
            16, 17, &state, &carry_offset))
        return 0;
#define SAME_MEMBER(root_i, member_i, expected_offset) \
    (mir_machine_global_byte_member( \
         (root_i), (member_i), &member_state, &member_offset) && \
     member_state == state && member_offset == (expected_offset))
    if (!SAME_MEMBER(38, 39, carry_offset) ||
        !SAME_MEMBER(43, 44, carry_offset) ||
        !SAME_MEMBER(75, 76, carry_offset) ||
        !SAME_MEMBER(91, 92, carry_offset) ||
        !SAME_MEMBER(96, 97, carry_offset) ||
        !mir_machine_global_byte_member(
            123, 124, &member_state, &plan->negative_offset) ||
        member_state != state ||
        !mir_machine_global_byte_member(
            131, 132, &member_state, &plan->zero_offset) ||
        member_state != state)
        return 0;
#undef SAME_MEMBER
    for (instruction = 0; instruction < 8; ++instruction)
        if ((mir.insns[boolean_member_index[instruction]].type & 15) !=
                TYPE_BOOL ||
            (mir.insns[boolean_value_index[instruction]].type & 15) !=
                TYPE_BOOL)
            return 0;
    if (!mir_machine_constant_equals(mir.insns[21].src1, 128) ||
        mir.insns[21].immediate != '&' ||
        mir.insns[21].src2 != mir.insns[20].dst ||
        mir.insns[20].src1 != value->dst ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        mir.insns[23].src1 != mir.insns[17].dst ||
        mir.insns[23].src2 != mir.insns[22].dst ||
        mir.insns[26].immediate != TOK_SHL ||
        mir.insns[26].src1 != value->dst ||
        !mir_machine_constant_equals(mir.insns[26].src2, 1) ||
        mir.insns[27].src1 != mir.insns[26].dst ||
        !mir_machine_same_location(value, &mir.insns[28]) ||
        mir.insns[28].src1 != mir.insns[27].dst ||
        mir.insns[30].label != mir.insns[122].label)
        return 0;
    if (mir.insns[40].src1 != mir.insns[39].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[42]) ||
        mir.insns[42].src1 != mir.insns[40].dst ||
        !mir_machine_constant_equals(mir.insns[48].src1, 128) ||
        mir.insns[48].immediate != '&' ||
        mir.insns[48].src2 != mir.insns[47].dst ||
        mir.insns[47].src1 != value->dst ||
        mir.insns[49].src1 != mir.insns[48].dst ||
        mir.insns[50].src1 != mir.insns[44].dst ||
        mir.insns[50].src2 != mir.insns[49].dst ||
        mir.insns[53].immediate != TOK_SHL ||
        mir.insns[53].src1 != value->dst ||
        !mir_machine_constant_equals(mir.insns[53].src2, 1) ||
        mir.insns[54].src1 != mir.insns[53].dst ||
        !mir_machine_same_location(value, &mir.insns[55]) ||
        mir.insns[55].src1 != mir.insns[54].dst ||
        mir.insns[57].src1 != mir.insns[40].dst ||
        mir.insns[57].label != mir.insns[121].label ||
        mir.insns[60].immediate != 0 ||
        mir.insns[60].src1 != mir.insns[54].dst ||
        mir.insns[61].immediate != '|' ||
        mir.insns[61].src1 != mir.insns[60].dst ||
        !mir_machine_constant_equals(mir.insns[61].src2, 1) ||
        mir.insns[62].immediate != 0 ||
        mir.insns[62].src1 != mir.insns[61].dst ||
        !mir_machine_same_location(value, &mir.insns[64]) ||
        mir.insns[64].src1 != mir.insns[62].dst ||
        mir.insns[67].label != mir.insns[121].label)
        return 0;
    if (mir.insns[80].immediate != '&' ||
        mir.insns[80].src1 != mir.insns[79].dst ||
        !mir_machine_constant_equals(mir.insns[80].src2, 1) ||
        mir.insns[79].src1 != value->dst ||
        mir.insns[81].src1 != mir.insns[80].dst ||
        mir.insns[82].src1 != mir.insns[76].dst ||
        mir.insns[82].src2 != mir.insns[81].dst ||
        mir.insns[85].immediate != TOK_SHR ||
        mir.insns[85].src1 != value->dst ||
        !mir_machine_constant_equals(mir.insns[85].src2, 1) ||
        mir.insns[86].src1 != mir.insns[85].dst ||
        !mir_machine_same_location(value, &mir.insns[87]) ||
        mir.insns[87].src1 != mir.insns[86].dst ||
        mir.insns[89].label != mir.insns[120].label)
        return 0;
    if (mir.insns[93].src1 != mir.insns[92].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[95]) ||
        mir.insns[95].src1 != mir.insns[93].dst ||
        mir.insns[101].immediate != '&' ||
        mir.insns[101].src1 != mir.insns[100].dst ||
        !mir_machine_constant_equals(mir.insns[101].src2, 1) ||
        mir.insns[100].src1 != value->dst ||
        mir.insns[102].src1 != mir.insns[101].dst ||
        mir.insns[103].src1 != mir.insns[97].dst ||
        mir.insns[103].src2 != mir.insns[102].dst ||
        mir.insns[106].immediate != TOK_SHR ||
        mir.insns[106].src1 != value->dst ||
        !mir_machine_constant_equals(mir.insns[106].src2, 1) ||
        mir.insns[107].src1 != mir.insns[106].dst ||
        !mir_machine_same_location(value, &mir.insns[108]) ||
        mir.insns[108].src1 != mir.insns[107].dst ||
        mir.insns[110].src1 != mir.insns[93].dst ||
        mir.insns[110].label != mir.insns[118].label ||
        mir.insns[113].immediate != 0 ||
        mir.insns[113].src1 != mir.insns[107].dst ||
        mir.insns[114].immediate != '|' ||
        mir.insns[114].src1 != mir.insns[113].dst ||
        !mir_machine_constant_equals(mir.insns[114].src2, 128) ||
        mir.insns[115].immediate != 0 ||
        mir.insns[115].src1 != mir.insns[114].dst ||
        !mir_machine_same_location(value, &mir.insns[117]) ||
        mir.insns[117].src1 != mir.insns[115].dst)
        return 0;
    if (mir.insns[30].label != mir.insns[122].label ||
        mir.insns[67].label != mir.insns[121].label ||
        mir.insns[89].label != mir.insns[120].label ||
        !mir_machine_same_location(value, &mir.insns[125]) ||
        mir.insns[128].immediate != '&' ||
        mir.insns[128].src1 != mir.insns[127].dst ||
        !mir_machine_constant_equals(mir.insns[128].src2, 128) ||
        mir.insns[129].src1 != mir.insns[128].dst ||
        mir.insns[130].src1 != mir.insns[124].dst ||
        mir.insns[130].src2 != mir.insns[129].dst ||
        !mir_machine_same_location(value, &mir.insns[133]) ||
        mir.insns[134].immediate != '!' ||
        mir.insns[134].src1 != mir.insns[133].dst ||
        mir.insns[135].src1 != mir.insns[134].dst ||
        mir.insns[136].src1 != mir.insns[132].dst ||
        mir.insns[136].src2 != mir.insns[135].dst ||
        !mir_machine_same_location(value, &mir.insns[137]) ||
        mir.insns[138].src1 != mir.insns[137].dst)
        return 0;
    plan->state = state;
    plan->carry_offset = carry_offset;
    return 1;
}

static int mir_match_status_unpack(struct MirStatusUnpack *plan)
{
    static const int expected_opcodes[62] = {
        MIR_LABEL, MIR_ADDRESS, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_BINARY, MIR_STORE_INDIRECT, MIR_BINARY,
        MIR_LOAD_INDIRECT, MIR_STORE,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_STORE_INDIRECT,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_STORE_INDIRECT,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_STORE_INDIRECT,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_STORE_INDIRECT,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_STORE_INDIRECT,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_STORE_INDIRECT
    };
    static const int expected_masks[6] = { 128, 64, 8, 4, 2, 1 };
    const struct MirInsn *memory_root;
    const struct MirInsn *memory_base;
    const struct MirInsn *stack_member;
    const struct MirInsn *stack_load;
    const struct MirInsn *stack_increment;
    const struct MirInsn *stack_store;
    const struct MirInsn *element_address;
    const struct MirInsn *element_load;
    const struct MirInsn *local_store;
    struct Sym *state;
    struct Sym *member_state;
    int member_offset;
    int memory_type;
    int memory_storage;
    int memory_offset;
    int flag;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir_cfg_block_count() != 1 || mir.count != 62 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
        else if ((mir.insns[instruction].opcode ==
                      MIR_LOAD_INDIRECT ||
                  mir.insns[instruction].opcode ==
                      MIR_STORE_INDIRECT) &&
                 (mir.insns[instruction].memory_size != 1 ||
                  mir.insns[instruction].bit_width != 0 ||
                  (mir.insns[instruction].memory_flags & (1 | 8)) != 0))
            return 0;
    memory_root = &mir.insns[1];
    memory_base = &mir.insns[4];
    stack_member = &mir.insns[6];
    stack_load = &mir.insns[7];
    stack_increment = &mir.insns[9];
    stack_store = &mir.insns[10];
    element_address = &mir.insns[11];
    element_load = &mir.insns[12];
    local_store = &mir.insns[13];
    if (!mir_scalar_memory_location(
            memory_root, &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        memory_base->immediate != '+' ||
        memory_base->src1 != memory_root->dst ||
        !mir_machine_constant_equals(memory_base->src2, 256) ||
        !mir_machine_global_byte_member(
            5, 6, &state, &plan->stack_offset) ||
        stack_load->src1 != stack_member->dst ||
        (stack_load->type & TYPE_UNSIGNED) == 0 ||
        type_size(stack_load->type) != 1 ||
        stack_increment->immediate != '+' ||
        stack_increment->src1 != stack_load->dst ||
        (stack_increment->type & TYPE_UNSIGNED) == 0 ||
        type_size(stack_increment->type) != 1 ||
        (stack_increment->secondary_offset & TYPE_UNSIGNED) == 0 ||
        type_size(stack_increment->secondary_offset) != 1 ||
        !mir_machine_constant_equals(stack_increment->src2, 1) ||
        stack_store->src1 != stack_member->dst ||
        stack_store->src2 != stack_increment->dst ||
        element_address->immediate != '+' ||
        element_address->src1 != memory_base->dst ||
        element_address->src2 != stack_increment->dst ||
        element_load->src1 != element_address->dst ||
        !mir_machine_unobservable_local_store(local_store) ||
        local_store->src1 != element_load->dst)
        return 0;
    plan->memory = find_global(memory_root->name);
    plan->state = state;
    if (plan->memory == NULL || plan->memory->is_volatile ||
        memory_offset < -32768 || memory_offset > 32511)
        return 0;
    plan->memory_offset = memory_offset + 256;
    for (flag = 0; flag < 6; ++flag) {
        int base = 14 + flag * 8;
        const struct MirInsn *member = &mir.insns[base + 1];
        const struct MirInsn *constant = &mir.insns[base + 3];
        const struct MirInsn *source = &mir.insns[base + 4];
        const struct MirInsn *masked = &mir.insns[base + 5];
        const struct MirInsn *converted = &mir.insns[base + 6];
        const struct MirInsn *store = &mir.insns[base + 7];

        if (!mir_machine_global_byte_member(
                base, base + 1,
                &member_state, &member_offset) ||
            member_state != state ||
            (member->type & 15) != TYPE_BOOL ||
            constant->immediate != expected_masks[flag] ||
            source->immediate != 0 ||
            source->src1 != element_load->dst ||
            masked->immediate != '&' ||
            masked->src1 != source->dst ||
            masked->src2 != constant->dst ||
            converted->immediate != 0 ||
            converted->src1 != masked->dst ||
            (converted->type & 15) != TYPE_BOOL ||
            store->src1 != member->dst ||
            store->src2 != converted->dst)
            return 0;
        plan->flag_offsets[flag] = member_offset;
        plan->masks[flag] = expected_masks[flag];
    }
    return 1;
}

static int mir_match_status_pack(struct MirStatusPack *plan)
{
    static const int expected_opcodes[79] = {
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_STORE,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_UNARY, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_UNARY, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_UNARY, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_UNARY, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_UNARY, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_UNARY, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_LOAD, MIR_ARG, MIR_CALL
    };
    static const int expected_masks[6] = { 128, 64, 8, 4, 2, 1 };
    const struct MirInsn *initial_value = &mir.insns[2];
    const struct MirInsn *initial_store = &mir.insns[3];
    const struct MirInsn *final_load = &mir.insns[76];
    const struct MirInsn *argument = &mir.insns[77];
    const struct MirInsn *call = &mir.insns[78];
    struct Sym *state = NULL;
    int packed_value;
    int flag;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir_cfg_block_count() != 7 || mir.count != 79 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    if (initial_value->immediate != 48 ||
        (initial_value->type & TYPE_UNSIGNED) == 0 ||
        !mir_machine_unobservable_local_store(initial_store) ||
        initial_store->src1 != initial_value->dst)
        return 0;
    packed_value = initial_value->dst;
    for (flag = 0; flag < 6; ++flag) {
        int base = 4 + flag * 12;
        const struct MirInsn *member = &mir.insns[base + 1];
        const struct MirInsn *load = &mir.insns[base + 2];
        const struct MirInsn *branch = &mir.insns[base + 3];
        const struct MirInsn *packed_load = &mir.insns[base + 4];
        const struct MirInsn *constant = &mir.insns[base + 5];
        const struct MirInsn *source = &mir.insns[base + 6];
        const struct MirInsn *combined = &mir.insns[base + 7];
        const struct MirInsn *converted = &mir.insns[base + 8];
        const struct MirInsn *store = &mir.insns[base + 10];
        const struct MirInsn *label = &mir.insns[base + 11];
        struct Sym *member_state;
        int member_offset;
        int packed_source = packed_value;

        if (!mir_machine_global_byte_member(
                base, base + 1,
                &member_state, &member_offset) ||
            (state != NULL && member_state != state) ||
            (member->type & 15) != TYPE_BOOL ||
            load->src1 != member->dst ||
            (load->type & 15) != TYPE_BOOL ||
            load->memory_size != 1 ||
            (load->memory_flags & (1 | 8)) != 0 ||
            branch->src1 != load->dst ||
            branch->label != label->label ||
            (flag == 0 ?
                 packed_load->opcode != MIR_NOP :
                 (!mir_machine_same_location(
                      initial_store, packed_load) ||
                  !mir_machine_named_nonvolatile(packed_load))) ||
            constant->immediate != expected_masks[flag] ||
            source->immediate != 0 ||
            combined->immediate != '|' ||
            combined->src2 != constant->dst ||
            converted->immediate != 0 ||
            converted->src1 != combined->dst ||
            (converted->type & TYPE_UNSIGNED) == 0 ||
            !mir_machine_same_location(initial_store, store) ||
            !mir_machine_named_nonvolatile(store) ||
            store->src1 != converted->dst)
            return 0;
        if (flag != 0)
            packed_source = packed_load->dst;
        if (source->src1 != packed_source ||
            combined->src1 != source->dst)
            return 0;
        state = member_state;
        plan->flag_offsets[flag] = member_offset;
        plan->masks[flag] = expected_masks[flag];
        packed_value = converted->dst;
    }
    plan->state = state;
    plan->function = find_global(call->name);
    if (!mir_machine_same_location(initial_store, final_load) ||
        !mir_machine_named_nonvolatile(final_load) ||
        (final_load->type & TYPE_UNSIGNED) == 0 ||
        type_size(final_load->type) != 1 ||
        final_load->dst != argument->src1 ||
        argument->immediate != 0 ||
        argument->secondary_offset != call->secondary_offset ||
        (argument->type & TYPE_UNSIGNED) == 0 ||
        type_size(argument->type) != 1 ||
        type_ptr_depth(argument->type) != 0 ||
        plan->function == NULL || !plan->function->is_defined ||
        plan->function->storage != SC_FUNC ||
        plan->function->is_funcptr ||
        plan->function->is_noreturn ||
        !plan->function->has_proto ||
        plan->function->proto_nargs != 1 ||
        plan->function->proto_variadic ||
        plan->function->proto_types[0] != argument->type ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0 ||
        (call->base_name[0] != 0 &&
         strcmp(call->base_name,
                asm_name_for(sym_asm_name(plan->function)))))
        return 0;
    return 1;
}

static int mir_match_byte_math_flags(struct MirByteMathFlags *plan)
{
    static const int expected_opcodes[220] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_UNARY, MIR_NOP, MIR_STORE, MIR_CONST, MIR_NOP,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_NOP,
        MIR_ARG, MIR_CALL, MIR_RETURN, MIR_NOP,
        MIR_LABEL, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_BRANCH_FALSE, MIR_CONST, MIR_LOAD, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LOAD, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST,
        MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_PHI, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE,
        MIR_LOAD, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_RETURN,
        MIR_NOP, MIR_LABEL, MIR_CONST, MIR_LOAD, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_CONST, MIR_LOAD, MIR_UNARY, MIR_BINARY,
        MIR_UNARY, MIR_STORE, MIR_NOP, MIR_CONST, MIR_STORE, MIR_NOP,
        MIR_LABEL, MIR_CONST, MIR_LOAD, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_LOAD, MIR_UNARY, MIR_BINARY,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY,
        MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP, MIR_UNARY, MIR_NOP,
        MIR_STORE, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_NOP, MIR_BINARY, MIR_UNARY,
        MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_LOAD, MIR_UNARY,
        MIR_UNARY, MIR_BINARY, MIR_CONST, MIR_BINARY, MIR_UNARY,
        MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_BINARY,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_NOP,
        MIR_STORE_INDIRECT, MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_CONST,
        MIR_LOAD, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_LOAD,
        MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_STORE_INDIRECT, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LOAD, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_LOAD, MIR_UNARY, MIR_BINARY, MIR_UNARY,
        MIR_STORE_INDIRECT, MIR_JUMP, MIR_LABEL, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_LOAD, MIR_UNARY,
        MIR_BINARY, MIR_UNARY, MIR_STORE_INDIRECT, MIR_LABEL, MIR_LABEL,
        MIR_LABEL, MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_UNARY, MIR_STORE_INDIRECT, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_UNARY, MIR_STORE_INDIRECT
    };
    static const int edge_pairs[][2] = {
        { 14, 24 }, { 28, 58 }, { 33, 37 }, { 36, 52 },
        { 42, 46 }, { 45, 48 }, { 51, 52 }, { 54, 58 },
        { 57, 60 }, { 62, 70 }, { 75, 86 }, { 91, 158 },
        { 133, 147 }, { 143, 147 }, { 146, 149 },
        { 157, 201 }, { 163, 174 }, { 173, 200 },
        { 179, 190 }, { 189, 199 }
    };
    const struct MirInsn *op = &mir.insns[1];
    const struct MirInsn *rhs = &mir.insns[2];
    const struct MirInsn *op_store = &mir.insns[9];
    const struct MirInsn *rhs_store = &mir.insns[81];
    const struct MirInsn *result_store = &mir.insns[110];
    const struct MirInsn *compare_call = &mir.insns[21];
    const struct MirInsn *decimal_call = &mir.insns[67];
    int arguments[2];
    int edge;
    int instruction;
    struct Sym *state;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 220 || mir_cfg_block_count() != 26 ||
        (mir.return_type & 15) != TYPE_VOID || mir.has_vla)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode != expected_opcodes[instruction])
            return 0;
        if ((insn->opcode == MIR_LOAD_INDIRECT ||
             insn->opcode == MIR_STORE_INDIRECT) &&
            (insn->memory_size != 1 || insn->bit_width != 0 ||
             (insn->memory_flags & (1 | 8)) != 0))
            return 0;
    }
    for (edge = 0;
         edge < (int)(sizeof(edge_pairs) / sizeof(edge_pairs[0]));
         ++edge)
        if (mir.insns[edge_pairs[edge][0]].label !=
            mir.insns[edge_pairs[edge][1]].label)
            return 0;
    if ((op->type & 15) != TYPE_CHAR ||
        (op->type & TYPE_UNSIGNED) == 0 ||
        type_ptr_depth(op->type) != 0 ||
        rhs->type != op->type ||
        !mir_machine_parameter_value_offset(
            op->dst, &plan->op_stack_offset) ||
        !mir_machine_parameter_value_offset(
            rhs->dst, &plan->rhs_stack_offset) ||
        plan->op_stack_offset != 2 ||
        plan->rhs_stack_offset != 4 ||
        !mir_machine_same_location(op, op_store) ||
        !mir_machine_named_nonvolatile(op_store) ||
        mir.insns[5].immediate != 0 ||
        mir.insns[5].src1 != op->dst ||
        mir.insns[6].immediate != '&' ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        !mir_machine_constant_equals(mir.insns[6].src2, 224) ||
        mir.insns[7].immediate != 0 ||
        mir.insns[7].src1 != mir.insns[6].dst ||
        mir.insns[7].type != op->type ||
        op_store->src1 != mir.insns[7].dst)
        return 0;

    if (mir.insns[12].immediate != 0 ||
        mir.insns[12].src1 != mir.insns[7].dst ||
        mir.insns[13].immediate != TOK_EQ ||
        !mir_machine_constant_equals(mir.insns[13].src1, 192) ||
        mir.insns[13].src2 != mir.insns[12].dst ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        !mir_machine_global_byte_access(
            15, 16, 17, MIR_LOAD_INDIRECT,
            &state, &plan->accumulator_offset) ||
        (mir.insns[16].type & TYPE_UNSIGNED) == 0 ||
        (mir.insns[16].type & 15) != TYPE_CHAR ||
        mir.insns[17].type != op->type ||
        !mir_machine_two_call_arguments(compare_call, arguments) ||
        arguments[0] != mir.insns[17].dst ||
        arguments[1] != rhs->dst ||
        mir.insns[18].type != op->type ||
        mir.insns[20].type != rhs->type)
        return 0;
    plan->state = state;
    plan->compare_function = find_global(compare_call->name);
    if (plan->compare_function == NULL ||
        (compare_call->type & 15) != TYPE_VOID ||
        !plan->compare_function->is_defined ||
        plan->compare_function->storage != SC_FUNC ||
        plan->compare_function->is_funcptr ||
        plan->compare_function->is_noreturn ||
        (compare_call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0 ||
        (compare_call->base_name[0] != 0 &&
         strcmp(compare_call->base_name,
                asm_name_for(sym_asm_name(
                    plan->compare_function)))) ||
        (plan->compare_function->has_proto &&
         (plan->compare_function->proto_nargs != 2 ||
          plan->compare_function->proto_variadic ||
          plan->compare_function->proto_types[0] !=
              mir.insns[18].type ||
          plan->compare_function->proto_types[1] !=
              mir.insns[20].type)))
        return 0;

    if (!mir_machine_global_byte_access(
            25, 26, 27, MIR_LOAD_INDIRECT,
            &state, &plan->decimal_offset) ||
        state != plan->state ||
        (mir.insns[26].type & 15) != TYPE_BOOL ||
        (mir.insns[27].type & 15) != TYPE_BOOL ||
        mir.insns[28].src1 != mir.insns[27].dst ||
        !mir_machine_same_location(op_store, &mir.insns[30]) ||
        !mir_machine_same_location(op_store, &mir.insns[39]) ||
        !mir_machine_named_nonvolatile(&mir.insns[30]) ||
        !mir_machine_named_nonvolatile(&mir.insns[39]) ||
        mir.insns[31].immediate != 0 ||
        mir.insns[31].src1 != mir.insns[30].dst ||
        mir.insns[32].immediate != TOK_EQ ||
        !mir_machine_constant_equals(mir.insns[32].src1, 224) ||
        mir.insns[32].src2 != mir.insns[31].dst ||
        mir.insns[33].src1 != mir.insns[32].dst ||
        !mir_machine_constant_equals(mir.insns[35].dst, 1) ||
        mir.insns[40].immediate != 0 ||
        mir.insns[40].src1 != mir.insns[39].dst ||
        mir.insns[41].immediate != TOK_EQ ||
        !mir_machine_constant_equals(mir.insns[41].src1, 96) ||
        mir.insns[41].src2 != mir.insns[40].dst ||
        mir.insns[42].src1 != mir.insns[41].dst ||
        !mir_machine_constant_equals(mir.insns[44].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[47].dst, 0) ||
        mir.insns[49].src1 != mir.insns[44].dst ||
        mir.insns[49].src2 != mir.insns[47].dst ||
        mir.insns[49].phi_pred1 != mir.insns[43].label ||
        mir.insns[49].phi_pred2 != mir.insns[46].label ||
        mir.insns[53].src1 != mir.insns[35].dst ||
        mir.insns[53].src2 != mir.insns[49].dst ||
        mir.insns[53].phi_pred1 != mir.insns[34].label ||
        mir.insns[53].phi_pred2 != mir.insns[50].label ||
        mir.insns[54].src1 != mir.insns[53].dst ||
        !mir_machine_constant_equals(mir.insns[56].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[59].dst, 0) ||
        mir.insns[61].src1 != mir.insns[56].dst ||
        mir.insns[61].src2 != mir.insns[59].dst ||
        mir.insns[61].phi_pred1 != mir.insns[55].label ||
        mir.insns[61].phi_pred2 != mir.insns[58].label ||
        mir.insns[62].src1 != mir.insns[61].dst ||
        !mir_machine_same_location(op_store, &mir.insns[63]) ||
        !mir_machine_same_location(rhs, &mir.insns[65]) ||
        !mir_machine_named_nonvolatile(&mir.insns[63]) ||
        !mir_machine_named_nonvolatile(&mir.insns[65]) ||
        !mir_machine_two_call_arguments(decimal_call, arguments) ||
        arguments[0] != mir.insns[63].dst ||
        arguments[1] != mir.insns[65].dst ||
        mir.insns[64].type != op->type ||
        mir.insns[66].type != rhs->type)
        return 0;
    plan->decimal_function = find_global(decimal_call->name);
    if (plan->decimal_function == NULL ||
        (decimal_call->type & 15) != TYPE_VOID ||
        !plan->decimal_function->is_defined ||
        plan->decimal_function->storage != SC_FUNC ||
        plan->decimal_function->is_funcptr ||
        plan->decimal_function->is_noreturn ||
        (decimal_call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0 ||
        (decimal_call->base_name[0] != 0 &&
         strcmp(decimal_call->base_name,
                asm_name_for(sym_asm_name(
                    plan->decimal_function)))) ||
        (plan->decimal_function->has_proto &&
         (plan->decimal_function->proto_nargs != 2 ||
          plan->decimal_function->proto_variadic ||
          plan->decimal_function->proto_types[0] !=
              mir.insns[64].type ||
          plan->decimal_function->proto_types[1] !=
              mir.insns[66].type)))
        return 0;

    if (!mir_machine_same_location(op_store, &mir.insns[72]) ||
        !mir_machine_named_nonvolatile(&mir.insns[72]) ||
        mir.insns[73].immediate != 0 ||
        mir.insns[73].src1 != mir.insns[72].dst ||
        mir.insns[74].immediate != TOK_EQ ||
        !mir_machine_constant_equals(mir.insns[74].src1, 224) ||
        mir.insns[74].src2 != mir.insns[73].dst ||
        mir.insns[75].src1 != mir.insns[74].dst ||
        !mir_machine_same_location(rhs, &mir.insns[77]) ||
        !mir_machine_named_nonvolatile(&mir.insns[77]) ||
        mir.insns[78].immediate != 0 ||
        mir.insns[78].src1 != mir.insns[77].dst ||
        mir.insns[79].immediate != '-' ||
        !mir_machine_constant_equals(mir.insns[79].src1, 255) ||
        mir.insns[79].src2 != mir.insns[78].dst ||
        mir.insns[80].immediate != 0 ||
        mir.insns[80].src1 != mir.insns[79].dst ||
        mir.insns[80].type != rhs->type ||
        !mir_machine_same_location(rhs, rhs_store) ||
        !mir_machine_named_nonvolatile(rhs_store) ||
        rhs_store->src1 != mir.insns[80].dst ||
        !mir_machine_same_location(op_store, &mir.insns[84]) ||
        !mir_machine_named_nonvolatile(&mir.insns[84]) ||
        !mir_machine_constant_equals(mir.insns[84].src1, 96) ||
        !mir_machine_same_location(op_store, &mir.insns[88]) ||
        !mir_machine_named_nonvolatile(&mir.insns[88]) ||
        mir.insns[89].immediate != 0 ||
        mir.insns[89].src1 != mir.insns[88].dst ||
        mir.insns[90].immediate != TOK_EQ ||
        !mir_machine_constant_equals(mir.insns[90].src1, 96) ||
        mir.insns[90].src2 != mir.insns[89].dst ||
        mir.insns[91].src1 != mir.insns[90].dst)
        return 0;

    if (!mir_machine_same_global_byte_access(
            93, 94, 95, MIR_LOAD_INDIRECT, plan->state,
            plan->accumulator_offset) ||
        (mir.insns[95].type & TYPE_UNSIGNED) == 0 ||
        mir.insns[96].immediate != 0 ||
        mir.insns[96].src1 != mir.insns[95].dst ||
        type_size(mir.insns[96].type) != 2 ||
        (mir.insns[96].type & TYPE_UNSIGNED) == 0 ||
        !mir_machine_same_location(rhs, &mir.insns[97]) ||
        !mir_machine_named_nonvolatile(&mir.insns[97]) ||
        mir.insns[98].immediate != 0 ||
        mir.insns[98].src1 != mir.insns[97].dst ||
        mir.insns[98].type != mir.insns[96].type ||
        mir.insns[99].immediate != '+' ||
        mir.insns[99].src1 != mir.insns[96].dst ||
        mir.insns[99].src2 != mir.insns[98].dst ||
        !mir_machine_global_byte_access(
            100, 101, 102, MIR_LOAD_INDIRECT,
            &state, &plan->carry_offset) ||
        state != plan->state ||
        (mir.insns[101].type & 15) != TYPE_BOOL ||
        (mir.insns[102].type & 15) != TYPE_BOOL ||
        mir.insns[103].immediate != 0 ||
        mir.insns[103].src1 != mir.insns[102].dst ||
        type_size(mir.insns[103].type) != 2 ||
        (mir.insns[103].type & TYPE_UNSIGNED) == 0 ||
        mir.insns[104].immediate != '+' ||
        mir.insns[104].src1 != mir.insns[99].dst ||
        mir.insns[104].src2 != mir.insns[103].dst ||
        type_size(mir.insns[104].type) != 2 ||
        (mir.insns[104].type & TYPE_UNSIGNED) == 0 ||
        !mir_machine_unobservable_local_store(&mir.insns[106]) ||
        mir.insns[106].src1 != mir.insns[104].dst ||
        mir.insns[108].immediate != 0 ||
        mir.insns[108].src1 != mir.insns[104].dst ||
        mir.insns[108].type != rhs->type ||
        !mir_machine_unobservable_local_store(result_store) ||
        result_store->src1 != mir.insns[108].dst)
        return 0;

    if (!mir_machine_same_global_byte_access(
            111, 112, 120, MIR_STORE_INDIRECT,
            plan->state, plan->carry_offset) ||
        !mir_machine_constant_equals(mir.insns[113].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[115].dst, 65280) ||
        mir.insns[116].immediate != '&' ||
        mir.insns[116].src1 != mir.insns[104].dst ||
        mir.insns[116].src2 != mir.insns[115].dst ||
        mir.insns[118].immediate != TOK_NE ||
        mir.insns[118].src1 != mir.insns[113].dst ||
        mir.insns[118].src2 != mir.insns[116].dst ||
        mir.insns[119].immediate != 0 ||
        mir.insns[119].src1 != mir.insns[118].dst ||
        (mir.insns[119].type & 15) != TYPE_BOOL ||
        mir.insns[120].src2 != mir.insns[119].dst ||
        !mir_machine_global_byte_access(
            121, 122, 151, MIR_STORE_INDIRECT,
            &state, &plan->overflow_offset) ||
        state != plan->state ||
        (mir.insns[122].type & 15) != TYPE_BOOL ||
        !mir_machine_same_global_byte_access(
            123, 124, 125, MIR_LOAD_INDIRECT,
            plan->state, plan->accumulator_offset) ||
        !mir_machine_same_location(rhs, &mir.insns[126]) ||
        !mir_machine_named_nonvolatile(&mir.insns[126]) ||
        mir.insns[127].immediate != 0 ||
        mir.insns[127].src1 != mir.insns[125].dst ||
        mir.insns[128].immediate != 0 ||
        mir.insns[128].src1 != mir.insns[126].dst ||
        mir.insns[129].immediate != '^' ||
        mir.insns[129].src1 != mir.insns[127].dst ||
        mir.insns[129].src2 != mir.insns[128].dst ||
        mir.insns[131].immediate != '&' ||
        mir.insns[131].src1 != mir.insns[129].dst ||
        !mir_machine_constant_equals(mir.insns[131].src2, 128) ||
        mir.insns[132].immediate != '!' ||
        mir.insns[132].src1 != mir.insns[131].dst ||
        mir.insns[133].src1 != mir.insns[132].dst ||
        !mir_machine_same_global_byte_access(
            134, 135, 136, MIR_LOAD_INDIRECT,
            plan->state, plan->accumulator_offset) ||
        mir.insns[138].immediate != 0 ||
        mir.insns[138].src1 != mir.insns[136].dst ||
        mir.insns[139].immediate != 0 ||
        mir.insns[139].src1 != mir.insns[108].dst ||
        mir.insns[140].immediate != '^' ||
        mir.insns[140].src1 != mir.insns[138].dst ||
        mir.insns[140].src2 != mir.insns[139].dst ||
        mir.insns[142].immediate != '&' ||
        mir.insns[142].src1 != mir.insns[140].dst ||
        !mir_machine_constant_equals(mir.insns[142].src2, 128) ||
        mir.insns[143].src1 != mir.insns[142].dst ||
        !mir_machine_constant_equals(mir.insns[145].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[148].dst, 0) ||
        mir.insns[150].src1 != mir.insns[145].dst ||
        mir.insns[150].src2 != mir.insns[148].dst ||
        mir.insns[150].phi_pred1 != mir.insns[144].label ||
        mir.insns[150].phi_pred2 != mir.insns[147].label ||
        mir.insns[151].src2 != mir.insns[150].dst ||
        !mir_machine_same_global_byte_access(
            152, 153, 155, MIR_STORE_INDIRECT,
            plan->state, plan->accumulator_offset) ||
        mir.insns[155].src2 != mir.insns[108].dst)
        return 0;

    if (!mir_machine_same_location(op_store, &mir.insns[160]) ||
        !mir_machine_named_nonvolatile(&mir.insns[160]) ||
        mir.insns[161].immediate != 0 ||
        mir.insns[161].src1 != mir.insns[160].dst ||
        mir.insns[162].immediate != TOK_EQ ||
        !mir_machine_constant_equals(mir.insns[162].src1, 0) ||
        mir.insns[162].src2 != mir.insns[161].dst ||
        mir.insns[163].src1 != mir.insns[162].dst ||
        !mir_machine_same_global_byte_access(
            165, 166, 167, MIR_LOAD_INDIRECT,
            plan->state, plan->accumulator_offset) ||
        !mir_machine_same_location(rhs, &mir.insns[168]) ||
        !mir_machine_named_nonvolatile(&mir.insns[168]) ||
        mir.insns[169].immediate != 0 ||
        mir.insns[169].src1 != mir.insns[168].dst ||
        mir.insns[170].immediate != '|' ||
        mir.insns[170].src1 != mir.insns[167].dst ||
        mir.insns[170].src2 != mir.insns[169].dst ||
        mir.insns[171].immediate != 0 ||
        mir.insns[171].src1 != mir.insns[170].dst ||
        !mir_machine_same_global_byte_access(
            165, 166, 172, MIR_STORE_INDIRECT,
            plan->state, plan->accumulator_offset) ||
        mir.insns[172].src2 != mir.insns[171].dst ||
        !mir_machine_same_location(op_store, &mir.insns[176]) ||
        !mir_machine_named_nonvolatile(&mir.insns[176]) ||
        mir.insns[177].immediate != 0 ||
        mir.insns[177].src1 != mir.insns[176].dst ||
        mir.insns[178].immediate != TOK_EQ ||
        !mir_machine_constant_equals(mir.insns[178].src1, 32) ||
        mir.insns[178].src2 != mir.insns[177].dst ||
        mir.insns[179].src1 != mir.insns[178].dst)
        return 0;
    if (!mir_machine_same_global_byte_access(
            181, 182, 183, MIR_LOAD_INDIRECT,
            plan->state, plan->accumulator_offset) ||
        !mir_machine_same_location(rhs, &mir.insns[184]) ||
        !mir_machine_named_nonvolatile(&mir.insns[184]) ||
        mir.insns[185].immediate != 0 ||
        mir.insns[185].src1 != mir.insns[184].dst ||
        mir.insns[186].immediate != '&' ||
        mir.insns[186].src1 != mir.insns[183].dst ||
        mir.insns[186].src2 != mir.insns[185].dst ||
        mir.insns[187].immediate != 0 ||
        mir.insns[187].src1 != mir.insns[186].dst ||
        !mir_machine_same_global_byte_access(
            181, 182, 188, MIR_STORE_INDIRECT,
            plan->state, plan->accumulator_offset) ||
        mir.insns[188].src2 != mir.insns[187].dst ||
        !mir_machine_same_global_byte_access(
            191, 192, 193, MIR_LOAD_INDIRECT,
            plan->state, plan->accumulator_offset) ||
        !mir_machine_same_location(rhs, &mir.insns[194]) ||
        !mir_machine_named_nonvolatile(&mir.insns[194]) ||
        mir.insns[195].immediate != 0 ||
        mir.insns[195].src1 != mir.insns[194].dst ||
        mir.insns[196].immediate != '^' ||
        mir.insns[196].src1 != mir.insns[193].dst ||
        mir.insns[196].src2 != mir.insns[195].dst ||
        mir.insns[197].immediate != 0 ||
        mir.insns[197].src1 != mir.insns[196].dst ||
        !mir_machine_same_global_byte_access(
            191, 192, 198, MIR_STORE_INDIRECT,
            plan->state, plan->accumulator_offset) ||
        mir.insns[198].src2 != mir.insns[197].dst)
        return 0;

    if (!mir_machine_global_byte_access(
            202, 203, 211, MIR_STORE_INDIRECT,
            &state, &plan->negative_offset) ||
        state != plan->state ||
        (mir.insns[203].type & 15) != TYPE_BOOL ||
        !mir_machine_same_global_byte_access(
            204, 205, 206, MIR_LOAD_INDIRECT,
            plan->state, plan->accumulator_offset) ||
        mir.insns[208].immediate != 0 ||
        mir.insns[208].src1 != mir.insns[206].dst ||
        mir.insns[209].immediate != '&' ||
        mir.insns[209].src1 != mir.insns[208].dst ||
        !mir_machine_constant_equals(mir.insns[209].src2, 128) ||
        mir.insns[210].immediate != 0 ||
        mir.insns[210].src1 != mir.insns[209].dst ||
        (mir.insns[210].type & 15) != TYPE_BOOL ||
        mir.insns[211].src2 != mir.insns[210].dst ||
        !mir_machine_global_byte_access(
            212, 213, 219, MIR_STORE_INDIRECT,
            &state, &plan->zero_offset) ||
        state != plan->state ||
        (mir.insns[213].type & 15) != TYPE_BOOL ||
        !mir_machine_same_global_byte_access(
            214, 215, 216, MIR_LOAD_INDIRECT,
            plan->state, plan->accumulator_offset) ||
        mir.insns[217].immediate != '!' ||
        mir.insns[217].src1 != mir.insns[216].dst ||
        mir.insns[218].immediate != 0 ||
        mir.insns[218].src1 != mir.insns[217].dst ||
        (mir.insns[218].type & 15) != TYPE_BOOL ||
        mir.insns[219].src2 != mir.insns[218].dst)
        return 0;

    if (plan->accumulator_offset == plan->negative_offset ||
        plan->accumulator_offset == plan->overflow_offset ||
        plan->accumulator_offset == plan->decimal_offset ||
        plan->accumulator_offset == plan->zero_offset ||
        plan->accumulator_offset == plan->carry_offset ||
        plan->negative_offset == plan->overflow_offset ||
        plan->negative_offset == plan->decimal_offset ||
        plan->negative_offset == plan->zero_offset ||
        plan->negative_offset == plan->carry_offset ||
        plan->overflow_offset == plan->decimal_offset ||
        plan->overflow_offset == plan->zero_offset ||
        plan->overflow_offset == plan->carry_offset ||
        plan->decimal_offset == plan->zero_offset ||
        plan->decimal_offset == plan->carry_offset ||
        plan->zero_offset == plan->carry_offset)
        return 0;
    return 1;
}

static int mir_match_byte_range_union(struct MirByteRangeUnion *plan)
{
    static const int expected_opcodes[88] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
        MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
        MIR_PHI, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_PHI, MIR_RETURN
    };
    static const int lower_constants[3] = { 3, 25, 59 };
    static const int lower_unaries[3] = { 4, 26, 60 };
    static const int lower_binaries[3] = { 5, 27, 61 };
    static const int lower_branches[3] = { 6, 28, 62 };
    static const int upper_constants[3] = { 8, 30, 64 };
    static const int upper_unaries[3] = { 9, 31, 65 };
    static const int upper_binaries[3] = { 10, 32, 66 };
    static const int upper_branches[3] = { 11, 33, 67 };
    static const int parameter_nops[6] = { 2, 7, 24, 29, 58, 63 };
    static const int edge_pairs[][2] = {
        { 6, 15 }, { 11, 15 }, { 14, 17 }, { 19, 23 },
        { 22, 51 }, { 28, 37 }, { 33, 37 }, { 36, 39 },
        { 41, 45 }, { 44, 47 }, { 50, 51 }, { 53, 57 },
        { 56, 85 }, { 62, 71 }, { 67, 71 }, { 70, 73 },
        { 75, 79 }, { 78, 81 }, { 84, 85 }
    };
    const struct MirInsn *parameter = &mir.insns[1];
    int edge;
    int instruction;
    int range;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 88 || mir_cfg_block_count() != 24 ||
        (mir.return_type & 15) != TYPE_BOOL || mir.has_vla ||
        (parameter->type & 15) != TYPE_CHAR ||
        type_size(parameter->type) != 1 ||
        type_ptr_depth(parameter->type) != 0 ||
        !mir_machine_parameter_value_offset(
            parameter->dst, &plan->stack_offset) ||
        plan->stack_offset != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    for (instruction = 0; instruction < 6; ++instruction)
        if (mir.insns[parameter_nops[instruction]].object !=
            parameter->object)
            return 0;
    for (edge = 0;
         edge < (int)(sizeof(edge_pairs) / sizeof(edge_pairs[0]));
         ++edge)
        if (mir.insns[edge_pairs[edge][0]].label !=
            mir.insns[edge_pairs[edge][1]].label)
            return 0;
    for (range = 0; range < 3; ++range) {
        const struct MirInsn *lower_constant =
            &mir.insns[lower_constants[range]];
        const struct MirInsn *lower_unary =
            &mir.insns[lower_unaries[range]];
        const struct MirInsn *lower_binary =
            &mir.insns[lower_binaries[range]];
        const struct MirInsn *upper_constant =
            &mir.insns[upper_constants[range]];
        const struct MirInsn *upper_unary =
            &mir.insns[upper_unaries[range]];
        const struct MirInsn *upper_binary =
            &mir.insns[upper_binaries[range]];

        if (lower_unary->immediate != 0 ||
            lower_unary->src1 != parameter->dst ||
            lower_binary->immediate != TOK_GE ||
            lower_binary->src1 != lower_unary->dst ||
            lower_binary->src2 != lower_constant->dst ||
            mir.insns[lower_branches[range]].src1 !=
                lower_binary->dst ||
            upper_unary->immediate != 0 ||
            upper_unary->src1 != parameter->dst ||
            upper_binary->immediate != TOK_LE ||
            upper_binary->src1 != upper_unary->dst ||
            upper_binary->src2 != upper_constant->dst ||
            mir.insns[upper_branches[range]].src1 !=
                upper_binary->dst ||
            lower_constant->immediate < 0 ||
            lower_constant->immediate > 127 ||
            upper_constant->immediate <
                lower_constant->immediate ||
            upper_constant->immediate > 127)
            return 0;
        plan->lower[range] = (int)lower_constant->immediate;
        plan->upper[range] = (int)upper_constant->immediate;
    }
    if (!mir_machine_constant_equals(mir.insns[13].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[16].dst, 0) ||
        mir.insns[18].src1 != mir.insns[13].dst ||
        mir.insns[18].src2 != mir.insns[16].dst ||
        mir.insns[18].phi_pred1 != mir.insns[12].label ||
        mir.insns[18].phi_pred2 != mir.insns[15].label ||
        mir.insns[19].src1 != mir.insns[18].dst ||
        !mir_machine_constant_equals(mir.insns[21].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[35].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[38].dst, 0) ||
        mir.insns[40].src1 != mir.insns[35].dst ||
        mir.insns[40].src2 != mir.insns[38].dst ||
        mir.insns[40].phi_pred1 != mir.insns[34].label ||
        mir.insns[40].phi_pred2 != mir.insns[37].label ||
        mir.insns[41].src1 != mir.insns[40].dst ||
        !mir_machine_constant_equals(mir.insns[43].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[46].dst, 0) ||
        mir.insns[48].src1 != mir.insns[43].dst ||
        mir.insns[48].src2 != mir.insns[46].dst ||
        mir.insns[48].phi_pred1 != mir.insns[42].label ||
        mir.insns[48].phi_pred2 != mir.insns[45].label ||
        mir.insns[52].src1 != mir.insns[21].dst ||
        mir.insns[52].src2 != mir.insns[48].dst ||
        mir.insns[52].phi_pred1 != mir.insns[20].label ||
        mir.insns[52].phi_pred2 != mir.insns[49].label ||
        mir.insns[53].src1 != mir.insns[52].dst ||
        !mir_machine_constant_equals(mir.insns[55].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[69].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[72].dst, 0) ||
        mir.insns[74].src1 != mir.insns[69].dst ||
        mir.insns[74].src2 != mir.insns[72].dst ||
        mir.insns[74].phi_pred1 != mir.insns[68].label ||
        mir.insns[74].phi_pred2 != mir.insns[71].label ||
        mir.insns[75].src1 != mir.insns[74].dst ||
        !mir_machine_constant_equals(mir.insns[77].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[80].dst, 0) ||
        mir.insns[82].src1 != mir.insns[77].dst ||
        mir.insns[82].src2 != mir.insns[80].dst ||
        mir.insns[82].phi_pred1 != mir.insns[76].label ||
        mir.insns[82].phi_pred2 != mir.insns[79].label ||
        mir.insns[86].src1 != mir.insns[55].dst ||
        mir.insns[86].src2 != mir.insns[82].dst ||
        mir.insns[86].phi_pred1 != mir.insns[54].label ||
        mir.insns[86].phi_pred2 != mir.insns[83].label ||
        mir.insns[87].src1 != mir.insns[86].dst)
        return 0;
    return 1;
}

static int mir_match_byte_array_sum(struct MirByteArraySum *plan)
{
    static const int expected_opcodes[36] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP,
        MIR_PHI, MIR_PHI, MIR_NOP, MIR_NOP, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_BINARY, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_RETURN
    };
    const struct MirInsn *array = &mir.insns[1];
    const struct MirInsn *count = &mir.insns[2];
    const struct MirInsn *sum_phi = &mir.insns[12];
    const struct MirInsn *index_phi = &mir.insns[13];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 36 || mir_cfg_block_count() != 4 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_INT ||
        (mir.return_type & TYPE_UNSIGNED) != 0 ||
        type_ptr_depth(mir.return_type) != 0 ||
        type_size(mir.return_type) != 2 ||
        type_ptr_depth(mir.return_type) != 0)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    if (type_ptr_depth(array->type) != 1 ||
        (array->type & 15) != TYPE_CHAR ||
        (array->type & TYPE_UNSIGNED) == 0 ||
        type_size(array->type) != 2 ||
        type_ptr_depth(count->type) != 0 ||
        (count->type & 15) != TYPE_INT ||
        type_size(count->type) != 2 ||
        (count->type & TYPE_UNSIGNED) != 0 ||
        !mir_machine_parameter_value_offset(
            array->dst, &plan->array_stack_offset) ||
        !mir_machine_parameter_value_offset(
            count->dst, &plan->count_stack_offset) ||
        plan->array_stack_offset != 2 ||
        plan->count_stack_offset != 4 ||
        !mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[5]) ||
        mir.insns[5].memory_size != 2 ||
        mir.insns[5].src1 != mir.insns[3].dst ||
        !mir_machine_constant_equals(mir.insns[6].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[8]) ||
        mir.insns[8].memory_size != 2 ||
        mir.insns[8].src1 != mir.insns[6].dst)
        return 0;
    if ((sum_phi->type & 15) != TYPE_INT ||
        (sum_phi->type & TYPE_UNSIGNED) != 0 ||
        type_size(sum_phi->type) != 2 ||
        (index_phi->type & 15) != TYPE_INT ||
        (index_phi->type & TYPE_UNSIGNED) != 0 ||
        type_size(index_phi->type) != 2 ||
        sum_phi->src1 != mir.insns[3].dst ||
        sum_phi->src2 != mir.insns[24].dst ||
        sum_phi->phi_pred1 != mir.insns[0].label ||
        sum_phi->phi_pred2 != mir.insns[27].label ||
        index_phi->src1 != mir.insns[6].dst ||
        index_phi->src2 != mir.insns[30].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[27].label ||
        mir.insns[16].immediate != '<' ||
        type_size(mir.insns[16].secondary_offset) != 2 ||
        (mir.insns[16].secondary_offset & TYPE_UNSIGNED) != 0 ||
        mir.insns[16].src1 != index_phi->dst ||
        mir.insns[16].src2 != count->dst ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        mir.insns[17].label != mir.insns[33].label ||
        mir.insns[21].src1 != array->dst ||
        mir.insns[21].src2 != index_phi->dst ||
        mir.insns[21].immediate != 1 ||
        mir.insns[21].memory_size != 1 ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        mir.insns[22].memory_size != 1 ||
        (mir.insns[22].type & TYPE_UNSIGNED) == 0 ||
        (mir.insns[22].memory_flags & (1 | 8)) != 0 ||
        mir.insns[23].immediate != 0 ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        (mir.insns[23].type & 15) != TYPE_INT ||
        (mir.insns[23].type & TYPE_UNSIGNED) != 0 ||
        type_size(mir.insns[23].type) != 2 ||
        mir.insns[24].immediate != '+' ||
        type_size(mir.insns[24].secondary_offset) != 2 ||
        (mir.insns[24].secondary_offset & TYPE_UNSIGNED) != 0 ||
        mir.insns[24].src1 != sum_phi->dst ||
        mir.insns[24].src2 != mir.insns[23].dst ||
        !mir_machine_same_location(&mir.insns[5], &mir.insns[26]) ||
        mir.insns[26].memory_size != 2 ||
        mir.insns[26].src1 != mir.insns[24].dst ||
        !mir_machine_constant_equals(mir.insns[29].dst, 1) ||
        mir.insns[30].immediate != '+' ||
        type_size(mir.insns[30].secondary_offset) != 2 ||
        (mir.insns[30].secondary_offset & TYPE_UNSIGNED) != 0 ||
        mir.insns[30].src1 != index_phi->dst ||
        mir.insns[30].src2 != mir.insns[29].dst ||
        !mir_machine_same_location(&mir.insns[8], &mir.insns[31]) ||
        mir.insns[31].memory_size != 2 ||
        mir.insns[31].src1 != mir.insns[30].dst ||
        mir.insns[32].label != mir.insns[9].label ||
        mir.insns[35].src1 != sum_phi->dst)
        return 0;
    return 1;
}

static int mir_match_wraparound_bool_step(
    struct MirWraparoundBoolStep *plan)
{
    static const int expected_opcodes[94] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_CONST,
        MIR_STORE, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_NOP, MIR_BINARY, MIR_NOP,
        MIR_BINARY, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_STORE,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_NOP,
        MIR_BINARY, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_STORE,
        MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP, MIR_NOP, MIR_BINARY,
        MIR_UNARY, MIR_STORE_INDIRECT, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_RETURN
    };
    const struct MirInsn *count = &mir.insns[1];
    const struct MirInsn *current = &mir.insns[2];
    const struct MirInsn *next = &mir.insns[3];
    const struct MirInsn *index_phi = &mir.insns[37];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 94 || mir_cfg_block_count() != 5 ||
        type_ptr_depth(mir.return_type) != 0 ||
        type_is_float(mir.return_type) ||
        type_size(mir.return_type) != 4)
        return mir_machine_reject("wraparound-bool-step", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "wraparound-bool-step", "opcode");
    if (type_ptr_depth(count->type) != 0 ||
        (count->type & 15) != TYPE_INT ||
        (count->type & TYPE_UNSIGNED) != 0 ||
        type_size(count->type) != 2 ||
        type_ptr_depth(current->type) != 1 ||
        (current->type & 15) != TYPE_BOOL ||
        type_size(current->type) != 2 ||
        type_ptr_depth(next->type) != 1 ||
        (next->type & 15) != TYPE_BOOL ||
        type_size(next->type) != 2 ||
        mir_machine_pointee_is_volatile(current) ||
        mir_machine_pointee_is_volatile(next) ||
        !mir_machine_parameter_value_offset(
            count->dst, &plan->count_stack_offset) ||
        !mir_machine_parameter_value_offset(
            current->dst, &plan->current_stack_offset) ||
        !mir_machine_parameter_value_offset(
            next->dst, &plan->next_stack_offset))
        return mir_machine_reject(
            "wraparound-bool-step", "parameters");
    if (!mir_machine_constant_equals(mir.insns[5].dst, 0) ||
        type_size(mir.insns[5].type) != 4 ||
        !mir_machine_unobservable_local_store(&mir.insns[6]) ||
        mir.insns[6].memory_size != 4 ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        !mir_machine_constant_equals(mir.insns[30].dst, 0) ||
        type_size(mir.insns[30].type) != 2 ||
        !mir_machine_unobservable_local_store(&mir.insns[31]) ||
        mir.insns[31].memory_size != 2 ||
        mir.insns[31].src1 != mir.insns[30].dst)
        return mir_machine_reject(
            "wraparound-bool-step", "initializers");
    if (index_phi->src1 != mir.insns[30].dst ||
        index_phi->src2 != mir.insns[88].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[85].label ||
        type_size(index_phi->type) != 2 ||
        (index_phi->type & TYPE_UNSIGNED) != 0 ||
        mir.insns[42].immediate != '<' ||
        mir.insns[42].src1 != index_phi->dst ||
        mir.insns[42].src2 != count->dst ||
        type_size(mir.insns[42].secondary_offset) != 2 ||
        (mir.insns[42].secondary_offset & TYPE_UNSIGNED) != 0 ||
        mir.insns[43].src1 != mir.insns[42].dst ||
        mir.insns[43].label != mir.insns[91].label)
        return mir_machine_reject(
            "wraparound-bool-step", "loop");
    if (!mir_machine_constant_equals(mir.insns[47].dst, 1) ||
        mir.insns[48].immediate != '-' ||
        mir.insns[48].src1 != index_phi->dst ||
        mir.insns[48].src2 != mir.insns[47].dst ||
        mir.insns[50].immediate != '+' ||
        mir.insns[50].src1 != mir.insns[48].dst ||
        mir.insns[50].src2 != count->dst ||
        mir.insns[52].immediate != '%' ||
        mir.insns[52].src1 != mir.insns[50].dst ||
        mir.insns[52].src2 != count->dst ||
        mir.insns[53].src1 != current->dst ||
        mir.insns[53].src2 != mir.insns[52].dst ||
        mir.insns[53].immediate != 1 ||
        mir.insns[53].memory_size != 1 ||
        mir.insns[54].src1 != mir.insns[53].dst ||
        mir.insns[54].memory_size != 1 ||
        (mir.insns[54].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_unobservable_local_store(&mir.insns[55]) ||
        mir.insns[55].memory_size != 1 ||
        mir.insns[55].src1 != mir.insns[54].dst)
        return mir_machine_reject(
            "wraparound-bool-step", "left");
    if (!mir_machine_constant_equals(mir.insns[59].dst, 1) ||
        mir.insns[60].immediate != '+' ||
        mir.insns[60].src1 != index_phi->dst ||
        mir.insns[60].src2 != mir.insns[59].dst ||
        mir.insns[62].immediate != '%' ||
        mir.insns[62].src1 != mir.insns[60].dst ||
        mir.insns[62].src2 != count->dst ||
        mir.insns[63].src1 != current->dst ||
        mir.insns[63].src2 != mir.insns[62].dst ||
        mir.insns[63].immediate != 1 ||
        mir.insns[63].memory_size != 1 ||
        mir.insns[64].src1 != mir.insns[63].dst ||
        mir.insns[64].memory_size != 1 ||
        (mir.insns[64].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_unobservable_local_store(&mir.insns[65]) ||
        mir.insns[65].memory_size != 1 ||
        mir.insns[65].src1 != mir.insns[64].dst)
        return mir_machine_reject(
            "wraparound-bool-step", "right");
    if (mir.insns[68].src1 != next->dst ||
        mir.insns[68].src2 != index_phi->dst ||
        mir.insns[68].immediate != 1 ||
        mir.insns[68].memory_size != 1 ||
        mir.insns[71].immediate != '^' ||
        ((mir.insns[71].src1 != mir.insns[54].dst ||
          mir.insns[71].src2 != mir.insns[64].dst) &&
         (mir.insns[71].src1 != mir.insns[64].dst ||
          mir.insns[71].src2 != mir.insns[54].dst)) ||
        mir.insns[72].immediate != 0 ||
        mir.insns[72].src1 != mir.insns[71].dst ||
        mir.insns[73].src1 != mir.insns[68].dst ||
        mir.insns[73].src2 != mir.insns[72].dst ||
        mir.insns[73].memory_size != 1 ||
        (mir.insns[73].memory_flags & (1 | 8)) != 0 ||
        mir.insns[76].src1 != next->dst ||
        mir.insns[76].src2 != index_phi->dst ||
        mir.insns[76].immediate != 1 ||
        mir.insns[76].memory_size != 1 ||
        mir.insns[77].src1 != mir.insns[76].dst ||
        mir.insns[77].memory_size != 1 ||
        (mir.insns[77].memory_flags & (1 | 8)) != 0 ||
        mir.insns[78].src1 != mir.insns[77].dst ||
        mir.insns[78].label != mir.insns[83].label)
        return mir_machine_reject(
            "wraparound-bool-step", "store");
    if (mir.insns[6].object < 0 ||
        mir.insns[79].object != mir.insns[6].object ||
        type_size(mir.insns[79].type) != 4 ||
        (mir.insns[79].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject(
            "wraparound-bool-step", "live-load");
    if (!mir_machine_constant_equals(mir.insns[80].dst, 1) ||
        type_size(mir.insns[80].type) != 4)
        return mir_machine_reject(
            "wraparound-bool-step", "live-one");
    if (mir.insns[81].immediate != '+' ||
        mir.insns[81].src1 != mir.insns[79].dst ||
        mir.insns[81].src2 != mir.insns[80].dst ||
        type_size(mir.insns[81].type) != 4)
        return mir_machine_reject(
            "wraparound-bool-step", "live-add");
    if (mir.insns[82].object != mir.insns[6].object ||
        mir.insns[82].memory_size != 4 ||
        (mir.insns[82].memory_flags & (1 | 8)) != 0 ||
        mir.insns[82].src1 != mir.insns[81].dst)
        return mir_machine_reject(
            "wraparound-bool-step", "live-store");
    if (
        !mir_machine_constant_equals(mir.insns[87].dst, 1) ||
        mir.insns[88].immediate != '+' ||
        mir.insns[88].src1 != index_phi->dst ||
        mir.insns[88].src2 != mir.insns[87].dst ||
        mir.insns[31].object < 0 ||
        mir.insns[89].object != mir.insns[31].object ||
        mir.insns[89].memory_size != 2 ||
        (mir.insns[89].memory_flags & (1 | 8)) != 0 ||
        mir.insns[89].src1 != mir.insns[88].dst ||
        mir.insns[90].label != mir.insns[32].label)
        return mir_machine_reject(
            "wraparound-bool-step", "index-update");
    if (
        mir.insns[92].object != mir.insns[6].object ||
        type_size(mir.insns[92].type) != 4 ||
        (mir.insns[92].memory_flags & (1 | 8)) != 0 ||
        mir.insns[93].src1 != mir.insns[92].dst)
        return mir_machine_reject(
            "wraparound-bool-step", "result");
    return 1;
}

static int mir_match_fixed_row_word_sum(struct MirFixedRowWordSum *plan)
{
    static const int expected_opcodes[56] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_CONST, MIR_STORE, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP, MIR_PHI,
        MIR_NOP, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP,
        MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_LOAD, MIR_RETURN
    };
    const struct MirInsn *rows = &mir.insns[1];
    const struct MirInsn *array = &mir.insns[2];
    const struct MirInsn *outer_phi = &mir.insns[12];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 56 || mir_cfg_block_count() != 7 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_INT ||
        (mir.return_type & TYPE_UNSIGNED) != 0 ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    if ((rows->type & 15) != TYPE_INT ||
        (rows->type & TYPE_UNSIGNED) != 0 ||
        type_ptr_depth(rows->type) != 0 ||
        type_size(rows->type) != 2 ||
        (array->type & 15) != TYPE_INT ||
        type_ptr_depth(array->type) != 1 ||
        type_size(array->type) != 2 ||
        !mir_machine_parameter_value_offset(
            rows->dst, &plan->rows_stack_offset) ||
        !mir_machine_parameter_value_offset(
            array->dst, &plan->array_stack_offset) ||
        plan->rows_stack_offset != 2 ||
        plan->array_stack_offset != 4)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[4]) ||
        mir.insns[4].memory_size != 2 ||
        mir.insns[4].src1 != mir.insns[3].dst ||
        !mir_machine_constant_equals(mir.insns[5].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[7]) ||
        mir.insns[7].memory_size != 2 ||
        mir.insns[7].src1 != mir.insns[5].dst ||
        (outer_phi->type & 15) != TYPE_INT ||
        (outer_phi->type & TYPE_UNSIGNED) != 0 ||
        type_ptr_depth(outer_phi->type) != 0 ||
        type_size(outer_phi->type) != 2 ||
        outer_phi->src1 != mir.insns[5].dst ||
        outer_phi->src2 != mir.insns[50].dst ||
        outer_phi->phi_pred1 != mir.insns[0].label ||
        outer_phi->phi_pred2 != mir.insns[47].label)
        return 0;
    if (mir.insns[15].immediate != '<' ||
        mir.insns[15].src1 != outer_phi->dst ||
        mir.insns[15].src2 != rows->dst ||
        type_size(mir.insns[15].secondary_offset) != 2 ||
        (mir.insns[15].secondary_offset & TYPE_UNSIGNED) != 0 ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[16].label != mir.insns[53].label ||
        !mir_machine_constant_equals(mir.insns[17].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[19]) ||
        mir.insns[19].memory_size != 2 ||
        mir.insns[19].src1 != mir.insns[17].dst)
        return 0;
    if (!mir_machine_same_location(&mir.insns[19], &mir.insns[26]) ||
        !mir_machine_named_nonvolatile(&mir.insns[26]) ||
    (mir.insns[26].type & 15) != TYPE_INT ||
    (mir.insns[26].type & TYPE_UNSIGNED) != 0 ||
    type_ptr_depth(mir.insns[26].type) != 0 ||
    type_size(mir.insns[26].type) != 2 ||
        !mir_machine_constant_equals(mir.insns[27].dst, 3) ||
        mir.insns[28].immediate != '<' ||
        mir.insns[28].src1 != mir.insns[26].dst ||
        mir.insns[28].src2 != mir.insns[27].dst ||
        type_size(mir.insns[28].secondary_offset) != 2 ||
        (mir.insns[28].secondary_offset & TYPE_UNSIGNED) != 0 ||
        mir.insns[29].src1 != mir.insns[28].dst ||
        mir.insns[29].label != mir.insns[46].label ||
        !mir_machine_unobservable_local_store(&mir.insns[39]) ||
        !mir_machine_same_location(&mir.insns[4], &mir.insns[30]) ||
        (mir.insns[30].type & 15) != TYPE_INT ||
        (mir.insns[30].type & TYPE_UNSIGNED) != 0 ||
        type_ptr_depth(mir.insns[30].type) != 0 ||
        type_size(mir.insns[30].type) != 2 ||
        mir.insns[33].src1 != array->dst ||
        mir.insns[33].src2 != outer_phi->dst ||
        mir.insns[33].immediate != 6 ||
        mir.insns[33].memory_size != 2 ||
        !mir_machine_same_location(&mir.insns[19], &mir.insns[34]) ||
        mir.insns[35].src1 != mir.insns[33].dst ||
        mir.insns[35].src2 != mir.insns[34].dst ||
        mir.insns[35].immediate != 2 ||
        mir.insns[35].memory_size != 2 ||
        mir.insns[36].src1 != mir.insns[35].dst ||
        mir.insns[36].memory_size != 2 ||
        (mir.insns[36].type & 15) != TYPE_INT ||
        (mir.insns[36].type & TYPE_UNSIGNED) != 0 ||
        type_ptr_depth(mir.insns[36].type) != 0 ||
        (mir.insns[36].memory_flags & (1 | 8)) != 0 ||
        mir.insns[37].immediate != '+' ||
        type_size(mir.insns[37].secondary_offset) != 2 ||
        (mir.insns[37].secondary_offset & TYPE_UNSIGNED) != 0 ||
        mir.insns[37].src1 != mir.insns[30].dst ||
        mir.insns[37].src2 != mir.insns[36].dst ||
        mir.insns[39].src1 != mir.insns[37].dst ||
        mir.insns[39].memory_size != 2 ||
        !mir_machine_same_location(&mir.insns[4], &mir.insns[39]))
        return 0;
    if (!mir_machine_same_location(&mir.insns[19], &mir.insns[41]) ||
        !mir_machine_constant_equals(mir.insns[42].dst, 1) ||
        mir.insns[43].immediate != '+' ||
        type_size(mir.insns[43].secondary_offset) != 2 ||
        (mir.insns[43].secondary_offset & TYPE_UNSIGNED) != 0 ||
        mir.insns[43].src1 != mir.insns[41].dst ||
        mir.insns[43].src2 != mir.insns[42].dst ||
        !mir_machine_same_location(&mir.insns[19], &mir.insns[44]) ||
        mir.insns[44].src1 != mir.insns[43].dst ||
        mir.insns[44].memory_size != 2 ||
        mir.insns[45].label != mir.insns[20].label ||
        !mir_machine_constant_equals(mir.insns[49].dst, 1) ||
        mir.insns[50].immediate != '+' ||
        type_size(mir.insns[50].secondary_offset) != 2 ||
        (mir.insns[50].secondary_offset & TYPE_UNSIGNED) != 0 ||
        mir.insns[50].src1 != outer_phi->dst ||
        mir.insns[50].src2 != mir.insns[49].dst ||
        !mir_machine_same_location(&mir.insns[7], &mir.insns[51]) ||
        mir.insns[51].src1 != mir.insns[50].dst ||
        mir.insns[51].memory_size != 2 ||
        mir.insns[52].label != mir.insns[8].label ||
        !mir_machine_same_location(&mir.insns[4], &mir.insns[54]) ||
        (mir.insns[54].type & 15) != TYPE_INT ||
        (mir.insns[54].type & TYPE_UNSIGNED) != 0 ||
        type_ptr_depth(mir.insns[54].type) != 0 ||
        mir.insns[55].src1 != mir.insns[54].dst)
        return 0;
    return 1;
}

static int mir_match_fixed_wide_zero(struct MirFixedWideZero *plan)
{
    static const int expected_opcodes[42] = {
        MIR_LABEL, MIR_PARAM, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_PHI, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_RETURN, MIR_LABEL, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_RETURN
    };
    const struct MirInsn *parameter = &mir.insns[1];
    const struct MirInsn *index_phi = &mir.insns[7];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 42 || mir_cfg_block_count() != 5 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_INT ||
        type_ptr_depth(mir.return_type) != 0)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    if (type_ptr_depth(parameter->type) != 1 ||
        (parameter->type & 15) != TYPE_LONG ||
        mir_machine_pointee_is_volatile(parameter) ||
        !mir_machine_parameter_value_offset(
            parameter->dst, &plan->parameter_stack_offset) ||
        plan->parameter_stack_offset != 2 ||
        !mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[4]) ||
        mir.insns[4].memory_size != 2 ||
        mir.insns[4].src1 != mir.insns[2].dst)
        return 0;
    if ((index_phi->type & 15) != TYPE_INT ||
        (index_phi->type & TYPE_UNSIGNED) != 0 ||
        index_phi->src1 != mir.insns[2].dst ||
        index_phi->src2 != mir.insns[36].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[33].label ||
        mir.insns[20].immediate != '<' ||
        mir.insns[20].src1 != index_phi->dst ||
        mir.insns[20].src2 != mir.insns[19].dst ||
        mir.insns[21].src1 != mir.insns[20].dst ||
        mir.insns[21].label != mir.insns[39].label ||
        mir.insns[24].src1 != parameter->dst ||
        mir.insns[24].src2 != index_phi->dst ||
        mir.insns[24].immediate != 4 ||
        mir.insns[24].memory_size != 4 ||
        mir.insns[25].src1 != mir.insns[24].dst ||
        mir.insns[25].memory_size != 4 ||
        (mir.insns[25].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_constant_equals(mir.insns[27].dst, 0) ||
        mir.insns[28].immediate != TOK_NE ||
        mir.insns[28].src1 != mir.insns[25].dst ||
        mir.insns[28].src2 != mir.insns[27].dst ||
        mir.insns[29].src1 != mir.insns[28].dst ||
        mir.insns[29].label != mir.insns[32].label ||
        !mir_machine_constant_equals(mir.insns[30].dst, 0) ||
        mir.insns[31].src1 != mir.insns[30].dst ||
        !mir_machine_constant_equals(mir.insns[35].dst, 1) ||
        mir.insns[36].immediate != '+' ||
        mir.insns[36].src1 != index_phi->dst ||
        mir.insns[36].src2 != mir.insns[35].dst ||
        !mir_machine_same_location(&mir.insns[4], &mir.insns[37]) ||
        mir.insns[37].src1 != mir.insns[36].dst ||
        mir.insns[38].label != mir.insns[5].label ||
        !mir_machine_constant_equals(mir.insns[40].dst, 1) ||
        mir.insns[41].src1 != mir.insns[40].dst)
        return 0;
    if (mir.insns[19].immediate <= 0 ||
        mir.insns[19].immediate > 255)
        return 0;
    plan->count = (int)mir.insns[19].immediate;
    return 1;
}

static int mir_match_constant_byte_fill(struct MirConstantByteFill *plan)
{
    static const int expected_opcodes[26] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_PHI, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP,
        MIR_CONST, MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL
    };
    const struct MirInsn *parameter = &mir.insns[1];
    const struct MirInsn *index_phi = &mir.insns[7];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 26 || mir_cfg_block_count() != 4 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    if (type_ptr_depth(parameter->type) != 1 ||
        (parameter->type & 15) != TYPE_CHAR ||
        mir_machine_pointee_is_volatile(parameter) ||
        !mir_machine_parameter_value_offset(
            parameter->dst, &plan->parameter_stack_offset) ||
        plan->parameter_stack_offset != 2 ||
        !mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[4]) ||
        mir.insns[4].memory_size != 1 ||
        mir.insns[4].src1 != mir.insns[3].dst)
        return 0;
    if ((index_phi->type & 15) != TYPE_CHAR ||
        (index_phi->type & TYPE_UNSIGNED) == 0 ||
        index_phi->src1 != mir.insns[3].dst ||
        index_phi->src2 != mir.insns[22].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[19].label ||
        mir.insns[10].immediate != 0 ||
        mir.insns[10].src1 != index_phi->dst ||
        (mir.insns[10].type & 15) != TYPE_INT ||
        (mir.insns[10].type & TYPE_UNSIGNED) != 0 ||
        type_ptr_depth(mir.insns[10].type) != 0 ||
        type_size(mir.insns[10].type) != 2 ||
        mir.insns[11].immediate != '<' ||
        (mir.insns[11].secondary_offset & 15) != TYPE_INT ||
        (mir.insns[11].secondary_offset & TYPE_UNSIGNED) != 0 ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[11].src2 != mir.insns[9].dst ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[12].label != mir.insns[25].label ||
        mir.insns[15].src1 != parameter->dst ||
        mir.insns[15].src2 != index_phi->dst ||
        mir.insns[15].immediate != 1 ||
        mir.insns[15].memory_size != 1 ||
        mir.insns[18].src1 != mir.insns[15].dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        mir.insns[18].memory_size != 1 ||
        (mir.insns[18].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_constant_equals(mir.insns[21].dst, 1) ||
        (mir.insns[21].type & TYPE_UNSIGNED) == 0 ||
        type_size(mir.insns[21].type) != 1 ||
        mir.insns[22].immediate != '+' ||
        mir.insns[22].type != index_phi->type ||
        mir.insns[22].secondary_offset != index_phi->type ||
        mir.insns[22].src1 != index_phi->dst ||
        mir.insns[22].src2 != mir.insns[21].dst ||
        !mir_machine_same_location(&mir.insns[4], &mir.insns[23]) ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[23].memory_size != 1 ||
        mir.insns[24].label != mir.insns[5].label)
        return 0;
    if (mir.insns[9].immediate <= 0 || mir.insns[9].immediate > 255 ||
        (mir.insns[9].type & 15) != TYPE_INT ||
        (mir.insns[9].type & TYPE_UNSIGNED) != 0 ||
        type_size(mir.insns[17].type) != 1 ||
        mir.insns[17].immediate < -128 ||
        mir.insns[17].immediate > 255)
        return 0;
    plan->count = (int)mir.insns[9].immediate;
    plan->value = (int)mir.insns[17].immediate & 255;
    return 1;
}

static int mir_match_affine_byte_fill(struct MirAffineByteFill *plan)
{
    static const int expected_opcodes[31] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_NOP, MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_UNARY,
        MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL
    };
    const struct MirInsn *pointer = &mir.insns[1];
    const struct MirInsn *base = &mir.insns[2];
    const struct MirInsn *index_phi = &mir.insns[9];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 31 || mir_cfg_block_count() != 4 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (type_ptr_depth(pointer->type) != 1 ||
        (pointer->type & 15) != TYPE_CHAR ||
        mir_machine_pointee_is_volatile(pointer) ||
        (base->type & 15) != TYPE_INT ||
        type_ptr_depth(base->type) != 0 ||
        !mir_machine_parameter_value_offset(
            pointer->dst, &plan->pointer_stack_offset) ||
        !mir_machine_parameter_value_offset(
            base->dst, &plan->base_stack_offset) ||
        plan->pointer_stack_offset != 2 ||
        plan->base_stack_offset != 4 ||
        !mir_machine_constant_equals(mir.insns[4].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[5]) ||
        mir.insns[5].memory_size != 1)
        return 0;
    if ((index_phi->type & TYPE_UNSIGNED) == 0 ||
        type_size(index_phi->type) != 1 ||
        index_phi->src1 != mir.insns[4].dst ||
        index_phi->src2 != mir.insns[27].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[24].label ||
        mir.insns[12].immediate != 0 ||
        mir.insns[12].src1 != index_phi->dst ||
        mir.insns[13].immediate != '<' ||
        mir.insns[13].src1 != mir.insns[12].dst ||
        mir.insns[13].src2 != mir.insns[11].dst ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        mir.insns[14].label != mir.insns[30].label ||
        mir.insns[17].src1 != pointer->dst ||
        mir.insns[17].src2 != index_phi->dst ||
        mir.insns[17].immediate != 1 ||
        mir.insns[17].memory_size != 1 ||
        mir.insns[20].immediate != 0 ||
        mir.insns[20].src1 != index_phi->dst ||
        mir.insns[21].immediate != '+' ||
        mir.insns[21].src1 != base->dst ||
        mir.insns[21].src2 != mir.insns[20].dst ||
        mir.insns[22].immediate != 0 ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        type_size(mir.insns[22].type) != 1 ||
        mir.insns[23].src1 != mir.insns[17].dst ||
        mir.insns[23].src2 != mir.insns[22].dst ||
        mir.insns[23].memory_size != 1 ||
        (mir.insns[23].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_constant_equals(mir.insns[26].dst, 1) ||
        mir.insns[27].immediate != '+' ||
        mir.insns[27].src1 != index_phi->dst ||
        mir.insns[27].src2 != mir.insns[26].dst ||
        !mir_machine_same_location(&mir.insns[5], &mir.insns[28]) ||
        mir.insns[28].src1 != mir.insns[27].dst ||
        mir.insns[29].label != mir.insns[6].label)
        return 0;
    if (mir.insns[11].immediate <= 0 || mir.insns[11].immediate > 255)
        return 0;
    plan->count = (int)mir.insns[11].immediate;
    plan->value_from_parameter = 1;
    plan->step = 1;
    return 1;
}

static int mir_match_scaled_byte_fill(struct MirAffineByteFill *plan)
{
    static const int expected_opcodes[29] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_PHI, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_STORE_INDIRECT,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL
    };
    const struct MirInsn *parameter;
    const struct MirInsn *index_phi;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 29 || mir_cfg_block_count() != 4 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    parameter = &mir.insns[1];
    index_phi = &mir.insns[7];
    if (type_ptr_depth(parameter->type) != 1 ||
        (parameter->type & 15) != TYPE_CHAR ||
        mir_machine_pointee_is_volatile(parameter) ||
        !mir_machine_parameter_value_offset(
            parameter->dst, &plan->pointer_stack_offset) ||
        !mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[4]) ||
        mir.insns[4].memory_size != 1)
        return 0;
    if (index_phi->src1 != mir.insns[3].dst ||
        index_phi->src2 != mir.insns[25].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[22].label ||
        (index_phi->type & TYPE_UNSIGNED) == 0 ||
        type_size(index_phi->type) != 1 ||
        mir.insns[10].immediate != 0 ||
        mir.insns[10].src1 != index_phi->dst ||
        mir.insns[11].immediate != '<' ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[11].src2 != mir.insns[9].dst ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[12].label != mir.insns[28].label)
        return 0;
    if (mir.insns[15].src1 != parameter->dst ||
        mir.insns[15].src2 != index_phi->dst ||
        mir.insns[15].immediate != 1 ||
        mir.insns[15].memory_size != 1 ||
        mir.insns[18].immediate != 0 ||
        mir.insns[18].src1 != index_phi->dst ||
        mir.insns[19].immediate != '*' ||
        mir.insns[19].src1 != mir.insns[18].dst ||
        mir.insns[19].src2 != mir.insns[17].dst ||
        mir.insns[20].immediate != 0 ||
        mir.insns[20].src1 != mir.insns[19].dst ||
        type_size(mir.insns[20].type) != 1 ||
        mir.insns[21].src1 != mir.insns[15].dst ||
        mir.insns[21].src2 != mir.insns[20].dst ||
        mir.insns[21].memory_size != 1 ||
        (mir.insns[21].memory_flags & (1 | 8)) != 0)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[24].dst, 1) ||
        mir.insns[25].immediate != '+' ||
        mir.insns[25].src1 != index_phi->dst ||
        mir.insns[25].src2 != mir.insns[24].dst ||
        !mir_machine_same_location(&mir.insns[4], &mir.insns[26]) ||
        mir.insns[26].src1 != mir.insns[25].dst ||
        mir.insns[27].label != mir.insns[5].label ||
        mir.insns[9].immediate <= 0 ||
        mir.insns[9].immediate > 255 ||
        mir.insns[17].immediate <= 0 ||
        mir.insns[17].immediate > 255)
        return 0;
    plan->count = (int)mir.insns[9].immediate;
    plan->initial_value = 0;
    plan->step = (int)mir.insns[17].immediate;
    return 1;
}

static int mir_match_wide_left_shift_count(void)
{
    static const int expected_opcodes[30] = {
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_STORE, MIR_CONST, MIR_STORE,
        MIR_LABEL, MIR_PHI, MIR_PHI, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_RETURN
    };
    const struct MirInsn *wide_phi;
    const struct MirInsn *count_phi;
    int instruction;

    if (mir.count != 30 || mir_cfg_block_count() != 4 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    wide_phi = &mir.insns[7];
    count_phi = &mir.insns[8];
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (!mir_machine_constant_equals(mir.insns[2].dst, 1) ||
        type_ptr_depth(mir.insns[2].type) != 0 ||
        (mir.insns[2].type & 15) != TYPE_LONG ||
        (mir.insns[2].type & TYPE_UNSIGNED) == 0 ||
        type_size(mir.insns[2].type) != 4 ||
        !mir_machine_unobservable_local_store(&mir.insns[3]) ||
        mir.insns[3].memory_size != 4 ||
        !mir_machine_constant_equals(mir.insns[4].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[5]) ||
        mir.insns[5].memory_size != 2)
        return 0;
    if (wide_phi->src1 != mir.insns[2].dst ||
        wide_phi->src2 != mir.insns[21].dst ||
        wide_phi->phi_pred1 != mir.insns[0].label ||
        wide_phi->phi_pred2 != mir.insns[25].label ||
        wide_phi->type != mir.insns[2].type ||
        count_phi->src1 != mir.insns[4].dst ||
        count_phi->src2 != mir.insns[17].dst ||
        count_phi->phi_pred1 != mir.insns[0].label ||
        count_phi->phi_pred2 != mir.insns[25].label ||
        type_ptr_depth(count_phi->type) != 0 ||
        (count_phi->type & 15) != TYPE_INT ||
        type_size(count_phi->type) != 2)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[12].dst, 0) ||
        mir.insns[12].type != wide_phi->type ||
        mir.insns[13].immediate != TOK_NE ||
        mir.insns[13].src1 != mir.insns[12].dst ||
        mir.insns[13].src2 != wide_phi->dst ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        mir.insns[14].label != mir.insns[27].label ||
        !mir_machine_constant_equals(mir.insns[16].dst, 1) ||
        mir.insns[17].immediate != '+' ||
        mir.insns[17].src1 != count_phi->dst ||
        mir.insns[17].src2 != mir.insns[16].dst ||
        !mir_machine_same_location(&mir.insns[5], &mir.insns[18]) ||
        mir.insns[18].src1 != mir.insns[17].dst)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[20].dst, 1) ||
        mir.insns[20].type != wide_phi->type ||
        mir.insns[21].immediate != TOK_SHL ||
        mir.insns[21].src1 != wide_phi->dst ||
        mir.insns[21].src2 != mir.insns[20].dst ||
        mir.insns[21].type != wide_phi->type ||
        !mir_machine_same_location(&mir.insns[3], &mir.insns[23]) ||
        mir.insns[23].src1 != mir.insns[21].dst ||
        mir.insns[26].label != mir.insns[6].label ||
        mir.insns[29].src1 != count_phi->dst)
        return 0;
    return 1;
}

static int mir_match_palindrome_scan(struct MirPalindromeScan *plan)
{
    static const int expected_opcodes[64] = {
        MIR_LABEL, MIR_PARAM, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_NOP,
        MIR_PHI, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_CONST, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_PHI,
        MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_NOP, MIR_BINARY, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_UNARY, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_CONST, MIR_RETURN, MIR_LABEL, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_RETURN
    };
    const struct MirInsn *parameter;
    const struct MirInsn *length_phi;
    const struct MirInsn *index_phi;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 64 || mir_cfg_block_count() != 8 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_BOOL)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    parameter = &mir.insns[1];
    length_phi = &mir.insns[6];
    index_phi = &mir.insns[27];
    if (type_ptr_depth(parameter->type) != 1 ||
        (parameter->type & 15) != TYPE_CHAR ||
        mir_machine_pointee_is_volatile(parameter) ||
        !mir_machine_parameter_value_offset(
            parameter->dst, &plan->parameter_stack_offset) ||
        !mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[3]) ||
        mir.insns[3].memory_size != 2)
        return 0;
    if (length_phi->src1 != mir.insns[2].dst ||
        length_phi->src2 != mir.insns[14].dst ||
        length_phi->phi_pred1 != mir.insns[0].label ||
        length_phi->phi_pred2 != mir.insns[16].label ||
        type_ptr_depth(length_phi->type) != 0 ||
        (length_phi->type & 15) != TYPE_INT ||
        type_size(length_phi->type) != 2 ||
        mir.insns[9].src1 != parameter->dst ||
        mir.insns[9].src2 != length_phi->dst ||
        mir.insns[9].immediate != 1 ||
        mir.insns[9].memory_size != 1 ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[10].memory_size != 1 ||
        (mir.insns[10].memory_flags & (1 | 8)) != 0 ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[11].label != mir.insns[18].label)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[13].dst, 1) ||
        mir.insns[14].immediate != '+' ||
        mir.insns[14].src1 != length_phi->dst ||
        mir.insns[14].src2 != mir.insns[13].dst ||
        !mir_machine_same_location(&mir.insns[3], &mir.insns[15]) ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        mir.insns[17].label != mir.insns[4].label ||
        !mir_machine_constant_equals(mir.insns[22].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[23]) ||
        mir.insns[23].memory_size != 2)
        return 0;
    if (index_phi->src1 != mir.insns[22].dst ||
        index_phi->src2 != mir.insns[57].dst ||
        index_phi->phi_pred1 != mir.insns[18].label ||
        index_phi->phi_pred2 != mir.insns[54].label ||
        type_ptr_depth(index_phi->type) != 0 ||
        (index_phi->type & 15) != TYPE_INT ||
        type_size(index_phi->type) != 2 ||
        !mir_machine_constant_equals(mir.insns[30].dst, 2) ||
        mir.insns[31].immediate != '/' ||
        mir.insns[31].src1 != length_phi->dst ||
        mir.insns[31].src2 != mir.insns[30].dst ||
        mir.insns[32].immediate != '<' ||
        mir.insns[32].src1 != index_phi->dst ||
        mir.insns[32].src2 != mir.insns[31].dst ||
        mir.insns[33].src1 != mir.insns[32].dst ||
        mir.insns[33].label != mir.insns[60].label)
        return 0;
    if (mir.insns[36].src1 != parameter->dst ||
        mir.insns[36].src2 != index_phi->dst ||
        mir.insns[36].immediate != 1 ||
        mir.insns[36].memory_size != 1 ||
        mir.insns[37].src1 != mir.insns[36].dst ||
        mir.insns[37].memory_size != 1 ||
        (mir.insns[37].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_constant_equals(mir.insns[40].dst, 1) ||
        mir.insns[41].immediate != '-' ||
        mir.insns[41].src1 != length_phi->dst ||
        mir.insns[41].src2 != mir.insns[40].dst ||
        mir.insns[43].immediate != '-' ||
        mir.insns[43].src1 != mir.insns[41].dst ||
        mir.insns[43].src2 != index_phi->dst)
        return 0;
    if (mir.insns[44].src1 != parameter->dst ||
        mir.insns[44].src2 != mir.insns[43].dst ||
        mir.insns[44].immediate != 1 ||
        mir.insns[44].memory_size != 1 ||
        mir.insns[45].src1 != mir.insns[44].dst ||
        mir.insns[45].memory_size != 1 ||
        (mir.insns[45].memory_flags & (1 | 8)) != 0 ||
        mir.insns[46].immediate != 0 ||
        mir.insns[46].src1 != mir.insns[37].dst ||
        mir.insns[47].immediate != 0 ||
        mir.insns[47].src1 != mir.insns[45].dst ||
        mir.insns[48].immediate != TOK_NE ||
        mir.insns[48].src1 != mir.insns[46].dst ||
        mir.insns[48].src2 != mir.insns[47].dst ||
        mir.insns[49].src1 != mir.insns[48].dst ||
        mir.insns[49].label != mir.insns[53].label)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[51].dst, 0) ||
        mir.insns[52].src1 != mir.insns[51].dst ||
        !mir_machine_constant_equals(mir.insns[56].dst, 1) ||
        mir.insns[57].immediate != '+' ||
        mir.insns[57].src1 != index_phi->dst ||
        mir.insns[57].src2 != mir.insns[56].dst ||
        !mir_machine_same_location(&mir.insns[23], &mir.insns[58]) ||
        mir.insns[58].src1 != mir.insns[57].dst ||
        mir.insns[59].label != mir.insns[24].label ||
        !mir_machine_constant_equals(mir.insns[62].dst, 1) ||
        mir.insns[63].src1 != mir.insns[62].dst)
        return 0;
    return 1;
}

static int mir_match_dynamic_row_scan(struct MirDynamicRowScan *plan)
{
    static const int expected_opcodes[35] = {
        MIR_LABEL, MIR_CONST, MIR_STORE, MIR_ADDRESS, MIR_NOP, MIR_STORE,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_LOAD, MIR_PHI,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_ADDRESS, MIR_LOAD,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_RETURN
    };
    const struct MirInsn *column_phi;
    const struct MirInsn *table_address;
    int address_offset;
    int address_storage;
    int address_type;
    int instruction;
    int row_offset;
    int row_storage;
    int row_type;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 35 || mir_cfg_block_count() != 4 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return mir_machine_reject("dynamic-row-scan", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject("dynamic-row-scan", "opcode");
    column_phi = &mir.insns[11];
    table_address = &mir.insns[19];
    if (!mir_scalar_memory_location(
            &mir.insns[2], &row_type, &row_storage, &row_offset) ||
        !mir_scalar_memory_location(
            &mir.insns[3], &address_type, &address_storage,
            &address_offset) ||
        row_storage != SC_LOCAL || address_storage != row_storage ||
        address_offset != row_offset)
        return mir_machine_reject(
            "dynamic-row-scan", "setup-location");
    if (!mir_machine_constant_equals(mir.insns[1].dst, 0) ||
        mir.insns[2].memory_size != 2 ||
        (mir.insns[2].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject("dynamic-row-scan", "setup-row");
    if (mir.insns[5].src1 != mir.insns[3].dst ||
        mir.insns[5].memory_size != 2 ||
        (mir.insns[5].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject(
            "dynamic-row-scan", "setup-pointer");
    if (!mir_machine_constant_equals(mir.insns[6].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[8]) ||
        mir.insns[8].memory_size != 2)
        return mir_machine_reject(
            "dynamic-row-scan", "setup-column");
    if (!mir_machine_same_location(&mir.insns[2], &mir.insns[10]) ||
        column_phi->src1 != mir.insns[6].dst ||
        column_phi->src2 != mir.insns[29].dst ||
        column_phi->phi_pred1 != mir.insns[0].label ||
        column_phi->phi_pred2 != mir.insns[26].label ||
        type_ptr_depth(column_phi->type) != 0 ||
        (column_phi->type & 15) != TYPE_INT ||
        type_size(column_phi->type) != 2 ||
        mir.insns[13].immediate <= 0 ||
        mir.insns[13].immediate > 255 ||
        mir.insns[14].immediate != '<' ||
        mir.insns[14].src1 != column_phi->dst ||
        mir.insns[14].src2 != mir.insns[13].dst ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        mir.insns[15].label != mir.insns[32].label)
        return mir_machine_reject("dynamic-row-scan", "loop");
    if (!mir_machine_same_location(&mir.insns[5], &mir.insns[16]) ||
        !mir_machine_constant_equals(mir.insns[17].dst, 0) ||
        mir.insns[18].src1 != mir.insns[16].dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        mir.insns[18].immediate != 2 ||
        mir.insns[18].memory_size != 2 ||
        table_address->memory_flags != 0 ||
        !mir_machine_same_location(&mir.insns[2], &mir.insns[20]) ||
        mir.insns[21].src1 != table_address->dst ||
        mir.insns[21].src2 != mir.insns[20].dst ||
        mir.insns[21].immediate <= 0 ||
        mir.insns[21].memory_size <= 0 ||
        mir.insns[23].src1 != mir.insns[21].dst ||
        mir.insns[23].src2 != column_phi->dst ||
        mir.insns[23].immediate <= 0 ||
        mir.insns[23].memory_size != 2)
        return mir_machine_reject("dynamic-row-scan", "addresses");
    if (mir.insns[24].src1 != mir.insns[23].dst ||
        mir.insns[24].memory_size != 2 ||
        (mir.insns[24].memory_flags & (1 | 8)) != 0 ||
        mir.insns[25].src1 != mir.insns[18].dst ||
        mir.insns[25].src2 != mir.insns[24].dst ||
        mir.insns[25].memory_size != 2 ||
        (mir.insns[25].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_constant_equals(mir.insns[28].dst, 1) ||
        mir.insns[29].immediate != '+' ||
        mir.insns[29].src1 != column_phi->dst ||
        mir.insns[29].src2 != mir.insns[28].dst ||
        !mir_machine_same_location(&mir.insns[8], &mir.insns[30]) ||
        mir.insns[30].src1 != mir.insns[29].dst ||
        mir.insns[31].label != mir.insns[9].label ||
        !mir_machine_same_location(&mir.insns[2], &mir.insns[33]) ||
        mir.insns[34].src1 != mir.insns[33].dst)
        return mir_machine_reject("dynamic-row-scan", "body");
    plan->table = find_global(table_address->name);
    if (plan->table == NULL || !plan->table->is_defined ||
        plan->table->is_volatile)
        return mir_machine_reject("dynamic-row-scan", "table");
    plan->row_stride = (int)mir.insns[21].immediate;
    plan->element_stride = (int)mir.insns[23].immediate;
    plan->count = (int)mir.insns[13].immediate;
    if ((plan->row_stride & (plan->row_stride - 1)) != 0 ||
        plan->row_stride > 32767 ||
        plan->element_stride > 127 ||
        plan->count * plan->element_stride > 255)
        return mir_machine_reject("dynamic-row-scan", "bounds");
    return 1;
}

static int mir_match_byte_mismatch_scan(
    struct MirByteMismatchScan *plan)
{
    static const int expected_opcodes[42] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_NOP, MIR_UNARY,
        MIR_BINARY, MIR_UNARY, MIR_UNARY, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_UNARY, MIR_RETURN, MIR_LABEL,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_RETURN
    };
    const struct MirInsn *pointer = &mir.insns[1];
    const struct MirInsn *base = &mir.insns[2];
    const struct MirInsn *index_phi = &mir.insns[9];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 42 || mir_cfg_block_count() != 5 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return mir_machine_reject("byte-mismatch-scan", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject("byte-mismatch-scan", "opcode");
    if (type_ptr_depth(pointer->type) != 1 ||
        (pointer->type & 15) != TYPE_CHAR ||
        type_size(pointer->type) != 2 ||
        mir_machine_pointee_is_volatile(pointer) ||
        type_ptr_depth(base->type) != 0 ||
        (base->type & 15) != TYPE_INT ||
        type_size(base->type) != 2 ||
        !mir_machine_parameter_value_offset(
            pointer->dst, &plan->pointer_stack_offset) ||
        !mir_machine_parameter_value_offset(
            base->dst, &plan->base_stack_offset))
        return mir_machine_reject(
            "byte-mismatch-scan", "parameters");
    if (!mir_machine_constant_equals(mir.insns[4].dst, 0) ||
        type_size(mir.insns[4].type) != 1 ||
        (mir.insns[4].type & TYPE_UNSIGNED) == 0 ||
        !mir_machine_unobservable_local_store(&mir.insns[5]) ||
        mir.insns[5].memory_size != 1 ||
        mir.insns[5].src1 != mir.insns[4].dst ||
        index_phi->src1 != mir.insns[4].dst ||
        index_phi->src2 != mir.insns[35].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[32].label ||
        type_size(index_phi->type) != 1 ||
        (index_phi->type & TYPE_UNSIGNED) == 0)
        return mir_machine_reject(
            "byte-mismatch-scan", "index");
    if (mir.insns[11].immediate <= 0 ||
        mir.insns[11].immediate > 255 ||
        mir.insns[12].immediate != 0 ||
        mir.insns[12].src1 != index_phi->dst ||
        mir.insns[13].immediate != '<' ||
        mir.insns[13].src1 != mir.insns[12].dst ||
        mir.insns[13].src2 != mir.insns[11].dst ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        mir.insns[14].label != mir.insns[38].label)
        return mir_machine_reject(
            "byte-mismatch-scan", "bound");
    plan->count = (int)mir.insns[11].immediate;
    if (mir.insns[17].src1 != pointer->dst ||
        mir.insns[17].src2 != index_phi->dst ||
        mir.insns[17].immediate != 1 ||
        mir.insns[17].memory_size != 1 ||
        mir.insns[18].src1 != mir.insns[17].dst ||
        mir.insns[18].memory_size != 1 ||
        (mir.insns[18].memory_flags & (1 | 8)) != 0 ||
        mir.insns[21].immediate != 0 ||
        mir.insns[21].src1 != index_phi->dst ||
        mir.insns[22].immediate != '+' ||
        mir.insns[22].src1 != base->dst ||
        mir.insns[22].src2 != mir.insns[21].dst ||
        mir.insns[23].immediate != 0 ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        type_size(mir.insns[23].type) != 1 ||
        mir.insns[24].immediate != 0 ||
        mir.insns[24].src1 != mir.insns[18].dst ||
        mir.insns[25].immediate != 0 ||
        mir.insns[25].src1 != mir.insns[23].dst ||
        mir.insns[26].immediate != TOK_NE ||
        mir.insns[26].src1 != mir.insns[24].dst ||
        mir.insns[26].src2 != mir.insns[25].dst ||
        mir.insns[27].src1 != mir.insns[26].dst ||
        mir.insns[27].label != mir.insns[31].label ||
        mir.insns[29].immediate != 0 ||
        mir.insns[29].src1 != index_phi->dst ||
        mir.insns[30].src1 != mir.insns[29].dst)
        return mir_machine_reject(
            "byte-mismatch-scan", "comparison");
    if (!mir_machine_constant_equals(mir.insns[34].dst, 1) ||
        type_size(mir.insns[34].type) != 1 ||
        mir.insns[35].immediate != '+' ||
        mir.insns[35].src1 != index_phi->dst ||
        mir.insns[35].src2 != mir.insns[34].dst ||
        mir.insns[36].object != mir.insns[5].object ||
        mir.insns[36].memory_size != 1 ||
        mir.insns[36].src1 != mir.insns[35].dst ||
        mir.insns[37].label != mir.insns[6].label ||
        !mir_machine_constant_equals(mir.insns[40].dst, 65535) ||
        mir.insns[41].src1 != mir.insns[40].dst)
        return mir_machine_reject(
            "byte-mismatch-scan", "result");
    return 1;
}

static int mir_match_variable_byte_step_sum(
    struct MirVariableByteStepSum *plan)
{
    static const int expected_opcodes[41] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_CONST, MIR_STORE, MIR_NOP,
        MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_PHI, MIR_PHI, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP,
        MIR_UNARY, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP,
        MIR_NOP, MIR_UNARY, MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_NOP,
        MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_RETURN
    };
    const struct MirInsn *first = &mir.insns[1];
    const struct MirInsn *step = &mir.insns[2];
    const struct MirInsn *sum_phi = &mir.insns[15];
    const struct MirInsn *value_phi = &mir.insns[16];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 41 || mir_cfg_block_count() != 4 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return mir_machine_reject(
            "variable-byte-step-sum", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "variable-byte-step-sum", "opcode");
    if (type_ptr_depth(first->type) != 0 ||
        type_size(first->type) != 1 ||
        (first->type & TYPE_UNSIGNED) == 0 ||
        type_ptr_depth(step->type) != 0 ||
        type_size(step->type) != 1 ||
        (step->type & TYPE_UNSIGNED) == 0 ||
        !mir_machine_parameter_value_offset(
            first->dst, &plan->first_stack_offset) ||
        !mir_machine_parameter_value_offset(
            step->dst, &plan->step_stack_offset))
        return mir_machine_reject(
            "variable-byte-step-sum", "parameters");
    if (!mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[4]) ||
        mir.insns[4].memory_size != 2 ||
        mir.insns[4].src1 != mir.insns[3].dst ||
        mir.insns[7].immediate != 0 ||
        mir.insns[7].src1 != first->dst ||
        mir.insns[8].immediate != 0 ||
        mir.insns[8].src1 != step->dst ||
        mir.insns[9].immediate != '+' ||
        mir.insns[9].src1 != mir.insns[7].dst ||
        mir.insns[9].src2 != mir.insns[8].dst ||
        mir.insns[10].immediate != 0 ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        type_size(mir.insns[10].type) != 1 ||
        !mir_machine_unobservable_local_store(&mir.insns[11]) ||
        mir.insns[11].memory_size != 1 ||
        mir.insns[11].src1 != mir.insns[10].dst)
        return mir_machine_reject(
            "variable-byte-step-sum", "initializers");
    if (sum_phi->src1 != mir.insns[3].dst ||
        sum_phi->src2 != mir.insns[25].dst ||
        sum_phi->phi_pred1 != mir.insns[0].label ||
        sum_phi->phi_pred2 != mir.insns[28].label ||
        value_phi->src1 != mir.insns[10].dst ||
        value_phi->src2 != mir.insns[34].dst ||
        value_phi->phi_pred1 != mir.insns[0].label ||
        value_phi->phi_pred2 != mir.insns[28].label ||
        type_size(value_phi->type) != 1 ||
        (value_phi->type & TYPE_UNSIGNED) == 0)
        return mir_machine_reject(
            "variable-byte-step-sum", "phis");
    if (mir.insns[18].immediate <= 0 ||
        mir.insns[18].immediate > 255 ||
        mir.insns[19].immediate != 0 ||
        mir.insns[19].src1 != value_phi->dst ||
        mir.insns[20].immediate != '<' ||
        mir.insns[20].src1 != mir.insns[19].dst ||
        mir.insns[20].src2 != mir.insns[18].dst ||
        mir.insns[21].src1 != mir.insns[20].dst ||
        mir.insns[21].label != mir.insns[38].label)
        return mir_machine_reject(
            "variable-byte-step-sum", "condition");
    plan->bound = (int)mir.insns[18].immediate;
    if (mir.insns[24].immediate != 0 ||
        mir.insns[24].src1 != value_phi->dst ||
        mir.insns[25].immediate != '+' ||
        mir.insns[25].src1 != sum_phi->dst ||
        mir.insns[25].src2 != mir.insns[24].dst ||
        mir.insns[27].object != mir.insns[4].object ||
        mir.insns[27].src1 != mir.insns[25].dst ||
        mir.insns[31].immediate != 0 ||
        mir.insns[31].src1 != value_phi->dst ||
        mir.insns[32].immediate != 0 ||
        mir.insns[32].src1 != step->dst ||
        mir.insns[33].immediate != '+' ||
        mir.insns[33].src1 != mir.insns[31].dst ||
        mir.insns[33].src2 != mir.insns[32].dst ||
        mir.insns[34].immediate != 0 ||
        mir.insns[34].src1 != mir.insns[33].dst ||
        type_size(mir.insns[34].type) != 1 ||
        mir.insns[36].object != mir.insns[11].object ||
        mir.insns[36].src1 != mir.insns[34].dst ||
        mir.insns[37].label != mir.insns[12].label ||
        mir.insns[40].src1 != sum_phi->dst)
        return mir_machine_reject(
            "variable-byte-step-sum", "body");
    return 1;
}

static int mir_match_alias_byte_step_sum(
    struct MirVariableByteStepSum *plan)
{
    static const int expected_opcodes[59] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_ADDRESS, MIR_NOP, MIR_STORE,
        MIR_CONST, MIR_STORE, MIR_NOP, MIR_NOP, MIR_UNARY, MIR_UNARY,
        MIR_BINARY, MIR_UNARY, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP,
        MIR_LOAD, MIR_PHI, MIR_LOAD, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_LOAD, MIR_UNARY, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_LOAD, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD_INDIRECT, MIR_NOP, MIR_UNARY,
        MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_STORE_INDIRECT, MIR_LABEL,
        MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_UNARY, MIR_UNARY,
        MIR_BINARY, MIR_UNARY, MIR_NOP, MIR_STORE, MIR_JUMP, MIR_LABEL,
        MIR_NOP, MIR_RETURN
    };
    const struct MirInsn *first = &mir.insns[1];
    const struct MirInsn *step = &mir.insns[2];
    const struct MirInsn *alias_store = &mir.insns[5];
    const struct MirInsn *sum_store = &mir.insns[7];
    const struct MirInsn *value_store = &mir.insns[14];
    const struct MirInsn *sum_phi = &mir.insns[19];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 59 || mir_cfg_block_count() != 5 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return mir_machine_reject(
            "alias-byte-step-sum", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "alias-byte-step-sum", "opcode");
    if (type_size(first->type) != 1 ||
        (first->type & TYPE_UNSIGNED) == 0 ||
        type_size(step->type) != 1 ||
        (step->type & TYPE_UNSIGNED) == 0 ||
        !mir_machine_parameter_value_offset(
            first->dst, &plan->first_stack_offset) ||
        !mir_machine_parameter_value_offset(
            step->dst, &plan->step_stack_offset))
        return mir_machine_reject(
            "alias-byte-step-sum", "parameters");
    if (mir.insns[3].src1 >= 0 ||
        !mir_machine_unobservable_local_store(alias_store) ||
        alias_store->src1 != mir.insns[3].dst ||
        alias_store->memory_size != 2 ||
        !mir_machine_constant_equals(mir.insns[6].dst, 0) ||
        !mir_machine_unobservable_local_store(sum_store) ||
        sum_store->src1 != mir.insns[6].dst ||
        sum_store->memory_size != 2 ||
        mir.insns[10].immediate != 0 ||
        mir.insns[10].src1 != first->dst ||
        mir.insns[11].immediate != 0 ||
        mir.insns[11].src1 != step->dst ||
        mir.insns[12].immediate != '+' ||
        mir.insns[12].src1 != mir.insns[10].dst ||
        mir.insns[12].src2 != mir.insns[11].dst ||
        mir.insns[13].immediate != 0 ||
        mir.insns[13].src1 != mir.insns[12].dst ||
        type_size(mir.insns[13].type) != 1 ||
        value_store->src1 != mir.insns[13].dst ||
        value_store->memory_size != 1)
        return mir_machine_reject(
            "alias-byte-step-sum", "initializers");
    if (!mir_machine_same_location(value_store, &mir.insns[18]) ||
        !mir_machine_same_location(value_store, &mir.insns[20]) ||
        !mir_machine_same_location(value_store, &mir.insns[26]) ||
        !mir_machine_same_location(value_store, &mir.insns[31]) ||
        !mir_machine_same_location(value_store, &mir.insns[47]) ||
        sum_phi->src1 != mir.insns[6].dst ||
        sum_phi->src2 != mir.insns[28].dst ||
        sum_phi->phi_pred1 != mir.insns[0].label ||
        sum_phi->phi_pred2 != mir.insns[46].label)
        return mir_machine_reject(
            "alias-byte-step-sum", "state");
    if (mir.insns[21].immediate <= 0 ||
        mir.insns[21].immediate > 255 ||
        mir.insns[22].immediate != 0 ||
        mir.insns[22].src1 != mir.insns[20].dst ||
        mir.insns[23].immediate != '<' ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[23].src2 != mir.insns[21].dst ||
        mir.insns[24].src1 != mir.insns[23].dst ||
        mir.insns[24].label != mir.insns[56].label)
        return mir_machine_reject(
            "alias-byte-step-sum", "condition");
    plan->bound = (int)mir.insns[21].immediate;
    if (mir.insns[27].immediate != 0 ||
        mir.insns[27].src1 != mir.insns[26].dst ||
        mir.insns[28].immediate != '+' ||
        mir.insns[28].src1 != sum_phi->dst ||
        mir.insns[28].src2 != mir.insns[27].dst ||
        mir.insns[30].object != sum_store->object ||
        mir.insns[30].src1 != mir.insns[28].dst ||
        mir.insns[33].immediate != 0 ||
        mir.insns[33].src1 != mir.insns[31].dst ||
        mir.insns[34].immediate != TOK_EQ ||
        mir.insns[34].src1 != mir.insns[33].dst ||
        mir.insns[34].src2 != mir.insns[32].dst ||
        mir.insns[35].src1 != mir.insns[34].dst ||
        mir.insns[35].label != mir.insns[44].label)
        return mir_machine_reject(
            "alias-byte-step-sum", "sum-guard");
    plan->double_step_value = (int)mir.insns[32].immediate;
    plan->has_double_step = 1;
    if (!mir_machine_same_location(alias_store, &mir.insns[36]) ||
        mir.insns[37].src1 != mir.insns[36].dst ||
        mir.insns[37].memory_size != 1 ||
        (mir.insns[37].memory_flags & (1 | 8)) != 0 ||
        mir.insns[39].immediate != 0 ||
        mir.insns[39].src1 != mir.insns[37].dst ||
        mir.insns[40].immediate != 0 ||
        mir.insns[40].src1 != step->dst ||
        mir.insns[41].immediate != '+' ||
        mir.insns[41].src1 != mir.insns[39].dst ||
        mir.insns[41].src2 != mir.insns[40].dst ||
        mir.insns[42].immediate != 0 ||
        mir.insns[42].src1 != mir.insns[41].dst ||
        mir.insns[43].src1 != mir.insns[36].dst ||
        mir.insns[43].src2 != mir.insns[42].dst ||
        mir.insns[43].memory_size != 1 ||
        (mir.insns[43].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject(
            "alias-byte-step-sum", "alias-update");
    if (mir.insns[49].immediate != 0 ||
        mir.insns[49].src1 != mir.insns[47].dst ||
        mir.insns[50].immediate != 0 ||
        mir.insns[50].src1 != step->dst ||
        mir.insns[51].immediate != '+' ||
        mir.insns[51].src1 != mir.insns[49].dst ||
        mir.insns[51].src2 != mir.insns[50].dst ||
        mir.insns[52].immediate != 0 ||
        mir.insns[52].src1 != mir.insns[51].dst ||
        !mir_machine_same_location(value_store, &mir.insns[54]) ||
        mir.insns[54].src1 != mir.insns[52].dst ||
        mir.insns[55].label != mir.insns[15].label ||
        mir.insns[58].src1 != sum_phi->dst)
        return mir_machine_reject(
            "alias-byte-step-sum", "step");
    return 1;
}

static int mir_match_fixed_reverse_word_copy(
    struct MirFixedReverseWordCopy *plan)
{
    static const int expected_opcodes[31] = {
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_PHI,
        MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_ADDRESS, MIR_NOP,
        MIR_NOP, MIR_CONST, MIR_NOP, MIR_UNARY, MIR_BINARY,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT, MIR_LABEL,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL
    };
    const struct MirInsn *index_phi = &mir.insns[5];
    const struct MirInsn *destination = &mir.insns[11];
    const struct MirInsn *source = &mir.insns[14];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 31 || mir_cfg_block_count() != 4 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return mir_machine_reject(
            "fixed-reverse-word-copy", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "fixed-reverse-word-copy", "opcode");
    if (!mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[3]) ||
        mir.insns[3].memory_size != 1 ||
        index_phi->src1 != mir.insns[2].dst ||
        index_phi->src2 != mir.insns[27].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[24].label ||
        mir.insns[7].immediate <= 0 ||
        mir.insns[7].immediate > 127 ||
        mir.insns[8].immediate != 0 ||
        mir.insns[8].src1 != index_phi->dst ||
        mir.insns[9].immediate != '<' ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].src2 != mir.insns[7].dst ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[10].label != mir.insns[30].label)
        return mir_machine_reject(
            "fixed-reverse-word-copy", "loop");
    plan->count = (int)mir.insns[7].immediate;
    if (!mir_scalar_memory_location(
            destination, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        destination->type == 0 ||
        mir.insns[13].src1 != destination->dst ||
        mir.insns[13].src2 != index_phi->dst ||
        mir.insns[13].immediate != 2 ||
        mir.insns[13].memory_size != 2)
        return mir_machine_reject(
            "fixed-reverse-word-copy", "destination");
    plan->destination = find_global(destination->name);
    plan->destination_offset = memory_offset;
    if (plan->destination == NULL ||
        plan->destination->is_volatile)
        return mir_machine_reject(
            "fixed-reverse-word-copy", "destination-symbol");
    if (!mir_scalar_memory_location(
            source, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        mir.insns[17].immediate != plan->count - 1 ||
        mir.insns[19].immediate != 0 ||
        mir.insns[19].src1 != index_phi->dst ||
        mir.insns[20].immediate != '-' ||
        mir.insns[20].src1 != mir.insns[17].dst ||
        mir.insns[20].src2 != mir.insns[19].dst ||
        mir.insns[21].src1 != source->dst ||
        mir.insns[21].src2 != mir.insns[20].dst ||
        mir.insns[21].immediate != 2 ||
        mir.insns[21].memory_size != 2 ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        mir.insns[22].memory_size != 2 ||
        (mir.insns[22].memory_flags & (1 | 8)) != 0 ||
        mir.insns[23].src1 != mir.insns[13].dst ||
        mir.insns[23].src2 != mir.insns[22].dst ||
        mir.insns[23].memory_size != 2 ||
        (mir.insns[23].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject(
            "fixed-reverse-word-copy", "source");
    plan->source = find_global(source->name);
    plan->source_offset = memory_offset;
    if (plan->source == NULL || plan->source->is_volatile ||
        plan->source == plan->destination)
        return mir_machine_reject(
            "fixed-reverse-word-copy", "source-symbol");
    if (!mir_machine_constant_equals(mir.insns[26].dst, 1) ||
        mir.insns[27].immediate != '+' ||
        mir.insns[27].src1 != index_phi->dst ||
        mir.insns[27].src2 != mir.insns[26].dst ||
        mir.insns[28].object != mir.insns[3].object ||
        mir.insns[28].src1 != mir.insns[27].dst ||
        mir.insns[29].label != mir.insns[4].label)
        return mir_machine_reject(
            "fixed-reverse-word-copy", "increment");
    return 1;
}

static int mir_match_fixed_random_word_fill(
    struct MirFixedRandomWordFill *plan)
{
    static const int expected_opcodes[26] = {
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_PHI,
        MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_CALL, MIR_CONST,
        MIR_BINARY, MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_CALL
    };
    const struct MirInsn *index_phi = &mir.insns[5];
    const struct MirInsn *destination = &mir.insns[11];
    const struct MirInsn *random_call = &mir.insns[14];
    const struct MirInsn *finish_call = &mir.insns[25];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 26 || mir_cfg_block_count() != 4 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return mir_machine_reject(
            "fixed-random-word-fill", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "fixed-random-word-fill", "opcode");
    if (!mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[3]) ||
        mir.insns[3].memory_size != 1 ||
        index_phi->src1 != mir.insns[2].dst ||
        index_phi->src2 != mir.insns[21].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[18].label ||
        mir.insns[7].immediate <= 0 ||
        mir.insns[7].immediate > 127 ||
        mir.insns[8].immediate != 0 ||
        mir.insns[8].src1 != index_phi->dst ||
        mir.insns[9].immediate != '<' ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].src2 != mir.insns[7].dst ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[10].label != mir.insns[24].label)
        return mir_machine_reject(
            "fixed-random-word-fill", "loop");
    plan->count = (int)mir.insns[7].immediate;
    if (!mir_scalar_memory_location(
            destination, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        mir.insns[13].src1 != destination->dst ||
        mir.insns[13].src2 != index_phi->dst ||
        mir.insns[13].immediate != 2 ||
        mir.insns[13].memory_size != 2 ||
        !mir_machine_call_has_no_arguments(random_call) ||
        random_call->type != mir.insns[16].type ||
        mir.insns[15].immediate <= 1 ||
        mir.insns[15].immediate > 32767 ||
        mir.insns[16].immediate != '%' ||
        mir.insns[16].src1 != random_call->dst ||
        mir.insns[16].src2 != mir.insns[15].dst ||
        mir.insns[17].src1 != mir.insns[13].dst ||
        mir.insns[17].src2 != mir.insns[16].dst ||
        mir.insns[17].memory_size != 2 ||
        (mir.insns[17].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject(
            "fixed-random-word-fill", "body");
    plan->destination = find_global(destination->name);
    plan->destination_offset = memory_offset;
    plan->random_function = find_global(random_call->name);
    plan->finish_function = find_global(finish_call->name);
    plan->modulus = (int)mir.insns[15].immediate;
    if (plan->destination == NULL ||
        plan->destination->is_volatile ||
        plan->random_function == NULL ||
        !plan->random_function->is_defined ||
        plan->finish_function == NULL ||
        !plan->finish_function->is_defined ||
        plan->random_function->is_funcptr ||
        plan->finish_function->is_funcptr ||
        !mir_machine_call_has_no_arguments(finish_call))
        return mir_machine_reject(
            "fixed-random-word-fill", "symbols");
    if (!mir_machine_constant_equals(mir.insns[20].dst, 1) ||
        mir.insns[21].immediate != '+' ||
        mir.insns[21].src1 != index_phi->dst ||
        mir.insns[21].src2 != mir.insns[20].dst ||
        mir.insns[22].object != mir.insns[3].object ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        mir.insns[23].label != mir.insns[4].label)
        return mir_machine_reject(
            "fixed-random-word-fill", "increment");
    return 1;
}

static int mir_match_global_byte_copy_state(
    struct MirGlobalByteCopyState *plan)
{
    static const int expected_opcodes[42] = {
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_PHI,
        MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT, MIR_LABEL,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_CONST, MIR_STORE
    };
    static const int state_constants[3] = { 26, 30, 40 };
    static const int state_stores[3] = { 28, 32, 41 };
    const struct MirInsn *index_phi = &mir.insns[5];
    const struct MirInsn *destination = &mir.insns[11];
    const struct MirInsn *source = &mir.insns[14];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;
    int state;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 42 || mir_cfg_block_count() != 4 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return mir_machine_reject(
            "global-byte-copy-state", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "global-byte-copy-state", "opcode");
    if (!mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[3]) ||
        index_phi->src1 != mir.insns[2].dst ||
        index_phi->src2 != mir.insns[22].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[19].label ||
        mir.insns[7].immediate <= 0 ||
        mir.insns[7].immediate > 255 ||
        mir.insns[8].immediate != 0 ||
        mir.insns[8].src1 != index_phi->dst ||
        mir.insns[9].immediate != '<' ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].src2 != mir.insns[7].dst ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[10].label != mir.insns[25].label)
        return mir_machine_reject(
            "global-byte-copy-state", "loop");
    plan->count = (int)mir.insns[7].immediate;
    if (!mir_scalar_memory_location(
            destination, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        mir.insns[13].src1 != destination->dst ||
        mir.insns[13].src2 != index_phi->dst ||
        mir.insns[13].immediate != 1 ||
        mir.insns[13].memory_size != 1)
        return mir_machine_reject(
            "global-byte-copy-state", "destination");
    plan->destination = find_global(destination->name);
    plan->destination_offset = memory_offset;
    if (!mir_scalar_memory_location(
            source, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        mir.insns[16].src1 != source->dst ||
        mir.insns[16].src2 != index_phi->dst ||
        mir.insns[16].immediate != 1 ||
        mir.insns[16].memory_size != 1 ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        mir.insns[17].memory_size != 1 ||
        (mir.insns[17].memory_flags & (1 | 8)) != 0 ||
        mir.insns[18].src1 != mir.insns[13].dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        mir.insns[18].memory_size != 1 ||
        (mir.insns[18].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject(
            "global-byte-copy-state", "source");
    plan->source = find_global(source->name);
    plan->source_offset = memory_offset;
    if (plan->source == NULL || plan->destination == NULL ||
        plan->source->is_volatile || plan->destination->is_volatile ||
        plan->source == plan->destination)
        return mir_machine_reject(
            "global-byte-copy-state", "copy-symbols");
    if (!mir_machine_constant_equals(mir.insns[21].dst, 1) ||
        mir.insns[22].immediate != '+' ||
        mir.insns[22].src1 != index_phi->dst ||
        mir.insns[22].src2 != mir.insns[21].dst ||
        mir.insns[23].object != mir.insns[3].object ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[24].label != mir.insns[4].label)
        return mir_machine_reject(
            "global-byte-copy-state", "increment");
    for (state = 0; state < 3; ++state) {
        const struct MirInsn *store =
            &mir.insns[state_stores[state]];
        long value;

        if (!mir_machine_constant_value(
                mir.insns[state_constants[state]].dst, &value, 0) ||
            !mir_machine_named_nonvolatile(store) ||
            !mir_scalar_memory_location(
                store, &memory_type, &memory_storage, &memory_offset) ||
            memory_storage != SC_GLOBAL ||
            (type_size(memory_type) != 1 &&
             type_size(memory_type) != 2) ||
            store->src1 !=
                mir.insns[state_constants[state]].dst)
            return mir_machine_reject(
                "global-byte-copy-state", "state");
        plan->state[state] = find_global(store->name);
        plan->state_offsets[state] = memory_offset;
        plan->state_widths[state] = type_size(memory_type);
        plan->state_values[state] =
            (int)((unsigned long)value &
                  (plan->state_widths[state] == 1
                       ? 0xffUL : 0xffffUL));
        if (plan->state[state] == NULL ||
            plan->state[state]->is_volatile)
            return mir_machine_reject(
                "global-byte-copy-state", "state-symbol");
    }
    return 1;
}

static int mir_match_fixed_global_stride_call(
    struct MirFixedGlobalStrideCall *plan)
{
    static const int expected_opcodes[35] = {
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_PHI,
        MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_NOP, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_INDEX_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_NOP,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_INDEX_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_JUMP, MIR_LABEL
    };
    const struct MirInsn *index_phi = &mir.insns[5];
    const struct MirInsn *fixed = &mir.insns[11];
    const struct MirInsn *first = &mir.insns[13];
    const struct MirInsn *second = &mir.insns[20];
    const struct MirInsn *call = &mir.insns[27];
    int arguments[3];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 35 || mir_cfg_block_count() != 4 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return mir_machine_reject(
            "fixed-global-stride-call", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "fixed-global-stride-call", "opcode");
    if (!mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[3]) ||
        index_phi->src1 != mir.insns[2].dst ||
        index_phi->src2 != mir.insns[31].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[28].label ||
        mir.insns[7].immediate <= 0 ||
        mir.insns[7].immediate > 255 ||
        mir.insns[8].immediate != 0 ||
        mir.insns[8].src1 != index_phi->dst ||
        mir.insns[9].immediate != '<' ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].src2 != mir.insns[7].dst ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[10].label != mir.insns[34].label)
        return mir_machine_reject(
            "fixed-global-stride-call", "loop");
    plan->count = (int)mir.insns[7].immediate;
    if (!mir_scalar_memory_location(
            fixed, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL)
        return mir_machine_reject(
            "fixed-global-stride-call", "fixed");
    plan->fixed = find_global(fixed->name);
    plan->fixed_offset = memory_offset;
    if (!mir_scalar_memory_location(
            first, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        mir.insns[16].immediate != 0 ||
        mir.insns[16].src1 != index_phi->dst ||
        mir.insns[17].immediate != '*' ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        mir.insns[17].src2 != mir.insns[15].dst ||
        mir.insns[18].src1 != first->dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        mir.insns[18].immediate <= 0 ||
        mir.insns[18].memory_size != 2)
        return mir_machine_reject(
            "fixed-global-stride-call", "first");
    plan->first = find_global(first->name);
    plan->first_offset = memory_offset;
    plan->first_stride =
        (int)(mir.insns[15].immediate * mir.insns[18].immediate);
    if (!mir_scalar_memory_location(
            second, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        mir.insns[23].immediate != 0 ||
        mir.insns[23].src1 != index_phi->dst ||
        mir.insns[24].immediate != '*' ||
        mir.insns[24].src1 != mir.insns[23].dst ||
        mir.insns[24].src2 != mir.insns[22].dst ||
        mir.insns[25].src1 != second->dst ||
        mir.insns[25].src2 != mir.insns[24].dst ||
        mir.insns[25].immediate <= 0 ||
        mir.insns[25].memory_size != 2)
        return mir_machine_reject(
            "fixed-global-stride-call", "second");
    plan->second = find_global(second->name);
    plan->second_offset = memory_offset;
    plan->second_stride =
        (int)(mir.insns[22].immediate * mir.insns[25].immediate);
    if (plan->fixed == NULL || plan->first == NULL ||
        plan->second == NULL ||
        plan->fixed->is_volatile || plan->first->is_volatile ||
        plan->second->is_volatile ||
        plan->first_stride <= 0 || plan->first_stride > 32767 ||
        plan->second_stride <= 0 || plan->second_stride > 32767 ||
        !mir_machine_three_call_arguments(call, arguments) ||
        arguments[0] != fixed->dst ||
        arguments[1] != mir.insns[18].dst ||
        arguments[2] != mir.insns[25].dst)
        return mir_machine_reject(
            "fixed-global-stride-call", "arguments");
    plan->function = find_global(call->name);
    if (plan->function == NULL || !plan->function->is_defined ||
        plan->function->is_funcptr || plan->function->is_noreturn ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0)
        return mir_machine_reject(
            "fixed-global-stride-call", "function");
    if (!mir_machine_constant_equals(mir.insns[30].dst, 1) ||
        mir.insns[31].immediate != '+' ||
        mir.insns[31].src1 != index_phi->dst ||
        mir.insns[31].src2 != mir.insns[30].dst ||
        mir.insns[32].object != mir.insns[3].object ||
        mir.insns[32].src1 != mir.insns[31].dst ||
        mir.insns[33].label != mir.insns[4].label)
        return mir_machine_reject(
            "fixed-global-stride-call", "increment");
    return 1;
}

static int mir_match_constant_loop_check(
    struct MirConstantLoopCheck *plan)
{
    static const int expected_opcodes[35] = {
        MIR_LABEL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST,
        MIR_STORE, MIR_LABEL, MIR_PHI, MIR_PHI, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP,
        MIR_UNARY, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL
    };
    const struct MirInsn *sum_phi;
    const struct MirInsn *index_phi;
    int arguments[2];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 35 || mir_cfg_block_count() != 4 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    sum_phi = &mir.insns[8];
    index_phi = &mir.insns[9];
    if (!mir_machine_constant_equals(mir.insns[1].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[3]) ||
        mir.insns[3].memory_size != 2 ||
        !mir_machine_constant_equals(mir.insns[5].dst, 1) ||
        !mir_machine_unobservable_local_store(&mir.insns[6]) ||
        mir.insns[6].memory_size != 1)
        return 0;
    if (sum_phi->src1 != mir.insns[1].dst ||
        sum_phi->src2 != mir.insns[18].dst ||
        sum_phi->phi_pred1 != mir.insns[0].label ||
        sum_phi->phi_pred2 != mir.insns[21].label ||
        type_ptr_depth(sum_phi->type) != 0 ||
        (sum_phi->type & 15) != TYPE_INT ||
        type_size(sum_phi->type) != 2 ||
        index_phi->src1 != mir.insns[5].dst ||
        index_phi->src2 != mir.insns[24].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[21].label ||
        (index_phi->type & TYPE_UNSIGNED) == 0 ||
        type_size(index_phi->type) != 1)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[11].dst, 10) ||
        mir.insns[12].immediate != 0 ||
        mir.insns[12].src1 != index_phi->dst ||
        mir.insns[13].immediate != TOK_LE ||
        mir.insns[13].src1 != mir.insns[12].dst ||
        mir.insns[13].src2 != mir.insns[11].dst ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        mir.insns[14].label != mir.insns[27].label)
        return 0;
    if (mir.insns[17].immediate != 0 ||
        mir.insns[17].src1 != index_phi->dst ||
        mir.insns[18].immediate != '+' ||
        mir.insns[18].src1 != sum_phi->dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        !mir_machine_same_location(&mir.insns[3], &mir.insns[20]) ||
        mir.insns[20].src1 != mir.insns[18].dst ||
        !mir_machine_constant_equals(mir.insns[23].dst, 1) ||
        mir.insns[24].immediate != '+' ||
        mir.insns[24].src1 != index_phi->dst ||
        mir.insns[24].src2 != mir.insns[23].dst ||
        !mir_machine_same_location(&mir.insns[6], &mir.insns[25]) ||
        mir.insns[25].src1 != mir.insns[24].dst ||
        mir.insns[26].label != mir.insns[7].label)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[29].dst, 55) ||
        mir.insns[30].immediate != TOK_EQ ||
        mir.insns[30].src1 != sum_phi->dst ||
        mir.insns[30].src2 != mir.insns[29].dst ||
        mir.insns[31].src1 != mir.insns[30].dst ||
        mir.insns[33].src1 != mir.insns[32].dst ||
        !mir_machine_two_call_arguments(&mir.insns[34], arguments) ||
        arguments[0] != mir.insns[30].dst ||
        arguments[1] != mir.insns[32].dst)
        return 0;
    plan->function = find_global(mir.insns[34].name);
    if (plan->function == NULL || !plan->function->is_defined ||
        plan->function->storage != SC_FUNC ||
        plan->function->is_funcptr ||
        plan->function->is_noreturn ||
        !plan->function->has_proto ||
        plan->function->proto_nargs != 2 ||
        plan->function->proto_variadic ||
        plan->function->proto_types[0] != mir.insns[31].type ||
        plan->function->proto_types[1] != mir.insns[33].type ||
        mir.insns[34].memory_flags != 0)
        return 0;
    plan->string_id = (int)mir.insns[32].immediate;
    return 1;
}

static int mir_match_global_byte_countdown(
    struct MirGlobalByteCountdown *plan)
{
    static const int expected_opcodes[36] = {
        MIR_LABEL, MIR_PARAM, MIR_LOAD, MIR_LOAD, MIR_BINARY, MIR_LOAD,
        MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_PHI, MIR_PHI, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_LOAD, MIR_BINARY, MIR_LOAD, MIR_BINARY, MIR_LOAD, MIR_BINARY,
        MIR_RETURN
    };
    const struct MirInsn *parameter;
    const struct MirInsn *sum_phi;
    const struct MirInsn *count_phi;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 36 || mir_cfg_block_count() != 4 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    parameter = &mir.insns[1];
    sum_phi = &mir.insns[14];
    count_phi = &mir.insns[15];
    if (type_ptr_depth(parameter->type) != 0 ||
        (parameter->type & 15) != TYPE_CHAR ||
        (parameter->type & TYPE_UNSIGNED) == 0 ||
        !mir_machine_parameter_value_offset(
            parameter->dst, &plan->parameter_stack_offset) ||
        !mir_machine_named_nonvolatile(&mir.insns[2]) ||
        type_ptr_depth(mir.insns[2].type) != 0 ||
        (mir.insns[2].type & 15) != TYPE_INT ||
        type_size(mir.insns[2].type) != 2 ||
        !mir_machine_same_location(&mir.insns[2], &mir.insns[3]) ||
        !mir_machine_same_location(&mir.insns[2], &mir.insns[5]))
        return 0;
    if (mir.insns[4].immediate != '+' ||
        mir.insns[4].src1 != mir.insns[2].dst ||
        mir.insns[4].src2 != mir.insns[3].dst ||
        mir.insns[6].immediate != '+' ||
        mir.insns[6].src1 != mir.insns[4].dst ||
        mir.insns[6].src2 != mir.insns[5].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[8]) ||
        mir.insns[8].src1 != mir.insns[6].dst ||
        mir.insns[8].memory_size != 2 ||
        !mir_machine_unobservable_local_store(&mir.insns[11]) ||
        mir.insns[11].src1 != parameter->dst ||
        mir.insns[11].memory_size != 1)
        return 0;
    if (sum_phi->src1 != mir.insns[6].dst ||
        sum_phi->src2 != mir.insns[23].dst ||
        sum_phi->phi_pred1 != mir.insns[0].label ||
        sum_phi->phi_pred2 != mir.insns[25].label ||
        type_ptr_depth(sum_phi->type) != 0 ||
        (sum_phi->type & 15) != TYPE_INT ||
        type_size(sum_phi->type) != 2 ||
        count_phi->src1 != parameter->dst ||
        count_phi->src2 != mir.insns[18].dst ||
        count_phi->phi_pred1 != mir.insns[0].label ||
        count_phi->phi_pred2 != mir.insns[25].label ||
        (count_phi->type & TYPE_UNSIGNED) == 0 ||
        type_size(count_phi->type) != 1)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[17].dst, 1) ||
        mir.insns[18].immediate != '-' ||
        mir.insns[18].src1 != count_phi->dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        !mir_machine_same_location(&mir.insns[11], &mir.insns[19]) ||
        mir.insns[19].src1 != mir.insns[18].dst ||
        mir.insns[20].src1 != mir.insns[18].dst ||
        mir.insns[20].label != mir.insns[27].label ||
        !mir_machine_constant_equals(mir.insns[22].dst, 1) ||
        mir.insns[23].immediate != '+' ||
        mir.insns[23].src1 != sum_phi->dst ||
        mir.insns[23].src2 != mir.insns[22].dst ||
        !mir_machine_same_location(&mir.insns[8], &mir.insns[24]) ||
        mir.insns[24].src1 != mir.insns[23].dst ||
        mir.insns[26].label != mir.insns[12].label)
        return 0;
    if (!mir_machine_same_location(&mir.insns[2], &mir.insns[29]) ||
        mir.insns[30].immediate != '+' ||
        mir.insns[30].src1 != sum_phi->dst ||
        mir.insns[30].src2 != mir.insns[29].dst ||
        !mir_machine_same_location(&mir.insns[2], &mir.insns[31]) ||
        mir.insns[32].immediate != '+' ||
        mir.insns[32].src1 != mir.insns[30].dst ||
        mir.insns[32].src2 != mir.insns[31].dst ||
        !mir_machine_same_location(&mir.insns[2], &mir.insns[33]) ||
        mir.insns[34].immediate != '+' ||
        mir.insns[34].src1 != mir.insns[32].dst ||
        mir.insns[34].src2 != mir.insns[33].dst ||
        mir.insns[35].src1 != mir.insns[34].dst)
        return 0;
    plan->value = find_global(mir.insns[2].name);
    if (plan->value == NULL || !plan->value->is_defined ||
        plan->value->is_volatile)
        return 0;
    return 1;
}

static int mir_match_conditional_string_report(
    struct MirConditionalStringReport *plan)
{
    static const int expected_opcodes[19] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_LOAD, MIR_ARG, MIR_NOP, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_LABEL,
        MIR_LABEL, MIR_PHI, MIR_ARG, MIR_CALL
    };
    int arguments[3];
    int memory_offset;
    int memory_storage;
    int memory_type;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 19 || mir_cfg_block_count() != 5 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (type_ptr_depth(mir.insns[1].type) != 1 ||
        (mir.insns[1].type & 15) != TYPE_CHAR ||
        type_ptr_depth(mir.insns[2].type) != 0 ||
        (mir.insns[2].type & 15) != TYPE_INT ||
        type_size(mir.insns[2].type) != 2 ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &plan->condition_stack_offset) ||
        !mir_scalar_memory_location(
            &mir.insns[5], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_PARAM ||
        type_ptr_depth(memory_type) != 1 ||
        (memory_type & 15) != TYPE_CHAR ||
        !mir_machine_same_location(&mir.insns[1], &mir.insns[5]))
        return 0;
    plan->name_stack_offset = memory_offset - 2;
    if (plan->name_stack_offset < 0 ||
        mir.insns[4].src1 != mir.insns[3].dst ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        mir.insns[8].src1 != mir.insns[2].dst ||
        mir.insns[8].label != mir.insns[12].label ||
        mir.insns[11].label != mir.insns[15].label ||
        mir.insns[16].src1 != mir.insns[9].dst ||
        mir.insns[16].src2 != mir.insns[13].dst ||
        mir.insns[16].phi_pred1 != mir.insns[10].label ||
        mir.insns[16].phi_pred2 != mir.insns[14].label ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[18], arguments) ||
        arguments[0] != mir.insns[3].dst ||
        arguments[1] != mir.insns[5].dst ||
        arguments[2] != mir.insns[16].dst)
        return 0;
    plan->function = find_global(mir.insns[18].name);
    if (plan->function == NULL ||
        strcmp(mir.insns[18].name, "printf") ||
        (mir.insns[18].memory_flags & MIR_CALL_FLAG_VARIADIC) == 0 ||
        (mir.insns[18].memory_flags &
         MIR_CALL_FLAG_FORMAT_RUNTIME) != 0)
        return 0;
    plan->format_string_id = (int)mir.insns[3].immediate;
    plan->true_string_id = (int)mir.insns[9].immediate;
    plan->false_string_id = (int)mir.insns[13].immediate;
    return 1;
}

static int mir_match_word_range_bool(struct MirWordRangeBool *plan)
{
    static const int expected_opcodes[18] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_RETURN
    };
    const struct MirInsn *parameter;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 18 || mir_cfg_block_count() != 4 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        ((mir.return_type & 15) != TYPE_INT &&
         (mir.return_type & 15) != TYPE_BOOL))
        return mir_machine_reject("word-range-bool", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject("word-range-bool", "opcode");
    parameter = &mir.insns[1];
    if (type_ptr_depth(parameter->type) != 0 ||
        (parameter->type & 15) != TYPE_INT ||
        (parameter->type & TYPE_UNSIGNED) != 0 ||
        type_size(parameter->type) != 2 ||
        !mir_machine_parameter_value_offset(
            parameter->dst, &plan->parameter_stack_offset) ||
        mir.insns[4].immediate != TOK_GE ||
        mir.insns[4].src1 != parameter->dst ||
        mir.insns[4].src2 != mir.insns[3].dst ||
        mir.insns[5].src1 != mir.insns[4].dst ||
        mir.insns[5].label != mir.insns[13].label ||
        mir.insns[8].immediate != '<' ||
        mir.insns[8].src1 != parameter->dst ||
        mir.insns[8].src2 != mir.insns[7].dst ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].label != mir.insns[13].label)
        return mir_machine_reject("word-range-bool", "comparisons");
    if (!mir_machine_constant_equals(mir.insns[11].dst, 1) ||
        mir.insns[12].label != mir.insns[15].label ||
        !mir_machine_constant_equals(mir.insns[14].dst, 0) ||
        mir.insns[16].src1 != mir.insns[11].dst ||
        mir.insns[16].src2 != mir.insns[14].dst ||
        mir.insns[16].phi_pred1 != mir.insns[10].label ||
        mir.insns[16].phi_pred2 != mir.insns[13].label ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        mir.insns[3].immediate != 0 ||
        mir.insns[7].immediate <= 0 ||
        mir.insns[7].immediate > 255)
        return mir_machine_reject("word-range-bool", "result");
    plan->lower = 0;
    plan->upper = (int)mir.insns[7].immediate;
    return 1;
}

static int mir_match_ascii_upper(struct MirAsciiUpper *plan)
{
    static const int expected_opcodes[31] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_CONST, MIR_BINARY,
        MIR_UNARY, MIR_RETURN, MIR_LABEL, MIR_NOP, MIR_RETURN
    };
    const struct MirInsn *parameter;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 31 || mir_cfg_block_count() != 5 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_CHAR ||
        type_size(mir.return_type) != 1)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    parameter = &mir.insns[1];
    if (type_ptr_depth(parameter->type) != 0 ||
        (parameter->type & 15) != TYPE_CHAR ||
        type_size(parameter->type) != 1 ||
        !mir_machine_parameter_value_offset(
            parameter->dst, &plan->parameter_stack_offset) ||
        mir.insns[4].immediate != 0 ||
        mir.insns[4].src1 != parameter->dst ||
        mir.insns[5].immediate != TOK_GE ||
        mir.insns[5].src1 != mir.insns[4].dst ||
        mir.insns[5].src2 != mir.insns[3].dst ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        mir.insns[6].label != mir.insns[15].label)
        return 0;
    if (mir.insns[9].immediate != 0 ||
        mir.insns[9].src1 != parameter->dst ||
        mir.insns[10].immediate != TOK_LE ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[10].src2 != mir.insns[8].dst ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[11].label != mir.insns[15].label ||
        !mir_machine_constant_equals(mir.insns[13].dst, 1) ||
        mir.insns[14].label != mir.insns[17].label ||
        !mir_machine_constant_equals(mir.insns[16].dst, 0) ||
        mir.insns[18].src1 != mir.insns[13].dst ||
        mir.insns[18].src2 != mir.insns[16].dst ||
        mir.insns[18].phi_pred1 != mir.insns[12].label ||
        mir.insns[18].phi_pred2 != mir.insns[15].label ||
        mir.insns[19].src1 != mir.insns[18].dst ||
        mir.insns[19].label != mir.insns[28].label)
        return 0;
    if (mir.insns[22].immediate != 0 ||
        mir.insns[22].src1 != parameter->dst ||
        mir.insns[23].immediate != '-' ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[23].src2 != mir.insns[21].dst ||
        mir.insns[25].immediate != '+' ||
        mir.insns[25].src1 != mir.insns[23].dst ||
        mir.insns[25].src2 != mir.insns[24].dst ||
        mir.insns[26].immediate != 0 ||
        mir.insns[26].src1 != mir.insns[25].dst ||
        mir.insns[27].src1 != mir.insns[26].dst ||
        mir.insns[30].src1 != parameter->dst)
        return 0;
    plan->lower = (int)mir.insns[3].immediate;
    plan->upper = (int)mir.insns[8].immediate;
    plan->adjustment =
        (int)mir.insns[24].immediate - (int)mir.insns[21].immediate;
    plan->width = 1;
    if (plan->lower < 0 || plan->lower > 255 ||
        plan->upper < plan->lower || plan->upper > 255 ||
        plan->adjustment < -255 || plan->adjustment > 255)
        return 0;
    return 1;
}

static int mir_match_ascii_word_case(struct MirAsciiUpper *plan)
{
    static const int expected_opcodes[27] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_RETURN, MIR_LABEL,
        MIR_NOP, MIR_RETURN
    };
    const struct MirInsn *parameter = &mir.insns[1];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 27 || mir_cfg_block_count() != 5 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        (mir.return_type & TYPE_UNSIGNED) != 0 ||
        type_size(mir.return_type) != 2)
        return mir_machine_reject("ascii-word-case", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject("ascii-word-case", "opcode");
    if (type_ptr_depth(parameter->type) != 0 ||
        (parameter->type & 15) != TYPE_INT ||
        (parameter->type & TYPE_UNSIGNED) != 0 ||
        type_size(parameter->type) != 2 ||
        !mir_machine_parameter_value_offset(
            parameter->dst, &plan->parameter_stack_offset) ||
        mir.insns[4].immediate != TOK_GE ||
        mir.insns[4].src1 != parameter->dst ||
        mir.insns[4].src2 != mir.insns[3].dst ||
        mir.insns[5].src1 != mir.insns[4].dst ||
        mir.insns[5].label != mir.insns[13].label ||
        mir.insns[8].immediate != TOK_LE ||
        mir.insns[8].src1 != parameter->dst ||
        mir.insns[8].src2 != mir.insns[7].dst ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].label != mir.insns[13].label ||
        !mir_machine_constant_equals(mir.insns[11].dst, 1) ||
        mir.insns[12].label != mir.insns[15].label ||
        !mir_machine_constant_equals(mir.insns[14].dst, 0) ||
        mir.insns[16].src1 != mir.insns[11].dst ||
        mir.insns[16].src2 != mir.insns[14].dst ||
        mir.insns[16].phi_pred1 != mir.insns[10].label ||
        mir.insns[16].phi_pred2 != mir.insns[13].label ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        mir.insns[17].label != mir.insns[24].label ||
        mir.insns[22].immediate != '+' ||
        mir.insns[22].src1 != parameter->dst ||
        mir.insns[22].src2 != mir.insns[21].dst ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[26].src1 != parameter->dst)
        return mir_machine_reject("ascii-word-case", "flow");
    plan->width = 2;
    plan->lower = (int)mir.insns[3].immediate;
    plan->upper = (int)mir.insns[7].immediate;
    plan->adjustment = (int)mir.insns[21].immediate;
    if (plan->lower < 0 || plan->lower > 255 ||
        plan->upper < plan->lower || plan->upper > 255 ||
        plan->adjustment < -255 || plan->adjustment > 255)
        return mir_machine_reject("ascii-word-case", "constants");
    return 1;
}

static int mir_match_fixed_word_array_sum(
    struct MirFixedWordArraySum *plan)
{
    static const int expected_opcodes[34] = {
        MIR_LABEL, MIR_PARAM, MIR_CONST, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_CONST, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_PHI, MIR_PHI,
        MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_RETURN
    };
    const struct MirInsn *parameter;
    const struct MirInsn *sum_phi;
    const struct MirInsn *index_phi;
    int declared;
    int found_declaration = 0;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 34 || mir_cfg_block_count() != 4 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    parameter = &mir.insns[1];
    sum_phi = &mir.insns[10];
    index_phi = &mir.insns[11];
    if (type_ptr_depth(parameter->type) != 1 ||
        (parameter->type & 15) != TYPE_INT ||
        mir_machine_pointee_is_volatile(parameter) ||
        !mir_machine_parameter_value_offset(
            parameter->dst, &plan->parameter_stack_offset) ||
        !mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[4]) ||
        mir.insns[4].memory_size != 2 ||
        !mir_machine_constant_equals(mir.insns[6].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[7]) ||
        mir.insns[7].memory_size != 1)
        return 0;
    for (declared = 0; declared < mir.declared_count; ++declared)
        if (!strcmp(mir.declared_names[declared], parameter->name)) {
            plan->pointer_is_volatile =
                mir.declared_is_volatile[declared];
            found_declaration = 1;
            break;
        }
    if (!found_declaration)
        return 0;
    if (sum_phi->src1 != mir.insns[2].dst ||
        sum_phi->src2 != mir.insns[22].dst ||
        sum_phi->phi_pred1 != mir.insns[0].label ||
        sum_phi->phi_pred2 != mir.insns[25].label ||
        type_ptr_depth(sum_phi->type) != 0 ||
        (sum_phi->type & 15) != TYPE_INT ||
        type_size(sum_phi->type) != 2 ||
        index_phi->src1 != mir.insns[6].dst ||
        index_phi->src2 != mir.insns[28].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[25].label ||
        (index_phi->type & TYPE_UNSIGNED) == 0 ||
        type_size(index_phi->type) != 1)
        return 0;
    if (mir.insns[13].immediate <= 0 ||
        mir.insns[13].immediate > 16 ||
        mir.insns[14].immediate != 0 ||
        mir.insns[14].src1 != index_phi->dst ||
        mir.insns[15].immediate != '<' ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        mir.insns[15].src2 != mir.insns[13].dst ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[16].label != mir.insns[31].label ||
        mir.insns[20].src1 != parameter->dst ||
        mir.insns[20].src2 != index_phi->dst ||
        mir.insns[20].immediate != 2 ||
        mir.insns[20].memory_size != 2 ||
        mir.insns[21].src1 != mir.insns[20].dst ||
        mir.insns[21].memory_size != 2 ||
        (mir.insns[21].memory_flags & (1 | 8)) != 0)
        return 0;
    if (mir.insns[22].immediate != '+' ||
        mir.insns[22].src1 != sum_phi->dst ||
        mir.insns[22].src2 != mir.insns[21].dst ||
        !mir_machine_same_location(&mir.insns[4], &mir.insns[24]) ||
        mir.insns[24].src1 != mir.insns[22].dst ||
        !mir_machine_constant_equals(mir.insns[27].dst, 1) ||
        mir.insns[28].immediate != '+' ||
        mir.insns[28].src1 != index_phi->dst ||
        mir.insns[28].src2 != mir.insns[27].dst ||
        !mir_machine_same_location(&mir.insns[7], &mir.insns[29]) ||
        mir.insns[29].src1 != mir.insns[28].dst ||
        mir.insns[30].label != mir.insns[8].label ||
        mir.insns[33].src1 != sum_phi->dst)
        return 0;
    plan->count = (int)mir.insns[13].immediate;
    return 1;
}

static int mir_match_slice_word_sum(struct MirSliceWordSum *plan)
{
    static const int expected_opcodes[36] = {
        MIR_LABEL, MIR_PARAM, MIR_CONST, MIR_NOP, MIR_STORE, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_LABEL, MIR_PHI, MIR_PHI, MIR_NOP,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_RETURN
    };
    const struct MirInsn *sum_phi;
    const struct MirInsn *index_phi;
    int base_offset;
    int base_storage;
    int base_type;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 36 || mir_cfg_block_count() != 4 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    sum_phi = &mir.insns[9];
    index_phi = &mir.insns[10];
    if (!mir_scalar_memory_location(
            &mir.insns[12], &base_type, &base_storage, &base_offset) ||
        base_storage != SC_PARAM ||
        !type_is_struct_object(base_type) ||
        base_offset < 2 ||
        !mir_machine_same_location(&mir.insns[12], &mir.insns[18]) ||
        !mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[4]) ||
        mir.insns[4].memory_size != 2 ||
        !mir_machine_constant_equals(mir.insns[5].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[7]) ||
        mir.insns[7].memory_size != 2)
        return 0;
    if (sum_phi->src1 != mir.insns[2].dst ||
        sum_phi->src2 != mir.insns[24].dst ||
        sum_phi->phi_pred1 != mir.insns[0].label ||
        sum_phi->phi_pred2 != mir.insns[27].label ||
        type_ptr_depth(sum_phi->type) != 0 ||
        (sum_phi->type & 15) != TYPE_INT ||
        type_size(sum_phi->type) != 2 ||
        index_phi->src1 != mir.insns[5].dst ||
        index_phi->src2 != mir.insns[30].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[27].label ||
        type_ptr_depth(index_phi->type) != 0 ||
        (index_phi->type & 15) != TYPE_INT ||
        type_size(index_phi->type) != 2)
        return 0;
    if (mir.insns[13].src1 != mir.insns[12].dst ||
        mir.insns[13].immediate < 0 ||
        mir.insns[13].memory_size != 2 ||
        (mir.insns[13].memory_flags & (1 | 8)) != 0 ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        mir.insns[14].memory_size != 2 ||
        (mir.insns[14].memory_flags & (1 | 8)) != 0 ||
        mir.insns[15].immediate != '<' ||
        mir.insns[15].src1 != index_phi->dst ||
        mir.insns[15].src2 != mir.insns[14].dst ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[16].label != mir.insns[33].label)
        return 0;
    if (mir.insns[19].src1 != mir.insns[18].dst ||
        mir.insns[19].immediate < 0 ||
        mir.insns[19].memory_size != 2 ||
        (mir.insns[19].memory_flags & (1 | 8)) != 0 ||
        mir.insns[20].src1 != mir.insns[19].dst ||
        type_ptr_depth(mir.insns[20].type) != 1 ||
        (mir.insns[20].type & 15) != TYPE_INT ||
        mir.insns[20].memory_size != 2 ||
        (mir.insns[20].memory_flags & (1 | 8)) != 0 ||
        mir.insns[22].src1 != mir.insns[20].dst ||
        mir.insns[22].src2 != index_phi->dst ||
        mir.insns[22].immediate != 2 ||
        mir.insns[22].memory_size != 2 ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[23].memory_size != 2 ||
        (mir.insns[23].memory_flags & (1 | 8)) != 0)
        return 0;
    if (mir.insns[24].immediate != '+' ||
        mir.insns[24].src1 != sum_phi->dst ||
        mir.insns[24].src2 != mir.insns[23].dst ||
        !mir_machine_same_location(&mir.insns[4], &mir.insns[26]) ||
        mir.insns[26].src1 != mir.insns[24].dst ||
        !mir_machine_constant_equals(mir.insns[29].dst, 1) ||
        mir.insns[30].immediate != '+' ||
        mir.insns[30].src1 != index_phi->dst ||
        mir.insns[30].src2 != mir.insns[29].dst ||
        !mir_machine_same_location(&mir.insns[7], &mir.insns[31]) ||
        mir.insns[31].src1 != mir.insns[30].dst ||
        mir.insns[32].label != mir.insns[8].label ||
        mir.insns[35].src1 != sum_phi->dst)
        return 0;
    plan->parameter_stack_offset = base_offset - 2;
    plan->count_offset = (int)mir.insns[13].immediate;
    plan->data_offset = (int)mir.insns[19].immediate;
    if (plan->parameter_stack_offset < 0 ||
        plan->count_offset > 127 || plan->data_offset > 127 ||
        plan->count_offset == plan->data_offset)
        return 0;
    return 1;
}

static int mir_match_conditional_null_identity(
    struct MirConditionalNullIdentity *plan)
{
    static const int expected_opcodes[20] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_LOAD,
        MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_NOP, MIR_STORE, MIR_LOAD,
        MIR_LOAD, MIR_BINARY, MIR_RETURN
    };
    int memory_offset;
    int memory_storage;
    int memory_type;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 20 || mir_cfg_block_count() != 5 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (type_ptr_depth(mir.insns[1].type) != 0 ||
        (mir.insns[1].type & 15) != TYPE_INT ||
        !mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->condition_stack_offset) ||
        type_ptr_depth(mir.insns[2].type) != 1 ||
        !mir_scalar_memory_location(
            &mir.insns[2], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_PARAM ||
        type_ptr_depth(memory_type) != 1 ||
        memory_offset < 2 ||
        mir.insns[4].src1 != mir.insns[1].dst ||
        mir.insns[4].label != mir.insns[9].label ||
        !mir_machine_constant_equals(mir.insns[5].dst, 0))
        return 0;
    plan->pointer_stack_offset = memory_offset - 2;
    if (!mir_machine_same_location(&mir.insns[2], &mir.insns[10]) ||
        mir.insns[8].label != mir.insns[12].label ||
        mir.insns[13].src1 != mir.insns[5].dst ||
        mir.insns[13].src2 != mir.insns[10].dst ||
        mir.insns[13].phi_pred1 != mir.insns[7].label ||
        mir.insns[13].phi_pred2 != mir.insns[11].label ||
        mir.insns[15].src1 != mir.insns[13].dst ||
        !mir_machine_same_location(&mir.insns[15], &mir.insns[16]) ||
        !mir_machine_same_location(&mir.insns[2], &mir.insns[17]) ||
        mir.insns[18].immediate != TOK_EQ ||
        mir.insns[18].src1 != mir.insns[16].dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        mir.insns[19].src1 != mir.insns[18].dst)
        return 0;
    return 1;
}

static int mir_match_wide_constant_equal(
    struct MirWideConstantEqual *plan)
{
    static const int expected_opcodes[17] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_JUMP, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_RETURN, MIR_LABEL, MIR_CONST, MIR_RETURN,
        MIR_NOP, MIR_LABEL
    };
    int memory_offset;
    int memory_storage;
    int memory_type;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 17 || mir_cfg_block_count() != 5 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (!mir_scalar_memory_location(
            &mir.insns[1], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_PARAM ||
        type_ptr_depth(memory_type) != 0 ||
        (memory_type & 15) != TYPE_LONG ||
        type_size(memory_type) != 4 ||
        memory_offset < 2 ||
        mir.insns[3].type != memory_type ||
        mir.insns[4].immediate != TOK_EQ ||
        mir.insns[4].src1 != mir.insns[1].dst ||
        mir.insns[4].src2 != mir.insns[3].dst ||
        mir.insns[5].src1 != mir.insns[4].dst ||
        mir.insns[5].label != mir.insns[12].label ||
        mir.insns[6].label != mir.insns[9].label ||
        mir.insns[8].label != mir.insns[12].label ||
        !mir_machine_constant_equals(mir.insns[10].dst, 1) ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        !mir_machine_constant_equals(mir.insns[13].dst, 0) ||
        mir.insns[14].src1 != mir.insns[13].dst)
        return 0;
    plan->parameter_stack_offset = memory_offset - 2;
    plan->value = (unsigned long)mir.insns[3].immediate;
    return 1;
}

static int mir_match_float_truth_once(struct MirFloatTruthOnce *plan)
{
    static const int expected_opcodes[22] = {
        MIR_LABEL, MIR_PARAM, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_PHI,
        MIR_PHI, MIR_NOP, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_FLOAT_CONST, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_RETURN
    };
    int memory_offset;
    int memory_storage;
    int memory_type;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 22 || mir_cfg_block_count() != 4 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (!mir_scalar_memory_location(
            &mir.insns[1], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_PARAM ||
        !type_is_float(memory_type) ||
        type_size(memory_type) != 4 ||
        memory_offset < 2 ||
        !mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[3]) ||
        mir.insns[5].src1 != mir.insns[1].dst ||
        mir.insns[5].src2 != mir.insns[13].dst ||
        mir.insns[5].phi_pred1 != mir.insns[0].label ||
        mir.insns[5].phi_pred2 != mir.insns[17].label ||
        mir.insns[6].src1 != mir.insns[2].dst ||
        mir.insns[6].src2 != mir.insns[11].dst ||
        mir.insns[6].phi_pred1 != mir.insns[0].label ||
        mir.insns[6].phi_pred2 != mir.insns[17].label ||
        mir.insns[8].src1 != mir.insns[5].dst ||
        mir.insns[8].label != mir.insns[19].label)
        return 0;
    if (!mir_machine_constant_equals(mir.insns[10].dst, 1) ||
        mir.insns[11].immediate != '+' ||
        mir.insns[11].src1 != mir.insns[6].dst ||
        mir.insns[11].src2 != mir.insns[10].dst ||
        !mir_machine_same_location(&mir.insns[3], &mir.insns[12]) ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[13].immediate != 0 ||
        !mir_machine_same_location(&mir.insns[1], &mir.insns[15]) ||
        mir.insns[15].src1 != mir.insns[13].dst ||
        mir.insns[18].label != mir.insns[4].label ||
        mir.insns[21].src1 != mir.insns[6].dst)
        return 0;
    plan->parameter_stack_offset = memory_offset - 2;
    return 1;
}

static int mir_match_nested_word_long_select(
    struct MirNestedWordLongSelect *plan)
{
    static const int constant_opcodes[26] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_UNARY, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_BRANCH_FALSE, MIR_CONST, MIR_UNARY, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_FLOAT_CONST, MIR_LABEL, MIR_LABEL, MIR_PHI,
        MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_UNARY, MIR_RETURN
    };
    static const int parameter_opcodes[28] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_NOP,
        MIR_BRANCH_FALSE, MIR_CONST, MIR_UNARY, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_BRANCH_FALSE, MIR_CONST, MIR_UNARY,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_UNARY, MIR_LABEL,
        MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_LABEL, MIR_PHI,
        MIR_UNARY, MIR_RETURN
    };
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if ((mir.count != 26 && mir.count != 28) ||
        mir_cfg_block_count() != 9 || mir.has_vla ||
        type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_LONG ||
        type_size(mir.return_type) != 4)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            (mir.count == 26
                ? constant_opcodes[instruction]
                : parameter_opcodes[instruction]))
            return 0;
    if (mir.count == 26) {
        if (!mir_machine_parameter_value_offset(
                mir.insns[1].dst,
                &plan->first_condition_stack_offset) ||
            !mir_machine_parameter_value_offset(
                mir.insns[2].dst,
                &plan->second_condition_stack_offset) ||
            mir.insns[4].src1 != mir.insns[1].dst ||
            mir.insns[4].label != mir.insns[9].label ||
            !mir_machine_constant_equals(mir.insns[5].dst, 1) ||
            mir.insns[6].immediate != 0 ||
            mir.insns[6].src1 != mir.insns[5].dst ||
            !type_is_float(mir.insns[6].type) ||
            mir.insns[8].label != mir.insns[22].label ||
            mir.insns[11].src1 != mir.insns[2].dst ||
            mir.insns[11].label != mir.insns[16].label ||
            !mir_machine_constant_equals(mir.insns[12].dst, 2) ||
            mir.insns[13].immediate != 0 ||
            mir.insns[13].src1 != mir.insns[12].dst ||
            !type_is_float(mir.insns[13].type) ||
            mir.insns[15].label != mir.insns[19].label ||
            mir.insns[17].immediate != 1080033280L)
            return 0;
        if (mir.insns[20].src1 != mir.insns[13].dst ||
            mir.insns[20].src2 != mir.insns[17].dst ||
            mir.insns[20].phi_pred1 != mir.insns[14].label ||
            mir.insns[20].phi_pred2 != mir.insns[18].label ||
            mir.insns[23].src1 != mir.insns[6].dst ||
            mir.insns[23].src2 != mir.insns[20].dst ||
            mir.insns[23].phi_pred1 != mir.insns[7].label ||
            mir.insns[23].phi_pred2 != mir.insns[21].label ||
            mir.insns[24].immediate != 0 ||
            mir.insns[24].src1 != mir.insns[23].dst ||
            (mir.insns[24].type & 15) != TYPE_LONG ||
            mir.insns[25].src1 != mir.insns[24].dst)
            return 0;
        plan->first_value = 1;
        plan->second_value = 2;
        plan->third_value = 3;
        return 1;
    }
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst,
            &plan->first_condition_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst,
            &plan->second_condition_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[3].dst, &plan->third_stack_offset) ||
        mir.insns[5].src1 != mir.insns[1].dst ||
        mir.insns[5].label != mir.insns[10].label ||
        !mir_machine_constant_equals(mir.insns[6].dst, 1) ||
        mir.insns[7].immediate != 0 ||
        mir.insns[7].src1 != mir.insns[6].dst ||
        !type_is_float(mir.insns[7].type) ||
        mir.insns[9].label != mir.insns[24].label ||
        mir.insns[12].src1 != mir.insns[2].dst ||
        mir.insns[12].label != mir.insns[17].label ||
        !mir_machine_constant_equals(mir.insns[13].dst, 2) ||
        mir.insns[14].immediate != 0 ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        !type_is_float(mir.insns[14].type) ||
        mir.insns[16].label != mir.insns[21].label ||
        mir.insns[19].immediate != 0 ||
        mir.insns[19].src1 != mir.insns[3].dst ||
        !type_is_float(mir.insns[19].type))
        return 0;
    if (mir.insns[22].src1 != mir.insns[14].dst ||
        mir.insns[22].src2 != mir.insns[19].dst ||
        mir.insns[22].phi_pred1 != mir.insns[15].label ||
        mir.insns[22].phi_pred2 != mir.insns[20].label ||
        mir.insns[25].src1 != mir.insns[7].dst ||
        mir.insns[25].src2 != mir.insns[22].dst ||
        mir.insns[25].phi_pred1 != mir.insns[8].label ||
        mir.insns[25].phi_pred2 != mir.insns[23].label ||
        mir.insns[26].immediate != 0 ||
        mir.insns[26].src1 != mir.insns[25].dst ||
        (mir.insns[26].type & 15) != TYPE_LONG ||
        mir.insns[27].src1 != mir.insns[26].dst)
        return 0;
    plan->first_value = 1;
    plan->second_value = 2;
    plan->third_is_parameter = 1;
    return 1;
}

static int mir_match_float_int_truth(struct MirFloatIntTruth *plan)
{
    static const int and_opcodes[24] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_BRANCH_FALSE,
        MIR_NOP, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_CONST,
        MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_RETURN
    };
    static const int or_opcodes[32] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_PHI, MIR_BRANCH_FALSE, MIR_CONST, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_RETURN
    };
    int memory_offset;
    int memory_storage;
    int memory_type;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if ((mir.count != 24 && mir.count != 32) ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2 ||
        mir_cfg_block_count() != (mir.count == 24 ? 8 : 12))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            (mir.count == 24
                ? and_opcodes[instruction]
                : or_opcodes[instruction]))
            return 0;
    if (!mir_scalar_memory_location(
            &mir.insns[1], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_PARAM ||
        !type_is_float(memory_type) ||
        type_size(memory_type) != 4 ||
        memory_offset < 2 ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &plan->int_stack_offset))
        return 0;
    plan->float_stack_offset = memory_offset - 2;
    if (mir.count == 24) {
        if (mir.insns[4].src1 != mir.insns[1].dst ||
            mir.insns[4].label != mir.insns[10].label ||
            mir.insns[6].src1 != mir.insns[2].dst ||
            mir.insns[6].label != mir.insns[10].label ||
            !mir_machine_constant_equals(mir.insns[8].dst, 1) ||
            !mir_machine_constant_equals(mir.insns[11].dst, 0) ||
            mir.insns[13].src1 != mir.insns[8].dst ||
            mir.insns[13].src2 != mir.insns[11].dst ||
            mir.insns[14].src1 != mir.insns[13].dst ||
            mir.insns[14].label != mir.insns[18].label ||
            !mir_machine_constant_equals(mir.insns[15].dst, 1) ||
            !mir_machine_constant_equals(mir.insns[19].dst, 0) ||
            mir.insns[22].src1 != mir.insns[15].dst ||
            mir.insns[22].src2 != mir.insns[19].dst ||
            mir.insns[23].src1 != mir.insns[22].dst)
            return 0;
        plan->operation = '&';
        return 1;
    }
    if (mir.insns[4].src1 != mir.insns[1].dst ||
        mir.insns[4].label != mir.insns[8].label ||
        !mir_machine_constant_equals(mir.insns[6].dst, 1) ||
        mir.insns[10].src1 != mir.insns[2].dst ||
        mir.insns[10].label != mir.insns[14].label ||
        !mir_machine_constant_equals(mir.insns[12].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[15].dst, 0) ||
        mir.insns[17].src1 != mir.insns[12].dst ||
        mir.insns[17].src2 != mir.insns[15].dst ||
        mir.insns[21].src1 != mir.insns[6].dst ||
        mir.insns[21].src2 != mir.insns[17].dst ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        mir.insns[22].label != mir.insns[26].label ||
        !mir_machine_constant_equals(mir.insns[23].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[27].dst, 0) ||
        mir.insns[30].src1 != mir.insns[23].dst ||
        mir.insns[30].src2 != mir.insns[27].dst ||
        mir.insns[31].src1 != mir.insns[30].dst)
        return 0;
    plan->operation = '|';
    return 1;
}

static int mir_match_conditional_float_long(
    struct MirConditionalFloatLong *plan)
{
    const struct MirInsn *false_value;
    int memory_offset;
    int memory_storage;
    int memory_type;
    int instruction;
    long true_value;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 18 || mir_cfg_block_count() != 5 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_LONG ||
        type_size(mir.return_type) != 4 ||
        mir.insns[0].opcode != MIR_LABEL ||
        mir.insns[1].opcode != MIR_PARAM ||
        mir.insns[2].opcode != MIR_PARAM ||
        mir.insns[3].opcode != MIR_NOP ||
        mir.insns[4].opcode != MIR_BRANCH_FALSE ||
        mir.insns[5].opcode != MIR_CONST ||
        mir.insns[6].opcode != MIR_UNARY ||
        mir.insns[7].opcode != MIR_LABEL ||
        mir.insns[8].opcode != MIR_JUMP ||
        mir.insns[9].opcode != MIR_LABEL ||
        mir.insns[10].opcode != MIR_NOP ||
        mir.insns[13].opcode != MIR_LABEL ||
        mir.insns[14].opcode != MIR_LABEL ||
        mir.insns[15].opcode != MIR_PHI ||
        mir.insns[16].opcode != MIR_UNARY ||
        mir.insns[17].opcode != MIR_RETURN)
        return 0;
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->condition_stack_offset) ||
        mir.insns[4].src1 != mir.insns[1].dst ||
        mir.insns[4].label != mir.insns[9].label ||
        !mir_machine_constant_value(
            mir.insns[5].dst, &true_value, 0) ||
        true_value < -32768 || true_value > 65535 ||
        mir.insns[6].immediate != 0 ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        !type_is_float(mir.insns[6].type) ||
        mir.insns[8].label != mir.insns[14].label ||
        mir.insns[15].src1 != mir.insns[6].dst ||
        mir.insns[15].phi_pred1 != mir.insns[7].label ||
        mir.insns[15].phi_pred2 != mir.insns[13].label ||
        mir.insns[16].immediate != 0 ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        (mir.insns[16].type & 15) != TYPE_LONG ||
        mir.insns[17].src1 != mir.insns[16].dst)
        return 0;
    plan->true_value = (int)true_value;
    if (mir.insns[11].opcode == MIR_FLOAT_CONST &&
        mir.insns[12].opcode == MIR_BINARY) {
        if (!mir_scalar_memory_location(
                &mir.insns[2], &memory_type, &memory_storage,
                &memory_offset) ||
            memory_storage != SC_PARAM ||
            !type_is_float(memory_type) ||
            type_size(memory_type) != 4 ||
            memory_offset < 2 ||
            mir.insns[12].immediate != '+' ||
            mir.insns[12].src1 != mir.insns[2].dst ||
            mir.insns[12].src2 != mir.insns[11].dst ||
            !type_is_float(mir.insns[12].type))
            return 0;
        plan->kind = MIR_CONDITIONAL_FLOAT_ADD;
        plan->argument_stack_offset = memory_offset - 2;
        plan->add_bits = (unsigned long)mir.insns[11].immediate;
        false_value = &mir.insns[12];
    } else if (mir.insns[11].opcode == MIR_ARG &&
               mir.insns[12].opcode == MIR_CALL) {
        if (!mir_machine_parameter_value_offset(
                mir.insns[2].dst, &plan->argument_stack_offset) ||
            mir.insns[11].src1 != mir.insns[2].dst ||
            !type_is_float(mir.insns[12].type))
            return 0;
        plan->function = find_global(mir.insns[12].name);
        if (plan->function == NULL || !plan->function->is_defined ||
            plan->function->storage != SC_FUNC ||
            plan->function->is_funcptr ||
            plan->function->is_noreturn ||
            !plan->function->has_proto ||
            plan->function->proto_nargs != 1 ||
            plan->function->proto_variadic ||
            plan->function->proto_types[0] != mir.insns[11].type ||
            mir.insns[12].memory_flags != 0)
            return 0;
        plan->kind = MIR_CONDITIONAL_FLOAT_CALL;
        false_value = &mir.insns[12];
    } else {
        return 0;
    }
    if (mir.insns[15].src2 != false_value->dst)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_OPAQUE)
            return 0;
    return 1;
}

static int mir_match_conditional_pointer_float_long(
    struct MirConditionalPointerFloatLong *plan)
{
    static const int expected_opcodes[20] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_BRANCH_FALSE, MIR_ADDRESS,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_LABEL, MIR_LABEL,
        MIR_PHI, MIR_NOP, MIR_STORE, MIR_LOAD, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_RETURN
    };
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 20 || mir_cfg_block_count() != 5 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_LONG ||
        type_size(mir.return_type) != 4)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->condition_stack_offset) ||
        mir.insns[3].src1 != mir.insns[1].dst ||
        mir.insns[3].label != mir.insns[7].label ||
        type_ptr_depth(mir.insns[4].type) != 1 ||
        type_ptr_depth(mir.insns[8].type) != 1 ||
        mir.insns[6].label != mir.insns[10].label ||
        mir.insns[11].src1 != mir.insns[4].dst ||
        mir.insns[11].src2 != mir.insns[8].dst ||
        mir.insns[11].phi_pred1 != mir.insns[5].label ||
        mir.insns[11].phi_pred2 != mir.insns[9].label ||
        mir.insns[13].src1 != mir.insns[11].dst ||
        !mir_machine_same_location(&mir.insns[13], &mir.insns[14]) ||
        mir.insns[16].src1 != mir.insns[14].dst ||
        mir.insns[16].src2 != mir.insns[15].dst ||
        mir.insns[16].immediate != 4 ||
        mir.insns[16].memory_size != 4 ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        !type_is_float(mir.insns[17].type) ||
        mir.insns[17].memory_size != 4 ||
        (mir.insns[17].memory_flags & (1 | 8)) != 0 ||
        mir.insns[18].immediate != 0 ||
        mir.insns[18].src1 != mir.insns[17].dst ||
        (mir.insns[18].type & 15) != TYPE_LONG ||
        mir.insns[19].src1 != mir.insns[18].dst ||
        mir.insns[15].immediate < 0 ||
        mir.insns[15].immediate > 8191)
        return 0;
    plan->true_root = find_global(mir.insns[4].name);
    plan->false_pointer = find_global(mir.insns[8].name);
    if (plan->true_root == NULL || !plan->true_root->is_defined ||
        plan->true_root->is_volatile ||
        plan->false_pointer == NULL ||
        !plan->false_pointer->is_defined ||
        plan->false_pointer->is_volatile)
        return 0;
    plan->element_offset = (int)mir.insns[15].immediate * 4;
    return 1;
}

static int mir_match_nested_member_float_long(
    struct MirNestedMemberFloatLong *plan)
{
    static const int expected_opcodes[29] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_NOP,
        MIR_BRANCH_FALSE, MIR_CONST, MIR_UNARY, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_BRANCH_FALSE, MIR_CONST, MIR_UNARY,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_LABEL,
        MIR_LABEL, MIR_PHI, MIR_UNARY, MIR_RETURN
    };
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 29 || mir_cfg_block_count() != 9 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_LONG ||
        type_size(mir.return_type) != 4)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst,
            &plan->first_condition_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst,
            &plan->second_condition_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[3].dst, &plan->pointer_stack_offset) ||
        mir.insns[5].src1 != mir.insns[1].dst ||
        mir.insns[5].label != mir.insns[10].label ||
        !mir_machine_constant_equals(mir.insns[6].dst, 1) ||
        mir.insns[7].immediate != 0 ||
        !type_is_float(mir.insns[7].type) ||
        mir.insns[9].label != mir.insns[25].label ||
        mir.insns[12].src1 != mir.insns[2].dst ||
        mir.insns[12].label != mir.insns[17].label ||
        !mir_machine_constant_equals(mir.insns[13].dst, 2) ||
        mir.insns[14].immediate != 0 ||
        !type_is_float(mir.insns[14].type) ||
        mir.insns[16].label != mir.insns[22].label)
        return 0;
    if (mir.insns[19].src1 != mir.insns[3].dst ||
        mir.insns[19].immediate < 0 ||
        mir.insns[19].immediate > 32767 ||
        mir.insns[19].memory_size != 4 ||
        mir.insns[20].src1 != mir.insns[19].dst ||
        !type_is_float(mir.insns[20].type) ||
        mir.insns[20].memory_size != 4 ||
        (mir.insns[20].memory_flags & (1 | 8)) != 0 ||
        mir.insns[23].src1 != mir.insns[14].dst ||
        mir.insns[23].src2 != mir.insns[20].dst ||
        mir.insns[23].phi_pred1 != mir.insns[15].label ||
        mir.insns[23].phi_pred2 != mir.insns[21].label ||
        mir.insns[26].src1 != mir.insns[7].dst ||
        mir.insns[26].src2 != mir.insns[23].dst ||
        mir.insns[26].phi_pred1 != mir.insns[8].label ||
        mir.insns[26].phi_pred2 != mir.insns[24].label ||
        mir.insns[27].immediate != 0 ||
        (mir.insns[27].type & 15) != TYPE_LONG ||
        mir.insns[28].src1 != mir.insns[27].dst)
        return 0;
    plan->first_value = 1;
    plan->second_value = 2;
    plan->member_offset = (int)mir.insns[19].immediate;
    return 1;
}

static int mir_match_conditional_float_compare_long(
    struct MirConditionalFloatCompareLong *plan)
{
    static const int expected_opcodes[26] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_LABEL,
        MIR_PHI, MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_UNARY, MIR_RETURN
    };
    int memory_offset;
    int memory_storage;
    int memory_type;
    int instruction;
    long nonpositive_value;
    long positive_value;
    long true_value;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 26 || mir_cfg_block_count() != 9 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_LONG ||
        type_size(mir.return_type) != 4)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->condition_stack_offset) ||
        !mir_scalar_memory_location(
            &mir.insns[2], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_PARAM ||
        !type_is_float(memory_type) ||
        type_size(memory_type) != 4 ||
        memory_offset < 2 ||
        mir.insns[4].src1 != mir.insns[1].dst ||
        mir.insns[4].label != mir.insns[8].label ||
        !mir_machine_constant_value(
            mir.insns[5].dst, &true_value, 0) ||
        true_value < -32768 || true_value > 65535 ||
        mir.insns[7].label != mir.insns[22].label ||
        mir.insns[10].immediate != 0 ||
        mir.insns[11].immediate != '>' ||
        mir.insns[11].src1 != mir.insns[2].dst ||
        mir.insns[11].src2 != mir.insns[10].dst ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[12].label != mir.insns[16].label)
        return 0;
    if (!mir_machine_constant_value(
            mir.insns[13].dst, &positive_value, 0) ||
        positive_value < -32768 || positive_value > 65535 ||
        !mir_machine_constant_value(
            mir.insns[17].dst, &nonpositive_value, 0) ||
        nonpositive_value < -32768 || nonpositive_value > 65535 ||
        mir.insns[20].src1 != mir.insns[13].dst ||
        mir.insns[20].src2 != mir.insns[17].dst ||
        mir.insns[23].src1 != mir.insns[5].dst ||
        mir.insns[23].src2 != mir.insns[20].dst ||
        mir.insns[24].immediate != 0 ||
        (mir.insns[24].type & 15) != TYPE_LONG ||
        mir.insns[25].src1 != mir.insns[24].dst)
        return 0;
    plan->float_stack_offset = memory_offset - 2;
    plan->true_value = (int)true_value;
    plan->positive_value = (int)positive_value;
    plan->nonpositive_value = (int)nonpositive_value;
    return 1;
}

static int mir_match_conditional_bool(struct MirConditionalBool *plan)
{
    static const int expected_opcodes[14] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_BRANCH_FALSE, MIR_CONST,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_LABEL,
        MIR_PHI, MIR_UNARY, MIR_RETURN
    };
    long false_value;
    int instruction;
    long true_value;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 14 || mir_cfg_block_count() != 5 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        type_is_float(mir.return_type) ||
        (type_size(mir.return_type) != 1 &&
         type_size(mir.return_type) != 2))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->condition_stack_offset) ||
        mir.insns[3].src1 != mir.insns[1].dst ||
        mir.insns[3].label != mir.insns[7].label ||
        !mir_machine_constant_value(
            mir.insns[4].dst, &true_value, 0) ||
        mir.insns[6].label != mir.insns[10].label ||
        !mir_machine_constant_value(
            mir.insns[8].dst, &false_value, 0) ||
        mir.insns[11].src1 != mir.insns[4].dst ||
        mir.insns[11].src2 != mir.insns[8].dst ||
        mir.insns[11].phi_pred1 != mir.insns[5].label ||
        mir.insns[11].phi_pred2 != mir.insns[9].label ||
        mir.insns[12].immediate != 0 ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[12].type != mir.return_type ||
        mir.insns[13].src1 != mir.insns[12].dst)
        return 0;
    plan->result_type = mir.return_type;
    if ((mir.return_type & 15) == TYPE_BOOL) {
        plan->true_value = true_value != 0;
        plan->false_value = false_value != 0;
    } else {
        if (!mir_machine_convert_integer(
                true_value, mir.return_type, &true_value) ||
            !mir_machine_convert_integer(
                false_value, mir.return_type, &false_value))
            return 0;
        plan->true_value = (int)true_value;
        plan->false_value = (int)false_value;
    }
    return 1;
}

static int mir_match_logical_or_parameters(
    struct MirLogicalOrParameters *plan)
{
    static const int expected_opcodes[23] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_PHI, MIR_RETURN
    };
    const struct MirInsn *first = &mir.insns[1];
    const struct MirInsn *second = &mir.insns[2];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 23 || mir_cfg_block_count() != 8 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return mir_machine_reject(
            "logical-or-parameters", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "logical-or-parameters", "opcode");
    if (type_is_float(first->type) || type_size(first->type) != 2 ||
        type_is_float(second->type) || type_size(second->type) != 2 ||
        !mir_machine_parameter_value_offset(
            first->dst, &plan->first_stack_offset) ||
        !mir_machine_parameter_value_offset(
            second->dst, &plan->second_stack_offset) ||
        mir.insns[4].src1 != first->dst ||
        mir.insns[4].label != mir.insns[8].label ||
        !mir_machine_constant_equals(mir.insns[6].dst, 1) ||
        mir.insns[7].label != mir.insns[20].label ||
        mir.insns[10].src1 != second->dst ||
        mir.insns[10].label != mir.insns[14].label ||
        !mir_machine_constant_equals(mir.insns[12].dst, 1) ||
        mir.insns[13].label != mir.insns[16].label ||
        !mir_machine_constant_equals(mir.insns[15].dst, 0) ||
        mir.insns[17].src1 != mir.insns[12].dst ||
        mir.insns[17].src2 != mir.insns[15].dst ||
        mir.insns[17].phi_pred1 != mir.insns[11].label ||
        mir.insns[17].phi_pred2 != mir.insns[14].label ||
        mir.insns[19].label != mir.insns[20].label ||
        mir.insns[21].src1 != mir.insns[6].dst ||
        mir.insns[21].src2 != mir.insns[17].dst ||
        mir.insns[21].phi_pred1 != mir.insns[5].label ||
        mir.insns[21].phi_pred2 != mir.insns[18].label ||
        mir.insns[22].src1 != mir.insns[21].dst)
        return mir_machine_reject(
            "logical-or-parameters", "flow");
    return 1;
}

static int mir_match_cleared_record_append(
    struct MirClearedRecordAppend *plan)
{
    static const int expected_opcodes[46] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY,
        MIR_STORE_INDIRECT, MIR_NOP, MIR_STORE, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LOAD, MIR_NOP,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CONST, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_ARG, MIR_LOAD,
        MIR_ARG, MIR_CALL, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_NOP,
        MIR_STORE_INDIRECT, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_NOP,
        MIR_STORE_INDIRECT, MIR_NOP, MIR_RETURN
    };
    int clear_arguments[3];
    int copy_arguments[2];
    int memory_offset;
    int memory_storage;
    int memory_type;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 46 || mir_cfg_block_count() != 1 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (!mir_scalar_memory_location(
            &mir.insns[4], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        type_size(memory_type) != 2 ||
        !mir_machine_named_nonvolatile(&mir.insns[4]) ||
        !mir_machine_same_location(&mir.insns[4], &mir.insns[12]) ||
        mir.insns[5].src1 != mir.insns[4].dst ||
        mir.insns[5].memory_size != 2 ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        mir.insns[6].memory_size != 2 ||
        !mir_machine_constant_equals(mir.insns[7].dst, 1) ||
        mir.insns[8].immediate != '+' ||
        mir.insns[8].src1 != mir.insns[6].dst ||
        mir.insns[8].src2 != mir.insns[7].dst ||
        mir.insns[9].src1 != mir.insns[5].dst ||
        mir.insns[9].src2 != mir.insns[8].dst ||
        mir.insns[9].memory_size != 2 ||
        !mir_machine_unobservable_local_store(&mir.insns[11]) ||
        mir.insns[11].src1 != mir.insns[6].dst)
        return 0;
    plan->root = find_global(mir.insns[4].name);
    if (plan->root == NULL || !plan->root->is_defined ||
        plan->root->is_volatile)
        return 0;
    plan->root_offset = memory_offset;
    plan->cursor_member_offset = (int)mir.insns[5].immediate;
    if (mir.insns[13].src1 != mir.insns[12].dst ||
        mir.insns[13].memory_size != 2 ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        mir.insns[14].memory_size != 2 ||
        mir.insns[17].immediate != '*' ||
        mir.insns[17].src1 != mir.insns[6].dst ||
        mir.insns[17].src2 != mir.insns[16].dst ||
        mir.insns[16].immediate <= 0 ||
        mir.insns[16].immediate > 32767 ||
        mir.insns[18].immediate != '+' ||
        mir.insns[18].src1 != mir.insns[14].dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[20]) ||
        mir.insns[20].src1 != mir.insns[18].dst)
        return 0;
    plan->array_member_offset = (int)mir.insns[13].immediate;
    plan->stride = (int)mir.insns[16].immediate;
    if (!mir_machine_same_location(&mir.insns[20], &mir.insns[21]) ||
        mir.insns[23].src1 != mir.insns[21].dst ||
        !mir_machine_constant_equals(mir.insns[24].dst, 0) ||
        mir.insns[25].src1 != mir.insns[24].dst ||
        mir.insns[26].immediate != plan->stride ||
        mir.insns[28].src1 != mir.insns[26].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[29], clear_arguments) ||
        clear_arguments[0] != mir.insns[21].dst ||
        clear_arguments[1] != mir.insns[24].dst ||
        clear_arguments[2] != mir.insns[26].dst)
        return 0;
    plan->clear_function = find_global(mir.insns[29].name);
    if (plan->clear_function == NULL ||
        plan->clear_function->storage != SC_FUNC ||
        plan->clear_function->is_funcptr ||
        plan->clear_function->is_noreturn ||
        (mir.insns[29].memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME |
          MIR_CALL_FLAG_INLINE_SUBSTITUTABLE)) != 0)
        return 0;
    if (!mir_machine_same_location(&mir.insns[20], &mir.insns[30]) ||
        mir.insns[31].src1 != mir.insns[30].dst ||
        mir.insns[31].memory_size != 18 ||
        mir.insns[32].src1 != mir.insns[31].dst ||
        !mir_machine_same_location(&mir.insns[1], &mir.insns[33]) ||
        mir.insns[34].src1 != mir.insns[33].dst ||
        !mir_machine_two_call_arguments(
            &mir.insns[35], copy_arguments) ||
        copy_arguments[0] != mir.insns[31].dst ||
        copy_arguments[1] != mir.insns[33].dst)
        return 0;
    plan->copy_function = find_global(mir.insns[35].name);
    if (plan->copy_function == NULL ||
        plan->copy_function->storage != SC_FUNC ||
        plan->copy_function->is_funcptr ||
        plan->copy_function->is_noreturn ||
        (mir.insns[35].memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME |
          MIR_CALL_FLAG_INLINE_SUBSTITUTABLE)) != 0)
        return 0;
    if (!mir_machine_same_location(&mir.insns[20], &mir.insns[36]) ||
        mir.insns[37].src1 != mir.insns[36].dst ||
        mir.insns[37].memory_size != 2 ||
        mir.insns[39].src1 != mir.insns[37].dst ||
        mir.insns[39].src2 != mir.insns[2].dst ||
        mir.insns[39].memory_size != 2 ||
        !mir_machine_same_location(&mir.insns[20], &mir.insns[40]) ||
        mir.insns[41].src1 != mir.insns[40].dst ||
        mir.insns[41].memory_size != 2 ||
        mir.insns[43].src1 != mir.insns[41].dst ||
        mir.insns[43].src2 != mir.insns[3].dst ||
        mir.insns[43].memory_size != 2 ||
        mir.insns[45].src1 != mir.insns[6].dst)
        return 0;
    plan->name_field_offset = (int)mir.insns[31].immediate;
    plan->kind_field_offset = (int)mir.insns[37].immediate;
    plan->value_field_offset = (int)mir.insns[41].immediate;
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->name_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &plan->kind_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[3].dst, &plan->value_stack_offset) ||
        plan->name_field_offset != 0 ||
        plan->kind_field_offset < 0 ||
        plan->value_field_offset < 0)
        return 0;
    return 1;
}

static int mir_match_record_name_search(
    struct MirRecordNameSearch *plan)
{
    static const int expected_opcodes[43] = {
        MIR_LABEL, MIR_PARAM, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_LOAD, MIR_PHI, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BINARY, MIR_MEMBER_ADDRESS,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_UNARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_RETURN, MIR_LABEL, MIR_LABEL,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_RETURN
    };
    int arguments[2];
    int memory_offset;
    int memory_storage;
    int memory_type;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 43 || mir_cfg_block_count() != 5 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->name_stack_offset) ||
        !mir_scalar_memory_location(
            &mir.insns[2], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        type_size(memory_type) != 2 ||
        !mir_machine_named_nonvolatile(&mir.insns[2]) ||
        !mir_machine_same_location(&mir.insns[2], &mir.insns[16]) ||
        mir.insns[3].src1 != mir.insns[2].dst ||
        mir.insns[4].src1 != mir.insns[3].dst ||
        mir.insns[4].memory_size != 2 ||
        !mir_machine_constant_equals(mir.insns[5].dst, 1) ||
        mir.insns[6].immediate != '-' ||
        mir.insns[6].src1 != mir.insns[4].dst ||
        mir.insns[6].src2 != mir.insns[5].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[8]) ||
        mir.insns[8].src1 != mir.insns[6].dst)
        return 0;
    plan->root = find_global(mir.insns[2].name);
    if (plan->root == NULL || !plan->root->is_defined ||
        plan->root->is_volatile)
        return 0;
    plan->root_offset = memory_offset;
    plan->cursor_member_offset = (int)mir.insns[3].immediate;
    if (!mir_machine_same_location(&mir.insns[1], &mir.insns[10]) ||
        mir.insns[11].src1 != mir.insns[6].dst ||
        mir.insns[11].src2 != mir.insns[36].dst ||
        mir.insns[11].phi_pred1 != mir.insns[0].label ||
        mir.insns[11].phi_pred2 != mir.insns[33].label ||
        !mir_machine_constant_equals(mir.insns[13].dst, 0) ||
        mir.insns[14].immediate != TOK_GE ||
        mir.insns[14].src1 != mir.insns[11].dst ||
        mir.insns[14].src2 != mir.insns[13].dst ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        mir.insns[15].label != mir.insns[39].label)
        return 0;
    if (mir.insns[17].src1 != mir.insns[16].dst ||
        mir.insns[18].src1 != mir.insns[17].dst ||
        mir.insns[18].memory_size != 2 ||
        mir.insns[21].immediate != '*' ||
        mir.insns[21].src1 != mir.insns[11].dst ||
        mir.insns[21].src2 != mir.insns[20].dst ||
        mir.insns[20].immediate <= 0 ||
        mir.insns[20].immediate > 32767 ||
        mir.insns[22].immediate != '+' ||
        mir.insns[22].src1 != mir.insns[18].dst ||
        mir.insns[22].src2 != mir.insns[21].dst ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[23].memory_size <= 0 ||
        mir.insns[24].src1 != mir.insns[23].dst ||
        !mir_machine_same_location(&mir.insns[1], &mir.insns[25]) ||
        mir.insns[26].src1 != mir.insns[25].dst ||
        !mir_machine_two_call_arguments(
            &mir.insns[27], arguments) ||
        arguments[0] != mir.insns[23].dst ||
        arguments[1] != mir.insns[25].dst)
        return 0;
    plan->array_member_offset = (int)mir.insns[17].immediate;
    plan->stride = (int)mir.insns[20].immediate;
    plan->name_field_offset = (int)mir.insns[23].immediate;
    plan->compare_function = find_global(mir.insns[27].name);
    if (plan->compare_function == NULL ||
        plan->compare_function->storage != SC_FUNC ||
        plan->compare_function->is_funcptr ||
        plan->compare_function->is_noreturn ||
        (mir.insns[27].memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME |
          MIR_CALL_FLAG_INLINE_SUBSTITUTABLE)) != 0)
        return 0;
    if (mir.insns[28].immediate != '!' ||
        mir.insns[28].src1 != mir.insns[27].dst ||
        mir.insns[29].src1 != mir.insns[28].dst ||
        mir.insns[29].label != mir.insns[32].label ||
        mir.insns[31].src1 != mir.insns[11].dst ||
        !mir_machine_constant_equals(mir.insns[35].dst, 1) ||
        mir.insns[36].immediate != '-' ||
        mir.insns[36].src1 != mir.insns[11].dst ||
        mir.insns[36].src2 != mir.insns[35].dst ||
        !mir_machine_same_location(&mir.insns[8], &mir.insns[37]) ||
        mir.insns[37].src1 != mir.insns[36].dst ||
        mir.insns[38].label != mir.insns[9].label ||
        mir.insns[41].immediate != 65535 ||
        mir.insns[42].src1 != mir.insns[41].dst)
        return 0;
    return 1;
}

static int mir_match_sequential_unary_reports(
    struct MirSequentialUnaryReports *plan)
{
    int arguments[6];
    int parameter;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 29 || mir_cfg_block_count() != 1 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID ||
        mir.insns[0].opcode != MIR_LABEL ||
        mir.insns[6].opcode != MIR_STRING_ADDRESS ||
        mir.insns[7].opcode != MIR_ARG ||
        mir.insns[7].src1 != mir.insns[6].dst ||
        mir.insns[28].opcode != MIR_CALL)
        return 0;
    for (parameter = 0; parameter < 5; ++parameter) {
        int base = 1 + parameter;
        int call_base = 8 + parameter * 4;
        struct Sym *helper;

        if (mir.insns[base].opcode != MIR_PARAM ||
            (mir.insns[base].type & 15) != TYPE_BOOL ||
            !mir_machine_parameter_value_offset(
                mir.insns[base].dst,
                &plan->parameter_stack_offsets[parameter]) ||
            mir.insns[call_base].opcode != MIR_NOP ||
            mir.insns[call_base + 1].opcode != MIR_ARG ||
            mir.insns[call_base + 1].src1 != mir.insns[base].dst ||
            mir.insns[call_base + 2].opcode != MIR_CALL ||
            !mir_call_uses_value(
                &mir.insns[call_base + 2],
                mir.insns[base].dst) ||
            mir.insns[call_base + 3].opcode != MIR_ARG ||
            mir.insns[call_base + 3].src1 !=
                mir.insns[call_base + 2].dst)
            return 0;
        helper = find_global(mir.insns[call_base + 2].name);
        if (helper == NULL || !helper->is_defined ||
            helper->storage != SC_FUNC ||
            helper->is_funcptr || helper->is_noreturn ||
            !helper->has_proto || helper->proto_nargs != 1 ||
            helper->proto_variadic ||
            helper->proto_types[0] !=
                mir.insns[call_base + 1].type ||
            (helper->type & 15) != TYPE_CHAR ||
            mir.insns[call_base + 2].memory_flags != 0)
            return 0;
        if (parameter == 0)
            plan->helper = helper;
        else if (plan->helper != helper)
            return 0;
    }
    if (!mir_machine_six_call_arguments(
            &mir.insns[28], arguments) ||
        arguments[0] != mir.insns[6].dst)
        return 0;
    for (parameter = 0; parameter < 5; ++parameter)
        if (arguments[parameter + 1] !=
            mir.insns[10 + parameter * 4].dst)
            return 0;
    plan->print_function = find_global(mir.insns[28].name);
    if (plan->print_function == NULL ||
        strcmp(mir.insns[28].name, "printf") ||
        (mir.insns[28].memory_flags & MIR_CALL_FLAG_VARIADIC) == 0 ||
        (mir.insns[28].memory_flags &
         MIR_CALL_FLAG_FORMAT_RUNTIME) != 0)
        return 0;
    plan->string_id = (int)mir.insns[6].immediate;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_OPAQUE)
            return 0;
    return 1;
}

static int mir_match_nibble_append(struct MirNibbleAppend *plan)
{
    static const int expected_opcodes[32] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_CONST, MIR_BINARY, MIR_LABEL, MIR_LABEL, MIR_PHI,
        MIR_UNARY, MIR_STORE_INDIRECT, MIR_LOAD, MIR_RETURN
    };
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 32 || mir_cfg_block_count() != 5 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 1 ||
        (mir.return_type & 15) != TYPE_CHAR)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->pointer_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &plan->value_stack_offset) ||
        !mir_machine_same_location(&mir.insns[1], &mir.insns[3]) ||
        mir.insns[5].immediate != '+' ||
        mir.insns[5].src1 != mir.insns[3].dst ||
        mir.insns[5].src2 != mir.insns[4].dst ||
        !mir_machine_constant_equals(mir.insns[4].dst, 1) ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        !mir_machine_same_location(&mir.insns[3], &mir.insns[6]) ||
        mir.insns[9].immediate != 0 ||
        mir.insns[9].src1 != mir.insns[2].dst ||
        mir.insns[10].immediate != TOK_LE ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[10].src2 != mir.insns[8].dst ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[11].label != mir.insns[18].label)
        return 0;
    if (mir.insns[14].immediate != 0 ||
        mir.insns[14].src1 != mir.insns[2].dst ||
        mir.insns[15].immediate != '+' ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        mir.insns[15].src2 != mir.insns[13].dst ||
        mir.insns[17].label != mir.insns[26].label ||
        mir.insns[21].immediate != 0 ||
        mir.insns[21].src1 != mir.insns[2].dst ||
        mir.insns[22].immediate != '-' ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        mir.insns[22].src2 != mir.insns[20].dst ||
        mir.insns[24].immediate != '+' ||
        mir.insns[24].src1 != mir.insns[22].dst ||
        mir.insns[24].src2 != mir.insns[23].dst ||
        mir.insns[27].src1 != mir.insns[15].dst ||
        mir.insns[27].src2 != mir.insns[24].dst ||
        mir.insns[28].immediate != 0 ||
        mir.insns[28].src1 != mir.insns[27].dst ||
        type_size(mir.insns[28].type) != 1 ||
        mir.insns[29].src1 != mir.insns[3].dst ||
        mir.insns[29].src2 != mir.insns[28].dst ||
        mir.insns[29].memory_size != 1 ||
        (mir.insns[29].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_same_location(&mir.insns[6], &mir.insns[30]) ||
        mir.insns[31].src1 != mir.insns[30].dst)
        return 0;
    plan->threshold = (int)mir.insns[8].immediate + 1;
    plan->low_adjustment = (int)mir.insns[13].immediate;
    plan->high_adjustment =
        (int)mir.insns[23].immediate - (int)mir.insns[20].immediate;
    if (plan->threshold <= 0 || plan->threshold > 256 ||
        plan->low_adjustment < -255 || plan->low_adjustment > 255 ||
        plan->high_adjustment < -255 || plan->high_adjustment > 255)
        return 0;
    return 1;
}

static int mir_match_dead_constant_float_check(void)
{
    static const int expected_opcodes[24] = {
        MIR_LABEL, MIR_LABEL, MIR_FLOAT_CONST, MIR_FLOAT_CONST,
        MIR_BINARY, MIR_UNARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_NOP, MIR_LABEL, MIR_NOP,
        MIR_LABEL, MIR_CONST, MIR_BRANCH_FALSE, MIR_JUMP, MIR_LABEL
    };
    uint32_t left_bits;
    uint32_t right_bits;
    float left;
    float right;
    int comparison;
    int instruction;

    if (mir.count != 24 || mir_cfg_block_count() != 5 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (!type_is_float(mir.insns[2].type) ||
        mir.insns[3].type != mir.insns[2].type ||
        mir.insns[4].src1 != mir.insns[2].dst ||
        mir.insns[4].src2 != mir.insns[3].dst ||
        (mir.insns[4].immediate != TOK_EQ &&
         mir.insns[4].immediate != TOK_NE &&
         mir.insns[4].immediate != '<' &&
         mir.insns[4].immediate != '>' &&
         mir.insns[4].immediate != TOK_LE &&
         mir.insns[4].immediate != TOK_GE) ||
        mir.insns[5].immediate != '!' ||
        mir.insns[5].src1 != mir.insns[4].dst ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        mir_find_label(mir.insns[6].label) != 17 ||
        !mir_machine_constant_equals(mir.insns[20].dst, 0) ||
        mir.insns[21].src1 != mir.insns[20].dst ||
        mir_find_label(mir.insns[21].label) != 23 ||
        mir.insns[22].label != mir.insns[1].label)
        return 0;
    left_bits = (uint32_t)mir.insns[2].immediate;
    right_bits = (uint32_t)mir.insns[3].immediate;
    memcpy(&left, &left_bits, sizeof(left));
    memcpy(&right, &right_bits, sizeof(right));
    switch (mir.insns[4].immediate) {
    case TOK_EQ: comparison = left == right; break;
    case TOK_NE: comparison = left != right; break;
    case '<': comparison = left < right; break;
    case '>': comparison = left > right; break;
    case TOK_LE: comparison = left <= right; break;
    case TOK_GE: comparison = left >= right; break;
    default: return 0;
    }
    if (!comparison)
        return 0;
    return 1;
}

static int mir_match_volatile_fill_wide_constant(
    struct MirVolatileFillWideConstant *plan)
{
    static const int expected_opcodes[78] = {
        MIR_LABEL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_PHI,
        MIR_NOP, MIR_CONST, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP, MIR_UNARY,
        MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_NOP, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_CONST, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_NOP, MIR_RETURN, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST, MIR_NOP, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_RETURN, MIR_LABEL, MIR_CONST,
        MIR_NOP, MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST, MIR_NOP, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_RETURN, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP, MIR_RETURN
    };
    int declared;
    int found_volatile = 0;
    int memory_offset;
    int memory_storage;
    int memory_type;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 78 || mir_cfg_block_count() != 7 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_LONG ||
        (mir.return_type & TYPE_UNSIGNED) == 0 ||
        type_size(mir.return_type) != 4)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return 0;
    if (!mir_scalar_memory_location(
            &mir.insns[11], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_LOCAL ||
        type_ptr_depth(mir.insns[11].type) != 1 ||
        (mir.insns[11].type & 15) != TYPE_CHAR ||
        memory_offset >= 0 ||
        !mir_machine_constant_equals(mir.insns[1].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[3]) ||
        mir.insns[5].src1 != mir.insns[1].dst ||
        mir.insns[5].src2 != mir.insns[20].dst ||
        mir.insns[5].phi_pred1 != mir.insns[0].label ||
        mir.insns[5].phi_pred2 != mir.insns[17].label ||
        mir.insns[7].immediate <= 0 ||
        mir.insns[7].immediate > 255 ||
        mir.insns[9].immediate != '<' ||
        mir.insns[9].src1 != mir.insns[5].dst ||
        mir.insns[9].src2 != mir.insns[7].dst ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[10].label != mir.insns[23].label)
        return 0;
    for (declared = 0; declared < mir.declared_count; ++declared)
        if (!strcmp(mir.declared_names[declared], mir.insns[11].name)) {
            found_volatile = mir.declared_is_volatile[declared];
            break;
        }
    if (!found_volatile ||
        mir.insns[13].src1 != mir.insns[11].dst ||
        mir.insns[13].src2 != mir.insns[5].dst ||
        mir.insns[13].immediate != 1 ||
        mir.insns[15].immediate != 0 ||
        mir.insns[15].src1 != mir.insns[5].dst ||
        mir.insns[16].src1 != mir.insns[13].dst ||
        mir.insns[16].src2 != mir.insns[15].dst ||
        mir.insns[16].memory_size != 1 ||
        !mir_machine_constant_equals(mir.insns[19].dst, 1) ||
        mir.insns[20].immediate != '+' ||
        mir.insns[20].src1 != mir.insns[5].dst ||
        mir.insns[20].src2 != mir.insns[19].dst ||
        !mir_machine_same_location(&mir.insns[3], &mir.insns[21]) ||
        mir.insns[22].label != mir.insns[4].label)
        return 0;
    if (mir.insns[24].immediate != 0x12345678L ||
        mir.insns[30].immediate != TOK_SHR ||
        mir.insns[30].src1 != mir.insns[24].dst ||
        !mir_machine_constant_equals(mir.insns[29].dst, 8) ||
        mir.insns[34].immediate != 0x00123456L ||
        mir.insns[36].immediate != TOK_NE ||
        mir.insns[36].src1 != mir.insns[30].dst ||
        mir.insns[36].src2 != mir.insns[34].dst ||
        mir.insns[37].label != mir.insns[40].label ||
        mir.insns[43].immediate != TOK_SHR ||
        mir.insns[43].src1 != mir.insns[30].dst ||
        !mir_machine_constant_equals(mir.insns[42].dst, 8) ||
        mir.insns[47].immediate != 0x1234L ||
        mir.insns[49].immediate != TOK_NE ||
        mir.insns[49].src1 != mir.insns[43].dst ||
        mir.insns[49].src2 != mir.insns[47].dst ||
        mir.insns[50].label != mir.insns[53].label)
        return 0;
    if (mir.insns[54].immediate != 49 ||
        mir.insns[60].immediate != TOK_SHL ||
        mir.insns[60].src1 != mir.insns[54].dst ||
        !mir_machine_constant_equals(mir.insns[59].dst, 6) ||
        mir.insns[64].immediate != 3136 ||
        mir.insns[66].immediate != TOK_NE ||
        mir.insns[66].src1 != mir.insns[60].dst ||
        mir.insns[66].src2 != mir.insns[64].dst ||
        mir.insns[67].label != mir.insns[70].label ||
        mir.insns[73].immediate != TOK_SHR ||
        mir.insns[73].src1 != mir.insns[60].dst ||
        !mir_machine_constant_equals(mir.insns[72].dst, 8) ||
        mir.insns[77].src1 != mir.insns[73].dst)
        return 0;
    plan->buffer_offset = memory_offset;
    plan->count = (int)mir.insns[7].immediate;
    plan->result = 12;
    if (-plan->buffer_offset < plan->count)
        return 0;
    return 1;
}

static int mir_match_single_signed_div_check(
    struct MirSingleSignedDivCheck *plan)
{
    const struct MirInsn *call;
    const struct MirInsn *label_load;
    const struct MirInsn *result;
    const struct MirInsn *other;
    int arguments[3];
    int expected_parameter;
    int instruction;
    int label_parameter;

    memset(plan, 0, sizeof(*plan));
    if ((mir.count != 23 && mir.count != 22) ||
        mir_cfg_block_count() != 1 || mir.has_vla ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    if (mir.count == 23) {
        result = &mir.insns[8];
        other = &mir.insns[13];
        expected_parameter = 3;
        label_parameter = 5;
        label_load = &mir.insns[20];
        call = &mir.insns[22];
    } else {
        result = &mir.insns[12];
        other = &mir.insns[7];
        expected_parameter = 3;
        label_parameter = 4;
        label_load = &mir.insns[19];
        call = &mir.insns[21];
    }
    if (mir.insns[0].opcode != MIR_LABEL ||
        mir.insns[1].opcode != MIR_PARAM ||
        mir.insns[2].opcode != MIR_PARAM ||
        result->opcode != MIR_BINARY ||
        other->opcode != MIR_BINARY ||
        (result->immediate != '%' && result->immediate != '/') ||
        (other->immediate != '%' && other->immediate != '/') ||
        result->immediate == other->immediate ||
        result->src1 != mir.insns[1].dst ||
        result->src2 != mir.insns[2].dst ||
        other->src1 != mir.insns[1].dst ||
        other->src2 != mir.insns[2].dst ||
        type_ptr_depth(result->type) != 0 ||
        (result->type & 15) != TYPE_INT ||
        (result->type & TYPE_UNSIGNED) != 0 ||
        label_load->opcode != MIR_LOAD ||
        !mir_machine_same_location(
            &mir.insns[label_parameter], label_load) ||
        call->opcode != MIR_CALL ||
        !mir_machine_three_call_arguments(call, arguments) ||
        arguments[0] != result->dst ||
        arguments[1] != mir.insns[expected_parameter].dst ||
        arguments[2] != label_load->dst)
        return 0;
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->numerator_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &plan->denominator_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[expected_parameter].dst,
            &plan->expected_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[label_parameter].dst,
            &plan->label_stack_offset))
        return 0;
    plan->check_function = find_global(call->name);
    if (plan->check_function == NULL ||
        !plan->check_function->is_defined ||
        plan->check_function->storage != SC_FUNC ||
        plan->check_function->is_funcptr ||
        plan->check_function->is_noreturn ||
        !plan->check_function->has_proto ||
        plan->check_function->proto_nargs != 3 ||
        plan->check_function->proto_variadic ||
        call->memory_flags != 0)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_OPAQUE)
            return 0;
    plan->operation = (int)result->immediate;
    return 1;
}

static int mir_match_local_identity_array_result(
    struct MirLocalIdentityArrayResult *plan)
{
    static const int expected_opcodes[32] = {
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_PHI,
        MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP, MIR_UNARY,
        MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_ADDRESS, MIR_NOP, MIR_STORE,
        MIR_LOAD, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_RETURN
    };
    int instruction;
    int memory_offset;
    int memory_storage;
    int memory_type;
    long selected_index;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 32 || mir_cfg_block_count() != 4 ||
        mir.has_vla || type_ptr_depth(mir.return_type) != 0 ||
        type_size(mir.return_type) != 2)
        return mir_machine_reject("local-identity-array", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject("local-identity-array", "opcode");
    if (!mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[3]) ||
        mir.insns[5].src1 != mir.insns[2].dst ||
        mir.insns[5].src2 != mir.insns[20].dst ||
        mir.insns[5].phi_pred1 != mir.insns[0].label ||
        mir.insns[5].phi_pred2 != mir.insns[17].label ||
        mir.insns[7].immediate <= 0 ||
        mir.insns[7].immediate > 32767 ||
        mir.insns[8].immediate != 0 ||
        mir.insns[8].src1 != mir.insns[5].dst ||
        mir.insns[9].immediate != '<' ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].src2 != mir.insns[7].dst ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        mir.insns[10].label != mir.insns[23].label)
        return mir_machine_reject("local-identity-array", "loop");
    if (!mir_machine_named_nonvolatile(&mir.insns[11]) ||
        !mir_scalar_memory_location(
            &mir.insns[11], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_LOCAL || memory_offset >= 0 ||
        mir.insns[13].src1 != mir.insns[11].dst ||
        mir.insns[13].src2 != mir.insns[5].dst ||
        mir.insns[13].immediate != 2 ||
        mir.insns[13].memory_size != 2 ||
        mir.insns[15].immediate != 0 ||
        mir.insns[15].src1 != mir.insns[5].dst ||
        mir.insns[16].src1 != mir.insns[13].dst ||
        mir.insns[16].src2 != mir.insns[15].dst ||
        mir.insns[16].memory_size != 2 ||
        (mir.insns[16].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_constant_equals(mir.insns[19].dst, 1) ||
        mir.insns[20].immediate != '+' ||
        mir.insns[20].src1 != mir.insns[5].dst ||
        mir.insns[20].src2 != mir.insns[19].dst ||
        !mir_machine_same_location(&mir.insns[3], &mir.insns[21]) ||
        mir.insns[22].label != mir.insns[4].label)
        return mir_machine_reject("local-identity-array", "store");
    plan->array_offset = memory_offset;
    if (strcmp(mir.insns[11].name, mir.insns[24].name) ||
        !mir_machine_named_nonvolatile(&mir.insns[26]) ||
        !mir_scalar_memory_location(
            &mir.insns[26], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        type_ptr_depth(memory_type) != 1 ||
        mir.insns[26].src1 != mir.insns[24].dst ||
        !mir_machine_same_location(&mir.insns[26], &mir.insns[27]))
        return mir_machine_reject(
            "local-identity-array", "result-alias");
    plan->escaped_pointer = find_global(mir.insns[26].name);
    if (plan->escaped_pointer == NULL ||
        !plan->escaped_pointer->is_defined ||
        plan->escaped_pointer->is_volatile)
        return mir_machine_reject(
            "local-identity-array", "result-global");
    plan->escaped_pointer_offset = memory_offset;
    if (!mir_machine_constant_value(
            mir.insns[28].dst, &selected_index, 0) ||
        selected_index < 0 ||
        selected_index >= mir.insns[7].immediate ||
        mir.insns[29].src1 != mir.insns[27].dst ||
        mir.insns[29].src2 != mir.insns[28].dst ||
        mir.insns[29].immediate != 2)
        return mir_machine_reject(
            "local-identity-array", "result-index");
    if (
        mir.insns[30].src1 != mir.insns[29].dst ||
        mir.insns[30].memory_size != 2 ||
        (mir.insns[30].memory_flags & (1 | 8)) != 0 ||
        mir.insns[31].src1 != mir.insns[30].dst)
        return mir_machine_reject("local-identity-array", "result");
    plan->result = (int)selected_index;
    if (-plan->array_offset <
        (int)mir.insns[7].immediate * 2)
        return mir_machine_reject(
            "local-identity-array", "result-frame");
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

static int mir_machine_evaluate_constant_function(int *result)
{
    struct Sym *function = find_global(mir.name);
    long *values = NULL;
    long *objects = NULL;
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
        function->storage != SC_FUNC || function->is_static ||
        (function->has_proto &&
         (function->proto_nargs != 0 || function->proto_variadic)) ||
        mir.sink_purpose != EMIT_SINK_FINAL ||
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
    if (values == NULL || objects == NULL ||
        value_known == NULL || object_known == NULL)
        goto done;
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
            ++instruction;
            break;
        case MIR_STORE:
            if (insn->src1 < 0 || insn->src1 >= value_capacity ||
                !value_known[insn->src1] ||
                insn->object < 0 || insn->object >= mir.object_count ||
                !mir_machine_unobservable_local_store(insn))
                goto done;
            objects[insn->object] = values[insn->src1];
            object_known[insn->object] = 1;
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
                !value_known[insn->src1])
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
    free(value_known);
    free(objects);
    free(values);
    return ok;
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

static int mir_machine_global_byte_value(
    int value, struct Sym **symbol_out, int *offset_out,
    int *is_unsigned_out)
{
    const struct MirInsn *definition = mir_definition(value);
    const struct MirInsn *address;
    const struct MirInsn *root;
    struct Sym *symbol;
    int memory_type;
    int memory_storage;
    int memory_offset;
    long index;

    if (definition != NULL && definition->opcode == MIR_UNARY &&
        definition->immediate == 0)
        definition = mir_definition(definition->src1);
    if (definition == NULL)
        return 0;
    if (definition->opcode == MIR_LOAD) {
        if (!mir_machine_named_nonvolatile(definition) ||
            !mir_scalar_memory_location(
                definition, &memory_type, &memory_storage,
                &memory_offset) ||
            memory_storage != SC_GLOBAL ||
            type_size(memory_type) != 1)
            return 0;
        symbol = find_global(definition->name);
        if (symbol == NULL || symbol->is_volatile)
            return 0;
        *symbol_out = symbol;
        *offset_out = memory_offset;
        *is_unsigned_out =
            type_is_bool(definition->type) ||
            (definition->type & TYPE_UNSIGNED) != 0;
        return 1;
    }
    if (definition->opcode != MIR_LOAD_INDIRECT ||
        definition->memory_size != 1 ||
        definition->bit_width != 0 ||
        (definition->memory_flags & (1 | 8)) != 0)
        return 0;
    address = mir_definition(definition->src1);
    if (address == NULL ||
        (address->memory_flags & (1 | 8)) != 0)
        return 0;
    if (address->opcode == MIR_MEMBER_ADDRESS) {
        root = mir_definition(address->src1);
        index = address->immediate;
    } else if (address->opcode == MIR_INDEX_ADDRESS &&
               address->immediate > 0 &&
               mir_machine_constant_value(address->src2, &index, 0)) {
        root = mir_definition(address->src1);
        index *= address->immediate;
    } else {
        return 0;
    }
    if (root == NULL || root->opcode != MIR_ADDRESS ||
        !mir_scalar_memory_location(
            root, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        index < -32768 || index > 32767)
        return 0;
    symbol = find_global(root->name);
    if (symbol == NULL || symbol->is_volatile)
        return 0;
    *symbol_out = symbol;
    *offset_out = memory_offset + (int)index;
    *is_unsigned_out =
        type_is_bool(definition->type) ||
        (definition->type & TYPE_UNSIGNED) != 0;
    return 1;
}

static int mir_match_global_byte_checks(
    struct MirGlobalByteChecks *plan)
{
    int call_count = 0;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        (mir.return_type & 15) != TYPE_VOID)
        return mir_machine_reject("global-byte-checks", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        switch (insn->opcode) {
        case MIR_NOP:
        case MIR_LABEL:
        case MIR_CONST:
        case MIR_STRING_ADDRESS:
        case MIR_ADDRESS:
        case MIR_MEMBER_ADDRESS:
        case MIR_INDEX_ADDRESS:
        case MIR_LOAD:
        case MIR_LOAD_INDIRECT:
        case MIR_UNARY:
        case MIR_ARG:
            break;
        case MIR_CALL:
            {
                const struct MirInsn *string;
                long expected;
                int arguments[3];

                if (call_count >= 16 ||
                    !mir_machine_three_call_arguments(
                        insn, arguments) ||
                    !mir_machine_global_byte_value(
                        arguments[0],
                        &plan->symbols[call_count],
                        &plan->offsets[call_count],
                        &plan->is_unsigned[call_count]) ||
                    !mir_machine_constant_value(
                        arguments[1], &expected, 0))
                    return mir_machine_reject(
                        "global-byte-checks", "arguments");
                string = mir_definition(arguments[2]);
                if (string == NULL ||
                    string->opcode != MIR_STRING_ADDRESS ||
                    expected < -32768 || expected > 65535)
                    return mir_machine_reject(
                        "global-byte-checks", "expected");
                if (call_count == 0) {
                    plan->check_function = find_global(insn->name);
                    if (plan->check_function == NULL ||
                        !plan->check_function->is_defined ||
                        plan->check_function->is_funcptr ||
                        plan->check_function->is_noreturn)
                        return mir_machine_reject(
                            "global-byte-checks", "function");
                } else if (plan->check_function !=
                           find_global(insn->name)) {
                    return mir_machine_reject(
                        "global-byte-checks", "mixed-functions");
                }
                plan->expected[call_count] =
                    (int)((unsigned long)expected & 0xffffUL);
                plan->strings[call_count] = (int)string->immediate;
                ++call_count;
            }
            break;
        default:
            return mir_machine_reject(
                "global-byte-checks", "opcode");
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

static void mir_emit_call_sum_print(
    FILE *out, const struct MirCallSumPrint *plan)
{
    int call;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (call = 0; call < 4; ++call) {
        if (call < 2)
            fprintf(out, "\tld hl,%d\n\tpush hl\n",
                    plan->arguments[call]);
        mir_machine_emit_symbol_call(
            out, plan->value_functions[call]);
        if (call < 2)
            fputs("\tpop bc\n", out);
        if (call == 0)
            fputs("\tpush hl\n", out);
        else {
            fputs("\tpop de\n\tadd hl,de\n", out);
            if (call != 3)
                fputs("\tpush hl\n", out);
        }
    }
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    fputs("\tpop bc\n\tpop bc\n\tld hl,0\n\tret\n", out);
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

static void mir_emit_global_byte_checks(
    FILE *out, const struct MirGlobalByteChecks *plan)
{
    int check;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (check = 0; check < plan->count; ++check) {
        fprintf(out, "\tld hl,S%d\n\tpush hl\n"
                     "\tld hl,%d\n\tpush hl\n",
                plan->strings[check], plan->expected[check]);
        mir_machine_emit_global_byte_a(
            out, plan->symbols[check], plan->offsets[check], 0);
        fputs("\tld l,a\n", out);
        if (plan->is_unsigned[check])
            fputs("\tld h,0\n", out);
        else
            fputs("\trlca\n\tsbc a,a\n\tld h,a\n", out);
        fputs("\tpush hl\n", out);
        mir_machine_emit_symbol_call(out, plan->check_function);
        fputs("\tpop bc\n\tpop bc\n\tpop bc\n", out);
    }
    fputs("\tret\n", out);
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

static int mir_machine_name_nonvolatile(const char *name)
{
    int declared;
    struct Sym *global;

    if (name == NULL || name[0] == '\0')
        return 0;
    for (declared = 0; declared < mir.declared_count; ++declared)
        if (!strcmp(mir.declared_names[declared], name))
            return !mir.declared_is_volatile[declared];
    global = find_global(name);
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

static void mir_emit_vla_endpoint_reduction(
    FILE *out, const struct MirVlaEndpointReduction *plan)
{
    int done = new_label();

    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n"
            "\tdec hl\n\tld a,h\n\tor l\n\tjp z,L%d\n",
            plan->parameter_stack_offset, done);
    mir_machine_emit_hl_offset(out, plan->adjustment, 0);
    fprintf(out, "L%d:\n\tret\n", done);
}

static void mir_emit_masked_wide_product_high(
    FILE *out, const struct MirMaskedWideProductHigh *plan)
{
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tld hl,%u\n",
            plan->parameter_stack_offset, plan->multiplier);
    mir_emit_runtime_call(out, "__m1u");
    fputs("\tld l,e\n\tld h,d\n\tld de,0\n\tret\n", out);
}

static void mir_emit_wide_stack_equality_branch(
    FILE *out, int stack_offset, unsigned long value,
    int fallback)
{
    fprintf(out,
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

static void mir_emit_wide_equal_select(
    FILE *out, const struct MirWideEqualSelect *plan)
{
    int fallback = new_label();

    mir_emit_wide_stack_equality_branch(
        out, plan->parameter_stack_offset,
        plan->match_value, fallback);
    fprintf(out,
            "\tld hl,%lu\n\tld de,%lu\n\tret\n"
            "L%d:\n\tld hl,%lu\n\tld de,%lu\n\tret\n",
            plan->match_value & 0xffffUL,
            (plan->match_value >> 16) & 0xffffUL,
            fallback,
            plan->fallback_value & 0xffffUL,
            (plan->fallback_value >> 16) & 0xffffUL);
}

static void mir_emit_wide_equal_add_select(
    FILE *out, const struct MirWideEqualAddSelect *plan)
{
    int fallback = new_label();
    int extended = new_label();

    mir_emit_wide_stack_equality_branch(
        out, plan->wide_stack_offset,
        plan->match_value, fallback);
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tld l,c\n\tld h,b\n\tld de,0\n",
            plan->narrow_stack_offset);
    if (!plan->narrow_is_unsigned)
        fprintf(out,
                "\tld a,h\n\tor a\n\tjp p,L%d\n"
                "\tdec de\nL%d:\n",
                extended, extended);
    fprintf(out,
            "\tld bc,%lu\n\tadd hl,bc\n\tex de,hl\n"
            "\tld bc,%lu\n\tadc hl,bc\n\tex de,hl\n\tret\n"
            "L%d:\n\tld hl,%lu\n\tld de,%lu\n\tret\n",
            plan->match_value & 0xffffUL,
            (plan->match_value >> 16) & 0xffffUL,
            fallback,
            plan->fallback_value & 0xffffUL,
            (plan->fallback_value >> 16) & 0xffffUL);
}

static void mir_emit_wide_call_member_accumulate(
    FILE *out, const struct MirWideCallMemberAccumulate *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
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
    fputs("\tpop bc\n\tpop bc\n\tadd hl,bc\n"
          "\tex de,hl\n\tpop bc\n\tadc hl,bc\n\tex de,hl\n"
          "\tld b,d\n\tld c,e\n\tpop de\n\tex de,hl\n"
          "\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tinc hl\n"
          "\tld (hl),c\n\tinc hl\n\tld (hl),b\n\tret\n",
          out);
}

static void mir_emit_widened_parameter(
    FILE *out, int stack_offset, int is_unsigned)
{
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n",
            stack_offset);
    if (is_unsigned) {
        fputs("\tld de,0\n", out);
    } else {
        fputs("\tld a,h\n\trlca\n\tsbc a,a\n"
              "\tld e,a\n\tld d,a\n", out);
    }
}

static void mir_emit_wide_difference_call(
    FILE *out, const struct MirWideDifferenceCall *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_emit_widened_parameter(
        out, plan->left_stack_offset,
        plan->left_is_unsigned);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_emit_widened_parameter(
        out, plan->right_stack_offset + 4,
        plan->right_is_unsigned);
    fputs("\tld b,d\n\tld c,e\n\tex de,hl\n\tpop hl\n"
          "\tor a\n\tsbc hl,de\n\tex de,hl\n\tpop hl\n"
          "\tsbc hl,bc\n\tex de,hl\n"
          "\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->function);
    fputs("\tpop bc\n\tpop bc\n\tret\n", out);
}

static void mir_emit_scaled_wide_division_call(
    FILE *out, const struct MirScaledWideDivisionCall *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tld e,(hl)\n"
            "\tld h,a\n\tld l,0\n"
            "\tld a,e\n\trlca\n\tsbc a,a\n\tld d,a\n",
            plan->numerator_stack_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_emit_widened_parameter(
        out, plan->denominator_stack_offset + 4, 0);
    mir_emit_runtime_call(out, "__lds");
    fputs("\tpop bc\n\tpop bc\n\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->function);
    fputs("\tpop bc\n\tpop bc\n\tret\n", out);
}

static void mir_emit_record_append(
    FILE *out, const struct MirRecordAppend *plan)
{
    int current_offset = 0;
    int field;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_word(
        out, plan->root, plan->root_offset);
    mir_machine_emit_hl_offset(
        out, plan->array_member_offset, 0);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n",
          out);
    mir_machine_emit_global_word(
        out, plan->root, plan->root_offset);
    mir_machine_emit_hl_offset(
        out, plan->cursor_member_offset, 0);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n",
          out);
    mir_emit_mul_hl_const(out, (unsigned long)plan->stride);
    fputs("\tpop de\n\tadd hl,de\n", out);
    for (field = 0; field < 3; ++field) {
        mir_machine_emit_hl_offset(
            out, plan->field_offsets[field] - current_offset, 0);
        fprintf(out,
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
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tinc de\n"
          "\tdec hl\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tret\n",
          out);
}

static void mir_emit_direct_record_append(
    FILE *out, const struct MirRecordAppend *plan)
{
    int field;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (field = 0; field < 3; ++field) {
        mir_machine_emit_global_word(
            out, plan->root, plan->root_offset);
        mir_machine_emit_hl_offset(
            out, plan->array_member_offset, 0);
        fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n",
              out);
        mir_machine_emit_global_word(
            out, plan->root, plan->root_offset);
        mir_machine_emit_hl_offset(
            out, plan->cursor_member_offset, 0);
        fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n",
              out);
        mir_emit_mul_hl_const(out, (unsigned long)plan->stride);
        fputs("\tpop de\n\tadd hl,de\n", out);
        mir_machine_emit_hl_offset(
            out, plan->field_offsets[field], 0);
        fprintf(out,
                "\tpush hl\n\tld hl,%d\n\tadd hl,sp\n"
                "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
                "\tpop hl\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n",
                plan->parameter_stack_offsets[field] + 2);
    }
    mir_machine_emit_global_word(
        out, plan->root, plan->root_offset);
    mir_machine_emit_hl_offset(
        out, plan->cursor_member_offset, 0);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tinc de\n"
          "\tdec hl\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tret\n",
          out);
}

static void mir_emit_wide_parameter(
    FILE *out, int stack_offset)
{
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld l,c\n\tld h,b\n",
            stack_offset);
}

static void mir_emit_add_mixed_wide_parameter(
    FILE *out, int stack_offset, int width, int is_unsigned)
{
    fputs("\tpush de\n\tpush hl\n", out);
    if (width == 2)
        mir_emit_widened_parameter(
            out, stack_offset + 4, is_unsigned);
    else
        mir_emit_wide_parameter(out, stack_offset + 4);
    fputs("\tpop bc\n\tadd hl,bc\n\tex de,hl\n"
          "\tpop bc\n\tadc hl,bc\n\tex de,hl\n", out);
}

static void mir_emit_mixed_wide_sum(
    FILE *out, const struct MirMixedWideSum *plan)
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
    fputs("\tret\n", out);
}

static void mir_emit_float_member_scale_add(
    FILE *out, const struct MirFloatMemberScaleAdd *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n",
            plan->parameter_stack_offset);
    mir_machine_emit_hl_offset(
        out, plan->destination_offset, 0);
    fputs("\tpush hl\n"
          "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
          "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld l,c\n\tld h,b\n\tpush de\n\tpush hl\n",
          out);
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n",
            plan->parameter_stack_offset + 6);
    mir_machine_emit_hl_offset(out, plan->source_offset, 0);
    fputs("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
          "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld l,c\n\tld h,b\n\tpush de\n\tpush hl\n",
          out);
    fprintf(out,
            "\tld hl,%lu\n\tld de,%lu\n",
            plan->scale_bits & 0xffffUL,
            (plan->scale_bits >> 16) & 0xffffUL);
    mir_emit_runtime_call(out, "__fmaf");
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tld a,l\n\tld (bc),a\n\tinc bc\n"
          "\tld a,h\n\tld (bc),a\n\tinc bc\n"
          "\tld a,e\n\tld (bc),a\n\tinc bc\n"
          "\tld a,d\n\tld (bc),a\n\tret\n", out);
}

static void mir_emit_byte_mismatch_report(
    FILE *out, const struct MirByteMismatchReport *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
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
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_global_word(
        out, plan->counter, plan->counter_offset);
    fputs("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->counter, plan->counter_offset);
    fputs("\tret\n", out);
}

static void mir_emit_byte_parameter_word(
    FILE *out, int stack_offset, int is_unsigned)
{
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld a,(hl)\n\tld l,a\n",
            stack_offset);
    if (is_unsigned)
        fputs("\tld h,0\n", out);
    else
        fputs("\trlca\n\tsbc a,a\n\tld h,a\n", out);
}

static void mir_emit_byte_binary_operands(
    FILE *out, const struct MirByteArithmeticReports *plan)
{
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld c,(hl)\n",
            plan->left_stack_offset);
    if (plan->is_unsigned) {
        fputs("\tld b,0\n", out);
    } else {
        fputs("\tld a,c\n\trlca\n\tsbc a,a\n\tld b,a\n", out);
    }
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld e,(hl)\n",
            plan->right_stack_offset);
    if (plan->is_unsigned) {
        fputs("\tld d,0\n", out);
    } else {
        fputs("\tld a,e\n\trlca\n\tsbc a,a\n\tld d,a\n", out);
    }
    fputs("\tld l,c\n\tld h,b\n", out);
}

static void mir_emit_byte_arithmetic_reports(
    FILE *out, const struct MirByteArithmeticReports *plan)
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
        fputs("\tld a,l\n\tld l,a\n", out);
        if (plan->is_unsigned)
            fputs("\tld h,0\n", out);
        else
            fputs("\trlca\n\tsbc a,a\n\tld h,a\n", out);
        fputs("\tpush hl\n", out);
        mir_emit_byte_parameter_word(
            out, plan->right_stack_offset + 2,
            plan->is_unsigned);
        fputs("\tpush hl\n", out);
        mir_emit_byte_parameter_word(
            out, plan->left_stack_offset + 4,
            plan->is_unsigned);
        fprintf(out,
                "\tpush hl\n\tld hl,S%d\n\tpush hl\n",
                plan->string_ids[report]);
        mir_emit_runtime_call(out, plan->call_name);
        fputs("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    }
    fputs("\tret\n", out);
}

static void mir_emit_push_byte_in_a(
    FILE *out, int is_unsigned)
{
    fputs("\tld l,a\n", out);
    if (is_unsigned)
        fputs("\tld h,0\n", out);
    else
        fputs("\trlca\n\tsbc a,a\n\tld h,a\n", out);
    fputs("\tpush hl\n", out);
}

static void mir_emit_push_byte_binary(
    FILE *out, const struct MirByteBitwiseReport *plan,
    int pushed_words, int operation)
{
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld c,(hl)\n"
            "\tld hl,%d\n\tadd hl,sp\n\tld a,(hl)\n",
            plan->left_stack_offset + pushed_words * 2,
            plan->right_stack_offset + pushed_words * 2);
    if (operation == '&')
        fputs("\tand c\n", out);
    else if (operation == '|')
        fputs("\tor c\n", out);
    else
        fputs("\txor c\n", out);
    mir_emit_push_byte_in_a(out, plan->is_unsigned);
}

static void mir_emit_byte_bitwise_report(
    FILE *out, const struct MirByteBitwiseReport *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld a,(hl)\n\tcpl\n",
            plan->right_stack_offset);
    mir_emit_push_byte_in_a(out, plan->is_unsigned);
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld a,(hl)\n\tcpl\n",
            plan->left_stack_offset + 2);
    mir_emit_push_byte_in_a(out, plan->is_unsigned);
    mir_emit_push_byte_binary(out, plan, 2, '^');
    mir_emit_push_byte_binary(out, plan, 3, '|');
    mir_emit_push_byte_binary(out, plan, 4, '&');
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->string_id);
    if ((plan->runtime_flags & MIR_CALL_FLAG_FORMAT_HEX) != 0)
        mir_emit_runtime_call(out, "__pfehx");
    mir_emit_runtime_call(out, plan->call_name);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tpop bc\n\tpop bc\n\tpop bc\n\tret\n", out);
}

static void mir_emit_variadic_sum(
    FILE *out, const struct MirVariadicSum *plan)
{
    int loop = new_label();
    int done = new_label();
    int byte;

    fprintf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tld hl,%d\n\tadd hl,sp\n\tpush hl\n\tpop iy\n",
            plan->count_stack_offset + 2,
            plan->first_argument_stack_offset + 2);
    if (plan->value_width == 2)
        fputs("\tld de,0\n", out);
    else
        fputs("\tld hl,0\n\tld de,0\n", out);
    fprintf(out,
            "\tld a,b\n\tor a\n\tjp m,L%d\n"
            "\tor c\n\tjp z,L%d\n"
            "L%d:\n",
            done, done, loop);
    if (plan->value_width == 2) {
        fputs("\tld l,(iy+0)\n\tld h,(iy+1)\n"
              "\tex de,hl\n\tadd hl,de\n\tex de,hl\n",
              out);
    } else {
        for (byte = 0; byte < 4; ++byte) {
            const char registers[] = { 'l', 'h', 'e', 'd' };
            fprintf(out,
                    "\tld a,%c\n\t%s a,(iy+%d)\n\tld %c,a\n",
                    registers[byte],
                    byte == 0 ? "add" : "adc",
                    byte, registers[byte]);
        }
    }
    for (byte = 0; byte < plan->value_width; ++byte)
        fputs("\tinc iy\n", out);
    fprintf(out,
            "\tdec bc\n\tld a,b\n\tor c\n\tjp nz,L%d\n"
            "L%d:\n",
            loop, done);
    if (plan->value_width == 2)
        fputs("\tld l,e\n\tld h,d\n", out);
    fputs("\tpop iy\n\tret\n", out);
}

static void mir_emit_guarded_global_pop(
    FILE *out, const struct MirGuardedGlobalPop *plan)
{
    int guard = new_label();
    int after_guard = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_word(
        out, plan->cursor, plan->cursor_offset);
    fprintf(out,
            "\tld a,h\n\tor a\n\tjp m,L%d\n"
            "\tor l\n\tjp nz,L%d\n"
            "L%d:\n",
            guard, after_guard, guard);
    mir_machine_emit_symbol_call(out, plan->guard_function);
    fprintf(out, "L%d:\n", after_guard);
    mir_machine_emit_global_word(
        out, plan->cursor, plan->cursor_offset);
    fputs("\tdec hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->cursor, plan->cursor_offset);
    fputs("\tadd hl,hl\n", out);
    mir_machine_emit_global_address_de(
        out, plan->array, plan->array_offset);
    fputs("\tadd hl,de\n\tld e,(hl)\n\tinc hl\n"
          "\tld d,(hl)\n\tex de,hl\n\tret\n", out);
}

static void mir_emit_float_member_operand(
    FILE *out, const struct MirFloatMemberScalarCompare *plan,
    int pushed_bytes)
{
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n",
            plan->pointer_stack_offset + pushed_bytes);
    mir_machine_emit_hl_offset(out, plan->member_offset, 0);
    fputs("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
          "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld l,c\n\tld h,b\n", out);
}

static void mir_emit_float_scalar_operand(
    FILE *out, const struct MirFloatMemberScalarCompare *plan,
    int pushed_bytes)
{
    if (plan->scalar_width == 2) {
        fprintf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n",
                plan->scalar_stack_offset + pushed_bytes);
        mir_emit_runtime_call(out, "__fif");
    } else {
        mir_emit_wide_parameter(
            out, plan->scalar_stack_offset + pushed_bytes);
        mir_emit_runtime_call(out, "__flf");
    }
}

static void mir_emit_float_member_scalar_compare(
    FILE *out, const struct MirFloatMemberScalarCompare *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    if (plan->member_is_left)
        mir_emit_float_member_operand(out, plan, 0);
    else
        mir_emit_float_scalar_operand(out, plan, 0);
    fputs("\tpush de\n\tpush hl\n", out);
    if (plan->member_is_left)
        mir_emit_float_scalar_operand(out, plan, 4);
    else
        mir_emit_float_member_operand(out, plan, 4);
    mir_emit_runtime_call(out, "__fgtf");
    fputs("\tpop bc\n\tpop bc\n"
          "\tld a,h\n\tor l\n\tld hl,0\n\tret z\n"
          "\tinc hl\n\tret\n", out);
}

static void mir_emit_exact_float_mismatch_report(
    FILE *out, const struct MirExactFloatMismatchReport *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_word(
        out, plan->checks, plan->checks_offset);
    fputs("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->checks, plan->checks_offset);
    mir_emit_wide_parameter(out, plan->got_stack_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_emit_wide_parameter(out, plan->want_stack_offset + 4);
    mir_emit_runtime_call(out, "__fnef");
    fputs("\tpop bc\n\tpop bc\n"
          "\tld a,h\n\tor l\n\tret z\n", out);
    mir_machine_emit_global_word(
        out, plan->failures, plan->failures_offset);
    fputs("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->failures, plan->failures_offset);
    mir_emit_wide_parameter(out, plan->want_stack_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_emit_wide_parameter(out, plan->got_stack_offset + 4);
    fputs("\tpush de\n\tpush hl\n", out);
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->name_stack_offset + 8,
            plan->string_id);
    if ((plan->runtime_flags & MIR_CALL_FLAG_FORMAT_HEX) != 0)
        mir_emit_runtime_call(out, "__pfehx");
    mir_emit_runtime_call(out, plan->call_name);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tpop bc\n\tpop bc\n\tpop bc\n\tret\n", out);
}

static void mir_emit_float_tolerance_report(
    FILE *out, const struct MirFloatToleranceReport *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_word(
        out, plan->checks, plan->checks_offset);
    fputs("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->checks, plan->checks_offset);
    mir_emit_wide_parameter(out, plan->got_stack_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_emit_wide_parameter(out, plan->want_stack_offset + 4);
    mir_emit_runtime_call(out, "__fsf");
    fputs("\tpop bc\n\tpop bc\n", out);
    mir_emit_runtime_call(out, "__fabs");
    fputs("\tpush de\n\tpush hl\n", out);
    fprintf(out,
            "\tld hl,%lu\n\tld de,%lu\n",
            plan->epsilon_bits & 0xffffUL,
            (plan->epsilon_bits >> 16) & 0xffffUL);
    mir_emit_runtime_call(out, "__fltf");
    fputs("\tpop bc\n\tpop bc\n"
          "\tld a,h\n\tor l\n\tret z\n", out);
    mir_machine_emit_global_word(
        out, plan->failures, plan->failures_offset);
    fputs("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->failures, plan->failures_offset);
    mir_emit_wide_parameter(out, plan->want_stack_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_emit_wide_parameter(out, plan->got_stack_offset + 4);
    fputs("\tpush de\n\tpush hl\n", out);
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->name_stack_offset + 8,
            plan->string_id);
    mir_emit_runtime_call(out, plan->call_name);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tpop bc\n\tpop bc\n\tpop bc\n\tret\n", out);
}

static void mir_emit_float_tolerance_failure(
    FILE *out, const struct MirFloatToleranceFailure *plan)
{
    int done = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_emit_wide_parameter(out, plan->got_stack_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_emit_wide_parameter(out, plan->want_stack_offset + 4);
    mir_emit_runtime_call(out, "__fsf");
    fputs("\tpop bc\n\tpop bc\n"
          "\tld a,d\n\tand 127\n\tld d,a\n"
          "\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->epsilon_bits);
    mir_emit_runtime_call(out, "__fltf");
    fputs("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", done);
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->name_stack_offset, plan->string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    fputs("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_global_word(
        out, plan->failures, plan->failures_offset);
    fputs("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->failures, plan->failures_offset);
    fprintf(out, "L%d:\n\tret\n", done);
}

static void mir_emit_float_byte_report(
    FILE *out, const struct MirFloatByteReport *plan)
{
    int mismatch = new_label();
    int pushed_words = 0;
    int byte;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld b,h\n\tld c,l\n",
            plan->value_stack_offset);
    for (byte = 0; byte < 4; ++byte) {
        fprintf(out,
                "\tld a,(bc)\n\tinc bc\n"
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tcp (hl)\n\tjp nz,L%d\n",
                plan->expected_stack_offsets[byte], mismatch);
    }
    fputs("\tret\n", out);
    fprintf(out, "L%d:\n", mismatch);
    for (byte = 3; byte >= 0; --byte) {
        mir_emit_byte_parameter_word(
            out,
            plan->expected_stack_offsets[byte] +
                pushed_words * 2,
            1);
        fputs("\tpush hl\n", out);
        ++pushed_words;
    }
    for (byte = 3; byte >= 0; --byte) {
        fprintf(out,
                "\tld hl,%d\n\tadd hl,sp\n",
                plan->value_stack_offset + pushed_words * 2);
        mir_machine_emit_hl_offset(out, byte, 0);
        fputs("\tld a,(hl)\n\tld l,a\n\tld h,0\n\tpush hl\n",
              out);
        ++pushed_words;
    }
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->name_stack_offset + pushed_words * 2,
            plan->string_id);
    mir_emit_runtime_call(out, plan->call_name);
    for (pushed_words = 0; pushed_words < 10; ++pushed_words)
        fputs("\tpop bc\n", out);
    mir_machine_emit_global_word(
        out, plan->failures, plan->failures_offset);
    fputs("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->failures, plan->failures_offset);
    fputs("\tret\n", out);
}

static void mir_emit_relative_tolerance_call(
    FILE *out, const struct MirRelativeToleranceCall *plan)
{
    int nonnegative = new_label();
    int ready = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%lu\n\tld de,%lu\n"
            "\tpush de\n\tpush hl\n\tpush de\n\tpush hl\n",
            plan->scale_bits & 0xffffUL,
            (plan->scale_bits >> 16) & 0xffffUL);
    mir_emit_wide_parameter(out, plan->want_stack_offset + 8);
    fputs("\tpush de\n\tpush hl\n\tld hl,0\n\tld de,0\n", out);
    mir_emit_runtime_call(out, "__fgtf");
    fputs("\tpop bc\n\tpop bc\n"
          "\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", nonnegative);
    mir_emit_wide_parameter(out, plan->want_stack_offset + 8);
    fputs("\tld a,d\n\txor 128\n\tld d,a\n", out);
    fprintf(out, "\tjp L%d\nL%d:\n", ready, nonnegative);
    mir_emit_wide_parameter(out, plan->want_stack_offset + 8);
    fprintf(out, "L%d:\n", ready);
    mir_emit_runtime_call(out, "__fmaf");
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tpush de\n\tpush hl\n", out);
    mir_emit_wide_parameter(out, plan->want_stack_offset + 4);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_emit_wide_parameter(out, plan->got_stack_offset + 8);
    fputs("\tpush de\n\tpush hl\n", out);
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n",
            plan->name_stack_offset + 12);
    mir_machine_emit_symbol_call(out, plan->function);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tpop bc\n\tpop bc\n\tpop bc\n\tret\n", out);
}

static void mir_emit_fixed_float_grid_fill(
    FILE *out, const struct MirFixedFloatGridFill *plan)
{
    int loop = new_label();

    fprintf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_address_de(
        out, plan->root, plan->root_offset);
    fputs("\tpush de\n\tpop iy\n\tld bc,0\n", out);
    fprintf(out, "L%d:\n", loop);
    fprintf(out,
            "\tld a,b\n\tadd a,c\n\tadd a,%d\n"
            "\tld l,a\n\tld h,0\n\tpush bc\n",
            plan->add_constant);
    mir_emit_runtime_call(out, "__fif");
    fputs("\tpush de\n\tpush hl\n", out);
    fprintf(out,
            "\tld hl,%lu\n\tld de,%lu\n",
            plan->divisor_bits & 0xffffUL,
            (plan->divisor_bits >> 16) & 0xffffUL);
    mir_emit_runtime_call(out, "__fdf");
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tld (iy+0),l\n\tld (iy+1),h\n"
          "\tld (iy+2),e\n\tld (iy+3),d\n"
          "\tinc iy\n\tinc iy\n\tinc iy\n\tinc iy\n"
          "\tinc c\n", out);
    fprintf(out,
            "\tld a,c\n\tcp %d\n\tjp c,L%d\n"
            "\tld c,0\n\tinc b\n\tld a,b\n\tcp %d\n"
            "\tjp c,L%d\n\tpop iy\n\tret\n",
            plan->inner_bound, loop,
            plan->outer_bound, loop);
}

static void mir_emit_constant_float_arm(
    FILE *out, int is_float, int integer_width,
    unsigned long bits)
{
    if (is_float) {
        fprintf(out,
                "\tld hl,%lu\n\tld de,%lu\n",
                bits & 0xffffUL, (bits >> 16) & 0xffffUL);
    } else if (integer_width == 2) {
        fprintf(out, "\tld hl,%lu\n", bits & 0xffffUL);
        mir_emit_runtime_call(out, "__fif");
    } else {
        fprintf(out,
                "\tld hl,%lu\n\tld de,%lu\n",
                bits & 0xffffUL, (bits >> 16) & 0xffffUL);
        mir_emit_runtime_call(out, "__flf");
    }
    mir_emit_runtime_call(out, "__ffl");
    fputs("\tret\n", out);
}

static void mir_emit_constant_float_conditional(
    FILE *out, const struct MirConstantFloatConditional *plan)
{
    int false_arm = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n"
            "\tjp z,L%d\n",
            plan->condition_stack_offset, false_arm);
    mir_emit_constant_float_arm(
        out, plan->true_is_float,
        plan->true_integer_width, plan->true_bits);
    fprintf(out, "L%d:\n", false_arm);
    mir_emit_constant_float_arm(
        out, plan->false_is_float,
        plan->false_integer_width, plan->false_bits);
}

static void mir_emit_global_float_to_long_return(
    FILE *out, const struct MirConditionalGlobalFloatLoad *plan,
    int offset)
{
    mir_machine_emit_global_address_de(out, plan->root, offset);
    fputs("\tex de,hl\n"
          "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
          "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld l,c\n\tld h,b\n", out);
    mir_emit_runtime_call(out, "__ffl");
    fputs("\tret\n", out);
}

static void mir_emit_conditional_global_float_load(
    FILE *out, const struct MirConditionalGlobalFloatLoad *plan)
{
    int false_arm = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n"
            "\tjp z,L%d\n",
            plan->condition_stack_offset, false_arm);
    mir_emit_global_float_to_long_return(
        out, plan, plan->true_offset);
    fprintf(out, "L%d:\n", false_arm);
    mir_emit_global_float_to_long_return(
        out, plan, plan->false_offset);
}

static void mir_machine_emit_ix_wide_load(
    FILE *out, int offset)
{
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
            offset, offset + 1, offset + 2, offset + 3);
}

static void mir_machine_emit_ix_wide_store(
    FILE *out, int offset)
{
    fprintf(out,
            "\tld (ix%+d),l\n\tld (ix%+d),h\n"
            "\tld (ix%+d),e\n\tld (ix%+d),d\n",
            offset, offset + 1, offset + 2, offset + 3);
}

static void mir_machine_emit_float_bits(
    FILE *out, unsigned long bits)
{
    fprintf(out, "\tld hl,%lu\n\tld de,%lu\n",
            bits & 0xffffUL, (bits >> 16) & 0xffffUL);
}

static void mir_emit_reduced_float_polynomial(
    FILE *out, const struct MirReducedFloatPolynomial *plan)
{
    int after_quadrant = new_label();
    int after_sign = new_label();
    int check_lower = new_label();
    int lower_quadrant = new_label();
    int reduced = new_label();
    int x_offset = plan->parameter_stack_offset + 2;

    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-5\n\tadd hl,sp\n\tld sp,hl\n"
          "\tld (ix-5),0\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    mir_machine_emit_float_bits(out, plan->two_pi_bits);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, x_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->remainder_function);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, x_offset);

    mir_machine_emit_ix_wide_load(out, x_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->pi_bits);
    mir_emit_runtime_call(out, "__fltf");
    fputs("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", check_lower);
    mir_machine_emit_ix_wide_load(out, x_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->two_pi_bits);
    mir_emit_runtime_call(out, "__fsf");
    fputs("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, x_offset);

    fprintf(out, "L%d:\n", check_lower);
    mir_machine_emit_ix_wide_load(out, x_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(
        out, plan->pi_bits ^ 0x80000000UL);
    mir_emit_runtime_call(out, "__fgtf");
    fputs("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", reduced);
    mir_machine_emit_ix_wide_load(out, x_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->two_pi_bits);
    mir_emit_runtime_call(out, "__faf");
    fputs("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, x_offset);

    fprintf(out, "L%d:\n", reduced);
    mir_machine_emit_ix_wide_load(out, x_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->half_pi_bits);
    mir_emit_runtime_call(out, "__fltf");
    fputs("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", lower_quadrant);
    mir_machine_emit_float_bits(out, plan->pi_bits);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_emit_runtime_call(out, "__fsf");
    fputs("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, x_offset);
    if (!plan->sine_form)
        fputs("\tld (ix-5),1\n", out);
    fprintf(out, "\tjp L%d\nL%d:\n",
            after_quadrant, lower_quadrant);

    mir_machine_emit_ix_wide_load(out, x_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(
        out, plan->half_pi_bits ^ 0x80000000UL);
    mir_emit_runtime_call(out, "__fgtf");
    fputs("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", after_quadrant);
    mir_machine_emit_float_bits(
        out, plan->pi_bits ^ 0x80000000UL);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_emit_runtime_call(out, "__fsf");
    fputs("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, x_offset);
    if (!plan->sine_form)
        fputs("\tld (ix-5),1\n", out);
    fprintf(out, "L%d:\n", after_quadrant);

    mir_machine_emit_ix_wide_load(out, x_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_emit_runtime_call(out, "__fmf");
    fputs("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, -4);

    if (plan->sine_form) {
        mir_machine_emit_ix_wide_load(out, x_offset);
        fputs("\tpush de\n\tpush hl\n", out);
        mir_machine_emit_ix_wide_load(out, x_offset);
        fputs("\tpush de\n\tpush hl\n", out);
        mir_machine_emit_ix_wide_load(out, -4);
        mir_emit_runtime_call(out, "__fmf");
        fputs("\tpop bc\n\tpop bc\n\tpush de\n\tpush hl\n", out);
    } else {
        mir_machine_emit_float_bits(out, plan->one_bits);
        fputs("\tpush de\n\tpush hl\n", out);
        mir_machine_emit_ix_wide_load(out, -4);
        fputs("\tpush de\n\tpush hl\n", out);
    }
    mir_machine_emit_float_bits(out, plan->coefficients[0]);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, -4);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->coefficients[1]);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, -4);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->coefficients[2]);
    mir_emit_runtime_call(out, "__fmaf");
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_emit_runtime_call(out, "__fmaf");
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_emit_runtime_call(out, "__fmaf");
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    if (!plan->sine_form) {
        fputs("\tld a,(ix-5)\n\tor a\n", out);
        fprintf(out,
                "\tjp z,L%d\n\tld a,d\n\txor 128\n\tld d,a\nL%d:\n",
                after_sign, after_sign);
    }
    fputs("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static void mir_emit_float_tangent_rational(
    FILE *out, const struct MirFloatTangentRational *plan)
{
    int after_period = new_label();
    int after_quadrant = new_label();
    int check_lower_period = new_label();
    int check_lower_quadrant = new_label();
    int done = new_label();
    int nonzero_denominator = new_label();
    int nonzero_result = new_label();
    int x_offset = plan->parameter_stack_offset + 2;

    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-5\n\tadd hl,sp\n\tld sp,hl\n"
          "\tld (ix-5),0\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    mir_machine_emit_float_bits(out, plan->pi_bits);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, x_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->remainder_function);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, x_offset);

    mir_machine_emit_ix_wide_load(out, x_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->half_pi_bits);
    mir_emit_runtime_call(out, "__fltf");
    fputs("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", check_lower_period);
    mir_machine_emit_ix_wide_load(out, x_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->pi_bits);
    mir_emit_runtime_call(out, "__fsf");
    fputs("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, x_offset);
    fprintf(out, "\tjp L%d\nL%d:\n",
            after_period, check_lower_period);

    mir_machine_emit_ix_wide_load(out, x_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(
        out, plan->half_pi_bits ^ 0x80000000UL);
    mir_emit_runtime_call(out, "__fgtf");
    fputs("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", after_period);
    mir_machine_emit_ix_wide_load(out, x_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->pi_bits);
    mir_emit_runtime_call(out, "__faf");
    fputs("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, x_offset);

    fprintf(out, "L%d:\n", after_period);
    mir_machine_emit_ix_wide_load(out, x_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->quarter_pi_bits);
    mir_emit_runtime_call(out, "__fltf");
    fputs("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", check_lower_quadrant);
    mir_machine_emit_float_bits(out, plan->half_pi_bits);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_emit_runtime_call(out, "__fsf");
    fputs("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, x_offset);
    fprintf(out,
            "\tld (ix-5),1\n\tjp L%d\n"
            "L%d:\n",
            after_quadrant, check_lower_quadrant);

    mir_machine_emit_ix_wide_load(out, x_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(
        out, plan->quarter_pi_bits ^ 0x80000000UL);
    mir_emit_runtime_call(out, "__fgtf");
    fputs("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", after_quadrant);
    mir_machine_emit_float_bits(
        out, plan->half_pi_bits ^ 0x80000000UL);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_emit_runtime_call(out, "__fsf");
    fputs("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, x_offset);
    fprintf(out, "\tld (ix-5),1\nL%d:\n", after_quadrant);

    mir_machine_emit_ix_wide_load(out, x_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_emit_runtime_call(out, "__fmf");
    fputs("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, -4);

    mir_machine_emit_ix_wide_load(out, x_offset);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->fifteen_bits);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, -4);
    mir_emit_runtime_call(out, "__fsf");
    fputs("\tpop bc\n\tpop bc\n", out);
    mir_emit_runtime_call(out, "__fmf");
    fputs("\tpop bc\n\tpop bc\n\tpush de\n\tpush hl\n", out);

    mir_machine_emit_float_bits(out, plan->fifteen_bits);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->six_bits);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, -4);
    mir_emit_runtime_call(out, "__fmf");
    fputs("\tpop bc\n\tpop bc\n", out);
    mir_emit_runtime_call(out, "__fsf");
    fputs("\tpop bc\n\tpop bc\n"
          "\tld a,d\n\tand 127\n\tor e\n\tor h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n", nonzero_denominator);
    fputs("\tpop bc\n\tpop bc\n\tld hl,0\n\tld de,0\n", out);
    fprintf(out, "\tjp L%d\nL%d:\n", done, nonzero_denominator);

    mir_emit_runtime_call(out, "__fdf");
    fputs("\tpop bc\n\tpop bc\n\tld a,(ix-5)\n\tor a\n", out);
    fprintf(out, "\tjp z,L%d\n", done);
    fputs("\tld a,d\n\tand 127\n\tor e\n\tor h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n\tld hl,0\n\tld de,0\n"
                 "\tjp L%d\nL%d:\n",
            nonzero_result, done, nonzero_result);
    mir_machine_emit_ix_wide_store(out, -4);
    mir_machine_emit_float_bits(out, plan->one_bits);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, -4);
    mir_emit_runtime_call(out, "__fdf");
    fputs("\tpop bc\n\tpop bc\n", out);

    fprintf(out, "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n", done);
}

static void mir_emit_recursive_wide_product(
    FILE *out, const struct MirRecursiveWideProduct *plan)
{
    int recurse = new_label();
    int decremented = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_emit_wide_parameter(out, plan->parameter_stack_offset);
    fprintf(out,
            "\tld a,d\n\tor e\n\tor h\n\tor l\n\tjp nz,L%d\n"
            "\tld hl,%d\n\tld de,0\n\tret\n"
            "L%d:\n\tpush de\n\tpush hl\n"
            "\tld bc,1\n\tor a\n\tsbc hl,bc\n\tjp nc,L%d\n"
            "\tdec de\nL%d:\n\tpush de\n\tpush hl\n",
            recurse, plan->base_result,
            recurse, decremented, decremented);
    mir_machine_emit_symbol_call(out, plan->function);
    fputs("\tpop bc\n\tpop bc\n", out);
    if (plan->operation == '*') {
        mir_emit_runtime_call(out, "__lmul");
        fputs("\tpop bc\n\tpop bc\n", out);
    } else {
        fputs("\tpop bc\n\tadd hl,bc\n\tex de,hl\n"
              "\tpop bc\n\tadc hl,bc\n\tex de,hl\n", out);
    }
    fputs("\tret\n", out);
}

static void mir_emit_recursive_tree_pointer(
    FILE *out, const struct MirRecursiveWideTreeSum *plan,
    int pushed_bytes)
{
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n",
            plan->parameter_stack_offset + pushed_bytes);
}

static void mir_emit_recursive_tree_value(
    FILE *out, const struct MirRecursiveWideTreeSum *plan,
    int pushed_bytes)
{
    mir_emit_recursive_tree_pointer(out, plan, pushed_bytes);
    mir_machine_emit_hl_offset(out, plan->value_offset, 0);
    if (plan->value_width == 4) {
        fputs("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
              "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
              "\tld l,c\n\tld h,b\n", out);
    } else {
        fputs("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
              "\tld h,b\n\tld l,c\n", out);
        if (plan->value_is_unsigned)
            fputs("\tld de,0\n", out);
        else
            fputs("\tld a,b\n\trlca\n\tsbc a,a\n"
                  "\tld d,a\n\tld e,a\n", out);
    }
}

static void mir_emit_recursive_wide_tree_sum(
    FILE *out, const struct MirRecursiveWideTreeSum *plan)
{
    int nonnull = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_emit_recursive_tree_pointer(out, plan, 0);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out,
            "\tjp nz,L%d\n\tld hl,0\n\tld de,0\n\tret\nL%d:\n",
            nonnull, nonnull);

    if (plan->value_first) {
        mir_machine_emit_hl_offset(out, plan->value_offset, 0);
        fputs("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
              "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
              "\tld l,c\n\tld h,b\n\tpush de\n\tpush hl\n", out);
    }
    mir_emit_recursive_tree_pointer(
        out, plan, plan->value_first ? 4 : 0);
    mir_machine_emit_hl_offset(out, plan->left_offset, 0);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n", out);
    mir_machine_emit_symbol_call(out, plan->function);
    fputs("\tpop bc\n", out);
    if (!plan->value_first) {
        fputs("\tpush de\n\tpush hl\n", out);
        mir_emit_recursive_tree_value(out, plan, 4);
    }
    fputs("\tpop bc\n\tadd hl,bc\n\tex de,hl\n"
          "\tpop bc\n\tadc hl,bc\n\tex de,hl\n"
          "\tpush de\n\tpush hl\n", out);
    mir_emit_recursive_tree_pointer(out, plan, 4);
    mir_machine_emit_hl_offset(out, plan->right_offset, 0);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n", out);
    mir_machine_emit_symbol_call(out, plan->function);
    fputs("\tpop bc\n"
          "\tpop bc\n\tadd hl,bc\n\tex de,hl\n"
          "\tpop bc\n\tadc hl,bc\n\tex de,hl\n\tret\n", out);
}

static void mir_emit_byte_rotate_flags(
    FILE *out, const struct MirByteRotateFlags *plan)
{
    int asl = new_label();
    int rol = new_label();
    int lsr = new_label();
    int rol_ready = new_label();
    int ror_ready = new_label();
    int flags = new_label();
    int nonzero = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld a,(hl)\n"
            "\tand 224\n\tld b,a\n"
            "\tld hl,%d\n\tadd hl,sp\n\tld c,(hl)\n"
            "\tld a,b\n\tor a\n\tjp z,L%d\n"
            "\tcp 32\n\tjp z,L%d\n"
            "\tcp 64\n\tjp z,L%d\n",
            plan->operation_stack_offset,
            plan->value_stack_offset,
            asl, rol, lsr);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->carry_offset, 0);
    fprintf(out,
            "\tor a\n\tjp z,L%d\n\tscf\nL%d:\n\trr c\n",
            ror_ready, ror_ready);
    fputs("\tld a,0\n\tadc a,0\n", out);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->carry_offset, 1);
    fprintf(out, "\tjp L%d\n", flags);
    fprintf(out, "L%d:\n\tsla c\n", asl);
    fputs("\tld a,0\n\tadc a,0\n", out);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->carry_offset, 1);
    fprintf(out, "\tjp L%d\n", flags);
    fprintf(out, "L%d:\n", rol);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->carry_offset, 0);
    fprintf(out,
            "\tor a\n\tjp z,L%d\n\tscf\nL%d:\n\trl c\n",
            rol_ready, rol_ready);
    fputs("\tld a,0\n\tadc a,0\n", out);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->carry_offset, 1);
    fprintf(out, "\tjp L%d\n", flags);
    fprintf(out, "L%d:\n\tsrl c\n", lsr);
    fputs("\tld a,0\n\tadc a,0\n", out);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->carry_offset, 1);
    fprintf(out, "L%d:\n", flags);
    fputs("\tld a,c\n\trlca\n\tld a,0\n\tadc a,0\n", out);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->negative_offset, 1);
    fputs("\tld a,c\n\tor a\n\tld a,0\n", out);
    fprintf(out, "\tjp nz,L%d\n\tinc a\nL%d:\n", nonzero, nonzero);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->zero_offset, 1);
    fputs("\tld l,c\n\tld h,0\n\tret\n", out);
}

static void mir_emit_status_unpack(
    FILE *out, const struct MirStatusUnpack *plan)
{
    int flag;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->stack_offset, 0);
    fputs("\tinc a\n", out);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->stack_offset, 1);
    fputs("\tld l,a\n\tld h,0\n", out);
    mir_machine_emit_global_address_de(
        out, plan->memory, plan->memory_offset);
    fputs("\tadd hl,de\n\tld c,(hl)\n", out);
    for (flag = 0; flag < 6; ++flag) {
        int clear = new_label();

        fprintf(out, "\tbit %d,c\n\tld a,0\n\tjp z,L%d\n\tinc a\nL%d:\n",
                plan->masks[flag] == 128 ? 7 :
                plan->masks[flag] == 64 ? 6 :
                plan->masks[flag] == 8 ? 3 :
                plan->masks[flag] == 4 ? 2 :
                plan->masks[flag] == 2 ? 1 : 0,
                clear, clear);
        mir_machine_emit_global_byte_a(
            out, plan->state, plan->flag_offsets[flag], 1);
    }
    fputs("\tret\n", out);
}

static void mir_emit_status_pack(
    FILE *out, const struct MirStatusPack *plan)
{
    int flag;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fputs("\tld c,48\n", out);
    for (flag = 0; flag < 6; ++flag) {
        int clear = new_label();

        mir_machine_emit_global_byte_a(
            out, plan->state, plan->flag_offsets[flag], 0);
        fprintf(out, "\tor a\n\tjp z,L%d\n\tset %d,c\nL%d:\n",
                clear,
                plan->masks[flag] == 128 ? 7 :
                plan->masks[flag] == 64 ? 6 :
                plan->masks[flag] == 8 ? 3 :
                plan->masks[flag] == 4 ? 2 :
                plan->masks[flag] == 2 ? 1 : 0,
                clear);
    }
    fputs("\tld l,c\n\tld h,0\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->function);
    fputs("\tpop bc\n\tret\n", out);
}

static void mir_emit_byte_math_flags(
    FILE *out, const struct MirByteMathFlags *plan)
{
    int compare = new_label();
    int non_decimal = new_label();
    int decimal = new_label();
    int not_subtract = new_label();
    int addition = new_label();
    int logic_or = new_label();
    int logic_and = new_label();
    int logic_xor = new_label();
    int carry_ready = new_label();
    int flags = new_label();
    int nonzero = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld a,(hl)\n"
            "\tand 224\n\tld b,a\n"
            "\tld hl,%d\n\tadd hl,sp\n\tld c,(hl)\n"
            "\tld a,b\n\tcp 192\n\tjp z,L%d\n",
            plan->op_stack_offset, plan->rhs_stack_offset, compare);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->decimal_offset, 0);
    fprintf(out,
            "\tor a\n\tjp z,L%d\n"
            "\tld a,b\n\tcp 224\n\tjp z,L%d\n"
            "\tcp 96\n\tjp z,L%d\n"
            "L%d:\n\tld a,b\n\tcp 224\n\tjp nz,L%d\n"
            "\tld a,c\n\tcpl\n\tld c,a\n\tjp L%d\n"
            "L%d:\n\tld a,b\n\tcp 96\n\tjp z,L%d\n"
            "\tor a\n\tjp z,L%d\n"
            "\tcp 32\n\tjp z,L%d\n\tjp L%d\n",
            non_decimal, decimal, decimal, non_decimal, not_subtract,
            addition, not_subtract, addition, logic_or, logic_and,
            logic_xor);

    fprintf(out, "L%d:\n", addition);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->carry_offset, 0);
    fprintf(out, "\tor a\n\tjp z,L%d\n\tscf\nL%d:\n",
            carry_ready, carry_ready);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->accumulator_offset, 0);
    fputs("\tadc a,c\n\tpush af\n\tpop de\n"
          "\tld a,e\n\tand 1\n", out);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->carry_offset, 1);
    fputs("\tld a,e\n\trrca\n\trrca\n\tand 1\n", out);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->overflow_offset, 1);
    fprintf(out, "\tld c,d\n\tjp L%d\n", flags);

    fprintf(out, "L%d:\n", logic_or);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->accumulator_offset, 0);
    fprintf(out, "\tor c\n\tld c,a\n\tjp L%d\n", flags);
    fprintf(out, "L%d:\n", logic_and);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->accumulator_offset, 0);
    fprintf(out, "\tand c\n\tld c,a\n\tjp L%d\n", flags);
    fprintf(out, "L%d:\n", logic_xor);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->accumulator_offset, 0);
    fputs("\txor c\n\tld c,a\n", out);

    fprintf(out, "L%d:\n\tld a,c\n", flags);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->accumulator_offset, 1);
    fputs("\tld a,c\n\trlca\n\tld a,0\n\tadc a,0\n", out);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->negative_offset, 1);
    fputs("\tld a,c\n\tor a\n\tld a,0\n", out);
    fprintf(out, "\tjp nz,L%d\n\tinc a\nL%d:\n",
            nonzero, nonzero);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->zero_offset, 1);
    fputs("\tret\n", out);

    fprintf(out, "L%d:\n\tld l,c\n\tld h,0\n\tpush hl\n",
            compare);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->accumulator_offset, 0);
    fputs("\tld l,a\n\tld h,0\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->compare_function);
    fputs("\tpop bc\n\tpop bc\n\tret\n", out);

    fprintf(out, "L%d:\n\tld l,c\n\tld h,0\n\tpush hl\n"
                 "\tld l,b\n\tld h,0\n\tpush hl\n", decimal);
    mir_machine_emit_symbol_call(out, plan->decimal_function);
    fputs("\tpop bc\n\tpop bc\n\tret\n", out);
}

static void mir_emit_byte_range_union(
    FILE *out, const struct MirByteRangeUnion *plan)
{
    int accepted = new_label();
    int range;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld c,(hl)\n",
            plan->stack_offset);
    for (range = 0; range < 3; ++range)
        fprintf(out,
                "\tld a,c\n\tsub %d\n\tcp %d\n\tjp c,L%d\n",
                plan->lower[range],
                plan->upper[range] - plan->lower[range] + 1,
                accepted);
    fputs("\tld hl,0\n\tret\n", out);
    fprintf(out, "L%d:\n\tld hl,1\n\tret\n", accepted);
}

static void mir_emit_byte_array_sum(
    FILE *out, const struct MirByteArraySum *plan)
{
    int done = new_label();
    int exit = new_label();
    int loop = new_label();

    fprintf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tbit 7,d\n\tjp nz,L%d\n"
            "\tld a,d\n\tor e\n\tjp z,L%d\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tpush bc\n\tpop iy\n\tadd iy,de\n"
            "\tld de,0\n"
            "L%d:\n\tld a,(bc)\n\tinc bc\n"
            "\tld l,a\n\tld h,0\n\tadd hl,de\n\tex de,hl\n"
            "\tpush iy\n\tpop hl\n\tor a\n\tsbc hl,bc\n"
            "\tjp nz,L%d\n\tex de,hl\n\tjp L%d\n"
            "L%d:\n\tld hl,0\n"
            "L%d:\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            plan->count_stack_offset + 2, done, done,
            plan->array_stack_offset + 2, loop, loop,
            exit, done, exit);
}

static void mir_emit_wraparound_bool_step(
    FILE *out, const struct MirWraparoundBoolStep *plan)
{
    int done = new_label();
    int exit = new_label();
    int left_ready = new_label();
    int loop = new_label();
    int no_increment = new_label();
    int right_ready = new_label();
    int zero = new_label();

    fprintf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tbit 7,b\n\tjp nz,L%d\n"
            "\tld a,b\n\tor c\n\tjp z,L%d\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tpush bc\n\tpop iy\n"
            "\tld bc,0\n\texx\n\tld bc,0\n\texx\n"
            "L%d:\n"
            "\tld h,b\n\tld l,c\n"
            "\tld a,b\n\tor c\n\tjp nz,L%d\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n"
            "\tld h,(hl)\n\tld l,a\n"
            "L%d:\n"
            "\tdec hl\n\tadd hl,de\n"
            "\tld a,(hl)\n\tex af,af'\n"
            "\tinc bc\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tcp c\n\tjp nz,L%d\n"
            "\tinc hl\n\tld a,(hl)\n\tcp b\n\tjp nz,L%d\n"
            "\tld bc,0\n"
            "L%d:\n"
            "\tld h,b\n\tld l,c\n\tadd hl,de\n"
            "\tld a,(hl)\n\tld h,a\n\tex af,af'\n\txor h\n"
            "\tld (iy+0),a\n\tinc iy\n"
            "\tor a\n\tjp z,L%d\n"
            "\texx\n\tinc bc\n\texx\n"
            "L%d:\n"
            "\tld a,b\n\tor c\n\tjp nz,L%d\n"
            "L%d:\n"
            "\texx\n\tpush bc\n\texx\n\tpop hl\n"
            "\tld de,0\n\tjp L%d\n"
            "L%d:\n\tld de,0\n\tld hl,0\n"
            "L%d:\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            plan->count_stack_offset + 2, zero, zero,
            plan->current_stack_offset + 2,
            plan->next_stack_offset + 2,
            loop, left_ready,
            plan->count_stack_offset + 2, left_ready,
            plan->count_stack_offset + 2,
            right_ready, right_ready, right_ready,
            no_increment, no_increment, loop,
            done, exit, zero, exit);
}

static void mir_emit_fixed_row_word_sum(
    FILE *out, const struct MirFixedRowWordSum *plan)
{
    int done = new_label();
    int exit = new_label();
    int loop = new_label();

    fprintf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tbit 7,d\n\tjp nz,L%d\n"
            "\tld a,d\n\tor e\n\tjp z,L%d\n"
            "\tex de,hl\n\tadd hl,hl\n"
            "\tld d,h\n\tld e,l\n\tadd hl,hl\n\tadd hl,de\n"
            "\tex de,hl\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tpush bc\n\tpop iy\n\tadd iy,de\n"
            "\tld de,0\n"
            "L%d:\n\tld a,(bc)\n\tld l,a\n\tinc bc\n"
            "\tld a,(bc)\n\tld h,a\n\tinc bc\n"
            "\tadd hl,de\n\tex de,hl\n"
            "\tpush iy\n\tpop hl\n\tor a\n\tsbc hl,bc\n"
            "\tjp nz,L%d\n\tex de,hl\n\tjp L%d\n"
            "L%d:\n\tld hl,0\n"
            "L%d:\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            plan->rows_stack_offset + 2, done, done,
            plan->array_stack_offset + 2, loop, loop,
            exit, done, exit);
}

static void mir_emit_fixed_wide_zero(
    FILE *out, const struct MirFixedWideZero *plan)
{
    int done = new_label();
    int loop = new_label();
    int nonzero = new_label();

    fprintf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tpush de\n\tpop iy\n\tld b,%d\n"
            "L%d:\n\tld a,(iy+0)\n\tor (iy+1)\n"
            "\tor (iy+2)\n\tor (iy+3)\n\tjp nz,L%d\n"
            "\tinc iy\n\tinc iy\n\tinc iy\n\tinc iy\n"
            "\tdjnz L%d\n\tld hl,1\n\tjp L%d\n"
            "L%d:\n\tld hl,0\n"
            "L%d:\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            plan->parameter_stack_offset + 2, plan->count,
            loop, nonzero, loop, done, nonzero, done);
}

static void mir_emit_constant_byte_fill(
    FILE *out, const struct MirConstantByteFill *plan)
{
    int loop = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n"
            "\tld a,%d\n\tld b,%d\n"
            "L%d:\n\tld (hl),a\n\tinc hl\n\tdjnz L%d\n\tret\n",
            plan->parameter_stack_offset, plan->value,
            plan->count, loop, loop);
}

static void mir_emit_affine_byte_fill(
    FILE *out, const struct MirAffineByteFill *plan)
{
    int loop = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    if (plan->value_from_parameter)
        fprintf(out,
                "\tld hl,%d\n\tadd hl,sp\n\tld a,(hl)\n",
                plan->base_stack_offset);
    else
        fprintf(out, "\tld a,%d\n", plan->initial_value);
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n"
            "\tld b,%d\n"
            "L%d:\n\tld (hl),a\n\tinc hl\n",
            plan->pointer_stack_offset, plan->count, loop);
    if (plan->step == 1)
        fputs("\tinc a\n", out);
    else
        fprintf(out, "\tadd a,%d\n", plan->step);
    fprintf(out, "\tdjnz L%d\n\tret\n", loop);
}

static void mir_emit_wide_left_shift_count(FILE *out)
{
    int done = new_label();
    int loop = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,1\n\tld de,0\n\tld bc,0\n"
            "L%d:\n\tld a,d\n\tor e\n\tor h\n\tor l\n"
            "\tjp z,L%d\n\tinc bc\n\tadd hl,hl\n"
            "\trl e\n\trl d\n\tjp L%d\n"
            "L%d:\n\tld h,b\n\tld l,c\n\tret\n",
            loop, done, loop, done);
}

static void mir_emit_palindrome_scan(
    FILE *out, const struct MirPalindromeScan *plan)
{
    int different = new_label();
    int found = new_label();
    int loop = new_label();
    int scan = new_label();
    int same = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tld h,b\n\tld l,c\n"
            "L%d:\n\tld a,(hl)\n\tor a\n"
            "\tjp z,L%d\n\tinc hl\n\tjp L%d\n"
            "L%d:\n\tdec hl\n\tex de,hl\n"
            "L%d:\n\tld h,d\n\tld l,e\n\tor a\n\tsbc hl,bc\n"
            "\tjp c,L%d\n\tjp z,L%d\n"
            "\tld a,(bc)\n\tld h,d\n\tld l,e\n\tcp (hl)\n"
            "\tjp nz,L%d\n\tinc bc\n\tdec de\n\tjp L%d\n"
            "L%d:\n\tld hl,1\n\tret\n"
            "L%d:\n\tld hl,0\n\tret\n",
            plan->parameter_stack_offset,
            scan, found, scan, found, loop, same, same,
            different, loop, same, different);
}

static void mir_emit_dynamic_row_scan(
    FILE *out, const struct MirDynamicRowScan *plan)
{
    int loop = new_label();
    int step;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out, "\tld de,0\n\tld bc,0\nL%d:\n"
                 "\tld h,d\n\tld l,e\n", loop);
    for (step = 1; step < plan->row_stride; step <<= 1)
        fputs("\tadd hl,hl\n", out);
    fputs("\tadd hl,bc\n", out);
    mir_machine_emit_global_address_de(out, plan->table, 0);
    fputs("\tadd hl,de\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n",
          out);
    for (step = 0; step < plan->element_stride; ++step)
        fputs("\tinc bc\n", out);
    fprintf(out,
            "\tld a,c\n\tcp %d\n\tjp nz,L%d\n"
            "\tex de,hl\n\tret\n",
            plan->count * plan->element_stride, loop);
}

static void mir_emit_byte_mismatch_scan(
    FILE *out, const struct MirByteMismatchScan *plan)
{
    int loop = new_label();
    int mismatch = new_label();

    fprintf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tpush bc\n\tpop iy\n"
            "\tld de,%d\n\tadd iy,de\n"
            "\tld hl,%d\n\tadd hl,sp\n\tld e,(hl)\n"
            "L%d:\n\tld a,(bc)\n\tcp e\n\tjp nz,L%d\n"
            "\tinc bc\n\tinc e\n"
            "\tpush iy\n\tpop hl\n\tor a\n\tsbc hl,bc\n"
            "\tjp nz,L%d\n"
            "\tld hl,65535\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n"
            "L%d:\n\tld h,b\n\tld l,c\n"
            "\tpush iy\n\tpop de\n\tor a\n\tsbc hl,de\n"
            "\tld de,%d\n\tadd hl,de\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            plan->pointer_stack_offset + 2, plan->count,
            plan->base_stack_offset + 2,
            loop, mismatch, loop, mismatch, plan->count);
}

static void mir_emit_variable_byte_step_sum(
    FILE *out, const struct MirVariableByteStepSum *plan)
{
    int done = new_label();
    int loop = new_label();
    int normal_step = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld a,(hl)\n"
            "\tld hl,%d\n\tadd hl,sp\n\tld c,(hl)\n"
            "\tadd a,c\n\tld b,a\n\tld de,0\n"
            "L%d:\n\tld a,b\n\tcp %d\n\tjp nc,L%d\n"
            "\tld l,b\n\tld h,0\n\tadd hl,de\n\tex de,hl\n",
            plan->first_stack_offset,
            plan->step_stack_offset,
            loop, plan->bound, done);
    if (plan->has_double_step)
        fprintf(out,
                "\tld a,b\n\tcp %d\n\tjp nz,L%d\n"
                "\tadd a,c\n\tld b,a\nL%d:\n",
                plan->double_step_value, normal_step, normal_step);
    fprintf(out,
            "\tld a,b\n\tadd a,c\n\tld b,a\n\tjp L%d\n"
            "L%d:\n\tex de,hl\n\tret\n",
            loop, done);
}

static void mir_emit_fixed_reverse_word_copy(
    FILE *out, const struct MirFixedReverseWordCopy *plan)
{
    int loop = new_label();

    fprintf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_address_de(
        out, plan->source,
        plan->source_offset + (plan->count - 1) * 2);
    fputs("\tpush de\n\tpop iy\n", out);
    mir_machine_emit_global_address_de(
        out, plan->destination, plan->destination_offset);
    fprintf(out,
            "\tld c,%d\n"
            "L%d:\n\tld a,(iy+0)\n\tld (de),a\n\tinc de\n"
            "\tld a,(iy+1)\n\tld (de),a\n\tinc de\n"
            "\tdec iy\n\tdec iy\n\tdec c\n\tjp nz,L%d\n"
            "\tpop iy\n;@dcc.reg free=iy\n\tret\n",
            plan->count, loop, loop);
}

static void mir_emit_fixed_random_word_fill(
    FILE *out, const struct MirFixedRandomWordFill *plan)
{
    int loop = new_label();

    fprintf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_address_de(
        out, plan->destination, plan->destination_offset);
    fputs("\tpush de\n\tpop iy\n", out);
    fprintf(out, "L%d:\n", loop);
    mir_machine_emit_symbol_call(out, plan->random_function);
    fprintf(out, "\tld de,%d\n", plan->modulus);
    mir_emit_runtime_call(out, "__mods");
    fputs("\tld (iy+0),l\n\tld (iy+1),h\n\tinc iy\n\tinc iy\n"
          "\tpush iy\n\tpop hl\n", out);
    mir_machine_emit_global_address_de(
        out, plan->destination,
        plan->destination_offset + plan->count * 2);
    fputs("\tor a\n\tsbc hl,de\n", out);
    fprintf(out, "\tjp nz,L%d\n", loop);
    mir_machine_emit_symbol_call(out, plan->finish_function);
    fputs("\tpop iy\n;@dcc.reg free=iy\n\tret\n", out);
}

static void mir_emit_global_byte_copy_state(
    FILE *out, const struct MirGlobalByteCopyState *plan)
{
    int state;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_address_de(
        out, plan->source, plan->source_offset);
    fputs("\tex de,hl\n", out);
    mir_machine_emit_global_address_de(
        out, plan->destination, plan->destination_offset);
    fprintf(out, "\tld bc,%d\n\tldir\n", plan->count);
    for (state = 0; state < 3; ++state) {
        if (plan->state_widths[state] == 1) {
            fprintf(out, "\tld a,%d\n", plan->state_values[state]);
            mir_machine_emit_global_byte_a(
                out, plan->state[state],
                plan->state_offsets[state], 1);
        } else {
            fprintf(out, "\tld hl,%d\n", plan->state_values[state]);
            mir_machine_emit_global_word_store(
                out, plan->state[state],
                plan->state_offsets[state]);
        }
    }
    fputs("\tret\n", out);
}

static void mir_emit_stride_global_argument(
    FILE *out, struct Sym *symbol, int offset, int stride)
{
    fputs("\tpush iy\n\tpop hl\n", out);
    mir_emit_mul_hl_const(out, (unsigned long)stride);
    mir_machine_emit_global_address_de(out, symbol, offset);
    fputs("\tadd hl,de\n\tpush hl\n", out);
}

static void mir_emit_fixed_global_stride_call(
    FILE *out, const struct MirFixedGlobalStrideCall *plan)
{
    int loop = new_label();

    fprintf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fputs("\tld iy,0\n", out);
    fprintf(out, "L%d:\n", loop);
    mir_emit_stride_global_argument(
        out, plan->second, plan->second_offset,
        plan->second_stride);
    mir_emit_stride_global_argument(
        out, plan->first, plan->first_offset,
        plan->first_stride);
    mir_machine_emit_global_address_de(
        out, plan->fixed, plan->fixed_offset);
    fputs("\tpush de\n", out);
    mir_machine_emit_symbol_call(out, plan->function);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n\tinc iy\n"
          "\tpush iy\n\tpop hl\n\tld a,l\n", out);
    fprintf(out, "\tcp %d\n\tjp nz,L%d\n"
                 "\tpop iy\n;@dcc.reg free=iy\n\tret\n",
            plan->count, loop);
}

static void mir_emit_constant_loop_check(
    FILE *out, const struct MirConstantLoopCheck *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out, "\tld hl,S%d\n\tpush hl\n"
                 "\tld hl,1\n\tpush hl\n",
            plan->string_id);
    mir_machine_emit_symbol_call(out, plan->function);
    fputs("\tpop bc\n\tpop bc\n\tret\n", out);
}

static void mir_emit_global_byte_countdown(
    FILE *out, const struct MirGlobalByteCountdown *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld a,(hl)\n"
            "\tdec a\n\tld c,a\n\tld b,0\n",
            plan->parameter_stack_offset);
    mir_machine_emit_global_word(out, plan->value, 0);
    fputs("\tld d,h\n\tld e,l\n\tadd hl,hl\n"
          "\tadd hl,de\n\tadd hl,hl\n\tadd hl,bc\n\tret\n", out);
}

static void mir_emit_conditional_string_report(
    FILE *out, const struct MirConditionalStringReport *plan)
{
    int selected = new_label();
    int false_string = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n"
            "\tjp z,L%d\n\tld hl,S%d\n\tjp L%d\n"
            "L%d:\n\tld hl,S%d\n"
            "L%d:\n\tpush hl\n\tpush bc\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->name_stack_offset,
            plan->condition_stack_offset,
            false_string, plan->true_string_id, selected,
            false_string, plan->false_string_id,
            selected, plan->format_string_id);
    mir_machine_emit_symbol_call(out, plan->function);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n\tret\n", out);
}

static void mir_emit_word_range_bool(
    FILE *out, const struct MirWordRangeBool *plan)
{
    int outside = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld a,(hl)\n\tor a\n"
            "\tjp nz,L%d\n\tld a,e\n\tcp %d\n\tjp nc,L%d\n"
            "\tld hl,1\n\tret\nL%d:\n\tld hl,0\n\tret\n",
            plan->parameter_stack_offset,
            outside, plan->upper, outside, outside);
}

static void mir_emit_ascii_upper(
    FILE *out, const struct MirAsciiUpper *plan)
{
    int result = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    if (plan->width == 1)
        fprintf(out,
                "\tld hl,%d\n\tadd hl,sp\n\tld a,(hl)\n",
                plan->parameter_stack_offset);
    else
        fprintf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n"
                "\tld a,h\n\tor a\n\tjp nz,L%d\n\tld a,l\n",
                plan->parameter_stack_offset, result);
    fprintf(out,
            "\tcp %d\n\tjp c,L%d\n\tcp %d\n\tjp nc,L%d\n",
            plan->lower, result, plan->upper + 1, result);
    if (plan->adjustment < 0)
        fprintf(out, "\tsub %d\n", -plan->adjustment);
    else if (plan->adjustment > 0)
        fprintf(out, "\tadd a,%d\n", plan->adjustment);
    if (plan->width == 1)
        fprintf(out,
                "L%d:\n\tld l,a\n\trlca\n\tsbc a,a\n\tld h,a\n\tret\n",
                result);
    else
        fprintf(out, "\tld l,a\nL%d:\n\tret\n", result);
}

static void mir_emit_fixed_word_array_sum(
    FILE *out, const struct MirFixedWordArraySum *plan)
{
    int element;
    int offset;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    if (!plan->pointer_is_volatile) {
        fprintf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
                "\tld de,0\n",
                plan->parameter_stack_offset);
        for (element = 0; element < plan->count; ++element)
            fputs("\tld a,(bc)\n\tinc bc\n\tld l,a\n"
                  "\tld a,(bc)\n\tinc bc\n\tld h,a\n"
                  "\tadd hl,de\n\tex de,hl\n", out);
        fputs("\tex de,hl\n\tret\n", out);
        return;
    }
    fputs("\tld bc,0\n", out);
    for (element = 0; element < plan->count; ++element) {
        fprintf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n",
                plan->parameter_stack_offset);
        for (offset = 0; offset < element * 2; ++offset)
            fputs("\tinc hl\n", out);
        fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
              "\tld h,b\n\tld l,c\n\tadd hl,de\n"
              "\tld b,h\n\tld c,l\n", out);
    }
    fputs("\tld h,b\n\tld l,c\n\tret\n", out);
}

static void mir_emit_slice_word_sum(
    FILE *out, const struct MirSliceWordSum *plan)
{
    int done = new_label();
    int empty = new_label();
    int loop = new_label();

    fprintf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tbit 7,d\n\tjp nz,L%d\n"
            "\tld a,d\n\tor e\n\tjp z,L%d\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tpush bc\n\tpop iy\n\tadd iy,de\n\tadd iy,de\n"
            "\tld de,0\n"
            "L%d:\n\tld a,(bc)\n\tinc bc\n\tld l,a\n"
            "\tld a,(bc)\n\tinc bc\n\tld h,a\n"
            "\tadd hl,de\n\tex de,hl\n"
            "\tpush iy\n\tpop hl\n\tor a\n\tsbc hl,bc\n"
            "\tjp nz,L%d\n\tex de,hl\n\tjp L%d\n"
            "L%d:\n\tld hl,0\n"
            "L%d:\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            plan->parameter_stack_offset + plan->count_offset + 2,
            empty, empty,
            plan->parameter_stack_offset + plan->data_offset + 2,
            loop, loop, done, empty, done);
}

static void mir_emit_conditional_null_identity(
    FILE *out, const struct MirConditionalNullIdentity *plan)
{
    int is_false = new_label();
    int is_true = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tjp z,L%d\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tjp nz,L%d\n"
            "L%d:\n\tld hl,1\n\tret\n"
            "L%d:\n\tld hl,0\n\tret\n",
            plan->condition_stack_offset, is_true,
            plan->pointer_stack_offset, is_false,
            is_true, is_false);
}

static void mir_emit_wide_constant_equal(
    FILE *out, const struct MirWideConstantEqual *plan)
{
    int byte;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out, "\tld hl,%d\n\tadd hl,sp\n\tld c,0\n",
            plan->parameter_stack_offset);
    for (byte = 0; byte < 4; ++byte) {
        fprintf(out, "\tld a,(hl)\n\txor %lu\n\tor c\n\tld c,a\n",
                (plan->value >> (byte * 8)) & 0xffUL);
        if (byte != 3)
            fputs("\tinc hl\n", out);
    }
    fputs("\tld hl,0\n\tor a\n\tret nz\n\tinc hl\n\tret\n", out);
}

static void mir_emit_float_truth_once(
    FILE *out, const struct MirFloatTruthOnce *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tinc hl\n"
            "\tor (hl)\n\tld c,a\n\tinc hl\n\tld a,(hl)\n"
            "\tand 127\n\tor c\n\tld hl,0\n\tret z\n\tinc hl\n\tret\n",
            plan->parameter_stack_offset);
}

static void mir_emit_nested_word_long_select(
    FILE *out, const struct MirNestedWordLongSelect *plan)
{
    int second = new_label();
    int third = new_label();
    int selected = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tjp z,L%d\n"
            "\tld hl,%d\n\tjp L%d\n"
            "L%d:\n\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tjp z,L%d\n"
            "\tld hl,%d\n\tjp L%d\n"
            "L%d:\n",
            plan->first_condition_stack_offset, second,
            plan->first_value, selected,
            second, plan->second_condition_stack_offset, third,
            plan->second_value, selected, third);
    if (plan->third_is_parameter)
        fprintf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld e,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,e\n",
                plan->third_stack_offset);
    else
        fprintf(out, "\tld hl,%d\n", plan->third_value);
    fprintf(out,
            "L%d:\n\tld a,h\n\trlca\n\tsbc a,a\n"
            "\tld e,a\n\tld d,a\n\tret\n",
            selected);
}

static void mir_emit_float_int_truth(
    FILE *out, const struct MirFloatIntTruth *plan)
{
    int false_result = new_label();
    int true_result = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tinc hl\n"
            "\tor (hl)\n\tld c,a\n\tinc hl\n\tld a,(hl)\n"
            "\tand 127\n\tor c\n",
            plan->float_stack_offset);
    if (plan->operation == '&')
        fprintf(out, "\tjp z,L%d\n", false_result);
    else
        fprintf(out, "\tjp nz,L%d\n", true_result);
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n",
            plan->int_stack_offset);
    if (plan->operation == '&')
        fprintf(out, "\tjp z,L%d\n", false_result);
    else
        fprintf(out, "\tjp nz,L%d\n", true_result);
    if (plan->operation == '|')
        fprintf(out, "L%d:\n", false_result);
    fprintf(out, "\tld hl,%d\n\tret\n",
            plan->operation == '&' ? 1 : 0);
    if (plan->operation == '&')
        fprintf(out, "L%d:\n\tld hl,0\n\tret\n", false_result);
    else
        fprintf(out, "L%d:\n\tld hl,1\n\tret\n", true_result);
}

static void mir_emit_conditional_float_long(
    FILE *out, const struct MirConditionalFloatLong *plan)
{
    int false_arm = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tjp z,L%d\n"
            "\tld hl,%d\n\tld a,h\n\trlca\n\tsbc a,a\n"
            "\tld e,a\n\tld d,a\n\tret\n"
            "L%d:\n",
            plan->condition_stack_offset, false_arm,
            plan->true_value, false_arm);
    if (plan->kind == MIR_CONDITIONAL_FLOAT_ADD) {
        mir_emit_wide_parameter(out, plan->argument_stack_offset);
        fputs("\tpush de\n\tpush hl\n", out);
        fprintf(out, "\tld hl,%lu\n\tld de,%lu\n",
                plan->add_bits & 0xffffUL,
                (plan->add_bits >> 16) & 0xffffUL);
        mir_emit_runtime_call(out, "__faf");
        fputs("\tpop bc\n\tpop bc\n", out);
    } else {
        fprintf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n",
                plan->argument_stack_offset);
        mir_machine_emit_symbol_call(out, plan->function);
        fputs("\tpop bc\n", out);
    }
    mir_emit_runtime_call(out, "__ffl");
    fputs("\tret\n", out);
}

static void mir_emit_word_as_long_return(FILE *out, int value)
{
    fprintf(out,
            "\tld hl,%d\n\tld a,h\n\trlca\n\tsbc a,a\n"
            "\tld e,a\n\tld d,a\n\tret\n",
            value);
}

static void mir_emit_conditional_pointer_float_long(
    FILE *out, const struct MirConditionalPointerFloatLong *plan)
{
    int false_arm = new_label();
    int selected = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tjp z,L%d\n",
            plan->condition_stack_offset, false_arm);
    mir_machine_emit_global_address_de(
        out, plan->true_root, plan->element_offset);
    fprintf(out, "\tex de,hl\n\tjp L%d\nL%d:\n",
            selected, false_arm);
    mir_machine_emit_global_word(out, plan->false_pointer, 0);
    if (plan->element_offset != 0)
        fprintf(out, "\tld de,%d\n\tadd hl,de\n",
                plan->element_offset);
    fprintf(out, "L%d:\n", selected);
    fputs("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
          "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld l,c\n\tld h,b\n", out);
    mir_emit_runtime_call(out, "__ffl");
    fputs("\tret\n", out);
}

static void mir_emit_nested_member_float_long(
    FILE *out, const struct MirNestedMemberFloatLong *plan)
{
    int second = new_label();
    int third = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tjp z,L%d\n",
            plan->first_condition_stack_offset, second);
    mir_emit_word_as_long_return(out, plan->first_value);
    fprintf(out,
            "L%d:\n\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tjp z,L%d\n",
            second, plan->second_condition_stack_offset, third);
    mir_emit_word_as_long_return(out, plan->second_value);
    fprintf(out,
            "L%d:\n\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n",
            third, plan->pointer_stack_offset);
    if (plan->member_offset != 0)
        fprintf(out, "\tld de,%d\n\tadd hl,de\n",
                plan->member_offset);
    fputs("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
          "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld l,c\n\tld h,b\n", out);
    mir_emit_runtime_call(out, "__ffl");
    fputs("\tret\n", out);
}

static void mir_emit_conditional_float_compare_long(
    FILE *out, const struct MirConditionalFloatCompareLong *plan)
{
    int false_arm = new_label();
    int nonpositive = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tjp z,L%d\n",
            plan->condition_stack_offset, false_arm);
    mir_emit_word_as_long_return(out, plan->true_value);
    fprintf(out, "L%d:\n", false_arm);
    mir_emit_wide_parameter(out, plan->float_stack_offset);
    fputs("\tpush de\n\tpush hl\n\tld hl,0\n\tld de,0\n", out);
    mir_emit_runtime_call(out, "__fltf");
    fputs("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", nonpositive);
    mir_emit_word_as_long_return(out, plan->positive_value);
    fprintf(out, "L%d:\n", nonpositive);
    mir_emit_word_as_long_return(out, plan->nonpositive_value);
}

static void mir_emit_conditional_bool(
    FILE *out, const struct MirConditionalBool *plan)
{
    int false_arm = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    if (plan->true_value == plan->false_value) {
        fprintf(out, "\tld hl,%d\n\tret\n", plan->true_value);
        return;
    }
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tjp z,L%d\n"
            "\tld hl,%d\n\tret\n"
            "L%d:\n\tld hl,%d\n\tret\n",
            plan->condition_stack_offset, false_arm,
            plan->true_value, false_arm, plan->false_value);
}

static void mir_emit_logical_or_parameters(
    FILE *out, const struct MirLogicalOrParameters *plan)
{
    int nonzero = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tjp nz,L%d\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n\tjp nz,L%d\n"
            "\tld hl,0\n\tret\n"
            "L%d:\n\tld hl,1\n\tret\n",
            plan->first_stack_offset, nonzero,
            plan->second_stack_offset, nonzero, nonzero);
}

static void mir_emit_cleared_record_field(
    FILE *out, int field_offset, int parameter_offset)
{
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n",
            parameter_offset);
    fputs("\tpush iy\n\tpop hl\n", out);
    mir_machine_emit_hl_offset(out, field_offset, 0);
    fputs("\tld (hl),e\n\tinc hl\n\tld (hl),d\n", out);
}

static void mir_emit_cleared_record_append(
    FILE *out, const struct MirClearedRecordAppend *plan)
{
    fprintf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_word(out, plan->root, plan->root_offset);
    mir_machine_emit_hl_offset(out, plan->cursor_member_offset, 0);
    fputs("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
          "\tinc bc\n\tld (hl),b\n\tdec hl\n\tld (hl),c\n", out);
    fputs("\tdec bc\n", out);
    mir_machine_emit_global_word(out, plan->root, plan->root_offset);
    mir_machine_emit_hl_offset(out, plan->array_member_offset, 0);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tpush de\n\tld h,b\n\tld l,c\n", out);
    mir_emit_mul_hl_const(out, (unsigned long)plan->stride);
    fputs("\tpop de\n\tadd hl,de\n\tpush hl\n\tpop iy\n", out);
    fprintf(out, "\tld hl,%d\n\tpush hl\n\tld hl,0\n\tpush hl\n",
            plan->stride);
    fputs("\tpush iy\n", out);
    mir_machine_emit_symbol_call(out, plan->clear_function);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n", out);
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n",
            plan->name_stack_offset + 2);
    fputs("\tpush iy\n", out);
    mir_machine_emit_symbol_call(out, plan->copy_function);
    fputs("\tpop bc\n\tpop bc\n", out);
    mir_emit_cleared_record_field(
        out, plan->kind_field_offset, plan->kind_stack_offset + 2);
    mir_emit_cleared_record_field(
        out, plan->value_field_offset, plan->value_stack_offset + 2);
    mir_machine_emit_global_word(out, plan->root, plan->root_offset);
    mir_machine_emit_hl_offset(out, plan->cursor_member_offset, 0);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n\tdec hl\n\tpop iy\n"
          ";@dcc.reg free=iy\n\tret\n", out);
}

static void mir_emit_record_name_search(
    FILE *out, const struct MirRecordNameSearch *plan)
{
    int done = new_label();
    int found = new_label();
    int loop = new_label();
    int not_found = new_label();

    fprintf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_word(out, plan->root, plan->root_offset);
    mir_machine_emit_hl_offset(out, plan->cursor_member_offset, 0);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n\tdec hl\n\tpush hl\n\tpop iy\n", out);
    fprintf(out,
            "L%d:\n\tpush iy\n\tpop hl\n"
            "\tbit 7,h\n\tjp nz,L%d\n",
            loop, not_found);
    mir_machine_emit_global_word(out, plan->root, plan->root_offset);
    mir_machine_emit_hl_offset(out, plan->array_member_offset, 0);
    fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n"
          "\tpush iy\n\tpop hl\n", out);
    mir_emit_mul_hl_const(out, (unsigned long)plan->stride);
    fputs("\tpop de\n\tadd hl,de\n", out);
    mir_machine_emit_hl_offset(out, plan->name_field_offset, 0);
    fputs("\tld c,l\n\tld b,h\n", out);
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tpush de\n\tpush bc\n",
            plan->name_stack_offset + 2);
    mir_machine_emit_symbol_call(out, plan->compare_function);
    fputs("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    fprintf(out,
            "\tjp z,L%d\n\tdec iy\n\tjp L%d\n"
            "L%d:\n\tpush iy\n\tpop hl\n\tjp L%d\n"
            "L%d:\n\tld hl,65535\n"
            "L%d:\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            found, loop, found, done,
            not_found, done);
}

static void mir_emit_sequential_unary_reports(
    FILE *out, const struct MirSequentialUnaryReports *plan)
{
    int parameter;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (parameter = 4; parameter >= 0; --parameter) {
        fprintf(out,
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n",
                plan->parameter_stack_offsets[parameter] +
                    (4 - parameter) * 2);
        mir_machine_emit_symbol_call(out, plan->helper);
        fputs("\tpop bc\n\tpush hl\n", out);
    }
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", plan->string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    for (parameter = 0; parameter < 6; ++parameter)
        fputs("\tpop bc\n", out);
    fputs("\tret\n", out);
}

static void mir_emit_nibble_append(
    FILE *out, const struct MirNibbleAppend *plan)
{
    int done = new_label();
    int ready = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld c,(hl)\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n"
            "\tld a,c\n\tcp %d\n",
            plan->value_stack_offset,
            plan->pointer_stack_offset, plan->threshold);
    if (plan->high_adjustment < 0)
        fprintf(out, "\tjp c,L%d\n\tsub %d\n\tjp L%d\n",
                ready, -plan->high_adjustment, done);
    else
        fprintf(out, "\tjp c,L%d\n\tadd a,%d\n\tjp L%d\n",
                ready, plan->high_adjustment, done);
    fprintf(out, "L%d:\n", ready);
    if (plan->low_adjustment < 0)
        fprintf(out, "\tsub %d\n", -plan->low_adjustment);
    else if (plan->low_adjustment > 0)
        fprintf(out, "\tadd a,%d\n", plan->low_adjustment);
    fprintf(out, "L%d:\n\tld (hl),a\n\tinc hl\n\tret\n", done);
}

static void mir_emit_volatile_fill_wide_constant(
    FILE *out, const struct MirVolatileFillWideConstant *plan)
{
    int loop = new_label();

    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    fprintf(out, "\tld hl,%d\n\tadd hl,sp\n\tld sp,hl\n",
            plan->buffer_offset);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fputs("\tpush ix\n\tpop hl\n", out);
    fprintf(out,
            "\tld de,%d\n\tadd hl,de\n\tld a,0\n\tld b,%d\n"
            "L%d:\n\tld (hl),a\n\tinc hl\n\tinc a\n\tdjnz L%d\n"
            "\tld hl,%lu\n\tld de,%lu\n"
            "\tld sp,ix\n\tpop ix\n\tret\n",
            plan->buffer_offset, plan->count, loop, loop,
            plan->result & 0xffffUL,
            (plan->result >> 16) & 0xffffUL);
}

static void mir_emit_single_signed_div_check(
    FILE *out, const struct MirSingleSignedDivCheck *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld h,b\n\tld l,c\n",
            plan->numerator_stack_offset,
            plan->denominator_stack_offset);
    mir_emit_runtime_call(
        out, plan->operation == '%' ? "__mods" : "__divs");
    fputs("\tld c,l\n\tld b,h\n", out);
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n"
            "\tpush bc\n",
            plan->label_stack_offset,
            plan->expected_stack_offset + 2);
    mir_machine_emit_symbol_call(out, plan->check_function);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n\tret\n", out);
}

static void mir_emit_local_identity_array_result(
    FILE *out, const struct MirLocalIdentityArrayResult *plan)
{
    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    fprintf(out, "\tld hl,%d\n\tadd hl,sp\n\tld sp,hl\n",
            plan->array_offset);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fputs("\tpush ix\n\tpop hl\n", out);
    mir_machine_emit_hl_offset(out, plan->array_offset, 0);
    mir_machine_emit_global_word_store(
        out, plan->escaped_pointer, plan->escaped_pointer_offset);
    fprintf(out,
            "\tld hl,%d\n\tld sp,ix\n\tpop ix\n\tret\n",
            plan->result);
}

static void mir_emit_constant_function(FILE *out, int result)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out, "\tld hl,%d\n\tret\n", result);
}

static void mir_emit_constant_result_switch(
    FILE *out, const struct MirConstantResultSwitch *plan)
{
    int default_label = new_label();
    int table_label = new_label();
    int width = plan->maximum_case - plan->minimum_case + 1;
    int value;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    fprintf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n",
            plan->parameter_stack_offset);
    if (plan->minimum_case != 0)
        fprintf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n",
                plan->minimum_case);
    fprintf(out,
            "\tld a,h\n\tor a\n\tjp nz,L%d\n"
            "\tld a,l\n\tcp %d\n\tjp nc,L%d\n"
            "\tadd hl,hl\n\tld de,L%d\n\tadd hl,de\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tex de,hl\n\tret\n"
            "L%d:\n",
            default_label, width, default_label,
            table_label, table_label);
    for (value = 0; value < width; ++value)
        fprintf(out, "\tdw %d\n", plan->results[value]);
    fprintf(out, "L%d:\n\tld hl,%d\n\tret\n",
            default_label, plan->default_result);
}

static void mir_emit_local_byte_fill_sum_print(
    FILE *out, const struct MirLocalByteFillSumPrint *plan)
{
    int loop = new_label();

    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    fprintf(out, "\tld hl,%d\n\tadd hl,sp\n\tld sp,hl\n",
            plan->buffer_offset);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    if (plan->fill_has_value)
        fprintf(out, "\tld hl,%d\n\tpush hl\n",
                plan->fill_value);
    fputs("\tpush ix\n\tpop hl\n", out);
    fprintf(out, "\tld de,%d\n\tadd hl,de\n\tpush hl\n",
            plan->buffer_offset);
    mir_machine_emit_symbol_call(out, plan->fill_function);
    fputs("\tpop bc\n", out);
    if (plan->fill_has_value)
        fputs("\tpop bc\n", out);
    fputs("\tpush ix\n\tpop hl\n", out);
    fprintf(out,
            "\tld de,%d\n\tadd hl,de\n\tld de,0\n\tld b,%d\n"
            "L%d:\n",
            plan->buffer_offset, plan->count, loop);
    if (plan->element_is_unsigned) {
        fputs("\tld a,(hl)\n\tinc hl\n\tadd a,e\n\tld e,a\n"
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
    fprintf(out,
            "\tdjnz L%d\n\tex de,hl\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            loop, plan->string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    fputs("\tpop bc\n\tpop bc\n\tld hl,0\n"
          "\tld sp,ix\n\tpop ix\n\tret\n", out);
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
    struct MirGlobalByteChecks global_byte_checks;
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
    struct MirByteMismatchReport byte_mismatch_report;
    struct MirByteArithmeticReports byte_arithmetic_reports;
    struct MirByteBitwiseReport byte_bitwise_report;
    struct MirVariadicSum variadic_sum;
    struct MirGuardedGlobalPop guarded_global_pop;
    struct MirFloatMemberScalarCompare float_member_scalar_compare;
    struct MirExactFloatMismatchReport exact_float_mismatch_report;
    struct MirFloatToleranceReport float_tolerance_report;
    struct MirFloatToleranceFailure float_tolerance_failure;
    struct MirFloatByteReport float_byte_report;
    struct MirRelativeToleranceCall relative_tolerance_call;
    struct MirFixedFloatGridFill fixed_float_grid_fill;
    struct MirConstantFloatConditional constant_float_conditional;
    struct MirConditionalGlobalFloatLoad conditional_global_float_load;
    struct MirReducedFloatPolynomial reduced_float_polynomial;
    struct MirFloatTangentRational float_tangent_rational;
    struct MirRecursiveWideProduct recursive_wide_product;
    struct MirRecursiveWideTreeSum recursive_wide_tree_sum;
    struct MirByteRotateFlags byte_rotate_flags;
    struct MirStatusUnpack status_unpack;
    struct MirStatusPack status_pack;
    struct MirByteMathFlags byte_math_flags;
    struct MirByteRangeUnion byte_range_union;
    struct MirByteArraySum byte_array_sum;
    struct MirWraparoundBoolStep wraparound_bool_step;
    struct MirFixedRowWordSum fixed_row_word_sum;
    struct MirFixedWideZero fixed_wide_zero;
    struct MirConstantByteFill constant_byte_fill;
    struct MirAffineByteFill affine_byte_fill;
    struct MirPalindromeScan palindrome_scan;
    struct MirDynamicRowScan dynamic_row_scan;
    struct MirByteMismatchScan byte_mismatch_scan;
    struct MirVariableByteStepSum variable_byte_step_sum;
    struct MirFixedReverseWordCopy fixed_reverse_word_copy;
    struct MirFixedRandomWordFill fixed_random_word_fill;
    struct MirGlobalByteCopyState global_byte_copy_state;
    struct MirFixedGlobalStrideCall fixed_global_stride_call;
    struct MirConstantLoopCheck constant_loop_check;
    struct MirGlobalByteCountdown global_byte_countdown;
    struct MirConditionalStringReport conditional_string_report;
    struct MirWordRangeBool word_range_bool;
    struct MirAsciiUpper ascii_upper;
    struct MirFixedWordArraySum fixed_word_array_sum;
    struct MirSliceWordSum slice_word_sum;
    struct MirConditionalNullIdentity conditional_null_identity;
    struct MirWideConstantEqual wide_constant_equal;
    struct MirFloatTruthOnce float_truth_once;
    struct MirNestedWordLongSelect nested_word_long_select;
    struct MirFloatIntTruth float_int_truth;
    struct MirConditionalFloatLong conditional_float_long;
    struct MirConditionalPointerFloatLong conditional_pointer_float_long;
    struct MirNestedMemberFloatLong nested_member_float_long;
    struct MirConditionalFloatCompareLong conditional_float_compare_long;
    struct MirConditionalBool conditional_bool;
    struct MirLogicalOrParameters logical_or_parameters;
    struct MirClearedRecordAppend cleared_record_append;
    struct MirRecordNameSearch record_name_search;
    struct MirSequentialUnaryReports sequential_unary_reports;
    struct MirNibbleAppend nibble_append;
    struct MirVolatileFillWideConstant volatile_fill_wide_constant;
    struct MirSingleSignedDivCheck single_signed_div_check;
    struct MirLocalIdentityArrayResult local_identity_array_result;
    struct MirConstantResultSwitch constant_result_switch;
    struct MirLocalByteFillSumPrint local_byte_fill_sum_print;
    struct MirIndexedMemberWrite indexed_member_write;
    long constant;
    int constant_function_result;

    if (mir_match_dead_constant_float_check()) {
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
        fputs("\tret\n", out);
        return 1;
    }
    if (mir_match_local_identity_array_result(
            &local_identity_array_result)) {
        mir_emit_local_identity_array_result(
            out, &local_identity_array_result);
        return 1;
    }
    if (mir_match_word_range_bool(&word_range_bool)) {
        mir_emit_word_range_bool(out, &word_range_bool);
        return 1;
    }
    if (mir_match_ascii_upper(&ascii_upper) ||
        mir_match_ascii_word_case(&ascii_upper)) {
        mir_emit_ascii_upper(out, &ascii_upper);
        return 1;
    }
    if (mir_match_fixed_word_array_sum(
            &fixed_word_array_sum)) {
        mir_emit_fixed_word_array_sum(
            out, &fixed_word_array_sum);
        return 1;
    }
    if (mir_match_slice_word_sum(&slice_word_sum)) {
        mir_emit_slice_word_sum(out, &slice_word_sum);
        return 1;
    }
    if (mir_match_conditional_null_identity(
            &conditional_null_identity)) {
        mir_emit_conditional_null_identity(
            out, &conditional_null_identity);
        return 1;
    }
    if (mir_match_wide_constant_equal(
            &wide_constant_equal)) {
        mir_emit_wide_constant_equal(
            out, &wide_constant_equal);
        return 1;
    }
    if (mir_match_float_truth_once(&float_truth_once)) {
        mir_emit_float_truth_once(out, &float_truth_once);
        return 1;
    }
    if (mir_match_nested_word_long_select(
            &nested_word_long_select)) {
        mir_emit_nested_word_long_select(
            out, &nested_word_long_select);
        return 1;
    }
    if (mir_match_float_int_truth(&float_int_truth)) {
        mir_emit_float_int_truth(out, &float_int_truth);
        return 1;
    }
    if (mir_match_conditional_float_long(
            &conditional_float_long)) {
        mir_emit_conditional_float_long(
            out, &conditional_float_long);
        return 1;
    }
    if (mir_match_conditional_pointer_float_long(
            &conditional_pointer_float_long)) {
        mir_emit_conditional_pointer_float_long(
            out, &conditional_pointer_float_long);
        return 1;
    }
    if (mir_match_nested_member_float_long(
            &nested_member_float_long)) {
        mir_emit_nested_member_float_long(
            out, &nested_member_float_long);
        return 1;
    }
    if (mir_match_conditional_float_compare_long(
            &conditional_float_compare_long)) {
        mir_emit_conditional_float_compare_long(
            out, &conditional_float_compare_long);
        return 1;
    }
    if (mir_match_conditional_bool(&conditional_bool)) {
        mir_emit_conditional_bool(out, &conditional_bool);
        return 1;
    }
    if (mir_match_logical_or_parameters(
            &logical_or_parameters)) {
        mir_emit_logical_or_parameters(
            out, &logical_or_parameters);
        return 1;
    }
    if (mir_match_cleared_record_append(
            &cleared_record_append)) {
        mir_emit_cleared_record_append(
            out, &cleared_record_append);
        return 1;
    }
    if (mir_match_record_name_search(&record_name_search)) {
        mir_emit_record_name_search(out, &record_name_search);
        return 1;
    }
    if (mir_match_sequential_unary_reports(
            &sequential_unary_reports)) {
        mir_emit_sequential_unary_reports(
            out, &sequential_unary_reports);
        return 1;
    }
    if (mir_match_nibble_append(&nibble_append)) {
        mir_emit_nibble_append(out, &nibble_append);
        return 1;
    }
    if (mir_match_volatile_fill_wide_constant(
            &volatile_fill_wide_constant)) {
        mir_emit_volatile_fill_wide_constant(
            out, &volatile_fill_wide_constant);
        return 1;
    }
    if (mir_match_single_signed_div_check(
            &single_signed_div_check)) {
        mir_emit_single_signed_div_check(
            out, &single_signed_div_check);
        return 1;
    }
    if (mir_match_affine_pointer_constant_return(&constant)) {
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
        fprintf(out, "\tld hl,%ld\n\tret\n", constant);
        return 1;
    }
    if (mir_match_local_constant_store_return(&constant)) {
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
    if (mir_match_global_byte_checks(
            &global_byte_checks)) {
        mir_emit_global_byte_checks(
            out, &global_byte_checks);
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
    if (mir_match_byte_bitwise_report(&byte_bitwise_report)) {
        mir_emit_byte_bitwise_report(out, &byte_bitwise_report);
        return 1;
    }
    if (mir_match_variadic_sum(&variadic_sum)) {
        mir_emit_variadic_sum(out, &variadic_sum);
        return 1;
    }
    if (mir_match_guarded_global_pop(&guarded_global_pop)) {
        mir_emit_guarded_global_pop(out, &guarded_global_pop);
        return 1;
    }
    if (mir_match_float_member_scalar_compare(
            &float_member_scalar_compare)) {
        mir_emit_float_member_scalar_compare(
            out, &float_member_scalar_compare);
        return 1;
    }
    if (mir_match_exact_float_mismatch_report(
            &exact_float_mismatch_report)) {
        mir_emit_exact_float_mismatch_report(
            out, &exact_float_mismatch_report);
        return 1;
    }
    if (mir_match_float_tolerance_report(
            &float_tolerance_report)) {
        mir_emit_float_tolerance_report(
            out, &float_tolerance_report);
        return 1;
    }
    if (mir_match_float_tolerance_failure(
            &float_tolerance_failure)) {
        mir_emit_float_tolerance_failure(
            out, &float_tolerance_failure);
        return 1;
    }
    if (mir_match_float_byte_report(&float_byte_report)) {
        mir_emit_float_byte_report(out, &float_byte_report);
        return 1;
    }
    if (mir_match_relative_tolerance_call(
            &relative_tolerance_call)) {
        mir_emit_relative_tolerance_call(
            out, &relative_tolerance_call);
        return 1;
    }
    if (mir_match_fixed_float_grid_fill(
            &fixed_float_grid_fill)) {
        mir_emit_fixed_float_grid_fill(
            out, &fixed_float_grid_fill);
        return 1;
    }
    if (mir_match_constant_float_conditional(
            &constant_float_conditional)) {
        mir_emit_constant_float_conditional(
            out, &constant_float_conditional);
        return 1;
    }
    if (mir_match_conditional_global_float_load(
            &conditional_global_float_load)) {
        mir_emit_conditional_global_float_load(
            out, &conditional_global_float_load);
        return 1;
    }
    if (mir_match_float_cosine_polynomial(
            &reduced_float_polynomial) ||
        mir_match_float_sine_polynomial(
            &reduced_float_polynomial)) {
        mir_emit_reduced_float_polynomial(
            out, &reduced_float_polynomial);
        return 1;
    }
    if (mir_match_float_tangent_rational(
            &float_tangent_rational)) {
        mir_emit_float_tangent_rational(
            out, &float_tangent_rational);
        return 1;
    }
    if (mir_match_recursive_wide_product(
            &recursive_wide_product)) {
        mir_emit_recursive_wide_product(
            out, &recursive_wide_product);
        return 1;
    }
    if (mir_match_recursive_wide_tree_sum(
            &recursive_wide_tree_sum) ||
        mir_match_recursive_word_member_tree_sum(
            &recursive_wide_tree_sum)) {
        mir_emit_recursive_wide_tree_sum(
            out, &recursive_wide_tree_sum);
        return 1;
    }
    if (mir_match_byte_rotate_flags(&byte_rotate_flags)) {
        mir_emit_byte_rotate_flags(out, &byte_rotate_flags);
        return 1;
    }
    if (mir_match_status_unpack(&status_unpack)) {
        mir_emit_status_unpack(out, &status_unpack);
        return 1;
    }
    if (mir_match_status_pack(&status_pack)) {
        mir_emit_status_pack(out, &status_pack);
        return 1;
    }
    if (mir_match_byte_math_flags(&byte_math_flags)) {
        mir_emit_byte_math_flags(out, &byte_math_flags);
        return 1;
    }
    if (mir_match_byte_range_union(&byte_range_union)) {
        mir_emit_byte_range_union(out, &byte_range_union);
        return 1;
    }
    if (mir_match_byte_array_sum(&byte_array_sum)) {
        mir_emit_byte_array_sum(out, &byte_array_sum);
        return 1;
    }
    if (mir_match_wraparound_bool_step(
            &wraparound_bool_step)) {
        mir_emit_wraparound_bool_step(
            out, &wraparound_bool_step);
        return 1;
    }
    if (mir_match_fixed_row_word_sum(&fixed_row_word_sum)) {
        mir_emit_fixed_row_word_sum(out, &fixed_row_word_sum);
        return 1;
    }
    if (mir_match_fixed_wide_zero(&fixed_wide_zero)) {
        mir_emit_fixed_wide_zero(out, &fixed_wide_zero);
        return 1;
    }
    if (mir_match_constant_byte_fill(&constant_byte_fill)) {
        mir_emit_constant_byte_fill(out, &constant_byte_fill);
        return 1;
    }
    if (mir_match_affine_byte_fill(&affine_byte_fill)) {
        mir_emit_affine_byte_fill(out, &affine_byte_fill);
        return 1;
    }
    if (mir_match_scaled_byte_fill(&affine_byte_fill)) {
        mir_emit_affine_byte_fill(out, &affine_byte_fill);
        return 1;
    }
    if (mir_match_wide_left_shift_count()) {
        mir_emit_wide_left_shift_count(out);
        return 1;
    }
    if (mir_match_palindrome_scan(&palindrome_scan)) {
        mir_emit_palindrome_scan(out, &palindrome_scan);
        return 1;
    }
    if (mir_match_dynamic_row_scan(&dynamic_row_scan)) {
        mir_emit_dynamic_row_scan(out, &dynamic_row_scan);
        return 1;
    }
    if (mir_match_byte_mismatch_scan(
            &byte_mismatch_scan)) {
        mir_emit_byte_mismatch_scan(
            out, &byte_mismatch_scan);
        return 1;
    }
    if (mir_match_variable_byte_step_sum(
            &variable_byte_step_sum) ||
        mir_match_alias_byte_step_sum(
            &variable_byte_step_sum)) {
        mir_emit_variable_byte_step_sum(
            out, &variable_byte_step_sum);
        return 1;
    }
    if (mir_match_fixed_reverse_word_copy(
            &fixed_reverse_word_copy)) {
        mir_emit_fixed_reverse_word_copy(
            out, &fixed_reverse_word_copy);
        return 1;
    }
    if (mir_match_fixed_random_word_fill(
            &fixed_random_word_fill)) {
        mir_emit_fixed_random_word_fill(
            out, &fixed_random_word_fill);
        return 1;
    }
    if (mir_match_global_byte_copy_state(
            &global_byte_copy_state)) {
        mir_emit_global_byte_copy_state(
            out, &global_byte_copy_state);
        return 1;
    }
    if (mir_match_fixed_global_stride_call(
            &fixed_global_stride_call)) {
        mir_emit_fixed_global_stride_call(
            out, &fixed_global_stride_call);
        return 1;
    }
    if (mir_match_constant_loop_check(&constant_loop_check)) {
        mir_emit_constant_loop_check(out, &constant_loop_check);
        return 1;
    }
    if (mir_match_global_byte_countdown(
            &global_byte_countdown)) {
        mir_emit_global_byte_countdown(
            out, &global_byte_countdown);
        return 1;
    }
    if (mir_match_conditional_string_report(
            &conditional_string_report)) {
        mir_emit_conditional_string_report(
            out, &conditional_string_report);
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
    if (mir_match_constant_flow_result_switch(
            &constant_result_switch)) {
        mir_emit_constant_result_switch(
            out, &constant_result_switch);
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
