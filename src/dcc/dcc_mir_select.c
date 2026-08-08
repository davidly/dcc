/* dcc_mir_select.c - loop selectors (countdown/accumulator/unsigned-
 * division/repeated-invariant-add), the general CFG rollout and
 * comparison-branch selectors, the top-level mir_try_emit_z80
 * dispatcher, and mir_end_function's transactional accept/reject
 * entry point.
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

#define MIR_SPILLED_FEATURE_RHS_STACK             (1UL << 0)
#define MIR_SPILLED_FEATURE_STORE_VALUE           (1UL << 1)
#define MIR_SPILLED_FEATURE_BRANCH_CONDITION      (1UL << 2)
#define MIR_SPILLED_FEATURE_STORE_ADDRESS         (1UL << 3)
#define MIR_SPILLED_FEATURE_WIDE_BINARY_LHS       (1UL << 4)
#define MIR_SPILLED_FEATURE_STABLE_POINTER_ARG    (1UL << 5)
#define MIR_SPILLED_FEATURE_GLOBAL_ARG            (1UL << 6)
#define MIR_SPILLED_FEATURE_WIDE_FIRST_ARG        (1UL << 7)
#define MIR_SPILLED_FEATURE_NARROW_DIRECT_PUSH    (1UL << 8)
#define MIR_SPILLED_FEATURE_CONSTANT_PREPACK      (1UL << 9)
#define MIR_SPILLED_FEATURE_PROMOTED_LOCAL_SLOT   (1UL << 10)
#define MIR_SPILLED_FEATURE_WIDE_BINARY_RHS       (1UL << 11)
#define MIR_SPILLED_FEATURE_WIDE_STORE            (1UL << 12)
#define MIR_SPILLED_FEATURE_PHI_SLOT              (1UL << 13)

#define MIR_SPILLED_FEATURES_RHS \
    (MIR_SPILLED_FEATURE_RHS_STACK | MIR_SPILLED_FEATURE_STORE_VALUE | \
     MIR_SPILLED_FEATURE_BRANCH_CONDITION)
#define MIR_SPILLED_FEATURES_STORE_ADDRESS \
    (MIR_SPILLED_FEATURES_RHS | MIR_SPILLED_FEATURE_STORE_ADDRESS)
#define MIR_SPILLED_FEATURES_WIDE_LHS \
    (MIR_SPILLED_FEATURES_STORE_ADDRESS | MIR_SPILLED_FEATURE_WIDE_BINARY_LHS)
#define MIR_SPILLED_FEATURES_STABLE_ARG \
    (MIR_SPILLED_FEATURES_WIDE_LHS | \
     MIR_SPILLED_FEATURE_STABLE_POINTER_ARG)
#define MIR_SPILLED_FEATURES_GLOBAL_ARG \
    (MIR_SPILLED_FEATURES_STABLE_ARG | MIR_SPILLED_FEATURE_GLOBAL_ARG)
#define MIR_SPILLED_FEATURES_CALL_STACK \
    (MIR_SPILLED_FEATURES_GLOBAL_ARG | MIR_SPILLED_FEATURE_WIDE_FIRST_ARG | \
     MIR_SPILLED_FEATURE_NARROW_DIRECT_PUSH | \
     MIR_SPILLED_FEATURE_CONSTANT_PREPACK)
#define MIR_SPILLED_FEATURES_PROMOTED_LOCAL \
    (MIR_SPILLED_FEATURES_CALL_STACK | \
     MIR_SPILLED_FEATURE_PROMOTED_LOCAL_SLOT)
#define MIR_SPILLED_FEATURES_ALL \
    (MIR_SPILLED_FEATURES_PROMOTED_LOCAL | \
     MIR_SPILLED_FEATURE_WIDE_BINARY_RHS | MIR_SPILLED_FEATURE_WIDE_STORE)
#define MIR_SPILLED_FEATURES_PHI_SLOT \
    (MIR_SPILLED_FEATURES_ALL | MIR_SPILLED_FEATURE_PHI_SLOT)

struct MirCandidateDescriptor {
    const char *name;
    const char *stream_error;
    int (*selector)(FILE *);
    unsigned long spilled_features;
};

struct MirCandidateResult {
    const struct MirCandidateDescriptor *descriptor;
    FILE *stream;
    int emitted;
    int label_id_after;
    long generated_size;
    int generated_instructions;
    const char *reason;
};

static int mir_call_count(void);
static int mir_has_inline_substitution_call(void);
static int mir_has_declared_pointer_array(void);
static int mir_has_label_only_phi_fallthrough(void);
static int mir_has_wide_values(void);

static void mir_configure_spilled_fallback_features(
    unsigned long features, int enabled)
{
    if ((features & MIR_SPILLED_FEATURE_RHS_STACK) != 0) {
        if (enabled)
            mir_begin_general_rhs_stack_forwarding();
        else
            mir_end_general_rhs_stack_forwarding();
    }
    if ((features & MIR_SPILLED_FEATURE_STORE_VALUE) != 0) {
        if (enabled)
            mir_begin_indirect_store_value_forwarding();
        else
            mir_end_indirect_store_value_forwarding();
    }
    if ((features & MIR_SPILLED_FEATURE_BRANCH_CONDITION) != 0) {
        if (enabled)
            mir_begin_branch_condition_forwarding();
        else
            mir_end_branch_condition_forwarding();
    }
    if ((features & MIR_SPILLED_FEATURE_STORE_ADDRESS) != 0) {
        if (enabled)
            mir_begin_indirect_store_address_forwarding();
        else
            mir_end_indirect_store_address_forwarding();
    }
    if ((features & MIR_SPILLED_FEATURE_WIDE_BINARY_LHS) != 0) {
        if (enabled)
            mir_begin_wide_binary_lhs_forwarding();
        else
            mir_end_wide_binary_lhs_forwarding();
    }
    if ((features & MIR_SPILLED_FEATURE_STABLE_POINTER_ARG) != 0) {
        if (enabled)
            mir_begin_stable_pointer_argument_rematerialization();
        else
            mir_end_stable_pointer_argument_rematerialization();
    }
    if ((features & MIR_SPILLED_FEATURE_GLOBAL_ARG) != 0) {
        if (enabled)
            mir_begin_global_argument_rematerialization();
        else
            mir_end_global_argument_rematerialization();
    }
    if ((features & MIR_SPILLED_FEATURE_WIDE_FIRST_ARG) != 0) {
        if (enabled)
            mir_begin_wide_first_argument_stack_cache();
        else
            mir_end_wide_first_argument_stack_cache();
    }
    if ((features & MIR_SPILLED_FEATURE_NARROW_DIRECT_PUSH) != 0) {
        if (enabled)
            mir_begin_narrow_argument_direct_push();
        else
            mir_end_narrow_argument_direct_push();
    }
    if ((features & MIR_SPILLED_FEATURE_CONSTANT_PREPACK) != 0) {
        if (enabled)
            mir_begin_constant_argument_prepacking();
        else
            mir_end_constant_argument_prepacking();
    }
    if ((features & MIR_SPILLED_FEATURE_PROMOTED_LOCAL_SLOT) != 0) {
        if (enabled)
            mir_begin_promoted_local_slot_reuse();
        else
            mir_end_promoted_local_slot_reuse();
    }
    if ((features & MIR_SPILLED_FEATURE_WIDE_BINARY_RHS) != 0) {
        if (enabled)
            mir_begin_wide_binary_rhs_forwarding();
        else
            mir_end_wide_binary_rhs_forwarding();
    }
    if ((features & MIR_SPILLED_FEATURE_WIDE_STORE) != 0) {
        if (enabled)
            mir_begin_wide_store_forwarding();
        else
            mir_end_wide_store_forwarding();
    }
    if ((features & MIR_SPILLED_FEATURE_PHI_SLOT) != 0) {
        if (enabled)
            mir_begin_phi_slot_cleanup();
        else
            mir_end_phi_slot_cleanup();
    }
}

static void mir_begin_all_spilled_fallback_optimizations(void)
{
    mir_configure_spilled_fallback_features(MIR_SPILLED_FEATURES_ALL, 1);
}

static void mir_end_all_spilled_fallback_optimizations(void)
{
    mir_configure_spilled_fallback_features(MIR_SPILLED_FEATURES_ALL, 0);
}

static int mir_try_emit_general_rollout(FILE *out)
{
    int i;
    int frame_bytes;
    int return_count = 0;
    int parameter_count = 0;

    if (mir.has_vla || mir.has_runtime_stride_param ||
        mir.is_variadic_function || mir.count > 64 ||
        mir.declaration_count > 0 ||
        (mir.return_type & 15) != TYPE_INT || type_size(mir.return_type) > 2)
        return 0;
    frame_bytes = mir_current_frame_bytes();
    if (frame_bytes > 64)
        return 0;
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->dst >= 0 && (type_size(insn->type) > 2 ||
                               type_ptr_depth(insn->type) > 0))
            return 0;
        switch (insn->opcode) {
        case MIR_NOP: case MIR_LABEL: case MIR_CONST:
        case MIR_UNARY: case MIR_BINARY:
            break;
        case MIR_PARAM:
            ++parameter_count;
            break;
        case MIR_RETURN:
            ++return_count;
            break;
        default:
            return 0;
        }
    }
    if (return_count != 1 || parameter_count == 0)
        return 0;
    return mir_try_emit_homed_scalar_dag(out);
}

/* Phase 8 Item 78/80: mir_try_emit_home_cfg_rollout was removed here.
 * It gated a narrower loop-phi structural subset but its body did nothing
 * but call mir_try_emit_homed_scalar_cfg() directly - the exact same
 * function production already tries unconditionally for every function.
 * Since that call can never produce output different from what the
 * already-active production path produces for the same function, the
 * wrapper was provably redundant diagnostic scaffolding, not a distinct
 * selector. Confirmed via census: DCC_MIR_EMIT_HOME_CFG never changed a
 * single byte/instruction count relative to the production path for any
 * function it matched. */

static int mir_is_const_value(int value, long expected)
{
    const struct MirInsn *definition = mir_definition(value);
    return definition != NULL && definition->opcode == MIR_CONST &&
           definition->immediate == expected;
}

/* Strict first loop selector:
 *
 *     while (n > 0) --n;
 *     return n;
 *
 * N is one word parameter represented by an object phi at the loop header.
 * Keep it in BC for the complete loop: this is the first emitted path that
 * consumes a MIR allocation decision rather than merely using fixed HL/DE
 * expression conventions. */
static int mir_try_emit_countdown_loop(FILE *out)
{
    const struct MirInsn *parameter = NULL;
    const struct MirInsn *phi = NULL;
    const struct MirInsn *decrement = NULL;
    const struct MirInsn *compare = NULL;
    const struct MirInsn *branch = NULL;
    const struct MirInsn *return_insn = NULL;
    const struct MirObject *object;
    int object_index = -1;
    int top_label;
    int end_label;
    int i;
    int unsigned_value;

    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->opcode == MIR_PARAM) {
            if (parameter != NULL)
                return 0;
            parameter = insn;
            object_index = insn->object;
        } else if (insn->opcode == MIR_PHI && insn->object == object_index) {
            phi = insn;
        } else if (insn->opcode == MIR_STORE && insn->object == object_index) {
            decrement = mir_definition(insn->src1);
        } else if (insn->opcode == MIR_BRANCH_FALSE) {
            const struct MirInsn *candidate = mir_definition(insn->src1);
            if (candidate != NULL && candidate->opcode == MIR_BINARY &&
                candidate->immediate == '>') {
                compare = candidate;
                branch = insn;
            }
        } else if (insn->opcode == MIR_RETURN) {
            return_insn = insn;
        } else if (insn->opcode == MIR_CALL || insn->opcode == MIR_OPAQUE ||
                   insn->opcode == MIR_INDEX_LOAD || insn->opcode == MIR_ARG) {
            return 0;
        }
    }
    if (parameter == NULL || phi == NULL || decrement == NULL ||
        compare == NULL || branch == NULL || return_insn == NULL ||
        object_index < 0 || object_index >= mir.object_count)
        return 0;
    if (decrement->opcode != MIR_BINARY || decrement->immediate != '-' ||
        decrement->src1 != phi->dst ||
        !mir_is_const_value(decrement->src2, 1))
        return 0;
    if (compare->src1 != phi->dst || !mir_is_const_value(compare->src2, 0) ||
        return_insn->src1 != phi->dst)
        return 0;
    if (!((phi->src1 == parameter->dst && phi->src2 == decrement->dst) ||
          (phi->src2 == parameter->dst && phi->src1 == decrement->dst)))
        return 0;

    object = &mir.objects[object_index];
    if (object->storage != SC_PARAM || type_size(object->type) != 2)
        return 0;
    unsigned_value = (object->type & TYPE_UNSIGNED) != 0 ||
                     type_ptr_depth(object->type) > 0;
    top_label = new_label();
    end_label = new_label();

    mir_emit_prologue(out);
    fprintf(out, "\tld c,(ix%+d)\n", object->offset);
    fprintf(out, "\tld b,(ix%+d)\n", object->offset + 1);
    fprintf(out, "L%d:\n", top_label);
    if (unsigned_value) {
        fputs("\tld a,b\n\tor c\n", out);
        fprintf(out, "\tjp z, L%d\n", end_label);
    } else {
        fputs("\tld a,b\n\tor a\n", out);
        fprintf(out, "\tjp m, L%d\n", end_label);
        fputs("\tor c\n", out);
        fprintf(out, "\tjp z, L%d\n", end_label);
    }
    fputs("\tdec bc\n", out);
    fprintf(out, "\tjp L%d\n", top_label);
    fprintf(out, "L%d:\n", end_label);
    fputs("\tld l,c\n\tld h,b\n\tld sp,ix\n\tpop ix\n\tret\n", out);
    return 1;
}

/* Two-register loop selector:
 *
 *     int sum = 0;
 *     while (n > 0) { sum += n; --n; }
 *     return sum;
 *
 * BC holds n and DE holds sum. Both values are object phis at the header and
 * neither is written back to the frame inside the loop. */
static int mir_try_emit_accumulator_loop(FILE *out)
{
    const struct MirInsn *parameter = NULL;
    const struct MirInsn *n_phi = NULL;
    const struct MirInsn *sum_phi = NULL;
    const struct MirInsn *n_update = NULL;
    const struct MirInsn *sum_update = NULL;
    const struct MirInsn *compare = NULL;
    const struct MirInsn *return_insn = NULL;
    int n_object = -1;
    int sum_object = -1;
    int i;
    int top_label;
    int end_label;
    int unsigned_value;

    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->opcode == MIR_PARAM) {
            if (parameter != NULL)
                return 0;
            parameter = insn;
            n_object = insn->object;
        }
    }
    if (parameter == NULL || n_object < 0)
        return 0;
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->opcode == MIR_PHI) {
            if (insn->object == n_object)
                n_phi = insn;
            else if (sum_phi == NULL) {
                sum_phi = insn;
                sum_object = insn->object;
            } else {
                return 0;
            }
        } else if (insn->opcode == MIR_STORE) {
            const struct MirInsn *definition = mir_definition(insn->src1);
            if (insn->object == n_object)
                n_update = definition;
            else if (insn->object == sum_object || sum_object < 0)
                sum_update = definition;
        } else if (insn->opcode == MIR_BRANCH_FALSE) {
            const struct MirInsn *candidate = mir_definition(insn->src1);
            if (candidate != NULL && candidate->opcode == MIR_BINARY &&
                candidate->immediate == '>')
                compare = candidate;
        } else if (insn->opcode == MIR_RETURN) {
            return_insn = insn;
        } else if (insn->opcode == MIR_CALL || insn->opcode == MIR_OPAQUE ||
                   insn->opcode == MIR_INDEX_LOAD || insn->opcode == MIR_ARG) {
            return 0;
        }
    }
    if (n_phi == NULL || sum_phi == NULL || n_update == NULL ||
        sum_update == NULL || compare == NULL || return_insn == NULL ||
        sum_object < 0)
        return 0;
    if (n_update->opcode != MIR_BINARY || n_update->immediate != '-' ||
        n_update->src1 != n_phi->dst || !mir_is_const_value(n_update->src2, 1))
        return 0;
    if (sum_update->opcode != MIR_BINARY || sum_update->immediate != '+' ||
        !((sum_update->src1 == sum_phi->dst && sum_update->src2 == n_phi->dst) ||
          (sum_update->src2 == sum_phi->dst && sum_update->src1 == n_phi->dst)))
        return 0;
    if (compare->src1 != n_phi->dst || !mir_is_const_value(compare->src2, 0) ||
        return_insn->src1 != sum_phi->dst)
        return 0;
    if (!((n_phi->src1 == parameter->dst && n_phi->src2 == n_update->dst) ||
          (n_phi->src2 == parameter->dst && n_phi->src1 == n_update->dst)))
        return 0;
    if (!((mir_is_const_value(sum_phi->src1, 0) &&
           sum_phi->src2 == sum_update->dst) ||
          (mir_is_const_value(sum_phi->src2, 0) &&
           sum_phi->src1 == sum_update->dst)))
        return 0;

    unsigned_value = (mir.objects[n_object].type & TYPE_UNSIGNED) != 0 ||
                     type_ptr_depth(mir.objects[n_object].type) > 0;
    top_label = new_label();
    end_label = new_label();
    mir_emit_prologue(out);
    fprintf(out, "\tld c,(ix%+d)\n", mir.objects[n_object].offset);
    fprintf(out, "\tld b,(ix%+d)\n", mir.objects[n_object].offset + 1);
    fputs("\tld de,0\n", out);
    fprintf(out, "L%d:\n", top_label);
    if (unsigned_value) {
        fputs("\tld a,b\n\tor c\n", out);
        fprintf(out, "\tjp z, L%d\n", end_label);
    } else {
        fputs("\tld a,b\n\tor a\n", out);
        fprintf(out, "\tjp m, L%d\n", end_label);
        fputs("\tor c\n", out);
        fprintf(out, "\tjp z, L%d\n", end_label);
    }
    fputs("\tex de,hl\n\tadd hl,bc\n\tex de,hl\n\tdec bc\n", out);
    fprintf(out, "\tjp L%d\n", top_label);
    fprintf(out, "L%d:\n", end_label);
    fputs("\tex de,hl\n\tld sp,ix\n\tpop ix\n\tret\n", out);
    return 1;
}

/* Unsigned quotient/remainder loop:
 *
 *     q = 0;
 *     while (K <= r) { r -= K; ++q; }
 *     return q;
 *
 * BC holds r and DE holds q. Adding -K to BC in HL provides both the
 * unsigned loop test (carry means r >= K) and the updated remainder. */
static int mir_try_emit_unsigned_division_loop(FILE *out)
{
    const struct MirInsn *parameter = NULL;
    const struct MirInsn *remainder_phi = NULL;
    const struct MirInsn *quotient_phi = NULL;
    const struct MirInsn *remainder_update = NULL;
    const struct MirInsn *quotient_update = NULL;
    const struct MirInsn *compare = NULL;
    const struct MirInsn *return_insn = NULL;
    const struct MirInsn *divisor_definition;
    int remainder_object = -1;
    int quotient_object = -1;
    long divisor;
    int top_label;
    int end_label;
    int i;

    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->opcode == MIR_PARAM) {
            if (parameter != NULL)
                return 0;
            parameter = insn;
            remainder_object = insn->object;
        }
    }
    if (parameter == NULL || remainder_object < 0)
        return 0;
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->opcode == MIR_PHI) {
            if (insn->object == remainder_object)
                remainder_phi = insn;
            else if (quotient_phi == NULL) {
                quotient_phi = insn;
                quotient_object = insn->object;
            } else {
                return 0;
            }
        } else if (insn->opcode == MIR_STORE) {
            const struct MirInsn *definition = mir_definition(insn->src1);
            if (insn->object == remainder_object)
                remainder_update = definition;
            else if (insn->object == quotient_object || quotient_object < 0)
                quotient_update = definition;
        } else if (insn->opcode == MIR_BRANCH_FALSE) {
            const struct MirInsn *candidate = mir_definition(insn->src1);
            if (candidate != NULL && candidate->opcode == MIR_BINARY &&
                candidate->immediate == TOK_LE)
                compare = candidate;
        } else if (insn->opcode == MIR_RETURN) {
            return_insn = insn;
        } else if (insn->opcode == MIR_CALL || insn->opcode == MIR_OPAQUE ||
                   insn->opcode == MIR_INDEX_LOAD || insn->opcode == MIR_ARG) {
            return 0;
        }
    }
    if (remainder_phi == NULL || quotient_phi == NULL ||
        remainder_update == NULL || quotient_update == NULL ||
        compare == NULL || return_insn == NULL || quotient_object < 0)
        return 0;
    if (remainder_update->opcode != MIR_BINARY ||
        remainder_update->immediate != '-' ||
        remainder_update->src1 != remainder_phi->dst ||
        quotient_update->opcode != MIR_BINARY ||
        quotient_update->immediate != '+' ||
        quotient_update->src1 != quotient_phi->dst ||
        !mir_is_const_value(quotient_update->src2, 1))
        return 0;
    divisor_definition = mir_definition(remainder_update->src2);
    if (divisor_definition == NULL ||
        divisor_definition->opcode != MIR_CONST)
        return 0;
    if (compare->src2 != remainder_phi->dst ||
        !mir_is_const_value(compare->src1, divisor_definition->immediate) ||
        return_insn->src1 != quotient_phi->dst)
        return 0;
    divisor = divisor_definition->immediate;
    if (divisor <= 0 || divisor > 32768)
        return 0;
    if (!((remainder_phi->src1 == parameter->dst &&
           remainder_phi->src2 == remainder_update->dst) ||
          (remainder_phi->src2 == parameter->dst &&
           remainder_phi->src1 == remainder_update->dst)))
        return 0;
    if (!((mir_is_const_value(quotient_phi->src1, 0) &&
           quotient_phi->src2 == quotient_update->dst) ||
          (mir_is_const_value(quotient_phi->src2, 0) &&
           quotient_phi->src1 == quotient_update->dst)))
        return 0;
    if (mir.objects[remainder_object].storage != SC_PARAM ||
        type_size(mir.objects[remainder_object].type) != 2 ||
        (mir.objects[remainder_object].type & TYPE_UNSIGNED) == 0 ||
        type_size(mir.objects[quotient_object].type) != 2 ||
        (mir.objects[quotient_object].type & TYPE_UNSIGNED) == 0)
        return 0;

    top_label = new_label();
    end_label = new_label();
    mir_emit_prologue(out);
    fprintf(out, "\tld c,(ix%+d)\n", mir.objects[remainder_object].offset);
    fprintf(out, "\tld b,(ix%+d)\n",
            mir.objects[remainder_object].offset + 1);
    fputs("\tld de,0\n", out);
    fprintf(out, "L%d:\n", top_label);
    fprintf(out, "\tld hl,%ld\n\tadd hl,bc\n", -divisor);
    fprintf(out, "\tjp nc, L%d\n", end_label);
    fputs("\tld b,h\n\tld c,l\n\tinc de\n", out);
    fprintf(out, "\tjp L%d\n", top_label);
    fprintf(out, "L%d:\n", end_label);
    fputs("\tex de,hl\n\tld sp,ix\n\tpop ix\n\tret\n", out);
    return 1;
}

