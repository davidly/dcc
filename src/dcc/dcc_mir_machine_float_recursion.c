/**
 * @file dcc_mir_machine_float_recursion.c
 * @brief Emits exact floating-point, recursive, and byte-operation kernels.
 *
 * @par Role
 * Matches compact numeric reports, floating-point comparisons and polynomial
 * schedules, recursive frame/product/tree kernels, and byte status operations.
 * It preserves the numeric-family pass-through at its selector position.
 *
 * @par Key entry point
 * mir_try_emit_float_recursion_kernels().
 */

#include "dcc_mir_machine_internal.h"

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

/* Plan structs moved verbatim from dcc_mir_machine_emit.c. */

struct MirPackedBitDecode { int parameter_stack_offset; };

struct MirSortSearchReport { struct Sym *print_function; int format_id; };

/* Enum tag paired with the plan structs above (also moved
 * verbatim from dcc_mir_machine_emit.c). */

enum MirMachineFormKind {
    MIR_MACHINE_FORM_INTEGER = 1,
    MIR_MACHINE_FORM_POINTER = 2
};

/* The following struct/macro definitions are shared plan
 * types used by helper functions in this file; moved here
 * verbatim from dcc_mir_machine_emit.c during the family
 * split. */

struct MirMachineForm {
    int kind;
    long value;
    int storage;
    int offset;
    int pointer_terms;
    char name[64];
};

struct MirPointerWordSumUntilZero {
    struct Sym *value;
    int pointer_stack_offset;
    int count_stack_offset;
};

struct MirPrefixUpdateChecks {
    struct Sym *integer_function, *long_function, *print_function;
    int count, string_ids[25], values[25], long_string, final_string;
    unsigned long long_value;
};

struct MirManyWideChecks {
    struct Sym *check_function, *print_function;
    int count, string_ids[32], final_string;
    unsigned long values[32];
};

struct MirManyByte4Checks {
    struct Sym *check_function, *print_function;
    int count, string_ids[32], bytes[32][4], start_string, final_string;
};

struct MirPointerDifferenceMain {
    struct Sym *length_function, *distance_function, *print_function;
    int failure_strings[2], summary_strings[2];
};

struct MirConstexprWideChecks {
    struct Sym *function;
    int strings[5];
    unsigned long values[5];
};

struct MirLeafConstructor {
    int seed_stack_offset, value_offset, byte_offset, long_offset;
    int words_offset, bytes_offset, longs_offset;
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

struct MirFloatAtanPolynomial {
    int parameter_stack_offset;
    unsigned long zero_bits;
    unsigned long one_bits;
    unsigned long large_bound_bits;
    unsigned long small_bound_bits;
    unsigned long half_pi_bits;
    unsigned long quarter_pi_bits;
    unsigned long coefficients[4];
};

struct MirFloatTaylorSine {
    struct Sym *remainder_function;
    struct Sym *power_function;
    struct Sym *factorial_function;
    struct Sym *iteration_bound;
    int parameter_stack_offset;
    unsigned long zero_bits;
    unsigned long one_bits;
    unsigned long two_pi_bits;
    unsigned long pi_bits;
    unsigned long half_pi_bits;
};

struct MirRecursiveWideProduct {
    struct Sym *function;
    int parameter_stack_offset;
    int operation;
    int base_result;
};

struct MirRecursiveFrameFill {
    struct Sym *function;
    struct Sym *sink;
    int parameter_frame_offset;
    int array_offset;
    int count;
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

static void mir_emit_leaf_dest(MirStream *out,int off)
{
    mir_stream_puts("\tld l,(ix+4)\n\tld h,(ix+5)\n",out);
    mir_machine_emit_hl_offset(out,off,0);
}

static void mir_emit_push_byte_in_a(
    MirStream *out, int is_unsigned)
{
    mir_stream_puts("\tld l,a\n", out);
    if (is_unsigned)
        mir_stream_puts("\tld h,0\n", out);
    else
        mir_stream_puts("\trlca\n\tsbc a,a\n\tld h,a\n", out);
    mir_stream_puts("\tpush hl\n", out);
}

static void mir_emit_push_byte_binary(
    MirStream *out, const struct MirByteBitwiseReport *plan,
    int pushed_words, int operation)
{
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld c,(hl)\n"
            "\tld hl,%d\n\tadd hl,sp\n\tld a,(hl)\n",
            plan->left_stack_offset + pushed_words * 2,
            plan->right_stack_offset + pushed_words * 2);
    if (operation == '&')
        mir_stream_puts("\tand c\n", out);
    else if (operation == '|')
        mir_stream_puts("\tor c\n", out);
    else
        mir_stream_puts("\txor c\n", out);
    mir_emit_push_byte_in_a(out, plan->is_unsigned);
}

static void mir_emit_float_member_operand(
    MirStream *out, const struct MirFloatMemberScalarCompare *plan,
    int pushed_bytes)
{
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n",
            plan->pointer_stack_offset + pushed_bytes);
    mir_machine_emit_hl_offset(out, plan->member_offset, 0);
    mir_stream_puts("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
          "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld l,c\n\tld h,b\n", out);
}

