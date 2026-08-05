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

static long mir_stream_size(FILE *stream)
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

static int mir_stream_instruction_count(FILE *stream)
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

static int mir_cfg_block_count(void)
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
            (mir.insns[i].memory_flags & 2048) != 0)
            return 1;
    return 0;
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

static int mir_has_cfg_backedge(void)
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

static int mir_is_profiled_slotless_two_block_win(
    long generated_size, long captured_size, int generated_instructions,
    int captured_instructions)
{
    /* The refreshed post-Phase-2 census found exactly three two-block
     * candidates within this margin. The two add_string instances still
     * require three backend slots and miscompile under forced acceptance;
     * global_escape_store has no backend slots and is non-regressing in
     * both modes (10 nopeep cycles faster, peep neutral). Slotlessness is
     * the structural discriminator: the selector is not hiding unmodelled
     * frame traffic behind a small assembly-text delta. */
    return !mir.has_vla && mir_cfg_block_count() <= 2 &&
           mir.backend_slot_count == 0 &&
           generated_size <= captured_size + 10 &&
           generated_instructions < captured_instructions;
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
            if (!emitted && (general_filter == NULL ||
                             general_filter[0] == 0)) {
                selector_name = "spilled-scalar-cfg";
                label_id = mir_label_base;
                emitted = mir_try_selector(generated,
                                           mir_try_emit_spilled_scalar_cfg);
                generated_label_id_after = label_id;
            }
            if (emitted) {
                generated_size = mir_stream_size(generated);
                if (!strcmp(selector_name, "spilled-scalar-cfg") &&
                    mir_spilled_scalar_cfg_elided_epilogue_bytes > 0 &&
                    generated_size >= 0)
                    /* mir-migration-plan-next10 Item 3: restore the byte
                     * count the acceptance gate would have seen before the
                     * dead trailing epilogue was deduplicated, so this
                     * unrelated dead-code removal cannot newly promote a
                     * function that only cleared the gate because of its
                     * savings (skill rule 1). The real emitted stream
                     * `generated` is left untouched and still
                     * deduplicated. */
                    generated_size += mir_spilled_scalar_cfg_elided_epilogue_bytes;
                captured_size = mir_stream_size(mir.capture_stream);
                generated_instructions = mir_stream_instruction_count(generated);
                captured_instructions =
                    mir_stream_instruction_count(mir.capture_stream);
                fallback_reason = NULL;
                {
                    const char *forced_function =
                        getenv("DCC_MIR_FORCE_FALLBACK_FUNCTION");
                    if (getenv("DCC_MIR_FORCE_FALLBACK") != NULL ||
                        (forced_function != NULL &&
                         !strcmp(forced_function, mir.name)))
                        fallback_reason = "forced";
                }
                if (fallback_reason != NULL) {
                    /* Keep the selected reason. */
                }
                else if (generated_size < 0 || captured_size < 0 ||
                    generated_instructions < 0 || captured_instructions < 0)
                    fallback_reason = "measurement";
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
                                                 !mir_is_profiled_dead_suffix_instruction_win(
                                                     generated_size, captured_size,
                                                     generated_instructions,
                                                     captured_instructions))
                    fallback_reason = "text-size";
                else if (generated_instructions > captured_instructions +
                        (!strcmp(selector_name, "homed-scalar-cfg")
                            ? (mir_cfg_block_count() <= 2 ? 2 : 1)
                        : (!strcmp(selector_name, "spilled-scalar-cfg") &&
                           generated_size <= captured_size ? 1 : 0)) &&
                         !mir_is_profiled_near_cost_single_block(
                             generated_size, captured_size,
                             generated_instructions, captured_instructions) &&
                         !mir_is_byte_profitable_single_block(
                             generated_size, captured_size,
                             generated_instructions, captured_instructions) &&
                         !mir_is_profiled_constant_bound_loop_pair(
                             generated_size, captured_size,
                             generated_instructions, captured_instructions))
                    fallback_reason = "instruction-count";
                else if (mir_cfg_block_count() > 64)
                    fallback_reason = "cfg-block-count";
                /* Item A (mir-migration-plan-forward.md): the previous
                 * "near-cost" exception here let a MIR_CALL to a
                 * static-inline callee through even though such a callee
                 * has no standalone emitted body once legacy's AST-level
                 * inline substitution eliminates every call site (verified
                 * via DCC_MIR_FORCE_ACCEPT_FUNCTION=assign_pre in
                 * tests/forint.c: MIR emitted `call _Z0026` to
                 * set_sym_val, a label with zero definitions anywhere in
                 * the program, causing total loss of execution). A
                 * corpus-wide scan confirmed no function currently
                 * exploits the exception (dead code, zero net coverage
                 * change), but it remains unsound because it never checks
                 * whether the callee actually has a materialized body.
                 * Any inline-substitution call must always fall back. */
                else if (mir_has_inline_substitution_call())
                    fallback_reason = "inline-substitution";
                else if (mir_has_declared_pointer_array())
                    fallback_reason = "pointer-array";
                else if (mir_has_cfg_backedge() &&
                         !mir_has_profiled_positive_loop() &&
                         !mir_is_profiled_constant_bound_loop_pair(
                             generated_size, captured_size,
                             generated_instructions, captured_instructions))
                    fallback_reason = "cfg-backedge";
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
                {
                    const char *forced_accept =
                        getenv("DCC_MIR_FORCE_ACCEPT_FUNCTION");
                    if (forced_accept != NULL &&
                        !strcmp(forced_accept, mir.name))
                        fallback_reason = NULL;
                }
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