/* Three-register invariant-add loop:
 *
 *     total = 0;
 *     for (i = 0; i < K; ++i) {
 *         total += factor;
 *         total += factor;
 *     }
 *
 * IY holds the loop-invariant 2*factor, BC holds i and DE holds total. */
static int mir_try_emit_repeated_invariant_add_loop(FILE *out)
{
    const struct MirInsn *parameter = NULL;
    const struct MirInsn *total_phi = NULL;
    const struct MirInsn *index_phi = NULL;
    const struct MirInsn *first_add = NULL;
    const struct MirInsn *second_add = NULL;
    const struct MirInsn *index_update = NULL;
    const struct MirInsn *compare = NULL;
    const struct MirInsn *return_insn = NULL;
    int factor_values[2];
    int factor_load_count = 0;
    int factor_object = -1;
    int total_object = -1;
    int index_object = -1;
    long limit;
    int top_label;
    int end_label;
    int i;

    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->opcode == MIR_PARAM) {
            if (parameter != NULL)
                return 0;
            parameter = insn;
            factor_object = insn->object;
        } else if (insn->opcode == MIR_LOAD &&
                   insn->object == factor_object) {
            if (factor_load_count >= 2)
                return 0;
            factor_values[factor_load_count++] = insn->dst;
        }
    }
    if (parameter == NULL || factor_object < 0)
        return 0;
    if (factor_load_count == 0) {
        factor_values[0] = parameter->dst;
        factor_values[1] = parameter->dst;
    } else if (factor_load_count != 2) {
        return 0;
    }
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->opcode == MIR_PHI) {
            if (total_phi == NULL) {
                total_phi = insn;
                total_object = insn->object;
            } else if (index_phi == NULL) {
                index_phi = insn;
                index_object = insn->object;
            } else {
                return 0;
            }
        } else if (insn->opcode == MIR_STORE) {
            const struct MirInsn *definition = mir_definition(insn->src1);
            if (total_phi != NULL && insn->object == total_object) {
                if (definition != NULL && definition->opcode == MIR_BINARY &&
                    (definition->src1 == factor_values[1] ||
                     definition->src2 == factor_values[1]) &&
                    definition->src1 != total_phi->dst &&
                    definition->src2 != total_phi->dst)
                    second_add = definition;
                else if (definition != NULL &&
                         definition->opcode == MIR_BINARY)
                    first_add = definition;
            } else if (index_phi != NULL && insn->object == index_object) {
                index_update = definition;
            }
        } else if (insn->opcode == MIR_BRANCH_FALSE) {
            const struct MirInsn *candidate = mir_definition(insn->src1);
            if (candidate != NULL && candidate->opcode == MIR_BINARY &&
                candidate->immediate == '<')
                compare = candidate;
        } else if (insn->opcode == MIR_RETURN) {
            return_insn = insn;
        } else if (insn->opcode == MIR_CALL || insn->opcode == MIR_OPAQUE ||
                   insn->opcode == MIR_INDEX_LOAD || insn->opcode == MIR_ARG) {
            return 0;
        }
    }
    if (total_phi == NULL || index_phi == NULL || first_add == NULL ||
        second_add == NULL || index_update == NULL || compare == NULL ||
        return_insn == NULL || total_object < 0 || index_object < 0)
        return 0;
    if (first_add->immediate != '+' ||
        !((first_add->src1 == total_phi->dst &&
              first_add->src2 == factor_values[0]) ||
          (first_add->src2 == total_phi->dst &&
              first_add->src1 == factor_values[0])))
        return 0;
    if (second_add->immediate != '+' ||
        !((second_add->src1 == first_add->dst &&
              second_add->src2 == factor_values[1]) ||
          (second_add->src2 == first_add->dst &&
              second_add->src1 == factor_values[1])))
        return 0;
    if (index_update->opcode != MIR_BINARY || index_update->immediate != '+' ||
        index_update->src1 != index_phi->dst ||
        !mir_is_const_value(index_update->src2, 1) ||
        compare->src1 != index_phi->dst || return_insn->src1 != total_phi->dst)
        return 0;
    {
        const struct MirInsn *limit_definition = mir_definition(compare->src2);
        if (limit_definition == NULL || limit_definition->opcode != MIR_CONST)
            return 0;
        limit = limit_definition->immediate;
    }
    if (limit <= 0 || limit > 32768)
        return 0;
    if (!((mir_is_const_value(total_phi->src1, 0) &&
           total_phi->src2 == second_add->dst) ||
          (mir_is_const_value(total_phi->src2, 0) &&
           total_phi->src1 == second_add->dst)) ||
        !((mir_is_const_value(index_phi->src1, 0) &&
           index_phi->src2 == index_update->dst) ||
          (mir_is_const_value(index_phi->src2, 0) &&
           index_phi->src1 == index_update->dst)))
        return 0;
    if (mir.objects[factor_object].storage != SC_PARAM ||
        type_size(mir.objects[factor_object].type) != 2 ||
        type_size(mir.objects[total_object].type) != 2 ||
        (type_size(mir.objects[index_object].type) != 2 &&
         type_size(mir.objects[index_object].type) != 1) ||
        (type_size(mir.objects[index_object].type) == 1 && limit > 255))
        return 0;

    top_label = new_label();
    end_label = new_label();
    mir_emit_iy_prologue(out);
    fprintf(out, "\tld l,(ix%+d)\n", mir.objects[factor_object].offset + 2);
    fprintf(out, "\tld h,(ix%+d)\n", mir.objects[factor_object].offset + 3);
    fputs("\tadd hl,hl\n\tpush hl\n\tpop iy\n", out);
    fputs("\tld bc,0\n\tld de,0\n", out);
    fprintf(out, "L%d:\n", top_label);
    fprintf(out, "\tld hl,%ld\n\tadd hl,bc\n", -limit);
    fprintf(out, "\tjp c, L%d\n", end_label);
    fputs("\tpush iy\n\tpop hl\n\tadd hl,de\n\tex de,hl\n\tinc bc\n", out);
    fprintf(out, "\tjp L%d\n", top_label);
    fprintf(out, "L%d:\n", end_label);
    fputs("\tex de,hl\n\tld sp,ix\n\tpop ix\n\tpop iy\n\tret\n", out);
    return 1;
}

/* Strict first CFG selector:
 *
 *     if (a == b) return C1; return C2;
 *
 * (or !=). This validates labels, branch polarity, two live inputs and
 * multiple exits without claiming general relational/comparison support. */
static int mir_try_emit_comparison_branch(FILE *out)
{
    const struct MirInsn *branch = NULL;
    const struct MirInsn *compare;
    const struct MirInsn *left;
    const struct MirInsn *right;
    const struct MirInsn *true_return = NULL;
    const struct MirInsn *false_return = NULL;
    const struct MirInsn *true_value;
    const struct MirInsn *false_value;
    int branch_index = -1;
    int target_index;
    int false_label;
    int operation;
    int unsigned_compare;
    int i;

    for (i = 0; i < mir.count; ++i) {
        if (mir.insns[i].opcode == MIR_BRANCH_FALSE) {
            if (branch != NULL)
                return 0;
            branch = &mir.insns[i];
            branch_index = i;
        }
    }
    if (branch == NULL)
        return 0;
    target_index = mir_find_label(branch->label);
    if (target_index <= branch_index)
        return 0;
    compare = mir_definition(branch->src1);
    if (compare == NULL)
        return 0;
    if (compare->opcode == MIR_PARAM) {
        /* Item T72 (mir-text-size-plan.md): `if (param) return A;
         * return B;` - a bare truthiness test with no explicit
         * comparison instruction at all (the branch tests the
         * parameter's value directly), found via tests/tctxflt.c's
         * `truth_if(float f) { if (f) return 1; return 0; }`. `right`
         * has no counterpart in this shape. */
        left = compare;
        right = NULL;
    } else if (compare->opcode == MIR_BINARY &&
               (compare->immediate == TOK_EQ || compare->immediate == TOK_NE ||
                compare->immediate == '<' || compare->immediate == TOK_GE ||
                compare->immediate == '>' || compare->immediate == TOK_LE)) {
        left = mir_definition(compare->src1);
        right = mir_definition(compare->src2);
        if (left == NULL || right == NULL || left->opcode != MIR_PARAM ||
            right->opcode != MIR_PARAM)
            return 0;
    } else {
        return 0;
    }
    if (left->object < 0 || left->object >= mir.object_count)
        return 0;
    for (i = branch_index + 1; i < target_index; ++i)
        if (mir.insns[i].opcode == MIR_RETURN)
            true_return = &mir.insns[i];
    for (i = target_index + 1; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_RETURN) {
            false_return = &mir.insns[i];
            break;
        }
    if (true_return == NULL || false_return == NULL)
        return 0;
    true_value = mir_definition(true_return->src1);
    false_value = mir_definition(false_return->src1);
    if (true_value == NULL || false_value == NULL ||
        true_value->opcode != MIR_CONST || false_value->opcode != MIR_CONST)
        return 0;

    /* Reject any operation outside this exact graph shape. */
    for (i = 0; i < mir.count; ++i) {
        int opcode = mir.insns[i].opcode;
        if (opcode != MIR_PARAM && opcode != MIR_NOP && opcode != MIR_CONST &&
            opcode != MIR_BINARY && opcode != MIR_BRANCH_FALSE &&
            opcode != MIR_LABEL && opcode != MIR_RETURN)
            return 0;
    }

    /* Item T72 (mir-text-size-plan.md): the bare-truthiness shape
     * identified above (`compare->opcode == MIR_PARAM`) tests the
     * parameter's own value for nonzero, exactly mirroring the
     * MIR_BRANCH_FALSE zero-test the general spilled-scalar-cfg
     * selector already emits (dcc_mir_spilled_cfg.c) - including its
     * float handling, which masks off the sign bit before the OR chain
     * so `-0.0` is correctly treated as false, not true. Declines
     * (returns 0) for any parameter width other than 2 bytes (scalar/
     * pointer) or 4 bytes (`long`/`float`), matching
     * mir_emit_load_param/_wide's own exact width requirements, so an
     * unexpected width falls back to the general selector instead of
     * emitting nothing. */
    if (compare->opcode == MIR_PARAM) {
        int width = type_size(mir.objects[left->object].type);
        int is_float = type_is_float(mir.objects[left->object].type);

        if (width != 2 && width != 4)
            return 0;
        false_label = new_label();
        mir_emit_prologue(out);
        if (width == 4) {
            if (!mir_emit_load_param_wide(out, left))
                return 0;
            if (is_float)
                fputs("\tld a,d\n\tand 127\n\tor e\n\tor h\n\tor l\n", out);
            else
                fputs("\tld a,d\n\tor e\n\tor h\n\tor l\n", out);
        } else {
            if (!mir_emit_load_param(out, left))
                return 0;
            fputs("\tld a,h\n\tor l\n", out);
        }
        fprintf(out, "\tjp z, L%d\n", false_label);
        {
            int epilogue_label = new_label();
            fprintf(out, "\tld hl,%ld\n", true_value->immediate);
            fprintf(out, "\tjp L%d\n", epilogue_label);
            fprintf(out, "L%d:\n", false_label);
            fprintf(out, "\tld hl,%ld\n", false_value->immediate);
            fprintf(out, "L%d:\n", epilogue_label);
            fputs("\tld sp,ix\n\tpop ix\n\tret\n", out);
        }
        return 1;
    }

    /* Item T57 (mir-text-size-plan.md): a wide (4-byte, `long`/`float`)
     * comparison in this exact shape used to be rejected outright by
     * mir_emit_load_param/_de's own `type_size == 2` requirement,
     * falling through to the general spilled-scalar-cfg selector - a
     * needless loss, since mir_emit_wide_operation (already relied on,
     * and already verified for float by Item T53) handles every wide
     * comparison operator directly, with no operand-order normalization
     * needed (unlike the narrow path's sign-bias trick below, which
     * only exists to reuse a single unsigned 16-bit `sbc`). Left/right
     * are used exactly as `compare` originally defined them - no swap. */
    if (type_size(compare->secondary_offset) == 4) {
        false_label = new_label();
        mir_emit_prologue(out);
        if (!mir_emit_load_param_wide(out, left))
            return 0;
        fputs("\tpush de\n\tpush hl\n", out);
        if (!mir_emit_load_param_wide(out, right))
            return 0;
        if (!mir_emit_wide_operation(out, compare))
            return 0;
        fputs("\tld a,h\n\tor l\n", out);
        fprintf(out, "\tjp z, L%d\n", false_label);
        {
            int epilogue_label = new_label();
            fprintf(out, "\tld hl,%ld\n", true_value->immediate);
            fprintf(out, "\tjp L%d\n", epilogue_label);
            fprintf(out, "L%d:\n", false_label);
            fprintf(out, "\tld hl,%ld\n", false_value->immediate);
            fprintf(out, "L%d:\n", epilogue_label);
            fputs("\tld sp,ix\n\tpop ix\n\tret\n", out);
        }
        return 1;
    }

    operation = (int)compare->immediate;
    if (operation == '>') {
        const struct MirInsn *temporary = left;
        left = right;
        right = temporary;
        operation = '<';
    } else if (operation == TOK_LE) {
        const struct MirInsn *temporary = left;
        left = right;
        right = temporary;
        operation = TOK_GE;
    }
    unsigned_compare =
        (mir.objects[left->object].type & TYPE_UNSIGNED) != 0 ||
        type_ptr_depth(mir.objects[left->object].type) > 0;

    false_label = new_label();
    mir_emit_prologue(out);
    if (!mir_emit_load_param(out, left) || !mir_emit_load_param_de(out, right))
        return 0;
    if (!unsigned_compare && (operation == '<' || operation == TOK_GE)) {
        /* Bias the sign bit on both operands, mapping signed order onto
         * unsigned order before the ordinary 16-bit subtract. */
        fputs("\tld a,h\n\txor 80h\n\tld h,a\n"
              "\tld a,d\n\txor 80h\n\tld d,a\n", out);
    }
    fputs("\tor a\n\tsbc hl,de\n", out);
    if (operation == TOK_EQ)
        fprintf(out, "\tjp nz, L%d\n", false_label);
    else if (operation == TOK_NE)
        fprintf(out, "\tjp z, L%d\n", false_label);
    else if (operation == '<')
        fprintf(out, "\tjp nc, L%d\n", false_label);
    else
        fprintf(out, "\tjp c, L%d\n", false_label);
    /* Share one epilogue between both return paths instead of calling
     * mir_emit_return_constant twice (which would duplicate
     * "ld sp,ix / pop ix / ret" in full for each side). Legacy's own
     * capture for this exact shape already merges both paths into a
     * single epilogue via a short jump - measured directly: duplicating
     * it instead made every peep-mode .COM in tests/tmirfuse.c *larger*
     * than legacy despite every individual function's raw generated byte
     * count being smaller pre-peephole, because dccpeep's jp-to-jr/
     * dead-epilogue passes were tuned to legacy's merged shape. */
    {
        int epilogue_label = new_label();
        fprintf(out, "\tld hl,%ld\n", true_value->immediate);
        fprintf(out, "\tjp L%d\n", epilogue_label);
        fprintf(out, "L%d:\n", false_label);
        fprintf(out, "\tld hl,%ld\n", false_value->immediate);
        fprintf(out, "L%d:\n", epilogue_label);
        fputs("\tld sp,ix\n\tpop ix\n\tret\n", out);
    }
    return 1;
}

/* Item T71 (mir-text-size-plan.md): unlike legacy's emit_runtime_extrn_if_needed
 * (dcc_symbols.c), which dedups a runtime/external symbol's EXTRN line against
 * a cache that persists for the whole compilation, the spilled-scalar-cfg call/
 * global-load emitters (dcc_mir_spilled_cfg.c) re-check only the symbol's
 * static `needs_extrn` property at every reference site, so a function that
 * calls (or reads) the same still-undefined external symbol more than once -
 * e.g. two `printf` calls in one if/else, found via tests/taddr.c's chki() -
 * re-emits an identical `extrn NAME` line per call, purely inflating the
 * generated-assembly-text byte count `mir_stream_size` uses for the text-size
 * acceptance gate (that gate has no correlation to real Z80 machine bytes; an
 * EXTRN is an assembler-time-only declaration). A whole-compilation cache
 * (mirroring legacy exactly) is unsafe here: a rejected/discarded selector
 * attempt (mir_select_and_emit tries several selectors per function in
 * sequence) writes its own EXTRN lines into a throwaway tmpfile before its
 * acceptance is known, so marking a symbol "done" during a discarded attempt
 * would wrongly suppress a needed EXTRN in a later, actually-accepted stream -
 * the same class of hazard the g_inline_body_buffering epoch comment
 * (dcc_symbols.c) already documents for legacy's own buffering path. Instead,
 * mir_extrn_begin_attempt() bumps a generation counter once per
 * mir_try_selector() call (i.e. once per independent candidate stream build,
 * whether or not it is ultimately accepted), and mir_extrn_should_emit()
 * stamps a symbol with that generation the first time it is asked about
 * within the current attempt only - safe because every accepted stream is
 * itself one complete, self-contained mir_try_selector() call, and never
 * spans two attempts. This only dedupes within one candidate's own text, not
 * across the whole file the way legacy does, so a function is not penalized
 * for a sibling function's earlier EXTRN of the same symbol - a smaller but
 * strictly safe subset of legacy's optimization. */
static int mir_extrn_attempt_generation = 1;
#define MIR_EXTRN_NAME_CAPACITY 64
static const char *mir_extrn_emitted_names[MIR_EXTRN_NAME_CAPACITY];
static int mir_extrn_emitted_name_count;

void mir_extrn_begin_attempt(void)
{
    ++mir_extrn_attempt_generation;
    mir_extrn_emitted_name_count = 0;
}

int mir_extrn_should_emit(struct Sym *sym)
{
    if (sym == NULL)
        return 1;
    if (sym->mir_extrn_attempt_stamp == mir_extrn_attempt_generation)
        return 0;
    sym->mir_extrn_attempt_stamp = mir_extrn_attempt_generation;
    return 1;
}

/* DCCRTL runtime helpers (__mulu, __sdivmod, __icf, __call_hl, and the
 * float support entry points) have no struct Sym at all - they are plain
 * string literals threaded straight from each fastcall/instruction-
 * selection site to an "extrn NAME\ncall NAME\n" pair, unconditionally,
 * every time that site fires. tests/tstrcmpi.c's main() (6 direct calls
 * to stricmp, each lowered to the __icf fastcall) showed this is the same
 * duplicate-EXTRN text-size padding mir_extrn_should_emit() above already
 * fixed for struct-Sym-backed callees/globals, just keyed by name instead
 * of by symbol - confirmed via legacy's captured output, which emits
 * "extrn __icf" exactly once for the whole function (legacy routes every
 * runtime-helper reference through emit_runtime_extrn_if_needed's
 * (dcc_symbols.c) persistent per-name cache). This table is reset by
 * mir_extrn_begin_attempt() every mir_try_selector() attempt, for the
 * identical reason mir_extrn_attempt_generation is (see the comment
 * above) - a discarded candidate attempt must never suppress a needed
 * EXTRN in a later, actually-accepted stream. */
int mir_extrn_should_emit_name(const char *name)
{
    int i;

    if (name == NULL)
        return 1;
    for (i = 0; i < mir_extrn_emitted_name_count; ++i)
        if (!strcmp(mir_extrn_emitted_names[i], name))
            return 0;
    if (mir_extrn_emitted_name_count < MIR_EXTRN_NAME_CAPACITY)
        mir_extrn_emitted_names[mir_extrn_emitted_name_count++] = name;
    return 1;
}

void mir_emit_runtime_call(FILE *out, const char *name)
{
    if (mir_extrn_should_emit_name(name))
        fprintf(out, "\textrn %s\n", name);
    fprintf(out, "\tcall %s\n", name);
}

/* First emitted subset: one straight-line return of a word parameter,
 * constant, or parameter +/- constant. This intentionally proves the
 * transactional backend path before attempting general instruction
 * selection. Anything else falls back byte-for-byte to the captured existing
 * codegen. */
static int mir_try_selector(FILE *out, int (*selector)(FILE *))
{
    FILE *candidate = tmpfile();
    int accepted;
    int character;

    if (candidate == NULL)
        fatal("cannot create MIR selector stream");
    mir_extrn_begin_attempt();
    accepted = selector(candidate);
    if (accepted) {
        rewind(candidate);
        while ((character = fgetc(candidate)) != EOF)
            fputc(character, out);
    }
    fclose(candidate);
    return accepted;
}

static void mir_init_spilled_candidate(
    struct MirCandidateDescriptor *candidate, const char *name,
    const char *stream_error, unsigned long features)
{
    candidate->name = name;
    candidate->stream_error = stream_error;
    candidate->selector = mir_try_emit_spilled_scalar_cfg;
    candidate->spilled_features = features;
}

static void mir_build_spilled_candidate(
    const struct MirCandidateDescriptor *candidate,
    struct MirCandidateResult *result, int label_base)
{
    result->descriptor = candidate;
    result->stream = tmpfile();
    result->emitted = 0;
    result->label_id_after = label_base;
    result->generated_size = -1;
    result->generated_instructions = -1;
    result->reason = "selector";
    if (result->stream == NULL)
        fatal(candidate->stream_error);

    /*
     * Every feature set owns one fresh stream and one balanced state scope.
     * A declined candidate therefore cannot leak text, labels, or feature
     * state into the next attempt.
     */
    mir_configure_spilled_fallback_features(
        candidate->spilled_features, 1);
    label_id = label_base;
    result->emitted = mir_try_selector(result->stream, candidate->selector);
    result->label_id_after = label_id;
    mir_configure_spilled_fallback_features(
        candidate->spilled_features, 0);
    if (result->emitted) {
        result->generated_size = mir_stream_size(result->stream);
        result->generated_instructions =
            mir_stream_instruction_count(result->stream);
        result->reason = "emitted";
    }
}

static void mir_close_candidate_result(struct MirCandidateResult *result)
{
    if (result->stream != NULL)
        fclose(result->stream);
    result->stream = NULL;
}

static int mir_adopt_candidate_result(
    FILE **generated, struct MirCandidateResult *result)
{
    if (!result->emitted)
        return 0;
    fclose(*generated);
    *generated = result->stream;
    result->stream = NULL;
    return 1;
}