static void mir_emit_float_scalar_operand(
    MirStream *out, const struct MirFloatMemberScalarCompare *plan,
    int pushed_bytes)
{
    if (plan->scalar_width == 2) {
        mir_stream_printf(out,
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

static void mir_emit_constant_float_arm(
    MirStream *out, int is_float, int integer_width,
    unsigned long bits)
{
    if (is_float) {
        mir_stream_printf(out,
                "\tld hl,%lu\n\tld de,%lu\n",
                bits & 0xffffUL, (bits >> 16) & 0xffffUL);
    } else if (integer_width == 2) {
        mir_stream_printf(out, "\tld hl,%lu\n", bits & 0xffffUL);
        mir_emit_runtime_call(out, "__fif");
    } else {
        mir_stream_printf(out,
                "\tld hl,%lu\n\tld de,%lu\n",
                bits & 0xffffUL, (bits >> 16) & 0xffffUL);
        mir_emit_runtime_call(out, "__flf");
    }
    mir_emit_runtime_call(out, "__ffl");
    mir_stream_puts("\tret\n", out);
}

static void mir_emit_global_float_to_long_return(
    MirStream *out, const struct MirConditionalGlobalFloatLoad *plan,
    int offset)
{
    mir_machine_emit_global_address_de(out, plan->root, offset);
    mir_stream_puts("\tex de,hl\n"
          "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
          "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld l,c\n\tld h,b\n", out);
    mir_emit_runtime_call(out, "__ffl");
    mir_stream_puts("\tret\n", out);
}

static void mir_emit_recursive_tree_pointer(
    MirStream *out, const struct MirRecursiveWideTreeSum *plan,
    int pushed_bytes)
{
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n",
            plan->parameter_stack_offset + pushed_bytes);
}

static void mir_emit_recursive_tree_value(
    MirStream *out, const struct MirRecursiveWideTreeSum *plan,
    int pushed_bytes)
{
    mir_emit_recursive_tree_pointer(out, plan, pushed_bytes);
    mir_machine_emit_hl_offset(out, plan->value_offset, 0);
    if (plan->value_width == 4) {
        mir_stream_puts("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
              "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
              "\tld l,c\n\tld h,b\n", out);
    } else {
        mir_stream_puts("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
              "\tld h,b\n\tld l,c\n", out);
        if (plan->value_is_unsigned)
            mir_stream_puts("\tld de,0\n", out);
        else
            mir_stream_puts("\tld a,b\n\trlca\n\tsbc a,a\n"
                  "\tld d,a\n\tld e,a\n", out);
    }
}

static int mir_match_prefix_update_checks(struct MirPrefixUpdateChecks *p)
{
    int i;
    memset(p,0,sizeof(*p));
    if(mir.count!=425||mir_cfg_block_count()!=2||mir.has_vla||
       mir.insns[414].opcode!=MIR_CALL||mir.insns[422].opcode!=MIR_CALL||
       mir.insns[424].opcode!=MIR_RETURN)return 0;
    for(i=0;i<mir.count;++i){const struct MirInsn*c=&mir.insns[i];int a[3];
        const struct MirInsn*s;struct MirMachineForm e;struct Sym*f;
        if(c->opcode!=MIR_CALL||!mir_machine_three_call_arguments(c,a))continue;
        s=mir_definition(a[0]);if(!s||s->opcode!=MIR_STRING_ADDRESS)continue;
        f=find_global(c->name);
        if(i==414){if(!mir_machine_pointer_form(a[2],i,&e,0))return 0;
            p->long_function=f;p->long_string=(int)s->immediate;p->long_value=(unsigned long)e.value;continue;}
        if(p->count>=25||!mir_machine_pointer_form(a[2],i,&e,0))return 0;
        if(!p->integer_function)p->integer_function=f;else if(p->integer_function!=f)return 0;
        p->string_ids[p->count]=(int)s->immediate;p->values[p->count]=(int)(e.value&0xffffL);++p->count;}
    if(p->count!=25||!p->integer_function||!p->long_function)return 0;
    for(i=0;i<mir.count;++i){const struct MirInsn*a=&mir.insns[i],*d;
        if(a->opcode!=MIR_ARG||a->secondary_offset!=mir.insns[422].secondary_offset)continue;
        d=mir_definition(a->src1);if(d&&d->opcode==MIR_STRING_ADDRESS)p->final_string=(int)d->immediate;}
    p->print_function=find_global(mir.insns[422].name);return p->print_function!=NULL;
}

static void mir_emit_prefix_update_checks(MirStream *out,const struct MirPrefixUpdateChecks*p)
{
    int i;if(opt_stack_check)mir_emit_runtime_call(out,"__stchk");
    for(i=0;i<p->count;++i){mir_stream_printf(out,"\tld hl,%d\n\tpush hl\n\tpush hl\n"
        "\tld hl,S%d\n\tpush hl\n",p->values[i],p->string_ids[i]);
        mir_machine_emit_symbol_call(out,p->integer_function);mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n",out);}
    mir_emit_fixed_point_constant(out,p->long_value);mir_emit_fixed_point_constant(out,p->long_value);
    mir_stream_printf(out,"\tld hl,S%d\n\tpush hl\n",p->long_string);mir_machine_emit_symbol_call(out,p->long_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n",out);
    mir_stream_printf(out,"\tld hl,S%d\n\tpush hl\n",p->final_string);mir_machine_emit_symbol_call(out,p->print_function);
    mir_stream_puts("\tpop bc\n\tld hl,0\n\tret\n",out);
}

static int mir_match_many_wide_checks(struct MirManyWideChecks *p)
{
    int i;
    memset(p,0,sizeof(*p));
    if(mir.count!=285||mir_cfg_block_count()!=2||mir.has_vla)return 0;
    for(i=0;i<mir.count;++i){const struct MirInsn*c=&mir.insns[i];int a[3];
        const struct MirInsn*s;struct MirMachineForm e;struct Sym*f;
        if(c->opcode!=MIR_CALL||!mir_machine_three_call_arguments(c,a))continue;
        s=mir_definition(a[0]);if(!s||s->opcode!=MIR_STRING_ADDRESS)continue;
        if(!mir_machine_pointer_form(a[2],i,&e,0)||e.kind!=MIR_MACHINE_FORM_INTEGER)continue;
        f=find_global(c->name);if(!p->check_function)p->check_function=f;else if(p->check_function!=f)continue;
        if(p->count>=32)
            return 0;
        p->string_ids[p->count]=(int)s->immediate;
        p->values[p->count]=(unsigned long)e.value;++p->count;}
    if(p->count!=22||!p->check_function)return 0;
    for(i=mir.count-1;i>=0;--i)if(mir.insns[i].opcode==MIR_CALL){
        int j;if(find_global(mir.insns[i].name)==p->check_function)continue;
        p->print_function=find_global(mir.insns[i].name);
        for(j=0;j<mir.count;++j){const struct MirInsn*a=&mir.insns[j],*d;
            if(a->opcode!=MIR_ARG||a->secondary_offset!=mir.insns[i].secondary_offset)continue;
            d=mir_definition(a->src1);if(d&&d->opcode==MIR_STRING_ADDRESS)p->final_string=(int)d->immediate;}break;}
    return p->print_function!=NULL;
}

static void mir_emit_many_wide_checks(MirStream *out,const struct MirManyWideChecks*p)
{
    int i;if(opt_stack_check)mir_emit_runtime_call(out,"__stchk");
    for(i=0;i<p->count;++i){mir_emit_fixed_point_constant(out,p->values[i]);
        mir_emit_fixed_point_constant(out,p->values[i]);mir_stream_printf(out,"\tld hl,S%d\n\tpush hl\n",p->string_ids[i]);
        mir_machine_emit_symbol_call(out,p->check_function);mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n",out);}
    mir_stream_printf(out,"\tld hl,S%d\n\tpush hl\n",p->final_string);mir_machine_emit_symbol_call(out,p->print_function);
    mir_stream_puts("\tpop bc\n\tld hl,0\n\tret\n",out);
}

static int mir_match_many_byte4_checks(struct MirManyByte4Checks *p)
{
    int i, prints = 0;
    memset(p,0,sizeof(*p));
    if(mir.count!=619||mir_cfg_block_count()!=2||mir.has_vla)
        return mir_machine_reject("many-byte4-checks","shape");
    for(i=0;i<mir.count;++i){const struct MirInsn*c=&mir.insns[i];int a[6],b;
        const struct MirInsn*s;struct MirMachineForm f;struct Sym*fn;
        if(c->opcode!=MIR_CALL||!mir_machine_six_call_arguments(c,a))continue;
        s=mir_definition(a[0]);if(!s||s->opcode!=MIR_STRING_ADDRESS)continue;
        fn=find_global(c->name);if(!p->check_function)p->check_function=fn;else if(p->check_function!=fn)return 0;
        if(p->count>=32)
            return 0;
        p->string_ids[p->count]=(int)s->immediate;
        for(b=0;b<4;++b){if(!mir_machine_pointer_form(a[b+2],i,&f,0)||f.kind!=MIR_MACHINE_FORM_INTEGER)return 0;
            p->bytes[p->count][b]=(int)(f.value&255);}
        ++p->count;}
    if(p->count!=25||!p->check_function)
        return mir_machine_reject("many-byte4-checks","count");
    for(i=0;i<mir.count;++i){const struct MirInsn*c=&mir.insns[i],*s;int j;
        if(c->opcode!=MIR_CALL||find_global(c->name)==p->check_function)continue;
        {
        int found_string=0;
        for(j=0;j<mir.count;++j){const struct MirInsn*a=&mir.insns[j];
            if(a->opcode!=MIR_ARG||a->secondary_offset!=c->secondary_offset)continue;
            s=mir_definition(a->src1);if(s&&s->opcode==MIR_STRING_ADDRESS){
                if(prints==0)p->start_string=(int)s->immediate;else p->final_string=(int)s->immediate;
                found_string=1;}}
        if(found_string){++prints;p->print_function=find_global(c->name);}
        }}
    if(p->print_function==NULL||prints!=3)
        return mir_machine_reject("many-byte4-checks","prints");
    return 1;
}

static void mir_emit_many_byte4_checks(MirStream *out,const struct MirManyByte4Checks*p)
{
    int i,b;
    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n\tld hl,-4\n\tadd hl,sp\n\tld sp,hl\n",out);
    if(opt_stack_check)mir_emit_runtime_call(out,"__stchk");
    mir_stream_printf(out,"\tld hl,S%d\n\tpush hl\n",p->start_string);mir_machine_emit_symbol_call(out,p->print_function);mir_stream_puts("\tpop bc\n",out);
    for(i=0;i<p->count;++i){for(b=0;b<4;++b)mir_stream_printf(out,"\tld (ix%+d),%d\n",-4+b,p->bytes[i][b]);
        for(b=3;b>=0;--b)mir_stream_printf(out,"\tld hl,%d\n\tpush hl\n",p->bytes[i][b]);
        mir_emit_local_address(out,-4);mir_stream_puts("\tpush hl\n",out);mir_stream_printf(out,"\tld hl,S%d\n\tpush hl\n",p->string_ids[i]);
        mir_machine_emit_symbol_call(out,p->check_function);for(b=0;b<6;++b)mir_stream_puts("\tpop bc\n",out);}
    mir_stream_printf(out,"\tld hl,S%d\n\tpush hl\n",p->final_string);mir_machine_emit_symbol_call(out,p->print_function);
    mir_stream_puts("\tpop bc\n\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n",out);
}

static int mir_match_pointer_difference_main(struct MirPointerDifferenceMain *p)
{
    memset(p,0,sizeof(*p));
    if(mir.count!=90||mir_cfg_block_count()!=4||mir.has_vla||
       mir.insns[24].opcode!=MIR_CALL||mir.insns[33].opcode!=MIR_CALL||
       mir.insns[51].opcode!=MIR_CALL||mir.insns[65].opcode!=MIR_CALL||
       mir.insns[36].opcode!=MIR_CALL||mir.insns[67].opcode!=MIR_CALL||
       mir.insns[80].opcode!=MIR_CALL||mir.insns[87].opcode!=MIR_CALL)return 0;
    p->length_function=find_global(mir.insns[24].name);
    p->distance_function=find_global(mir.insns[51].name);
    p->print_function=find_global(mir.insns[36].name);
    p->failure_strings[0]=(int)mir.insns[29].immediate;
    p->failure_strings[1]=(int)mir.insns[55].immediate;
    p->summary_strings[0]=(int)mir.insns[76].immediate;
    p->summary_strings[1]=(int)mir.insns[85].immediate;
    return p->length_function&&p->distance_function&&p->print_function;
}

static void mir_emit_pointer_difference_main(MirStream *out,
    const struct MirPointerDifferenceMain*p)
{
    int length_ok=new_label(),distance_ok=new_label(),success=new_label(),done=new_label();
    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n\tld hl,-30\n\tadd hl,sp\n\tld sp,hl\n",out);
    if(opt_stack_check)mir_emit_runtime_call(out,"__stchk");
    mir_stream_puts("\tld (ix-30),0\n\tld (ix-29),0\n"
          "\tld (ix-8),97\n\tld (ix-7),0\n\tld (ix-6),98\n\tld (ix-5),0\n"
          "\tld (ix-4),0\n\tld (ix-3),0\n",out);
    mir_emit_local_address(out,-8);mir_stream_puts("\tpush hl\n",out);
    mir_machine_emit_symbol_call(out,p->length_function);mir_stream_puts("\tpop bc\n\tld de,2\n\tor a\n\tsbc hl,de\n",out);
    mir_stream_printf(out,"\tjp z,L%d\n",length_ok);
    mir_emit_local_address(out,-8);mir_stream_puts("\tpush hl\n",out);mir_machine_emit_symbol_call(out,p->length_function);
    mir_stream_puts("\tpop bc\n\tpush hl\n",out);mir_stream_printf(out,"\tld hl,S%d\n\tpush hl\n",p->failure_strings[0]);
    mir_machine_emit_symbol_call(out,p->print_function);mir_stream_puts("\tpop bc\n\tpop bc\n\tinc (ix-30)\n",out);
    mir_stream_printf(out,"L%d:\n",length_ok);
    mir_emit_local_address(out,-12);mir_stream_puts("\tpush hl\n",out);mir_emit_local_address(out,-28);mir_stream_puts("\tpush hl\n",out);
    mir_machine_emit_symbol_call(out,p->distance_function);mir_stream_puts("\tpop bc\n\tpop bc\n\tld de,4\n\tor a\n\tsbc hl,de\n",out);
    mir_stream_printf(out,"\tjp z,L%d\n",distance_ok);
    mir_emit_local_address(out,-12);mir_stream_puts("\tpush hl\n",out);mir_emit_local_address(out,-28);mir_stream_puts("\tpush hl\n",out);
    mir_machine_emit_symbol_call(out,p->distance_function);mir_stream_puts("\tpop bc\n\tpop bc\n\tpush hl\n",out);
    mir_stream_printf(out,"\tld hl,S%d\n\tpush hl\n",p->failure_strings[1]);mir_machine_emit_symbol_call(out,p->print_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tinc (ix-30)\n",out);
    mir_stream_printf(out,"L%d:\n\tld a,(ix-30)\n\tor a\n\tjp z,L%d\n",distance_ok,success);
    mir_stream_puts("\tld l,(ix-30)\n\tld h,0\n\tpush hl\n",out);mir_stream_printf(out,"\tld hl,S%d\n\tpush hl\n",p->summary_strings[0]);
    mir_machine_emit_symbol_call(out,p->print_function);mir_stream_puts("\tpop bc\n\tpop bc\n\tld hl,1\n",out);mir_stream_printf(out,"\tjp L%d\nL%d:\n",done,success);
    mir_stream_printf(out,"\tld hl,S%d\n\tpush hl\n",p->summary_strings[1]);mir_machine_emit_symbol_call(out,p->print_function);
    mir_stream_puts("\tpop bc\n\tld hl,0\n",out);mir_stream_printf(out,"L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",done);
}

static int mir_match_constexpr_wide_checks(struct MirConstexprWideChecks *p)
{
    static const int calls[5]={7,35,51,102,109};
    int i;memset(p,0,sizeof(*p));
    if(mir.count!=110||mir_cfg_block_count()!=4||mir.has_vla)return 0;
    for(i=0;i<5;++i){int a[3];const struct MirInsn*s;struct MirMachineForm e;
        if(!mir_machine_three_call_arguments(&mir.insns[calls[i]],a)||
           (s=mir_definition(a[0]))==NULL||s->opcode!=MIR_STRING_ADDRESS||
           !mir_machine_pointer_form(a[2],calls[i],&e,0))return 0;
        p->strings[i]=(int)s->immediate;p->values[i]=(unsigned long)e.value;}
    p->function=find_global(mir.insns[7].name);return p->function!=NULL;
}

static void mir_emit_constexpr_wide_checks(MirStream *out,const struct MirConstexprWideChecks*p)
{
    int i;if(opt_stack_check)mir_emit_runtime_call(out,"__stchk");
    for(i=0;i<5;++i){mir_emit_fixed_point_constant(out,p->values[i]);mir_emit_fixed_point_constant(out,p->values[i]);
        mir_stream_printf(out,"\tld hl,S%d\n\tpush hl\n",p->strings[i]);mir_machine_emit_symbol_call(out,p->function);
        mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n",out);}
    mir_stream_puts("\tret\n",out);
}

static int mir_match_packed_bit_decode(struct MirPackedBitDecode *p)
{
    int mt,ms,mo;
    memset(p,0,sizeof(*p));
    if(mir.count!=25||mir_cfg_block_count()!=5||mir.has_vla||
       type_size(mir.return_type)!=4||mir.insns[1].opcode!=MIR_PARAM||
       mir.insns[3].bit_mask!=15||mir.insns[7].bit_mask!=112||
       mir.insns[9].immediate!=TOK_SHL||mir.insns[12].bit_mask!=128||
       mir.insns[16].immediate!='-'||mir.insns[24].src1!=mir.insns[23].dst||
       !mir_scalar_memory_location(&mir.insns[1],&mt,&ms,&mo)||ms!=SC_PARAM)
        return 0;
    p->parameter_stack_offset=mo-2;return 1;
}

static void mir_emit_packed_bit_decode(MirStream *out,const struct MirPackedBitDecode*p)
{
    int shift=new_label(),shift_done=new_label(),positive=new_label();
    if(opt_stack_check)mir_emit_runtime_call(out,"__stchk");
    mir_stream_printf(out,"\tld hl,%d\n\tadd hl,sp\n\tld a,(hl)\n\tld c,a\n"
                "\tand 15\n\tld l,a\n\tld h,0\n\tld a,c\n"
                "\trrca\n\trrca\n\trrca\n\trrca\n\tand 7\n\tld b,a\n"
                "\tld a,b\n\tor a\n\tjp z,L%d\nL%d:\n\tadd hl,hl\n\tdjnz L%d\n"
                "L%d:\n\tbit 7,c\n\tjp z,L%d\n"
                "\txor a\n\tsub l\n\tld l,a\n\tsbc a,a\n\tsub h\n\tld h,a\n"
                "\tld de,65535\n\tret\nL%d:\n\tld de,0\n\tret\n",
            p->parameter_stack_offset,shift_done,shift,shift,shift_done,positive,positive);
}

static int mir_match_sort_search_report(struct MirSortSearchReport *p)
{
    int i,found=0;
    memset(p,0,sizeof(*p));
    if(mir.count!=95||mir_cfg_block_count()!=4||mir.has_vla||
       mir.insns[53].opcode!=MIR_CALL||mir.insns[67].opcode!=MIR_CALL||
       mir.insns[86].opcode!=MIR_CALL||mir.insns[91].opcode!=MIR_CALL||
       !mir_machine_constant_equals(mir.insns[93].dst,0)||
       mir.insns[94].src1!=mir.insns[93].dst)return 0;
    for(i=0;i<mir.count;++i){const struct MirInsn*a=&mir.insns[i],*d;
        if(a->opcode!=MIR_ARG||a->secondary_offset!=mir.insns[86].secondary_offset)continue;
        d=mir_definition(a->src1);if(d&&d->opcode==MIR_STRING_ADDRESS){p->format_id=(int)d->immediate;++found;}}
    p->print_function=find_global(mir.insns[86].name);
    return found==1&&p->print_function!=NULL;
}

static void mir_emit_sort_search_report(MirStream *out,const struct MirSortSearchReport*p)
{
    if(opt_stack_check)mir_emit_runtime_call(out,"__stchk");
    mir_stream_puts("\tld hl,5\n\tpush hl\n\tld hl,13\n\tpush hl\n",out);
    mir_stream_printf(out,"\tld hl,S%d\n\tpush hl\n",p->format_id);
    mir_machine_emit_symbol_call(out,p->print_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tld hl,0\n\tret\n",out);
}

static int mir_match_leaf_constructor(struct MirLeafConstructor *p)
{
    memset(p,0,sizeof(*p));
    if(mir.count!=74||mir_cfg_block_count()!=4||mir.has_vla||
       mir.insns[1].opcode!=MIR_PARAM||!mir_machine_constant_equals(mir.insns[9].dst,1)||
       !mir_machine_constant_equals(mir.insns[17].dst,1000)||
       !mir_machine_constant_equals(mir.insns[21].dst,0)||
       !mir_machine_constant_equals(mir.insns[27].dst,4)||
       !mir_machine_constant_equals(mir.insns[45].dst,10)||
       !mir_machine_constant_equals(mir.insns[58].dst,1000)||
       mir.insns[73].src1!=mir.insns[72].dst||
       !mir_machine_parameter_value_offset(mir.insns[1].dst,&p->seed_stack_offset))return 0;
    p->value_offset=(int)mir.insns[3].immediate;p->byte_offset=(int)mir.insns[7].immediate;
    p->long_offset=(int)mir.insns[14].immediate;p->words_offset=(int)mir.insns[32].immediate;
    p->bytes_offset=(int)mir.insns[41].immediate;p->longs_offset=(int)mir.insns[53].immediate;
    return 1;
}

static void mir_emit_leaf_constructor(MirStream *out,const struct MirLeafConstructor*p)
{
    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n\tld hl,-4\n\tadd hl,sp\n\tld sp,hl\n",out);
    if(opt_stack_check)mir_emit_runtime_call(out,"__stchk");
    mir_stream_printf(out,"\tld c,(ix+%d)\n\tld b,(ix+%d)\n\tld hl,1000\n",
            p->seed_stack_offset+2,p->seed_stack_offset+3);
    mir_emit_runtime_call(out,"__m1s");
    mir_stream_puts("\tld (ix-4),l\n\tld (ix-3),h\n\tld (ix-2),e\n\tld (ix-1),d\n",out);
    mir_emit_leaf_dest(out,p->value_offset);mir_stream_printf(out,"\tld e,(ix+%d)\n\tld d,(ix+%d)\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n",
            p->seed_stack_offset+2,p->seed_stack_offset+3);
    mir_emit_leaf_dest(out,p->byte_offset);mir_stream_printf(out,"\tld a,(ix+%d)\n\tinc a\n\tld (hl),a\n",p->seed_stack_offset+2);
    mir_emit_leaf_dest(out,p->long_offset);mir_stream_puts("\tld e,(ix-4)\n\tld d,(ix-3)\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tinc hl\n\tld e,(ix-2)\n\tld d,(ix-1)\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n",out);
    {
        int words=new_label(),bytes=new_label(),longs=new_label();
        mir_emit_leaf_dest(out,p->words_offset);
        mir_stream_printf(out,"\tld e,(ix+%d)\n\tld d,(ix+%d)\n\tld b,4\nL%d:\n"
                    "\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tinc hl\n"
                    "\tinc de\n\tdjnz L%d\n",
                p->seed_stack_offset+2,p->seed_stack_offset+3,words,words);
        mir_emit_leaf_dest(out,p->bytes_offset);
        mir_stream_printf(out,"\tld a,(ix+%d)\n\tadd a,10\n\tld b,4\nL%d:\n"
                    "\tld (hl),a\n\tinc hl\n\tinc a\n\tdjnz L%d\n",
                p->seed_stack_offset+2,bytes,bytes);
        mir_emit_leaf_dest(out,p->longs_offset);
        mir_stream_printf(out,"\tld c,(ix-4)\n\tld b,(ix-3)\n"
                    "\tld e,(ix-2)\n\tld d,(ix-1)\n\tld a,4\nL%d:\n"
                    "\tld (hl),c\n\tinc hl\n\tld (hl),b\n\tinc hl\n"
                    "\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tinc hl\n"
                    "\tinc bc\n\tdec a\n\tjp nz,L%d\n",longs,longs);
    }
    mir_stream_puts("\tld sp,ix\n\tpop ix\n\tret\n",out);
}

static int mir_match_pointer_word_sum_until_zero(
    struct MirPointerWordSumUntilZero *plan)
{
    static const int global_loads[6] = { 3, 4, 6, 51, 53, 55 };
    int type, storage, offset;
    int load;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 58 || mir_cfg_block_count() != 5 ||
        mir.has_vla || type_size(mir.return_type) != 2 ||
        mir.insns[1].opcode != MIR_PARAM ||
        mir.insns[2].opcode != MIR_PARAM ||
        mir.insns[5].immediate != '+' ||
        mir.insns[7].immediate != '+' ||
        !mir_machine_constant_equals(mir.insns[11].dst, 0) ||
        mir.insns[16].opcode != MIR_PHI ||
        mir.insns[16].src1 != mir.insns[7].dst ||
        mir.insns[16].src2 != mir.insns[39].dst ||
        mir.insns[17].opcode != MIR_PHI ||
        mir.insns[17].src1 != mir.insns[11].dst ||
        mir.insns[17].src2 != mir.insns[46].dst ||
        mir.insns[22].immediate != '<' ||
        mir.insns[22].src1 != mir.insns[20].dst ||
        mir.insns[22].src2 != mir.insns[21].dst ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[23].label != mir.insns[49].label)
        return mir_machine_reject("pointer-word-sum-zero", "shape");
    for (load = 1; load < 6; ++load)
        if (!mir_machine_same_location(
                &mir.insns[global_loads[0]],
                &mir.insns[global_loads[load]]))
            return mir_machine_reject(
                "pointer-word-sum-zero", "globals");
    plan->value = find_global(mir.insns[3].name);
    if (plan->value == NULL || plan->value->is_volatile ||
        mir.insns[26].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[26].src1 != mir.insns[1].dst ||
        mir.insns[26].src2 != mir.insns[17].dst ||
        mir.insns[26].immediate != 2 ||
        mir.insns[27].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[27].src1 != mir.insns[26].dst ||
        !mir_machine_constant_equals(mir.insns[28].dst, 0) ||
        mir.insns[29].immediate != TOK_EQ ||
        mir.insns[30].src1 != mir.insns[29].dst ||
        mir.insns[30].label != mir.insns[33].label ||
        mir.insns[32].src1 != mir.insns[16].dst)
        return mir_machine_reject("pointer-word-sum-zero", "early");
    if (
        mir.insns[37].opcode != MIR_INDEX_ADDRESS ||
        mir.insns[37].src1 != mir.insns[1].dst ||
        mir.insns[37].src2 != mir.insns[17].dst ||
        mir.insns[37].immediate != 2 ||
        mir.insns[38].opcode != MIR_LOAD_INDIRECT ||
        mir.insns[38].src1 != mir.insns[37].dst ||
        mir.insns[39].immediate != '+' ||
        mir.insns[39].src1 != mir.insns[16].dst ||
        mir.insns[39].src2 != mir.insns[38].dst ||
        !mir_machine_constant_equals(mir.insns[45].dst, 1) ||
        mir.insns[46].immediate != '+' ||
        mir.insns[48].label != mir.insns[13].label)
        return mir_machine_reject("pointer-word-sum-zero", "body");
    if (
        mir.insns[52].immediate != '+' ||
        mir.insns[54].immediate != '+' ||
        mir.insns[56].immediate != '+' ||
        mir.insns[57].src1 != mir.insns[56].dst)
        return mir_machine_reject("pointer-word-sum-zero", "flow");
    if (!mir_scalar_memory_location(
            &mir.insns[1], &type, &storage, &offset) ||
        storage != SC_PARAM || offset < 2)
        return mir_machine_reject("pointer-word-sum-zero", "pointer");
    plan->pointer_stack_offset = offset - 2;
    if (!mir_scalar_memory_location(
            &mir.insns[2], &type, &storage, &offset) ||
        storage != SC_PARAM || offset < 2)
        return mir_machine_reject("pointer-word-sum-zero", "count");
    plan->count_stack_offset = offset - 2;
    return 1;
}

static void mir_emit_pointer_word_sum_until_zero(
    MirStream *out, const struct MirPointerWordSumUntilZero *plan)
{
    int loop = new_label();
    int nonzero = new_label();
    int early = new_label();
    int done = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_word(out, plan->value, 0);
    mir_stream_puts("\tld d,h\n\tld e,l\n\tadd hl,hl\n\tadd hl,de\n"
          "\tex de,hl\n", out);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tld h,b\n\tld l,c\n"
            "\tpush hl\n"
            "\tld hl,%d\n\tadd hl,sp\n\tld b,(hl)\n"
            "\tpop hl\n\tld a,b\n\tor a\n\tjp z,L%d\n"
            "L%d:\n\tld c,(hl)\n\tinc hl\n\tld a,c\n\tor a\n"
            "\tjp nz,L%d\n\tld a,(hl)\n\tor a\n\tjp z,L%d\n"
            "L%d:\n\tld a,(hl)\n\tinc hl\n\tpush hl\n"
            "\tld l,c\n\tld h,a\n\tadd hl,de\n\tex de,hl\n"
            "\tpop hl\n\tdjnz L%d\n",
            plan->pointer_stack_offset,
            plan->count_stack_offset + 2,
            done, loop, nonzero, early,
            nonzero, loop);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n\tex de,hl\n\tret\nL%d:\n",
            done, early, done);
    mir_machine_emit_global_word(out, plan->value, 0);
    mir_stream_puts("\tld b,h\n\tld c,l\n\tadd hl,hl\n\tadd hl,bc\n"
          "\tadd hl,de\n\tret\n", out);
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

static void mir_emit_byte_bitwise_report(
    MirStream *out, const struct MirByteBitwiseReport *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld a,(hl)\n\tcpl\n",
            plan->right_stack_offset);
    mir_emit_push_byte_in_a(out, plan->is_unsigned);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld a,(hl)\n\tcpl\n",
            plan->left_stack_offset + 2);
    mir_emit_push_byte_in_a(out, plan->is_unsigned);
    mir_emit_push_byte_binary(out, plan, 2, '^');
    mir_emit_push_byte_binary(out, plan, 3, '|');
    mir_emit_push_byte_binary(out, plan, 4, '&');
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", plan->string_id);
    if ((plan->runtime_flags & MIR_CALL_FLAG_FORMAT_HEX) != 0)
        mir_emit_runtime_call(out, "__pfehx");
    mir_emit_runtime_call(out, plan->call_name);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tpop bc\n\tpop bc\n\tpop bc\n\tret\n", out);
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

static void mir_emit_variadic_sum(
    MirStream *out, const struct MirVariadicSum *plan)
{
    int loop = new_label();
    int done = new_label();
    int byte;

    mir_stream_printf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tld hl,%d\n\tadd hl,sp\n\tpush hl\n\tpop iy\n",
            plan->count_stack_offset + 2,
            plan->first_argument_stack_offset + 2);
    if (plan->value_width == 2)
        mir_stream_puts("\tld de,0\n", out);
    else
        mir_stream_puts("\tld hl,0\n\tld de,0\n", out);
    mir_stream_printf(out,
            "\tld a,b\n\tor a\n\tjp m,L%d\n"
            "\tor c\n\tjp z,L%d\n"
            "L%d:\n",
            done, done, loop);
    if (plan->value_width == 2) {
        mir_stream_puts("\tld l,(iy+0)\n\tld h,(iy+1)\n"
              "\tex de,hl\n\tadd hl,de\n\tex de,hl\n",
              out);
    } else {
        for (byte = 0; byte < 4; ++byte) {
            const char registers[] = { 'l', 'h', 'e', 'd' };
            mir_stream_printf(out,
                    "\tld a,%c\n\t%s a,(iy+%d)\n\tld %c,a\n",
                    registers[byte],
                    byte == 0 ? "add" : "adc",
                    byte, registers[byte]);
        }
    }
    for (byte = 0; byte < plan->value_width; ++byte)
        mir_stream_puts("\tinc iy\n", out);
    mir_stream_printf(out,
            "\tdec bc\n\tld a,b\n\tor c\n\tjp nz,L%d\n"
            "L%d:\n",
            loop, done);
    if (plan->value_width == 2)
        mir_stream_puts("\tld l,e\n\tld h,d\n", out);
    mir_stream_puts("\tpop iy\n\tret\n", out);
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

static void mir_emit_guarded_global_pop(
    MirStream *out, const struct MirGuardedGlobalPop *plan)
{
    int guard = new_label();
    int after_guard = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_word(
        out, plan->cursor, plan->cursor_offset);
    mir_stream_printf(out,
            "\tld a,h\n\tor a\n\tjp m,L%d\n"
            "\tor l\n\tjp nz,L%d\n"
            "L%d:\n",
            guard, after_guard, guard);
    mir_machine_emit_symbol_call(out, plan->guard_function);
    mir_stream_printf(out, "L%d:\n", after_guard);
    mir_machine_emit_global_word(
        out, plan->cursor, plan->cursor_offset);
    mir_stream_puts("\tdec hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->cursor, plan->cursor_offset);
    mir_stream_puts("\tadd hl,hl\n", out);
    mir_machine_emit_global_address_de(
        out, plan->array, plan->array_offset);
    mir_stream_puts("\tadd hl,de\n\tld e,(hl)\n\tinc hl\n"
          "\tld d,(hl)\n\tex de,hl\n\tret\n", out);
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

static void mir_emit_float_member_scalar_compare(
    MirStream *out, const struct MirFloatMemberScalarCompare *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    if (plan->member_is_left)
        mir_emit_float_member_operand(out, plan, 0);
    else
        mir_emit_float_scalar_operand(out, plan, 0);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    if (plan->member_is_left)
        mir_emit_float_scalar_operand(out, plan, 4);
    else
        mir_emit_float_member_operand(out, plan, 4);
    mir_emit_runtime_call(out, "__fgtf");
    mir_stream_puts("\tpop bc\n\tpop bc\n"
          "\tld a,h\n\tor l\n\tld hl,0\n\tret z\n"
          "\tinc hl\n\tret\n", out);
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

static void mir_emit_exact_float_mismatch_report(
    MirStream *out, const struct MirExactFloatMismatchReport *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_word(
        out, plan->checks, plan->checks_offset);
    mir_stream_puts("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->checks, plan->checks_offset);
    mir_emit_wide_parameter(out, plan->got_stack_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_emit_wide_parameter(out, plan->want_stack_offset + 4);
    mir_emit_runtime_call(out, "__fnef");
    mir_stream_puts("\tpop bc\n\tpop bc\n"
          "\tld a,h\n\tor l\n\tret z\n", out);
    mir_machine_emit_global_word(
        out, plan->failures, plan->failures_offset);
    mir_stream_puts("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->failures, plan->failures_offset);
    mir_emit_wide_parameter(out, plan->want_stack_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_emit_wide_parameter(out, plan->got_stack_offset + 4);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->name_stack_offset + 8,
            plan->string_id);
    if ((plan->runtime_flags & MIR_CALL_FLAG_FORMAT_HEX) != 0)
        mir_emit_runtime_call(out, "__pfehx");
    mir_emit_runtime_call(out, plan->call_name);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tpop bc\n\tpop bc\n\tpop bc\n\tret\n", out);
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

static int mir_match_inline_float_tolerance_report(
    struct MirFloatToleranceReport *plan)
{
    static const int tail_opcodes[20] = {
        MIR_LABEL, MIR_LOAD, MIR_FLOAT_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG,
        MIR_NOP, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_NOP, MIR_LABEL
    };
    const struct MirInsn *difference = &mir.insns[6];
    const struct MirInsn *difference_store;
    const struct MirInsn *zero;
    const struct MirInsn *got;
    const struct MirInsn *want;
    const struct MirInsn *name;
    const struct MirInsn *epsilon = &mir.insns[19];
    const struct MirInsn *comparison = &mir.insns[20];
    const struct MirInsn *string = &mir.insns[22];
    const struct MirInsn *name_load = &mir.insns[24];
    const struct MirInsn *call = &mir.insns[30];
    const struct MirInsn *failures_load = &mir.insns[31];
    const struct MirInsn *failures_increment = &mir.insns[33];
    const struct MirInsn *failures_store = &mir.insns[34];
    struct Sym *report_function;
    int arguments[4];
    int memory_type, memory_storage, memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 37 || mir_cfg_block_count() != 3 ||
        mir.has_vla || (mir.return_type & 15) != TYPE_VOID ||
        mir.insns[0].opcode != MIR_LABEL ||
        mir.insns[1].opcode != MIR_PARAM ||
        mir.insns[2].opcode != MIR_PARAM ||
        mir.insns[3].opcode != MIR_PARAM ||
        difference->opcode != MIR_BINARY ||
        difference->immediate != '-' ||
        !type_is_float(difference->type) ||
        type_size(difference->type) != 4)
        return mir_machine_reject(
            "inline-float-tolerance-report", "shape");
    for (instruction = 17; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            tail_opcodes[instruction - 17])
            return mir_machine_reject(
                "inline-float-tolerance-report", "tail-opcode");
    got = mir_definition(difference->src1);
    want = mir_definition(difference->src2);
    if (got == NULL || want == NULL ||
        got->opcode != MIR_PARAM || want->opcode != MIR_PARAM ||
        !type_is_float(got->type) || type_size(got->type) != 4 ||
        !type_is_float(want->type) || type_size(want->type) != 4)
        return mir_machine_reject(
            "inline-float-tolerance-report", "operands");
    if (&mir.insns[1] != got && &mir.insns[1] != want)
        name = &mir.insns[1];
    else if (&mir.insns[2] != got && &mir.insns[2] != want)
        name = &mir.insns[2];
    else
        name = &mir.insns[3];
    if (type_size(name->type) != 2 ||
        type_ptr_depth(name->type) == 0)
        return mir_machine_reject(
            "inline-float-tolerance-report", "name");
    difference_store =
        mir.insns[7].opcode == MIR_STORE
            ? &mir.insns[7] : &mir.insns[8];
    zero = mir_definition(mir.insns[11].src2);
    if (zero != NULL && zero->opcode == MIR_UNARY &&
        zero->immediate == 0)
        zero = mir_definition(zero->src1);
    if (difference_store->opcode != MIR_STORE ||
        difference_store->src1 != difference->dst ||
        difference_store->memory_size != 4 ||
        (difference_store->memory_flags & (1 | 8)) != 0 ||
        !mir_machine_unobservable_local_store(difference_store) ||
        mir.insns[11].opcode != MIR_BINARY ||
        mir.insns[11].immediate != '<' ||
        mir.insns[11].src1 != difference->dst ||
        (zero == NULL ||
         !((zero->opcode == MIR_FLOAT_CONST &&
            (((unsigned long)zero->immediate & 0x7fffffffUL) == 0)) ||
           (zero->opcode == MIR_CONST && zero->immediate == 0))) ||
        mir.insns[12].opcode != MIR_BRANCH_FALSE ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[12].label != mir.insns[17].label ||
        mir.insns[14].opcode != MIR_UNARY ||
        mir.insns[14].immediate != '-' ||
        mir.insns[14].src1 != difference->dst ||
        mir.insns[16].opcode != MIR_STORE ||
        mir.insns[16].src1 != mir.insns[14].dst ||
        mir.insns[16].memory_size != 4 ||
        (mir.insns[16].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_same_location(
            difference_store, &mir.insns[16]) ||
        !mir_machine_same_location(
            difference_store, &mir.insns[18]))
        return mir_machine_reject(
            "inline-float-tolerance-report", "absolute");
    if (!type_is_float(epsilon->type) ||
        type_size(epsilon->type) != 4 ||
        comparison->immediate != '>' ||
        comparison->src1 != mir.insns[18].dst ||
        comparison->src2 != epsilon->dst ||
        mir.insns[21].src1 != comparison->dst ||
        mir.insns[21].label != mir.insns[36].label ||
        !mir_machine_same_location(name, name_load) ||
        !mir_machine_four_call_arguments(call, arguments) ||
        arguments[0] != string->dst ||
        arguments[1] != name_load->dst ||
        arguments[2] != got->dst ||
        arguments[3] != want->dst ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != MIR_CALL_FLAG_VARIADIC)
        return mir_machine_reject(
            "inline-float-tolerance-report", "report");
    report_function = find_global(call->name);
    if (strcmp(call->name, "printf") ||
        report_function == NULL || report_function->is_defined)
        return mir_machine_reject(
            "inline-float-tolerance-report", "print-function");
    if (!mir_machine_named_nonvolatile(failures_load) ||
        !mir_machine_same_location(failures_load, failures_store) ||
        !mir_scalar_memory_location(
            failures_load, &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL || type_size(memory_type) != 2 ||
        failures_increment->immediate != '+' ||
        failures_increment->src1 != failures_load->dst ||
        !mir_machine_constant_equals(failures_increment->src2, 1) ||
        failures_store->src1 != failures_increment->dst ||
        failures_store->memory_size != 2)
        return mir_machine_reject(
            "inline-float-tolerance-report", "failure-count");
    plan->failures = find_global(failures_load->name);
    plan->failures_offset = memory_offset;
    if (plan->failures == NULL || plan->failures->is_volatile ||
        !mir_machine_parameter_value_offset(
            name->dst, &plan->name_stack_offset))
        return mir_machine_reject(
            "inline-float-tolerance-report", "parameters");
    if (!mir_scalar_memory_location(
            got, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2)
        return mir_machine_reject(
            "inline-float-tolerance-report", "got");
    plan->got_stack_offset = memory_offset - 2;
    if (!mir_scalar_memory_location(
            want, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2)
        return mir_machine_reject(
            "inline-float-tolerance-report", "want");
    plan->want_stack_offset = memory_offset - 2;
    snprintf(plan->call_name, sizeof(plan->call_name), "%s",
             call->base_name[0] != 0
                 ? call->base_name
                 : asm_name_for(sym_asm_name(report_function)));
    plan->epsilon_bits =
        (unsigned long)epsilon->immediate & 0xffffffffUL;
    plan->string_id = (int)string->immediate;
    return 1;
}

static void mir_emit_float_tolerance_report(
    MirStream *out, const struct MirFloatToleranceReport *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    if (plan->checks != NULL) {
        mir_machine_emit_global_word(
            out, plan->checks, plan->checks_offset);
        mir_stream_puts("\tinc hl\n", out);
        mir_machine_emit_global_word_store(
            out, plan->checks, plan->checks_offset);
    }
    mir_emit_wide_parameter(out, plan->got_stack_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_emit_wide_parameter(out, plan->want_stack_offset + 4);
    mir_emit_runtime_call(out, "__fsf");
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_emit_runtime_call(out, "__fabs");
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out,
            "\tld hl,%lu\n\tld de,%lu\n",
            plan->epsilon_bits & 0xffffUL,
            (plan->epsilon_bits >> 16) & 0xffffUL);
    mir_emit_runtime_call(out, "__fltf");
    mir_stream_puts("\tpop bc\n\tpop bc\n"
          "\tld a,h\n\tor l\n\tret z\n", out);
    mir_machine_emit_global_word(
        out, plan->failures, plan->failures_offset);
    mir_stream_puts("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->failures, plan->failures_offset);
    mir_emit_wide_parameter(out, plan->want_stack_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_emit_wide_parameter(out, plan->got_stack_offset + 4);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->name_stack_offset + 8,
            plan->string_id);
    mir_emit_runtime_call(out, plan->call_name);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tpop bc\n\tpop bc\n\tpop bc\n\tret\n", out);
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

static void mir_emit_float_tolerance_failure(
    MirStream *out, const struct MirFloatToleranceFailure *plan)
{
    int done = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_emit_wide_parameter(out, plan->got_stack_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_emit_wide_parameter(out, plan->want_stack_offset + 4);
    mir_emit_runtime_call(out, "__fsf");
    mir_stream_puts("\tpop bc\n\tpop bc\n"
          "\tld a,d\n\tand 127\n\tld d,a\n"
          "\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->epsilon_bits);
    mir_emit_runtime_call(out, "__fltf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", done);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->name_stack_offset, plan->string_id);
    mir_machine_emit_symbol_call(out, plan->print_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_global_word(
        out, plan->failures, plan->failures_offset);
    mir_stream_puts("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->failures, plan->failures_offset);
    mir_stream_printf(out, "L%d:\n\tret\n", done);
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

static void mir_emit_float_byte_report(
    MirStream *out, const struct MirFloatByteReport *plan)
{
    int mismatch = new_label();
    int pushed_words = 0;
    int byte;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld b,h\n\tld c,l\n",
            plan->value_stack_offset);
    for (byte = 0; byte < 4; ++byte) {
        mir_stream_printf(out,
                "\tld a,(bc)\n\tinc bc\n"
                "\tld hl,%d\n\tadd hl,sp\n"
                "\tcp (hl)\n\tjp nz,L%d\n",
                plan->expected_stack_offsets[byte], mismatch);
    }
    mir_stream_puts("\tret\n", out);
    mir_stream_printf(out, "L%d:\n", mismatch);
    for (byte = 3; byte >= 0; --byte) {
        mir_emit_byte_parameter_word(
            out,
            plan->expected_stack_offsets[byte] +
                pushed_words * 2,
            1);
        mir_stream_puts("\tpush hl\n", out);
        ++pushed_words;
    }
    for (byte = 3; byte >= 0; --byte) {
        mir_stream_printf(out,
                "\tld hl,%d\n\tadd hl,sp\n",
                plan->value_stack_offset + pushed_words * 2);
        mir_machine_emit_hl_offset(out, byte, 0);
        mir_stream_puts("\tld a,(hl)\n\tld l,a\n\tld h,0\n\tpush hl\n",
              out);
        ++pushed_words;
    }
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->name_stack_offset + pushed_words * 2,
            plan->string_id);
    mir_emit_runtime_call(out, plan->call_name);
    for (pushed_words = 0; pushed_words < 10; ++pushed_words)
        mir_stream_puts("\tpop bc\n", out);
    mir_machine_emit_global_word(
        out, plan->failures, plan->failures_offset);
    mir_stream_puts("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->failures, plan->failures_offset);
    mir_stream_puts("\tret\n", out);
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

static void mir_emit_relative_tolerance_call(
    MirStream *out, const struct MirRelativeToleranceCall *plan)
{
    int nonnegative = new_label();
    int ready = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%lu\n\tld de,%lu\n"
            "\tpush de\n\tpush hl\n\tpush de\n\tpush hl\n",
            plan->scale_bits & 0xffffUL,
            (plan->scale_bits >> 16) & 0xffffUL);
    mir_emit_wide_parameter(out, plan->want_stack_offset + 8);
    mir_stream_puts("\tpush de\n\tpush hl\n\tld hl,0\n\tld de,0\n", out);
    mir_emit_runtime_call(out, "__fgtf");
    mir_stream_puts("\tpop bc\n\tpop bc\n"
          "\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", nonnegative);
    mir_emit_wide_parameter(out, plan->want_stack_offset + 8);
    mir_stream_puts("\tld a,d\n\txor 128\n\tld d,a\n", out);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", ready, nonnegative);
    mir_emit_wide_parameter(out, plan->want_stack_offset + 8);
    mir_stream_printf(out, "L%d:\n", ready);
    mir_emit_runtime_call(out, "__fmaf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tpush de\n\tpush hl\n", out);
    mir_emit_wide_parameter(out, plan->want_stack_offset + 4);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_emit_wide_parameter(out, plan->got_stack_offset + 8);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n",
            plan->name_stack_offset + 12);
    mir_machine_emit_symbol_call(out, plan->function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tpop bc\n\tpop bc\n\tpop bc\n\tret\n", out);
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

static void mir_emit_fixed_float_grid_fill(
    MirStream *out, const struct MirFixedFloatGridFill *plan)
{
    int loop = new_label();

    mir_stream_printf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_address_de(
        out, plan->root, plan->root_offset);
    mir_stream_puts("\tpush de\n\tpop iy\n\tld bc,0\n", out);
    mir_stream_printf(out, "L%d:\n", loop);
    mir_stream_printf(out,
            "\tld a,b\n\tadd a,c\n\tadd a,%d\n"
            "\tld l,a\n\tld h,0\n\tpush bc\n",
            plan->add_constant);
    mir_emit_runtime_call(out, "__fif");
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_stream_printf(out,
            "\tld hl,%lu\n\tld de,%lu\n",
            plan->divisor_bits & 0xffffUL,
            (plan->divisor_bits >> 16) & 0xffffUL);
    mir_emit_runtime_call(out, "__fdf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tld (iy+0),l\n\tld (iy+1),h\n"
          "\tld (iy+2),e\n\tld (iy+3),d\n"
          "\tinc iy\n\tinc iy\n\tinc iy\n\tinc iy\n"
          "\tinc c\n", out);
    mir_stream_printf(out,
            "\tld a,c\n\tcp %d\n\tjp c,L%d\n"
            "\tld c,0\n\tinc b\n\tld a,b\n\tcp %d\n"
            "\tjp c,L%d\n\tpop iy\n\tret\n",
            plan->inner_bound, loop,
            plan->outer_bound, loop);
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

static void mir_emit_constant_float_conditional(
    MirStream *out, const struct MirConstantFloatConditional *plan)
{
    int false_arm = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n"
            "\tjp z,L%d\n",
            plan->condition_stack_offset, false_arm);
    mir_emit_constant_float_arm(
        out, plan->true_is_float,
        plan->true_integer_width, plan->true_bits);
    mir_stream_printf(out, "L%d:\n", false_arm);
    mir_emit_constant_float_arm(
        out, plan->false_is_float,
        plan->false_integer_width, plan->false_bits);
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

static void mir_emit_conditional_global_float_load(
    MirStream *out, const struct MirConditionalGlobalFloatLoad *plan)
{
    int false_arm = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tor (hl)\n"
            "\tjp z,L%d\n",
            plan->condition_stack_offset, false_arm);
    mir_emit_global_float_to_long_return(
        out, plan, plan->true_offset);
    mir_stream_printf(out, "L%d:\n", false_arm);
    mir_emit_global_float_to_long_return(
        out, plan, plan->false_offset);
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

static void mir_emit_reduced_float_polynomial(
    MirStream *out, const struct MirReducedFloatPolynomial *plan)
{
    int after_quadrant = new_label();
    int after_sign = new_label();
    int check_lower = new_label();
    int lower_quadrant = new_label();
    int reduced = new_label();
    int x_offset = plan->parameter_stack_offset + 2;

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-5\n\tadd hl,sp\n\tld sp,hl\n"
          "\tld (ix-5),0\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    mir_machine_emit_float_bits(out, plan->two_pi_bits);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->remainder_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, x_offset);

    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->pi_bits);
    mir_emit_runtime_call(out, "__fltf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", check_lower);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->two_pi_bits);
    mir_emit_runtime_call(out, "__fsf");
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, x_offset);

    mir_stream_printf(out, "L%d:\n", check_lower);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(
        out, plan->pi_bits ^ 0x80000000UL);
    mir_emit_runtime_call(out, "__fgtf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", reduced);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->two_pi_bits);
    mir_emit_runtime_call(out, "__faf");
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, x_offset);

    mir_stream_printf(out, "L%d:\n", reduced);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->half_pi_bits);
    mir_emit_runtime_call(out, "__fltf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", lower_quadrant);
    mir_machine_emit_float_bits(out, plan->pi_bits);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_emit_runtime_call(out, "__fsf");
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, x_offset);
    if (!plan->sine_form)
        mir_stream_puts("\tld (ix-5),1\n", out);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n",
            after_quadrant, lower_quadrant);

    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(
        out, plan->half_pi_bits ^ 0x80000000UL);
    mir_emit_runtime_call(out, "__fgtf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", after_quadrant);
    mir_machine_emit_float_bits(
        out, plan->pi_bits ^ 0x80000000UL);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_emit_runtime_call(out, "__fsf");
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, x_offset);
    if (!plan->sine_form)
        mir_stream_puts("\tld (ix-5),1\n", out);
    mir_stream_printf(out, "L%d:\n", after_quadrant);

    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_emit_runtime_call(out, "__fmf");
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, -4);

    if (plan->sine_form) {
        mir_machine_emit_ix_wide_load(out, x_offset);
        mir_stream_puts("\tpush de\n\tpush hl\n", out);
        mir_machine_emit_ix_wide_load(out, x_offset);
        mir_stream_puts("\tpush de\n\tpush hl\n", out);
        mir_machine_emit_ix_wide_load(out, -4);
        mir_emit_runtime_call(out, "__fmf");
        mir_stream_puts("\tpop bc\n\tpop bc\n\tpush de\n\tpush hl\n", out);
    } else {
        mir_machine_emit_float_bits(out, plan->one_bits);
        mir_stream_puts("\tpush de\n\tpush hl\n", out);
        mir_machine_emit_ix_wide_load(out, -4);
        mir_stream_puts("\tpush de\n\tpush hl\n", out);
    }
    mir_machine_emit_float_bits(out, plan->coefficients[0]);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, -4);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->coefficients[1]);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, -4);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->coefficients[2]);
    mir_emit_runtime_call(out, "__fmaf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_emit_runtime_call(out, "__fmaf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_emit_runtime_call(out, "__fmaf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    if (!plan->sine_form) {
        mir_stream_puts("\tld a,(ix-5)\n\tor a\n", out);
        mir_stream_printf(out,
                "\tjp z,L%d\n\tld a,d\n\txor 128\n\tld d,a\nL%d:\n",
                after_sign, after_sign);
    }
    mir_stream_puts("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_match_float_atan_polynomial(
    struct MirFloatAtanPolynomial *plan)
{
    static const int expected_opcodes[88] = {
        MIR_LABEL, MIR_PARAM, MIR_FLOAT_CONST, MIR_STORE,
        MIR_FLOAT_CONST, MIR_STORE, MIR_NOP, MIR_FLOAT_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_FLOAT_CONST, MIR_UNARY,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_UNARY, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_FLOAT_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_FLOAT_CONST, MIR_NOP,
        MIR_STORE, MIR_FLOAT_CONST, MIR_UNARY, MIR_LOAD, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_LOAD,
        MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_FLOAT_CONST,
        MIR_NOP, MIR_STORE, MIR_LOAD, MIR_FLOAT_CONST, MIR_BINARY,
        MIR_LOAD, MIR_FLOAT_CONST, MIR_BINARY, MIR_BINARY, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_LABEL, MIR_LABEL, MIR_LOAD, MIR_LOAD,
        MIR_BINARY, MIR_STORE, MIR_LOAD, MIR_LOAD, MIR_NOP, MIR_BINARY,
        MIR_FLOAT_CONST, MIR_UNARY, MIR_NOP, MIR_FLOAT_CONST, MIR_NOP,
        MIR_FLOAT_CONST, MIR_UNARY, MIR_NOP, MIR_FLOAT_CONST, MIR_BINARY,
        MIR_BINARY, MIR_BINARY, MIR_BINARY, MIR_BINARY, MIR_BINARY,
        MIR_BINARY, MIR_BINARY, MIR_STORE, MIR_LOAD, MIR_LOAD, MIR_NOP,
        MIR_BINARY, MIR_BINARY, MIR_RETURN
    };
    static const int x_accesses[] = {
        1, 6, 14, 17, 20, 30, 33, 37, 44, 47,
        52, 56, 57, 60, 61
    };
    const struct MirInsn *x = &mir.insns[1];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 88 || mir_cfg_block_count() != 6 ||
        mir.has_vla || !type_is_float(mir.return_type) ||
        type_size(mir.return_type) != 4)
        return mir_machine_reject(
            "float-atan-polynomial", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "float-atan-polynomial", "opcode");
    if (!type_is_float(x->type) || type_size(x->type) != 4 ||
        !mir_scalar_memory_location(
            x, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2 ||
        memory_offset > 125 || x->object < 0)
        return mir_machine_reject(
            "float-atan-polynomial", "parameter");
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
                "float-atan-polynomial", "parameter-access");
    }
    if (!mir_machine_unobservable_local_store(&mir.insns[3]) ||
        mir.insns[3].memory_size != 4 ||
        mir.insns[3].src1 != mir.insns[2].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[5]) ||
        mir.insns[5].memory_size != 4 ||
        mir.insns[5].src1 != mir.insns[4].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[59]) ||
        mir.insns[59].memory_size != 4 ||
        mir.insns[59].src1 != mir.insns[58].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[81]) ||
        mir.insns[81].memory_size != 4 ||
        mir.insns[81].src1 != mir.insns[80].dst)
        return mir_machine_reject(
            "float-atan-polynomial", "locals");
    plan->one_bits =
        (unsigned long)mir.insns[2].immediate & 0xffffffffUL;
    plan->zero_bits =
        (unsigned long)mir.insns[4].immediate & 0xffffffffUL;
    plan->large_bound_bits =
        (unsigned long)mir.insns[21].immediate & 0xffffffffUL;
    plan->half_pi_bits =
        (unsigned long)mir.insns[25].immediate & 0xffffffffUL;
    plan->small_bound_bits =
        (unsigned long)mir.insns[38].immediate & 0xffffffffUL;
    plan->quarter_pi_bits =
        (unsigned long)mir.insns[41].immediate & 0xffffffffUL;
    if (plan->one_bits !=
            ((unsigned long)mir.insns[10].immediate & 0xffffffffUL) ||
        plan->one_bits !=
            ((unsigned long)mir.insns[28].immediate & 0xffffffffUL) ||
        plan->one_bits !=
            ((unsigned long)mir.insns[45].immediate & 0xffffffffUL) ||
        plan->one_bits !=
            ((unsigned long)mir.insns[48].immediate & 0xffffffffUL) ||
        plan->zero_bits !=
            ((unsigned long)mir.insns[7].immediate & 0xffffffffUL) ||
        mir.insns[11].immediate != '-' ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[15].immediate != '-' ||
        mir.insns[15].src1 != x->dst ||
        mir.insns[29].immediate != '-' ||
        mir.insns[29].src1 != mir.insns[28].dst)
        return mir_machine_reject(
            "float-atan-polynomial", "constants");
    if (mir.insns[8].immediate != '<' ||
        mir.insns[8].src1 != x->dst ||
        mir.insns[8].src2 != mir.insns[7].dst ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[13].src1 != mir.insns[11].dst ||
        mir.insns[17].src1 != mir.insns[15].dst ||
        mir.insns[22].immediate != '>' ||
        mir.insns[22].src1 != mir.insns[20].dst ||
        mir.insns[22].src2 != mir.insns[21].dst ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[27].src1 != mir.insns[25].dst ||
        mir.insns[31].immediate != '/' ||
        mir.insns[31].src1 != mir.insns[29].dst ||
        mir.insns[31].src2 != mir.insns[30].dst ||
        mir.insns[33].src1 != mir.insns[31].dst ||
        mir.insns[35].label != mir.insns[55].label ||
        mir.insns[39].immediate != '>' ||
        mir.insns[39].src1 != mir.insns[37].dst ||
        mir.insns[39].src2 != mir.insns[38].dst ||
        mir.insns[40].src1 != mir.insns[39].dst ||
        mir.insns[43].src1 != mir.insns[41].dst ||
        mir.insns[46].immediate != '-' ||
        mir.insns[46].src1 != mir.insns[44].dst ||
        mir.insns[46].src2 != mir.insns[45].dst ||
        mir.insns[49].immediate != '+' ||
        mir.insns[49].src1 != mir.insns[47].dst ||
        mir.insns[49].src2 != mir.insns[48].dst ||
        mir.insns[50].immediate != '/' ||
        mir.insns[50].src1 != mir.insns[46].dst ||
        mir.insns[50].src2 != mir.insns[49].dst ||
        mir.insns[52].src1 != mir.insns[50].dst)
        return mir_machine_reject(
            "float-atan-polynomial", "reduction");
    plan->coefficients[0] =
        ((unsigned long)mir.insns[64].immediate ^
         0x80000000UL) & 0xffffffffUL;
    plan->coefficients[1] =
        (unsigned long)mir.insns[67].immediate & 0xffffffffUL;
    plan->coefficients[2] =
        ((unsigned long)mir.insns[69].immediate ^
         0x80000000UL) & 0xffffffffUL;
    plan->coefficients[3] =
        (unsigned long)mir.insns[72].immediate & 0xffffffffUL;
    if (mir.insns[58].immediate != '*' ||
        mir.insns[58].src1 != mir.insns[56].dst ||
        mir.insns[58].src2 != mir.insns[57].dst ||
        mir.insns[63].immediate != '*' ||
        mir.insns[63].src1 != mir.insns[61].dst ||
        mir.insns[63].src2 != mir.insns[58].dst ||
        mir.insns[65].immediate != '-' ||
        mir.insns[65].src1 != mir.insns[64].dst ||
        mir.insns[70].immediate != '-' ||
        mir.insns[70].src1 != mir.insns[69].dst ||
        mir.insns[73].immediate != '*' ||
        mir.insns[73].src1 != mir.insns[58].dst ||
        mir.insns[73].src2 != mir.insns[72].dst ||
        mir.insns[74].immediate != '+' ||
        mir.insns[74].src1 != mir.insns[70].dst ||
        mir.insns[74].src2 != mir.insns[73].dst ||
        mir.insns[75].immediate != '*' ||
        mir.insns[75].src1 != mir.insns[58].dst ||
        mir.insns[75].src2 != mir.insns[74].dst ||
        mir.insns[76].immediate != '+' ||
        mir.insns[76].src1 != mir.insns[67].dst ||
        mir.insns[76].src2 != mir.insns[75].dst ||
        mir.insns[77].immediate != '*' ||
        mir.insns[77].src1 != mir.insns[58].dst ||
        mir.insns[77].src2 != mir.insns[76].dst ||
        mir.insns[78].immediate != '+' ||
        mir.insns[78].src1 != mir.insns[65].dst ||
        mir.insns[78].src2 != mir.insns[77].dst ||
        mir.insns[79].immediate != '*' ||
        mir.insns[79].src1 != mir.insns[63].dst ||
        mir.insns[79].src2 != mir.insns[78].dst ||
        mir.insns[80].immediate != '+' ||
        mir.insns[80].src1 != mir.insns[60].dst ||
        mir.insns[80].src2 != mir.insns[79].dst ||
        mir.insns[85].immediate != '+' ||
        mir.insns[85].src1 != mir.insns[83].dst ||
        mir.insns[85].src2 != mir.insns[80].dst ||
        mir.insns[86].immediate != '*' ||
        mir.insns[86].src1 != mir.insns[82].dst ||
        mir.insns[86].src2 != mir.insns[85].dst ||
        mir.insns[87].src1 != mir.insns[86].dst)
        return mir_machine_reject(
            "float-atan-polynomial", "polynomial");
    return 1;
}

static void mir_emit_float_atan_polynomial(
    MirStream *out, const struct MirFloatAtanPolynomial *plan)
{
    int after_sign = new_label();
    int check_small_bound = new_label();
    int reduced = new_label();
    int result_sign = new_label();
    int x_offset = plan->parameter_stack_offset + 2;

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-9\n\tadd hl,sp\n\tld sp,hl\n"
          "\tld (ix-9),0\n"
          "\tld (ix-4),0\n\tld (ix-3),0\n"
          "\tld (ix-2),0\n\tld (ix-1),0\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->zero_bits);
    mir_emit_runtime_call(out, "__fgtf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", after_sign);
    mir_stream_printf(out,
            "\tld a,(ix%+d)\n\txor 128\n"
            "\tld (ix%+d),a\n\tld (ix-9),1\n"
            "L%d:\n",
            x_offset + 3, x_offset + 3, after_sign);

    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(
        out, plan->large_bound_bits);
    mir_emit_runtime_call(out, "__fltf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", check_small_bound);
    mir_machine_emit_float_bits(out, plan->half_pi_bits);
    mir_machine_emit_ix_wide_store(out, -4);
    mir_machine_emit_float_bits(
        out, plan->one_bits ^ 0x80000000UL);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_emit_runtime_call(out, "__fdf");
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, x_offset);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n",
            reduced, check_small_bound);

    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(
        out, plan->small_bound_bits);
    mir_emit_runtime_call(out, "__fltf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", reduced);
    mir_machine_emit_float_bits(out, plan->quarter_pi_bits);
    mir_machine_emit_ix_wide_store(out, -4);

    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->one_bits);
    mir_emit_runtime_call(out, "__fsf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->one_bits);
    mir_emit_runtime_call(out, "__faf");
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_emit_runtime_call(out, "__fdf");
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, x_offset);

    mir_stream_printf(out, "L%d:\n", reduced);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_emit_runtime_call(out, "__fmf");
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, -8);

    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, -8);
    mir_emit_runtime_call(out, "__fmf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->coefficients[0]);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, -8);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->coefficients[1]);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, -8);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->coefficients[2]);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, -8);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->coefficients[3]);
    mir_emit_runtime_call(out, "__fmaf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_emit_runtime_call(out, "__fmaf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_emit_runtime_call(out, "__fmaf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_emit_runtime_call(out, "__fmaf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);

    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, -4);
    mir_emit_runtime_call(out, "__faf");
    mir_stream_puts("\tpop bc\n\tpop bc\n"
          "\tld a,(ix-9)\n\tor a\n", out);
    mir_stream_printf(out,
            "\tjp z,L%d\n\tld a,d\n\txor 128\n\tld d,a\n"
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            result_sign, result_sign);
}

static int mir_match_float_taylor_sine(
    struct MirFloatTaylorSine *plan)
{
    static const int expected_opcodes[119] = {
        MIR_LABEL, MIR_PARAM, MIR_FLOAT_CONST, MIR_STORE, MIR_CONST,
        MIR_STORE, MIR_NOP, MIR_ARG, MIR_FLOAT_CONST, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_FLOAT_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_FLOAT_CONST, MIR_BINARY, MIR_NOP,
        MIR_STORE, MIR_LABEL, MIR_LOAD, MIR_FLOAT_CONST, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_FLOAT_CONST,
        MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_LOAD,
        MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_FLOAT_CONST, MIR_LOAD, MIR_BINARY, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_FLOAT_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_FLOAT_CONST,
        MIR_UNARY, MIR_LOAD, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_LABEL, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_PHI, MIR_PHI, MIR_PHI, MIR_NOP, MIR_LOAD, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_LOAD, MIR_ARG,
        MIR_CONST, MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_CONST, MIR_BINARY,
        MIR_UNARY, MIR_ARG, MIR_CALL, MIR_UNARY, MIR_BINARY, MIR_CONST,
        MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_CONST, MIR_BINARY, MIR_UNARY,
        MIR_ARG, MIR_CALL, MIR_UNARY, MIR_BINARY, MIR_BINARY, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_RETURN
    };
    static const int x_accesses[] = {
        1, 6, 12, 13, 17, 21, 23, 28, 32, 34, 40,
        43, 47, 54, 57, 65, 76
    };
    const struct MirInsn *x = &mir.insns[1];
    int arguments[2];
    int factorial_argument;
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 119 || mir_cfg_block_count() != 10 ||
        mir.has_vla || !type_is_float(mir.return_type) ||
        type_size(mir.return_type) != 4)
        return mir_machine_reject(
            "float-taylor-sine", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "float-taylor-sine", "opcode");
    if (!type_is_float(x->type) || type_size(x->type) != 4 ||
        !mir_scalar_memory_location(
            x, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_PARAM || memory_offset < 2 ||
        memory_offset > 125 || x->object < 0)
        return mir_machine_reject(
            "float-taylor-sine", "parameter");
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
                "float-taylor-sine", "parameter-access");
    }
    if (!mir_machine_unobservable_local_store(&mir.insns[3]) ||
        mir.insns[3].memory_size != 4 ||
        mir.insns[3].src1 != mir.insns[2].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[5]) ||
        mir.insns[5].memory_size != 2 ||
        mir.insns[5].src1 != mir.insns[4].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[63]) ||
        mir.insns[63].memory_size != 1 ||
        mir.insns[63].src1 != mir.insns[62].dst)
        return mir_machine_reject(
            "float-taylor-sine", "locals");
    if (!mir_machine_two_call_arguments(
            &mir.insns[10], arguments) ||
        arguments[0] != x->dst ||
        arguments[1] != mir.insns[8].dst ||
        (plan->remainder_function =
             find_global(mir.insns[10].name)) == NULL ||
        !mir_machine_two_call_arguments(
            &mir.insns[86], arguments) ||
        arguments[0] != mir.insns[76].dst ||
        arguments[1] != mir.insns[84].dst ||
        (plan->power_function =
             find_global(mir.insns[86].name)) == NULL ||
        !mir_machine_single_call_argument(
            &mir.insns[97], &factorial_argument) ||
        factorial_argument != mir.insns[95].dst ||
        (plan->factorial_function =
             find_global(mir.insns[97].name)) == NULL)
        return mir_machine_reject(
            "float-taylor-sine", "calls");
    plan->iteration_bound =
        find_global(mir.insns[70].name);
    if (mir.insns[70].opcode != MIR_LOAD ||
        plan->iteration_bound == NULL ||
        plan->iteration_bound->is_volatile ||
        type_ptr_depth(plan->iteration_bound->type) != 0 ||
        type_size(plan->iteration_bound->type) != 2)
        return mir_machine_reject(
            "float-taylor-sine", "bound");
    plan->zero_bits =
        (unsigned long)mir.insns[2].immediate & 0xffffffffUL;
    plan->two_pi_bits =
        (unsigned long)mir.insns[8].immediate & 0xffffffffUL;
    plan->pi_bits =
        (unsigned long)mir.insns[14].immediate & 0xffffffffUL;
    plan->half_pi_bits =
        (unsigned long)mir.insns[35].immediate & 0xffffffffUL;
    plan->one_bits = 0x3f800000UL;
    if (plan->two_pi_bits !=
            ((unsigned long)mir.insns[18].immediate & 0xffffffffUL) ||
        plan->two_pi_bits !=
            ((unsigned long)mir.insns[29].immediate & 0xffffffffUL) ||
        plan->pi_bits !=
            ((unsigned long)mir.insns[24].immediate & 0xffffffffUL) ||
        plan->pi_bits !=
            ((unsigned long)mir.insns[39].immediate & 0xffffffffUL) ||
        plan->pi_bits !=
            ((unsigned long)mir.insns[52].immediate & 0xffffffffUL) ||
        plan->half_pi_bits !=
            ((unsigned long)mir.insns[48].immediate & 0xffffffffUL) ||
        !mir_machine_constant_equals(mir.insns[4].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[62].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[78].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[82].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[89].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[93].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[105].dst, 65535) ||
        !mir_machine_constant_equals(mir.insns[112].dst, 1))
        return mir_machine_reject(
            "float-taylor-sine", "constants");
    if (mir.insns[12].src1 != mir.insns[10].dst ||
        mir.insns[15].immediate != '>' ||
        mir.insns[15].src1 != mir.insns[10].dst ||
        mir.insns[15].src2 != mir.insns[14].dst ||
        mir.insns[19].immediate != '-' ||
        mir.insns[19].src1 != mir.insns[10].dst ||
        mir.insns[19].src2 != mir.insns[18].dst ||
        mir.insns[21].src1 != mir.insns[19].dst ||
        mir.insns[25].immediate != '-' ||
        mir.insns[25].src1 != mir.insns[24].dst ||
        mir.insns[26].immediate != '<' ||
        mir.insns[26].src1 != mir.insns[23].dst ||
        mir.insns[26].src2 != mir.insns[25].dst ||
        mir.insns[30].immediate != '+' ||
        mir.insns[30].src1 != mir.insns[28].dst ||
        mir.insns[30].src2 != mir.insns[29].dst ||
        mir.insns[32].src1 != mir.insns[30].dst ||
        mir.insns[36].immediate != '>' ||
        mir.insns[36].src1 != mir.insns[34].dst ||
        mir.insns[36].src2 != mir.insns[35].dst ||
        mir.insns[41].immediate != '-' ||
        mir.insns[41].src1 != mir.insns[39].dst ||
        mir.insns[41].src2 != mir.insns[40].dst ||
        mir.insns[43].src1 != mir.insns[41].dst ||
        mir.insns[49].immediate != '-' ||
        mir.insns[49].src1 != mir.insns[48].dst ||
        mir.insns[50].immediate != '<' ||
        mir.insns[50].src1 != mir.insns[47].dst ||
        mir.insns[50].src2 != mir.insns[49].dst ||
        mir.insns[53].immediate != '-' ||
        mir.insns[53].src1 != mir.insns[52].dst ||
        mir.insns[55].immediate != '-' ||
        mir.insns[55].src1 != mir.insns[53].dst ||
        mir.insns[55].src2 != mir.insns[54].dst ||
        mir.insns[57].src1 != mir.insns[55].dst)
        return mir_machine_reject(
            "float-taylor-sine", "reduction");
    if (mir.insns[72].immediate != TOK_LE ||
        mir.insns[81].immediate != '*' ||
        mir.insns[81].src1 != mir.insns[78].dst ||
        mir.insns[81].src2 != mir.insns[80].dst ||
        mir.insns[83].immediate != '-' ||
        mir.insns[83].src1 != mir.insns[81].dst ||
        mir.insns[83].src2 != mir.insns[82].dst ||
        mir.insns[88].immediate != '*' ||
        mir.insns[88].src1 != mir.insns[87].dst ||
        mir.insns[88].src2 != mir.insns[86].dst ||
        mir.insns[92].immediate != '*' ||
        mir.insns[92].src1 != mir.insns[89].dst ||
        mir.insns[92].src2 != mir.insns[91].dst ||
        mir.insns[94].immediate != '-' ||
        mir.insns[94].src1 != mir.insns[92].dst ||
        mir.insns[94].src2 != mir.insns[93].dst ||
        mir.insns[99].immediate != '/' ||
        mir.insns[99].src1 != mir.insns[88].dst ||
        mir.insns[99].src2 != mir.insns[98].dst ||
        mir.insns[100].immediate != '+' ||
        mir.insns[100].src1 != mir.insns[66].dst ||
        mir.insns[100].src2 != mir.insns[99].dst ||
        mir.insns[102].src1 != mir.insns[100].dst ||
        mir.insns[106].immediate != '*' ||
        mir.insns[106].src1 != mir.insns[67].dst ||
        mir.insns[106].src2 != mir.insns[105].dst ||
        mir.insns[108].src1 != mir.insns[106].dst ||
        mir.insns[113].immediate != '+' ||
        mir.insns[115].label != mir.insns[64].label ||
        mir.insns[118].src1 != mir.insns[66].dst)
        return mir_machine_reject(
            "float-taylor-sine", "loop");
    return 1;
}

