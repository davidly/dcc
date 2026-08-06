/* dcc_mir_homed_cfg.c - the mir_try_emit_homed_scalar_cfg selector: emits
 * Z80 for acyclic control flow whose scalar values can all stay in
 * fixed "home" registers/temporaries without a full backend spill
 * frame.
 *
 * Part of the dcc_mir.c MIR backend split; see dcc_mir_internal.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dcc.h"
#include "dcc_ast.h"
#include "dcc_mir.h"
#include "dcc_mir_internal.h"

static int mir_homed_constant_absolute_access_supported(
    const struct MirInsn *insn);
static int mir_homed_cfg_used_unary_not_branch;

int mir_homed_cfg_depends_on_unary_not_branch(void)
{
    return mir_homed_cfg_used_unary_not_branch;
}

/* mir-text-size Item T19: this selector's own MIR_INDEX_ADDRESS acceptance
 * (Item 22, below) already restricts to the fixed-stride, constant-index
 * shape only (insn->base_name[0] == 0, index_definition->opcode ==
 * MIR_CONST) - and emission (mir_emit_pointer_offset_address_to_home,
 * dcc_mir_emit_common.c) folds the byte offset entirely at compile time,
 * exactly like dcc_mir_spilled_cfg.c's own constant-index fast path, and
 * likewise never reads the index constant's own runtime value. A
 * MIR_CONST whose sole use is exactly this shape is therefore just as
 * dead here as Item T18 found it to be in the spilled-scalar-cfg
 * selector - this is the same predicate, ported to this file's own MIR
 * instruction table (a static duplicate rather than a shared symbol,
 * since dcc_mir_spilled_cfg.c and dcc_mir_homed_cfg.c are separate
 * translation units - see dcc_mir_internal.h). */
static int mir_index_only_constant(int value)
{
    const struct MirInsn *definition = mir_definition(value);
    int match_count = 0;
    int instruction;

    if (definition == NULL || definition->opcode != MIR_CONST)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->opcode == MIR_INDEX_ADDRESS && insn->src2 == value &&
            insn->base_name[0] == 0) {
            ++match_count;
            continue;
        }
        if (insn->src1 == value || insn->src2 == value)
            return 0;
    }
    return match_count == 1;
}

static int mir_homed_constant_binary(const struct MirInsn *insn,
                                     int *operation, long *value)
{
    const struct MirInsn *right;

    if (insn->opcode != MIR_BINARY)
        return 0;
    right = mir_definition(insn->src2);
    if (right == NULL || right->opcode != MIR_CONST)
        return 0;
    if (insn->immediate == '*') {
        unsigned long multiplier =
            (unsigned long)right->immediate & 0xffffUL;
        if (!mir_mul_const_fast_path_eligible(multiplier, insn->dst))
            return 0;
        *operation = '*';
        *value = (long)multiplier;
        return 1;
    }
    if ((insn->immediate == TOK_SHL || insn->immediate == TOK_SHR) &&
        right->immediate >= 0 && right->immediate < 16) {
        *operation = (int)insn->immediate;
        *value = right->immediate;
        return 1;
    }
    return 0;
}

static int mir_homed_binary_only_constant(int value)
{
    int instruction;
    int uses = 0;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        int operation;
        long count;
        if (insn->src2 == value &&
            mir_homed_constant_binary(insn, &operation, &count)) {
            ++uses;
        } else if (insn->src1 == value || insn->src2 == value) {
            return 0;
        }
    }
    return uses == 1;
}

static int mir_homed_wide_constant_binary(const struct MirInsn *insn,
                                           long *value)
{
    const struct MirInsn *right;

    if (insn->opcode != MIR_BINARY ||
        !type_is_long(insn->type) ||
        !type_is_long(insn->secondary_offset) ||
        type_size(insn->secondary_offset) != 4)
        return 0;
    right = mir_definition(insn->src2);
    if (right == NULL || right->opcode != MIR_CONST ||
        type_size(right->type) != 4)
        return 0;
    if (insn->immediate != '+' && insn->immediate != '-' &&
        insn->immediate != '&' && insn->immediate != '|' &&
        insn->immediate != '^' &&
        !((insn->immediate == TOK_SHL || insn->immediate == TOK_SHR) &&
          right->immediate >= 0 && right->immediate < 32) &&
        !(insn->immediate == '*' &&
          mir_ulong_log2_pow2((unsigned long)right->immediate) > 0))
        return 0;
    *value = right->immediate;
    return 1;
}

static int mir_homed_wide_binary_only_constant(int value)
{
    int instruction;
    int uses = 0;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        long constant;
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->src2 == value &&
            mir_homed_wide_constant_binary(insn, &constant))
            ++uses;
        else if (insn->src1 == value || insn->src2 == value)
            return 0;
    }
    return uses == 1;
}

static int mir_homed_value_is_rematerializable(int value)
{
    const struct MirInsn *definition = mir_definition(value);

    return definition != NULL &&
           ((definition->opcode == MIR_CONST &&
             mir_homed_wide_binary_only_constant(value)) ||
            (definition->opcode == MIR_STRING_ADDRESS &&
             mir_homed_string_call_argument(value)));
}

int mir_homed_rematerializable_wide_candidate_count(void)
{
    int has_wide = type_size(mir.return_type) == 4;
    int count = 0;
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if ((insn->dst >= 0 && type_size(insn->type) == 4) ||
            (insn->opcode == MIR_BINARY &&
             type_size(insn->secondary_offset) == 4))
            has_wide = 1;
        if (insn->dst >= 0 &&
            mir_homed_value_is_rematerializable(insn->dst))
            ++count;
    }
    return has_wide ? count : 0;
}

static int mir_homed_wide_type_supported(int type)
{
    return type_size(type) == 4 &&
           (type_is_long(type) || type_is_float(type));
}

static int mir_homed_byte_indirect_count(void)
{
    int count = 0;
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (mir_insn_is_reachable(instruction) &&
            (insn->opcode == MIR_LOAD_INDIRECT ||
             insn->opcode == MIR_STORE_INDIRECT) &&
            insn->memory_size == 1 &&
            !mir_homed_constant_absolute_access_supported(insn))
            ++count;
    }
    return count;
}

static int mir_homed_float_cast_supported(const struct MirInsn *insn)
{
    const struct MirInsn *source;

    if (insn == NULL || insn->opcode != MIR_UNARY ||
        insn->immediate != 0)
        return 0;
    source = mir_definition(insn->src1);
    if (source == NULL ||
        type_is_float(source->type) == type_is_float(insn->type))
        return 0;
    return (type_is_float(source->type) &&
            type_size(source->type) == 4 &&
            (type_size(insn->type) == 1 ||
             type_size(insn->type) == 2 ||
             type_is_long(insn->type))) ||
           (type_is_float(insn->type) &&
            type_size(insn->type) == 4 &&
            (type_size(source->type) == 1 ||
             type_size(source->type) == 2 ||
             type_is_long(source->type)));
}

static int mir_homed_wide_binary_supported(const struct MirInsn *insn)
{
    long constant;

    if (insn->opcode != MIR_BINARY ||
        !mir_homed_wide_type_supported(insn->secondary_offset))
        return 0;
    if (type_size(insn->type) <= 2)
        return insn->immediate == TOK_EQ || insn->immediate == TOK_NE ||
               insn->immediate == '<' || insn->immediate == '>' ||
               insn->immediate == TOK_LE || insn->immediate == TOK_GE;
    if (mir_cfg_block_count() != 1 ||
        !mir_homed_wide_type_supported(insn->type))
        return 0;
    if (type_is_float(insn->secondary_offset))
        return type_is_float(insn->type) &&
               (insn->immediate == '+' || insn->immediate == '-' ||
                insn->immediate == '*' || insn->immediate == '/');
    return type_is_long(insn->type) &&
           (insn->immediate == '+' || insn->immediate == '-' ||
            insn->immediate == '&' || insn->immediate == '|' ||
            insn->immediate == '^' ||
            mir_homed_wide_constant_binary(insn, &constant));
}

static int mir_emit_homed_wide_comparison_instruction(
    FILE *out, const struct MirInsn *insn)
{
    int instruction = (int)(insn - mir.insns);
    int dst_color = mir.allocation_colors[insn->dst];
    int preserve_hl_de =
        mir_home_color_live_across(instruction, MIR_COLOR_HL_DE);
    int preserve_bc_iy =
        mir_home_color_live_across(instruction, MIR_COLOR_BC_IY);
    int preserve_hl =
        dst_color != MIR_COLOR_HL &&
        mir_home_color_live_across(instruction, MIR_COLOR_HL);
    int preserve_de =
        dst_color != MIR_COLOR_DE &&
        mir_home_color_live_across(instruction, MIR_COLOR_DE);
    int preserve_bc =
        dst_color != MIR_COLOR_BC &&
        mir_home_color_live_across(instruction, MIR_COLOR_BC);

    if (preserve_hl_de) fputs("\tpush de\n\tpush hl\n", out);
    if (preserve_bc_iy) fputs("\tpush iy\n\tpush bc\n", out);
    if (preserve_hl) fputs("\tpush hl\n", out);
    if (preserve_de) fputs("\tpush de\n", out);
    if (preserve_bc) fputs("\tpush bc\n", out);
    if (!mir_emit_wide_home_to_stack(out, insn->src1) ||
        !mir_emit_wide_home_to_hl_de(out, insn->src2) ||
        !mir_emit_wide_operation(out, insn) ||
        !mir_emit_hl_to_home(out, insn->dst))
        return 0;
    if (preserve_bc) fputs("\tpop bc\n", out);
    if (preserve_de) fputs("\tpop de\n", out);
    if (preserve_hl) fputs("\tpop hl\n", out);
    if (preserve_bc_iy) fputs("\tpop bc\n\tpop iy\n", out);
    if (preserve_hl_de) fputs("\tpop hl\n\tpop de\n", out);
    return 1;
}

static int mir_emit_homed_truth_jump(FILE *out, int value, int instruction,
                                     int true_label)
{
    const struct MirInsn *condition = mir_definition(value);

    if (condition != NULL && type_size(condition->type) == 4) {
        int preserve_hl_de = mir_home_color_live_across(
            instruction, MIR_COLOR_HL_DE);
        if (preserve_hl_de)
            fputs("\tpush de\n\tpush hl\n", out);
        if (!mir_emit_wide_home_to_hl_de(out, value))
            return 0;
        fputs("\tld a,d\n", out);
        if (type_is_float(condition->type))
            fputs("\tand 7fh\n", out);
        fputs("\tor e\n\tor h\n\tor l\n", out);
        if (preserve_hl_de)
            fputs("\tpop hl\n\tpop de\n", out);
    } else {
        int preserve_hl = mir.allocation_colors[value] != MIR_COLOR_HL;
        if (preserve_hl)
            fputs("\tpush hl\n", out);
        if (!mir_emit_home_to_hl(out, value))
            return 0;
        fputs("\tld a,h\n\tor l\n", out);
        if (preserve_hl)
            fputs("\tpop hl\n", out);
    }
    fprintf(out, "\tjp nz, L%d\n", true_label);
    return 1;
}

static int mir_homed_reject(const char *reason)
{
    if (getenv("DCC_MIR_HOMED_REPORT") != NULL)
        fprintf(stderr, "; MIR homed function=%s reject=%s spills=%d\n",
                mir.name, reason, mir.allocation_spill_count);
    return 0;
}

int mir_homed_cfg_depends_on_word_store(void)
{
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_STORE &&
            !mir_object_is_fully_promoted(mir.insns[instruction].object))
            return 1;
    return 0;
}