long mir_stream_size(FILE *stream)
{
    long position = ftell(stream);
    long size;

    if (position < 0 || fseek(stream, 0, SEEK_END) != 0)
        return -1;
    size = ftell(stream);
    if (fseek(stream, position, SEEK_SET) != 0)
        return -1;
    return size;
}

static unsigned long mir_copy_selected_stream(FILE *source, FILE *destination)
{
    unsigned long hash = 2166136261UL;
    int character;

    rewind(source);
    while ((character = fgetc(source)) != EOF) {
        hash ^= (unsigned long)(unsigned char)character;
        hash = (hash * 16777619UL) & 0xffffffffUL;
        fputc(character, destination);
    }
    return hash;
}

int mir_stream_instruction_count(FILE *stream)
{
    char line[512];
    long position = ftell(stream);
    int count = 0;

    if (position < 0 || fseek(stream, 0, SEEK_SET) != 0)
        return -1;
    while (fgets(line, sizeof(line), stream) != NULL) {
        char *text = line;
        char *end;
        while (*text == ' ' || *text == '\t')
            ++text;
        end = text + strlen(text);
        while (end > text && (end[-1] == '\n' || end[-1] == '\r' ||
                              end[-1] == ' ' || end[-1] == '\t'))
            *--end = 0;
        if (*text == 0 || *text == ';' || end[-1] == ':' ||
            !strncmp(text, "extrn ", 6) || !strncmp(text, "public ", 7) ||
            !strncmp(text, "cseg", 4) || !strncmp(text, "dseg", 4) ||
            !strncmp(text, "db ", 3) || !strncmp(text, "dw ", 3) ||
            !strncmp(text, "ds ", 3) || !strncmp(text, "equ ", 4))
            continue;
        ++count;
    }
    if (fseek(stream, position, SEEK_SET) != 0)
        return -1;
    return count;
}

static unsigned long mir_stream_hash(FILE *stream)
{
    unsigned long hash = 2166136261UL;
    long position = ftell(stream);
    int character;

    if (position < 0 || fseek(stream, 0, SEEK_SET) != 0)
        return 0;
    while ((character = fgetc(stream)) != EOF) {
        hash ^= (unsigned long)(unsigned char)character;
        hash = (hash * 16777619UL) & 0xffffffffUL;
    }
    if (fseek(stream, position, SEEK_SET) != 0)
        return 0;
    return hash;
}

static void mir_report_spilled_candidate_matrix(int label_base)
{
    static const struct {
        const char *name;
        unsigned long features;
    } candidates[] = {
        {"baseline", 0},
        {"rhs-forward", MIR_SPILLED_FEATURES_RHS},
        {"store-address", MIR_SPILLED_FEATURES_STORE_ADDRESS},
        {"wide-binary-lhs", MIR_SPILLED_FEATURES_WIDE_LHS},
        {"stable-pointer-argument", MIR_SPILLED_FEATURES_STABLE_ARG},
        {"global-argument", MIR_SPILLED_FEATURES_GLOBAL_ARG},
        {"stack-argument", MIR_SPILLED_FEATURES_CALL_STACK},
        {"promoted-local-slot", MIR_SPILLED_FEATURES_PROMOTED_LOCAL},
        {"all", MIR_SPILLED_FEATURES_ALL},
        {"phi-slot", MIR_SPILLED_FEATURES_PHI_SLOT}
    };
    int label_id_save = label_id;
    int mir_count_save = mir.count;
    struct MirInsn *insns_save;
    int i;

    insns_save = (struct MirInsn *)malloc(
        (size_t)mir_count_save * sizeof(*insns_save));
    if (insns_save == NULL)
        fatal("cannot save MIR candidate-matrix instructions");
    memcpy(insns_save, mir.insns,
           (size_t)mir_count_save * sizeof(*insns_save));
    for (i = 0;
         i < (int)(sizeof(candidates) / sizeof(candidates[0])); ++i) {
        struct MirCandidateDescriptor candidate;
        struct MirCandidateResult result;
        unsigned long hash = 0;

        if (mir.count != mir_count_save)
            fatal("MIR candidate-matrix changed instruction count");
        memcpy(mir.insns, insns_save,
               (size_t)mir_count_save * sizeof(*insns_save));
        mir_init_spilled_candidate(
            &candidate, candidates[i].name,
            "cannot create MIR candidate-matrix stream",
            candidates[i].features);
        mir_build_spilled_candidate(&candidate, &result, label_base);
        if (result.emitted)
            hash = mir_stream_hash(result.stream);
        fprintf(stderr,
                "; MIR candidate-matrix\tfunction=%s\tcandidate=%s"
                "\tmask=%08lx\temitted=%d\treason=%s\tbytes=%ld"
                "\tinsns=%d\tblocks=%d\tslots=%d\tcalls=%d"
                "\tlocals=%d\treturn-kind=%d\tvla=%d\tbackedge=%d"
                "\tinline-substitution=%d\tpointer-array=%d"
                "\tboolean-simplifications=%d"
                "\tlabel-phi-fallthrough=%d\twide-values=%d"
                "\thash=%08lx\n",
                mir.name, result.descriptor->name,
                result.descriptor->spilled_features, result.emitted,
                result.reason, result.generated_size,
                result.generated_instructions, mir_cfg_block_count(),
                mir.backend_slot_count, mir_call_count(), mir.local_bytes,
                mir.return_type & 15, mir.has_vla, mir_has_cfg_backedge(),
                mir_has_inline_substitution_call(),
                mir_has_declared_pointer_array(),
                mir_boolean_phi_branch_simplification_count(),
                mir_has_label_only_phi_fallthrough(), mir_has_wide_values(),
                hash);
        mir_close_candidate_result(&result);
    }
    if (mir.count != mir_count_save)
        fatal("MIR candidate-matrix changed instruction count");
    memcpy(mir.insns, insns_save,
           (size_t)mir_count_save * sizeof(*insns_save));
    free(insns_save);
    label_id = label_id_save;
}

/* Item T71 addendum (mir-text-size-plan.md): an attempted follow-up
 * predicate ("indexed-homing-cost") to also gate tests/tstrcmpi.c's
 * main() - a function newly admitted by this fix's genuine byte win
 * (1578 generated vs 1665 captured) that still showed a tiny real
 * peep-mode cycle regression (+0.06%, 32 cycles) rooted in its two
 * indirect (function-pointer) calls each homing their callee value to
 * two ix-relative bytes across their own argument-evaluation sequence,
 * rather than legacy's cheaper push/SP-relative-reconstruct idiom - was
 * designed and measured, then reverted. A blanket "more (ix+d) operands
 * than legacy" check cost 60 functions of coverage (483 -> 432) for this
 * one fix; scoping it to "contains an indirect call" narrowed the loss to
 * 5 functions, but 2 of those (tc89core.main, tsyntax's
 * test_casted_function_pointer_call) were pre-existing, already-baselined
 * real wins that this guard wrongly reverted to fallback, causing worse
 * regressions (+1.36%, +0.03%) than the one it fixed. No static
 * byte/instruction-count-derived predicate found this session reliably
 * separates tstrcmpi.main's true regression from these true wins - all
 * three have large, real byte savings and more indexed operands than
 * legacy. This is exactly the gap mir-migration-plan-100.md's "Item 2:
 * real T-state cost model" already identifies: a genuine dynamic/
 * instruction-mix cost model (via scripts/dccprof.ps1) is needed to
 * discriminate this class, not another static text-shape heuristic.
 * Deferred, same as Item 6's precedent: tstrcmpi.main is left accepted
 * with its known, tiny (32-cycle, 0.06%) peep-mode regression until that
 * infrastructure exists; do not attempt another static predicate for it
 * without new discriminating evidence. */

int mir_cfg_block_count(void)
{
    int blocks = 0;
    int i;

    for (i = 0; i < mir.count; ++i) {
        if (mir.insns[i].opcode == MIR_LABEL)
            ++blocks;
    }
    return blocks;
}

static int mir_has_inline_substitution_call(void)
{
    int i;

    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_CALL &&
            (mir.insns[i].memory_flags &
             MIR_CALL_FLAG_INLINE_SUBSTITUTABLE) != 0)
            return 1;
    return 0;
}

static void mir_mark_selected_inline_call_bodies_needed(void)
{
    int i;

    /* Legacy codegen substitutes static-inline helpers at the AST layer, but
     * accepted MIR can still carry a real call when a cost gate (or a forced
     * diagnostic accept) keeps the caller out of that path. Mark those
     * helpers' buffered bodies needed only once the MIR stream actually wins,
     * so the selected output never carries an unresolved call target. */
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        struct Sym *callee;

        if ((insn->opcode != MIR_CALL &&
             insn->opcode != MIR_CALL_AGGREGATE) ||
            (insn->memory_flags &
             MIR_CALL_FLAG_INLINE_SUBSTITUTABLE) == 0 ||
            insn->name[0] == 0)
            continue;
        callee = find_global(insn->name);
        if (callee != NULL)
            callee->deferred_body_needed = 1;
    }
}

static int mir_has_declared_pointer_array(void)
{
    int i;

    for (i = 0; i < mir.declared_count; ++i)
        if (type_ptr_depth(mir.declared_types[i]) > 0 &&
            mir.declared_dim_counts[i] > 0)
            return 1;
    return 0;
}

/* Item T63 (mir-text-size-plan.md): counts conditional tests in the
 * function, used to flag a chained if/else-if shape (2+ separate
 * MIR_BRANCH_FALSE instructions) as opposed to a single if/else (only
 * one). tests/tgoto.c's gt_switch() reloads its one spilled `int`
 * parameter from its stack slot separately for each of its two
 * sequential comparisons rather than keeping it live in a register
 * across the whole chain - a redundant-reload tax the byte-count
 * acceptance proxy does not see. tests/tlcont.c's main(), by contrast,
 * has only one MIR_BRANCH_FALSE (a single trailing if/else) and was
 * verified regression-free by a focused runall -Mode full run, so this
 * predicate must not trigger for it - a plain mir_cfg_block_count()
 * threshold would have wrongly excluded it too (both produce more than
 * 2 blocks once the branch's own blocks are counted). */
static int mir_has_multiple_conditional_tests(void)
{
    int i;
    int branch_falses = 0;

    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_BRANCH_FALSE)
            ++branch_falses;
    return branch_falses >= 2;
}

int mir_has_cfg_backedge(void)
{
    int i;
    int j;

    for (i = 0; i < mir.count; ++i) {
        int target;
        if (mir.insns[i].opcode != MIR_JUMP &&
            mir.insns[i].opcode != MIR_BRANCH_FALSE)
            continue;
        target = mir.insns[i].label;
        for (j = 0; j <= i; ++j)
            if (mir.insns[j].opcode == MIR_LABEL &&
                mir.insns[j].label == target)
                return 1;
    }
    return 0;
}

/* Item T421 (mir-text-size-plan.md) stratified the generic cfg-backedge
 * fallback residue into a "group A" of 9 functions sharing exactly one
 * reducible loop header (all backward MIR_JUMP/MIR_BRANCH_FALSE
 * instructions target the same, single earlier label - a natural loop,
 * possibly with more than one back-branch into it, e.g. a `continue`-like
 * path) with no MIR_CALL/MIR_CALL_AGGREGATE anywhere inside that loop
 * body - the structurally safest possible loop shape, distinct from the
 * riskier call-in-loop, parser/dispatch, and multiple-distinct-loop-header
 * shapes that remain in the residue. All 9 were independently
 * forced-correctness clean (peep and nopeep) and full-mode A/B measured;
 * the only reason they had not been admitted before the 2026-08-08
 * coverage-first policy pivot was each measuring a small deliberate
 * peep-cycle regression (+0.69% to +6.08%). This checks the *structural*
 * predicate T421 used to select group A, not a function-name list, so it
 * generalizes to any future candidate with the same shape. */
static int mir_has_single_reducible_backedge_without_loop_calls(void)
{
    int i;
    int j;
    int header_target = -1;
    int header_seen = 0;
    int loop_start = -1;
    int loop_end = -1;

    for (i = 0; i < mir.count; ++i) {
        int target;
        if (mir.insns[i].opcode != MIR_JUMP &&
            mir.insns[i].opcode != MIR_BRANCH_FALSE)
            continue;
        target = mir.insns[i].label;
        for (j = 0; j <= i; ++j)
            if (mir.insns[j].opcode == MIR_LABEL &&
                mir.insns[j].label == target) {
                if (!header_seen) {
                    header_seen = 1;
                    header_target = target;
                    loop_start = j;
                } else if (target != header_target) {
                    /* A second, distinct loop header - not the simple
                     * single-natural-loop shape group A requires. */
                    return 0;
                }
                if (i > loop_end)
                    loop_end = i;
                break;
            }
    }
    if (!header_seen)
        return 0;
    for (i = loop_start; i <= loop_end; ++i)
        if (mir.insns[i].opcode == MIR_CALL ||
            mir.insns[i].opcode == MIR_CALL_AGGREGATE)
            return 0;
    return 1;
}

static int mir_has_wide_values(void)
{
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction)
        if (type_size(mir.insns[instruction].type) == 4)
            return 1;
    return 0;
}

static int mir_has_format_runtime_call(void)
{
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_CALL &&
            (mir.insns[instruction].memory_flags &
             MIR_CALL_FLAG_FORMAT_RUNTIME) != 0)
            return 1;
    return 0;
}

static int mir_has_printf_family_call(void)
{
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_CALL &&
            asm_printf_family_fmt_arg_index(
                mir.insns[instruction].name) >= 0)
            return 1;
    return 0;
}

static int mir_call_count(void)
{
    int count = 0;
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_CALL ||
            mir.insns[instruction].opcode == MIR_CALL_AGGREGATE)
            ++count;
    return count;
}

static int mir_is_call_heavy_general_compare(void)
{
    int branches = 0;
    int calls = 0;
    int comparison_branches = 0;
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        if (mir.insns[instruction].opcode == MIR_CALL)
            ++calls;
        else if (mir.insns[instruction].opcode == MIR_BRANCH_FALSE) {
            ++branches;
            if (mir_compare_definition_for_branch(instruction) >= 0)
                ++comparison_branches;
        }
    }
    return calls >= 3 && branches == comparison_branches &&
           mir_general_comparison_count() != 0;
}

static int mir_is_profiled_near_cost_single_block(long generated_size,
                                                   long captured_size,
                                                   int generated_instructions,
                                                   int captured_instructions)
{
    return !mir.has_vla && mir_cfg_block_count() == 1 &&
           generated_size <= captured_size + 24 &&
           generated_instructions <= captured_instructions + 2;
}

static int mir_is_byte_profitable_single_block(long generated_size,
                                                long captured_size,
                                                int generated_instructions,
                                                int captured_instructions)
{
    return !mir.has_vla && mir_cfg_block_count() == 1 &&
           generated_size <= captured_size - 20 &&
           generated_instructions <= captured_instructions + 3;
}

static int mir_is_profiled_indirect_rmw_single_block(
    long generated_size, long captured_size, int generated_instructions,
    int captured_instructions)
{
    const struct MirInsn *load = NULL;
    const struct MirInsn *binary = NULL;
    const struct MirInsn *store = NULL;
    int i;

    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        mir.allocation_spill_count != 0 ||
        generated_size > captured_size + 16 ||
        generated_instructions > captured_instructions + 4)
        return 0;
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->opcode == MIR_LOAD_INDIRECT) {
            if (load != NULL)
                return 0;
            load = insn;
        } else if (insn->opcode == MIR_BINARY) {
            if (binary != NULL)
                return 0;
            binary = insn;
        } else if (insn->opcode == MIR_STORE_INDIRECT) {
            if (store != NULL)
                return 0;
            store = insn;
        } else if (insn->opcode == MIR_CALL ||
                   insn->opcode == MIR_CALL_AGGREGATE ||
                   insn->opcode == MIR_BRANCH_FALSE ||
                   insn->opcode == MIR_JUMP ||
                   insn->opcode == MIR_PHI)
            return 0;
    }
    if (load == NULL || binary == NULL || store == NULL ||
        store->src1 != load->src1 || store->src2 != binary->dst)
        return 0;
    return binary->src1 == load->dst || binary->src2 == load->dst;
}

static int mir_is_profiled_pointer_offset_picker(
    long generated_size, long captured_size, int generated_instructions,
    int captured_instructions, int base_opcode, int size_margin,
    int instruction_margin)
{
    const struct MirInsn *base = NULL;
    const struct MirInsn *scale = NULL;
    const struct MirInsn *add = NULL;
    const struct MirInsn *load = NULL;
    const struct MirInsn *ret = NULL;
    int i;

    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        mir.allocation_spill_count != 0 ||
        type_ptr_depth(mir.return_type) == 0 ||
        generated_size > captured_size + size_margin ||
        generated_instructions > captured_instructions +
                                 instruction_margin)
        return 0;
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->opcode == base_opcode) {
            if (base != NULL)
                return 0;
            base = insn;
            if (base_opcode == MIR_LOAD &&
                type_ptr_depth(insn->type) == 0)
                return 0;
        } else if (base_opcode == MIR_LOAD &&
                   insn->opcode == MIR_MEMBER_ADDRESS) {
            return 0;
        } else if (insn->opcode == MIR_BINARY) {
            const struct MirInsn *right = mir_definition(insn->src2);
            if (insn->immediate == '*' && right != NULL &&
                right->opcode == MIR_CONST && right->immediate == 2) {
                if (scale != NULL)
                    return 0;
                scale = insn;
            } else if (insn->immediate == '+') {
                if (add != NULL)
                    return 0;
                add = insn;
            } else
                return 0;
        } else if (insn->opcode == MIR_LOAD_INDIRECT) {
            if (load != NULL)
                return 0;
            load = insn;
        } else if (insn->opcode == MIR_RETURN) {
            if (ret != NULL)
                return 0;
            ret = insn;
        }
        else if (insn->opcode == MIR_CALL ||
                 insn->opcode == MIR_CALL_AGGREGATE ||
                 insn->opcode == MIR_BRANCH_FALSE ||
                 insn->opcode == MIR_JUMP ||
                 insn->opcode == MIR_PHI)
            return 0;
        else if (base_opcode == MIR_LOAD &&
                 insn->opcode != MIR_NOP &&
                 insn->opcode != MIR_LABEL &&
                 insn->opcode != MIR_PARAM &&
                 insn->opcode != MIR_CONST)
            return 0;
    }
    if (base == NULL || scale == NULL || add == NULL || load == NULL ||
        ret == NULL || ret->src1 != load->dst || load->src1 != add->dst)
        return 0;
    return ((add->src1 == base->dst && add->src2 == scale->dst) ||
            (add->src2 == base->dst && add->src1 == scale->dst));
}

static int mir_is_profiled_pointer_member_picker(
    long generated_size, long captured_size, int generated_instructions,
    int captured_instructions)
{
    return mir_is_profiled_pointer_offset_picker(
        generated_size, captured_size, generated_instructions,
        captured_instructions, MIR_MEMBER_ADDRESS, 32, 5);
}

static int mir_is_profiled_pointer_index_picker(
    long generated_size, long captured_size, int generated_instructions,
    int captured_instructions)
{
    return mir_is_profiled_pointer_offset_picker(
        generated_size, captured_size, generated_instructions,
        captured_instructions, MIR_LOAD, 16, 0);
}

static int mir_is_profiled_masked_memset_wrapper(
    long generated_size, long captured_size, int generated_instructions,
    int captured_instructions)
{
    const struct MirInsn *call = NULL;
    const struct MirInsn *mask = NULL;
    int calls = 0;
    int masks = 0;
    int matching_args = 0;
    int i;

    if (mir.has_vla || mir_cfg_block_count() != 1 ||
        mir.allocation_spill_count != 0 ||
        (mir.return_type & 15) != TYPE_VOID ||
        generated_size > captured_size + 48 ||
        generated_instructions > captured_instructions + 8)
        return 0;
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->opcode == MIR_CALL) {
            if (strcmp(insn->name, "memset") != 0)
                return 0;
            call = insn;
            ++calls;
        } else if (insn->opcode == MIR_BINARY && insn->immediate == '&') {
            const struct MirInsn *right = mir_definition(insn->src2);
            if (right == NULL || right->opcode != MIR_CONST ||
                right->immediate != 255)
                return 0;
            mask = insn;
            ++masks;
        } else if (insn->opcode == MIR_CALL_AGGREGATE ||
                   insn->opcode == MIR_BRANCH_FALSE ||
                   insn->opcode == MIR_JUMP ||
                   insn->opcode == MIR_PHI)
            return 0;
    }
    if (calls != 1 || masks != 1)
        return 0;
    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_ARG &&
            mir.insns[i].secondary_offset == call->secondary_offset &&
            mir.insns[i].immediate == 1 &&
            mir.insns[i].src1 == mask->dst)
            ++matching_args;
    return matching_args == 1;
}

static int mir_is_profiled_slotless_two_block_win(
    long generated_size, long captured_size, int generated_instructions,
    int captured_instructions)
{
    /* The original ten-byte margin admitted global_escape_store.  The
     * post-T178 forced campaign found 00040b.main as the only additional
     * slotless, at-most-two-block candidate within twenty bytes; it also
     * improves both static measures and passes both runtime modes.
     * Slotlessness keeps the selector from hiding unmodelled frame traffic
     * behind the assembly-text delta. */
    return !mir.has_vla && mir_cfg_block_count() <= 2 &&
           mir.backend_slot_count == 0 &&
           generated_size <= captured_size + 20 &&
           generated_instructions < captured_instructions;
}

static int mir_is_profiled_two_block_format_near_cost(
    long generated_size, long captured_size, int generated_instructions,
    int captured_instructions)
{
    /* check_float and must_seek are the complete measured two-block
     * format-call population within this nine-byte/two-instruction boundary.
     * Both MIR forms improve both runtime modes despite the text proxy's
     * small deficit. */
    return !mir.has_vla && mir_cfg_block_count() == 2 &&
           mir_has_wide_values() && mir_has_printf_family_call() &&
           generated_size <= captured_size + 9 &&
           generated_instructions <= captured_instructions + 2;
}

static int mir_is_profiled_slotless_format_cfg(
    long generated_size, long captured_size, int generated_instructions,
    int captured_instructions)
{
    /* The measured four-block diagnostic helper is cycle/size neutral after
     * assembly when MIR needs no frame slots and adds no instructions. */
    return !mir.has_vla && mir_cfg_block_count() <= 4 &&
           mir.backend_slot_count == 0 && mir_has_printf_family_call() &&
           generated_size <= captured_size + 9 &&
           generated_instructions <= captured_instructions;
}

