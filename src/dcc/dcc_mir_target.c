/**
 * @file dcc_mir_target.c
 * @brief Models Z80 constraints for the diagnostic shadow scheduler.
 *
 * @par Role
 * Maps semantic MIR instructions to legal register colors, fixed inputs and
 * outputs, call clobbers, aliasing, and approximate template costs.
 *
 * @par Boundary
 * mir_target_constraint_for_insn() feeds dcc_mir_schedule.c, and
 * mir_target_report_shadow_plan() reports the model. This module neither
 * selects a production candidate nor emits Z80.
 */

#include "dcc_mir_internal.h"

#define MIR_NARROW_COLORS \
    ((1u << MIR_COLOR_HL) | (1u << MIR_COLOR_DE) | \
     (1u << MIR_COLOR_BC) | (1u << MIR_COLOR_IY))
#define MIR_WIDE_COLORS \
    ((1u << MIR_COLOR_HL_DE) | (1u << MIR_COLOR_BC_IY))

static int mir_target_width(const struct MirInsn *insn)
{
    int width;

    if (insn == NULL)
        return 0;
    width = type_size(insn->type);
    if (insn->opcode == MIR_LOAD_INDIRECT && insn->memory_size > 0)
        width = insn->memory_size;
    return width;
}

static int mir_target_value_width(int value)
{
    const struct MirInsn *definition = mir_definition(value);

    if (definition == NULL)
        return 0;
    if (definition->opcode == MIR_ADDRESS ||
        definition->opcode == MIR_COMPOUND_ADDRESS ||
        definition->opcode == MIR_INDEX_ADDRESS ||
        definition->opcode == MIR_MEMBER_ADDRESS ||
        definition->opcode == MIR_CALL_AGGREGATE)
        return 2;
    if ((definition->opcode == MIR_LOAD_INDIRECT ||
         definition->opcode == MIR_INDEX_LOAD) &&
        definition->memory_size > 0)
        return definition->memory_size;
    return type_size(definition->type);
}

static unsigned mir_target_required_home(int width)
{
    if (width <= 0)
        return 0;
    return width == 4 ? MIR_Z80_DE | MIR_Z80_HL : MIR_Z80_HL;
}

static int mir_target_binary_is_helper(const struct MirInsn *insn)
{
    int operand_type;

    if (insn == NULL || insn->opcode != MIR_BINARY)
        return 0;
    operand_type = insn->secondary_offset != 0
        ? insn->secondary_offset : insn->type;
    if (type_is_float(operand_type))
        return 1;
    return type_size(operand_type) == 4 &&
           (insn->immediate == '*' || insn->immediate == '/' ||
            insn->immediate == '%');
}

static void mir_target_init(
    struct MirTargetConstraint *out, int kind, int width)
{
    memset(out, 0, sizeof(*out));
    out->template_kind = kind;
    if (width == 4) {
        out->allowed_colors = MIR_WIDE_COLORS;
        out->flags |= MIR_TARGET_FLAG_WIDE;
    } else {
        out->allowed_colors = MIR_NARROW_COLORS;
        if (width == 1)
            out->flags |= MIR_TARGET_FLAG_BYTE;
    }
}

