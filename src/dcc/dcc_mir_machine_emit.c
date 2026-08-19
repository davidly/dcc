/**
 * @file dcc_mir_machine_emit.c
 * @brief Top-level coordinator for structurally exact MIR-to-Z80 schedules.
 *
 * @par Role
 * Holds the small set of Z80 emission/proof helpers shared across every
 * dcc_mir_machine_*.c family (declared in dcc_mir_machine_internal.h), and
 * implements mir_try_emit_scheduled_machine_cfg(), which calls each family
 * dispatcher in original selector-policy order and returns the first
 * emitted result. It does not implement any exact-schedule kernels itself;
 * those live in the dcc_mir_machine_*.c family modules.
 *
 * @par Key entry point
 * mir_try_emit_scheduled_machine_cfg() returns emitted output only after a
 * family dispatcher has proved the complete supported MIR shape. Candidate
 * selection and profitability remain in dcc_mir_select.c.
 */

#include "dcc_mir_machine_internal.h"
#include <stdint.h>

/* Private copy of mir_machine_constant_value (small helper
 * duplicated per family file rather than shared, matching
 * existing convention; needed here too since this coordinator
 * still calls it from mir_machine_global_address_offset). */

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

void mir_machine_emit_hl_offset(
    MirStream *out, int offset, int preserve_bc);
int mir_machine_named_nonvolatile(
    const struct MirInsn *insn);
int mir_machine_constant_equals(
    int value, long expected);
int mir_machine_parameter_value_offset(
    int value, int *stack_offset);
int mir_machine_pointee_is_volatile(
    const struct MirInsn *parameter);
void mir_machine_emit_global_address_de(
    MirStream *out, struct Sym *symbol, int offset);
int mir_machine_global_address_offset(
    int value, struct Sym **root_out,
    long *offset_out, int depth);
void mir_machine_emit_global_word(
    MirStream *out, struct Sym *symbol, int offset);
void mir_machine_emit_float_bits(
    MirStream *out, unsigned long bits);

int mir_machine_reject(const char *template_name, const char *reason)
{
    if (getenv("DCC_MIR_MACHINE_REPORT") != NULL)
        fprintf(stderr,
                "; MIR machine function=%s template=%s reject=%s\n",
                mir.name, template_name, reason);
    return 0;
}