static void mir_emit_float_taylor_sine(
    MirStream *out, const struct MirFloatTaylorSine *plan)
{
    int check_lower_period = new_label();
    int check_upper_quadrant = new_label();
    int check_lower_quadrant = new_label();
    int reduced = new_label();
    int loop = new_label();
    int loop_body = new_label();
    int loop_done = new_label();
    int positive_term = new_label();
    int x_offset = plan->parameter_stack_offset + 2;

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-8\n\tadd hl,sp\n\tld sp,hl\n"
          "\tld (ix-4),0\n\tld (ix-3),0\n"
          "\tld (ix-2),0\n\tld (ix-1),0\n"
          "\tld (ix-5),0\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    mir_machine_emit_float_bits(out, plan->two_pi_bits);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(
        out, plan->remainder_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, x_offset);

    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->pi_bits);
    mir_emit_runtime_call(out, "__fltf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", check_lower_period);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->two_pi_bits);
    mir_emit_runtime_call(out, "__fsf");
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, x_offset);

    mir_stream_printf(out, "L%d:\n", check_lower_period);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(
        out, plan->pi_bits ^ 0x80000000UL);
    mir_emit_runtime_call(out, "__fgtf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", check_upper_quadrant);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->two_pi_bits);
    mir_emit_runtime_call(out, "__faf");
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, x_offset);

    mir_stream_printf(out, "L%d:\n", check_upper_quadrant);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->half_pi_bits);
    mir_emit_runtime_call(out, "__fltf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", check_lower_quadrant);
    mir_machine_emit_float_bits(out, plan->pi_bits);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_emit_runtime_call(out, "__fsf");
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, x_offset);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n",
            reduced, check_lower_quadrant);

    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(
        out, plan->half_pi_bits ^ 0x80000000UL);
    mir_emit_runtime_call(out, "__fgtf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", reduced);
    mir_machine_emit_float_bits(
        out, plan->pi_bits ^ 0x80000000UL);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_emit_runtime_call(out, "__fsf");
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, x_offset);

    mir_stream_printf(out, "L%d:\n\tld (ix-6),1\nL%d:\n",
            reduced, loop);
    mir_machine_emit_global_word(
        out, plan->iteration_bound, 0);
    mir_stream_puts("\tex de,hl\n\tld l,(ix-6)\n\tld h,0\n"
          "\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out,
            "\tjp c,L%d\n\tjp z,L%d\n\tjp L%d\n"
            "L%d:\n",
            loop_body, loop_body, loop_done, loop_body);

    mir_stream_puts("\tld l,(ix-6)\n\tld h,0\n"
          "\tadd hl,hl\n\tdec hl\n", out);
    mir_emit_runtime_call(out, "__fif");
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(
        out, plan->power_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n"
          "\tld a,(ix-5)\n\tor a\n", out);
    mir_stream_printf(out,
            "\tjp z,L%d\n\tld a,d\n\txor 128\n\tld d,a\n"
            "L%d:\n\tpush de\n\tpush hl\n",
            positive_term, positive_term);

    mir_stream_puts("\tld l,(ix-6)\n\tld h,0\n"
          "\tadd hl,hl\n\tdec hl\n\tld de,0\n"
          "\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(
        out, plan->factorial_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_emit_runtime_call(out, "__fulf");
    mir_emit_runtime_call(out, "__fdf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, -4);
    mir_emit_runtime_call(out, "__faf");
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, -4);
    mir_stream_puts("\tld a,(ix-5)\n\txor 1\n\tld (ix-5),a\n"
          "\tinc (ix-6)\n", out);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n",
            loop, loop_done);
    mir_machine_emit_ix_wide_load(out, -4);
    mir_stream_puts("\tld sp,ix\n\tpop ix\n\tret\n", out);
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