int mir_target_constraint_for_insn(
    const struct MirInsn *insn, struct MirTargetConstraint *out)
{
    int width;

    if (insn == NULL || out == NULL)
        return 0;
    width = mir_target_width(insn);
    switch (insn->opcode) {
    case MIR_NOP:
    case MIR_PARAM:
    case MIR_LABEL:
    case MIR_JUMP:
    case MIR_DECL_PLACEHOLDER:
    case MIR_OBJECT_MERGE:
    case MIR_PHI:
        mir_target_init(out, MIR_TARGET_PSEUDO, width);
        if (insn->opcode == MIR_JUMP)
            out->flags |= MIR_TARGET_FLAG_EDGE;
        return 1;

    case MIR_CONST:
    case MIR_FLOAT_CONST:
    case MIR_STRING_ADDRESS:
        mir_target_init(out, MIR_TARGET_MATERIALIZE, width);
        out->required_output = width == 4
            ? MIR_Z80_DE | MIR_Z80_HL : MIR_Z80_HL;
        out->flags |= MIR_TARGET_FLAG_REMATERIALIZABLE;
        out->minimum_tstates = width == 4 ? 20 : 10;
        out->minimum_bytes = width == 4 ? 6 : 3;
        return 1;

    case MIR_ADDRESS:
    case MIR_COMPOUND_ADDRESS:
    case MIR_INDEX_ADDRESS:
    case MIR_MEMBER_ADDRESS:
        mir_target_init(out, MIR_TARGET_ADDRESS, 2);
        out->required_output = MIR_Z80_HL;
        out->flags |= MIR_TARGET_FLAG_REMATERIALIZABLE;
        if (insn->opcode == MIR_INDEX_ADDRESS) {
            out->required_input1 = MIR_Z80_HL;
            out->required_input2 = MIR_Z80_DE;
            out->clobbers = MIR_Z80_FLAGS;
            out->minimum_tstates = 21;
            out->minimum_bytes = 4;
        } else {
            out->minimum_tstates = 10;
            out->minimum_bytes = 3;
        }
        return 1;

    case MIR_LOAD:
    case MIR_LOAD_INDIRECT:
    case MIR_INDEX_LOAD:
    case MIR_VLA_SIZE:
        mir_target_init(out, MIR_TARGET_LOAD, width);
        out->required_output = width == 4
            ? MIR_Z80_DE | MIR_Z80_HL : MIR_Z80_HL;
        if (insn->opcode != MIR_LOAD)
            out->required_input1 = MIR_Z80_HL;
        out->flags |= MIR_TARGET_FLAG_MEMORY;
        out->minimum_tstates = width == 4 ? 38 : width == 1 ? 19 : 38;
        out->minimum_bytes = width == 4 ? 6 : 3;
        return 1;

    case MIR_STORE:
    case MIR_STORE_INDIRECT:
        mir_target_init(out, MIR_TARGET_STORE, width);
        out->required_input1 =
            mir_target_required_home(mir_target_value_width(insn->src1));
        if (insn->opcode == MIR_STORE_INDIRECT) {
            int value_width = mir_target_value_width(insn->src2);

            out->required_input1 =
                value_width == 4 ? MIR_Z80_BC : MIR_Z80_HL;
            out->required_input2 = value_width == 4
                ? MIR_Z80_DE | MIR_Z80_HL : MIR_Z80_DE;
        }
        out->flags |= MIR_TARGET_FLAG_MEMORY;
        out->minimum_tstates = width == 4 ? 38 : width == 1 ? 19 : 38;
        out->minimum_bytes = width == 4 ? 6 : 3;
        return 1;

    case MIR_UNARY:
        mir_target_init(out, MIR_TARGET_UNARY, width);
        out->required_input1 =
            mir_target_required_home(mir_target_value_width(insn->src1));
        out->required_output = mir_target_required_home(width);
        out->clobbers = MIR_Z80_A | MIR_Z80_FLAGS;
        out->minimum_tstates = 8;
        out->minimum_bytes = 2;
        return 1;

    case MIR_BINARY:
        mir_target_init(out, MIR_TARGET_BINARY, width);
        if (type_size(insn->secondary_offset) == 4) {
            out->required_input1 = MIR_Z80_DE | MIR_Z80_HL;
            out->required_input2 = MIR_Z80_BC | MIR_Z80_IY;
        } else {
            out->required_input1 = MIR_Z80_HL;
            out->required_input2 = MIR_Z80_DE;
        }
        out->required_output = mir_target_required_home(width);
        out->clobbers = MIR_Z80_A | MIR_Z80_FLAGS;
        out->minimum_tstates = width == 4 ? 32 : 11;
        out->minimum_bytes = width == 4 ? 6 : 1;
        if (mir_target_binary_is_helper(insn)) {
            out->template_kind = MIR_TARGET_CALL;
            out->flags |= MIR_TARGET_FLAG_CALL;
            out->required_input1 = 0;
            out->required_input2 = MIR_Z80_DE | MIR_Z80_HL;
            out->clobbers = MIR_Z80_CALLER_CLOBBERS;
            out->minimum_tstates += 96;
            out->minimum_bytes += 3;
        }
        return 1;

    case MIR_ARG:
        mir_target_init(out, MIR_TARGET_ARGUMENT, width);
        out->minimum_tstates = width == 4 ? 22 : 11;
        out->minimum_bytes = width == 4 ? 2 : 1;
        return 1;

    case MIR_CALL:
    case MIR_CALL_AGGREGATE:
        mir_target_init(out, MIR_TARGET_CALL, width);
        out->required_output = width == 4
            ? MIR_Z80_DE | MIR_Z80_HL : MIR_Z80_HL;
        out->clobbers = MIR_Z80_CALLER_CLOBBERS;
        out->flags |= MIR_TARGET_FLAG_CALL;
        out->minimum_tstates = 17;
        out->minimum_bytes = 3;
        return 1;

    case MIR_BRANCH_FALSE:
        mir_target_init(out, MIR_TARGET_BRANCH, width);
        out->required_input1 =
            mir_target_required_home(mir_target_value_width(insn->src1));
        out->clobbers = MIR_Z80_A | MIR_Z80_FLAGS;
        out->flags |= MIR_TARGET_FLAG_EDGE;
        out->minimum_tstates = 18;
        out->minimum_bytes = 4;
        return 1;

    case MIR_RETURN:
        mir_target_init(out, MIR_TARGET_RETURN, width);
        out->required_input1 =
            mir_target_required_home(mir_target_value_width(insn->src1));
        out->minimum_tstates = 10;
        out->minimum_bytes = 1;
        return 1;

    case MIR_VLA_SAVE:
    case MIR_VLA_ALLOC:
    case MIR_VLA_RESTORE:
        mir_target_init(out, MIR_TARGET_VLA, width);
        out->required_input1 = MIR_Z80_HL;
        out->clobbers = MIR_Z80_A | MIR_Z80_DE | MIR_Z80_HL |
                        MIR_Z80_FLAGS;
        out->flags |= MIR_TARGET_FLAG_MEMORY;
        out->minimum_tstates = 32;
        out->minimum_bytes = 6;
        return 1;

    case MIR_COPY_AGGREGATE:
        mir_target_init(out, MIR_TARGET_AGGREGATE, width);
        out->required_input1 = MIR_Z80_HL;
        out->required_input2 = MIR_Z80_DE;
        out->clobbers = MIR_Z80_BC | MIR_Z80_DE | MIR_Z80_HL |
                        MIR_Z80_FLAGS;
        out->flags |= MIR_TARGET_FLAG_MEMORY;
        out->minimum_tstates = 37;
        out->minimum_bytes = 6;
        return 1;

    case MIR_VA_START:
    case MIR_VA_END:
    case MIR_VA_ARG:
        mir_target_init(out, MIR_TARGET_VARIADIC, width);
        out->required_input1 = MIR_Z80_HL;
        out->required_output = width == 4
            ? MIR_Z80_DE | MIR_Z80_HL : MIR_Z80_HL;
        out->flags |= MIR_TARGET_FLAG_MEMORY;
        out->minimum_tstates = 38;
        out->minimum_bytes = 6;
        return 1;

    case MIR_OPAQUE:
    default:
        mir_target_init(out, MIR_TARGET_UNSUPPORTED, width);
        return 0;
    }
}