int mir_homed_cfg_depends_on_dynamic_index(void)
{
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        const struct MirInsn *index_definition;
        if (insn->opcode != MIR_INDEX_ADDRESS)
            continue;
        index_definition = mir_definition(insn->src2);
        if (index_definition == NULL ||
            index_definition->opcode != MIR_CONST)
            return 1;
    }
    return 0;
}

static int mir_homed_constant_absolute_access_supported(
    const struct MirInsn *insn)
{
    if (!mir_constant_absolute_access_supported(insn))
        return 0;
    if (insn->memory_size != 1)
        return 1;
    return insn->opcode == MIR_STORE_INDIRECT &&
        mir_cfg_block_count() == 1;
}

int mir_homed_cfg_depends_on_constant_absolute(void)
{
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir_homed_constant_absolute_access_supported(
                &mir.insns[instruction]))
            return 1;
    return 0;
}

static int mir_homed_requires_ix_frame(void)
{
    int instruction;

    if (mir_has_lazy_parameters())
        return 1;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        int memory_type, memory_storage, memory_offset;

        if (!mir_insn_is_reachable(instruction))
            continue;
        if (insn->opcode == MIR_PARAM && insn->dst >= 0 &&
            type_size(insn->type) == 4 &&
            mir_value_has_use(insn->dst))
            return 1;
        if (insn->opcode != MIR_LOAD && insn->opcode != MIR_ADDRESS &&
            (insn->opcode != MIR_STORE ||
             mir_object_is_fully_promoted(insn->object)))
            continue;
        if (mir_scalar_memory_location(insn, &memory_type, &memory_storage,
                                       &memory_offset) &&
            (memory_storage == SC_LOCAL || memory_storage == SC_PARAM))
            return 1;
    }
    return 0;
}

static int mir_homed_frame_offset(int storage, int offset, int uses_iy)
{
    return storage == SC_PARAM && uses_iy ? offset + 2 : offset;
}

static int mir_emit_homed_wide_binary_instruction(
    FILE *out, const struct MirInsn *insn)
{
    int instruction = (int)(insn - mir.insns);
    int dst_color = mir.allocation_colors[insn->dst];
    int preserve_hl_de =
        dst_color != MIR_COLOR_HL_DE &&
        mir_home_color_live_across(instruction, MIR_COLOR_HL_DE);
    int preserve_bc_iy =
        dst_color != MIR_COLOR_BC_IY &&
        mir_home_color_live_across(instruction, MIR_COLOR_BC_IY);
    int preserve_hl =
        dst_color != MIR_COLOR_HL_DE &&
        mir_home_color_live_across(instruction, MIR_COLOR_HL);
    int preserve_de =
        dst_color != MIR_COLOR_HL_DE &&
        mir_home_color_live_across(instruction, MIR_COLOR_DE);
    int preserve_bc =
        dst_color != MIR_COLOR_BC_IY &&
        mir_home_color_live_across(instruction, MIR_COLOR_BC);
    long constant;

    if (preserve_hl_de) fputs("\tpush de\n\tpush hl\n", out);
    if (preserve_bc_iy) fputs("\tpush iy\n\tpush bc\n", out);
    if (preserve_hl) fputs("\tpush hl\n", out);
    if (preserve_de) fputs("\tpush de\n", out);
    if (preserve_bc) fputs("\tpush bc\n", out);
    if (mir_homed_wide_constant_binary(insn, &constant)) {
        unsigned long bits = (unsigned long)constant;
        unsigned int low = (unsigned int)(bits & 0xffffUL);
        unsigned int high = (unsigned int)((bits >> 16) & 0xffffUL);
        if (!mir_emit_wide_home_to_hl_de(out, insn->src1))
            return 0;
        if (insn->immediate == '+' && bits == 1) {
            int no_carry = new_label();
            fputs("\tinc hl\n\tld a,h\n\tor l\n", out);
            fprintf(out, "\tjp nz, L%d\n\tinc de\nL%d:\n",
                    no_carry, no_carry);
        } else if (insn->immediate == '-' && bits == 1) {
            int no_borrow = new_label();
            fputs("\tld a,h\n\tor l\n", out);
            fprintf(out, "\tjp nz, L%d\n\tdec de\nL%d:\n\tdec hl\n",
                    no_borrow, no_borrow);
        } else if (insn->immediate == '+' || insn->immediate == '-') {
            fprintf(out, "\tld bc,%u\n\t%s\n\tex de,hl\n\tld bc,%u\n\t%s\n"
                         "\tex de,hl\n",
                    low, insn->immediate == '+' ? "add hl,bc"
                                                : "or a\n\tsbc hl,bc",
                    high, insn->immediate == '+' ? "adc hl,bc"
                                                 : "sbc hl,bc");
        } else if (insn->immediate == '*' ||
                   insn->immediate == TOK_SHL ||
                   insn->immediate == TOK_SHR) {
            long shift = insn->immediate == '*'
                ? mir_ulong_log2_pow2(bits)
                : constant;
            mir_emit_wide_shift_by_constant(
                out, insn->immediate != TOK_SHR,
                (insn->secondary_offset & TYPE_UNSIGNED) != 0, shift);
        } else {
            const char *operation = insn->immediate == '&' ? "and" :
                                    insn->immediate == '|' ? "or" : "xor";
            fprintf(out,
                    "\tld a,l\n\t%s %u\n\tld l,a\n"
                    "\tld a,h\n\t%s %u\n\tld h,a\n"
                    "\tld a,e\n\t%s %u\n\tld e,a\n"
                    "\tld a,d\n\t%s %u\n\tld d,a\n",
                    operation, low & 255, operation, low >> 8,
                    operation, high & 255, operation, high >> 8);
        }
        if (!mir_emit_hl_de_to_wide_home(out, insn->dst))
            return 0;
    } else {
        if (!mir_emit_wide_home_to_stack(out, insn->src1) ||
            !mir_emit_wide_home_to_hl_de(out, insn->src2) ||
            !mir_emit_wide_operation(out, insn) ||
            !mir_emit_hl_de_to_wide_home(out, insn->dst)) {
            return 0;
        }
    }
    if (preserve_bc) fputs("\tpop bc\n", out);
    if (preserve_de) fputs("\tpop de\n", out);
    if (preserve_hl) fputs("\tpop hl\n", out);
    if (preserve_bc_iy) fputs("\tpop bc\n\tpop iy\n", out);
    if (preserve_hl_de) fputs("\tpop hl\n\tpop de\n", out);
    return 1;
}

static int mir_emit_homed_wide_indirect_load(FILE *out,
                                             const struct MirInsn *insn)
{
    int instruction = (int)(insn - mir.insns);
    int dst_color = mir.allocation_colors[insn->dst];
    int preserve_hl_de =
        dst_color != MIR_COLOR_HL_DE &&
        mir_home_color_live_across(instruction, MIR_COLOR_HL_DE);
    int preserve_bc_iy =
        dst_color != MIR_COLOR_BC_IY &&
        mir_home_color_live_across(instruction, MIR_COLOR_BC_IY);
    int preserve_hl =
        dst_color != MIR_COLOR_HL_DE &&
        mir_home_color_live_across(instruction, MIR_COLOR_HL);
    int preserve_de =
        dst_color != MIR_COLOR_HL_DE &&
        mir_home_color_live_across(instruction, MIR_COLOR_DE);

    if (preserve_hl_de) fputs("\tpush de\n\tpush hl\n", out);
    if (preserve_bc_iy) fputs("\tpush iy\n\tpush bc\n", out);
    if (preserve_hl) fputs("\tpush hl\n", out);
    if (preserve_de) fputs("\tpush de\n", out);
    if (!mir_emit_home_to_hl(out, insn->src1))
        return 0;
    if (dst_color == MIR_COLOR_HL_DE &&
        mir_homed_cfg_depends_on_dynamic_index() &&
        mir.allocation_colors[insn->src1] != MIR_COLOR_DE &&
        mir.allocation_colors[insn->src1] != MIR_COLOR_HL_DE) {
        fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tinc hl\n"
              "\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n"
              "\tex de,hl\n", out);
    } else {
        fputs("\tpush hl\n\tld a,(hl)\n\tinc hl\n"
              "\tld h,(hl)\n\tld l,a\n\tex (sp),hl\n"
              "\tinc hl\n\tinc hl\n\tld e,(hl)\n\tinc hl\n"
              "\tld d,(hl)\n\tpop hl\n", out);
    }
    if (!mir_emit_hl_de_to_wide_home(out, insn->dst))
        return 0;
    if (preserve_de) fputs("\tpop de\n", out);
    if (preserve_hl) fputs("\tpop hl\n", out);
    if (preserve_bc_iy) fputs("\tpop bc\n\tpop iy\n", out);
    if (preserve_hl_de) fputs("\tpop hl\n\tpop de\n", out);
    return 1;
}

static int mir_emit_homed_wide_indirect_store(FILE *out,
                                              const struct MirInsn *insn)
{
    int instruction = (int)(insn - mir.insns);
    int preserve_hl_de =
        mir_home_color_live_across(instruction, MIR_COLOR_HL_DE);
    int preserve_bc_iy =
        mir_home_color_live_across(instruction, MIR_COLOR_BC_IY);
    int preserve_hl =
        mir_home_color_live_across(instruction, MIR_COLOR_HL);
    int preserve_de =
        mir_home_color_live_across(instruction, MIR_COLOR_DE);
    int preserve_bc =
        mir_home_color_live_across(instruction, MIR_COLOR_BC);

    if (preserve_hl_de) fputs("\tpush de\n\tpush hl\n", out);
    if (preserve_bc_iy) fputs("\tpush iy\n\tpush bc\n", out);
    if (preserve_hl) fputs("\tpush hl\n", out);
    if (preserve_de) fputs("\tpush de\n", out);
    if (preserve_bc) fputs("\tpush bc\n", out);
    if (!mir_emit_home_push(out, insn->src1))
        return 0;
    if (!mir_emit_wide_home_to_hl_de(out, insn->src2))
        return 0;
    fputs("\tpop bc\n\tpush de\n\tex de,hl\n"
          "\tld h,b\n\tld l,c\n\tld (hl),e\n\tinc hl\n"
          "\tld (hl),d\n\tinc hl\n\tpop de\n"
          "\tld (hl),e\n\tinc hl\n\tld (hl),d\n", out);
    if (preserve_bc) fputs("\tpop bc\n", out);
    if (preserve_de) fputs("\tpop de\n", out);
    if (preserve_hl) fputs("\tpop hl\n", out);
    if (preserve_bc_iy) fputs("\tpop bc\n\tpop iy\n", out);
    if (preserve_hl_de) fputs("\tpop hl\n\tpop de\n", out);
    return 1;
}

static int mir_emit_homed_wide_named_load(FILE *out,
                                          const struct MirInsn *insn,
                                          const char *assembly_name)
{
    int instruction = (int)(insn - mir.insns);
    int dst_color = mir.allocation_colors[insn->dst];
    int preserve_hl_de =
        dst_color != MIR_COLOR_HL_DE &&
        mir_home_color_live_across(instruction, MIR_COLOR_HL_DE);
    int preserve_bc_iy =
        dst_color != MIR_COLOR_BC_IY &&
        mir_home_color_live_across(instruction, MIR_COLOR_BC_IY);
    int preserve_hl =
        dst_color != MIR_COLOR_HL_DE &&
        mir_home_color_live_across(instruction, MIR_COLOR_HL);
    int preserve_de =
        dst_color != MIR_COLOR_HL_DE &&
        mir_home_color_live_across(instruction, MIR_COLOR_DE);

    if (preserve_hl_de) fputs("\tpush de\n\tpush hl\n", out);
    if (preserve_bc_iy) fputs("\tpush iy\n\tpush bc\n", out);
    if (preserve_hl) fputs("\tpush hl\n", out);
    if (preserve_de) fputs("\tpush de\n", out);
    fprintf(out, "\tld hl,(%s)\n\tld de,(%s+2)\n",
            assembly_name, assembly_name);
    if (!mir_emit_hl_de_to_wide_home(out, insn->dst))
        return 0;
    if (preserve_de) fputs("\tpop de\n", out);
    if (preserve_hl) fputs("\tpop hl\n", out);
    if (preserve_bc_iy) fputs("\tpop bc\n\tpop iy\n", out);
    if (preserve_hl_de) fputs("\tpop hl\n\tpop de\n", out);
    return 1;
}