static void mir_emit_float_tangent_rational(
    MirStream *out, const struct MirFloatTangentRational *plan)
{
    int after_period = new_label();
    int after_quadrant = new_label();
    int check_lower_period = new_label();
    int check_lower_quadrant = new_label();
    int done = new_label();
    int nonzero_denominator = new_label();
    int nonzero_result = new_label();
    int x_offset = plan->parameter_stack_offset + 2;

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-5\n\tadd hl,sp\n\tld sp,hl\n"
          "\tld (ix-5),0\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    mir_machine_emit_float_bits(out, plan->pi_bits);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->remainder_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, x_offset);

    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->half_pi_bits);
    mir_emit_runtime_call(out, "__fltf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", check_lower_period);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->pi_bits);
    mir_emit_runtime_call(out, "__fsf");
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, x_offset);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n",
            after_period, check_lower_period);

    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(
        out, plan->half_pi_bits ^ 0x80000000UL);
    mir_emit_runtime_call(out, "__fgtf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", after_period);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->pi_bits);
    mir_emit_runtime_call(out, "__faf");
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, x_offset);

    mir_stream_printf(out, "L%d:\n", after_period);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->quarter_pi_bits);
    mir_emit_runtime_call(out, "__fltf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", check_lower_quadrant);
    mir_machine_emit_float_bits(out, plan->half_pi_bits);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_emit_runtime_call(out, "__fsf");
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, x_offset);
    mir_stream_printf(out,
            "\tld (ix-5),1\n\tjp L%d\n"
            "L%d:\n",
            after_quadrant, check_lower_quadrant);

    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(
        out, plan->quarter_pi_bits ^ 0x80000000UL);
    mir_emit_runtime_call(out, "__fgtf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", after_quadrant);
    mir_machine_emit_float_bits(
        out, plan->half_pi_bits ^ 0x80000000UL);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_emit_runtime_call(out, "__fsf");
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, x_offset);
    mir_stream_printf(out, "\tld (ix-5),1\nL%d:\n", after_quadrant);

    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_emit_runtime_call(out, "__fmf");
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, -4);

    mir_machine_emit_ix_wide_load(out, x_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->fifteen_bits);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, -4);
    mir_emit_runtime_call(out, "__fsf");
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_emit_runtime_call(out, "__fmf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpush de\n\tpush hl\n", out);

    mir_machine_emit_float_bits(out, plan->fifteen_bits);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->six_bits);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, -4);
    mir_emit_runtime_call(out, "__fmf");
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    mir_emit_runtime_call(out, "__fsf");
    mir_stream_puts("\tpop bc\n\tpop bc\n"
          "\tld a,d\n\tand 127\n\tor e\n\tor h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", nonzero_denominator);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld hl,0\n\tld de,0\n", out);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", done, nonzero_denominator);

    mir_emit_runtime_call(out, "__fdf");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,(ix-5)\n\tor a\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", done);
    mir_stream_puts("\tld a,d\n\tand 127\n\tor e\n\tor h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n\tld hl,0\n\tld de,0\n"
                 "\tjp L%d\nL%d:\n",
            nonzero_result, done, nonzero_result);
    mir_machine_emit_ix_wide_store(out, -4);
    mir_machine_emit_float_bits(out, plan->one_bits);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_ix_wide_load(out, -4);
    mir_emit_runtime_call(out, "__fdf");
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);

    mir_stream_printf(out, "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n", done);
}