static int mir_is_profiled_multiblock_text_proxy_win(
    long generated_size, long captured_size, int generated_instructions,
    int captured_instructions)
{
    if (mir.has_vla || generated_size <= captured_size)
        return 0;
    /*
     * Forced full-mode profiling covers the complete current populations
     * inside these block/size/instruction boundaries. The two-block lookup
     * wrappers and four-block constant parser improve both peep and nopeep
     * execution. The next four-block candidate is 62 text bytes over legacy
     * and regresses both modes and linked size.
     */
    if (mir_cfg_block_count() == 2)
        return mir.backend_slot_count == 1 && mir_call_count() == 3 &&
               generated_size <= captured_size + 44 &&
               generated_instructions == captured_instructions;
    if (mir_cfg_block_count() == 4)
        return generated_size <= captured_size + 53 &&
               generated_instructions <= captured_instructions + 2;
    return 0;
}

static int mir_is_profiled_small_unary_not_near_cost(
    long generated_size, long captured_size, int generated_instructions,
    int captured_instructions)
{
    /* The complete sub-25-byte unary-not near-cost population contains two
     * seven-to-nine-block functions. Both improve peep and nopeep execution
     * despite adding at most two raw instructions. The next candidate is
     * 32 bytes larger and regresses peep execution. */
    return !mir.has_vla && mir_cfg_block_count() <= 10 &&
           ((generated_size >= captured_size &&
             generated_size <= captured_size + 25 &&
             generated_instructions <= captured_instructions + 2) ||
            (mir_boolean_phi_branch_simplification_count() > 0 &&
             generated_size <= captured_size &&
             generated_instructions * 10L <=
                 captured_instructions * 9L));
}

static int mir_is_profiled_unary_not_rollout(
    long generated_size, long captured_size,
    int generated_instructions, int captured_instructions)
{
    int calls = mir_call_count();
    int blocks = mir_cfg_block_count();
    int return_kind = mir.return_type & 15;

    if (return_kind == TYPE_VOID && blocks == 2 &&
        mir.local_bytes == 0 && mir.backend_slot_count == 1 &&
        calls >= 7 && calls <= 11)
        return 1;
    return return_kind == TYPE_INT && blocks == 5 &&
           mir.local_bytes == 6 && mir.backend_slot_count == 5 &&
           calls == 2 && generated_size <= captured_size + 272 &&
           generated_instructions <= captured_instructions + 18;
}

static int mir_is_profiled_rhs_stack_rollout(
    long generated_size, long captured_size,
    int generated_instructions, int captured_instructions)
{
    return mir_cfg_block_count() == 1 &&
           generated_size <= captured_size - 37 &&
           generated_instructions <= captured_instructions - 4;
}

static int mir_is_profiled_branch_condition_rollout(
    long generated_size, long captured_size,
    int generated_instructions, int captured_instructions)
{
    return mir_cfg_block_count() <= 8 &&
           generated_size <= captured_size - 100 &&
           generated_instructions <= captured_instructions - 20;
}

static int mir_is_profiled_boolean_phi_branch_retry(
    long generated_size, long captured_size,
    int generated_instructions, int captured_instructions)
{
    int calls = mir_call_count();
    int blocks = mir_cfg_block_count();
    int return_kind = mir.return_type & 15;

    if (mir_boolean_phi_branch_simplification_count() <= 0 ||
        generated_size > captured_size + 40 ||
        generated_instructions > captured_instructions)
        return 0;
    if (mir_spilled_cfg_depends_on_binary_load_pair_forwarding() &&
        mir.backend_slot_count == 0)
        return 1;
    /* The fallback-only sweep exposed these reusable populations without a
     * regression in either peep mode. Keep the retry narrow until general
     * CFG instruction selection can price the remaining candidates. */
    if (mir.backend_slot_count == 0 || calls >= 18)
        return 1;
    if (return_kind == TYPE_INT && blocks <= 8 && calls >= 9)
        return 1;
    return return_kind == TYPE_VOID && blocks <= 9 && calls == 2 &&
           mir.local_bytes == 4 && mir.backend_slot_count == 2;
}

static int mir_is_profiled_boolean_phi_measured_cohort(
    long generated_size, long captured_size,
    int generated_instructions, int captured_instructions)
{
    int calls = mir_call_count();
    int blocks = mir_cfg_block_count();
    int return_kind = mir.return_type & 15;

    if (mir_boolean_phi_branch_simplification_count() <= 0 ||
        generated_size > captured_size + 40 ||
        generated_instructions > captured_instructions)
        return 0;
    /*
     * The train/holdout profile separates these allocation shapes from the
     * peep regressions in the surrounding population. Slotless comparison
     * arms, the seven-call three-slot shape, and multi-local spill sets stay
     * excluded even when their assembly-text proxies look better.
     */
    if (return_kind == TYPE_INT && blocks <= 40) {
        if (mir.backend_slot_count == 1 && calls == 0)
            return 1;
        if (mir.backend_slot_count == 2 && calls <= 2 &&
            mir.local_bytes == 0)
            return 1;
        if (mir.backend_slot_count == 3 &&
            (calls <= 5 || (calls >= 10 && blocks <= 20)))
            return 1;
        if (mir.backend_slot_count == 0 && calls >= 20)
            return 1;
    }
    return return_kind == TYPE_VOID && blocks <= 12 && calls == 1 &&
           mir.local_bytes == 2 && mir.backend_slot_count == 2;
}

static int mir_is_profiled_rematerialized_home_measured_cohort(
    long generated_size, long captured_size,
    int generated_instructions, int captured_instructions)
{
    (void)generated_instructions;
    (void)captured_instructions;
    /*
     * Train/holdout across the entire current calls==0 rematerialized-
     * home population (4/4 functions: tlongopt.ret_deref_live_add,
     * tlongopt.ret_member_live_shr, tpostptr.pre_bump_i32,
     * tpostptr.pre_drop_i32 -- single-block pointer/struct-member compound
     * assignment forms) each showed a real forced-accept full-mode A/B win
     * in both peep and nopeep cycle counts despite 25-33 more raw generated
     * instructions than the legacy form; static instruction count is not
     * predictive for this shape (assembly-text size is not proof of speed,
     * see SKILL.md rule #4). The two calls>=2 candidates sharing the same
     * raw-delta bucket (too.test_dispatch_table, tap.rand_ui32) both
     * regress in the same A/B check; mir_call_count()==0 is the exact
     * boundary observed, not a threshold nudge on the raw delta itself.
     * The byte ceiling (+300, observed max delta 273 bytes across the four
     * measured winners) is a defensive bound against extrapolating this
     * exception to an unmeasured, much larger call-free function.
     */
    return mir_call_count() == 0 && generated_size <= captured_size + 300;
}

static int mir_profile_matches_function(const char *variable)
{
    const char *profile = getenv(variable);
    const char *name;
    size_t function_length;

    if (profile == NULL)
        return 0;
    if (!strcmp(profile, "*"))
        return 1;
    function_length = strlen(mir.name);
    name = profile;
    while (*name != 0) {
        const char *end = strchr(name, ',');
        size_t length = end != NULL
            ? (size_t)(end - name) : strlen(name);
        if (length == function_length &&
            !strncmp(name, mir.name, length))
            return 1;
        if (end == NULL)
            break;
        name = end + 1;
    }
    return 0;
}

/* Diagnostic only, mirrors DCC_MIR_FORCE_ACCEPT_FUNCTION's comma-separated
 * matching but against the candidate's final fallback_reason string
 * instead of its function name. Lets a single measurement sweep force-
 * accept every candidate whose *only* remaining objection is one of a
 * given set of cost-gate reasons (e.g. "text-size,boolean-phi-cost"),
 * turning what used to require N individual per-function forced-accept
 * investigations into one flag. Never a production default - exactly
 * like its sibling, this exists to measure a bucket via
 * scripts/mir-bulk-accept-scan.py and runall.ps1, then get replaced by a
 * real structural gate change once a bucket is proven safe (2026-08-08
 * coverage-first pivot, Step 1). */
static int mir_force_accept_reasons_matches(const char *reason)
{
    const char *list = getenv("DCC_MIR_FORCE_ACCEPT_REASONS");
    const char *name;
    size_t reason_length;

    if (list == NULL || reason == NULL)
        return 0;
    if (!strcmp(list, "*"))
        return 1;
    reason_length = strlen(reason);
    name = list;
    while (*name != 0) {
        const char *end = strchr(name, ',');
        size_t length = end != NULL
            ? (size_t)(end - name) : strlen(name);
        if (length == reason_length &&
            !strncmp(name, reason, length))
            return 1;
        if (end == NULL)
            break;
        name = end + 1;
    }
    return 0;
}

/* 2026-08-08 mega-experiment bisection (Step 2/3): every cost-only reason
 * was force-accepted in isolation against the full extended correctness
 * gate. 16 of 25 hid genuine correctness bugs for specific untested shapes
 * (wrong output or infinite loops, not just slower code) - the "cost-only"
 * classification in scripts/mir-bulk-accept-scan.py's COST_ONLY_REASONS
 * table was therefore wrong for most of that list; those reasons still
 * bundle real semantic risk and need per-shape investigation like the
 * existing profiled predicates above, not bulk relaxation.
 *
 * These nine, by contrast, came back 100% clean individually *and*
 * combined (no cascading interaction with each other): admit them
 * permanently. absolute-address-cost, constant-conversion-frame-cost,
 * rhs-stack-cost, branch-condition-cost, indirect-store-stack-cost,
 * lazy-parameter-cost, dynamic-index-cost, rematerialized-home-cost,
 * stable-pointer-local-cost. +108/+110 functions (ordinary/stack-check). */
static int mir_reason_is_proven_cost_only(const char *reason)
{
    static const char *const proven[] = {
        "absolute-address-cost",
        "constant-conversion-frame-cost",
        "rhs-stack-cost",
        "branch-condition-cost",
        "indirect-store-stack-cost",
        "lazy-parameter-cost",
        "dynamic-index-cost",
        "rematerialized-home-cost",
        "stable-pointer-local-cost",
    };
    size_t i;

    if (reason == NULL)
        return 0;
    for (i = 0; i < sizeof(proven) / sizeof(proven[0]); i++)
        if (!strcmp(reason, proven[i]))
            return 1;
    return 0;
}

static int mir_boolean_phi_profile_is_semantically_eligible(void)
{
    return mir_cfg_block_count() <= 64 &&
           !mir_has_inline_substitution_call() &&
           !mir_has_declared_pointer_array() &&
           !mir_has_cfg_backedge();
}

static int mir_has_label_only_phi_fallthrough(void)
{
    int instruction;

    for (instruction = 0; instruction + 1 < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_LABEL &&
            mir.insns[instruction + 1].opcode == MIR_LABEL &&
            mir_instruction_has_phi_fallthrough(instruction, 1))
            return 1;
    return 0;
}

static int mir_boolean_phi_coverage_is_semantically_eligible(
    long generated_size, long captured_size)
{
    return mir_boolean_phi_profile_is_semantically_eligible() &&
           !mir_has_label_only_phi_fallthrough() &&
           generated_size <= captured_size + 2048;
}

static int mir_boolean_phi_small_loop_is_semantically_eligible(
    long generated_size, long captured_size)
{
    return mir_has_cfg_backedge() &&
           mir_cfg_block_count() <= 20 &&
           mir_call_count() <= 32 &&
           !mir.has_vla &&
           !mir_has_inline_substitution_call() &&
           !mir_has_declared_pointer_array() &&
           !mir_has_label_only_phi_fallthrough() &&
           generated_size <= 2500 &&
           generated_size <= captured_size + 2048;
}

static int mir_boolean_phi_medium_scalar_loop_is_semantically_eligible(
    long generated_size, long captured_size)
{
    int blocks = mir_cfg_block_count();
    return mir_has_cfg_backedge() &&
           blocks >= 21 && blocks <= 35 &&
           mir_call_count() <= 32 &&
           !mir.has_vla &&
           !mir_has_inline_substitution_call() &&
           !mir_has_declared_pointer_array() &&
           !mir_has_label_only_phi_fallthrough() &&
           generated_size <= 5000 &&
           generated_size <= captured_size + 2048;
}

static int mir_unary_not_call_free_loop_is_semantically_eligible(
    long generated_size, long captured_size)
{
    return mir_has_cfg_backedge() &&
           mir_cfg_block_count() <= 64 &&
           mir_call_count() == 0 &&
           !mir.has_vla &&
           !mir_has_wide_values() &&
           !mir_has_inline_substitution_call() &&
           !mir_has_declared_pointer_array() &&
           !mir_has_label_only_phi_fallthrough() &&
           generated_size <= 5000 &&
           generated_size <= captured_size + 2048;
}

static int mir_text_size_coverage_is_semantically_eligible(
    long generated_size, long captured_size)
{
    return mir_cfg_block_count() <= 64 &&
           mir_call_count() <= 32 &&
           !mir.has_vla &&
           !mir_has_wide_values() &&
           !mir_has_cfg_backedge() &&
           !mir_has_inline_substitution_call() &&
           !mir_has_declared_pointer_array() &&
           !mir_has_label_only_phi_fallthrough() &&
           generated_size <= captured_size + 2048;
}

static int mir_text_size_wide_coverage_is_semantically_eligible(
    long generated_size, long captured_size)
{
    return mir_cfg_block_count() <= 64 &&
           mir_call_count() <= 32 &&
           !mir.has_vla &&
           mir_has_wide_values() &&
           !mir_has_cfg_backedge() &&
           !mir_has_inline_substitution_call() &&
           !mir_has_declared_pointer_array() &&
           !mir_has_label_only_phi_fallthrough() &&
           generated_size <= captured_size + 2048;
}

static int mir_text_size_simple_backedge_is_semantically_eligible(
    long generated_size, long captured_size)
{
    return mir_cfg_block_count() <= 6 &&
           mir_call_count() <= 32 &&
           !mir.has_vla &&
           !mir_has_wide_values() &&
           mir_has_single_reducible_backedge_without_loop_calls() &&
           !mir_has_inline_substitution_call() &&
           !mir_has_declared_pointer_array() &&
           !mir_has_label_only_phi_fallthrough() &&
           generated_size <= captured_size + 2048;
}

static int mir_text_size_post_phi_is_semantically_eligible(
    long generated_size)
{
    return generated_size <= 10000 || mir_call_count() >= 80;
}

static int mir_bounded_acyclic_coverage_is_semantically_eligible(
    long generated_size, long captured_size)
{
    return mir_cfg_block_count() <= 64 &&
           mir_call_count() <= 32 &&
           !mir.has_vla &&
           !mir_has_cfg_backedge() &&
           !mir_has_inline_substitution_call() &&
           !mir_has_declared_pointer_array() &&
           !mir_has_label_only_phi_fallthrough() &&
           generated_size <= 5000 &&
           generated_size <= captured_size + 2048;
}

static int mir_dynamic_index_base_loop_is_semantically_eligible(
    long generated_size, long captured_size)
{
    return mir_has_cfg_backedge() &&
           mir_cfg_block_count() <= 64 &&
           mir_call_count() <= 32 &&
           !mir.has_vla &&
           !mir_has_wide_values() &&
           !mir_has_inline_substitution_call() &&
           !mir_has_declared_pointer_array() &&
           !mir_has_label_only_phi_fallthrough() &&
           generated_size <= 5000 &&
           generated_size <= captured_size + 2048;
}

static int mir_dynamic_index_base_wide_is_semantically_eligible(
    long generated_size)
{
    return mir_cfg_block_count() <= 64 &&
           mir_call_count() <= 32 &&
           !mir.has_vla &&
           mir_has_wide_values() &&
           !mir_has_inline_substitution_call() &&
           !mir_has_declared_pointer_array() &&
           !mir_has_label_only_phi_fallthrough() &&
           generated_size <= 10000;
}

static int mir_dynamic_index_base_vla_is_semantically_eligible(
    long generated_size)
{
    return mir.has_vla &&
           mir_cfg_block_count() <= 16 &&
           mir_call_count() <= 5 &&
           !mir_has_inline_substitution_call() &&
           !mir_has_declared_pointer_array() &&
           !mir_has_label_only_phi_fallthrough() &&
           generated_size <= 5000;
}

static int mir_reason_uses_bounded_acyclic_coverage(const char *reason)
{
    static const char *const proven[] = {
        "absolute-index-cost",
        "constant-conversion-home-cost",
        "dead-store-forwarding-cost",
        "dead-local-suffix-cost",
        "dynamic-index-base-cost",
        "indirect-store-address-cost",
        "planned-index-base-cost",
        "planned-stack-cost",
        "unary-not-cost",
        "wide-constant-cost",
    };
    size_t index;

    if (reason == NULL)
        return 0;
    for (index = 0; index < sizeof(proven) / sizeof(proven[0]); ++index)
        if (!strcmp(reason, proven[index]))
            return 1;
    return 0;
}

static int mir_wide_store_coverage_is_semantically_eligible(
    long generated_size, long captured_size)
{
    return mir_call_count() > 0 &&
           mir_bounded_acyclic_coverage_is_semantically_eligible(
               generated_size, captured_size);
}

static int mir_wide_store_large_acyclic_is_semantically_eligible(
    long generated_size)
{
    return !mir_has_cfg_backedge() &&
           mir_call_count() > 0 &&
           !mir.has_vla &&
           !mir_has_inline_substitution_call() &&
           !mir_has_declared_pointer_array() &&
           !mir_has_label_only_phi_fallthrough() &&
           generated_size <= 10000;
}

static int mir_binary_load_pair_coverage_is_semantically_eligible(
    long generated_size, long captured_size)
{
    (void)generated_size;
    (void)captured_size;
    return mir_call_count() != 1;
}

static int mir_inline_substitution_coverage_is_semantically_eligible(
    long generated_size, long captured_size)
{
    return mir_cfg_block_count() <= 64 &&
           mir_call_count() <= 32 &&
           mir.local_bytes >= 32 &&
           !mir.has_vla &&
           !mir_has_cfg_backedge() &&
           !mir_has_declared_pointer_array() &&
           !mir_has_label_only_phi_fallthrough() &&
           generated_size <= 5000 &&
           generated_size <= captured_size + 2048;
}

static int mir_block_cse_post_phi_is_semantically_eligible(
    long generated_size)
{
    return generated_size <= 10000 &&
           !(mir_has_wide_values() && mir_call_count() == 20);
}

static int mir_has_unconsumed_inline_temp_overwrite(void)
{
    int pending[MAX_PROTO_PARAMS] = {0};
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        int index;
        if (strncmp(insn->name, "#itmp", 5) != 0 ||
            insn->name[5] < '0' || insn->name[5] > '9' ||
            insn->name[6] != 0)
            continue;
        index = insn->name[5] - '0';
        if (index >= MAX_PROTO_PARAMS)
            continue;
        if (insn->opcode == MIR_STORE) {
            if (pending[index])
                return 1;
            pending[index] = 1;
        } else if (insn->opcode == MIR_LOAD) {
            pending[index] = 0;
        }
    }
    return 0;
}

static int mir_is_profiled_vla_single_block_instruction_win(
    long generated_size, long captured_size, int generated_instructions,
    int captured_instructions)
{
    /* vla_sizeof_saved_once is the only profiled one-block VLA candidate
     * with this eight-instruction win and twenty-byte ceiling.  Full-mode
     * validation confirms that the real frame-size adjustment remains a
     * peep/nopeep size and cycle improvement. */
    return mir.has_vla && mir_cfg_block_count() == 1 &&
           generated_size <= captured_size + 20 &&
           generated_instructions <= captured_instructions - 8;
}

static int mir_is_profiled_constant_absolute_no_worse(
    long generated_size, long captured_size, int generated_instructions,
    int captured_instructions)
{
    /*
     * Forced full-mode A/B covered every constant-absolute candidate that
     * is already no worse by both static measures. Besides the existing
     * one-block wins (a1.m_hook, cint.init_compile_storage), the current
     * two-block no-worse population splits cleanly on the new centralized
     * resolver's exact isolated-global-field view: cint.emit,
     * cobint.add_stmt, and cobint.add_var still reload the same pointer-
     * valued named base multiple times (their unresolved base reuse is
     * what regressed peep mode), while cint.add_string, cobint.add_string,
     * cobint.tget, and the already-accepted slotless emit_tok shape do not.
     * Admit the no-worse two-block subset only when there are no repeated
     * named-pointer reloads left for Campaign 2's base-retention
     * work to solve.
     */
    return !mir.has_vla &&
           generated_size <= captured_size &&
           generated_instructions <= captured_instructions &&
           (mir_cfg_block_count() != 2 ||
            mir_repeated_named_pointer_load_count() == 0);
}

static int mir_is_profiled_dead_suffix_instruction_win(
    long generated_size, long captured_size, int generated_instructions,
    int captured_instructions)
{
    return mir.dead_local_suffix_bytes > 0 && !mir.has_vla &&
           mir_cfg_block_count() <= 2 &&
           generated_size <= captured_size + 24 &&
           generated_instructions <= captured_instructions - 4;
}

static int mir_dead_suffix_layout_is_profitable(
    const char *selector_name, long generated_size, long captured_size,
    int generated_instructions, int captured_instructions)
{
    if (mir.dead_local_suffix_bytes == 0)
        return 1;
    if (!strcmp(selector_name, "homed-scalar-cfg"))
        return generated_instructions <= captured_instructions;
    if (!strcmp(selector_name, "spilled-scalar-cfg") &&
        mir.dead_local_suffix_bytes >= 8 &&
        generated_size > captured_size &&
        generated_instructions > captured_instructions - 2)
        return 0;
    return 1;
}

/* The duplicate trailing epilogue was removed before its temporary size
 * compensation could be retired safely. Forced full-app A/B now admits the
 * no-PHI, multi-block slice only when real emitted bytes and instructions
 * both beat legacy. The constant-absolute and eight-byte near margins are
 * the measured boundary that adds cint.find_sym, tchess.ch_bk_move,
 * tcodegen.tchk2, and tgoto.gt_switch while retaining compensation for the
 * slower PHI, one-block wide, and comparison-branch alternatives. */
static int mir_is_profiled_elided_epilogue_win(
    const char *selector_name, long generated_size, long captured_size,
    int generated_instructions, int captured_instructions)
{
    return !strcmp(selector_name, "spilled-scalar-cfg") &&
           !mir_has_phi_instruction() &&
           mir_cfg_block_count() > 2 &&
           generated_size <= captured_size &&
           generated_instructions <= captured_instructions &&
           (mir_spilled_cfg_depends_on_constant_absolute() ||
            generated_size +
                    mir_spilled_scalar_cfg_elided_epilogue_bytes >
                captured_size - 8);
}