void mir_target_report_shadow_plan(void)
{
    const char *filter;
    long minimum_tstates = 0;
    long minimum_bytes = 0;
    int template_counts[MIR_TARGET_UNSUPPORTED + 1];
    int calls = 0;
    int edges = 0;
    int wide = 0;
    int byte = 0;
    int unsupported = 0;
    int instruction;

    if (getenv("DCC_MIR_TARGET_REPORT") == NULL)
        return;
    filter = getenv("DCC_MIR_TARGET_FUNCTION");
    if (filter != NULL && filter[0] != 0 && strcmp(filter, mir.name))
        return;
    memset(template_counts, 0, sizeof(template_counts));
    for (instruction = 0; instruction < mir.count; ++instruction) {
        struct MirTargetConstraint constraint;

        if (!mir_target_constraint_for_insn(
                &mir.insns[instruction], &constraint)) {
            ++unsupported;
            continue;
        }
        if (constraint.template_kind >= MIR_TARGET_PSEUDO &&
            constraint.template_kind <= MIR_TARGET_UNSUPPORTED)
            ++template_counts[constraint.template_kind];
        minimum_tstates += constraint.minimum_tstates;
        minimum_bytes += constraint.minimum_bytes;
        if ((constraint.flags & MIR_TARGET_FLAG_CALL) != 0)
            ++calls;
        if ((constraint.flags & MIR_TARGET_FLAG_EDGE) != 0)
            ++edges;
        if ((constraint.flags & MIR_TARGET_FLAG_WIDE) != 0)
            ++wide;
        if ((constraint.flags & MIR_TARGET_FLAG_BYTE) != 0)
            ++byte;
    }
    fprintf(stderr,
            "; MIR target-plan function=%s insns=%d minimum-tstates=%ld "
            "minimum-bytes=%ld calls=%d edges=%d wide=%d byte=%d "
            "pseudo=%d materialize=%d address=%d load=%d store=%d "
            "unary=%d binary=%d unsupported=%d\n",
            mir.name, mir.count, minimum_tstates, minimum_bytes,
            calls, edges, wide, byte,
            template_counts[MIR_TARGET_PSEUDO],
            template_counts[MIR_TARGET_MATERIALIZE],
            template_counts[MIR_TARGET_ADDRESS],
            template_counts[MIR_TARGET_LOAD],
            template_counts[MIR_TARGET_STORE],
            template_counts[MIR_TARGET_UNARY],
            template_counts[MIR_TARGET_BINARY],
            unsupported);
}