static int mir_match_recursive_frame_fill(
    struct MirRecursiveFrameFill *plan)
{
    static const int expected_opcodes[47] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_PHI, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP, MIR_NOP, MIR_UNARY,
        MIR_BINARY, MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_ADDRESS,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY, MIR_RETURN
    };
    const struct MirInsn *parameter = &mir.insns[1];
    const struct MirInsn *call = &mir.insns[40];
    int parameter_type, parameter_storage, parameter_offset;
    int array_type, array_storage, array_offset;
    int other_type, other_storage, other_offset;
    int call_argument;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 47 || mir_cfg_block_count() != 4 ||
        mir.has_vla || type_size(mir.return_type) != 2 ||
        type_ptr_depth(mir.return_type) != 0)
        return mir_machine_reject("recursive-frame-fill", "shape");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "recursive-frame-fill", "opcode");
    if (!mir_scalar_memory_location(
            parameter, &parameter_type, &parameter_storage,
            &parameter_offset) ||
        parameter_storage != SC_PARAM || parameter_offset < 4 ||
        type_size(parameter_type) != 2 ||
        type_ptr_depth(parameter_type) != 0 ||
        !mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[4]) ||
        mir.insns[4].memory_size != 1 ||
        mir.insns[7].src1 != mir.insns[3].dst ||
        mir.insns[7].src2 != mir.insns[24].dst ||
        mir.insns[7].phi_pred1 != mir.insns[0].label ||
        mir.insns[7].phi_pred2 != mir.insns[21].label ||
        mir.insns[9].immediate <= 1 ||
        mir.insns[9].immediate > 255 ||
        mir.insns[10].immediate != 0 ||
        mir.insns[10].src1 != mir.insns[7].dst ||
        mir.insns[11].immediate != '<' ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[11].src2 != mir.insns[9].dst ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[12].label != mir.insns[27].label)
        return mir_machine_reject("recursive-frame-fill", "loop");
    plan->count = (int)mir.insns[9].immediate;
    if ((plan->count & (plan->count - 1)) != 0 ||
        !mir_scalar_memory_location(
            &mir.insns[13], &array_type, &array_storage,
            &array_offset) ||
        array_storage != SC_LOCAL ||
        array_offset != -2 * plan->count ||
        type_size(array_type) != 2 ||
        mir.insns[15].src1 != mir.insns[13].dst ||
        mir.insns[15].src2 != mir.insns[7].dst ||
        mir.insns[15].immediate != 2 ||
        mir.insns[18].immediate != 0 ||
        mir.insns[18].src1 != mir.insns[7].dst ||
        mir.insns[19].immediate != '+' ||
        mir.insns[19].src1 != parameter->dst ||
        mir.insns[19].src2 != mir.insns[18].dst ||
        mir.insns[20].src1 != mir.insns[15].dst ||
        mir.insns[20].src2 != mir.insns[19].dst ||
        mir.insns[20].memory_size != 2 ||
        !mir_machine_constant_equals(mir.insns[23].dst, 1) ||
        mir.insns[24].immediate != '+' ||
        mir.insns[24].src1 != mir.insns[7].dst ||
        mir.insns[24].src2 != mir.insns[23].dst ||
        !mir_machine_unobservable_local_store(&mir.insns[25]) ||
        mir.insns[25].src1 != mir.insns[24].dst ||
        mir.insns[26].label != mir.insns[5].label)
        return mir_machine_reject("recursive-frame-fill", "fill");
    if (!mir_scalar_memory_location(
            &mir.insns[28], &other_type, &other_storage, &other_offset) ||
        other_storage != array_storage || other_offset != array_offset ||
        type_size(other_type) != 2 ||
        !mir_machine_constant_equals(
            mir.insns[30].dst, plan->count - 1) ||
        mir.insns[31].immediate != '&' ||
        mir.insns[31].src1 != parameter->dst ||
        mir.insns[31].src2 != mir.insns[30].dst ||
        mir.insns[32].src1 != mir.insns[28].dst ||
        mir.insns[32].src2 != mir.insns[31].dst ||
        mir.insns[32].immediate != 2 ||
        mir.insns[33].src1 != mir.insns[32].dst ||
        mir.insns[33].memory_size != 2 ||
        mir.insns[35].src1 != mir.insns[33].dst ||
        mir.insns[35].memory_size != 2 ||
        !mir_machine_name_nonvolatile(mir.insns[35].name))
        return mir_machine_reject("recursive-frame-fill", "publish");
    plan->sink = find_global(mir.insns[35].name);
    if (plan->sink == NULL || plan->sink->storage == SC_EXTERN ||
        plan->sink->needs_extrn ||
        !mir_machine_constant_equals(mir.insns[37].dst, 1) ||
        mir.insns[38].immediate != '+' ||
        mir.insns[38].src1 != parameter->dst ||
        mir.insns[38].src2 != mir.insns[37].dst ||
        !mir_machine_single_call_argument(call, &call_argument) ||
        call_argument != mir.insns[38].dst ||
        strcmp(call->name, mir.name) ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC | MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0)
        return mir_machine_reject("recursive-frame-fill", "call");
    plan->function = find_global(call->name);
    if (plan->function == NULL || !plan->function->is_defined ||
        plan->function->is_funcptr ||
        (call->base_name[0] != 0 &&
         strcmp(call->base_name,
                asm_name_for(sym_asm_name(plan->function)))) ||
        !mir_scalar_memory_location(
            &mir.insns[41], &other_type, &other_storage, &other_offset) ||
        other_storage != array_storage || other_offset != array_offset ||
        !mir_machine_constant_equals(mir.insns[42].dst, 0) ||
        mir.insns[43].src1 != mir.insns[41].dst ||
        mir.insns[43].src2 != mir.insns[42].dst ||
        mir.insns[43].immediate != 2 ||
        mir.insns[44].src1 != mir.insns[43].dst ||
        mir.insns[44].memory_size != 2 ||
        mir.insns[45].immediate != '+' ||
        mir.insns[45].src1 != call->dst ||
        mir.insns[45].src2 != mir.insns[44].dst ||
        mir.insns[46].src1 != mir.insns[45].dst)
        return mir_machine_reject("recursive-frame-fill", "return");
    plan->parameter_frame_offset = parameter_offset;
    plan->array_offset = array_offset;
    return 1;
}