static int mir_is_profiled_constant_bound_loop_pair(
    long generated_size, long captured_size, int generated_instructions,
    int captured_instructions)
{
    int i;
    int branch_count = 0;

    if (mir_cfg_block_count() != 7 || mir.allocation_spill_count != 0 ||
        generated_size > captured_size ||
        generated_instructions > captured_instructions + 11)
        return 0;
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->opcode == MIR_CALL || insn->opcode == MIR_CALL_AGGREGATE ||
            insn->opcode == MIR_OPAQUE)
            return 0;
        if (insn->opcode == MIR_BRANCH_FALSE) {
            const struct MirInsn *comparison = mir_definition(insn->src1);
            const struct MirInsn *bound;
            if (comparison == NULL || comparison->opcode != MIR_BINARY ||
                comparison->type != TYPE_INT || comparison->immediate != '<')
                return 0;
            bound = mir_definition(comparison->src2);
            if (bound == NULL || bound->opcode != MIR_CONST ||
                bound->type != TYPE_INT)
                return 0;
            ++branch_count;
        }
    }
    return branch_count == 2 && mir_has_cfg_backedge();
}

static int mir_has_profiled_positive_loop(void)
{
    int i;
    int has_positive_condition = 0;

    if (mir_cfg_block_count() != 4 || mir.allocation_spill_count != 0)
        return 0;
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->opcode == MIR_CALL || insn->opcode == MIR_CALL_AGGREGATE ||
            insn->opcode == MIR_OPAQUE)
            return 0;
        if (insn->opcode == MIR_BRANCH_FALSE) {
            const struct MirInsn *comparison = mir_definition(insn->src1);
            const struct MirInsn *zero;
            if (comparison == NULL || comparison->opcode != MIR_BINARY ||
                comparison->type != TYPE_INT || comparison->immediate != '>')
                continue;
            zero = mir_definition(comparison->src2);
            if (zero != NULL && zero->opcode == MIR_CONST &&
                zero->type == TYPE_INT && zero->immediate == 0)
                has_positive_condition = 1;
        }
    }
    return has_positive_condition && mir_has_cfg_backedge();
}

static int mir_is_profiled_allocator_backedge(
    const char *selector_name, long generated_size, long captured_size,
    int generated_instructions, int captured_instructions)
{
    int calls = mir_call_count();
    int blocks = mir_cfg_block_count();
    int homed = !strcmp(selector_name, "homed-scalar-cfg");
    int spilled = !strcmp(selector_name, "spilled-scalar-cfg");

    if (getenv("DCC_MIR_BACKEDGE_REPORT") != NULL)
        fprintf(stderr,
                "; MIR backedge-profile function=%s selector=%s vla=%d "
                "locals=%d aggregate=%d slots=%d spills=%d frameless=%d "
                "calls=%d blocks=%d generated-bytes=%ld captured-bytes=%ld "
                "generated-insns=%d captured-insns=%d\n",
                mir.name, selector_name, mir.has_vla, mir.local_bytes,
                mir.aggregate_temp_bytes, mir.backend_slot_count,
                mir.allocation_spill_count,
                homed && mir_homed_cfg_was_frameless(), calls, blocks,
                generated_size, captured_size, generated_instructions,
                captured_instructions);
    if (!mir_has_cfg_backedge() || mir.has_vla ||
        mir.aggregate_temp_bytes != 0 || mir.allocation_spill_count != 0 ||
        generated_size > captured_size ||
        generated_instructions >= captured_instructions)
        return 0;
    if (homed)
        /* Homed emission owns its frame decision. backend_slot_count is
         * spilled-selector state and may describe an earlier candidate.
         * Retain the source-local-free rule for leaf loops: an effectively
         * frameless promoted local can still displace profitable legacy BC
         * loop registerization. */
        return (mir.local_bytes == 0 && mir_homed_cfg_was_frameless()) ||
               calls > 0;
    if (!spilled)
        return 0;
    if (mir.backend_slot_count == 0 &&
        (mir.local_bytes == 0 ||
         (mir.local_bytes <= 2 && calls > 0)))
        return 1;
    return mir.local_bytes > 0 && mir.backend_slot_count <= 3 &&
           blocks <= 13 &&
           (calls > 0 ||
            generated_instructions <= captured_instructions - 9);
}

static int mir_is_profiled_text_proxy_instruction_win(
    long generated_size, long captured_size,
    int generated_instructions, int captured_instructions)
{
    if (generated_size > captured_size)
        return 0;
    if (mir.has_vla)
        return mir_cfg_block_count() == 1 &&
            generated_instructions * 20L <= captured_instructions * 17L;
    if (mir_has_multiple_conditional_tests())
        return generated_instructions * 40L <=
            captured_instructions * 37L;
    return generated_instructions * 100L <=
        captured_instructions * 93L;
}

static int mir_is_profiled_stable_pointer_local_win(
    long generated_size, long captured_size,
    int generated_instructions, int captured_instructions)
{
    return mir_spilled_cfg_depends_on_stable_pointer_local_slot() &&
        generated_size <= captured_size &&
        generated_instructions <= captured_instructions - 4;
}

static int mir_is_profiled_compact_homed_cfg(
    long generated_size, long captured_size,
    int generated_instructions, int captured_instructions)
{
    return generated_size * 10L <= captured_size * 9L &&
        generated_instructions <= captured_instructions + 4;
}

static int mir_try_emit_z80(FILE *out)
{
    const struct MirInsn *return_insn = NULL;
    const struct MirInsn *parameter;
    const struct MirInsn *definition;
    const struct MirInsn *left_parameter = NULL;
    const struct MirInsn *right_parameter = NULL;
    long constant;
    int two_parameters = 0;
    int two_parameter_operation = 0;
    int i;

    if (mir_try_selector(out, mir_try_emit_homed_scalar_cfg))
        return 1;
    if (mir_try_selector(out, mir_try_emit_spilled_scalar_cfg))
        return 1;

    /* The current selectors implement only the ordinary 16-bit HL result
     * convention. Other return ABIs remain with the existing backend. */
    if ((mir.return_type & 15) != TYPE_INT)
        return 0;

    if (mir_try_selector(out, mir_try_emit_accumulator_loop))
        return 1;
    if (mir_try_selector(out, mir_try_emit_unsigned_division_loop))
        return 1;
    if (mir_try_selector(out, mir_try_emit_repeated_invariant_add_loop))
        return 1;
    if (mir_try_selector(out, mir_try_emit_countdown_loop))
        return 1;
    if (mir_try_selector(out, mir_try_emit_comparison_branch))
        return 1;
    if (mir_try_selector(out, mir_try_emit_scalar_dag))
        return 1;

    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->opcode == MIR_RETURN)
            return_insn = insn;
        else if (insn->opcode != MIR_NOP && insn->opcode != MIR_PARAM &&
                 insn->opcode != MIR_CONST && insn->opcode != MIR_BINARY &&
                 insn->opcode != MIR_LABEL &&
                 !(insn->opcode == MIR_STORE && insn->object >= 0))
            return 0;
    }
    if (return_insn == NULL || return_insn->src1 < 0)
        return 0;
    definition = mir_definition(return_insn->src1);
    if (definition != NULL && definition->opcode == MIR_BINARY &&
        (definition->immediate == '+' || definition->immediate == '-')) {
        left_parameter = mir_definition(definition->src1);
        right_parameter = mir_definition(definition->src2);
        if (left_parameter != NULL && right_parameter != NULL &&
            left_parameter->opcode == MIR_PARAM &&
            right_parameter->opcode == MIR_PARAM) {
            two_parameters = 1;
            two_parameter_operation = (int)definition->immediate;
        }
    }
    if (!two_parameters &&
        !mir_affine_value(return_insn->src1, &parameter, &constant, 0))
        return 0;

    mir_emit_prologue(out);
    if (two_parameters) {
        if (!mir_emit_load_param(out, left_parameter) ||
            !mir_emit_load_param_de(out, right_parameter))
            return 0;
        if (two_parameter_operation == '+')
            fputs("\tadd hl,de\n", out);
        else
            fputs("\tor a\n\tsbc hl,de\n", out);
        constant = 0;
    } else if (parameter != NULL) {
        if (!mir_emit_load_param(out, parameter))
            return 0;
    } else {
        fprintf(out, "\tld hl,%ld\n", constant);
        constant = 0;
    }
    if (constant == 1)
        fputs("\tinc hl\n", out);
    else if (constant == -1)
        fputs("\tdec hl\n", out);
    else if (constant != 0)
        fprintf(out, "\tld de,%ld\n\tadd hl,de\n", constant);
    fputs("\tld sp,ix\n\tpop ix\n\tret\n", out);
    return 1;
}