int mir_machine_same_location(
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

int mir_machine_unobservable_local_store(
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

int mir_machine_evaluate_constant(
    int value, long *result, int depth)
{
    const struct MirInsn *insn;
    long left;
    long right;

    if (result == NULL || depth > 32)
        return 0;
    insn = mir_definition(value);
    if (insn == NULL)
        return 0;
    if (insn->opcode == MIR_CONST) {
        *result = insn->immediate;
        if (type_size(insn->type) == 1) {
            *result &= 0xffL;
            if ((insn->type & 15) == TYPE_BOOL)
                *result = *result != 0;
            else if ((insn->type & TYPE_UNSIGNED) == 0 &&
                     (*result & 0x80L) != 0)
                *result -= 0x100L;
        } else if (type_size(insn->type) == 2) {
            *result &= 0xffffL;
            if ((insn->type & TYPE_UNSIGNED) == 0 &&
                (*result & 0x8000L) != 0)
                *result -= 0x10000L;
        } else if (type_size(insn->type) == 4) {
            *result = (long)((unsigned long)*result &
                             0xffffffffUL);
            if ((insn->type & TYPE_UNSIGNED) == 0 &&
                (*result & 0x80000000L) != 0)
                *result -= 0x100000000L;
        }
        return 1;
    }
    if (insn->opcode == MIR_UNARY) {
        int operation;
        int folded;

        if (!mir_machine_evaluate_constant(
                insn->src1, &left, depth + 1))
            return 0;
        operation = (int)insn->immediate;
        if (operation == 0 || operation == '+')
            folded = mir_fold_constant_binary(
                '+', 0, left, insn->type, result);
        else if (operation == '-')
            folded = mir_fold_constant_binary(
                '-', 0, left, insn->type, result);
        else if (operation == '~')
            folded = mir_fold_constant_binary(
                '^', left, -1, insn->type, result);
        else if (operation == '!')
            folded = mir_fold_constant_binary(
                TOK_EQ, left, 0, insn->type, result) ||
                   (*result = left == 0, 1);
        else
            return 0;
        if (!folded)
            return 0;
        if (type_size(insn->type) == 1) {
            *result &= 0xffL;
            if ((insn->type & 15) == TYPE_BOOL)
                *result = *result != 0;
            else if ((insn->type & TYPE_UNSIGNED) == 0 &&
                     (*result & 0x80L) != 0)
                *result -= 0x100L;
        }
        return 1;
    }
    if (insn->opcode != MIR_BINARY ||
        !mir_machine_evaluate_constant(
            insn->src1, &left, depth + 1) ||
        !mir_machine_evaluate_constant(
            insn->src2, &right, depth + 1))
        return 0;
    if (!mir_fold_constant_binary(
            (int)insn->immediate, left, right,
            insn->secondary_offset, result) &&
        !mir_fold_constant_compare(
            (int)insn->immediate, left, right,
            insn->secondary_offset, result))
        return 0;
    if (type_size(insn->secondary_offset) == 1) {
        *result &= 0xffL;
        if ((insn->secondary_offset & 15) == TYPE_BOOL)
            *result = *result != 0;
        else if ((insn->secondary_offset & TYPE_UNSIGNED) == 0 &&
                 (*result & 0x80L) != 0)
            *result -= 0x100L;
    }
    return 1;
}

int mir_match_final_call_integer_type(int type, int width)
{
    return type_ptr_depth(type) == 0 &&
        type_size(type) == width &&
        !type_is_float(type) &&
        (type & TYPE_UNSIGNED) == 0;
}

int mir_match_math_symbol_target(
    const struct MirInsn *call, struct Sym *function)
{
    return call->base_name[0] == 0 ||
        !strcmp(call->base_name,
                asm_name_for(sym_asm_name(function)));
}

int mir_match_action_decode_pointer_type(int type)
{
    return type_ptr_depth(type) != 0 && type_size(type) == 2;
}

int mir_match_action_decode_word_type(int type)
{
    return type_ptr_depth(type) == 0 &&
        !type_is_float(type) && type_size(type) == 2;
}

int mir_match_buffered_declaration_buffer(
    int instruction, const struct MirInsn *first,
    int *offset_out)
{
    const struct MirInsn *address = &mir.insns[instruction];
    int memory_type;
    int memory_storage;
    int memory_offset;

    if (address->opcode != MIR_ADDRESS ||
        !mir_machine_same_location(first, address) ||
        !mir_scalar_memory_location(
            address, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_LOCAL ||
        type_ptr_depth(address->type) != 1 ||
        (address->type & 15) != TYPE_CHAR ||
        (memory_type & 15) != TYPE_CHAR ||
        type_size(memory_type) != 1 ||
        memory_offset >= 0 || memory_offset < -32768)
        return 0;
    *offset_out = memory_offset;
    return 1;
}

void mir_machine_emit_symbol_call(
    MirStream *out, struct Sym *symbol)
{
    const char *name = asm_name_for(sym_asm_name(symbol));

    if ((symbol->storage == SC_EXTERN || symbol->needs_extrn) &&
        mir_extrn_should_emit(symbol))
        mir_stream_printf(out, "\textrn %s\n", name);
    mir_stream_printf(out, "\tcall %s\n", name);
}

void mir_machine_emit_hl_scalar_call(
    MirStream *out, struct Sym *symbol)
{
    if (!symbol->is_fastcall)
        mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, symbol);
    if (!symbol->is_fastcall)
        mir_stream_puts("\tpop bc\n", out);
}

int mir_machine_parameter_value_offset(
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

int mir_machine_global_address_offset(
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

int mir_machine_named_nonvolatile(const struct MirInsn *insn)
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

int mir_machine_constant_equals(int value, long expected)
{
    const struct MirInsn *constant = mir_definition(value);

    return constant != NULL && constant->opcode == MIR_CONST &&
           constant->immediate == expected;
}

int mir_machine_pointee_is_volatile(
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

void mir_machine_emit_global_word(
    MirStream *out, struct Sym *symbol, int offset)
{
    const char *name = asm_name_for(sym_asm_name(symbol));

    if ((symbol->storage == SC_EXTERN || symbol->needs_extrn) &&
        mir_extrn_should_emit(symbol))
        mir_stream_printf(out, "\textrn %s\n", name);
    if (offset == 0)
        mir_stream_printf(out, "\tld hl,(%s)\n", name);
    else
        mir_stream_printf(out, "\tld hl,(%s%+d)\n", name, offset);
}

void mir_machine_emit_global_address_de(
    MirStream *out, struct Sym *symbol, int offset)
{
    const char *name = asm_name_for(sym_asm_name(symbol));

    if ((symbol->storage == SC_EXTERN || symbol->needs_extrn) &&
        mir_extrn_should_emit(symbol))
        mir_stream_printf(out, "\textrn %s\n", name);
    if (offset == 0)
        mir_stream_printf(out, "\tld de,%s\n", name);
    else
        mir_stream_printf(out, "\tld de,%s%+d\n", name, offset);
}

void mir_machine_emit_global_word_store(
    MirStream *out, struct Sym *symbol, int offset)
{
    const char *name = asm_name_for(sym_asm_name(symbol));

    if (offset == 0)
        mir_stream_printf(out, "\tld (%s),hl\n", name);
    else
        mir_stream_printf(out, "\tld (%s%+d),hl\n", name, offset);
}

void mir_machine_emit_vla_allocate_rows(
    MirStream *out, unsigned long row_bytes)
{
    mir_emit_mul_hl_const(out, row_bytes);
    mir_stream_puts("\tex de,hl\n"
          "\tld hl,0\n\tadd hl,sp\n"
          "\tor a\n\tsbc hl,de\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
}

void mir_machine_emit_hl_offset(
    MirStream *out, int offset, int preserve_bc)
{
    int count;

    if (offset >= -8 && offset <= 8) {
        const char *operation = offset < 0 ? "\tdec hl\n" : "\tinc hl\n";
        for (count = 0; count < (offset < 0 ? -offset : offset); ++count)
            mir_stream_puts(operation, out);
        return;
    }
    if (preserve_bc)
        mir_stream_puts("\tpush bc\n", out);
    mir_stream_printf(out, "\tld bc,%d\n\tadd hl,bc\n", offset);
    if (preserve_bc)
        mir_stream_puts("\tpop bc\n", out);
}

void mir_machine_emit_ix_wide_load(
    MirStream *out, int offset)
{
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
            offset, offset + 1, offset + 2, offset + 3);
}

void mir_machine_emit_ix_wide_store(
    MirStream *out, int offset)
{
    mir_stream_printf(out,
            "\tld (ix%+d),l\n\tld (ix%+d),h\n"
            "\tld (ix%+d),e\n\tld (ix%+d),d\n",
            offset, offset + 1, offset + 2, offset + 3);
}

void mir_machine_emit_float_bits(
    MirStream *out, unsigned long bits)
{
    mir_stream_printf(out, "\tld hl,%lu\n\tld de,%lu\n",
            bits & 0xffffUL, (bits >> 16) & 0xffffUL);
}

void mir_emit_final_call_constant(
    MirStream *out, unsigned long value, int width)
{
    if (width == 4)
        mir_emit_fixed_point_constant(out, value);
    else
        mir_stream_printf(out, "\tld hl,%lu\n\tpush hl\n",
                value & 0xffffUL);
}

void mir_emit_final_call_cleanup(
    MirStream *out, int words)
{
    while (words-- > 0)
        mir_stream_puts("\tpop bc\n", out);
}

int mir_try_emit_scheduled_machine_cfg(MirStream *out)
{
    int result;

    result = mir_try_emit_container_kernels(out);
    if (result >= 0)
        return result;
    result = mir_try_emit_wide_record_kernels(out);
    if (result >= 0)
        return result;
    result = mir_try_emit_structural_checks(out);
    if (result >= 0)
        return result;
    result = mir_try_emit_float_recursion_kernels(out);
    if (result >= 0)
        return result;
    result = mir_try_emit_byte_scan_kernels(out);
    if (result >= 0)
        return result;
    return mir_try_emit_constant_folding_kernels(out);
}