static void mir_emit_recursive_frame_fill(
    MirStream *out, const struct MirRecursiveFrameFill *plan)
{
    int loop = new_label();

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld sp,hl\n",
            plan->array_offset);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tpush ix\n\tpop hl\n\tld bc,%d\n\tadd hl,bc\n"
            "\tld e,(ix+%d)\n\tld d,(ix+%d)\n\tld b,%d\n"
            "L%d:\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n"
            "\tinc hl\n\tinc de\n\tdjnz L%d\n"
            "\tpush ix\n\tpop hl\n\tld bc,%d\n\tadd hl,bc\n"
            "\tld a,(ix+%d)\n\tand %d\n\tadd a,a\n"
            "\tld e,a\n\tld d,0\n\tadd hl,de\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n",
            plan->array_offset,
            plan->parameter_frame_offset,
            plan->parameter_frame_offset + 1,
            plan->count, loop, loop,
            plan->array_offset, plan->parameter_frame_offset,
            plan->count - 1);
    mir_machine_emit_global_word_store(out, plan->sink, 0);
    mir_stream_printf(out,
            "\tld l,(ix+%d)\n\tld h,(ix+%d)\n\tinc hl\n\tpush hl\n",
            plan->parameter_frame_offset,
            plan->parameter_frame_offset + 1);
    mir_machine_emit_symbol_call(out, plan->function);
    mir_stream_printf(out,
            "\tpop bc\n\tld e,(ix+%d)\n\tld d,(ix+%d)\n"
            "\tadd hl,de\n\tld sp,ix\n\tpop ix\n\tret\n",
            plan->parameter_frame_offset,
            plan->parameter_frame_offset + 1);
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