void mir_end_function(void)
{
    int verified;

    if (!mir.active)
        return;
    if (errors > 0) {
        emit_sink_restore(&mir.saved_sink);
        fclose(mir.capture_stream);
        mir.capture_stream = NULL;
        mir.emit_mode = 0;
        mir.active = 0;
        return;
    }
    if (mir.count > MIR_MAX_ROLLOUT_INSNS) {
        FILE *destination = mir.saved_sink.stream;
        int character;

        emit_sink_restore(&mir.saved_sink);
        rewind(mir.capture_stream);
        while ((character = fgetc(mir.capture_stream)) != EOF)
            fputc(character, destination);
        /* Item (post-Plan-100): legacy retries a function's codegen from
         * scratch - and re-drives mir_begin_function/mir_end_function in
         * lockstep - for every discard-capable speculative attempt (no-IX-
         * frame, BC/E regalloc, IY regalloc, loop-scoped-BC-first; see
         * g_speculative_codegen_active's call sites in dcc_regalloc.c).
         * Each discarded attempt uses its own frame convention and register
         * allocation, so its generated/captured byte counts do not describe
         * the function's real, final, committed codegen at all - only the
         * attempt that is actually kept (or the plain fallback path) does.
         * Deliberately NOT gated on g_inline_body_buffering: the static-
         * inline/plain-static body-buffering branches in dcc_func.c set that
         * flag too, but only to defer real, kept output to a file for later
         * placement - that output is final and must still be reported.
         * Reporting from a discarded speculative attempt pollutes
         * DCC_MIR_SELECT_REPORT/the census with numbers from a codegen path
         * whose output never reaches the real .mac output, which was
         * observed directly: a single compile of `check` in tests/tesc.c
         * emitted five different "captured-bytes" values (424/311/370/370/
         * 311) for the one function, none reliably the real committed
         * size. */
        if (mir.report_mode && !g_speculative_codegen_active)
            fprintf(stderr, "; MIR emit function=%s result=oversized-fallback\n",
                    mir.name);
        fclose(mir.capture_stream);
        mir.capture_stream = NULL;
        mir.emit_mode = 0;
        mir.active = 0;
        return;
    }
    mir_thread_jumps();
    mir_resolve_deferred_metadata();
    mir_reset_boolean_phi_branch_simplification_count();
    verified = mir_verify_and_dump();
    if (verified) {
        mir_compute_dead_local_suffix();
        mir_report_dead_local_suffix();
    }
    if (mir.opaque_count != 0 &&
        getenv("DCC_MIR_REQUIRE_COMPLETE") != NULL) {
        fprintf(stderr, "MIR completeness failed for function %s\n", mir.name);
        fatal("incomplete MIR coverage");
    }
    if (!mir.emit_mode && verified &&
        getenv("DCC_MIR_CANDIDATES") != NULL) {
        FILE *candidate = tmpfile();
        int accepted;
        int label_id_save = label_id;
        if (candidate == NULL)
            fatal("cannot create MIR candidate stream");
        accepted = mir_try_emit_z80(candidate);
        fclose(candidate);
        /* Item T66b: this candidate's output is a diagnostic probe only -
         * it never reaches any real .mac output, so any labels it
         * allocated via new_label() must not shift subsequent legacy/MIR
         * label numbering for unrelated functions later in the same
         * translation unit. */
        label_id = label_id_save;
        if (accepted)
            fprintf(stderr, "; MIR candidate function=%s sink=%s\n",
                    mir.name, mir_sink_name(mir.sink_purpose));
    }
    if (!mir.emit_mode && verified &&
        getenv("DCC_MIR_GENERAL_CANDIDATES") != NULL) {
        FILE *candidate = tmpfile();
        int accepted;
        int label_id_save = label_id;
        if (candidate == NULL)
            fatal("cannot create MIR general candidate stream");
        accepted = mir_try_emit_general_rollout(candidate);
        fclose(candidate);
        /* Item T66b: same rationale as the candidate probe above. */
        label_id = label_id_save;
        if (accepted)
            fprintf(stderr, "; MIR general candidate function=%s sink=%s "
                            "slots=%d\n",
                    mir.name, mir_sink_name(mir.sink_purpose),
                    mir.backend_slot_count);
    }
    if (mir.emit_mode) {
        FILE *destination = mir.saved_sink.stream;
        FILE *generated = NULL;
        int emitted = 0;
        unsigned long selected_hash;
        const char *selector_name = "none";
        const char *fallback_reason = verified ? "selector" : "verify";
        long generated_size = -1;
        long captured_size = -1;
        int generated_instructions = -1;
        int captured_instructions = -1;
        int candidate_matrix_label_base = label_id;

        emit_sink_restore(&mir.saved_sink);
        if (verified) {
            const char *emit_filter = getenv("DCC_MIR_EMIT_FUNCTION");
            const char *general_filter = getenv("DCC_MIR_GENERAL_FUNCTION");
            /* Item T66b (mir-text-size-plan.md): every speculative candidate
             * trial below shares the SAME global new_label() counter also
             * used by legacy AST-backend codegen. Discarded candidates (a
             * losing size comparison, a rejected rescue attempt, or the
             * whole function eventually falling back to legacy) still
             * called new_label() while building their own throwaway
             * .mac text, silently burning label numbers that never reach
             * any real output. This let unrelated LATER functions in the
             * same translation unit receive different label numbers than
             * they would have without the wasted trial, causing tiny but
             * real code-placement-sensitive cycle/byte deltas (confirmed
             * twice: Item T56's CI failure, Item T65's fint.c register-
             * allocation shift). Fix: reset label_id to the same base
             * before every independent trial (so trials never compound
             * off each other's waste), track which candidate's ending
             * label_id corresponds to the content actually kept in
             * `generated`, and restore exactly that value (or the base,
             * on full fallback) once the final accept/reject decision is
             * known, right before any output is copied to `destination`. */
            int mir_label_base = label_id;
            int generated_label_id_after = mir_label_base;
            int lazy_retry_attempted = 0;
            int lazy_allocation_active = 0;
            int stable_local_retry_attempted = 0;
            int stable_local_homes_active = 0;
            int rhs_forward_retry_attempted = 0;
            int store_address_retry_attempted = 0;
            int wide_binary_retry_attempted = 0;
            int wide_binary_rhs_retry_attempted = 0;
            int stable_pointer_argument_retry_attempted = 0;
            int global_argument_retry_attempted = 0;
            int wide_argument_stack_retry_attempted = 0;
            int promoted_local_slot_retry_attempted = 0;
            int phi_slot_retry_attempted = 0;
            int strict_phi_retry_attempted = 0;
            int strict_phi_fallthrough_active = 0;
            int phi_return_forwarding_retry_attempted = 0;
            int block_cse_retry_attempted = 0;
            int block_cse_captured_spills = 0;
            int block_cse_captured_fixed_moves = 0;
            int block_cse_captured_operand_moves = 0;
            int block_cse_captured_phi_moves = 0;
            int boolean_phi_retry_attempted = 0;
            int measured_boolean_candidate = 0;
            int rematerialized_home_retry_attempted = 0;
            int rematerialized_home_allocation_active = 0;
            int address_rematerialization_retry_attempted = 0;
            int address_rematerialization_active = 0;
            int block_cse_address_rematerialization_active = 0;

retry_selection:
            if (block_cse_address_rematerialization_active) {
                mir_end_block_cse_address_rematerialization();
                block_cse_address_rematerialization_active = 0;
            }
            if (address_rematerialization_active) {
                mir_end_address_rematerialization();
                mir_end_all_spilled_fallback_optimizations();
                address_rematerialization_active = 0;
            }
            if (rematerialized_home_allocation_active) {
                mir_end_rematerialized_home_allocation();
                rematerialized_home_allocation_active = 0;
            }
            rematerialized_home_retry_attempted = 0;
            address_rematerialization_retry_attempted = 0;
            lazy_retry_attempted = 0;
            lazy_allocation_active = 0;
            stable_local_retry_attempted = 0;
            stable_local_homes_active = 0;
            rhs_forward_retry_attempted = 0;
            store_address_retry_attempted = 0;
            wide_binary_retry_attempted = 0;
            wide_binary_rhs_retry_attempted = 0;
            stable_pointer_argument_retry_attempted = 0;
            global_argument_retry_attempted = 0;
            wide_argument_stack_retry_attempted = 0;
            promoted_local_slot_retry_attempted = 0;
            phi_slot_retry_attempted = 0;
            strict_phi_retry_attempted = 0;
            strict_phi_fallthrough_active = 0;
            phi_return_forwarding_retry_attempted = 0;
            mir_end_all_spilled_fallback_optimizations();
            mir_end_strict_phi_fallthrough();
            if (block_cse_retry_attempted) {
                /* Address-only same-block CSE is profitable only when the
                 * retained value can stay rematerializable instead of being
                 * forced into a new call-crossing/register home. Re-enable
                 * the spilled selector's address planner on the retry so the
                 * shared MIR_ADDRESS root can keep a "no home needed" plan. */
                mir_begin_address_rematerialization();
                mir_begin_block_cse_address_rematerialization();
                address_rematerialization_active = 1;
                block_cse_address_rematerialization_active = 1;
            }
            generated = tmpfile();
            if (generated == NULL)
                fatal("cannot create MIR generated stream");
            if (emit_filter != NULL && emit_filter[0] != 0 &&
                strcmp(emit_filter, mir.name) == 0) {
                selector_name = "specialized";
                label_id = mir_label_base;
                emitted = mir_try_emit_z80(generated);
                generated_label_id_after = label_id;
            } else if (general_filter != NULL && general_filter[0] != 0 &&
                       (strcmp(general_filter, "*") == 0 ||
                        strcmp(general_filter, mir.name) == 0)) {
                selector_name = "homed-scalar-cfg";
                label_id = mir_label_base;
                emitted = mir_try_selector(generated,
                                           mir_try_emit_homed_scalar_cfg);
                generated_label_id_after = label_id;
                if (!emitted) {
                    selector_name = "spilled-scalar-cfg";
                    label_id = mir_label_base;
                    emitted = mir_try_selector(generated,
                                               mir_try_emit_spilled_scalar_cfg);
                    generated_label_id_after = label_id;
                }
            } else if (getenv("DCC_MIR_EMIT_GENERAL") != NULL) {
                selector_name = "general-rollout";
                label_id = mir_label_base;
                emitted = mir_try_selector(generated,
                                           mir_try_emit_general_rollout);
                generated_label_id_after = label_id;
            } else {
                /* Phase 8 Item 78/79: mir_try_emit_general_rollout (backed
                 * by mir_try_emit_homed_scalar_dag) was previously
                 * reachable only via the DCC_MIR_EMIT_GENERAL diagnostic
                 * above. A fresh census cross-check found it produces
                 * smaller AND measurably faster code than
                 * mir_try_emit_homed_scalar_cfg for its narrow structural
                 * subset (single-block, arithmetic-only opcodes, one
                 * return, >=1 parameter) in the overwhelming majority of
                 * cases - but not always (it lost for one function in the
                 * corpus), so both are tried here and the smaller
                 * structural candidate is kept, exactly like the existing
                 * homed-then-spilled comparison below. This can never
                 * regress any function homed-scalar-cfg alone would have
                 * produced, because homed-scalar-cfg's own result is only
                 * replaced when the alternative is strictly smaller. */
                FILE *general_candidate = tmpfile();
                int general_emitted = 0;
                int general_label_id_after;

                if (general_candidate == NULL)
                    fatal("cannot create MIR general-rollout candidate "
                          "stream");
                label_id = mir_label_base;
                general_emitted = mir_try_selector(general_candidate,
                                                    mir_try_emit_general_rollout);
                general_label_id_after = label_id;
                selector_name = "homed-scalar-cfg";
                label_id = mir_label_base;
                emitted = mir_try_selector(generated,
                                           mir_try_emit_homed_scalar_cfg);
                generated_label_id_after = label_id;
                if (general_emitted &&
                    (!emitted ||
                     mir_stream_size(general_candidate) <
                         mir_stream_size(generated))) {
                    fclose(generated);
                    generated = general_candidate;
                    general_candidate = NULL;
                    selector_name = "general-rollout";
                    emitted = 1;
                    generated_label_id_after = general_label_id_after;
                }
                if (general_candidate != NULL)
                    fclose(general_candidate);
            }
            if (emitted && !strcmp(selector_name, "homed-scalar-cfg") &&
                (mir_effective_local_bytes() != 0 ||
                 mir.allocation_spill_count != 0 ||
                 mir_homed_cfg_depends_on_word_store() ||
                 mir_homed_cfg_depends_on_constant_absolute() ||
                 mir_homed_cfg_depends_on_dynamic_index() ||
                 /* Repeated general comparisons are sensitive to boundary
                  * moves, so compare both complete selector outputs. */
                 (mir_general_comparison_count() > 1 &&
                  !mir_has_phi_instruction() &&
                  mir_cfg_block_count() <= 18) ||
                 mir_has_wide_values()) &&
                (general_filter == NULL || general_filter[0] == 0) &&
                (emit_filter == NULL || emit_filter[0] == 0)) {
                FILE *spilled_candidate = tmpfile();
                int spilled_emitted;
                int spilled_label_id_after;

                if (spilled_candidate == NULL)
                    fatal("cannot create MIR spilled candidate stream");
                label_id = mir_label_base;
                spilled_emitted = mir_try_selector(
                    spilled_candidate, mir_try_emit_spilled_scalar_cfg);
                spilled_label_id_after = label_id;
                if (spilled_emitted &&
                    /* The homed profitability gate rejects this call-heavy
                     * shape; retain an available spilled implementation
                     * rather than losing an already-profitable migration. */
                    ((mir_is_call_heavy_general_compare() &&
                      !(mir_homed_cfg_depends_on_constant_absolute() &&
                        mir_cfg_block_count() <= 4)) ||
                     mir_stream_size(spilled_candidate) <
                         mir_stream_size(generated) ||
                     (mir.allocation_spill_count != 0 &&
                      mir_stream_instruction_count(spilled_candidate) <
                          mir_stream_instruction_count(generated)))) {
                    fclose(generated);
                    generated = spilled_candidate;
                    spilled_candidate = NULL;
                    selector_name = "spilled-scalar-cfg";
                    generated_label_id_after = spilled_label_id_after;
                }
                if (spilled_candidate != NULL)
                    fclose(spilled_candidate);
            }
            if (!emitted && (general_filter == NULL ||
                             general_filter[0] == 0)) {
                selector_name = "spilled-scalar-cfg";
                label_id = mir_label_base;
                emitted = mir_try_selector(generated,
                                           mir_try_emit_spilled_scalar_cfg);
                generated_label_id_after = label_id;
            }
            if (emitted) {
evaluate_generated:
                generated_size = mir_stream_size(generated);
                captured_size = mir_stream_size(mir.capture_stream);
                generated_instructions =
                    mir_stream_instruction_count(generated);
                captured_instructions =
                    mir_stream_instruction_count(mir.capture_stream);
                if (!strcmp(selector_name, "spilled-scalar-cfg") &&
                    mir_spilled_scalar_cfg_elided_epilogue_bytes > 0 &&
                    generated_size >= 0 &&
                    !mir_is_profiled_elided_epilogue_win(
                        selector_name, generated_size, captured_size,
                        generated_instructions, captured_instructions))
                    generated_size +=
                        mir_spilled_scalar_cfg_elided_epilogue_bytes;
                fallback_reason = NULL;
                {
                    const char *forced_function =
                        getenv("DCC_MIR_FORCE_FALLBACK_FUNCTION");
                    if (getenv("DCC_MIR_FORCE_FALLBACK") != NULL ||
                        (forced_function != NULL &&
                         !strcmp(forced_function, mir.name)))
                        fallback_reason = "forced";
                }
                if (boolean_phi_retry_attempted &&
                    mir_boolean_phi_branch_simplification_count() > 0 &&
                    mir_boolean_phi_profile_is_semantically_eligible() &&
                    mir_is_profiled_boolean_phi_measured_cohort(
                        generated_size, captured_size,
                        generated_instructions, captured_instructions))
                    measured_boolean_candidate = 1;
                if (fallback_reason != NULL) {
                    /* Keep the selected reason. */
                }
                else if (generated_size < 0 || captured_size < 0 ||
                    generated_instructions < 0 || captured_instructions < 0)
                    fallback_reason = "measurement";
                else if (rematerialized_home_allocation_active &&
                         mir_is_profiled_rematerialized_home_measured_cohort(
                             generated_size, captured_size,
                             generated_instructions, captured_instructions))
                    /* Fully-measured calls==0 cohort (see the predicate's
                     * own comment): accept immediately and skip the rest of
                     * this chain, since several later checks (e.g. the
                     * dead-local-suffix-cost homed-selector rule, which
                     * independently requires generated_instructions <=
                     * captured_instructions) are tuned for unrelated
                     * populations and would otherwise reject this same
                     * already-proven-safe candidate for a different reason,
                     * triggering a worse alternate retry. */
                    { /* accepted; fallback_reason stays NULL */ }
                else if (rematerialized_home_allocation_active &&
                         generated_instructions >
                             captured_instructions - 8)
                    /* Excluding one-use constants from pair coloring exposes
                     * real homed wins, but the two candidates saving only
                     * two and seven raw instructions both regressed peep
                     * execution. Every measured eight-or-more-instruction
                     * candidate improved both shipping modes. The
                     * call-free measured cohort below is a separate,
                     * fully-tested exception to this raw-delta model. */
                    fallback_reason = "rematerialized-home-cost";
                else if (!strcmp(selector_name, "spilled-scalar-cfg") &&
                         mir_spilled_cfg_depends_on_wide_store_forwarding() &&
                         mir_cfg_block_count() != 1)
                    /* The four-block mm.main candidate is much smaller
                     * statically but regresses both measured modes; the
                     * single-block candidates improve both. */
                    fallback_reason = "wide-store-cost";
                else if (!strcmp(selector_name, "spilled-scalar-cfg") &&
                         mir_extended_integer_constant_conversion_folds() > 0 &&
                         mir.local_bytes == 0 &&
                         mir.aggregate_temp_bytes == 0 &&
                         mir.backend_slot_count > 0)
                    /* Folding integer constant conversions removes real raw
                     * instructions, but dccpeep already folds the equivalent
                     * legacy sign-extension sequences. If MIR alone creates
                     * a spill frame, that frame is therefore an unpriced
                     * runtime cost; the measured candidate regressed peep
                     * execution despite a large pre-peep instruction win. */
                    fallback_reason = "constant-conversion-frame-cost";
                else if (!strcmp(selector_name, "homed-scalar-cfg") &&
                         mir_extended_integer_constant_conversion_folds() > 0 &&
                         mir_cfg_block_count() > 1 &&
                         mir_home_uses_iy())
                    /* The same text proxy can prefer IY homes for folded
                     * constant high words. Repeated four-byte `ld iy,N`
                     * instructions plus the callee-save frame lost to
                     * dccpeep's existing constant-extension fold. */
                    fallback_reason = "constant-conversion-home-cost";
                else if (!strcmp(selector_name, "homed-scalar-cfg") &&
                         mir_homed_cfg_depends_on_unary_not_branch() &&
                         generated_instructions >
                             captured_instructions - 5)
                    /* Direct `!value` branches remove real materialization,
                     * but the measured three-instruction loop candidates
                     * still lose after dccpeep. Every candidate saving at
                     * least five instructions passes both runtime modes. */
                    fallback_reason = "unary-not-cost";
                else if (block_cse_retry_attempted &&
                         ((mir_common_block_expression_elimination_count() > 0 &&
                           (mir_cfg_block_count() != 1 ||
                            strcmp(selector_name, "homed-scalar-cfg") != 0 ||
                            generated_instructions >
                                captured_instructions - 5)) ||
                          (mir_global_field_value_numbering_count() > 0 &&
                           (generated_instructions >
                                captured_instructions - 3 ||
                            mir.allocation_spill_count >
                                block_cse_captured_spills ||
                            mir.allocation_fixed_moves >
                                block_cse_captured_fixed_moves ||
                            mir.allocation_operand_moves >
                                block_cse_captured_operand_moves ||
                            mir.allocation_phi_moves >
                                block_cse_captured_phi_moves))))
                    fallback_reason = "block-cse-cost";
                else if (boolean_phi_retry_attempted &&
                         mir_boolean_phi_branch_simplification_count() > 0 &&
                         !measured_boolean_candidate &&
                         !mir_is_profiled_boolean_phi_branch_retry(
                             generated_size, captured_size,
                             generated_instructions,
                             captured_instructions))
                    fallback_reason = "boolean-phi-cost";
                else if (!mir_dead_suffix_layout_is_profitable(
                             selector_name, generated_size, captured_size,
                             generated_instructions, captured_instructions))
                    /* Reclaiming frame bytes is semantically safe, but it can
                     * expose a slower selector or disturb profitable peep
                     * shapes. Require homed emission not to add instructions;
                     * for a large spilled-frame rewrite that is still
                     * text-larger, require at least two instructions of
                     * margin. */
                    fallback_reason = "dead-local-suffix-cost";
                else if (!strcmp(selector_name, "spilled-scalar-cfg") &&
                         mir_spilled_cfg_depends_on_dead_store_forwarding() &&
                         !mir_spilled_cfg_depends_on_promoted_local_slot_reuse() &&
                         mir.local_bytes + mir.aggregate_temp_bytes > 0 &&
                         mir.backend_slot_count > 0 &&
                         (generated_size > captured_size ||
                          generated_instructions >
                              captured_instructions - 4))
                    /* Dead-store forwarding can expose a smaller instruction
                     * stream while leaving both promoted-object frame bytes
                     * and a separate backend slot allocated. Until those
                     * overlapping homes are coalesced, require enough margin
                     * to pay that unmodelled prologue/frame cost. */
                    fallback_reason = "dead-store-forwarding-cost";
                else if (!strcmp(selector_name, "spilled-scalar-cfg") &&
                         mir_spilled_cfg_depends_on_wide_constant_rematerialization() &&
                         (mir_has_format_runtime_call() ||
                          generated_size >= captured_size) &&
                         !(mir_spilled_cfg_depends_only_on_unsigned_wide_constant_relational() &&
                           !mir_has_format_runtime_call() &&
                           generated_instructions <= captured_instructions))
                    /* T394 (mir-text-size-plan.md): an unsigned wide
                     * relational compare against a constant already calls
                     * the same runtime helper on both sides (legacy has no
                     * inline shortcut there either, unlike the signed case),
                     * so a generated stream that is not strictly smaller in
                     * raw bytes is still behavior- and cost-equivalent, not
                     * worse. Forced full-mode A/B of every such candidate
                     * (tlongopt.u_gtbig, tlongopt.u_lebig) passed both
                     * runtime modes with real cycle improvements and zero
                     * regressions. Require the instruction count to be no
                     * worse as the only remaining static guard, since the
                     * byte-size proxy is known unreliable for this exact
                     * call-vs-call shape. */
                    fallback_reason = "wide-constant-cost";
                else if (!strcmp(selector_name, "spilled-scalar-cfg") &&
                         mir_spilled_cfg_depends_on_unary_not_branch_fusion() &&
                         !mir_is_profiled_small_unary_not_near_cost(
                             generated_size, captured_size,
                             generated_instructions, captured_instructions) &&
                         !mir_is_profiled_unary_not_rollout(
                             generated_size, captured_size,
                             generated_instructions,
                             captured_instructions) &&
                         !mir_profile_matches_function(
                             "DCC_MIR_PROFILE_UNARY_NOT") &&
                         (mir_cfg_block_count() > 18 ||
                          generated_size + 10 > captured_size))
                    fallback_reason = "unary-not-cost";
                else if (!strcmp(selector_name, "spilled-scalar-cfg") &&
                         mir_spilled_cfg_depends_on_constant_index_absolute() &&
                         generated_instructions * 25L >
                             captured_instructions * 24L)
                    /* Constant-index absolute addressing can make the raw
                     * MIR stream text-smaller while still leaving too much
                     * generic scalar-CFG setup for dccpeep to recover.
                     * Forced full-mode A/B of the exact newly admitted set
                     * found two such near-cost cases: tc89init.main linked
                     * 128 bytes larger, and too.test_dispatch_table was 50
                     * peep cycles slower. Every non-regressing candidate
                     * reduced raw instructions by at least 4.6%; require a
                     * conservative 4% margin for this structurally distinct
                     * class instead of naming either function. */
                    fallback_reason = "absolute-index-cost";
                else if (!strcmp(selector_name, "spilled-scalar-cfg") &&
                         mir_spilled_cfg_depends_on_constant_absolute() &&
                         !mir_spilled_cfg_depends_on_constant_index_absolute() &&
                         !mir_is_profiled_slotless_two_block_win(
                             generated_size, captured_size,
                             generated_instructions, captured_instructions) &&
                         !mir_is_profiled_constant_absolute_no_worse(
                             generated_size, captured_size,
                             generated_instructions, captured_instructions) &&
                         (generated_size * 50L > captured_size * 47L ||
                          generated_instructions * 50L >
                              captured_instructions * 47L))
                    /* CI-equivalent ntvcm profiling found that the weak
                     * member-only absolute-address additions could improve
                     * static text while regressing one shipping mode. Require
                     * at least a 6% reduction in both the assembly-text proxy
                     * and instructions. This also keeps the same functions
                     * selected with and without stack checking. The separately
                     * measured constant-index and slotless two-block
                     * populations retain their own structural profitability
                     * rules above. */
                    fallback_reason = "absolute-address-cost";
                else if (!strcmp(selector_name, "spilled-scalar-cfg") &&
                         mir_spilled_cfg_depends_on_dynamic_index_base_forwarding() &&
                         mir_call_count() > 0 &&
                         generated_instructions >
                             captured_instructions - 15)
                    /* Dynamic index-base forwarding can remove a complete
                     * spill slot while still exposing a call-containing
                     * candidate whose legacy register/layout choices win
                     * after dccpeep. Full-mode A/B found that the affected
                     * functions with fewer than fifteen saved instructions
                     * regress peep cycles; cint.add_func reaches the measured
                     * non-regressing boundary exactly. */
                    fallback_reason = "dynamic-index-base-cost";
                else if (!strcmp(selector_name, "spilled-scalar-cfg") &&
                         mir_spilled_cfg_depends_on_indirect_store_address_forwarding() &&
                         mir_spilled_cfg_indirect_store_address_forwarding_uses()
                             < 2)
                    /* One address/value pair removes two real slots but did
                     * not amortize the selector displacement in the first
                     * measured list helpers: both regressed peep execution.
                     * The two-handoff candidate improves both modes. */
                    fallback_reason = "indirect-store-address-cost";
                else if (!strcmp(selector_name, "spilled-scalar-cfg") &&
                         mir_spilled_cfg_depends_on_planned_stack_handoff() &&
                         !(mir_spilled_cfg_depends_on_unary_not_branch_fusion() &&
                           mir_is_profiled_small_unary_not_near_cost(
                               generated_size, captured_size,
                               generated_instructions,
                               captured_instructions)) &&
                         mir_call_count() >= 8 &&
                         generated_instructions >
                             captured_instructions - 8)
                    /* Planned stack handoff can move a call-heavy function
                     * just under the text gate without a reliable runtime
                     * win. Exact-upstream full-mode A/B found a seven-
                     * instruction win still regressed peep cycles; require
                     * at least eight when eight or more calls amplify
                     * layout and optimizer sensitivity. */
                    fallback_reason = "planned-stack-cost";
                else if (!strcmp(selector_name, "spilled-scalar-cfg") &&
                         mir_spilled_cfg_depends_on_planned_index_base_handoff() &&
                         generated_instructions >
                             captured_instructions -
                                 (mir_call_count() > 0
                                      ? 7 * mir_call_count() : 2))
                    /* Nonadjacent index-base handoffs are semantically
                     * proven by the stack plan, but exact-upstream A/B
                     * rejects a one-instruction call-free margin and a
                     * twenty-instruction margin across three calls.
                     * Preserve the stronger measured wins by charging seven
                     * saved instructions per call, with a two-instruction
                     * floor for call-free functions. */
                    fallback_reason = "planned-index-base-cost";
                else if (!strcmp(selector_name, "homed-scalar-cfg") &&
                         mir_has_lazy_parameters() &&
                         ((mir_lazy_parameter_count() > 4 &&
                           generated_instructions >
                               captured_instructions - 5) ||
                          (mir_lazy_byte_parameter_count() > 0 &&
                           generated_instructions >
                               captured_instructions - 3) ||
                          (mir_has_phi_instruction() &&
                           mir_cfg_block_count() <= 5 &&
                           generated_instructions >
                               captured_instructions - 2)))
                    /* Lazy parameter binding removes artificial entry
                     * lifetimes, but exact full-mode A/B found three
                     * shallow static wins whose extra IX-relative loads
                     * still lose after dccpeep: a six-parameter arithmetic
                     * chain, a mixed byte-parameter expression, and a
                     * one-instruction phi-CFG win. Require measured margins
                     * for those structural classes; other lazy retries keep
                     * the ordinary homed profitability policy. */
                    fallback_reason = "lazy-parameter-cost";
                else if (!strcmp(selector_name, "spilled-scalar-cfg") &&
                         mir_spilled_cfg_depends_on_stable_pointer_local_home() &&
                         mir_cfg_block_count() == 1 &&
                         mir_call_count() >= 8)
                    /* Reloading a stable pointer local directly from its
                     * named frame slot removes backend slots, but a
                     * call-heavy straight-line candidate can still disrupt
                     * stronger dccpeep shapes. Exact full-mode A/B found the
                     * ten-call case regressed peep cycles despite saving ten
                     * raw instructions. Multi-block candidates with a
                     * four-instruction win were non-regressing. */
                    fallback_reason = "stable-pointer-local-cost";
                else if (mir_strict_phi_fallthrough_was_used() &&
                         generated_instructions >
                             captured_instructions - 10)
                    /* Suppressing a duplicate copy on a label-only pseudo
                     * edge is required for correctness, but the newly exposed
                     * shallow candidates can still regress after dccpeep.
                     * The complete measured population below a ten-
                     * instruction win regressed at least one runtime mode. */
                    fallback_reason = "phi-fallthrough-cost";
                else if (!strcmp(selector_name, "spilled-scalar-cfg") &&
                         mir_spilled_cfg_depends_on_binary_load_pair_forwarding() &&
                         generated_size > captured_size)
                    /* The load-pair handoff removes a real spill, but it
                     * cannot justify the generic slotless single-block size
                     * exception by itself. Require the candidate stream to
                     * remain text-profitable before considering its measured
                     * boolean-PHI cohort. */
                    fallback_reason = "binary-load-pair-cost";
                else if (!strcmp(selector_name, "spilled-scalar-cfg") &&
                         !(mir_spilled_cfg_depends_on_unary_not_branch_fusion() &&
                           mir_is_profiled_small_unary_not_near_cost(
                               generated_size, captured_size,
                               generated_instructions,
                               captured_instructions)) &&
                                                 ((generated_size > captured_size + 1 &&
                                                     !(mir.local_bytes == 0 &&
                                                         mir.aggregate_temp_bytes == 0 &&
                                                         mir.backend_slot_count == 0 && !mir.has_vla &&
                                                         mir_cfg_block_count() == 1 &&
                                                         generated_instructions <= captured_instructions)) ||
                                                     /* Item T61 follow-up: for VLA-bearing frames the
                                                      * generated/captured "size" fields are assembly-
                                                      * text byte lengths, not real assembled bytes (see
                                                      * mir_stream_size()'s documented proxy caveat). A
                                                      * dead-label-elision fix (Item T61) exposed a case
                                                      * where this proxy actively misleads for VLA frames:
                                                      * tvla.c's vla_sizeof_element/vla_sizeof_op_and/
                                                      * vla_sizeof_op_mulrhs/vla_sizeof_shadow_outer_after
                                                      * each showed generated_size 2-3 bytes UNDER
                                                      * captured_size (a normally-safe auto-accept
                                                      * margin), yet direct .PRN symbol-address
                                                      * measurement under -fstack-check showed each one
                                                      * actually costs 16-23 MORE real bytes than legacy
                                                      * once truly assembled - an 8x+ divergence from the
                                                      * text-size proxy. Require a real safety margin
                                                      * before trusting the proxy for any has_vla
                                                      * candidate, mirroring the codebase's existing
                                                      * practice of excluding has_vla from the "profiled"
                                                      * bypass predicates below. */
                                                     (mir.has_vla &&
                                                         captured_size - generated_size < 8) ||
                                                     /* Item T63 (mir-text-size-plan.md), the same class
                                                      * of proxy failure as the has_vla margin above but a
                                                      * different trigger: tests/tgoto.c's gt_switch(), a
                                                      * 3-way if/else-if chain testing one spilled `int`
                                                      * parameter twice, was exposed by Item T62's dead-
                                                      * jump elision (6 fewer text bytes) with only a
                                                      * 4-byte margin (419 vs 423). It passed both text-
                                                      * size and instruction-count gates, yet a focused
                                                      * runall -Mode full run showed a real cycle
                                                      * regression in both peep (+0.14%) and nopeep
                                                      * (+0.03%): each comparison in the chain reloads the
                                                      * same spilled parameter from its stack slot rather
                                                      * than keeping it live in a register across the
                                                      * whole chain, a redundant-reload tax the byte-count
                                                      * proxy does not see. Two or more MIR_BRANCH_FALSE
                                                      * instructions (mir_has_multiple_conditional_tests())
                                                      * is the structural signature of a chained if/else-if
                                                      * (as opposed to a single if/else, which measured
                                                      * regression-free); require the same real safety
                                                      * margin used for has_vla whenever this shape is
                                                      * present, rather than a name-based exception for
                                                      * gt_switch alone. */
                                                     (mir_has_multiple_conditional_tests() &&
                                                         captured_size - generated_size < 8)) &&
                                                 !mir_is_profiled_near_cost_single_block(
                                                     generated_size, captured_size,
                                                     generated_instructions,
                                                     captured_instructions) &&
                                                 !mir_is_byte_profitable_single_block(
                                                     generated_size, captured_size,
                                                     generated_instructions,
                                                     captured_instructions) &&
                                                 !mir_is_profiled_slotless_two_block_win(
                                                     generated_size, captured_size,
                                                     generated_instructions,
                                                     captured_instructions) &&
                                                 !mir_is_profiled_two_block_format_near_cost(
                                                     generated_size, captured_size,
                                                     generated_instructions,
                                                     captured_instructions) &&
                                                 !mir_is_profiled_slotless_format_cfg(
                                                     generated_size, captured_size,
                                                     generated_instructions,
                                                     captured_instructions) &&
                                                 !mir_is_profiled_multiblock_text_proxy_win(
                                                     generated_size, captured_size,
                                                     generated_instructions,
                                                     captured_instructions) &&
                                                 !mir_is_profiled_vla_single_block_instruction_win(
                                                     generated_size, captured_size,
                                                     generated_instructions,
                                                     captured_instructions) &&
                                                 !mir_is_profiled_dead_suffix_instruction_win(
                                                     generated_size, captured_size,
                                                     generated_instructions,
                                                     captured_instructions) &&
                                                 !mir_is_profiled_stable_pointer_local_win(
                                                     generated_size, captured_size,
                                                     generated_instructions,
                                                     captured_instructions) &&
                                                 !(!strcmp(selector_name,
                                                           "spilled-scalar-cfg") &&
                                                   mir_is_profiled_text_proxy_instruction_win(
                                                       generated_size,
                                                       captured_size,
                                                       generated_instructions,
                                                       captured_instructions)))
                    fallback_reason = "text-size";
                else if (generated_instructions > captured_instructions +
                        (!strcmp(selector_name, "homed-scalar-cfg")
                            ? (mir_cfg_block_count() <= 2 ? 2 : 1)
                        : (!strcmp(selector_name, "spilled-scalar-cfg") &&
                           generated_size <= captured_size ? 1 : 0)) &&
                         !(!strcmp(selector_name, "spilled-scalar-cfg") &&
                           mir_spilled_cfg_depends_on_unary_not_branch_fusion() &&
                           mir_is_profiled_small_unary_not_near_cost(
                               generated_size, captured_size,
                               generated_instructions,
                               captured_instructions)) &&
                         !mir_is_profiled_near_cost_single_block(
                             generated_size, captured_size,
                             generated_instructions, captured_instructions) &&
                         !mir_is_byte_profitable_single_block(
                             generated_size, captured_size,
                             generated_instructions, captured_instructions) &&
                         !mir_is_profiled_indirect_rmw_single_block(
                             generated_size, captured_size,
                             generated_instructions, captured_instructions) &&
                         !mir_is_profiled_two_block_format_near_cost(
                             generated_size, captured_size,
                             generated_instructions, captured_instructions) &&
                         !mir_is_profiled_slotless_format_cfg(
                             generated_size, captured_size,
                             generated_instructions, captured_instructions) &&
                         !mir_is_profiled_multiblock_text_proxy_win(
                             generated_size, captured_size,
                             generated_instructions, captured_instructions) &&
                         !mir_is_profiled_vla_single_block_instruction_win(
                             generated_size, captured_size,
                             generated_instructions, captured_instructions) &&
                         !mir_is_profiled_pointer_member_picker(
                             generated_size, captured_size,
                             generated_instructions, captured_instructions) &&
                         !mir_is_profiled_masked_memset_wrapper(
                             generated_size, captured_size,
                             generated_instructions, captured_instructions) &&
                         !mir_is_profiled_constant_bound_loop_pair(
                             generated_size, captured_size,
                             generated_instructions, captured_instructions) &&
                         !(!strcmp(selector_name, "homed-scalar-cfg") &&
                              mir_is_profiled_compact_homed_cfg(
                                  generated_size, captured_size,
                                  generated_instructions,
                                  captured_instructions)))
                    fallback_reason = "instruction-count";
                else if (!strcmp(selector_name, "spilled-scalar-cfg") &&
                         mir_spilled_cfg_depends_on_direct_byte_param() &&
                         mir_cfg_block_count() > 1 &&
                         generated_instructions >= captured_instructions)
                    /* Reloading an unmodified byte parameter directly from
                     * its incoming home is semantically safe, but exact-CI
                     * profiling found that it can expose a multi-block MIR
                     * candidate whose frame setup outweighs the saved slot.
                     * Require a real instruction win for that shape; simple
                     * single-block byte forwarding retains the ordinary cost
                     * policy. */
                    fallback_reason = "direct-byte-param-cost";
                else if (!strcmp(selector_name, "spilled-scalar-cfg") &&
                         mir_spilled_cfg_depends_on_rhs_stack_forwarding() &&
                         !mir_spilled_cfg_depends_on_binary_load_pair_forwarding() &&
                         !mir_is_profiled_rhs_stack_rollout(
                             generated_size, captured_size,
                             generated_instructions,
                             captured_instructions) &&
                         !mir_profile_matches_function(
                             "DCC_MIR_PROFILE_RHS_STACK") &&
                         !(mir_spilled_cfg_depends_on_unary_not_branch_fusion() &&
                           mir_is_profiled_small_unary_not_near_cost(
                               generated_size, captured_size,
                               generated_instructions,
                               captured_instructions)) &&
                         !mir_is_profiled_pointer_member_picker(
                             generated_size, captured_size,
                             generated_instructions, captured_instructions) &&
                         !mir_is_profiled_pointer_index_picker(
                             generated_size, captured_size,
                             generated_instructions, captured_instructions))
                    /* General adjacent-RHS forwarding removes a real slot
                     * round trip, but the first fallback-only rollout
                     * exposed seven candidates and four regressed peep
                     * execution or linked size. The two pointer-index
                     * pickers reuse the already-profiled offset shape
                     * and improve both modes; retain only that structural
                     * class until instruction selection improves further. */
                    fallback_reason = "rhs-stack-cost";
                else if (!strcmp(selector_name, "spilled-scalar-cfg") &&
                         mir_spilled_cfg_indirect_store_value_forwarding_uses()
                             > 1)
                    /* The first fallback-only rollout admitted one helper
                     * with two such handoffs, but both peep and nopeep
                     * execution regressed despite a static instruction
                     * reduction. Keep the independently profitable
                     * single-handoff shapes while instruction selection
                     * still cannot price interactions across a larger
                     * indirect-update body. */
                    fallback_reason = "indirect-store-stack-cost";
                else if (!strcmp(selector_name, "spilled-scalar-cfg") &&
                         mir_spilled_cfg_depends_on_branch_condition_forwarding() &&
                         !mir_is_profiled_branch_condition_rollout(
                             generated_size, captured_size,
                             generated_instructions,
                             captured_instructions) &&
                         !mir_profile_matches_function(
                             "DCC_MIR_PROFILE_BRANCH_CONDITION") &&
                         ((mir_cfg_block_count() > 2 &&
                           captured_instructions > 50) ||
                          captured_instructions > 100 ||
                          mir_spilled_cfg_branch_condition_forwarding_uses()
                              > 1))
                    /* Direct condition forwarding is consistently
                     * profitable for the measured two-block if/exit
                     * helpers. The first larger-CFG candidate regressed
                     * peep execution despite an eleven-instruction win. A
                     * second full-corpus gate exposed the same problem in
                     * a 490-instruction two-block body. Keep the measured
                     * one-handoff helpers at no more than 100 legacy
                     * instructions while this local saving cannot price
                     * either larger interaction. Full-mode A/B of the
                     * multi-block population found the block-count arm was
                     * too broad: a genuinely tiny seven-block helper
                     * (bint.compile_line, 27 legacy instructions, one
                     * forwarding use) is a clean win, while every other
                     * multi-block candidate measured (95-421 legacy
                     * instructions) regresses. Require more than fifty
                     * legacy instructions before the block-count arm
                     * rejects, since the nearest regressing candidate sits
                     * at 95. */
                    fallback_reason = "branch-condition-cost";
                else if (!strcmp(selector_name, "homed-scalar-cfg") &&
                         mir_homed_cfg_depends_on_dynamic_index() &&
                         generated_instructions >= captured_instructions)
                    /* Dynamic constant-stride addressing can be text-smaller
                     * without being faster after peephole optimization.
                     * Exact-CI A/B found the equal-instruction two-block
                     * pointer-null check regressed peep cycles, while the
                     * retained single-block transition-table candidate saves
                     * thirteen instructions. Require a real instruction win
                     * for this structurally distinct class. */
                    fallback_reason = "dynamic-index-cost";
                else if (!strcmp(selector_name, "homed-scalar-cfg") &&
                         mir_is_call_heavy_general_compare() &&
                         !(mir_homed_cfg_depends_on_constant_absolute() &&
                           mir_cfg_block_count() <= 4))
                    fallback_reason = "call-heavy-general-compare";
                else if (mir_cfg_block_count() > 64)
                    fallback_reason = "cfg-block-count";
                else if (mir_has_inline_substitution_call())
                    fallback_reason = "inline-substitution";
                else if (mir_has_declared_pointer_array())
                    fallback_reason = "pointer-array";
                else if (mir_has_cfg_backedge() &&
                         !mir_profile_matches_function(
                             "DCC_MIR_PROFILE_CFG_BACKEDGE") &&
                         !mir_has_profiled_positive_loop() &&
                         !(!strcmp(selector_name, "spilled-scalar-cfg") &&
                           mir_spilled_cfg_depends_on_unary_not_branch_fusion() &&
                           mir_is_profiled_small_unary_not_near_cost(
                               generated_size, captured_size,
                               generated_instructions,
                               captured_instructions)) &&
                         !mir_is_profiled_constant_bound_loop_pair(
                             generated_size, captured_size,
                             generated_instructions,
                             captured_instructions) &&
                         !mir_has_single_reducible_backedge_without_loop_calls())
                    fallback_reason = "cfg-backedge";
                if (fallback_reason != NULL &&
                    (!strcmp(fallback_reason, "instruction-count") ||
                     !strcmp(fallback_reason, "text-size")) &&
                    !lazy_retry_attempted &&
                    !g_speculative_codegen_active) {
                    FILE *lazy_candidate = tmpfile();
                    int lazy_emitted = 0;

                    lazy_retry_attempted = 1;
                    if (lazy_candidate == NULL)
                        fatal("cannot create MIR lazy-parameter candidate "
                              "stream");
                    if (mir_begin_lazy_parameter_allocation()) {
                        lazy_allocation_active = 1;
                        label_id = mir_label_base;
                        lazy_emitted = mir_try_selector(
                            lazy_candidate, mir_try_emit_homed_scalar_cfg);
                    }
                    if (lazy_emitted) {
                        fclose(generated);
                        generated = lazy_candidate;
                        lazy_candidate = NULL;
                        selector_name = "homed-scalar-cfg";
                        emitted = 1;
                        generated_label_id_after = label_id;
                        fallback_reason = NULL;
                        goto evaluate_generated;
                    }
                    if (lazy_allocation_active) {
                        mir_end_lazy_parameter_allocation();
                        lazy_allocation_active = 0;
                    }
                    fclose(lazy_candidate);
                }
                if (fallback_reason != NULL && lazy_allocation_active) {
                    mir_end_lazy_parameter_allocation();
                    lazy_allocation_active = 0;
                }
                if (fallback_reason != NULL &&
                    (!strcmp(fallback_reason, "instruction-count") ||
                     !strcmp(fallback_reason, "text-size") ||
                     !strcmp(fallback_reason, "unary-not-cost") ||
                     !strcmp(fallback_reason, "planned-stack-cost")) &&
                    !stable_local_retry_attempted &&
                    !g_speculative_codegen_active) {
                    FILE *local_candidate = tmpfile();
                    int local_emitted;

                    stable_local_retry_attempted = 1;
                    if (local_candidate == NULL)
                        fatal("cannot create MIR stable-local candidate "
                              "stream");
                    mir_begin_stable_pointer_local_homes();
                    stable_local_homes_active = 1;
                    label_id = mir_label_base;
                    local_emitted = mir_try_selector(
                        local_candidate, mir_try_emit_spilled_scalar_cfg);
                    if (local_emitted) {
                        fclose(generated);
                        generated = local_candidate;
                        local_candidate = NULL;
                        selector_name = "spilled-scalar-cfg";
                        emitted = 1;
                        generated_label_id_after = label_id;
                        fallback_reason = NULL;
                        goto evaluate_generated;
                    }
                    mir_end_stable_pointer_local_homes();
                    stable_local_homes_active = 0;
                    fclose(local_candidate);
                }
                if (fallback_reason != NULL && stable_local_homes_active) {
                    mir_end_stable_pointer_local_homes();
                    stable_local_homes_active = 0;
                }
                /* Phase 5 Item 46: homed/spilled-scalar-cfg already passed
                 * every other cost gate above - the *only* reason this
                 * candidate is about to fall back is the generic backedge
                 * veto. The specialized loop selectors below exist
                 * precisely to hand-verify specific backedge shapes as
                 * safe; since conceding to legacy output is the only other
                 * option at this point, retry them here before giving up.
                 * This can never affect any function homed/spilled-scalar-
                 * cfg would otherwise accept outright, because
                 * fallback_reason only ever reaches exactly
                 * "cfg-backedge" once every earlier gate has already
                 * passed for the original candidate. */
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "cfg-backedge") &&
                    (mir.return_type & 15) == TYPE_INT) {
                    FILE *loop_candidate = tmpfile();
                    if (loop_candidate == NULL)
                        fatal("cannot create MIR loop-selector candidate "
                              "stream");
                    /* Item T66b: reset label_id before each independent
                     * sub-attempt in this OR chain so a rejected earlier
                     * attempt's wasted labels never compound into the
                     * next one's numbering; only the one that ultimately
                     * wins (see generated_label_id_after below) is kept. */
                    if ((label_id = mir_label_base,
                         mir_try_selector(loop_candidate,
                                         mir_try_emit_accumulator_loop)) ||
                        (label_id = mir_label_base,
                         mir_try_selector(loop_candidate,
                                         mir_try_emit_unsigned_division_loop)) ||
                        (label_id = mir_label_base,
                         mir_try_selector(
                            loop_candidate,
                            mir_try_emit_repeated_invariant_add_loop)) ||
                        (label_id = mir_label_base,
                         mir_try_selector(loop_candidate,
                                         mir_try_emit_countdown_loop))) {
                        long loop_size = mir_stream_size(loop_candidate);
                        int loop_instructions =
                            mir_stream_instruction_count(loop_candidate);
                        if (loop_size >= 0 && loop_instructions >= 0 &&
                            !(loop_size > captured_size + 1 &&
                              !mir_is_profiled_near_cost_single_block(
                                  loop_size, captured_size,
                                  loop_instructions, captured_instructions) &&
                              !mir_is_byte_profitable_single_block(
                                  loop_size, captured_size,
                                  loop_instructions,
                                  captured_instructions))) {
                            fclose(generated);
                            generated = loop_candidate;
                            loop_candidate = NULL;
                            selector_name = "loop-family";
                            generated_size = loop_size;
                            generated_instructions = loop_instructions;
                            generated_label_id_after = label_id;
                            fallback_reason = NULL;
                        }
                    }
                    if (loop_candidate != NULL)
                        fclose(loop_candidate);
                }
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "cfg-backedge") &&
                    mir_is_profiled_allocator_backedge(
                        selector_name, generated_size, captured_size,
                        generated_instructions, captured_instructions))
                    fallback_reason = NULL;
                /* Phase 1 (mir-migration-plan-to-100pct.md): homed/spilled-
                 * scalar-cfg already passed every other cost gate above -
                 * the only reason this candidate is about to fall back is
                 * the generic per-block instruction-count margin.
                 * mir_try_emit_comparison_branch exists precisely for the
                 * exact whole-function shape "if (param OP param) return
                 * CONST; return CONST;" and was found, via a census
                 * instruction-count bucket audit, to already implement a
                 * tighter direct compare-and-branch for several real
                 * functions (tmirfuse.c's sge/sgt/sle/slt/sne/uge/ugt/ule/
                 * ult/seq) that the general homed/spilled lowering
                 * materializes as an explicit 0/1 boolean, stores, and
                 * re-tests instead - but it was only ever reachable through
                 * the DCC_MIR_EMIT_FUNCTION diagnostic dispatcher
                 * (mir_try_emit_z80), never from this production path.
                 * Retrying it here, gated on the exact same
                 * "everything else already passed" precondition the
                 * loop-family rescue above uses, can never affect any
                 * function homed/spilled-scalar-cfg would otherwise accept
                 * outright, and is only kept if it is not worse than
                 * legacy's own captured cost.
                 *
                 * Item T57 (mir-text-size-plan.md): also retry on a
                 * "text-size" fallback, not just "instruction-count" -
                 * the original gate only covered the one failure reason
                 * a census audit happened to find first. The exact same
                 * safety property applies regardless of which specific
                 * cost margin the general selector missed by: this
                 * candidate is only ever substituted in when it is not
                 * worse than legacy's own captured cost (the near-cost/
                 * byte-profitable check just below), so widening which
                 * fallback reasons attempt it cannot admit a function
                 * this rescue would not otherwise have accepted on its
                 * own merits. Found via tlongsub.c's if_lt/if_gt/if_le/
                 * if_ge (`if (a OP b) return 1; return 0;` for `long`
                 * a, b), which fail with "text-size", never "instruction-
                 * count", and so never reached this rescue at all before
                 * this change - independently of Item T57's other change
                 * (wide-operand support in mir_try_emit_comparison_branch
                 * itself, needed for these same functions since `long`
                 * comparisons were rejected by mir_emit_load_param's own
                 * 2-byte-only requirement). */
                if (fallback_reason != NULL &&
                    (!strcmp(fallback_reason, "instruction-count") ||
                     !strcmp(fallback_reason, "text-size")) &&
                    (mir.return_type & 15) == TYPE_INT) {
                    FILE *branch_candidate = tmpfile();
                    if (branch_candidate == NULL)
                        fatal("cannot create MIR comparison-branch "
                              "candidate stream");
                    label_id = mir_label_base;
                    if (mir_try_selector(branch_candidate,
                                         mir_try_emit_comparison_branch)) {
                        long branch_size = mir_stream_size(branch_candidate);
                        int branch_instructions =
                            mir_stream_instruction_count(branch_candidate);
                        if (branch_size >= 0 && branch_instructions >= 0 &&
                            !(branch_size > captured_size + 1 &&
                              !mir_is_profiled_near_cost_single_block(
                                  branch_size, captured_size,
                                  branch_instructions, captured_instructions) &&
                              !mir_is_byte_profitable_single_block(
                                  branch_size, captured_size,
                                  branch_instructions,
                                  captured_instructions))) {
                            fclose(generated);
                            generated = branch_candidate;
                            branch_candidate = NULL;
                            selector_name = "comparison-branch";
                            generated_size = branch_size;
                            generated_instructions = branch_instructions;
                            generated_label_id_after = label_id;
                            fallback_reason = NULL;
                        }
                    }
                    if (branch_candidate != NULL)
                        fclose(branch_candidate);
                }
                if (fallback_reason != NULL &&
                    (!strcmp(fallback_reason, "instruction-count") ||
                     !strcmp(fallback_reason, "text-size") ||
                     !strcmp(fallback_reason, "boolean-phi-cost")) &&
                    !rhs_forward_retry_attempted &&
                    !g_speculative_codegen_active) {
                    struct MirCandidateDescriptor candidate;
                    struct MirCandidateResult result;

                    rhs_forward_retry_attempted = 1;
                    mir_init_spilled_candidate(
                        &candidate, "rhs-forward",
                        "cannot create MIR RHS-forward candidate stream",
                        MIR_SPILLED_FEATURES_RHS);
                    mir_build_spilled_candidate(
                        &candidate, &result, mir_label_base);
                    if (mir_adopt_candidate_result(&generated, &result)) {
                        selector_name = "spilled-scalar-cfg";
                        emitted = 1;
                        generated_label_id_after = result.label_id_after;
                        fallback_reason = NULL;
                        goto evaluate_generated;
                    }
                    mir_close_candidate_result(&result);
                }
                if (fallback_reason != NULL &&
                    (!strcmp(fallback_reason, "instruction-count") ||
                     !strcmp(fallback_reason, "text-size") ||
                     !strcmp(fallback_reason, "planned-stack-cost")) &&
                    !store_address_retry_attempted &&
                    !g_speculative_codegen_active) {
                    struct MirCandidateDescriptor candidate;
                    struct MirCandidateResult result;

                    store_address_retry_attempted = 1;
                    mir_init_spilled_candidate(
                        &candidate, "store-address",
                        "cannot create MIR store-address candidate stream",
                        MIR_SPILLED_FEATURES_STORE_ADDRESS);
                    mir_build_spilled_candidate(
                        &candidate, &result, mir_label_base);
                    if (mir_adopt_candidate_result(&generated, &result)) {
                        selector_name = "spilled-scalar-cfg";
                        emitted = 1;
                        generated_label_id_after = result.label_id_after;
                        fallback_reason = NULL;
                        goto evaluate_generated;
                    }
                    mir_close_candidate_result(&result);
                }
                if (fallback_reason != NULL &&
                    (!strcmp(fallback_reason, "instruction-count") ||
                     !strcmp(fallback_reason, "text-size") ||
                     !strcmp(fallback_reason, "planned-stack-cost") ||
                     !strcmp(fallback_reason,
                             "indirect-store-address-cost")) &&
                    !wide_binary_retry_attempted &&
                    !g_speculative_codegen_active) {
                    struct MirCandidateDescriptor candidate;
                    struct MirCandidateResult result;

                    wide_binary_retry_attempted = 1;
                    mir_init_spilled_candidate(
                        &candidate, "wide-binary-lhs",
                        "cannot create MIR wide-binary candidate stream",
                        MIR_SPILLED_FEATURES_WIDE_LHS);
                    mir_build_spilled_candidate(
                        &candidate, &result, mir_label_base);
                    if (mir_adopt_candidate_result(&generated, &result)) {
                        selector_name = "spilled-scalar-cfg";
                        emitted = 1;
                        generated_label_id_after = result.label_id_after;
                        fallback_reason = NULL;
                        goto evaluate_generated;
                    }
                    mir_close_candidate_result(&result);
                }
                if (fallback_reason != NULL &&
                    (!strcmp(fallback_reason, "instruction-count") ||
                     !strcmp(fallback_reason, "text-size") ||
                     !strcmp(fallback_reason, "planned-stack-cost") ||
                     !strcmp(fallback_reason,
                             "indirect-store-address-cost")) &&
                    !stable_pointer_argument_retry_attempted &&
                    !g_speculative_codegen_active) {
                    struct MirCandidateDescriptor candidate;
                    struct MirCandidateResult result;

                    stable_pointer_argument_retry_attempted = 1;
                    mir_init_spilled_candidate(
                        &candidate, "stable-pointer-argument",
                        "cannot create MIR pointer-argument candidate stream",
                        MIR_SPILLED_FEATURES_STABLE_ARG);
                    mir_build_spilled_candidate(
                        &candidate, &result, mir_label_base);
                    if (mir_adopt_candidate_result(&generated, &result)) {
                        selector_name = "spilled-scalar-cfg";
                        emitted = 1;
                        generated_label_id_after = result.label_id_after;
                        fallback_reason = NULL;
                        goto evaluate_generated;
                    }
                    mir_close_candidate_result(&result);
                }
                if (fallback_reason != NULL &&
                    (!strcmp(fallback_reason, "instruction-count") ||
                     !strcmp(fallback_reason, "text-size") ||
                     !strcmp(fallback_reason, "planned-stack-cost") ||
                     !strcmp(fallback_reason,
                             "indirect-store-address-cost")) &&
                    !global_argument_retry_attempted &&
                    !g_speculative_codegen_active) {
                    struct MirCandidateDescriptor candidate;
                    struct MirCandidateResult result;

                    global_argument_retry_attempted = 1;
                    mir_init_spilled_candidate(
                        &candidate, "global-argument",
                        "cannot create MIR global-argument candidate stream",
                        MIR_SPILLED_FEATURES_GLOBAL_ARG);
                    mir_build_spilled_candidate(
                        &candidate, &result, mir_label_base);
                    if (mir_adopt_candidate_result(&generated, &result)) {
                        selector_name = "spilled-scalar-cfg";
                        emitted = 1;
                        generated_label_id_after = result.label_id_after;
                        fallback_reason = NULL;
                        goto evaluate_generated;
                    }
                    mir_close_candidate_result(&result);
                }
                if (fallback_reason != NULL &&
                    (!strcmp(fallback_reason, "instruction-count") ||
                     !strcmp(fallback_reason, "text-size") ||
                     !strcmp(fallback_reason, "planned-stack-cost") ||
                     !strcmp(fallback_reason,
                             "indirect-store-address-cost")) &&
                    !wide_argument_stack_retry_attempted &&
                    !g_speculative_codegen_active) {
                    struct MirCandidateDescriptor candidate;
                    struct MirCandidateResult result;

                    wide_argument_stack_retry_attempted = 1;
                    mir_init_spilled_candidate(
                        &candidate, "stack-argument",
                        "cannot create MIR wide stack-argument candidate "
                        "stream",
                        MIR_SPILLED_FEATURES_CALL_STACK);
                    mir_build_spilled_candidate(
                        &candidate, &result, mir_label_base);
                    if (mir_adopt_candidate_result(&generated, &result)) {
                        selector_name = "spilled-scalar-cfg";
                        emitted = 1;
                        generated_label_id_after = result.label_id_after;
                        fallback_reason = NULL;
                        goto evaluate_generated;
                    }
                    mir_close_candidate_result(&result);
                }
                if (fallback_reason != NULL &&
                    (!strcmp(fallback_reason, "instruction-count") ||
                     !strcmp(fallback_reason, "text-size") ||
                     !strcmp(fallback_reason, "planned-stack-cost") ||
                     !strcmp(fallback_reason,
                             "dead-store-forwarding-cost") ||
                     !strcmp(fallback_reason,
                             "indirect-store-address-cost")) &&
                    !promoted_local_slot_retry_attempted &&
                    !g_speculative_codegen_active) {
                    struct MirCandidateDescriptor candidate;
                    struct MirCandidateResult result;

                    promoted_local_slot_retry_attempted = 1;
                    mir_init_spilled_candidate(
                        &candidate, "promoted-local-slot",
                        "cannot create MIR promoted-local-slot candidate "
                        "stream",
                        MIR_SPILLED_FEATURES_PROMOTED_LOCAL);
                    mir_build_spilled_candidate(
                        &candidate, &result, mir_label_base);
                    if (mir_adopt_candidate_result(&generated, &result)) {
                        selector_name = "spilled-scalar-cfg";
                        emitted = 1;
                        generated_label_id_after = result.label_id_after;
                        fallback_reason = NULL;
                        goto evaluate_generated;
                    }
                    mir_close_candidate_result(&result);
                }
                if (fallback_reason != NULL &&
                    (!strcmp(fallback_reason, "instruction-count") ||
                     !strcmp(fallback_reason, "text-size") ||
                     !strcmp(fallback_reason, "planned-stack-cost") ||
                     !strcmp(fallback_reason,
                             "dead-store-forwarding-cost") ||
                     !strcmp(fallback_reason,
                             "indirect-store-address-cost")) &&
                    !wide_binary_rhs_retry_attempted &&
                    !g_speculative_codegen_active) {
                    struct MirCandidateDescriptor candidate;
                    struct MirCandidateResult result;

                    wide_binary_rhs_retry_attempted = 1;
                    mir_init_spilled_candidate(
                        &candidate, "wide-rhs",
                        "cannot create MIR wide-RHS candidate stream",
                        MIR_SPILLED_FEATURES_ALL);
                    mir_build_spilled_candidate(
                        &candidate, &result, mir_label_base);
                    if (mir_adopt_candidate_result(&generated, &result)) {
                        selector_name = "spilled-scalar-cfg";
                        emitted = 1;
                        generated_label_id_after = result.label_id_after;
                        fallback_reason = NULL;
                        goto evaluate_generated;
                    }
                    mir_close_candidate_result(&result);
                }
                if (fallback_reason != NULL &&
                    (!strcmp(fallback_reason, "instruction-count") ||
                     !strcmp(fallback_reason, "text-size")) &&
                    !strict_phi_retry_attempted &&
                    !g_speculative_codegen_active &&
                    (!strcmp(selector_name, "spilled-scalar-cfg") ||
                     !strcmp(selector_name, "homed-scalar-cfg"))) {
                    FILE *phi_candidate = tmpfile();
                    int phi_emitted;

                    strict_phi_retry_attempted = 1;
                    if (phi_candidate == NULL)
                        fatal("cannot create MIR strict-phi candidate stream");
                    mir_begin_strict_phi_fallthrough();
                    strict_phi_fallthrough_active = 1;
                    label_id = mir_label_base;
                    phi_emitted = mir_try_selector(
                        phi_candidate,
                        !strcmp(selector_name, "homed-scalar-cfg")
                            ? mir_try_emit_homed_scalar_cfg
                            : mir_try_emit_spilled_scalar_cfg);
                    if (phi_emitted) {
                        fclose(generated);
                        generated = phi_candidate;
                        phi_candidate = NULL;
                        emitted = 1;
                        generated_label_id_after = label_id;
                        fallback_reason = NULL;
                        goto evaluate_generated;
                    }
                    mir_end_strict_phi_fallthrough();
                    strict_phi_fallthrough_active = 0;
                    fclose(phi_candidate);
                }
                if (fallback_reason != NULL &&
                    strict_phi_fallthrough_active) {
                    mir_end_strict_phi_fallthrough();
                    strict_phi_fallthrough_active = 0;
                }
                if (fallback_reason != NULL &&
                    !rematerialized_home_retry_attempted &&
                    mir_homed_rematerializable_wide_candidate_count() > 0 &&
                    !g_speculative_codegen_active) {
                    FILE *rematerialized_candidate = tmpfile();
                    int rematerialized_emitted = 0;

                    rematerialized_home_retry_attempted = 1;
                    if (rematerialized_candidate == NULL)
                        fatal("cannot create MIR rematerialized-home "
                              "candidate stream");
                    if (mir_begin_rematerialized_home_allocation()) {
                        rematerialized_home_allocation_active = 1;
                        label_id = mir_label_base;
                        rematerialized_emitted = mir_try_selector(
                            rematerialized_candidate,
                            mir_try_emit_homed_scalar_cfg);
                    }
                    if (rematerialized_emitted) {
                        fclose(generated);
                        generated = rematerialized_candidate;
                        rematerialized_candidate = NULL;
                        selector_name = "homed-scalar-cfg";
                        emitted = 1;
                        generated_label_id_after = label_id;
                        fallback_reason = NULL;
                        goto evaluate_generated;
                    }
                    if (rematerialized_home_allocation_active) {
                        mir_end_rematerialized_home_allocation();
                        rematerialized_home_allocation_active = 0;
                    }
                    fclose(rematerialized_candidate);
                }
                if (fallback_reason != NULL &&
                    rematerialized_home_allocation_active) {
                    mir_end_rematerialized_home_allocation();
                    rematerialized_home_allocation_active = 0;
                }
                {
                    const char *forced_accept =
                        getenv("DCC_MIR_FORCE_ACCEPT_FUNCTION");
                    if (forced_accept != NULL &&
                        !strcmp(forced_accept, mir.name))
                        fallback_reason = NULL;
                    if (fallback_reason != NULL &&
                        mir_force_accept_reasons_matches(fallback_reason))
                        fallback_reason = NULL;
                    if (fallback_reason != NULL &&
                        mir_reason_is_proven_cost_only(fallback_reason))
                        fallback_reason = NULL;
                    if (fallback_reason != NULL &&
                        !strcmp(fallback_reason, "cfg-backedge"))
                        /* T435: the complete remaining 16-function cohort
                         * passed the full extended correctness gate after
                         * fixing forwarded final fastcall arguments. Clear
                         * this only at the final acceptance point so all
                         * specialized loop retries above retain priority. */
                        fallback_reason = NULL;
                    if (fallback_reason != NULL &&
                        strcmp(fallback_reason, "forced") &&
                        measured_boolean_candidate &&
                        mir_boolean_phi_profile_is_semantically_eligible())
                        fallback_reason = NULL;
                    if (fallback_reason != NULL &&
                        !strcmp(fallback_reason, "boolean-phi-cost") &&
                        mir_boolean_phi_coverage_is_semantically_eligible(
                            generated_size, captured_size))
                        /* T436: the complete no-backedge, no-inline-call,
                         * no-pointer-array, no-label-fallthrough population
                         * under the 2 KiB growth ceiling passed the full
                         * extended correctness gate as one cohort. */
                        fallback_reason = NULL;
                    if (fallback_reason != NULL &&
                        !strcmp(fallback_reason, "text-size") &&
                        mir_text_size_coverage_is_semantically_eligible(
                            generated_size, captured_size))
                        /* T437: the complete bounded scalar/acyclic
                         * text-size population passed the full extended
                         * correctness gate as one cohort. */
                        fallback_reason = NULL;
                    if (fallback_reason != NULL &&
                        !strcmp(fallback_reason, "text-size") &&
                        !strcmp(selector_name, "spilled-scalar-cfg") &&
                        mir_spilled_cfg_has_wide_mulmod_fusion())
                        /* T438: the spilled selector now mirrors legacy's
                         * exact overflow-safe __m1mu fused multiply/modulo
                         * lowering for three plain unsigned-word sources. */
                        fallback_reason = NULL;
                    if (fallback_reason != NULL &&
                        !strcmp(fallback_reason, "text-size") &&
                        mir_text_size_wide_coverage_is_semantically_eligible(
                            generated_size, captured_size))
                        /* T438: after the parameter-only mulmod fusion fix,
                         * the complete bounded acyclic wide population
                         * passed the full extended correctness gate. */
                        fallback_reason = NULL;
                    if (fallback_reason != NULL &&
                        !strcmp(fallback_reason, "boolean-phi-cost") &&
                        mir_profile_matches_function(
                            "DCC_MIR_PROFILE_BOOLEAN_PHI") &&
                        mir_boolean_phi_profile_is_semantically_eligible() &&
                        generated_size <= captured_size + 1 &&
                        generated_instructions <= captured_instructions)
                        fallback_reason = NULL;
                }
                if (rematerialized_home_allocation_active) {
                    mir_end_rematerialized_home_allocation();
                    rematerialized_home_allocation_active = 0;
                }
                if (address_rematerialization_active) {
                    if (block_cse_address_rematerialization_active) {
                        mir_end_block_cse_address_rematerialization();
                        block_cse_address_rematerialization_active = 0;
                    }
                    mir_end_address_rematerialization();
                    mir_end_all_spilled_fallback_optimizations();
                    address_rematerialization_active = 0;
                }
                if (lazy_allocation_active) {
                    mir_end_lazy_parameter_allocation();
                    lazy_allocation_active = 0;
                }
                if (stable_local_homes_active) {
                    mir_end_stable_pointer_local_homes();
                    stable_local_homes_active = 0;
                }
                if (strict_phi_fallthrough_active) {
                    mir_end_strict_phi_fallthrough();
                    strict_phi_fallthrough_active = 0;
                }
                if (fallback_reason != NULL &&
                    !phi_return_forwarding_retry_attempted &&
                    mir_cfg_block_count() <= 10 &&
                    !g_speculative_codegen_active) {
                    phi_return_forwarding_retry_attempted = 1;
                    mir_reset_phi_return_forwarding_count();
                    mir_forward_immediate_phi_returns();
                    if (mir_phi_return_forwarding_count_value() > 0) {
                        fclose(generated);
                        generated = NULL;
                        verified = mir_verify_and_dump();
                        if (verified) {
                            mir_compute_dead_local_suffix();
                            mir_report_dead_local_suffix();
                            goto retry_selection;
                        }
                    }
                }
                if (fallback_reason != NULL &&
                    !boolean_phi_retry_attempted &&
                    !g_speculative_codegen_active) {
                    /* Preserve every already-selected function byte-for-byte:
                     * simplify only after the ordinary selector and all of
                     * its established retries have chosen legacy fallback. */
                    boolean_phi_retry_attempted = 1;
                    mir_simplify_boolean_phi_branches();
                    if (mir_boolean_phi_branch_simplification_count() > 0) {
                        fclose(generated);
                        generated = NULL;
                        verified = mir_verify_and_dump();
                        if (verified) {
                            mir_compute_dead_local_suffix();
                            mir_report_dead_local_suffix();
                            goto retry_selection;
                        }
                    }
                }
                if (fallback_reason != NULL &&
                    !block_cse_retry_attempted &&
                    !g_speculative_codegen_active) {
                    int block_cse_eliminated =
                        mir_value_number_global_field_loads();

                    /* Keep incumbent MIR byte-identical unless an exact
                     * block-local reuse pass proves a real opportunity. */
                    block_cse_retry_attempted = 1;
                    block_cse_captured_spills = mir.allocation_spill_count;
                    block_cse_captured_fixed_moves =
                        mir.allocation_fixed_moves;
                    block_cse_captured_operand_moves =
                        mir.allocation_operand_moves;
                    block_cse_captured_phi_moves = mir.allocation_phi_moves;
                    if (block_cse_eliminated == 0 &&
                        mir_cfg_block_count() == 1 &&
                        mir_eliminate_common_block_expressions() >= 3)
                        block_cse_eliminated +=
                            mir_common_block_expression_elimination_count();
                    if (block_cse_eliminated > 0) {
                        fclose(generated);
                        generated = NULL;
                        verified = mir_verify_and_dump();
                        if (verified) {
                            mir_compute_dead_local_suffix();
                            mir_report_dead_local_suffix();
                            goto retry_selection;
                        }
                    }
                }
                /* Single-use MIR_ADDRESS bases can already rematerialize
                 * directly at their MIR_INDEX_ADDRESS use site. Retry that
                 * existing path before keeping a measured planned-index-base
                 * miss on the books, and for absolute-index-cost only when
                 * the provisional candidate is not already raw-byte smaller
                 * than legacy. */
                if (fallback_reason != NULL &&
                    (!strcmp(fallback_reason, "instruction-count") ||
                     !strcmp(fallback_reason, "text-size") ||
                     !strcmp(fallback_reason, "planned-index-base-cost") ||
                     !strcmp(fallback_reason, "absolute-index-cost")) &&
                    (strcmp(fallback_reason, "absolute-index-cost") != 0 ||
                     generated_size >= captured_size) &&
                    !address_rematerialization_retry_attempted &&
                    mir_address_rematerialization_candidate_count() > 0 &&
                    !g_speculative_codegen_active) {
                    FILE *address_rematerialized_candidate = tmpfile();
                    int address_rematerialized_emitted;

                    address_rematerialization_retry_attempted = 1;
                    if (address_rematerialized_candidate == NULL)
                        fatal("cannot create MIR address-rematerialized "
                              "candidate stream");
                    mir_begin_all_spilled_fallback_optimizations();
                    mir_begin_address_rematerialization();
                    address_rematerialization_active = 1;
                    label_id = mir_label_base;
                    address_rematerialized_emitted = mir_try_selector(
                        address_rematerialized_candidate,
                        mir_try_emit_spilled_scalar_cfg);
                    if (address_rematerialized_emitted) {
                        fclose(generated);
                        generated = address_rematerialized_candidate;
                        address_rematerialized_candidate = NULL;
                        selector_name = "spilled-scalar-cfg";
                        emitted = 1;
                        generated_label_id_after = label_id;
                        fallback_reason = NULL;
                        goto evaluate_generated;
                    }
                    fclose(address_rematerialized_candidate);
                }
                if (address_rematerialization_active) {
                    mir_end_address_rematerialization();
                    mir_end_all_spilled_fallback_optimizations();
                    address_rematerialization_active = 0;
                }
                /* Item T402 (mir-text-size-plan.md): the same-slot phi
                 * spill/reload cleanup is a real large-gap backedge
                 * reducer, but bounded full-mode A/B on near-cost or
                 * already-byte-profitable functions found regressions.
                 * Keep it as a final spilled-selector retry only for
                 * backedge bodies that are still materially over budget
                 * after every earlier retry, so it improves the targeted
                 * text-size cluster without perturbing selected output. */
                if (fallback_reason != NULL &&
                    !strcmp(selector_name, "spilled-scalar-cfg") &&
                    mir_has_cfg_backedge() &&
                    (generated_size > captured_size + 64 ||
                     generated_instructions > captured_instructions + 4) &&
                    (!strcmp(fallback_reason, "instruction-count") ||
                     !strcmp(fallback_reason, "text-size") ||
                     !strcmp(fallback_reason, "cfg-backedge") ||
                     !strcmp(fallback_reason, "wide-constant-cost") ||
                     !strcmp(fallback_reason, "rhs-stack-cost") ||
                     !strcmp(fallback_reason, "planned-stack-cost") ||
                     !strcmp(fallback_reason,
                             "dead-store-forwarding-cost") ||
                     !strcmp(fallback_reason,
                             "indirect-store-address-cost")) &&
                    !phi_slot_retry_attempted &&
                    !g_speculative_codegen_active) {
                    struct MirCandidateDescriptor candidate;
                    struct MirCandidateResult result;

                    phi_slot_retry_attempted = 1;
                    mir_init_spilled_candidate(
                        &candidate, "phi-slot",
                        "cannot create MIR phi-slot candidate stream",
                        MIR_SPILLED_FEATURES_PHI_SLOT);
                    mir_build_spilled_candidate(
                        &candidate, &result, mir_label_base);
                    if (mir_adopt_candidate_result(&generated, &result)) {
                        selector_name = "spilled-scalar-cfg";
                        emitted = 1;
                        generated_label_id_after = result.label_id_after;
                        fallback_reason = NULL;
                        goto evaluate_generated;
                    }
                    mir_close_candidate_result(&result);
                }
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "text-size") &&
                    mir_text_size_simple_backedge_is_semantically_eligible(
                        generated_size, captured_size))
                    /* T439: the terminal scalar tiny-loop cohort passed the
                     * full extended gate. Keep this at the actual final
                     * decision point so all earlier retries retain priority. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "text-size") &&
                    mir_text_size_post_phi_is_semantically_eligible(
                        generated_size))
                    /* T449: after fixing wide forwarding across emitted
                     * constants, the complete terminal post-PHI text-size
                     * cohort below 10,000 bytes passed full extended. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "boolean-phi-cost") &&
                    mir_boolean_phi_small_loop_is_semantically_eligible(
                        generated_size, captured_size))
                    /* T448: after forcing concrete storage for computed PHI
                     * sources, the bounded small-loop boolean cohort passed. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "boolean-phi-cost") &&
                    mir_boolean_phi_medium_scalar_loop_is_semantically_eligible(
                        generated_size, captured_size))
                    /* T452: the medium boolean-loop stratum below every known
                     * unsafe block/size boundary passed full extended. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "dynamic-index-base-cost") &&
                    !g_speculative_codegen_active &&
                    mir_dynamic_index_base_loop_is_semantically_eligible(
                        generated_size, captured_size)) {
                    /* T451: the non-speculative scalar bounded dynamic-index
                     * loop cohort passed the full extended gate. */
                    fallback_reason = NULL;
                }
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "unary-not-cost") &&
                    !g_speculative_codegen_active &&
                    mir_unary_not_call_free_loop_is_semantically_eligible(
                        generated_size, captured_size))
                    /* T452: the non-speculative call-free scalar unary-loop
                     * stratum passed the full extended gate. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "dynamic-index-base-cost") &&
                    !g_speculative_codegen_active &&
                    mir_dynamic_index_base_wide_is_semantically_eligible(
                        generated_size))
                    /* T453: the bounded wide dynamic-index stratum outside
                     * every known unsafe shape passed full extended. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "dynamic-index-base-cost") &&
                    !g_speculative_codegen_active &&
                    mir_dynamic_index_base_vla_is_semantically_eligible(
                        generated_size))
                    /* T454: the bounded VLA dynamic-index stratum passed the
                     * full extended gate after PHI repair. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "wide-constant-cost") &&
                    !g_speculative_codegen_active)
                    /* T454: every remaining terminal wide-constant candidate
                     * passed together; transient tpfauto remains block-CSE. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    mir_reason_uses_bounded_acyclic_coverage(
                        fallback_reason) &&
                    mir_bounded_acyclic_coverage_is_semantically_eligible(
                        generated_size, captured_size)) {
                    /* T440-T445: each listed terminal-reason cohort passed
                     * independently, and each multi-reason batch also passed
                     * together with all previously landed cohorts. */
                    fallback_reason = NULL;
                }
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "wide-store-cost") &&
                    mir_wide_store_coverage_is_semantically_eligible(
                        generated_size, captured_size))
                    /* T443: the terminal bounded acyclic call-containing
                     * wide-store cohort passed the full extended gate. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "binary-load-pair-cost") &&
                    mir_binary_load_pair_coverage_is_semantically_eligible(
                        generated_size, captured_size))
                    /* T445: the terminal bounded binary-load-pair cohort
                     * excluding its single-call resource stratum passed. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "wide-store-cost") &&
                    mir_wide_store_large_acyclic_is_semantically_eligible(
                        generated_size))
                    /* T453: the larger acyclic wide-store stratum outside all
                     * known failures passed full extended. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "pointer-array"))
                    /* T446: after preserving dereferenced pointer-array
                     * dimension consumption through metadata repair, the
                     * complete pointer-array cohort passed full extended. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "inline-substitution") &&
                    mir_inline_substitution_coverage_is_semantically_eligible(
                        generated_size, captured_size))
                    /* T446: the complete terminal acyclic inline-temp
                     * stratum with at least 32 reserved local bytes passed. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "block-cse-cost") &&
                    mir_block_cse_post_phi_is_semantically_eligible(
                        generated_size))
                    /* T450: the post-PHI bounded block-CSE population outside
                     * the wide/20-call failure stratum passed full extended. */
                    fallback_reason = NULL;
                if (fallback_reason == NULL &&
                    mir_has_unconsumed_inline_temp_overwrite())
                    /* Nested inline expansion reused one #itmp slot before
                     * the outer value's first load. Keep this semantic shape
                     * on fallback until MIR gives nesting levels distinct
                     * temporary identities. */
                    fallback_reason = "inline-temp-overlap";
                if (fallback_reason != NULL)
                    emitted = 0;
                /* Item T66b: this is the single point where the accept/
                 * reject decision for this function is now final. Restore
                 * label_id to reflect exactly the labels actually kept -
                 * the winning candidate's own consumption on accept, or
                 * the pre-trial base (discarding every trial's waste
                 * entirely) on fallback - so no discarded candidate can
                 * ever shift a later function's label numbering. */
                label_id = emitted ? generated_label_id_after : mir_label_base;
            }
        }
        if (emitted)
            mir_mark_selected_inline_call_bodies_needed();
        selected_hash = mir_copy_selected_stream(
            emitted ? generated : mir.capture_stream, destination);
        if (generated != NULL)
            fclose(generated);
        if (mir.report_mode && !g_speculative_codegen_active)
            fprintf(stderr, "; MIR emit function=%s result=%s\n",
                mir.name, emitted ? "mir" : "fallback");
        /* See the oversized-fallback report above: a buffered/speculative
         * legacy attempt's generated/captured sizes describe codegen that
         * is discarded and never reaches the real output, so it must not
         * be reported to the census or DCC_MIR_SELECT_REPORT consumers. */
        if (getenv("DCC_MIR_SELECT_REPORT") != NULL && !g_speculative_codegen_active)
            fprintf(stderr,
                    "; MIR selection function=%s selector=%s result=%s "
                    "reason=%s generated-bytes=%ld captured-bytes=%ld "
                    "generated-insns=%d captured-insns=%d blocks=%d "
                    "selected-hash=%08lx\n",
                    mir.name, selector_name, emitted ? "mir" : "fallback",
                    fallback_reason != NULL ? fallback_reason : "accepted",
                    generated_size, captured_size, generated_instructions,
                    captured_instructions, mir_cfg_block_count(),
                    selected_hash);
        if (verified && !g_speculative_codegen_active &&
            getenv("DCC_MIR_CANDIDATE_MATRIX") != NULL)
            mir_report_spilled_candidate_matrix(candidate_matrix_label_base);
        fclose(mir.capture_stream);
        mir.capture_stream = NULL;
        mir.emit_mode = 0;
    }
    free(mir.live_in);
    free(mir.live_out);
    mir.live_in = NULL;
    mir.live_out = NULL;
    mir.active = 0;
}