static int mir_emit_homed_wide_named_store(FILE *out,
                                           const struct MirInsn *insn,
                                           const char *assembly_name)
{
    int instruction = (int)(insn - mir.insns);
    int preserve_hl_de =
        mir_home_color_live_across(instruction, MIR_COLOR_HL_DE);
    int preserve_bc_iy =
        mir_home_color_live_across(instruction, MIR_COLOR_BC_IY);
    int preserve_hl =
        mir_home_color_live_across(instruction, MIR_COLOR_HL);
    int preserve_de =
        mir_home_color_live_across(instruction, MIR_COLOR_DE);
    int preserve_bc =
        mir_home_color_live_across(instruction, MIR_COLOR_BC);

    if (preserve_hl_de) fputs("\tpush de\n\tpush hl\n", out);
    if (preserve_bc_iy) fputs("\tpush iy\n\tpush bc\n", out);
    if (preserve_hl) fputs("\tpush hl\n", out);
    if (preserve_de) fputs("\tpush de\n", out);
    if (preserve_bc) fputs("\tpush bc\n", out);
    if (!mir_emit_wide_home_to_hl_de(out, insn->src1))
        return 0;
    fprintf(out, "\tld (%s),hl\n\tld (%s+2),de\n",
            assembly_name, assembly_name);
    if (preserve_bc) fputs("\tpop bc\n", out);
    if (preserve_de) fputs("\tpop de\n", out);
    if (preserve_hl) fputs("\tpop hl\n", out);
    if (preserve_bc_iy) fputs("\tpop bc\n\tpop iy\n", out);
    if (preserve_hl_de) fputs("\tpop hl\n\tpop de\n", out);
    return 1;
}

static int mir_emit_homed_constant_absolute_load(
    FILE *out, const struct MirInsn *insn)
{
    char operand[160];
    int instruction = (int)(insn - mir.insns);
    int preserve_hl_de = mir_home_color_live_across(
        instruction, MIR_COLOR_HL_DE);
    int preserve_hl = !preserve_hl_de &&
        mir.allocation_colors[insn->dst] != MIR_COLOR_HL &&
        mir_home_color_live_across(instruction, MIR_COLOR_HL);

    if (!mir_prepare_constant_absolute_operand(
            out, insn->src1, operand, sizeof(operand)))
        return 0;
    if (preserve_hl_de)
        fputs("\tpush de\n\tpush hl\n", out);
    if (preserve_hl)
        fputs("\tpush hl\n", out);
    if (insn->memory_size == 1) {
        fprintf(out, "\tld a,(%s)\n\tld l,a\n", operand);
        if (type_is_bool(insn->type)) {
            int end_label = new_label();
            fputs("\tld a,l\n\tor a\n\tld hl,0\n", out);
            fprintf(out, "\tjp z, L%d\n\tinc hl\nL%d:\n",
                    end_label, end_label);
        } else if ((insn->type & TYPE_UNSIGNED) != 0) {
            fputs("\tld h,0\n", out);
        } else {
            mir_emit_signed_byte_extend(out);
        }
    } else {
        fprintf(out, "\tld hl,(%s)\n", operand);
    }
    if (mir.allocation_colors[insn->dst] != MIR_COLOR_HL &&
        !mir_emit_hl_to_home(out, insn->dst))
        return 0;
    if (preserve_hl)
        fputs("\tpop hl\n", out);
    if (preserve_hl_de)
        fputs("\tpop hl\n\tpop de\n", out);
    return 1;
}

static int mir_emit_homed_constant_absolute_store(
    FILE *out, const struct MirInsn *insn)
{
    char operand[160];
    int instruction = (int)(insn - mir.insns);
    int preserve_hl_de = mir_home_color_live_across(
        instruction, MIR_COLOR_HL_DE);
    int preserve_hl = !preserve_hl_de &&
        mir.allocation_colors[insn->src2] != MIR_COLOR_HL &&
        mir_home_color_live_across(instruction, MIR_COLOR_HL);

    if (!mir_prepare_constant_absolute_operand(
            out, insn->src1, operand, sizeof(operand)))
        return 0;
    if (preserve_hl_de)
        fputs("\tpush de\n\tpush hl\n", out);
    if (preserve_hl)
        fputs("\tpush hl\n", out);
    if (!mir_emit_home_to_hl(out, insn->src2))
        return 0;
    if (insn->memory_size == 1)
        fprintf(out, "\tld a,l\n\tld (%s),a\n", operand);
    else
        fprintf(out, "\tld (%s),hl\n", operand);
    if (preserve_hl)
        fputs("\tpop hl\n", out);
    if (preserve_hl_de)
        fputs("\tpop hl\n\tpop de\n", out);
    return 1;
}

static int mir_homed_is_single_call_boolean_phi(void)
{
    int calls = 0;
    int branches = 0;
    int phis = 0;
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        if (mir.insns[instruction].opcode == MIR_CALL)
            ++calls;
        else if (mir.insns[instruction].opcode == MIR_BRANCH_FALSE)
            ++branches;
        else if (mir.insns[instruction].opcode == MIR_PHI)
            ++phis;
    }
    return calls == 1 && branches == 2 && phis != 0;
}

static int mir_homed_is_large_call_phi_cfg(void)
{
    int has_call = 0;
    int has_phi = 0;
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        has_call |= mir.insns[instruction].opcode == MIR_CALL;
        has_phi |= mir.insns[instruction].opcode == MIR_PHI;
    }
    return has_call && has_phi && mir_cfg_block_count() >= 16;
}

static int mir_homed_cfg_rematerializes_string_argument(void)
{
    int i;

    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_STRING_ADDRESS &&
            mir_homed_string_call_argument(mir.insns[i].dst))
            return 1;
    return 0;
}