static void mir_emit_recursive_wide_product(
    MirStream *out, const struct MirRecursiveWideProduct *plan)
{
    int recurse = new_label();
    int decremented = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_emit_wide_parameter(out, plan->parameter_stack_offset);
    mir_stream_printf(out,
            "\tld a,d\n\tor e\n\tor h\n\tor l\n\tjp nz,L%d\n"
            "\tld hl,%d\n\tld de,0\n\tret\n"
            "L%d:\n\tpush de\n\tpush hl\n"
            "\tld bc,1\n\tor a\n\tsbc hl,bc\n\tjp nc,L%d\n"
            "\tdec de\nL%d:\n\tpush de\n\tpush hl\n",
            recurse, plan->base_result,
            recurse, decremented, decremented);
    mir_machine_emit_symbol_call(out, plan->function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    if (plan->operation == '*') {
        mir_emit_runtime_call(out, "__lmul");
        mir_stream_puts("\tpop bc\n\tpop bc\n", out);
    } else {
        mir_stream_puts("\tpop bc\n\tadd hl,bc\n\tex de,hl\n"
              "\tpop bc\n\tadc hl,bc\n\tex de,hl\n", out);
    }
    mir_stream_puts("\tret\n", out);
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

static void mir_emit_recursive_wide_tree_sum(
    MirStream *out, const struct MirRecursiveWideTreeSum *plan)
{
    int nonnull = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_emit_recursive_tree_pointer(out, plan, 0);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out,
            "\tjp nz,L%d\n\tld hl,0\n\tld de,0\n\tret\nL%d:\n",
            nonnull, nonnull);

    if (plan->value_first) {
        mir_machine_emit_hl_offset(out, plan->value_offset, 0);
        mir_stream_puts("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
              "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
              "\tld l,c\n\tld h,b\n\tpush de\n\tpush hl\n", out);
    }
    mir_emit_recursive_tree_pointer(
        out, plan, plan->value_first ? 4 : 0);
    mir_machine_emit_hl_offset(out, plan->left_offset, 0);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n", out);
    mir_machine_emit_symbol_call(out, plan->function);
    mir_stream_puts("\tpop bc\n", out);
    if (!plan->value_first) {
        mir_stream_puts("\tpush de\n\tpush hl\n", out);
        mir_emit_recursive_tree_value(out, plan, 4);
    }
    mir_stream_puts("\tpop bc\n\tadd hl,bc\n\tex de,hl\n"
          "\tpop bc\n\tadc hl,bc\n\tex de,hl\n"
          "\tpush de\n\tpush hl\n", out);
    mir_emit_recursive_tree_pointer(out, plan, 4);
    mir_machine_emit_hl_offset(out, plan->right_offset, 0);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpush de\n", out);
    mir_machine_emit_symbol_call(out, plan->function);
    mir_stream_puts("\tpop bc\n"
          "\tpop bc\n\tadd hl,bc\n\tex de,hl\n"
          "\tpop bc\n\tadc hl,bc\n\tex de,hl\n\tret\n", out);
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

static void mir_emit_byte_rotate_flags(
    MirStream *out, const struct MirByteRotateFlags *plan)
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
    mir_stream_printf(out,
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
    mir_stream_printf(out,
            "\tor a\n\tjp z,L%d\n\tscf\nL%d:\n\trr c\n",
            ror_ready, ror_ready);
    mir_stream_puts("\tld a,0\n\tadc a,0\n", out);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->carry_offset, 1);
    mir_stream_printf(out, "\tjp L%d\n", flags);
    mir_stream_printf(out, "L%d:\n\tsla c\n", asl);
    mir_stream_puts("\tld a,0\n\tadc a,0\n", out);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->carry_offset, 1);
    mir_stream_printf(out, "\tjp L%d\n", flags);
    mir_stream_printf(out, "L%d:\n", rol);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->carry_offset, 0);
    mir_stream_printf(out,
            "\tor a\n\tjp z,L%d\n\tscf\nL%d:\n\trl c\n",
            rol_ready, rol_ready);
    mir_stream_puts("\tld a,0\n\tadc a,0\n", out);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->carry_offset, 1);
    mir_stream_printf(out, "\tjp L%d\n", flags);
    mir_stream_printf(out, "L%d:\n\tsrl c\n", lsr);
    mir_stream_puts("\tld a,0\n\tadc a,0\n", out);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->carry_offset, 1);
    mir_stream_printf(out, "L%d:\n", flags);
    mir_stream_puts("\tld a,c\n\trlca\n\tld a,0\n\tadc a,0\n", out);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->negative_offset, 1);
    mir_stream_puts("\tld a,c\n\tor a\n\tld a,0\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n\tinc a\nL%d:\n", nonzero, nonzero);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->zero_offset, 1);
    mir_stream_puts("\tld l,c\n\tld h,0\n\tret\n", out);
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

static void mir_emit_status_unpack(
    MirStream *out, const struct MirStatusUnpack *plan)
{
    int flag;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->stack_offset, 0);
    mir_stream_puts("\tinc a\n", out);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->stack_offset, 1);
    mir_stream_puts("\tld l,a\n\tld h,0\n", out);
    mir_machine_emit_global_address_de(
        out, plan->memory, plan->memory_offset);
    mir_stream_puts("\tadd hl,de\n\tld c,(hl)\n", out);
    for (flag = 0; flag < 6; ++flag) {
        int clear = new_label();

        mir_stream_printf(out, "\tbit %d,c\n\tld a,0\n\tjp z,L%d\n\tinc a\nL%d:\n",
                plan->masks[flag] == 128 ? 7 :
                plan->masks[flag] == 64 ? 6 :
                plan->masks[flag] == 8 ? 3 :
                plan->masks[flag] == 4 ? 2 :
                plan->masks[flag] == 2 ? 1 : 0,
                clear, clear);
        mir_machine_emit_global_byte_a(
            out, plan->state, plan->flag_offsets[flag], 1);
    }
    mir_stream_puts("\tret\n", out);
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

static void mir_emit_status_pack(
    MirStream *out, const struct MirStatusPack *plan)
{
    int flag;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\tld c,48\n", out);
    for (flag = 0; flag < 6; ++flag) {
        int clear = new_label();

        mir_machine_emit_global_byte_a(
            out, plan->state, plan->flag_offsets[flag], 0);
        mir_stream_printf(out, "\tor a\n\tjp z,L%d\n\tset %d,c\nL%d:\n",
                clear,
                plan->masks[flag] == 128 ? 7 :
                plan->masks[flag] == 64 ? 6 :
                plan->masks[flag] == 8 ? 3 :
                plan->masks[flag] == 4 ? 2 :
                plan->masks[flag] == 2 ? 1 : 0,
                clear);
    }
    mir_stream_puts("\tld l,c\n\tld h,0\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->function);
    mir_stream_puts("\tpop bc\n\tret\n", out);
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

static void mir_emit_byte_math_flags(
    MirStream *out, const struct MirByteMathFlags *plan)
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
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld a,(hl)\n"
            "\tand 224\n\tld b,a\n"
            "\tld hl,%d\n\tadd hl,sp\n\tld c,(hl)\n"
            "\tld a,b\n\tcp 192\n\tjp z,L%d\n",
            plan->op_stack_offset, plan->rhs_stack_offset, compare);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->decimal_offset, 0);
    mir_stream_printf(out,
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

    mir_stream_printf(out, "L%d:\n", addition);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->carry_offset, 0);
    mir_stream_printf(out, "\tor a\n\tjp z,L%d\n\tscf\nL%d:\n",
            carry_ready, carry_ready);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->accumulator_offset, 0);
    mir_stream_puts("\tadc a,c\n\tpush af\n\tpop de\n"
          "\tld a,e\n\tand 1\n", out);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->carry_offset, 1);
    mir_stream_puts("\tld a,e\n\trrca\n\trrca\n\tand 1\n", out);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->overflow_offset, 1);
    mir_stream_printf(out, "\tld c,d\n\tjp L%d\n", flags);

    mir_stream_printf(out, "L%d:\n", logic_or);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->accumulator_offset, 0);
    mir_stream_printf(out, "\tor c\n\tld c,a\n\tjp L%d\n", flags);
    mir_stream_printf(out, "L%d:\n", logic_and);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->accumulator_offset, 0);
    mir_stream_printf(out, "\tand c\n\tld c,a\n\tjp L%d\n", flags);
    mir_stream_printf(out, "L%d:\n", logic_xor);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->accumulator_offset, 0);
    mir_stream_puts("\txor c\n\tld c,a\n", out);

    mir_stream_printf(out, "L%d:\n\tld a,c\n", flags);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->accumulator_offset, 1);
    mir_stream_puts("\tld a,c\n\trlca\n\tld a,0\n\tadc a,0\n", out);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->negative_offset, 1);
    mir_stream_puts("\tld a,c\n\tor a\n\tld a,0\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n\tinc a\nL%d:\n",
            nonzero, nonzero);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->zero_offset, 1);
    mir_stream_puts("\tret\n", out);

    mir_stream_printf(out, "L%d:\n\tld l,c\n\tld h,0\n\tpush hl\n",
            compare);
    mir_machine_emit_global_byte_a(
        out, plan->state, plan->accumulator_offset, 0);
    mir_stream_puts("\tld l,a\n\tld h,0\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->compare_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tret\n", out);

    mir_stream_printf(out, "L%d:\n\tld l,c\n\tld h,0\n\tpush hl\n"
                 "\tld l,b\n\tld h,0\n\tpush hl\n", decimal);
    mir_machine_emit_symbol_call(out, plan->decimal_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tret\n", out);
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

static void mir_emit_byte_range_union(
    MirStream *out, const struct MirByteRangeUnion *plan)
{
    int accepted = new_label();
    int range;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n\tld c,(hl)\n",
            plan->stack_offset);
    for (range = 0; range < 3; ++range)
        mir_stream_printf(out,
                "\tld a,c\n\tsub %d\n\tcp %d\n\tjp c,L%d\n",
                plan->lower[range],
                plan->upper[range] - plan->lower[range] + 1,
                accepted);
    mir_stream_puts("\tld hl,0\n\tret\n", out);
    mir_stream_printf(out, "L%d:\n\tld hl,1\n\tret\n", accepted);
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

static void mir_emit_byte_array_sum(
    MirStream *out, const struct MirByteArraySum *plan)
{
    int done = new_label();
    int exit = new_label();
    int loop = new_label();

    mir_stream_printf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
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

static void mir_emit_wraparound_bool_step(
    MirStream *out, const struct MirWraparoundBoolStep *plan)
{
    int done = new_label();
    int exit = new_label();
    int left_ready = new_label();
    int loop = new_label();
    int no_increment = new_label();
    int right_ready = new_label();
    int zero = new_label();

    mir_stream_printf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
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

int mir_try_emit_float_recursion_kernels(MirStream *out)
{
    struct MirPrefixUpdateChecks prefix_update_checks;
    struct MirManyWideChecks many_wide_checks;
    struct MirManyByte4Checks many_byte4_checks;
    struct MirPointerDifferenceMain pointer_difference_main;
    struct MirConstexprWideChecks constexpr_wide_checks;
    struct MirPackedBitDecode packed_bit_decode;
    struct MirSortSearchReport sort_search_report;
    struct MirLeafConstructor leaf_constructor;
    struct MirPointerWordSumUntilZero pointer_word_sum_until_zero;
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
    struct MirFloatAtanPolynomial float_atan_polynomial;
    struct MirFloatTaylorSine float_taylor_sine;
    struct MirFloatTangentRational float_tangent_rational;
    struct MirRecursiveFrameFill recursive_frame_fill;
    struct MirRecursiveWideProduct recursive_wide_product;
    struct MirRecursiveWideTreeSum recursive_wide_tree_sum;
    struct MirByteRotateFlags byte_rotate_flags;
    struct MirStatusUnpack status_unpack;
    struct MirStatusPack status_pack;
    struct MirByteMathFlags byte_math_flags;
    struct MirByteRangeUnion byte_range_union;
    struct MirByteArraySum byte_array_sum;
    struct MirWraparoundBoolStep wraparound_bool_step;

    if (mir_match_prefix_update_checks(&prefix_update_checks)) {
        mir_emit_prefix_update_checks(out, &prefix_update_checks);
        return 1;
    }
    if (mir_match_many_wide_checks(&many_wide_checks)) {
        mir_emit_many_wide_checks(out, &many_wide_checks);
        return 1;
    }
    if (mir_match_many_byte4_checks(&many_byte4_checks)) {
        mir_emit_many_byte4_checks(out, &many_byte4_checks);
        return 1;
    }
    if (mir_match_pointer_difference_main(&pointer_difference_main)) {
        mir_emit_pointer_difference_main(out, &pointer_difference_main);
        return 1;
    }
    if (mir_match_constexpr_wide_checks(&constexpr_wide_checks)) {
        mir_emit_constexpr_wide_checks(out, &constexpr_wide_checks);
        return 1;
    }
    if (mir_match_packed_bit_decode(&packed_bit_decode)) {
        mir_emit_packed_bit_decode(out, &packed_bit_decode);
        return 1;
    }
    if (mir_match_sort_search_report(&sort_search_report)) {
        mir_emit_sort_search_report(out, &sort_search_report);
        return 1;
    }
    if (mir_match_leaf_constructor(&leaf_constructor)) {
        mir_emit_leaf_constructor(out, &leaf_constructor);
        return 1;
    }
    if (mir_match_pointer_word_sum_until_zero(
            &pointer_word_sum_until_zero)) {
        mir_emit_pointer_word_sum_until_zero(
            out, &pointer_word_sum_until_zero);
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
            &float_tolerance_report) ||
        mir_match_inline_float_tolerance_report(
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
    if (mir_match_float_atan_polynomial(
            &float_atan_polynomial)) {
        mir_emit_float_atan_polynomial(
            out, &float_atan_polynomial);
        return 1;
    }
    if (mir_match_float_taylor_sine(
            &float_taylor_sine)) {
        mir_emit_float_taylor_sine(
            out, &float_taylor_sine);
        return 1;
    }
    {
        int numeric_result =
            mir_try_emit_numeric_kernels(out, 1);

        if (numeric_result >= 0)
            return numeric_result;
    }
    if (mir_match_float_tangent_rational(
            &float_tangent_rational)) {
        mir_emit_float_tangent_rational(
            out, &float_tangent_rational);
        return 1;
    }
    if (mir_match_recursive_frame_fill(&recursive_frame_fill)) {
        mir_emit_recursive_frame_fill(out, &recursive_frame_fill);
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
    return -1;
}