int mir_try_emit_homed_scalar_cfg(FILE *out)
{
    int *labels;
    int uses_iy;
    int frameless;
    int return_count = 0;
    int last_insn_is_return;
    int shared_epilogue_label = -1;
    int i;
    int value;
    int accepted = 0;
    /* Wide integer values are retained only when the pair allocator finds a
     * zero-spill assignment. This first two-pair slice remains single-block
     * and helper-free; call clobbers and wide CFG copies are separate work. */
    int has_wide = 0;
    int wide_return = mir_homed_wide_type_supported(mir.return_type);

    mir_homed_cfg_used_unary_not_branch = 0;
    /* Phase 1 (mir-migration-plan-to-100pct.md), Item 8: a corpus-wide
     * zero-spill-fallback survey found "return-type" (base type != int)
     * is by far the single largest homed-scalar-cfg rejection cause
     * (997 of 1861 zero-spill functions surveyed). void is the safest
     * subset to add first: no calling-convention width change, no new
     * return-value register to track, and MIR_CALL's own void handling
     * below (skip storing a result to home when the callee's type is
     * void) already establishes the pattern MIR_RETURN reuses. */
    if (type_ptr_depth(mir.return_type) == 0 &&
        (mir.return_type & 15) != TYPE_VOID &&
        (mir.return_type & 15) != TYPE_INT &&
        !wide_return) {
        if (getenv("DCC_MIR_HOMED_REPORT") != NULL)
            fprintf(stderr,
                    "; MIR homed-return-type function=%s type=%d size=%d\n",
                    mir.name, mir.return_type, type_size(mir.return_type));
        return mir_homed_reject("return-type");
    }
    if (mir_homed_byte_indirect_count() > 0 &&
        (mir_cfg_block_count() > 1 || mir.count <= 20))
        return mir_homed_reject("byte-indirect-cost");
    if (mir.allocation_spill_count > 4)
        return mir_homed_reject("spill");
    for (value = 0; value < mir.next_value; ++value)
        if (mir_home_spill_offset(value, NULL)) {
            const struct MirInsn *definition = mir_definition(value);
            if (definition == NULL || type_size(definition->type) > 2)
                return mir_homed_reject("spill-type");
        }
    /* Address-taken locals and the bounded scalar spill slot share the
     * IX-relative frame. Keep both within the selector's signed-byte
     * addressing range. */
    if (mir_effective_local_bytes() +
            2 * mir.allocation_spill_count > 120)
        return mir_homed_reject("frame-size");
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->dst >= 0 && type_size(insn->type) > 2) {
            if (((insn->opcode == MIR_CONST ||
                  insn->opcode == MIR_FLOAT_CONST ||
                  insn->opcode == MIR_PARAM ||
                  insn->opcode == MIR_BINARY ||
                  insn->opcode == MIR_LOAD ||
                  insn->opcode == MIR_CALL) &&
                 mir_homed_wide_type_supported(insn->type)) ||
                (insn->opcode == MIR_LOAD_INDIRECT &&
                 mir_homed_wide_type_supported(insn->type)) ||
                (insn->opcode == MIR_UNARY &&
                 ((type_is_long(insn->type) &&
                   type_size(insn->type) == 4) ||
                  mir_float_identity_unary(insn) ||
                  mir_homed_float_cast_supported(insn))))
                has_wide = 1;
            else {
                if (getenv("DCC_MIR_HOMED_REPORT") != NULL)
                    fprintf(stderr,
                            "; MIR homed-wide-value function=%s opcode=%s "
                            "type=%d\n",
                            mir.name, mir_opcode_name(insn->opcode),
                            insn->type);
                return mir_homed_reject("wide-value");
            }
        }
        if (insn->opcode == MIR_BINARY &&
            type_size(insn->secondary_offset) > 2) {
            if (!mir_homed_wide_binary_supported(insn)) {
                if (getenv("DCC_MIR_HOMED_REPORT") != NULL)
                    fprintf(stderr,
                            "; MIR homed-wide-binary function=%s op=%ld "
                            "type=%d operand-type=%d blocks=%d\n",
                            mir.name, insn->immediate, insn->type,
                            insn->secondary_offset, mir_cfg_block_count());
                return mir_homed_reject("wide-binary");
            }
            has_wide = 1;
        }
        if (insn->opcode == MIR_PHI &&
            (mir_home_spill_offset(insn->dst, NULL) ||
             mir_home_spill_offset(insn->src1, NULL) ||
             mir_home_spill_offset(insn->src2, NULL)))
            return mir_homed_reject("spill-phi");
        if (insn->dst >= 0 && mir.allocation_colors[insn->dst] < 0 &&
            !mir_home_spill_offset(insn->dst, NULL) &&
            !mir_is_lazy_parameter(insn->dst))
            return mir_homed_reject("uncolored-value");
        switch (insn->opcode) {
        case MIR_NOP: case MIR_LABEL: case MIR_PARAM: case MIR_CONST:
        case MIR_FLOAT_CONST:
        case MIR_PHI: case MIR_JUMP: case MIR_BRANCH_FALSE:
            break;
        case MIR_STORE:
            if (!mir_object_is_fully_promoted(insn->object)) {
                int memory_type, memory_storage, memory_offset;
                const struct MirInsn *source = mir_definition(insn->src1);

                if (source == NULL ||
                    !mir_scalar_memory_location(insn, &memory_type,
                                                &memory_storage,
                                                &memory_offset))
                    return mir_homed_reject("store-location");
                if (memory_storage != SC_LOCAL &&
                    memory_storage != SC_PARAM &&
                    memory_storage != SC_GLOBAL &&
                    memory_storage != SC_EXTERN)
                    return mir_homed_reject("store-storage");
                if (memory_storage == SC_PARAM)
                    return mir_homed_reject("parameter-store");
                if (type_is_struct_object(memory_type) ||
                    (!((type_size(memory_type) == 1 ||
                        type_size(memory_type) == 2) &&
                       (type_size(source->type) == 1 ||
                        type_size(source->type) == 2)) &&
                     !(mir_homed_wide_type_supported(memory_type) &&
                       mir_homed_wide_type_supported(source->type) &&
                       type_is_float(memory_type) ==
                           type_is_float(source->type) &&
                       (memory_storage == SC_GLOBAL ||
                        memory_storage == SC_EXTERN))))
                    return mir_homed_reject("store-width");
                if ((memory_storage == SC_LOCAL ||
                     memory_storage == SC_PARAM) &&
                    (memory_offset < -128 ||
                     memory_offset + type_size(memory_type) - 1 > 127))
                    return mir_homed_reject("store-offset");
            }
            break;
        case MIR_ADDRESS:
            if (mir_value_only_used_by_absolute_access(
                    insn->dst,
                    mir_homed_constant_absolute_access_supported))
                break;
            {
                /* Item 16 (mir-migration-plan-to-100pct.md): address-of a
                 * scalar object, mirroring mir_scalar_memory_location's
                 * existing storage dispatch (same as the spilled-scalar-cfg
                 * selector's own MIR_ADDRESS case). VLA objects are
                 * excluded here - their address is itself loaded from a
                 * memory slot (ix-relative pointer read), a different
                 * shape not yet worth widening this selector for. */
                int memory_type, memory_storage, memory_offset;
                if (!mir_scalar_memory_location(insn, &memory_type,
                                                &memory_storage,
                                                &memory_offset))
                    return mir_homed_reject("address-location");
                if (memory_storage != SC_LOCAL && memory_storage != SC_PARAM &&
                    memory_storage != SC_GLOBAL &&
                    memory_storage != SC_EXTERN && memory_storage != SC_FUNC)
                    return mir_homed_reject("address-storage");
                if (mir_declared_is_vla_object(insn->name))
                    return mir_homed_reject("vla-address");
            }
            break;
        case MIR_STRING_ADDRESS:
            /* Item 21 (mir-migration-plan-to-100pct.md): the address of a
             * string literal is always a plain 2-byte pointer immediate
             * (assembler label `S<n>`) - no memory-storage dispatch or
             * width concern at all, unlike MIR_ADDRESS above, so there is
             * nothing further to validate here; emission reuses Item 16's
             * mir_emit_label_address_to_home exactly like MIR_ADDRESS's
             * global/extern/func case. */
            break;
        case MIR_MEMBER_ADDRESS:
            /* Item 22 (mir-migration-plan-to-100pct.md): a struct/union
             * member's address is always src1 (an already-homed pointer
             * value, not a memory location) plus a compile-time-constant
             * byte offset (insn->immediate) - the single biggest opcode
             * gap found by a fresh disposable-survey re-run after Item 21
             * (261 hits across the corpus). Unlike MIR_ADDRESS, there is
             * no memory-storage dispatch at all here: src1's color was
             * already validated when its own defining instruction was
             * visited (every value assigned a color earlier in the
             * function has already passed the generic dst-color check at
             * the top of this loop), so nothing further needs validating
             * before accepting. Emission is mir_emit_pointer_address_to_
             * home (a new, general "base value + constant offset" helper
             * shared with MIR_INDEX_ADDRESS's constant-index case below). */
            break;
        case MIR_INDEX_ADDRESS:
            /* Constant indexes fold into the member-style address helper.
             * Dynamic indexes are accepted only when the fixed stride clears
             * the shared bounded constant-multiply policy; runtime VLA
             * strides remain on the spilled path. */
            {
                const struct MirInsn *index_definition =
                    mir_definition(insn->src2);
                if (insn->base_name[0] != 0)
                    return mir_homed_reject("runtime-stride");
                if ((index_definition == NULL ||
                     index_definition->opcode != MIR_CONST) &&
                    !mir_mul_const_fast_path_eligible(
                        (unsigned long)insn->immediate & 0xffffUL,
                        insn->dst))
                    return mir_homed_reject("dynamic-index");
            }
            break;
        case MIR_LOAD_INDIRECT:
            /* Dereference an arbitrary homed pointer. Word, bitfield, and
             * wide loads share the spilled emitter's width rules. */
            if (!mir_homed_constant_absolute_access_supported(insn) &&
                (type_is_struct_object(insn->type) ||
                (insn->bit_width > 0 && insn->memory_size != 2) ||
                !((type_size(insn->type) == 1 &&
                   insn->memory_size == 1) ||
                  (type_size(insn->type) == 2 &&
                   (insn->memory_size == 0 || insn->memory_size == 2)) ||
                  (mir_homed_wide_type_supported(insn->type) &&
                   insn->memory_size == 4))))
                return mir_homed_reject("indirect-load-type");
            break;
        case MIR_STORE_INDIRECT:
            /* Write through an arbitrary homed pointer. Word, bitfield, and
             * wide stores mirror the spilled backend's width rules. */
            if (!mir_homed_constant_absolute_access_supported(insn) &&
                ((insn->bit_width > 0 && insn->memory_size != 2) ||
                !((insn->memory_size == 0 || insn->memory_size == 1 ||
                   insn->memory_size == 2) ||
                  (insn->memory_size == 4 &&
                   mir_definition(insn->src2) != NULL &&
                   mir_homed_wide_type_supported(
                       mir_definition(insn->src2)->type)))))
                return mir_homed_reject("indirect-store-type");
            break;
        case MIR_COPY_AGGREGATE:
            /* Item 24 (mir-migration-plan-to-100pct.md): struct/union
             * assignment by value between two already-homed pointer
             * values (dst/src addresses), mirroring the byte-copy loop
             * mir_try_emit_spilled_scalar_cfg already uses (ld a,(bc)/
             * ld (hl),a, walking both pointers in lockstep via bc/hl).
             * Same size cap as that selector's own case (1..1024 bytes)
             * to bound the emitted instruction stream; zero-size or
             * negative-size aggregates are not valid C and are rejected
             * defensively. */
            if (insn->memory_size <= 0 || insn->memory_size > 1024)
                return mir_homed_reject("aggregate-copy-size");
            break;
        case MIR_LOAD:
            {
                /* Item 9 (mir-migration-plan-to-100pct.md): the "opcode-load"
                 * fallback bucket found by Item 8's groundwork survey is
                 * mostly reads of globals or non-promoted (address-taken,
                 * aliased, or too-large-to-register-fully) locals/params.
                 * Only the narrowest, unambiguous slice is accepted here:
                 * a plain 2-byte scalar whose storage type is exactly the
                 * loaded value's type (no implicit sign/zero-extension or
                 * bool normalization needed), read from a local, parameter,
                 * or global/extern/func-linkage location. 1-byte (char) and
                 * mismatched-width loads are deliberately deferred - they
                 * need the same sign/zero-extend and bool-normalization
                 * logic mir_try_emit_spilled_scalar_cfg's MIR_LOAD case
                 * carries, which is real but separate scope creep from this
                 * narrow first step. */
                int memory_type, memory_storage, memory_offset;
                if (!mir_scalar_memory_location(insn, &memory_type,
                                                &memory_storage,
                                                &memory_offset))
                    return mir_homed_reject("load-location");
                if (memory_storage != SC_LOCAL && memory_storage != SC_PARAM &&
                    memory_storage != SC_GLOBAL &&
                    memory_storage != SC_EXTERN && memory_storage != SC_FUNC)
                    return mir_homed_reject("load-storage");
                if (type_is_struct_object(memory_type) ||
                    type_is_struct_object(insn->type))
                    return mir_homed_reject("aggregate-load");
                if (!((type_size(memory_type) == 1 ||
                       type_size(memory_type) == 2) &&
                      type_size(insn->type) == type_size(memory_type)) &&
                    !(mir_homed_wide_type_supported(memory_type) &&
                      mir_homed_wide_type_supported(insn->type) &&
                      type_is_float(memory_type) ==
                          type_is_float(insn->type) &&
                      (memory_storage == SC_GLOBAL ||
                       memory_storage == SC_EXTERN)))
                    return mir_homed_reject("load-width");
                if (mir_general_comparison_count() > 1 &&
                    (mir_has_phi_instruction() ||
                     mir_cfg_block_count() > 18))
                    return mir_homed_reject("load-comparison-cfg");
            }
            break;
        case MIR_UNARY:
            {
                const struct MirInsn *source = mir_definition(insn->src1);
                int source_type = source != NULL ? source->type : 0;
                int source_wide = type_size(source_type) == 4;
                int target_wide = type_size(insn->type) == 4;
                if ((source_wide || target_wide) &&
                    (mir_cfg_block_count() != 1 ||
                     ((type_is_float(source_type) ||
                        type_is_float(insn->type)) &&
                       !mir_float_identity_unary(insn) &&
                       !mir_homed_float_cast_supported(insn)) ||
                     (insn->immediate != 0 &&
                       !mir_float_identity_unary(insn) &&
                       !(source_wide && target_wide &&
                        (insn->immediate == '+' ||
                         insn->immediate == '-' ||
                         insn->immediate == '~')) &&
                      !(source_wide && !target_wide &&
                        insn->immediate == '!')))) {
                    if (getenv("DCC_MIR_HOMED_REPORT") != NULL)
                        fprintf(stderr,
                                "; MIR homed-wide-unary function=%s op=%ld "
                                "source-type=%d target-type=%d blocks=%d\n",
                                mir.name, insn->immediate, source_type,
                                insn->type, mir_cfg_block_count());
                    return mir_homed_reject("wide-unary");
                }
                if (source_wide || target_wide)
                    has_wide = 1;
            }
            if (insn->immediate != 0 && insn->immediate != '+' &&
                insn->immediate != '-' && insn->immediate != '~' &&
                insn->immediate != '!')
                return mir_homed_reject("unary-op");
            break;
        case MIR_BINARY:
            {
                int operation;
                long count;
            /* Wide operations were fully validated by
             * mir_homed_wide_binary_supported() in the value pass above.
             * The remaining whitelist and constant-strength checks describe
             * only the narrow emitter. */
            if (type_size(insn->secondary_offset) > 2)
                break;
            if (insn->immediate != '+' && insn->immediate != '-' &&
                insn->immediate != '&' && insn->immediate != '|' &&
                insn->immediate != '^' && insn->immediate != TOK_EQ &&
                insn->immediate != TOK_NE && insn->immediate != '<' &&
                insn->immediate != '>' && insn->immediate != TOK_LE &&
                insn->immediate != TOK_GE &&
                !(type_size(insn->secondary_offset) <= 2 &&
                  ((insn->immediate == '*' &&
                    (mir_definition(insn->src1) == NULL ||
                     mir_definition(insn->src1)->opcode != MIR_CONST)) ||
                   insn->immediate == '/' || insn->immediate == '%')) &&
                !mir_homed_constant_binary(insn, &operation, &count)) {
                if (getenv("DCC_MIR_HOMED_REPORT") != NULL)
                {
                    const struct MirInsn *right =
                        mir_definition(insn->src2);
                    fprintf(stderr,
                            "; MIR homed-binary-op function=%s op=%ld "
                            "type=%d operand-type=%d right-op=%s "
                            "right-imm=%ld\n",
                            mir.name, insn->immediate, insn->type,
                            insn->secondary_offset,
                            right != NULL ? mir_opcode_name(right->opcode)
                                          : "none",
                            right != NULL ? right->immediate : 0);
                }
                return mir_homed_reject("binary-op");
            }
            }
            break;
        case MIR_RETURN:
            ++return_count;
            break;
        case MIR_ARG:
            if (type_is_struct_object(insn->type) ||
                (type_size(insn->type) > 2 &&
                 !mir_homed_wide_type_supported(insn->type)))
                return mir_homed_reject("argument-type");
            break;
        case MIR_CALL:
            {
                struct Sym *callee = find_global(insn->name);
                int is_indirect = strcmp(insn->name, "<indirect>") == 0;
                /* Phase 1 (mir-migration-plan-to-100pct.md): a defined-in-TU
                 * callee was previously required, on the theory that only
                 * mir_emit_home_prologue/epilogue's own push/pop iy could be
                 * trusted to preserve a caller's IY. That is stricter than
                 * the invariant the rest of the compiler already relies on
                 * (dcc.h's REG_IY comment, verified by
                 * scripts/rtl-iy-safety.py): IY is CALLEE-SAVED across *any*
                 * call, defined or not - DCCRTL contains no IY instruction
                 * at all, and CP/M's 8080-coded BDOS has no index registers
                 * to write with, so nothing reachable from an ordinary call
                 * can clobber it. This is exactly the same guarantee
                 * function_qualifies_for_speculative_iy_regalloc
                 * (dcc_regalloc.c) already leans on for the legacy backend,
                 * which claims IY for any call-containing function without
                 * distinguishing defined-in-TU calls from library calls.
                 * Only an indirect call (whose target isn't known at
                 * compile time, so it can't be proven to be dcc-compiled or
                 * part of DCCRTL/BDOS) remains excluded here. */
                if (is_indirect || callee == NULL)
                    return mir_homed_reject("call-target");
                if ((insn->memory_flags & MIR_CALL_FLAG_FORMAT_RUNTIME) != 0)
                    return mir_homed_reject("format-runtime");
            }
            break;
        default:
            if (getenv("DCC_MIR_HOMED_REPORT") != NULL)
                fprintf(stderr,
                        "; MIR homed-opcode function=%s instruction=%d "
                        "opcode=%s\n",
                        mir.name, i,
                        mir_opcode_name(insn->opcode));
            return mir_homed_reject("opcode");
        }
    }
    /* A void function may legitimately fall off the end with no explicit
     * "return;" at all - mir_try_emit_spilled_scalar_cfg's own preflight
     * (the "implicit-return" reject reason) already treats this as valid
     * only for TYPE_VOID; mirror that here instead of requiring at least
     * one MIR_RETURN unconditionally. */
    if (return_count == 0 && (mir.return_type & 15) != TYPE_VOID)
        return mir_homed_reject("missing-return");
    /* A one-call short-circuit boolean that merges through a phi measured
     * slower after peephole optimization even though MIR removed six
     * instructions. Keep this distinct call/branch/merge class on the
     * established backend; call-heavier boolean phis have separate measured
     * wins and are intentionally unaffected. */
    if (mir_homed_is_single_call_boolean_phi())
        return mir_homed_reject("single-call-boolean-phi");
    /* Large call/phi CFGs exposed by the corrected block-boundary scan saved
     * instructions before peephole optimization but increased shipping code
     * size and cycles. Keep that high-interaction class on the established
     * backend; smaller call/phi CFGs retain their existing measured wins. */
    if (mir_homed_is_large_call_phi_cfg())
        return mir_homed_reject("large-call-phi-cfg");
    /* Item 20d: a wide long return with no wide value at all (e.g. an
     * implicit-int-promoted narrow expression) still needs the probe, so
     * gate on wide_return too, not just has_wide. */
    if (has_wide || wide_return) {
        unsigned char *rematerializable = NULL;
        int wide_colors_ok;

        if (mir_rematerialized_home_allocation_is_active()) {
            rematerializable = (unsigned char *)calloc(
                (size_t)mir.next_value, 1);
            if (rematerializable == NULL)
                fatal("out of memory planning rematerializable MIR homes");
            for (value = 0; value < mir.next_value; ++value)
                rematerializable[value] =
                    (unsigned char)mir_homed_value_is_rematerializable(value);
        }
        wide_colors_ok =
            mir_probe_wide_colors_for_homed(rematerializable);
        free(rematerializable);
        if (!wide_colors_ok)
            return mir_homed_reject("wide-color");
    }
    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].dst >= 0 &&
            mir_homed_value_is_rematerializable(mir.insns[i].dst))
            mir.allocation_colors[mir.insns[i].dst] = -1;

    labels = (int *)malloc((size_t)mir.next_label * sizeof(*labels));
    if (labels == NULL)
        fatal("out of memory selecting homed MIR CFG labels");
    for (i = 0; i < mir.next_label; ++i)
        labels[i] = new_label();

    uses_iy = mir_home_uses_iy();
    frameless = !uses_iy && mir_effective_local_bytes() == 0 &&
                mir.allocation_spill_count == 0 &&
                !mir_homed_requires_ix_frame();
    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_PARAM &&
            (mir.insns[i].object < 0 ||
             (type_size(mir.objects[mir.insns[i].object].type) != 1 &&
              type_size(mir.objects[mir.insns[i].object].type) != 2 &&
              type_size(mir.objects[mir.insns[i].object].type) != 4))) {
            free(labels);
            return 0;
        }
    /* mir-text-size Item T14: mirror dcc_mir_spilled_cfg.c's shared-
     * epilogue optimization - a function with more than one MIR_RETURN
     * only needs the real epilogue text once; every other return can
     * `jp` to it instead of duplicating ix/iy restore + ret. */
    last_insn_is_return =
        mir.count > 0 && mir.insns[mir.count - 1].opcode == MIR_RETURN;
    if (frameless) {
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
    } else {
        mir_emit_home_prologue(out, uses_iy);
    }

    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        const struct MirObject *object;
        int target;
        int true_label;
        int preserve_hl;

        switch (insn->opcode) {
        case MIR_NOP: case MIR_PHI:
            break;
        case MIR_STORE:
            if (!mir_object_is_fully_promoted(insn->object)) {
                int memory_type, memory_storage, memory_offset;
                int instruction = (int)(insn - mir.insns);
                int preserve_hl_de;

                if (!mir_scalar_memory_location(insn, &memory_type,
                                                &memory_storage,
                                                &memory_offset))
                    goto done;
                if (type_size(memory_type) == 4) {
                    struct Sym *global = find_global(insn->name);
                    const char *assembly_name = asm_name_for(
                        global != NULL ? sym_asm_name(global)
                                       : mir_declared_link_name(insn->name));
                    if ((memory_storage == SC_EXTERN ||
                         (global != NULL && global->needs_extrn)) &&
                        mir_extrn_should_emit(global))
                        fprintf(out, "\textrn %s\n", assembly_name);
                    if (!mir_emit_homed_wide_named_store(
                            out, insn, assembly_name))
                        goto done;
                    break;
                }
                memory_offset = mir_homed_frame_offset(
                    memory_storage, memory_offset, uses_iy);
                preserve_hl_de = mir_home_color_live_across(
                    instruction, MIR_COLOR_HL_DE);
                preserve_hl = !preserve_hl_de &&
                    mir.allocation_colors[insn->src1] != MIR_COLOR_HL &&
                    mir_home_color_live_across(instruction, MIR_COLOR_HL);
                if (preserve_hl_de)
                    fputs("\tpush de\n\tpush hl\n", out);
                if (preserve_hl)
                    fputs("\tpush hl\n", out);
                if (!mir_emit_home_to_hl(out, insn->src1))
                    goto done;
                if (memory_storage == SC_LOCAL ||
                    memory_storage == SC_PARAM) {
                    fprintf(out, "\tld (ix%+d),l\n", memory_offset);
                    if (type_size(memory_type) == 2)
                        fprintf(out, "\tld (ix%+d),h\n",
                                memory_offset + 1);
                } else {
                    struct Sym *global = find_global(insn->name);
                    const char *assembly_name = asm_name_for(
                        global != NULL ? sym_asm_name(global)
                                       : mir_declared_link_name(insn->name));
                    if ((memory_storage == SC_EXTERN ||
                         (global != NULL && global->needs_extrn)) &&
                        mir_extrn_should_emit(global))
                        fprintf(out, "\textrn %s\n", assembly_name);
                    if (type_size(memory_type) == 1)
                        fprintf(out, "\tld a,l\n\tld (%s),a\n",
                                assembly_name);
                    else
                        fprintf(out, "\tld (%s),hl\n", assembly_name);
                }
                if (preserve_hl)
                    fputs("\tpop hl\n", out);
                if (preserve_hl_de)
                    fputs("\tpop hl\n\tpop de\n", out);
            }
            break;
        case MIR_LOAD:
            {
                int memory_type, memory_storage, memory_offset;
                int instruction = (int)(insn - mir.insns);
                int preserve_hl_de;

                if (!mir_scalar_memory_location(insn, &memory_type,
                                                &memory_storage,
                                                &memory_offset))
                    goto done;
                if (type_size(memory_type) == 4) {
                    struct Sym *global = find_global(insn->name);
                    const char *assembly_name = asm_name_for(
                        global != NULL ? sym_asm_name(global)
                                       : mir_declared_link_name(insn->name));
                    if ((memory_storage == SC_EXTERN ||
                         (global != NULL && global->needs_extrn)) &&
                        mir_extrn_should_emit(global))
                        fprintf(out, "\textrn %s\n", assembly_name);
                    if (!mir_emit_homed_wide_named_load(
                            out, insn, assembly_name))
                        goto done;
                    break;
                }
                memory_offset = mir_homed_frame_offset(
                    memory_storage, memory_offset, uses_iy);
                preserve_hl_de = mir_home_color_live_across(
                    instruction, MIR_COLOR_HL_DE);
                preserve_hl = !preserve_hl_de &&
                    mir.allocation_colors[insn->dst] != MIR_COLOR_HL &&
                    mir_home_color_live_across(instruction, MIR_COLOR_HL);
                if (preserve_hl_de)
                    fputs("\tpush de\n\tpush hl\n", out);
                if (preserve_hl)
                    fputs("\tpush hl\n", out);
                if (memory_storage == SC_LOCAL ||
                    memory_storage == SC_PARAM) {
                    if (type_size(memory_type) == 1) {
                        fprintf(out, "\tld l,(ix%+d)\n", memory_offset);
                    } else if (memory_offset >= -128 &&
                        memory_offset + 1 <= 127) {
                        fprintf(out, "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
                                memory_offset, memory_offset + 1);
                    } else {
                        fputs("\tpush ix\n\tpop hl\n", out);
                        fprintf(out,
                                "\tld de,%d\n\tadd hl,de\n"
                                "\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n"
                                "\tld l,a\n",
                                memory_offset);
                    }
                } else {
                    struct Sym *global = find_global(insn->name);
                    const char *assembly_name = asm_name_for(
                        global != NULL ? sym_asm_name(global)
                                       : mir_declared_link_name(insn->name));
                    if ((memory_storage == SC_EXTERN ||
                         (memory_storage == SC_FUNC && global != NULL &&
                          global->needs_extrn)) &&
                        mir_extrn_should_emit(global))
                        fprintf(out, "\textrn %s\n", assembly_name);
                    if (type_size(memory_type) == 1)
                        fprintf(out, "\tld a,(%s)\n\tld l,a\n",
                                assembly_name);
                    else
                        fprintf(out, "\tld hl,(%s)\n", assembly_name);
                }
                if (type_size(memory_type) == 1) {
                    if (type_is_bool(memory_type)) {
                        int end_label = new_label();
                        fputs("\tld a,l\n\tor a\n\tld hl,0\n", out);
                        fprintf(out, "\tjp z, L%d\n\tinc hl\nL%d:\n",
                                end_label, end_label);
                    } else if ((memory_type & TYPE_UNSIGNED) != 0) {
                        fputs("\tld h,0\n", out);
                    } else {
                        mir_emit_signed_byte_extend(out);
                    }
                }
                if (!mir_emit_hl_to_home(out, insn->dst))
                    goto done;
                if (preserve_hl)
                    fputs("\tpop hl\n", out);
                if (preserve_hl_de)
                    fputs("\tpop hl\n\tpop de\n", out);
            }
            break;
        case MIR_ADDRESS:
            {
                /* Item 16 (mir-migration-plan-to-100pct.md, re-adopting
                 * Item 14 now that Item 15's memset fastcall removes the
                 * MIR_CALL cost gap that caused Item 14's regression):
                 * compute the address directly into the destination's own
                 * home color wherever possible, so this never has to
                 * route through HL/DE as scratch and risk clobbering
                 * another still-live homed value (the bug Item 14's first
                 * attempt hit). Only the non-zero-offset ix-relative case
                 * still needs HL/DE scratch, handled conservatively by
                 * mir_emit_ix_offset_address_to_home. */
                int memory_type, memory_storage, memory_offset;
                struct Sym *global = find_global(insn->name);
                if (!mir_scalar_memory_location(insn, &memory_type,
                                                &memory_storage,
                                                &memory_offset))
                    goto done;
                memory_offset = mir_homed_frame_offset(
                    memory_storage, memory_offset, uses_iy);
                if ((global != NULL && global->storage == SC_FUNC) ||
                    memory_storage == SC_GLOBAL ||
                    memory_storage == SC_EXTERN ||
                    memory_storage == SC_FUNC) {
                    const char *assembly_name = asm_name_for(
                        global != NULL ? sym_asm_name(global)
                                       : mir_declared_link_name(insn->name));
                    if ((memory_storage == SC_EXTERN ||
                         (global != NULL && global->storage == SC_FUNC &&
                          global->needs_extrn)) &&
                        mir_extrn_should_emit(global))
                        fprintf(out, "\textrn %s\n", assembly_name);
                    if (!mir_emit_label_address_to_home(out, insn->dst,
                                                        assembly_name))
                        goto done;
                } else {
                    if (!mir_emit_ix_offset_address_to_home(out, insn->dst,
                                                            memory_offset))
                        goto done;
                }
            }
            break;
        case MIR_STRING_ADDRESS:
            {
                char label[32];
                if (mir_homed_string_call_argument(insn->dst))
                    break;
                sprintf(label, "S%ld", insn->immediate);
                if (!mir_emit_label_address_to_home(out, insn->dst, label))
                    goto done;
            }
            break;
        case MIR_MEMBER_ADDRESS:
            if (mir_value_only_used_by_absolute_access(
                    insn->dst,
                    mir_homed_constant_absolute_access_supported))
                break;
            if (!mir_emit_pointer_offset_address_to_home(
                    out, insn->dst, insn->src1, insn->immediate))
                goto done;
            break;
        case MIR_INDEX_ADDRESS:
            {
                const struct MirInsn *index_definition =
                    mir_definition(insn->src2);
                long byte_offset;
                if (mir_value_only_used_by_absolute_access(
                        insn->dst,
                        mir_homed_constant_absolute_access_supported))
                    break;
                if (index_definition != NULL &&
                    index_definition->opcode == MIR_CONST) {
                    byte_offset =
                        index_definition->immediate * insn->immediate;
                    if (!mir_emit_pointer_offset_address_to_home(
                            out, insn->dst, insn->src1, byte_offset))
                        goto done;
                } else {
                    int instruction = (int)(insn - mir.insns);
                    int dst_color = mir.allocation_colors[insn->dst];
                    int preserve_hl_de =
                        mir_home_color_live_across(
                            instruction, MIR_COLOR_HL_DE);
                    int preserve_hl =
                        !preserve_hl_de &&
                        dst_color != MIR_COLOR_HL &&
                        mir_home_color_live_across(
                            instruction, MIR_COLOR_HL);
                    int preserve_de =
                        !preserve_hl_de &&
                        dst_color != MIR_COLOR_DE &&
                        mir_home_color_live_across(
                            instruction, MIR_COLOR_DE);
                    if (preserve_hl_de)
                        fputs("\tpush de\n\tpush hl\n", out);
                    if (preserve_hl) fputs("\tpush hl\n", out);
                    if (preserve_de) fputs("\tpush de\n", out);
                    if (!mir_emit_home_push(out, insn->src1))
                        goto done;
                    if (!mir_emit_home_to_hl(out, insn->src2))
                        goto done;
                    if (insn->immediate != 1)
                        mir_emit_mul_hl_const(
                            out, (unsigned long)insn->immediate & 0xffffUL);
                    fputs("\tpop de\n\tadd hl,de\n", out);
                    if (dst_color != MIR_COLOR_HL &&
                        !mir_emit_hl_to_home(out, insn->dst))
                        goto done;
                    if (preserve_de) fputs("\tpop de\n", out);
                    if (preserve_hl) fputs("\tpop hl\n", out);
                    if (preserve_hl_de)
                        fputs("\tpop hl\n\tpop de\n", out);
                }
            }
            break;
        case MIR_LOAD_INDIRECT:
            {
                int instruction;
                int preserve_hl_de;
                int preserve_hl;
                if ((insn->memory_size == 1 || insn->memory_size == 2) &&
                    mir_homed_constant_absolute_access_supported(insn)) {
                    if (!mir_emit_homed_constant_absolute_load(out, insn))
                        goto done;
                    break;
                }
                if (insn->memory_size == 4) {
                    if (!mir_emit_homed_wide_indirect_load(out, insn))
                        goto done;
                    break;
                }
                instruction = (int)(insn - mir.insns);
                preserve_hl_de =
                    mir_home_color_live_across(
                        instruction, MIR_COLOR_HL_DE);
                preserve_hl =
                    !preserve_hl_de &&
                    mir.allocation_colors[insn->dst] != MIR_COLOR_HL &&
                    mir_home_color_live_across(instruction, MIR_COLOR_HL);
                if (preserve_hl_de)
                    fputs("\tpush de\n\tpush hl\n", out);
                if (preserve_hl)
                    fputs("\tpush hl\n", out);
                if (!mir_emit_home_to_hl(out, insn->src1))
                    goto done;
                if (insn->memory_size == 1) {
                    fputs("\tld l,(hl)\n", out);
                    if (type_is_bool(insn->type)) {
                        int end_label = new_label();
                        fputs("\tld a,l\n\tor a\n\tld hl,0\n", out);
                        fprintf(out, "\tjp z, L%d\n\tinc hl\nL%d:\n",
                                end_label, end_label);
                    } else if ((insn->type & TYPE_UNSIGNED) != 0) {
                        fputs("\tld h,0\n", out);
                    } else {
                        mir_emit_signed_byte_extend(out);
                    }
                } else {
                    fputs("\tld a,(hl)\n\tinc hl\n"
                          "\tld h,(hl)\n\tld l,a\n", out);
                }
                if (insn->bit_width > 0)
                    mir_emit_bitfield_extract(out, insn);
                if (mir.allocation_colors[insn->dst] != MIR_COLOR_HL &&
                    !mir_emit_hl_to_home(out, insn->dst))
                    goto done;
                if (preserve_hl) fputs("\tpop hl\n", out);
                if (preserve_hl_de) fputs("\tpop hl\n\tpop de\n", out);
            }
            break;
        case MIR_STORE_INDIRECT:
            {
                int instruction;
                int preserve_hl_de;
                int preserve_hl;
                int preserve_de;
                if ((insn->memory_size == 1 || insn->memory_size == 2) &&
                    mir_homed_constant_absolute_access_supported(insn)) {
                    if (!mir_emit_homed_constant_absolute_store(out, insn))
                        goto done;
                    break;
                }
                if (insn->memory_size == 4) {
                    if (!mir_emit_homed_wide_indirect_store(out, insn))
                        goto done;
                    break;
                }
                if (insn->bit_width > 0) {
                    int shift;
                    int instruction = (int)(insn - mir.insns);
                    int preserve_hl_de = mir_home_color_live_across(
                        instruction, MIR_COLOR_HL_DE);
                    int preserve_bc_iy = mir_home_color_live_across(
                        instruction, MIR_COLOR_BC_IY);
                    int preserve_hl = !preserve_hl_de &&
                        mir_home_color_live_across(
                            instruction, MIR_COLOR_HL);
                    int preserve_de = !preserve_hl_de &&
                        mir_home_color_live_across(
                            instruction, MIR_COLOR_DE);
                    int preserve_bc = !preserve_bc_iy &&
                        mir_home_color_live_across(
                            instruction, MIR_COLOR_BC);
                    if (preserve_hl_de)
                        fputs("\tpush de\n\tpush hl\n", out);
                    if (preserve_bc_iy)
                        fputs("\tpush iy\n\tpush bc\n", out);
                    if (preserve_hl) fputs("\tpush hl\n", out);
                    if (preserve_de) fputs("\tpush de\n", out);
                    if (preserve_bc) fputs("\tpush bc\n", out);
                    if (!mir_emit_home_to_hl(out, insn->src2))
                        goto done;
                    mir_emit_hl_and_const(
                        out, insn->bit_width >= 16
                            ? 0xffffU : (1U << insn->bit_width) - 1U);
                    for (shift = 0; shift < insn->bit_shift; ++shift)
                        fputs("\tadd hl,hl\n", out);
                    mir_emit_hl_and_const(out, insn->bit_mask);
                    fputs("\tpush hl\n", out);
                    if (!mir_emit_home_to_hl(out, insn->src1))
                        goto done;
                    fputs("\tpush hl\n\tld a,(hl)\n\tinc hl\n"
                          "\tld h,(hl)\n\tld l,a\n", out);
                    mir_emit_hl_and_const(
                        out, (~insn->bit_mask) & 0xffffU);
                    fputs("\tpop bc\n\tpop de\n"
                          "\tld a,l\n\tor e\n\tld l,a\n"
                          "\tld a,h\n\tor d\n\tld h,a\n"
                          "\tex de,hl\n\tld h,b\n\tld l,c\n"
                          "\tld (hl),e\n\tinc hl\n\tld (hl),d\n", out);
                    if (preserve_bc) fputs("\tpop bc\n", out);
                    if (preserve_de) fputs("\tpop de\n", out);
                    if (preserve_hl) fputs("\tpop hl\n", out);
                    if (preserve_bc_iy)
                        fputs("\tpop bc\n\tpop iy\n", out);
                    if (preserve_hl_de)
                        fputs("\tpop hl\n\tpop de\n", out);
                    break;
                }
                /* Item 23: preserve any OTHER value still live in hl/de
                 * across this instruction (src1/src2's own colors are
                 * naturally excluded by mir_home_color_live_across, since
                 * their own consumption here doesn't count as a "later"
                 * use - see that helper's comment) - both registers are
                 * used unconditionally as scratch below regardless of
                 * src1/src2's actual home colors, mirroring the
                 * spilled-scalar-cfg selector's own two-hl-loads-then-ex
                 * dance for combining an address and a value that may
                 * both be homed anywhere. */
                instruction = (int)(insn - mir.insns);
                preserve_hl_de = mir_home_color_live_across(
                    instruction, MIR_COLOR_HL_DE);
                preserve_hl = !preserve_hl_de &&
                    mir_home_color_live_across(instruction, MIR_COLOR_HL);
                preserve_de = !preserve_hl_de &&
                    mir_home_color_live_across(instruction, MIR_COLOR_DE);
                if (preserve_hl_de)
                    fputs("\tpush de\n\tpush hl\n", out);
                if (preserve_hl) fputs("\tpush hl\n", out);
                if (preserve_de) fputs("\tpush de\n", out);
                if (mir.allocation_colors[insn->src2] == MIR_COLOR_HL) {
                    /* Loading the address into HL first would destroy a
                     * value already homed there. Preserve the value before
                     * materializing the address, then arrange address/value
                     * in HL/DE without rereading either home. */
                    fputs("\tpush hl\n", out);
                    if (!mir_emit_home_to_hl(out, insn->src1))
                        goto done;
                    fputs("\tex de,hl\n\tpop hl\n\tex de,hl\n", out);
                } else {
                    if (!mir_emit_home_to_hl(out, insn->src1))
                        goto done;
                    fputs("\tpush hl\n", out);
                    if (!mir_emit_home_to_hl(out, insn->src2))
                        goto done;
                    fputs("\tex de,hl\n\tpop hl\n", out);
                }
                fputs("\tld (hl),e\n", out);
                if (insn->memory_size != 1)
                    fputs("\tinc hl\n\tld (hl),d\n", out);
                if (preserve_de) fputs("\tpop de\n", out);
                if (preserve_hl) fputs("\tpop hl\n", out);
                if (preserve_hl_de) fputs("\tpop hl\n\tpop de\n", out);
            }
            break;
        case MIR_COPY_AGGREGATE:
            {
                /* Item 24: struct/union assignment by value between two
                 * already-homed pointer addresses. Both hl and bc are
                 * used unconditionally as scratch below regardless of
                 * src1/src2's actual home colors, so protect any OTHER
                 * value still live there across this instruction the
                 * same way Item 23 protects hl/de for MIR_STORE_INDIRECT.
                 *
                 * mir-text-size Item T6: src2 (source) lands in HL
                 * directly from mir_emit_home_to_hl - exactly what
                 * `ldir` needs - so pop the saved destination straight
                 * into DE and copy with `ldir` instead of the old
                 * unrolled byte-by-byte loop (same fix shape as
                 * mir_try_emit_spilled_scalar_cfg's own
                 * MIR_COPY_AGGREGATE case, Item T5/T6). */
                int instruction = (int)(insn - mir.insns);
                 int preserve_hl_de = mir_home_color_live_across(
                     instruction, MIR_COLOR_HL_DE);
                 int preserve_bc_iy = mir_home_color_live_across(
                     instruction, MIR_COLOR_BC_IY);
                 int preserve_hl = !preserve_hl_de &&
                     mir_home_color_live_across(instruction, MIR_COLOR_HL);
                 int preserve_de = !preserve_hl_de &&
                     mir_home_color_live_across(instruction, MIR_COLOR_DE);
                 int preserve_bc = !preserve_bc_iy &&
                     mir_home_color_live_across(instruction, MIR_COLOR_BC);
                 if (preserve_hl_de)
                     fputs("\tpush de\n\tpush hl\n", out);
                 if (preserve_bc_iy)
                     fputs("\tpush iy\n\tpush bc\n", out);
                 if (preserve_hl) fputs("\tpush hl\n", out);
                 if (preserve_de) fputs("\tpush de\n", out);
                 if (preserve_bc) fputs("\tpush bc\n", out);
                if (!mir_emit_home_to_hl(out, insn->src1))
                    goto done;
                fputs("\tpush hl\n", out);
                if (!mir_emit_home_to_hl(out, insn->src2))
                    goto done;
                fputs("\tpop de\n", out);
                if (insn->memory_size > 0)
                    fprintf(out, "\tld bc,%d\n\tldir\n", insn->memory_size);
                if (preserve_bc) fputs("\tpop bc\n", out);
                if (preserve_de) fputs("\tpop de\n", out);
                if (preserve_hl) fputs("\tpop hl\n", out);
                if (preserve_bc_iy)
                    fputs("\tpop bc\n\tpop iy\n", out);
                if (preserve_hl_de)
                    fputs("\tpop hl\n\tpop de\n", out);
            }
            break;
        case MIR_LABEL:
            if (insn->label < 0 || insn->label >= mir.next_label)
                goto done;
            /* Item T61 (mir-text-size-plan.md): skip printing a label
             * nothing ever jumps to - see mir_label_is_jump_target's
             * comment in dcc_mir.c for why this is always safe. */
            if (mir_label_is_jump_target(insn->label))
                fprintf(out, "L%d:\n", labels[insn->label]);
            break;
        case MIR_PARAM:
            if (!mir_value_has_use(insn->dst))
                break;
            if (mir_is_lazy_parameter(insn->dst))
                break;
            object = &mir.objects[insn->object];
            if (type_size(object->type) == 4) {
                int offset = object->offset + (uses_iy ? 2 : 0);
                if (mir.allocation_colors[insn->dst] == MIR_COLOR_HL_DE)
                    fprintf(out,
                            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
                            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
                            offset, offset + 1, offset + 2, offset + 3);
                else if (mir.allocation_colors[insn->dst] ==
                         MIR_COLOR_BC_IY) {
                    fputs("\tpush hl\n", out);
                    fprintf(out,
                            "\tld c,(ix%+d)\n\tld b,(ix%+d)\n"
                            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
                            offset, offset + 1, offset + 2, offset + 3);
                    fputs("\tpush hl\n\tpop iy\n\tpop hl\n", out);
                } else {
                    goto done;
                }
            } else if (!(type_size(object->type) == 1
                  ? (frameless
                     ? mir_emit_stack_byte_param_to_home(
                           out, insn->dst, object->offset, object->type)
                     : mir_emit_byte_param_to_home(
                           out, insn->dst,
                           object->offset + (uses_iy ? 2 : 0), object->type))
                  : (frameless
                     ? mir_emit_stack_word_param_to_home(
                           out, insn->dst, object->offset)
                     : mir_emit_word_param_to_home(
                           out, insn->dst,
                           object->offset + (uses_iy ? 2 : 0)))))
                goto done;
            break;
        case MIR_CONST:
            /* mir-text-size Item T19: skip materializing a constant whose
             * sole use is the dead index-address shape described above
             * mir_index_only_constant's definition. Mirrors MIR_UNARY's
             * own dead-result skip just below (Item T12) and
             * dcc_mir_spilled_cfg.c's identical MIR_CONST check (Item
             * T18). */
            if (mir_index_only_constant(insn->dst) ||
                mir_homed_binary_only_constant(insn->dst) ||
                mir_homed_wide_binary_only_constant(insn->dst))
                break;
            /* Item 20d: dst may be wide (long) only if mir_probe_wide_
             * colors_for_homed accepted this function - dispatch on the
             * value's own type rather than a separate has_wide flag so
             * this stays correct even if more wide-eligible opcodes are
             * added above later. */
            if (mir_homed_wide_type_supported(insn->type)) {
                if (!mir_emit_wide_constant_to_home(out, insn->dst,
                                                    insn->immediate))
                    goto done;
            } else if (!mir_emit_constant_to_home(out, insn->dst,
                                                  insn->immediate)) {
                goto done;
            }
            break;
        case MIR_FLOAT_CONST:
            if (!mir_emit_wide_constant_to_home(out, insn->dst,
                                                insn->immediate))
                goto done;
            break;
        case MIR_UNARY:
            /* mir-text-size Item T12: same dead-result skip as the
             * spilled-scalar-cfg selector's MIR_UNARY case - see
             * dcc_mir_spilled_cfg.c for the full rationale. */
            if (!mir_value_has_use(insn->dst))
                break;
            if (mir_direct_branch_for_unary_not(i) >= 0)
                break;
            if (!mir_emit_homed_unary_instruction(out, insn)) {
                goto done;
            }
            break;
        case MIR_BINARY:
            {
                int operation;
                long count;
                if (type_size(insn->secondary_offset) == 4) {
                    if (!(type_size(insn->type) <= 2
                          ? mir_emit_homed_wide_comparison_instruction(
                                out, insn)
                          : mir_emit_homed_wide_binary_instruction(
                                out, insn)))
                        goto done;
                    break;
                }
                if (mir_direct_branch_for_comparison(i) >= 0)
                    break;
                if (mir_homed_constant_binary(insn, &operation, &count)) {
                    if (!mir_emit_homed_constant_binary_instruction(
                            out, insn, operation, count))
                        goto done;
                    break;
                }
            }
            if (!mir_emit_homed_binary_instruction(out, insn, 1))
                goto done;
            break;
        case MIR_JUMP:
            target = mir_find_label(insn->label);
            if (target < 0 || !mir_emit_homed_phi_copies(out, i, target))
                goto done;
            /* mir-text-size Item T8: same fallthrough-jump elision as the
             * spilled-scalar-cfg selector - see dcc_mir_spilled_cfg.c's
             * MIR_JUMP case for the full rationale. Item T62: same
             * unreachable-jump elision too. */
            if (!mir_target_is_noop_fallthrough(i, target) &&
                mir_insn_is_reachable(i))
                fprintf(out, "\tjp L%d\n", labels[insn->label]);
            break;
        case MIR_BRANCH_FALSE:
            target = mir_find_label(insn->label);
            if (target < 0)
                goto done;
            {
                int compare_index = mir_compare_definition_for_branch(i);
                if (compare_index >= 0) {
                    int false_has_phi = mir_edge_phi_names_predecessor(i, target);
                    int true_has_phi = i + 1 < mir.count &&
                        mir_edge_phi_names_predecessor(i, i + 1);
                    if (!false_has_phi) {
                        if (!mir_emit_homed_compare_false(
                                out, &mir.insns[compare_index],
                                labels[insn->label]))
                            goto done;
                        if (true_has_phi &&
                            !mir_emit_homed_phi_copies(out, i, i + 1))
                            goto done;
                    } else {
                        int false_stub = new_label();
                        int continue_label = new_label();
                        if (!mir_emit_homed_compare_false(
                                out, &mir.insns[compare_index], false_stub))
                            goto done;
                        if (true_has_phi &&
                            !mir_emit_homed_phi_copies(out, i, i + 1))
                            goto done;
                        fprintf(out, "\tjp L%d\nL%d:\n",
                                continue_label, false_stub);
                        if (!mir_emit_homed_phi_copies(out, i, target))
                            goto done;
                        fprintf(out, "\tjp L%d\nL%d:\n",
                                labels[insn->label], continue_label);
                    }
                    break;
                }
            }
            {
                int not_index = mir_unary_not_definition_for_branch(i);
                if (not_index >= 0) {
                    int false_has_phi =
                        mir_edge_phi_names_predecessor(i, target);
                    int true_has_phi = i + 1 < mir.count &&
                        mir_edge_phi_names_predecessor(i, i + 1);
                    int false_label = false_has_phi
                        ? new_label() : labels[insn->label];
                    int continue_label = false_has_phi ? new_label() : -1;

                    mir_homed_cfg_used_unary_not_branch = 1;
                    if (!mir_emit_homed_truth_jump(
                            out, mir.insns[not_index].src1, i, false_label))
                        goto done;
                    if (true_has_phi &&
                        !mir_emit_homed_phi_copies(out, i, i + 1))
                        goto done;
                    if (false_has_phi) {
                        fprintf(out, "\tjp L%d\nL%d:\n",
                                continue_label, false_label);
                        if (!mir_emit_homed_phi_copies(out, i, target))
                            goto done;
                        fprintf(out, "\tjp L%d\nL%d:\n",
                                labels[insn->label], continue_label);
                    }
                    break;
                }
            }
            true_label = new_label();
            if (!mir_emit_homed_truth_jump(
                    out, insn->src1, i, true_label))
                goto done;
            if (!mir_emit_homed_phi_copies(out, i, target))
                goto done;
            fprintf(out, "\tjp L%d\nL%d:\n", labels[insn->label], true_label);
            if (i + 1 < mir.count &&
                !mir_emit_homed_phi_copies(out, i, i + 1))
                goto done;
            break;
        case MIR_ARG:
            break;
        case MIR_CALL:
            {
                struct Sym *callee = find_global(insn->name);
                const char *assembly_name = insn->base_name[0] != 0
                    ? insn->base_name
                    : asm_name_for(sym_asm_name(callee));
                int call_arg_count = 0;
                int argument_bytes = 0;
                int argument;
                int scan;
                int dest_value, fill_value, count_value;
                int s_value, c_value;
                int s1_value, s2_value, n_value;
                int fn_value, dearg_value;
                const char *rtl_name;
                if (mir_call_is_memset_fastcall(i, &dest_value, &fill_value,
                                                &count_value)) {
                    if (!mir_emit_home_push(out, dest_value) ||
                        !mir_emit_home_push(out, fill_value) ||
                        !mir_emit_home_push(out, count_value))
                        goto done;
                    fputs("\tpop bc\n\tpop de\n\tpop hl\n", out);
                    mir_emit_runtime_call(out, "__msf");
                    if (type_ptr_depth(insn->type) > 0 ||
                        (insn->type & 15) != TYPE_VOID) {
                        if (!mir_emit_hl_to_home(out, insn->dst))
                            goto done;
                    }
                    break;
                }
                if (mir_call_is_strlen_fastcall(i, &s_value)) {
                    if (!mir_emit_home_to_hl(out, s_value))
                        goto done;
                    mir_emit_runtime_call(out, "__slf");
                    if (!mir_emit_hl_to_home(out, insn->dst))
                        goto done;
                    break;
                }
                if (mir_call_is_strchr_fastcall(i, &s_value, &c_value)) {
                    if (!mir_emit_home_push(out, s_value) ||
                        !mir_emit_home_push(out, c_value))
                        goto done;
                    fputs("\tpop hl\n\tld a,l\n\tpop hl\n", out);
                    mir_emit_runtime_call(out, "__chf");
                    if (!mir_emit_hl_to_home(out, insn->dst))
                        goto done;
                    break;
                }
                if (mir_call_is_strrchr_fastcall(i, &s_value, &c_value)) {
                    if (!mir_emit_home_push(out, s_value) ||
                        !mir_emit_home_push(out, c_value))
                        goto done;
                    fputs("\tpop hl\n\tld a,l\n\tpop hl\n", out);
                    mir_emit_runtime_call(out, "__rcf");
                    if (!mir_emit_hl_to_home(out, insn->dst))
                        goto done;
                    break;
                }
                if (mir_call_is_memchr_fastcall(i, &s_value, &c_value,
                                               &n_value)) {
                    if (!mir_emit_home_push(out, s_value) ||
                        !mir_emit_home_push(out, c_value) ||
                        !mir_emit_home_push(out, n_value))
                        goto done;
                    fputs("\tpop bc\n\tpop de\n\tpop hl\n", out);
                    mir_emit_runtime_call(out, "__mhf");
                    if (!mir_emit_hl_to_home(out, insn->dst))
                        goto done;
                    break;
                }
                if (mir_call_is_memcmp_fastcall(i, &s1_value, &s2_value,
                                               &n_value)) {
                    if (!mir_emit_home_push(out, s1_value) ||
                        !mir_emit_home_push(out, s2_value) ||
                        !mir_emit_home_push(out, n_value))
                        goto done;
                    fputs("\tpop bc\n\tpop hl\n\tpop de\n", out);
                    mir_emit_runtime_call(out, "__cmpf");
                    if (!mir_emit_hl_to_home(out, insn->dst))
                        goto done;
                    break;
                }
                if (mir_call_is_memcpy_fastcall(i, &dest_value, &fill_value,
                                               &n_value)) {
                    if (!mir_emit_home_push(out, dest_value) ||
                        !mir_emit_home_push(out, fill_value) ||
                        !mir_emit_home_push(out, n_value))
                        goto done;
                    fputs("\tpop bc\n\tpop hl\n\tpop de\n", out);
                    mir_emit_runtime_call(out, "__mcf");
                    if (!mir_emit_hl_to_home(out, insn->dst))
                        goto done;
                    break;
                }
                if (mir_call_is_de_hl_fastcall(i, &rtl_name, &s1_value,
                                              &s2_value)) {
                    if (!mir_emit_home_push(out, s1_value) ||
                        !mir_emit_home_push(out, s2_value))
                        goto done;
                    fputs("\tpop hl\n\tpop de\n", out);
                    mir_emit_runtime_call(out, rtl_name);
                    if (!mir_emit_hl_to_home(out, insn->dst))
                        goto done;
                    break;
                }
                if (mir_call_is_bdos_family_fastcall(i, &rtl_name, &fn_value,
                                                    &dearg_value)) {
                    if (!mir_emit_home_push(out, fn_value) ||
                        !mir_emit_home_push(out, dearg_value))
                        goto done;
                    fputs("\tpop de\n\tpop hl\n\tld c,l\n", out);
                    mir_emit_runtime_call(out, rtl_name);
                    if (type_ptr_depth(insn->type) > 0 ||
                        (insn->type & 15) != TYPE_VOID) {
                        if (!mir_emit_hl_to_home(out, insn->dst))
                            goto done;
                    }
                    break;
                }

                for (scan = 0; scan < i; ++scan)
                    if (mir.insns[scan].opcode == MIR_ARG &&
                        mir.insns[scan].secondary_offset ==
                            insn->secondary_offset) {
                        int index = (int)mir.insns[scan].immediate;
                        if (index != call_arg_count)
                            goto done;
                        ++call_arg_count;
                    }
                argument = call_arg_count - 1;
                for (scan = i - 1; scan >= 0; --scan) {
                    const struct MirInsn *arg = &mir.insns[scan];
                    int size;
                    if (arg->opcode != MIR_ARG ||
                        arg->secondary_offset != insn->secondary_offset)
                        continue;
                    if (arg->immediate != argument--)
                        goto done;
                    size = type_size(arg->type);
                    if (size == 4) {
                        if (!mir_emit_wide_home_to_stack(out, arg->src1))
                            goto done;
                        argument_bytes += 4;
                    } else {
                        if (!mir_emit_home_push(out, arg->src1))
                            goto done;
                        argument_bytes += 2;
                    }
                }
                if (argument != -1)
                    goto done;
                if (callee->needs_extrn && mir_extrn_should_emit(callee))
                    fprintf(out, "\textrn %s\n", assembly_name);
                fprintf(out, "\tcall %s\n", assembly_name);
                for (argument = 0; argument < argument_bytes / 2; ++argument)
                    fputs("\tpop bc\n", out);
                if (type_ptr_depth(insn->type) > 0 ||
                    (insn->type & 15) != TYPE_VOID) {
                    if (mir_homed_wide_type_supported(insn->type)) {
                        if (!mir_emit_hl_de_to_wide_home(out, insn->dst))
                            goto done;
                    } else if (!mir_emit_hl_to_home(out, insn->dst)) {
                        goto done;
                    }
                }
            }
            break;
        case MIR_RETURN:
            /* void: nothing to load into HL (MIR_RETURN's src1 is not a
             * real value for "return;") - mirrors MIR_CALL's own
             * void-result skip above. Item 20d: a wide (long) return
             * loads HL:DE instead of just HL, matching the calling
             * convention mir_emit_virtual_load_wide already establishes
             * for the spilled-scalar-cfg selector. */
            if (type_ptr_depth(mir.return_type) > 0 ||
                (mir.return_type & 15) != TYPE_VOID) {
                if (mir_homed_wide_type_supported(mir.return_type)) {
                    if (!mir_emit_wide_home_to_hl_de(out, insn->src1))
                        goto done;
                } else if (!mir_emit_home_to_hl(out, insn->src1)) {
                    goto done;
                }
            }
            /* mir-text-size Item T14: only share the epilogue when it is
             * more than a bare `ret` (frameless emits just that, 1 byte -
             * smaller than a `jp` to a shared copy, so sharing would
             * regress it); otherwise mirror dcc_mir_spilled_cfg.c's
             * shared-epilogue optimization for the real ix/iy-restoring
             * epilogue. Only take the "early return" path when the
             * shared label is guaranteed a definition: either this
             * function's last MIR instruction is itself a MIR_RETURN
             * (the owner, further down in program order, always defines
             * it), or the function is void (the fall-off-the-end tail
             * below defines it in that case). A non-void function whose
             * last instruction isn't a MIR_RETURN has nowhere to home
             * the label, so fall back to the original always-inline
             * behavior for that (believed unreachable in practice for
             * this acyclic selector, but not proven, so guarded here). */
            if (frameless) {
                fputs("\tret\n", out);
            } else if (return_count > 1 &&
                       (last_insn_is_return ||
                        (mir.return_type & 15) == TYPE_VOID) &&
                       !(last_insn_is_return && i == mir.count - 1)) {
                if (shared_epilogue_label < 0)
                    shared_epilogue_label = new_label();
                fprintf(out, "\tjp L%d\n", shared_epilogue_label);
            } else {
                if (shared_epilogue_label >= 0)
                    fprintf(out, "L%d:\n", shared_epilogue_label);
                mir_emit_home_epilogue(out, uses_iy);
            }
            break;
        default:
            goto done;
        }
        if (mir_instruction_has_phi_fallthrough(i, 0) &&
            !mir_emit_homed_phi_copies(out, i, i + 1))
            goto done;
    }
    /* A void function that falls off the end (no MIR_RETURN reached as
     * the final instruction - either return_count==0 entirely, or the
     * last statement was an early "return;" followed by more code with
     * no trailing return) still needs the epilogue emitted once at the
     * true end of the body, mirroring what every MIR_RETURN case above
     * already does inline. */
    if ((mir.return_type & 15) == TYPE_VOID &&
        (mir.count == 0 || mir.insns[mir.count - 1].opcode != MIR_RETURN)) {
        if (frameless) {
            fputs("\tret\n", out);
        } else {
            if (shared_epilogue_label >= 0)
                fprintf(out, "L%d:\n", shared_epilogue_label);
            mir_emit_home_epilogue(out, uses_iy);
        }
    }
    if (mir_homed_cfg_depends_on_word_store() &&
        mir_stream_instruction_count(out) >
            mir_stream_instruction_count(mir.capture_stream) - 2 &&
        mir_stream_size(out) * 50L >
            mir_stream_size(mir.capture_stream) * 47L)
        goto done;
    /* A two-instruction deficit regressed the narrow-shift candidates.
     * A one-instruction deficit remains allowed because the measured
     * tmirfast/tmirfuse candidates improve in both peep modes. */
    if (mir_homed_cfg_rematerializes_string_argument() &&
        mir_stream_instruction_count(out) >
            mir_stream_instruction_count(mir.capture_stream) + 1)
        goto done;
    if (mir.allocation_spill_count != 0 &&
        (mir_cfg_block_count() > 4 ||
         mir_stream_size(out) >= mir_stream_size(mir.capture_stream) ||
         mir_stream_instruction_count(out) >
             mir_stream_instruction_count(mir.capture_stream)))
        goto done;
    accepted = 1;
done:
    if (!accepted && getenv("DCC_MIR_SELECT_REPORT") != NULL)
        fprintf(stderr,
                "; MIR home-cfg reject function=%s insn=%d opcode=%s "
                "generated-bytes=%ld captured-bytes=%ld "
                "generated-insns=%d captured-insns=%d\n",
                mir.name, i,
                i >= 0 && i < mir.count
                    ? mir_opcode_name(mir.insns[i].opcode) : "preflight",
                mir_stream_size(out), mir_stream_size(mir.capture_stream),
                mir_stream_instruction_count(out),
                mir_stream_instruction_count(mir.capture_stream));
    free(labels);
    return accepted;
}
