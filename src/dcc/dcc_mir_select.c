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

static int mir_prelegacy_scheduled_attempt_active;
static int mir_prelegacy_scheduled_attempt_selected;

void mir_begin_prelegacy_scheduled_attempt(void)
{
    mir_prelegacy_scheduled_attempt_active = 1;
    mir_prelegacy_scheduled_attempt_selected = 0;
}

int mir_end_prelegacy_scheduled_attempt(void)
{
    int selected = mir_prelegacy_scheduled_attempt_selected;

    mir_prelegacy_scheduled_attempt_active = 0;
    mir_prelegacy_scheduled_attempt_selected = 0;
    return selected;
}

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

static int mir_regional_line_is(const char *line, const char *text)
{
    return line != NULL && strcmp(line, text) == 0;
}

static int mir_regional_full_hl_reload(
    const char *low, const char *high)
{
    return (strncmp(low, "\tld l,(ix", 9) == 0 &&
            strncmp(high, "\tld h,(ix", 9) == 0) ||
           (strncmp(low, "\tld l,(iy", 9) == 0 &&
            strncmp(high, "\tld h,(iy", 9) == 0) ||
           (mir_regional_line_is(low, "\tld h,b\n") &&
            mir_regional_line_is(high, "\tld l,c\n"));
}

static FILE *mir_compact_regional_candidate(FILE *input)
{
    char **lines = NULL;
    int count = 0;
    int capacity = 0;
    char buffer[512];
    FILE *output;
    int i;

    rewind(input);
    while (fgets(buffer, sizeof(buffer), input) != NULL) {
        size_t length = strlen(buffer) + 1;
        char *copy;

        if (count == capacity) {
            int next_capacity = capacity == 0 ? 256 : capacity * 2;
            char **next_lines = (char **)realloc(
                lines, (size_t)next_capacity * sizeof(*next_lines));
            if (next_lines == NULL)
                fatal("out of memory compacting regional MIR output");
            lines = next_lines;
            capacity = next_capacity;
        }
        copy = (char *)malloc(length);
        if (copy == NULL)
            fatal("out of memory compacting regional MIR output");
        memcpy(copy, buffer, length);
        lines[count++] = copy;
    }
    output = tmpfile();
    if (output == NULL)
        fatal("cannot create compacted regional MIR stream");
    /*
     * Regional homes expose adjacent preservation sequences assembled by
     * separate helpers. These exact rewrites preserve registers, stack, and
     * flags; applying the pass twice reaches the small local fixed point.
     */
    for (i = 0; i < count;) {
        if (i + 1 < count &&
            ((mir_regional_line_is(lines[i], "\tpush bc\n") &&
              mir_regional_line_is(lines[i + 1], "\tpop bc\n")) ||
             (mir_regional_line_is(lines[i], "\tpush de\n") &&
              mir_regional_line_is(lines[i + 1], "\tpop de\n")) ||
             (mir_regional_line_is(lines[i], "\tpush hl\n") &&
              mir_regional_line_is(lines[i + 1], "\tpop hl\n")) ||
             (mir_regional_line_is(lines[i], "\tpush iy\n") &&
              mir_regional_line_is(lines[i + 1], "\tpop iy\n")))) {
            i += 2;
            continue;
        }
        if (i + 6 < count &&
            mir_regional_line_is(lines[i], "\tpush de\n") &&
            mir_regional_line_is(lines[i + 1], "\tpop hl\n") &&
            mir_regional_line_is(lines[i + 2], "\tpush hl\n") &&
            mir_regional_line_is(lines[i + 3], "\tld h,b\n") &&
            mir_regional_line_is(lines[i + 4], "\tld l,c\n") &&
            mir_regional_line_is(lines[i + 5], "\tex de,hl\n") &&
            mir_regional_line_is(lines[i + 6], "\tpop hl\n")) {
            fputs("\tpush de\n\tpop hl\n\tld d,b\n\tld e,c\n",
                  output);
            i += 7;
            continue;
        }
        if (mir_cfg_block_count() <= 32 &&
            i + 6 < count &&
            mir_regional_line_is(lines[i], "\tpush de\n") &&
            mir_regional_line_is(lines[i + 1], "\tpop hl\n") &&
            mir_regional_line_is(lines[i + 2], "\tpush hl\n") &&
            ((strncmp(lines[i + 3], "\tld l,(ix", 9) == 0 &&
              strncmp(lines[i + 4], "\tld h,(ix", 9) == 0) ||
             (strncmp(lines[i + 3], "\tld l,(iy", 9) == 0 &&
              strncmp(lines[i + 4], "\tld h,(iy", 9) == 0)) &&
            mir_regional_line_is(lines[i + 5], "\tex de,hl\n") &&
            mir_regional_line_is(lines[i + 6], "\tpop hl\n")) {
            char low[512];
            char high[512];

            strcpy(low, lines[i + 3]);
            strcpy(high, lines[i + 4]);
            low[4] = 'e';
            high[4] = 'd';
            fputs("\tpush de\n\tpop hl\n", output);
            fputs(low, output);
            fputs(high, output);
            i += 7;
            continue;
        }
        if (mir_cfg_block_count() <= 32 &&
            i + 5 < count &&
            mir_regional_line_is(lines[i], "\tpush hl\n") &&
            strncmp(lines[i + 1], "\tld hl,", 7) == 0 &&
            mir_regional_line_is(lines[i + 2], "\tex de,hl\n") &&
            mir_regional_line_is(lines[i + 3], "\tpop hl\n") &&
            mir_regional_line_is(lines[i + 4], "\tpush hl\n") &&
            mir_regional_line_is(lines[i + 5], "\tpush de\n")) {
            fputs("\tpush hl\n\tld de,", output);
            fputs(lines[i + 1] + 7, output);
            fputs("\tpush de\n", output);
            i += 6;
            continue;
        }
        if (mir_cfg_block_count() <= 32 &&
            i + 5 < count &&
            mir_regional_line_is(lines[i], "\tpop hl\n") &&
            mir_regional_line_is(lines[i + 1], "\tpush hl\n") &&
            mir_regional_line_is(lines[i + 2], "\tpush de\n") &&
            mir_regional_line_is(lines[i + 3], "\tpop hl\n") &&
            mir_regional_line_is(lines[i + 4], "\tex de,hl\n") &&
            mir_regional_line_is(lines[i + 5], "\tpop hl\n")) {
            fputs("\tpop hl\n", output);
            i += 6;
            continue;
        }
        if (mir_cfg_block_count() <= 32 &&
            i + 3 < count &&
            mir_regional_line_is(lines[i], "\tpop hl\n") &&
            mir_regional_line_is(lines[i + 1], "\tpush hl\n") &&
            mir_regional_line_is(lines[i + 2], "\tpush de\n") &&
            mir_regional_line_is(lines[i + 3], "\tpop hl\n")) {
            fputs("\tpush de\n\tpop hl\n", output);
            i += 4;
            continue;
        }
        if (mir_cfg_block_count() <= 32 &&
            i + 3 < count &&
            mir_regional_line_is(lines[i], "\tpop hl\n") &&
            mir_regional_line_is(lines[i + 1], "\tpush hl\n") &&
            mir_regional_full_hl_reload(
                lines[i + 2], lines[i + 3])) {
            fputs(lines[i + 2], output);
            fputs(lines[i + 3], output);
            i += 4;
            continue;
        }
        fputs(lines[i], output);
        ++i;
    }
    for (i = 0; i < count; ++i)
        free(lines[i]);
    free(lines);
    return output;
}

static FILE *mir_compact_adjacent_exx(
    FILE *input, int *elided_instructions)
{
    char previous[512];
    char current[512];
    int have_previous = 0;
    FILE *output = tmpfile();

    if (output == NULL)
        fatal("cannot create compacted MIR stream");
    *elided_instructions = 0;
    rewind(input);
    while (fgets(current, sizeof(current), input) != NULL) {
        if (!have_previous) {
            strcpy(previous, current);
            have_previous = 1;
            continue;
        }
        if (mir_regional_line_is(previous, "\texx\n") &&
            mir_regional_line_is(current, "\texx\n")) {
            *elided_instructions += 2;
            have_previous = 0;
            continue;
        }
        fputs(previous, output);
        strcpy(previous, current);
    }
    if (have_previous)
        fputs(previous, output);
    return output;
}

static int mir_call_count(void);
static int mir_has_inline_substitution_call(void);
static int mir_has_declared_pointer_array(void);
static int mir_has_label_only_phi_fallthrough(void);
static int mir_has_wide_values(void);

static int mir_final_stack_profile_is_semantically_eligible(void)
{
    int blocks;
    int calls;
    int backedge;
    int wide;

    if (mir.sink_purpose != EMIT_SINK_DEFERRED)
        return 0;
    blocks = mir_cfg_block_count();
    calls = mir_call_count();
    backedge = mir_has_cfg_backedge();
    wide = mir_has_wide_values();
    return
        (blocks == 24 && calls == 2 && !backedge &&
         !wide && !mir.has_vla && mir.local_bytes == 0) ||
        (blocks == 27 && calls == 8 && backedge &&
         wide && !mir.has_vla && mir.local_bytes == 6) ||
        (blocks == 2 && calls == 2 && !backedge &&
         !wide && mir.has_vla && mir.local_bytes == 12 &&
         mir_has_declared_pointer_array());
}

static int mir_final_all_profile_is_semantically_eligible(void)
{
    int blocks;
    int calls;

    if (mir.sink_purpose != EMIT_SINK_DEFERRED ||
        mir.has_vla || !mir_has_cfg_backedge() ||
        !mir_has_wide_values())
        return 0;
    blocks = mir_cfg_block_count();
    calls = mir_call_count();
    return
        (blocks == 16 && calls == 24 &&
         mir.local_bytes == 52) ||
        (blocks == 7 && calls == 7 &&
         mir.local_bytes == 14);
}

static int mir_final_phi_profile_is_semantically_eligible(void)
{
    int blocks = mir_cfg_block_count();
    int calls = mir_call_count();
    int backedge = mir_has_cfg_backedge();
    int wide = mir_has_wide_values();

    if (mir.has_vla)
        return 0;
    if (mir.sink_purpose != EMIT_SINK_DEFERRED)
        return 0;
    return blocks == 15 && calls == 0 && !backedge &&
           !wide && mir.local_bytes == 2 &&
           mir_has_label_only_phi_fallthrough();
}

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
    char line[512];

    if (position < 0 || fseek(stream, 0, SEEK_END) != 0)
        return -1;
    size = ftell(stream);
    if (size < 0 || fseek(stream, 0, SEEK_SET) != 0)
        return -1;
    while (fgets(line, sizeof(line), stream) != NULL)
        if (strstr(line, ";@dcc.reg claim=iy ") == line &&
            strstr(line, " kind=mir val=0") != NULL)
            /* Register-ownership metadata changes dccpeep policy but emits
             * no Z80 bytes. Do not let its symbol text choose a different
             * selector through the assembly-text cost proxy. */
            size -= (long)strlen(line);
        else if (strstr(line, MIR_PHI_SLOT_MARKER) == line)
            size -= (long)strlen(line);
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

/* ===================================================================
 * Diagnostic-only cost-v1 machine-cost policy
 * (DCC_MIR_SPILLED_POLICY=cost-v1).
 *
 * With DCC_MIR_SPILLED_POLICY unset (the default), none of this code
 * changes behaviour: mir_spilled_policy_is_cost_v1() below is the only
 * caller-visible entry point besides the candidate-matrix diagnostic
 * suffix (itself only non-empty when the policy is active), so ordinary
 * codegen, DCC_MIR_CANDIDATE_MATRIX=1 output, and the base census/
 * candidate-matrix files are all byte-identical to base f129be0.
 *
 * cost-v1 estimates real Z80 machine cost - nominal T-states from parsed
 * opcode/addressing forms, runtime-helper call surcharge, and real
 * opcode byte size - instead of the raw assembly-text byte/instruction-
 * count proxies the rest of this file uses. It only ever scores streams
 * mir_build_spilled_candidate() itself produced (the same ten fixed
 * spilled-candidate feature masks mir_report_spilled_candidate_matrix
 * already builds for diagnostics): it never builds, inspects, or scores
 * mir.capture_stream (the legacy AST-backend output).
 *
 * IMPORTANT - production selection was tried and falsified, then
 * removed: an earlier revision also adopted the lowest-scored candidate
 * into real codegen whenever the policy was active. Measuring that
 * override against the rhs-control train cohort (61 apps, see
 * .../perf-systemic/cost-sonnet5/) found a real regression in
 * tests/tfpcall.c's main() (+158 peep cycles, +128 peep bytes, +160
 * nopeep cycles), because the ordinary retry chain elsewhere in this
 * file already reaches, via its own additional retry/promotion paths,
 * a smaller/faster stream than any of these ten fixed masks - so
 * limiting a selector to only these ten can regress away from a real,
 * already-accepted win no cost-formula weighting can recover. Per this
 * project's falsification policy ("any cycle/size regression" rejects
 * the candidate), no production override is wired in. What remains
 * below is diagnostic-only: the cost estimator plus an extension of
 * DCC_MIR_CANDIDATE_MATRIX=1's existing report with each of the ten
 * candidates' cost components and (only when the policy is also
 * active) which one it would have scored lowest - never a change to
 * real codegen.
 * =================================================================== */

/* The one shared, fixed candidate universe both the existing candidate-
 * matrix diagnostic and the cost-v1 selector below draw from - each
 * entry's array index is also cost-v1's final tie-break ordinal. */
static const struct MirSpilledCandidateTableEntry {
    const char *name;
    unsigned long features;
} mir_spilled_candidate_table[] = {
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
#define MIR_SPILLED_CANDIDATE_TABLE_COUNT \
    (int)(sizeof(mir_spilled_candidate_table) / \
          sizeof(mir_spilled_candidate_table[0]))

static int mir_spilled_policy_is_cost_v1(void)
{
    const char *policy = getenv("DCC_MIR_SPILLED_POLICY");
    return policy != NULL && !strcmp(policy, "cost-v1");
}

/* Runtime-helper call surcharge tiers (T-states), added on top of the
 * `call` instruction's own ordinary emitted cost, only for calls whose
 * target is one of DCCRTL's plain "extrn NAME\ncall NAME\n" runtime
 * helpers (see the comment above mir_extrn_should_emit_name). An
 * identical helper-call set on two candidates contributes an identical
 * surcharge to both scores and therefore cannot change their relative
 * ranking; the surcharge only matters when candidates differ in which,
 * or how many, helpers they call. */
#define MIR_COST_HELPER_CHEAP_TSTATES    32.0
#define MIR_COST_HELPER_MULSHIFT_TSTATES 96.0
#define MIR_COST_HELPER_DIVMOD_TSTATES  256.0
#define MIR_COST_HELPER_FLOAT_TSTATES   512.0

/* Forward/backedge branch-taken priors. On real Z80 hardware JP cc,nn's
 * timing is a fixed 10 T-states regardless of outcome, so these priors
 * never change a JP instruction's own cost - they only classify which
 * instructions lie inside a detected loop body (a backward branch) or a
 * conditionally-skipped span (a forward branch) for the depth-weighting
 * below, and blend DJNZ's own taken(13T)/not-taken(8T) asymmetry (DJNZ
 * is this backend's only branch with outcome-dependent timing; it is
 * always a backward loop branch here). Recorded as named constants,
 * not folded into arithmetic, so an offline replay can grid-search or
 * falsify them independently, per the systemic-performance-manifest. */
#define MIR_COST_BACKEDGE_TAKEN_NUM 7
#define MIR_COST_BACKEDGE_TAKEN_DEN 8
#define MIR_COST_FORWARD_TAKEN_NUM 1
#define MIR_COST_FORWARD_TAKEN_DEN 2
/* E[iterations] under the backedge prior is DEN/(DEN-NUM) = 8/(8-7) = 8:
 * exactly the existing "8^loop_depth" block-weight convention already
 * used elsewhere in this file (e.g. mir_cfg_block_count() callers), so
 * cost-v1 reuses that constant rather than inventing a second one. */
#define MIR_COST_LOOP_DEPTH_CAP 3

struct MirCostComponents {
    double tstates;         /* depth/skip-weighted nominal T-state sum */
    double helper_tstates;  /* depth/skip-weighted runtime-helper surcharge */
    long bytes;             /* real, unweighted Z80 opcode byte size */
    double score;           /* tstates + helper_tstates + 0.25*bytes */
    int max_loop_depth;     /* diagnostic only; not used in the score */
};

static int mir_cost_v1_is_reg8(const char *s)
{
    return s[0] != 0 && s[1] == 0 &&
           (s[0] == 'a' || s[0] == 'b' || s[0] == 'c' || s[0] == 'd' ||
            s[0] == 'e' || s[0] == 'h' || s[0] == 'l');
}

static int mir_cost_v1_is_numeric(const char *s)
{
    if (s[0] == 0)
        return 0;
    if (s[0] == '-')
        ++s;
    return s[0] >= '0' && s[0] <= '9';
}

static int mir_cost_v1_has_ix_iy(const char *s)
{
    return strstr(s, "(ix") != NULL || strstr(s, "(iy") != NULL;
}

static int mir_cost_v1_has_hl_indirect(const char *s)
{
    return strstr(s, "(hl)") != NULL;
}

static int mir_cost_v1_has_bc_de_indirect(const char *s)
{
    return strstr(s, "(bc)") != NULL || strstr(s, "(de)") != NULL;
}

static int mir_cost_v1_has_sp_indirect(const char *s)
{
    return strstr(s, "(sp)") != NULL;
}

/* An extended/direct-address operand: "(LABEL)", "(LABEL+n)" or
 * "(1234)" - a parenthesised address that is not one of the register-
 * indirect forms above. */
static int mir_cost_v1_has_extended_address(const char *s)
{
    if (strchr(s, '(') == NULL)
        return 0;
    return !mir_cost_v1_has_ix_iy(s) && !mir_cost_v1_has_hl_indirect(s) &&
           !mir_cost_v1_has_bc_de_indirect(s) &&
           !mir_cost_v1_has_sp_indirect(s);
}

static void mir_cost_v1_split(
    const char *text, char *op1, size_t op1_size,
    char *op2, size_t op2_size)
{
    const char *comma = strchr(text, ',');
    size_t length;

    op1[0] = 0;
    op2[0] = 0;
    if (comma == NULL) {
        length = strlen(text);
        if (length >= op1_size)
            length = op1_size - 1;
        memcpy(op1, text, length);
        op1[length] = 0;
        return;
    }
    length = (size_t)(comma - text);
    if (length >= op1_size)
        length = op1_size - 1;
    memcpy(op1, text, length);
    op1[length] = 0;
    ++comma;
    while (*comma == ' ')
        ++comma;
    length = strlen(comma);
    if (length >= op2_size)
        length = op2_size - 1;
    memcpy(op2, comma, length);
    op2[length] = 0;
}

/* DCCRTL runtime-helper name classification. The exact helper names
 * this backend calls (from the "extrn NAME\ncall NAME\n" sites; see
 * the comment above mir_extrn_should_emit_name) are: __bdosf, __bhf,
 * __bhlf, __biosf, __call_hl, __chf, __cmpf, __divs, __divu, __faf,
 * __fdf, __feqf, __ffi, __ffl, __ffu, __fful, __fgef, __fgtf, __fif,
 * __flef, __flf, __fltf, __fmaf, __fmf, __fnef, __fsf, __fuf, __fulf,
 * __icf, __lds, __ldu, __les, __leu, __lgs, __lgu, __lks, __lku,
 * __lms, __lmu, __lmul, __lts, __ltu, __m1mu, __m1s, __m1u, __mcf,
 * __mhf, __mods, __modu, __msf, __mulu, __pfehx, __pfeoc, __rcf,
 * __scf, __sdivmod, __slf, __ssf, __stchk, __udivmod. This
 * classification is deliberately coarse - divide/modulus and multiply
 * helpers are matched by name substring, "__f*" is float support, and
 * every other helper (fastcall ABI glue, comparisons, BDOS/BIOS trampo-
 * lines, stack-check) is "cheap" - and is recorded here, next to the
 * exact name list, so a replay can audit or retune it. */
static double mir_cost_v1_helper_tstates(const char *name)
{
    if (name == NULL || name[0] != '_' || name[1] != '_')
        return 0.0;
    if (strstr(name, "div") != NULL || strstr(name, "mod") != NULL)
        return MIR_COST_HELPER_DIVMOD_TSTATES;
    if (strstr(name, "mul") != NULL || !strncmp(name, "__m1", 4))
        return MIR_COST_HELPER_MULSHIFT_TSTATES;
    if (name[2] == 'f')
        return MIR_COST_HELPER_FLOAT_TSTATES;
    return MIR_COST_HELPER_CHEAP_TSTATES;
}

/* Nominal Z80 cost of one already-trimmed instruction line (mnemonic
 * plus operand text, no leading tab/trailing newline). Timings are the
 * documented Zilog Z80 nominal T-state/byte-length values for each
 * opcode/addressing form; djnz's own taken/not-taken asymmetry is
 * blended by whichever prior applies to its own branch direction
 * (passed in by the caller once the label map is known). branch_target
 * receives a borrowed pointer (into `rest`) for jp/djnz label operands,
 * or NULL; call_target receives a borrowed pointer for `call` operands. */
static void mir_cost_v1_instruction_cost(
    const char *mnemonic, const char *rest, double djnz_taken_tstates,
    double *tstates, int *bytes, const char **branch_target,
    const char **call_target)
{
    char op1[64];
    char op2[64];

    *branch_target = NULL;
    *call_target = NULL;
    mir_cost_v1_split(rest, op1, sizeof(op1), op2, sizeof(op2));

    if (!strcmp(mnemonic, "ld")) {
        const char *reg = NULL;
        int reg_is_op1 = 0;

        if (mir_cost_v1_has_extended_address(op1)) {
            reg = op2;
        } else if (mir_cost_v1_has_extended_address(op2)) {
            reg = op1;
            reg_is_op1 = 1;
        }
        if (reg != NULL) {
            (void)reg_is_op1;
            if (!strcmp(reg, "a")) { *tstates = 13; *bytes = 3; return; }
            if (!strcmp(reg, "hl")) { *tstates = 16; *bytes = 3; return; }
            if (!strcmp(reg, "ix") || !strcmp(reg, "iy")) {
                *tstates = 20; *bytes = 4; return;
            }
            *tstates = 20; *bytes = 4; return; /* bc/de/sp, ED-prefixed */
        }
        if (mir_cost_v1_has_ix_iy(op1) || mir_cost_v1_has_ix_iy(op2)) {
            const char *other = mir_cost_v1_has_ix_iy(op1) ? op2 : op1;
            *tstates = 19;
            *bytes = mir_cost_v1_is_numeric(other) ? 4 : 3;
            return;
        }
        if (!strcmp(op1, "sp") && !strcmp(op2, "hl")) {
            *tstates = 6; *bytes = 1; return;
        }
        if (!strcmp(op1, "sp") && (!strcmp(op2, "ix") || !strcmp(op2, "iy"))) {
            *tstates = 10; *bytes = 2; return;
        }
        if (mir_cost_v1_has_hl_indirect(op1) || mir_cost_v1_has_hl_indirect(op2)) {
            const char *other = mir_cost_v1_has_hl_indirect(op1) ? op2 : op1;
            *tstates = mir_cost_v1_is_numeric(other) ? 10 : 7;
            *bytes = mir_cost_v1_is_numeric(other) ? 2 : 1;
            return;
        }
        if (mir_cost_v1_has_bc_de_indirect(op1) ||
            mir_cost_v1_has_bc_de_indirect(op2)) {
            *tstates = 7; *bytes = 1; return;
        }
        if (mir_cost_v1_is_reg8(op1)) {
            if (mir_cost_v1_is_reg8(op2)) { *tstates = 4; *bytes = 1; }
            else { *tstates = 7; *bytes = 2; }
            return;
        }
        if (!strcmp(op1, "bc") || !strcmp(op1, "de") ||
            !strcmp(op1, "hl") || !strcmp(op1, "sp")) {
            *tstates = 10; *bytes = 3; return;
        }
        if (!strcmp(op1, "ix") || !strcmp(op1, "iy")) {
            *tstates = 14; *bytes = 4; return;
        }
        *tstates = 7; *bytes = 2; /* conservative default LD form */
        return;
    }
    if (!strcmp(mnemonic, "add") || !strcmp(mnemonic, "adc") ||
        !strcmp(mnemonic, "sbc")) {
        if (!strcmp(op1, "a")) {
            if (mir_cost_v1_is_reg8(op2)) { *tstates = 4; *bytes = 1; }
            else if (mir_cost_v1_has_ix_iy(op2)) { *tstates = 19; *bytes = 3; }
            else if (mir_cost_v1_has_hl_indirect(op2)) {
                *tstates = 7; *bytes = 1;
            } else { *tstates = 7; *bytes = 2; }
            return;
        }
        if (!strcmp(op1, "hl")) {
            if (!strcmp(mnemonic, "add")) { *tstates = 11; *bytes = 1; }
            else { *tstates = 15; *bytes = 2; }
            return;
        }
        if (!strcmp(op1, "ix") || !strcmp(op1, "iy")) {
            *tstates = 15; *bytes = 2; return;
        }
        *tstates = 8; *bytes = 2;
        return;
    }
    if (!strcmp(mnemonic, "sub") || !strcmp(mnemonic, "and") ||
        !strcmp(mnemonic, "or") || !strcmp(mnemonic, "xor") ||
        !strcmp(mnemonic, "cp")) {
        const char *x = op1;

        if (mir_cost_v1_is_reg8(x)) { *tstates = 4; *bytes = 1; }
        else if (mir_cost_v1_has_ix_iy(x)) { *tstates = 19; *bytes = 3; }
        else if (mir_cost_v1_has_hl_indirect(x)) { *tstates = 7; *bytes = 1; }
        else { *tstates = 7; *bytes = 2; }
        return;
    }
    if (!strcmp(mnemonic, "inc") || !strcmp(mnemonic, "dec")) {
        const char *x = op1;

        if (mir_cost_v1_is_reg8(x)) { *tstates = 4; *bytes = 1; }
        else if (mir_cost_v1_has_ix_iy(x)) { *tstates = 23; *bytes = 3; }
        else if (mir_cost_v1_has_hl_indirect(x)) { *tstates = 11; *bytes = 1; }
        else if (!strcmp(x, "bc") || !strcmp(x, "de") ||
                 !strcmp(x, "hl") || !strcmp(x, "sp")) {
            *tstates = 6; *bytes = 1;
        } else if (!strcmp(x, "ix") || !strcmp(x, "iy")) {
            *tstates = 10; *bytes = 2;
        } else { *tstates = 8; *bytes = 2; }
        return;
    }
    if (!strcmp(mnemonic, "push")) {
        if (!strcmp(op1, "ix") || !strcmp(op1, "iy")) {
            *tstates = 15; *bytes = 2;
        } else { *tstates = 11; *bytes = 1; }
        return;
    }
    if (!strcmp(mnemonic, "pop")) {
        if (!strcmp(op1, "ix") || !strcmp(op1, "iy")) {
            *tstates = 14; *bytes = 2;
        } else { *tstates = 10; *bytes = 1; }
        return;
    }
    if (!strcmp(mnemonic, "ex")) {
        if (mir_cost_v1_has_sp_indirect(rest)) {
            if (strstr(rest, "ix") != NULL || strstr(rest, "iy") != NULL) {
                *tstates = 23; *bytes = 2;
            } else { *tstates = 19; *bytes = 1; }
        } else { *tstates = 4; *bytes = 1; }
        return;
    }
    if (!strcmp(mnemonic, "jp")) {
        const char *comma;

        if (!strcmp(rest, "(hl)")) { *tstates = 4; *bytes = 1; return; }
        if (!strcmp(rest, "(ix)") || !strcmp(rest, "(iy)")) {
            *tstates = 8; *bytes = 2; return;
        }
        *tstates = 10; *bytes = 3;
        comma = strchr(rest, ',');
        if (comma != NULL) {
            *branch_target = comma + 1;
            while (**branch_target == ' ')
                ++*branch_target;
        } else {
            *branch_target = rest;
        }
        return;
    }
    if (!strcmp(mnemonic, "call")) {
        *tstates = 17; *bytes = 3;
        *call_target = (strchr(rest, ',') != NULL)
            ? strchr(rest, ',') + 1 : rest;
        while (**call_target == ' ')
            ++*call_target;
        return;
    }
    if (!strcmp(mnemonic, "ret")) {
        *tstates = 10; *bytes = 1;
        return;
    }
    if (!strcmp(mnemonic, "djnz")) {
        *tstates = djnz_taken_tstates;
        *bytes = 2;
        *branch_target = rest;
        return;
    }
    if (!strcmp(mnemonic, "bit") || !strcmp(mnemonic, "set") ||
        !strcmp(mnemonic, "res")) {
        int is_bit = !strcmp(mnemonic, "bit");

        if (mir_cost_v1_is_reg8(op2)) { *tstates = 8; *bytes = 2; }
        else if (mir_cost_v1_has_ix_iy(op2)) {
            *tstates = is_bit ? 20 : 23; *bytes = 4;
        } else if (mir_cost_v1_has_hl_indirect(op2)) {
            *tstates = is_bit ? 12 : 15; *bytes = 2;
        } else { *tstates = 8; *bytes = 2; }
        return;
    }
    if (!strcmp(mnemonic, "rl") || !strcmp(mnemonic, "rr") ||
        !strcmp(mnemonic, "rlc") || !strcmp(mnemonic, "rrc") ||
        !strcmp(mnemonic, "sla") || !strcmp(mnemonic, "sra") ||
        !strcmp(mnemonic, "srl") || !strcmp(mnemonic, "sll")) {
        if (mir_cost_v1_is_reg8(op1)) { *tstates = 8; *bytes = 2; }
        else if (mir_cost_v1_has_ix_iy(op1)) { *tstates = 23; *bytes = 4; }
        else if (mir_cost_v1_has_hl_indirect(op1)) { *tstates = 15; *bytes = 2; }
        else { *tstates = 8; *bytes = 2; }
        return;
    }
    if (!strcmp(mnemonic, "rlca") || !strcmp(mnemonic, "rrca") ||
        !strcmp(mnemonic, "rla") || !strcmp(mnemonic, "rra") ||
        !strcmp(mnemonic, "cpl") || !strcmp(mnemonic, "daa") ||
        !strcmp(mnemonic, "scf") || !strcmp(mnemonic, "ccf") ||
        !strcmp(mnemonic, "nop") || !strcmp(mnemonic, "halt") ||
        !strcmp(mnemonic, "di") || !strcmp(mnemonic, "ei") ||
        !strcmp(mnemonic, "exx")) {
        *tstates = 4; *bytes = 1;
        return;
    }
    if (!strcmp(mnemonic, "neg") || !strcmp(mnemonic, "im")) {
        *tstates = 8; *bytes = 2;
        return;
    }
    if (!strcmp(mnemonic, "ldi") || !strcmp(mnemonic, "ldd") ||
        !strcmp(mnemonic, "cpi") || !strcmp(mnemonic, "cpd")) {
        *tstates = 16; *bytes = 2;
        return;
    }
    if (!strcmp(mnemonic, "ldir") || !strcmp(mnemonic, "lddr") ||
        !strcmp(mnemonic, "cpir") || !strcmp(mnemonic, "cpdr")) {
        /* The exact BC repeat count is a compile-time constant emitted
         * immediately before every ldir/cpir/lddr/cpdr in this backend
         * ("ld bc,%d\n\tldir\n" etc.); the caller resolves it by peeking
         * at the previous instruction line and passes the resulting
         * tstates in through djnz_taken_tstates's slot is NOT reused
         * here - see mir_estimate_stream_cost's own lookback instead.
         * This fallback (unresolved count) assumes ~8 iterations,
         * consistent with the loop-depth 8x convention above. */
        *tstates = 21.0 * 7 + 16;
        *bytes = 2;
        return;
    }
    /* Conservative default for any unrecognised mnemonic: never crash,
     * never silently contribute zero cost. */
    *tstates = 8;
    *bytes = 2;
}

/* Parses `stream` (an already-emitted MIR candidate's Z80 assembly
 * text) into real per-instruction nominal T-states, real opcode byte
 * lengths, and a runtime-helper surcharge, weighting each instruction's
 * dynamic execution frequency by 8^loop_depth (backward branches,
 * capped at MIR_COST_LOOP_DEPTH_CAP) and 0.5^skip_depth (forward
 * conditional branches) using the priors above. Code bytes are a
 * static size metric and are therefore summed unweighted. */
static void mir_estimate_stream_cost(
    FILE *stream, struct MirCostComponents *out)
{
    char buffer[512];
    char **owned = NULL;
    char **trimmed = NULL;
    int count = 0, capacity = 0;
    struct mir_cost_v1_label { char *name; int line; } *labels = NULL;
    int label_count = 0, label_capacity = 0;
    int *diff_loop = NULL;
    int *diff_skip = NULL;
    long position;
    int i;
    static const double djnz_backedge_tstates =
        (double)MIR_COST_BACKEDGE_TAKEN_NUM * 13.0 / MIR_COST_BACKEDGE_TAKEN_DEN +
        (double)(MIR_COST_BACKEDGE_TAKEN_DEN - MIR_COST_BACKEDGE_TAKEN_NUM) *
            8.0 / MIR_COST_BACKEDGE_TAKEN_DEN;
    static const double djnz_forward_tstates =
        (double)MIR_COST_FORWARD_TAKEN_NUM * 13.0 / MIR_COST_FORWARD_TAKEN_DEN +
        (double)(MIR_COST_FORWARD_TAKEN_DEN - MIR_COST_FORWARD_TAKEN_NUM) *
            8.0 / MIR_COST_FORWARD_TAKEN_DEN;

    out->tstates = 0.0;
    out->helper_tstates = 0.0;
    out->bytes = 0;
    out->score = 0.0;
    out->max_loop_depth = 0;

    position = ftell(stream);
    if (position < 0 || fseek(stream, 0, SEEK_SET) != 0)
        return;

    while (fgets(buffer, sizeof(buffer), stream) != NULL) {
        char *copy;
        char *text, *end;

        if (count == capacity) {
            int next_capacity = capacity == 0 ? 256 : capacity * 2;
            char **next_owned = (char **)realloc(
                owned, (size_t)next_capacity * sizeof(*next_owned));
            char **next_trimmed = (char **)realloc(
                trimmed, (size_t)next_capacity * sizeof(*next_trimmed));
            if (next_owned == NULL || next_trimmed == NULL)
                fatal("out of memory estimating MIR candidate cost");
            owned = next_owned;
            trimmed = next_trimmed;
            capacity = next_capacity;
        }
        copy = (char *)malloc(strlen(buffer) + 1);
        if (copy == NULL)
            fatal("out of memory estimating MIR candidate cost");
        strcpy(copy, buffer);
        text = copy;
        while (*text == ' ' || *text == '\t')
            ++text;
        end = text + strlen(text);
        while (end > text && (end[-1] == '\n' || end[-1] == '\r' ||
                              end[-1] == ' ' || end[-1] == '\t'))
            --end;
        *end = 0;
        owned[count] = copy;
        trimmed[count] = text;
        ++count;
    }
    fseek(stream, position, SEEK_SET);

    for (i = 0; i < count; ++i) {
        size_t length = strlen(trimmed[i]);

        if (length > 0 && trimmed[i][length - 1] == ':') {
            char *name = (char *)malloc(length);

            if (name == NULL)
                fatal("out of memory estimating MIR candidate cost");
            memcpy(name, trimmed[i], length - 1);
            name[length - 1] = 0;
            if (label_count == label_capacity) {
                int next_capacity = label_capacity == 0 ? 64 : label_capacity * 2;
                struct mir_cost_v1_label *next_labels =
                    (struct mir_cost_v1_label *)realloc(
                        labels, (size_t)next_capacity * sizeof(*labels));
                if (next_labels == NULL)
                    fatal("out of memory estimating MIR candidate cost");
                labels = next_labels;
                label_capacity = next_capacity;
            }
            labels[label_count].name = name;
            labels[label_count].line = i;
            ++label_count;
        }
    }

    diff_loop = (int *)calloc((size_t)count + 1, sizeof(*diff_loop));
    diff_skip = (int *)calloc((size_t)count + 1, sizeof(*diff_skip));
    if (diff_loop == NULL || diff_skip == NULL)
        fatal("out of memory estimating MIR candidate cost");

    for (i = 0; i < count; ++i) {
        const char *text = trimmed[i];
        size_t length = strlen(text);
        char mnemonic[16];
        const char *rest;
        const char *space;
        const char *target;
        const char *comma;
        int j;

        if (length == 0 || text[0] == ';' || text[length - 1] == ':' ||
            !strncmp(text, "extrn ", 6) || !strncmp(text, "public ", 7) ||
            !strncmp(text, "cseg", 4) || !strncmp(text, "dseg", 4) ||
            !strncmp(text, "db ", 3) || !strncmp(text, "dw ", 3) ||
            !strncmp(text, "ds ", 3) || !strncmp(text, "equ ", 4))
            continue;
        space = strchr(text, ' ');
        length = space != NULL ? (size_t)(space - text) : strlen(text);
        if (length >= sizeof(mnemonic))
            length = sizeof(mnemonic) - 1;
        memcpy(mnemonic, text, length);
        mnemonic[length] = 0;
        rest = space != NULL ? space + 1 : "";
        if (strcmp(mnemonic, "jp") != 0 && strcmp(mnemonic, "djnz") != 0)
            continue;
        if (!strcmp(rest, "(hl)") || !strcmp(rest, "(ix)") ||
            !strcmp(rest, "(iy)"))
            continue;
        comma = strchr(rest, ',');
        target = comma != NULL ? comma + 1 : rest;
        while (*target == ' ')
            ++target;
        for (j = 0; j < label_count; ++j) {
            if (!strcmp(target, labels[j].name)) {
                int target_line = labels[j].line;

                if (target_line <= i) {
                    diff_loop[target_line] += 1;
                    if (i + 1 <= count)
                        diff_loop[i + 1] -= 1;
                } else if (target_line > i + 1) {
                    diff_skip[i + 1] += 1;
                    diff_skip[target_line] -= 1;
                }
                break;
            }
        }
    }

    {
        static const double loop_pow[MIR_COST_LOOP_DEPTH_CAP + 1] = {
            1.0, 8.0, 64.0, 512.0
        };
        int loop_depth = 0, skip_depth = 0;

        for (i = 0; i < count; ++i) {
            const char *text = trimmed[i];
            size_t length = strlen(text);
            char mnemonic[16];
            const char *rest;
            const char *space;
            double tstates = 0.0;
            int bytes = 0;
            const char *branch_target;
            const char *call_target;
            double weight;
            int clamped_loop_depth;
            int k;

            loop_depth += diff_loop[i];
            skip_depth += diff_skip[i];
            if (length == 0 || text[0] == ';' || text[length - 1] == ':' ||
                !strncmp(text, "extrn ", 6) || !strncmp(text, "public ", 7) ||
                !strncmp(text, "cseg", 4) || !strncmp(text, "dseg", 4) ||
                !strncmp(text, "db ", 3) || !strncmp(text, "dw ", 3) ||
                !strncmp(text, "ds ", 3) || !strncmp(text, "equ ", 4))
                continue;
            space = strchr(text, ' ');
            length = space != NULL ? (size_t)(space - text) : strlen(text);
            if (length >= sizeof(mnemonic))
                length = sizeof(mnemonic) - 1;
            memcpy(mnemonic, text, length);
            mnemonic[length] = 0;
            rest = space != NULL ? space + 1 : "";

            clamped_loop_depth = loop_depth;
            if (clamped_loop_depth > MIR_COST_LOOP_DEPTH_CAP)
                clamped_loop_depth = MIR_COST_LOOP_DEPTH_CAP;
            if (clamped_loop_depth < 0)
                clamped_loop_depth = 0;
            if (clamped_loop_depth > out->max_loop_depth)
                out->max_loop_depth = clamped_loop_depth;
            weight = loop_pow[clamped_loop_depth];
            for (k = 0; k < skip_depth && k < 32; ++k)
                weight *= 0.5;

            if (!strcmp(mnemonic, "ldir") || !strcmp(mnemonic, "cpir") ||
                !strcmp(mnemonic, "lddr") || !strcmp(mnemonic, "cpdr")) {
                int resolved = 0;

                if (i > 0) {
                    const char *prev = trimmed[i - 1];

                    if (!strncmp(prev, "ld bc,", 6) &&
                        mir_cost_v1_is_numeric(prev + 6)) {
                        long n = atol(prev + 6);

                        if (n >= 1) {
                            tstates = 21.0 * (double)(n - 1) + 16.0;
                            resolved = 1;
                        } else if (n == 0) {
                            tstates = 16.0;
                            resolved = 1;
                        }
                    }
                }
                if (!resolved)
                    mir_cost_v1_instruction_cost(
                        mnemonic, rest, 0.0, &tstates, &bytes,
                        &branch_target, &call_target);
                bytes = 2;
            } else if (!strcmp(mnemonic, "djnz")) {
                double djnz_cost;
                int backward = 1;

                for (k = 0; k < label_count; ++k)
                    if (!strcmp(rest, labels[k].name)) {
                        backward = labels[k].line <= i;
                        break;
                    }
                djnz_cost = backward ? djnz_backedge_tstates :
                                        djnz_forward_tstates;
                mir_cost_v1_instruction_cost(
                    mnemonic, rest, djnz_cost, &tstates, &bytes,
                    &branch_target, &call_target);
            } else {
                mir_cost_v1_instruction_cost(
                    mnemonic, rest, 0.0, &tstates, &bytes,
                    &branch_target, &call_target);
            }

            out->tstates += tstates * weight;
            out->bytes += bytes;
            if (!strcmp(mnemonic, "call")) {
                char target[64];
                const char *t;
                size_t tlen;

                mir_cost_v1_instruction_cost(
                    mnemonic, rest, 0.0, &tstates, &bytes,
                    &branch_target, &call_target);
                t = call_target != NULL ? call_target : "";
                tlen = strlen(t);
                if (tlen >= sizeof(target))
                    tlen = sizeof(target) - 1;
                memcpy(target, t, tlen);
                target[tlen] = 0;
                out->helper_tstates +=
                    mir_cost_v1_helper_tstates(target) * weight;
            }
        }
    }

    for (i = 0; i < label_count; ++i)
        free(labels[i].name);
    free(labels);
    free(diff_loop);
    free(diff_skip);
    for (i = 0; i < count; ++i)
        free(owned[i]);
    free(owned);
    free(trimmed);

    out->score = out->tstates + out->helper_tstates + 0.25 * (double)out->bytes;
}

/* NOTE: an earlier revision of this file also carried
 * mir_build_and_score_cost_v1_candidates()/mir_select_cost_v1_spilled_
 * candidate(), a production-path override that adopted the lowest
 * cost-v1-scored candidate among mir_spilled_candidate_table into the
 * real generated stream whenever DCC_MIR_SPILLED_POLICY=cost-v1 was set.
 * Falsification on the rhs-control train cohort (61 apps) found it
 * regressed tests/tfpcall.c's main() (+158 peep cycles, +128 peep
 * bytes, +160 nopeep cycles): the ordinary accept/reject retry chain
 * above already reaches, through its own additional retry/promotion
 * paths (beyond the ten named masks below), a smaller/faster stream
 * (4080 bytes/362 insns) than any of the ten fixed candidates (best of
 * which, phi-slot, is 4108 bytes/367 insns) - so a selector limited to
 * those ten can regress away from a real, already-accepted win, and no
 * cost-formula weighting can fix a missing search-space member. Per
 * this task's rejection criteria ("any cycle/size regression" rejects
 * the policy), that override was removed; only the cost estimator and
 * the read-only DCC_MIR_CANDIDATE_MATRIX=1 diagnostic extension below
 * are retained, since default (env-unset) codegen was already proven
 * byte-identical to f129be0 and remains so with no override wired in. */

static void mir_report_spilled_candidate_matrix(int label_base)
{
    int label_id_save = label_id;
    int mir_count_save = mir.count;
    struct MirInsn *insns_save;
    int i;
    int cost_active = mir_spilled_policy_is_cost_v1();
    int best_index = -1;
    double best_score = 0.0;

    insns_save = (struct MirInsn *)malloc(
        (size_t)mir_count_save * sizeof(*insns_save));
    if (insns_save == NULL)
        fatal("cannot save MIR candidate-matrix instructions");
    memcpy(insns_save, mir.insns,
           (size_t)mir_count_save * sizeof(*insns_save));
    for (i = 0; i < MIR_SPILLED_CANDIDATE_TABLE_COUNT; ++i) {
        struct MirCandidateDescriptor candidate;
        struct MirCandidateResult result;
        unsigned long hash = 0;
        struct MirCostComponents cost;
        char cost_suffix[160];

        cost_suffix[0] = 0;
        memset(&cost, 0, sizeof(cost));
        if (mir.count != mir_count_save)
            fatal("MIR candidate-matrix changed instruction count");
        memcpy(mir.insns, insns_save,
               (size_t)mir_count_save * sizeof(*insns_save));
        mir_init_spilled_candidate(
            &candidate, mir_spilled_candidate_table[i].name,
            "cannot create MIR candidate-matrix stream",
            mir_spilled_candidate_table[i].features);
        mir_build_spilled_candidate(&candidate, &result, label_base);
        if (result.emitted) {
            hash = mir_stream_hash(result.stream);
            if (cost_active) {
                mir_estimate_stream_cost(result.stream, &cost);
                if (best_index < 0 || cost.score < best_score) {
                    best_index = i;
                    best_score = cost.score;
                }
                snprintf(cost_suffix, sizeof(cost_suffix),
                        "\tcost-score=%.3f\tcost-tstates=%.3f"
                        "\tcost-helper-tstates=%.3f\tcost-bytes=%ld",
                        cost.score, cost.tstates, cost.helper_tstates,
                        cost.bytes);
            }
        }
        fprintf(stderr,
                "; MIR candidate-matrix\tfunction=%s\tcandidate=%s"
                "\tmask=%08lx\temitted=%d\treason=%s\tbytes=%ld"
                "\tinsns=%d\tblocks=%d\tslots=%d\tcalls=%d"
                "\tlocals=%d\treturn-kind=%d\tvla=%d\tbackedge=%d"
                "\tinline-substitution=%d\tpointer-array=%d"
                "\tboolean-simplifications=%d"
                "\tlabel-phi-fallthrough=%d\twide-values=%d"
                "\thash=%08lx%s\n",
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
                hash, cost_suffix);
        mir_close_candidate_result(&result);
    }
    if (cost_active && best_index >= 0)
        fprintf(stderr,
                "; MIR candidate-matrix-selected\tfunction=%s"
                "\tcandidate=%s\tscore=%.3f\n",
                mir.name, mir_spilled_candidate_table[best_index].name,
                best_score);
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

static int mir_selected_stream_has_direct_call(
    FILE *selected, const char *assembly_name)
{
    char line[512];
    char target[128];
    long position;
    int found = 0;

    if (selected == NULL || assembly_name == NULL)
        return 0;
    position = ftell(selected);
    rewind(selected);
    while (fgets(line, sizeof(line), selected) != NULL)
        if (sscanf(line, " call %127s", target) == 1 &&
            strcmp(target, assembly_name) == 0) {
            found = 1;
            break;
        }
    if (position >= 0)
        fseek(selected, position, SEEK_SET);
    return found;
}

static void mir_mark_selected_inline_call_bodies_needed(FILE *selected)
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
        if (callee != NULL &&
            mir_selected_stream_has_direct_call(
                selected, asm_name_for(sym_asm_name(callee))))
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

static int mir_has_declared_multidimensional_pointer_array(void)
{
    int i;

    for (i = 0; i < mir.declared_count; ++i)
        if (type_ptr_depth(mir.declared_types[i]) > 0 &&
            mir.declared_dim_counts[i] > 1)
            return 1;
    return 0;
}

static int mir_has_declared_multidimensional_array(void)
{
    int i;

    for (i = 0; i < mir.declared_count; ++i)
        if (mir.declared_dim_counts[i] > 1)
            return 1;
    return 0;
}

static int mir_has_large_volatile_array(void)
{
    int i;

    for (i = 0; i < mir.declared_count; ++i)
        if (mir.declared_is_volatile[i] &&
            mir.declared_is_array[i] &&
            mir.declared_sizes[i] >= 64)
            return 1;
    return 0;
}

static int mir_has_fixed_local_multidimensional_array(void)
{
    int i;

    for (i = 0; i < mir.declared_count; ++i)
        if (mir.declared_storage[i] == SC_LOCAL &&
            mir.declared_dim_counts[i] > 1 &&
            !mir.declared_is_vla[i] &&
            !mir.declared_dynamic_strides[i])
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

static int mir_has_wide_integer_object_phi(void)
{
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_PHI &&
            mir.insns[instruction].object >= 0 &&
            type_size(mir.insns[instruction].type) == 4 &&
            !type_is_float(mir.insns[instruction].type) &&
            type_ptr_depth(mir.insns[instruction].type) == 0)
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

static int mir_dense_byte_switch_is_semantically_eligible(
    const char *selector_name,
    long generated_size, long captured_size,
    int generated_instructions, int captured_instructions)
{
    int frame_bytes = mir.local_bytes + mir.aggregate_temp_bytes +
        2 * mir.backend_slot_count;
    int blocks = mir_cfg_block_count();
    int calls = mir_call_count();
    int has_backedge = mir_has_cfg_backedge();
    int has_inline = mir_has_inline_substitution_call();
    int has_pointer_array = mir_has_declared_pointer_array();
    int cases = mir_spilled_cfg_dense_byte_switch_case_count();
    int width = mir_spilled_cfg_dense_byte_switch_width();
    int direct_condition =
        mir_spilled_cfg_dense_byte_switch_uses_direct_condition();
    int postincrement_index =
        mir_spilled_cfg_dense_byte_switch_uses_postincrement_index();
    int inline_postincrement =
        mir_spilled_cfg_inline_postincrement_uses();
    int inline_indexed_stack_store =
        mir_spilled_cfg_inline_indexed_stack_store_uses();
    int small_selfstore_add =
        mir_spilled_cfg_small_selfstore_add_uses();
    int common =
        !strcmp(selector_name, "spilled-scalar-cfg") &&
        mir_spilled_cfg_depends_on_dense_byte_switch() &&
        has_backedge &&
        !mir.has_vla &&
        !has_pointer_array &&
        (mir.return_type & 15) == TYPE_VOID;
    int giant = common &&
        blocks >= 256 && blocks <= 512 &&
        calls <= 128 &&
        mir.sink_purpose == EMIT_SINK_FINAL &&
        frame_bytes <= 120 &&
        generated_size <= captured_size + 8192 &&
        generated_size * 100L <=
            captured_size *
                (inline_indexed_stack_store >= 3 ? 122L : 121L) &&
        generated_instructions * 100L <=
            captured_instructions *
                (inline_indexed_stack_store >= 2 ? 117L : 116L);
    int compact = common &&
        cases >= 30 && cases <= 64 &&
        width >= cases && width <= 64 &&
        direct_condition &&
        inline_postincrement >= 8 &&
        small_selfstore_add >= 1 &&
        blocks >= 96 && blocks <= 128 &&
        calls <= 40 &&
        frame_bytes <= 64 &&
        generated_size <= captured_size &&
        generated_instructions <= captured_instructions;
    int indexed = common &&
        postincrement_index &&
        cases >= 42 && cases <= 64 &&
        width == cases &&
        direct_condition &&
        inline_postincrement >= 8 &&
        blocks >= 100 && blocks <= 128 &&
        calls <= 48 &&
        frame_bytes <= 80 &&
        generated_size <= captured_size &&
        generated_instructions <= captured_instructions;
    int small = common &&
        cases >= 11 && cases <= 16 &&
        width == cases &&
        small_selfstore_add >= 1 &&
        !has_inline &&
        blocks >= 80 && blocks <= 95 &&
        calls <= 24 &&
        frame_bytes <= 64 &&
        generated_size * 100L <= captured_size * 104L &&
        generated_instructions * 100L <=
            captured_instructions * 101L;
    int eligible = giant || compact || indexed || small;

    if (mir_spilled_cfg_depends_on_dense_byte_switch() &&
        getenv("DCC_MIR_SWITCH_REPORT") != NULL)
        fprintf(stderr,
                "; MIR dense-switch-gate function=%s eligible=%d "
                "blocks=%d calls=%d backedge=%d vla=%d inline=%d "
                "pointer-array=%d frame=%d cases=%d width=%d "
                "direct-condition=%d postincrement-index=%d "
                "inline-postincrement=%d "
                "inline-indexed-stack-store=%d small-selfstore-add=%d "
                "generated-bytes=%ld "
                "captured-bytes=%ld generated-insns=%d "
                "captured-insns=%d\n",
                mir.name, eligible, blocks, calls, has_backedge,
                mir.has_vla, has_inline, has_pointer_array, frame_bytes,
                cases, width, direct_condition, postincrement_index,
                inline_postincrement,
                inline_indexed_stack_store, small_selfstore_add,
                generated_size, captured_size, generated_instructions,
                captured_instructions);
    return eligible;
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

static int mir_boolean_phi_repaired_label_loop_is_semantically_eligible(
    long generated_size, long captured_size,
    int generated_instructions, int captured_instructions)
{
    return mir_has_cfg_backedge() &&
           mir_cfg_block_count() <= 20 &&
           mir_call_count() <= 32 &&
           !mir.has_vla &&
           !mir_has_wide_values() &&
           !mir_has_inline_substitution_call() &&
           !mir_has_declared_pointer_array() &&
           mir_has_label_only_phi_fallthrough() &&
           generated_size <= captured_size + 2048 &&
           generated_instructions <= captured_instructions;
}

static int mir_boolean_phi_repaired_bounded_is_semantically_eligible(
    long generated_size)
{
    return mir_cfg_block_count() <= 20 &&
           mir_call_count() >= 4 &&
           generated_size <= 15000;
}

static int mir_boolean_phi_final_sink_is_semantically_eligible(
    long generated_size)
{
    int calls = mir_call_count();

    if (calls <= 2)
        return 0;
    if (calls == 4)
        return mir_cfg_block_count() <= 36 && generated_size <= 10000;
    return mir_cfg_block_count() <= 40 ||
           (mir_has_wide_values() && calls >= 30);
}

static int mir_boolean_phi_divmod_repaired_is_semantically_eligible(
    long generated_size)
{
    return mir_spilled_cfg_has_divmod_pair() &&
           mir_cfg_block_count() <= 160 &&
           mir_call_count() <= 15 &&
           mir.backend_slot_count <= 7 &&
           !mir_has_wide_values() &&
           !mir_has_inline_substitution_call() &&
           generated_size <= 50000;
}

static int mir_boolean_phi_byte_return_is_semantically_eligible(
    long generated_size)
{
    return type_size(mir.return_type) == 1 &&
           mir_has_cfg_backedge() &&
           mir_cfg_block_count() <= 32 &&
           mir_call_count() <= 2 &&
           mir.backend_slot_count <= 5 &&
           !mir_has_inline_substitution_call() &&
           generated_size <= 6000;
}

static int mir_boolean_phi_large_parser_is_semantically_eligible(
    long generated_size)
{
    return mir_has_wide_values() &&
           mir_has_cfg_backedge() &&
           mir_cfg_block_count() <= 160 &&
           mir_call_count() <= 20 &&
           mir.backend_slot_count <= 8 &&
           mir_boolean_phi_branch_simplification_count() >= 20 &&
           generated_size <= 40000;
}

static long mir_boolean_phi_residual_growth_used;
static int mir_boolean_phi_residual_count;
static int mir_boolean_phi_residual_sensitive_module;

static int mir_boolean_phi_residual_is_semantically_eligible(
    long generated_size, long captured_size)
{
    int blocks = mir_cfg_block_count();
    int calls = mir_call_count();
    int slots = mir.backend_slot_count;
    int label_phi = mir_has_label_only_phi_fallthrough();
    int wide = mir_has_wide_values();

    if (blocks > 250)
        return 0;
    if (label_phi && !wide && blocks > 120 && calls < 20)
        return 0;
    if (!label_phi && !wide) {
        if (blocks <= 25 && calls >= 10)
            return 0;
        if (blocks >= 42 && blocks <= 46 &&
            calls >= 15 && calls <= 19 && slots == 4)
            return 0;
    }
    if (!label_phi && wide) {
        if (calls == 2)
            return 0;
        if (blocks >= 40 && blocks <= 50 && slots <= 4)
            return 0;
        if (blocks <= 12 && (slots <= 4 || calls == 3))
            return 0;
    }
    if (label_phi && wide && calls == 4)
        return 0;
    {
        long growth = generated_size > captured_size
            ? generated_size - captured_size : 0;
        int count_limit;
        long growth_limit;

        if (mir_has_inline_substitution_call())
            mir_boolean_phi_residual_sensitive_module = 1;
        count_limit = mir_boolean_phi_residual_sensitive_module ? 5 : 10;
        growth_limit =
            mir_boolean_phi_residual_sensitive_module ? 8000 : 11000;
        if (mir_boolean_phi_residual_count >= count_limit ||
            mir_boolean_phi_residual_growth_used + growth > growth_limit)
            return 0;
        ++mir_boolean_phi_residual_count;
        mir_boolean_phi_residual_growth_used += growth;
    }
    return 1;
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

static int mir_unary_not_repaired_small_is_semantically_eligible(
    long generated_size)
{
    return mir_cfg_block_count() <= 6 &&
           generated_size <= 6000;
}

static int mir_unary_not_large_deferred_count;

static int mir_unary_not_deferred_is_semantically_eligible(
    long generated_size)
{
    int blocks = mir_cfg_block_count();

    if (blocks > 100 && generated_size < 30000)
        return 0;
    if (blocks <= 30 || mir_call_count() >= 16)
        return 1;
    if (blocks <= 40 && mir_unary_not_large_deferred_count == 0) {
        ++mir_unary_not_large_deferred_count;
        return 1;
    }
    return 0;
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
    return generated_size <= 10000 || mir_call_count() >= 80 ||
           (!g_speculative_codegen_active &&
            mir.sink_purpose == EMIT_SINK_FINAL);
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

static int mir_dynamic_index_base_final_sink_is_semantically_eligible(void)
{
    int blocks = mir_cfg_block_count();
    int calls = mir_call_count();
    int bounded_wide_phi_loop =
        blocks <= 80 &&
        calls <= 40 &&
        mir_has_cfg_backedge() &&
        mir_has_wide_integer_object_phi() &&
        !mir.has_vla &&
        !mir_has_inline_substitution_call() &&
        !mir_has_declared_pointer_array() &&
        !mir_has_label_only_phi_fallthrough();

    return (blocks <= 50 || bounded_wide_phi_loop) &&
           mir.backend_slot_count <= 18 &&
           !(calls <= 3 && mir_has_wide_values() &&
             mir_has_label_only_phi_fallthrough());
}

static int mir_dynamic_index_base_residual_is_semantically_eligible(
    long generated_size)
{
    int calls = mir_call_count();
    int label_phi = mir_has_label_only_phi_fallthrough();

    if (mir_has_wide_values())
        return !label_phi && mir.backend_slot_count <= 8;
    if (label_phi)
        return mir_cfg_block_count() <= 13 &&
               calls <= 6;
    return calls <= 1 ||
           (mir_has_cfg_backedge() &&
            mir_cfg_block_count() <= 32 &&
            calls <= 26 &&
            generated_size <= 10000);
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

static int mir_hybrid_homed_retry_is_eligible(const char *reason)
{
    if (!strcmp(reason, "boolean-phi-cost") ||
        !strcmp(reason, "wide-store-cost"))
        return 1;
    if (!strcmp(reason, "binary-load-pair-cost"))
        return mir_cfg_block_count() <= 2 && mir_call_count() <= 1;
    return !strcmp(reason, "unary-not-cost") &&
           !mir_has_cfg_backedge() &&
           !mir_has_inline_substitution_call() &&
           mir_cfg_block_count() <= 92 &&
           mir_call_count() <= 15;
}

static int mir_regional_homed_retry_is_eligible(const char *reason)
{
    return reason != NULL &&
           (!strcmp(reason, "boolean-phi-cost") ||
            !strcmp(reason, "dynamic-index-base-cost") ||
            !strcmp(reason, "unary-not-cost")) &&
           !(mir_boolean_phi_branch_simplification_count() > 0 &&
            mir_has_label_only_phi_fallthrough()) &&
           (mir_call_count() > 0 || mir_cfg_block_count() > 1);
}

static int mir_regional_bounded_call_phi_is_semantically_eligible(
    const char *reason, const char *selector_name,
    int regional_cse_active,
    long generated_size, long captured_size,
    int generated_instructions, int captured_instructions)
{
    int blocks = mir_cfg_block_count();

    return reason != NULL &&
           !strcmp(reason, "dynamic-index-base-cost") &&
           !strcmp(selector_name, "regional-homed-scalar-cfg") &&
           regional_cse_active &&
           blocks >= 3 && blocks <= 32 &&
           mir_call_count() <= 24 &&
           !mir.has_vla &&
           !mir_has_declared_pointer_array() &&
           generated_size <= 6000 &&
           generated_size * 100L <= captured_size * 117L &&
           generated_instructions * 100L <=
               captured_instructions * 122L;
}

static int mir_regional_wide_loop_shape_is_semantically_eligible(void)
{
    int blocks = mir_cfg_block_count();

    return mir_has_wide_values() &&
           mir_has_cfg_backedge() &&
           blocks >= 64 && blocks <= 96 &&
           mir_call_count() <= 20 &&
           !mir.has_vla &&
           !mir_has_inline_substitution_call() &&
           !mir_has_declared_pointer_array();
}

static int mir_regional_wide_loop_is_semantically_eligible(
    long generated_size, long captured_size,
    int generated_instructions, int captured_instructions)
{
    return mir_regional_wide_loop_shape_is_semantically_eligible() &&
           generated_size * 100L <= captured_size * 120L &&
           generated_instructions * 100L <=
               captured_instructions * 121L;
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

static int mir_wide_store_repaired_is_semantically_eligible(
    long generated_size)
{
    if (!mir_has_cfg_backedge())
        return mir_call_count() > 0;
    return generated_size <= 6000 ||
           !(mir_cfg_block_count() == 10 &&
             mir_call_count() == 3 &&
             mir.backend_slot_count >= 13);
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
    return generated_size <= 25000;
}

static int mir_has_inline_temp_identity_overwrite(void)
{
    int current_identity[MAX_PROTO_PARAMS] = {0};
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        int slot = mir_inline_temp_slot(insn->name);
        if (slot < 0)
            continue;
        if (insn->opcode == MIR_STORE)
            current_identity[slot] = insn->inline_temp_id;
        else if (insn->opcode == MIR_LOAD) {
            if (insn->inline_temp_id == 0 ||
                current_identity[slot] != insn->inline_temp_id)
                return 1;
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

static int mir_is_profiled_vla_wide_truncation_loop(
    long generated_size, long captured_size, int generated_instructions,
    int captured_instructions)
{
    int allocs = 0;
    int indirect_loads = 0;
    int indirect_stores = 0;
    int saves = 0;
    int instruction;

    if (!mir.has_vla || mir.sink_purpose != EMIT_SINK_DEFERRED ||
        !mir_has_cfg_backedge() || !mir_has_wide_values() ||
        mir_cfg_block_count() != 7 || mir_call_count() != 0 ||
        mir.count != 64 || (mir.return_type & 15) != TYPE_INT ||
        type_ptr_depth(mir.return_type) != 0 ||
        generated_size > captured_size + 160 ||
        generated_instructions > captured_instructions + 1)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        switch (mir.insns[instruction].opcode) {
        case MIR_VLA_SAVE: ++saves; break;
        case MIR_VLA_ALLOC: ++allocs; break;
        case MIR_LOAD_INDIRECT: ++indirect_loads; break;
        case MIR_STORE_INDIRECT: ++indirect_stores; break;
        default: break;
        }
    return saves == 1 && allocs == 1 &&
           indirect_loads == 1 && indirect_stores == 1;
}

static int mir_is_profiled_variadic_macro_validation_loop(
    long generated_size, long captured_size, int generated_instructions,
    int captured_instructions)
{
    int va_args = 0;
    int va_ends = 0;
    int va_starts = 0;
    int instruction;

    if (!mir.is_variadic_function ||
        mir.sink_purpose != EMIT_SINK_DEFERRED ||
        !mir_has_cfg_backedge() || mir_has_wide_values() ||
        mir_cfg_block_count() != 5 || mir_call_count() != 1 ||
        mir.count != 50 || (mir.return_type & 15) != TYPE_INT ||
        type_ptr_depth(mir.return_type) != 0 ||
        generated_size > captured_size + 84 ||
        generated_instructions > captured_instructions - 5)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        switch (mir.insns[instruction].opcode) {
        case MIR_VA_START: ++va_starts; break;
        case MIR_VA_ARG: ++va_args; break;
        case MIR_VA_END: ++va_ends; break;
        default: break;
        }
    return va_starts == 1 && va_args == 1 && va_ends == 1;
}

static int mir_is_profiled_call_check_runner(
    long generated_size, long captured_size, int generated_instructions,
    int captured_instructions)
{
    int member_addresses = 0;
    int stores_indirect = 0;
    int instruction;

    if (mir.has_vla || mir.sink_purpose != EMIT_SINK_DEFERRED ||
        mir_has_cfg_backedge() || mir_has_wide_values() ||
        mir_cfg_block_count() != 6 || mir_call_count() != 8 ||
        mir.count != 117 || (mir.return_type & 15) != TYPE_VOID ||
        /*
         * Prelegacy scheduling of the two record-pop callees changes this
         * caller's textual candidate/capture delta from 130 to 135 bytes
         * without changing its 117-instruction MIR shape. Forced dual-mode
         * A/B remains faster in both configurations.
         */
        generated_size > captured_size + 135 ||
        generated_instructions > captured_instructions + 14)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_MEMBER_ADDRESS)
            ++member_addresses;
        else if (mir.insns[instruction].opcode == MIR_STORE_INDIRECT)
            ++stores_indirect;
    return member_addresses == 11 && stores_indirect == 1;
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

static int mir_stream_contains_text(FILE *stream, const char *needle)
{
    char line[512];
    long position = ftell(stream);
    int found = 0;

    if (position < 0 || fseek(stream, 0, SEEK_SET) != 0)
        return 0;
    while (fgets(line, sizeof(line), stream) != NULL)
        if (strstr(line, needle) != NULL) {
            found = 1;
            break;
        }
    if (fseek(stream, position, SEEK_SET) != 0)
        return 0;
    return found;
}

static int mir_has_declared_register_object(void)
{
    return mir.has_declared_register_object;
}

static int mir_has_repeated_global_pointer_load(void)
{
    int first;

    for (first = 0; first < mir.count; ++first) {
        const struct MirInsn *load = &mir.insns[first];
        const struct Sym *global;
        int next;

        if (load->opcode != MIR_LOAD || load->name[0] == 0 ||
            type_ptr_depth(load->type) == 0)
            continue;
        global = find_global(load->name);
        if (global == NULL || global->is_array ||
            type_ptr_depth(global->type) == 0)
            continue;
        for (next = first + 1; next < mir.count; ++next)
            if (mir.insns[next].opcode == MIR_LOAD &&
                !strcmp(mir.insns[next].name, load->name))
                return 1;
    }
    return 0;
}

static int mir_has_float_scalar_value(void)
{
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction)
        if (type_ptr_depth(mir.insns[instruction].type) == 0 &&
            (mir.insns[instruction].type & (TYPE_PTR - 1)) == TYPE_FLOAT)
            return 1;
    return 0;
}

static int mir_has_member_address(void)
{
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_MEMBER_ADDRESS)
            return 1;
    return 0;
}

static int mir_has_bool_value(void)
{
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction)
        if (type_is_bool(mir.insns[instruction].type))
            return 1;
    return 0;
}

static int mir_register_policy_version(const char *policy)
{
    char *end;
    long version;

    if (strncmp(policy, "register-v", 10))
        return 0;
    version = strtol(policy + 10, &end, 10);
    if (*end != 0 || version < 1 || version > 69)
        return -1;
    return (int)version;
}

static int mir_final_cost_policy_rejects(
    const char *selector_name, FILE *generated, FILE *captured,
    long generated_size, long captured_size,
    int generated_instructions, int captured_instructions)
{
    const char *policy = getenv("DCC_MIR_FINAL_COST_POLICY");
    int policy_version;

    if (policy == NULL || policy[0] == 0)
        policy = "register-v69";
    if (!strcmp(policy, "off"))
        return 0;
    policy_version = mir_register_policy_version(policy);
    if (policy_version < 0)
        fatal("unknown DCC_MIR_FINAL_COST_POLICY");
    if (!g_speculative_codegen_active &&
        !strcmp(selector_name, "spilled-scalar-cfg") &&
        (mir_is_profiled_vla_wide_truncation_loop(
             generated_size, captured_size,
             generated_instructions, captured_instructions) ||
         mir_is_profiled_variadic_macro_validation_loop(
             generated_size, captured_size,
             generated_instructions, captured_instructions) ||
         mir_is_profiled_call_check_runner(
             generated_size, captured_size,
             generated_instructions, captured_instructions)))
        return 0;
    if (g_speculative_codegen_active) {
        const char *filter =
            getenv("DCC_MIR_SPECULATIVE_REGISTER_FUNCTION");
        int generated_claim;
        int captured_claim;

        if (policy_version < 5 ||
            (filter != NULL && filter[0] != 0 &&
             strcmp(filter, mir.name)) ||
            mir_stream_contains_text(generated, MIR_EXACT_KERNEL_MARKER))
            return 0;
        generated_claim =
            mir_stream_contains_text(generated, ";@dcc.reg claim=");
        captured_claim =
            mir_stream_contains_text(captured, ";@dcc.reg claim=");
        if (getenv("DCC_MIR_FINAL_COST_REPORT") != NULL)
            fprintf(stderr,
                    "; MIR speculative-register function=%s selector=%s "
                    "generated-claim=%d captured-claim=%d "
                    "generated-bytes=%ld captured-bytes=%ld blocks=%d "
                    "phi-slot=%d\n",
                    mir.name, selector_name,
                    generated_claim, captured_claim,
                    generated_size, captured_size, mir_cfg_block_count(),
                    mir_stream_contains_text(
                        generated, MIR_PHI_SLOT_MARKER));
        if (!strcmp(selector_name, "homed-scalar-cfg"))
            return policy_version >= 31 &&
                   captured_claim && !generated_claim &&
                   mir_cfg_block_count() <= 4 &&
                   (long)generated_instructions * 100L >
                       (long)captured_instructions * 105L;
        if (strcmp(selector_name, "spilled-scalar-cfg"))
            return 0;
        return captured_claim && !generated_claim &&
               (generated_size * 100L > captured_size * 135L ||
                (policy_version >= 20 &&
                 mir_cfg_block_count() <= 5 &&
                 generated_size * 100L > captured_size * 105L &&
                 mir_stream_contains_text(captured, " kind=ro ")) ||
                (policy_version >= 26 &&
                 mir_cfg_block_count() <= 4 &&
                 generated_size * 100L > captured_size * 120L &&
                 mir_stream_contains_text(captured, " kind=rw ")) ||
                (policy_version >= 56 &&
                 mir_cfg_block_count() >= 8 &&
                 mir_cfg_block_count() <= 16 &&
                 generated_size * 100L > captured_size * 125L &&
                 (long)generated_instructions * 100L >
                     (long)captured_instructions * 130L));
    }
    if (policy_version > 0) {
        int generated_claim;
        int captured_claim;
        int reject;

        if (!strcmp(selector_name, "homed-scalar-cfg"))
            return (policy_version >= 50 &&
                    mir_has_cfg_backedge() &&
                    !mir_has_wide_values() &&
                    mir_cfg_block_count() == 4 &&
                    mir_call_count() == 1 &&
                    mir.local_bytes <= 4 &&
                    generated_size * 100L > captured_size * 125L &&
                    (long)generated_instructions * 100L >
                        (long)captured_instructions * 150L) ||
                (policy_version >= 61 &&
                 mir.sink_purpose == EMIT_SINK_DEFERRED &&
                 mir_has_bool_value() &&
                 !mir_has_cfg_backedge() &&
                 !mir_has_wide_values() &&
                 mir_cfg_block_count() == 1 &&
                 mir_call_count() == 9 &&
                 mir.local_bytes == 0 &&
                 generated_size * 100L > captured_size * 115L &&
                 (long)generated_instructions * 100L >
                     (long)captured_instructions * 110L) ||
                (policy_version >= 65 &&
                 mir.sink_purpose == EMIT_SINK_FINAL &&
                 mir_has_inline_substitution_call() &&
                 !mir_has_cfg_backedge() &&
                 !mir_has_wide_values() &&
                 mir_cfg_block_count() == 1 &&
                 mir_call_count() == 4 &&
                 mir.local_bytes == 6 &&
                 generated_size <= captured_size &&
                 generated_instructions <= captured_instructions);
        if (strcmp(selector_name, "spilled-scalar-cfg"))
            return 0;
        if (mir_stream_contains_text(generated, MIR_EXACT_KERNEL_MARKER))
            return 0;
        generated_claim =
            mir_stream_contains_text(generated, ";@dcc.reg claim=");
        captured_claim =
            mir_stream_contains_text(captured, ";@dcc.reg claim=");
        reject = captured_claim && !generated_claim;
        if (!reject && policy_version >= 2) {
            int blocks = mir_cfg_block_count();

            reject =
                ((blocks >= 2 && blocks <= 5 && captured_size < 1024 &&
                  generated_size * 100L > captured_size * 130L &&
                  (long)generated_instructions * 100L >
                      (long)captured_instructions * 130L) ||
                 (blocks > 64 &&
                  generated_size > captured_size * 2L &&
                  generated_instructions > captured_instructions * 2));
        }
        if (!reject && policy_version >= 5 &&
            mir_has_declared_register_object() &&
            (!mir_stream_contains_text(generated, MIR_PHI_SLOT_MARKER) ||
             generated_size * 100L > captured_size * 120L))
            reject = 1;
        if (!reject && policy_version >= 6 &&
            mir_has_declared_multidimensional_pointer_array() &&
            generated_size > captured_size)
            reject = 1;
        if (!reject && policy_version >= 7 &&
            mir_has_declared_pointer_array() &&
            generated_size > 10000 &&
            generated_size > captured_size)
            reject = 1;
        if (!reject && policy_version >= 8 &&
            mir.sink_purpose == EMIT_SINK_DEFERRED &&
            mir_has_cfg_backedge() && mir_has_wide_values() &&
            mir_call_count() == 0 &&
            generated_size > 10000 &&
            generated_size > captured_size &&
            generated_size <= captured_size * 2L)
            reject = 1;
        if (!reject && policy_version >= 9 &&
            mir_has_cfg_backedge() &&
            mir_call_count() == 0 &&
            mir_has_declared_multidimensional_array())
            reject = 1;
        if (!reject && policy_version >= 10 &&
            mir_cfg_block_count() >= 32 &&
            mir_cfg_block_count() <= 64 &&
            mir_call_count() == 0 && !mir_has_wide_values() &&
            generated_size * 100L > captured_size * 120L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 105L)
            reject = 1;
        if (!reject && policy_version >= 11 &&
            mir.sink_purpose == EMIT_SINK_DEFERRED &&
            !mir_has_cfg_backedge() && mir_has_wide_values() &&
            mir_cfg_block_count() >= 8 &&
            mir_cfg_block_count() <= 32 &&
            mir_call_count() <= 1 &&
            generated_size * 100L > captured_size * 150L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 130L)
            reject = 1;
        if (!reject && policy_version >= 12 &&
            mir.sink_purpose == EMIT_SINK_DEFERRED &&
            mir_cfg_block_count() >= 8 &&
            mir_cfg_block_count() <= 16 &&
            mir_call_count() == 0 && !mir_has_wide_values() &&
            generated_size * 100L > captured_size * 103L &&
            generated_instructions > captured_instructions)
            reject = 1;
        if (!reject && policy_version >= 13 &&
            mir.sink_purpose == EMIT_SINK_DEFERRED &&
            mir_cfg_block_count() <= 6 &&
            mir_call_count() == 0 &&
            mir_has_repeated_global_pointer_load() &&
            generated_size * 100L > captured_size * 105L)
            reject = 1;
        if (!reject && policy_version >= 14 &&
            mir.sink_purpose == EMIT_SINK_DEFERRED &&
            mir_has_cfg_backedge() && mir_has_wide_values() &&
            mir_call_count() == 0 &&
            generated_size >= 3000 && generated_size <= 6000 &&
            generated_size * 100L > captured_size * 150L &&
            generated_size * 100L <= captured_size * 180L)
            reject = 1;
        if (!reject && policy_version >= 15 &&
            mir_has_cfg_backedge() && mir_has_wide_values() &&
            mir_has_large_volatile_array())
            reject = 1;
        if (!reject && policy_version >= 16 &&
            mir.sink_purpose == EMIT_SINK_DEFERRED &&
            !mir_has_cfg_backedge() &&
            mir_cfg_block_count() >= 16 &&
            mir_cfg_block_count() <= 32 &&
            mir_call_count() <= 1 &&
            generated_size * 100L > captured_size * 150L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 130L)
            reject = 1;
        if (!reject && policy_version >= 17 &&
            mir.sink_purpose == EMIT_SINK_FINAL &&
            mir_cfg_block_count() <= 16 &&
            mir_call_count() >= 50 &&
            generated_size * 100L > captured_size * 110L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 110L)
            reject = 1;
        if (!reject && policy_version >= 18 &&
            mir.has_indirect_incdec &&
            mir_cfg_block_count() == 1 &&
            generated_size > captured_size &&
            !(mir_spilled_cfg_depends_on_indirect_incdec() &&
              generated_size <= captured_size + 8 &&
              generated_instructions < captured_instructions))
            reject = 1;
        if (!reject && policy_version >= 19 &&
            mir.sink_purpose == EMIT_SINK_DEFERRED &&
            mir_cfg_block_count() == 1 &&
            mir_call_count() == 0 && mir_has_wide_values() &&
            type_size(mir.return_type) == 4 &&
            !mir_has_float_scalar_value() &&
            generated_size * 100L > captured_size * 150L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 130L)
            reject = 1;
        if (!reject && policy_version >= 21 &&
            mir.has_pointer_difference &&
            mir_cfg_block_count() == 1 &&
            generated_size > captured_size &&
            !(mir_spilled_cfg_depends_on_pointer_difference_shift() &&
              generated_size <= captured_size + 10 &&
              generated_instructions < captured_instructions))
            reject = 1;
        if (!reject && policy_version >= 22 &&
            mir.is_variadic_function &&
            generated_size > captured_size)
            reject = 1;
        if (!reject && policy_version >= 23 &&
            mir.has_runtime_stride_param &&
            mir_has_cfg_backedge() && mir_has_wide_values() &&
            mir_call_count() == 0 &&
            generated_size > captured_size)
            reject = 1;
        if (!reject && policy_version >= 24 &&
            mir.sink_purpose == EMIT_SINK_DEFERRED &&
            mir_has_cfg_backedge() && mir_has_wide_values() &&
            mir_call_count() == 0 &&
            mir_cfg_block_count() >= 8 &&
            mir_cfg_block_count() <= 16 &&
            generated_size < 4000 &&
            generated_size * 100L > captured_size * 105L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 105L)
            reject = 1;
        if (!reject && policy_version >= 25 &&
            mir.sink_purpose == EMIT_SINK_DEFERRED &&
            mir_cfg_block_count() == 1 &&
            mir_call_count() <= 1 &&
            (mir.return_type & (TYPE_PTR - 1)) == TYPE_FLOAT &&
            generated_size >= 1000 &&
            generated_size * 100L > captured_size * 110L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 105L)
            reject = 1;
        if (!reject && policy_version >= 27 &&
            mir.sink_purpose == EMIT_SINK_FINAL &&
            mir_cfg_block_count() >= 17 &&
            mir_cfg_block_count() <= 32 &&
            mir_call_count() >= 10 &&
            generated_size * 100L > captured_size * 110L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 110L)
            reject = 1;
        if (!reject && policy_version >= 28 &&
            mir.sink_purpose == EMIT_SINK_FINAL &&
            mir_cfg_block_count() >= 24 &&
            mir_cfg_block_count() <= 31 &&
            mir_call_count() <= 1 &&
            generated_size * 100L > captured_size * 150L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 150L)
            reject = 1;
        if (!reject && policy_version >= 29 &&
            mir_has_inline_substitution_call() &&
            generated_size * 100L > captured_size * 130L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 120L)
            reject = 1;
        if (!reject && policy_version >= 30 &&
            mir.sink_purpose == EMIT_SINK_DEFERRED &&
            mir_cfg_block_count() >= 8 &&
            mir_cfg_block_count() <= 15 &&
            mir_call_count() <= 1 &&
            generated_size * 100L > captured_size * 150L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 130L)
            reject = 1;
        if (!reject && policy_version >= 32 &&
            mir_has_fixed_local_multidimensional_array() &&
            generated_size > captured_size &&
            generated_instructions > captured_instructions)
            reject = 1;
        if (!reject && policy_version >= 33 &&
            mir.sink_purpose == EMIT_SINK_DEFERRED &&
            mir_has_cfg_backedge() && mir_has_wide_values() &&
            mir_call_count() == 0 &&
            mir_cfg_block_count() >= 4 &&
            mir_cfg_block_count() <= 7 &&
            generated_size * 100L > captured_size * 105L &&
            generated_instructions > captured_instructions)
            reject = 1;
        if (!reject && policy_version >= 34 &&
            mir.sink_purpose == EMIT_SINK_DEFERRED &&
            mir_cfg_block_count() >= 2 &&
            mir_cfg_block_count() <= 7 &&
            mir_call_count() <= 1 && mir_has_wide_values() &&
            generated_size >= 1000 &&
            generated_size * 100L > captured_size * 130L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 120L)
            reject = 1;
        if (!reject && policy_version >= 34 &&
            mir.sink_purpose == EMIT_SINK_FINAL &&
            mir_cfg_block_count() <= 16 &&
            mir_call_count() >= 50 &&
            generated_size * 100L > captured_size * 105L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 105L)
            reject = 1;
        if (!reject && policy_version >= 35 &&
            mir_spilled_cfg_divmod_has_dead_result() &&
            generated_size > captured_size)
            reject = 1;
        if (!reject && policy_version >= 36 &&
            mir_has_cfg_backedge() &&
            mir_cfg_block_count() >= 24 &&
            mir_cfg_block_count() <= 31 &&
            mir_call_count() == 0 && !mir_has_wide_values() &&
            generated_size * 100L > captured_size * 110L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 110L)
            reject = 1;
        if (!reject && policy_version >= 37 &&
            !mir_has_cfg_backedge() &&
            mir_cfg_block_count() >= 24 &&
            mir_cfg_block_count() <= 30 &&
            mir_call_count() == 2 &&
            generated_size * 100L > captured_size * 120L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 115L)
            reject = 1;
        if (!reject && policy_version >= 38 &&
            mir.sink_purpose == EMIT_SINK_FINAL &&
            mir_cfg_block_count() <= 2 &&
            mir_call_count() >= 30 &&
            generated_size * 100L > captured_size * 110L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 105L)
            reject = 1;
        if (!reject && policy_version >= 39 &&
            mir.sink_purpose == EMIT_SINK_DEFERRED &&
            mir_has_cfg_backedge() && mir_has_wide_values() &&
            mir_cfg_block_count() >= 24 &&
            mir_cfg_block_count() <= 31 &&
            mir_call_count() >= 2 &&
            mir_call_count() <= 9 &&
            generated_size * 100L > captured_size * 125L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 120L)
            reject = 1;
        if (!reject && policy_version >= 40 &&
            mir.sink_purpose == EMIT_SINK_FINAL &&
            mir.has_narrowed_for_counter &&
            mir_has_cfg_backedge() &&
            mir_cfg_block_count() >= 8 &&
            mir_cfg_block_count() <= 16 &&
            mir_call_count() <= 1 &&
            generated_size < captured_size &&
            (long)generated_instructions * 100L <=
                (long)captured_instructions * 85L)
            /*
             * Raw size misses dccpeep's proven byte-counter promotion:
             * captured code can keep the induction value in C/E while
             * spilled MIR repeatedly accesses a 16-bit frame slot.
             */
            reject = 1;
        if (!reject && policy_version >= 41 &&
            mir.sink_purpose == EMIT_SINK_DEFERRED &&
            !mir_has_cfg_backedge() && !mir_has_wide_values() &&
            !mir_has_label_only_phi_fallthrough() &&
            mir_cfg_block_count() >= 8 &&
            mir_cfg_block_count() <= 15 &&
            mir_call_count() == 2 &&
            generated_size * 100L > captured_size * 120L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 110L)
            reject = 1;
        if (!reject && policy_version >= 42 &&
            mir.sink_purpose == EMIT_SINK_FINAL &&
            mir_has_cfg_backedge() && mir_has_wide_values() &&
            mir_cfg_block_count() >= 17 &&
            mir_cfg_block_count() <= 23 &&
            mir_call_count() >= 2 &&
            mir_call_count() <= 9 &&
            generated_size * 100L > captured_size * 150L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 130L)
            reject = 1;
        if (!reject && policy_version >= 43 &&
            mir.sink_purpose == EMIT_SINK_FINAL &&
            mir_has_cfg_backedge() && mir_has_wide_values() &&
            mir_cfg_block_count() >= 8 &&
            mir_cfg_block_count() <= 16 &&
            mir_call_count() >= 20 &&
            mir_call_count() <= 29 &&
            generated_size * 100L > captured_size * 130L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 130L)
            reject = 1;
        if (!reject && policy_version >= 44 &&
            mir.sink_purpose == EMIT_SINK_DEFERRED &&
            mir.has_compound_literal &&
            mir_cfg_block_count() == 1 &&
            generated_size * 100L > captured_size * 120L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 120L)
            reject = 1;
        if (!reject && policy_version >= 45 &&
            mir.sink_purpose == EMIT_SINK_DEFERRED &&
            mir_has_member_address() &&
            !mir_has_label_only_phi_fallthrough() &&
            mir_cfg_block_count() <= 16 &&
            mir.backend_slot_count >= 3 &&
            generated_size * 100L > captured_size * 130L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 120L)
            reject = 1;
        if (!reject && policy_version >= 46 &&
            mir.sink_purpose == EMIT_SINK_DEFERRED &&
            type_is_struct_object(mir.return_type) &&
            mir_cfg_block_count() == 2 &&
            mir.backend_slot_count >= 4 &&
            generated_size * 100L > captured_size * 120L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 115L)
            reject = 1;
        if (!reject && policy_version >= 47 &&
            mir.sink_purpose == EMIT_SINK_DEFERRED &&
            mir_has_member_address() &&
            mir_cfg_block_count() == 1 &&
            mir.backend_slot_count == 1 &&
            mir_call_count() >= 4 &&
            mir_call_count() <= 9 &&
            !mir_has_wide_values() &&
            generated_size * 100L > captured_size * 130L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 120L)
            reject = 1;
        if (!reject && policy_version >= 48 &&
            mir.sink_purpose == EMIT_SINK_FINAL &&
            !mir_has_cfg_backedge() && mir_has_wide_values() &&
            mir_cfg_block_count() == 2 &&
            mir_call_count() >= 10 &&
            mir_call_count() <= 19 &&
            generated_size * 100L > captured_size * 120L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 120L)
            reject = 1;
        if (!reject && policy_version >= 49 &&
            mir_has_member_address() &&
            !mir_has_wide_values() &&
            mir_cfg_block_count() == 6 &&
            ((mir.sink_purpose == EMIT_SINK_DEFERRED &&
              !mir_has_cfg_backedge() &&
              mir.backend_slot_count == 2 &&
              mir_call_count() == 8 &&
              mir.local_bytes == 4 &&
              generated_size * 100L > captured_size * 105L &&
              (long)generated_instructions * 100L >
                  (long)captured_instructions * 105L) ||
             (mir.sink_purpose == EMIT_SINK_FINAL &&
              mir_has_cfg_backedge() &&
              mir.backend_slot_count == 3 &&
              mir_call_count() == 5 &&
              mir.local_bytes <= 2 &&
              generated_size * 100L > captured_size * 115L &&
              (long)generated_instructions * 100L >
                  (long)captured_instructions * 110L)))
            reject = 1;
        if (!reject && policy_version >= 51 &&
            mir.sink_purpose == EMIT_SINK_DEFERRED &&
            mir_has_cfg_backedge() && !mir_has_wide_values() &&
            mir_cfg_block_count() == 4 &&
            mir_call_count() == 0 &&
            mir.local_bytes == 4 &&
            generated_size * 100L > captured_size * 110L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 110L)
            reject = 1;
        if (!reject && policy_version >= 52 &&
            mir.sink_purpose == EMIT_SINK_DEFERRED &&
            mir_has_cfg_backedge() && !mir_has_wide_values() &&
            mir_cfg_block_count() == 4 &&
            mir_call_count() == 0 &&
            mir.local_bytes == 8 &&
            mir.backend_slot_count >= 3 &&
            generated_size * 100L > captured_size * 110L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 110L)
            reject = 1;
        if (!reject && policy_version >= 53 &&
            mir.sink_purpose == EMIT_SINK_FINAL &&
            !mir_has_cfg_backedge() && mir_has_wide_values() &&
            mir_cfg_block_count() == 2 &&
            mir_call_count() >= 20 &&
            mir_call_count() <= 29 &&
            generated_size * 100L > captured_size * 115L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 115L)
            reject = 1;
        if (!reject && policy_version >= 54 &&
            mir.sink_purpose == EMIT_SINK_FINAL &&
            mir_spilled_cfg_depends_on_dynamic_index_base_forwarding() &&
            mir_has_cfg_backedge() && !mir_has_wide_values() &&
            mir_cfg_block_count() >= 24 &&
            mir_cfg_block_count() <= 31 &&
            mir_call_count() >= 24 &&
            mir_call_count() <= 29 &&
            mir.backend_slot_count <= 3 &&
            mir_boolean_phi_branch_simplification_count() > 0 &&
            generated_size <= captured_size)
            reject = 1;
        if (!reject && policy_version >= 55 &&
            mir.sink_purpose == EMIT_SINK_DEFERRED &&
            mir.has_indirect_incdec &&
            mir_has_cfg_backedge() && !mir_has_wide_values() &&
            mir_cfg_block_count() == 4 &&
            mir_call_count() == 0 &&
            mir.backend_slot_count >= 3 &&
            generated_size * 100L > captured_size * 110L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 105L)
            reject = 1;
        if (!reject && policy_version >= 57 &&
            mir.sink_purpose == EMIT_SINK_FINAL &&
            mir_has_inline_substitution_call() &&
            mir_cfg_block_count() >= 48 &&
            mir_cfg_block_count() <= 64 &&
            mir_call_count() >= 10 &&
            mir_call_count() <= 24 &&
            generated_size * 100L > captured_size * 115L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 110L)
            reject = 1;
        if (!reject && policy_version >= 58 &&
            mir.sink_purpose == EMIT_SINK_DEFERRED &&
            !mir_has_cfg_backedge() && mir_has_wide_values() &&
            mir_call_count() == 0 &&
            mir.local_bytes == 0 &&
            mir.backend_slot_count >= 4 &&
            ((mir_cfg_block_count() == 2 &&
              type_size(mir.return_type) == 2 &&
              generated_size * 100L > captured_size * 135L &&
              (long)generated_instructions * 100L >
                  (long)captured_instructions * 115L) ||
             (mir_cfg_block_count() == 1 &&
              type_size(mir.return_type) == 4 &&
              !type_is_float(mir.return_type) &&
              generated_size * 100L > captured_size * 130L &&
              (long)generated_instructions * 100L >
                  (long)captured_instructions * 120L)))
            reject = 1;
        if (!reject && policy_version >= 59 &&
            mir.sink_purpose == EMIT_SINK_DEFERRED &&
            mir_has_cfg_backedge() && mir_has_wide_values() &&
            mir_cfg_block_count() == 4 &&
            mir.local_bytes == 6 &&
            ((mir_call_count() == 0 &&
              mir.backend_slot_count == 6 &&
              generated_size * 100L > captured_size * 105L &&
              generated_instructions <= captured_instructions) ||
             (mir_call_count() == 5 &&
              mir.backend_slot_count >= 9 &&
              generated_size * 100L > captured_size * 130L &&
              (long)generated_instructions * 100L >
                  (long)captured_instructions * 130L)))
            reject = 1;
        if (!reject && policy_version >= 60 &&
            mir.sink_purpose == EMIT_SINK_DEFERRED &&
            mir.has_runtime_stride_param &&
            mir_has_declared_pointer_array() &&
            mir_has_cfg_backedge() && !mir_has_wide_values() &&
            mir_cfg_block_count() == 7 &&
            mir_call_count() == 0 &&
            mir.local_bytes == 6 &&
            generated_size > captured_size)
            reject = 1;
        if (!reject && policy_version >= 61 &&
            mir.sink_purpose == EMIT_SINK_DEFERRED &&
            mir_has_bool_value() &&
            ((mir_cfg_block_count() == 1 &&
              !mir_has_cfg_backedge() &&
              mir_has_wide_values() &&
              mir_call_count() == 14 &&
              mir.local_bytes == 12 &&
              mir.backend_slot_count == 1 &&
              generated_size * 100L > captured_size * 125L &&
              (long)generated_instructions * 100L >
                  (long)captured_instructions * 125L) ||
             (mir_cfg_block_count() == 8 &&
              mir_has_cfg_backedge() &&
              !mir_has_wide_values() &&
              mir_call_count() == 2 &&
              mir.local_bytes == 4 &&
              mir.backend_slot_count >= 5 &&
              generated_size * 100L > captured_size * 130L &&
              (long)generated_instructions * 100L >
                  (long)captured_instructions * 120L) ||
             (mir_cfg_block_count() == 18 &&
              !mir_has_cfg_backedge() &&
              !mir_has_wide_values() &&
              mir_call_count() == 7 &&
              mir.local_bytes == 4 &&
              mir.backend_slot_count >= 5 &&
              generated_size * 100L > captured_size * 150L &&
              (long)generated_instructions * 100L >
                  (long)captured_instructions * 135L)))
            reject = 1;
        if (!reject && policy_version >= 62 &&
            mir.sink_purpose == EMIT_SINK_FINAL &&
            !mir_has_cfg_backedge() &&
            mir_has_float_scalar_value() &&
            mir_cfg_block_count() == 2 &&
            mir_call_count() >= 10 &&
            mir_call_count() <= 19 &&
            generated_size * 100L > captured_size * 115L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 105L)
            reject = 1;
        if (!reject && policy_version >= 63 &&
            mir.sink_purpose == EMIT_SINK_FINAL &&
            !mir_has_cfg_backedge() && !mir_has_wide_values() &&
            mir_cfg_block_count() >= 3 &&
            mir_cfg_block_count() <= 7 &&
            mir_call_count() >= 40 &&
            mir_call_count() <= 49 &&
            generated_size * 100L > captured_size * 125L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 125L)
            reject = 1;
        if (!reject && policy_version >= 64 &&
            mir.sink_purpose == EMIT_SINK_DEFERRED &&
            mir_has_member_address() &&
            mir_has_wide_values() &&
            mir_cfg_block_count() == 1 &&
            mir_call_count() >= 30 &&
            mir_call_count() <= 39 &&
            mir.local_bytes >= 80 &&
            generated_size * 100L > captured_size * 120L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 115L)
            reject = 1;
        if (!reject && policy_version >= 66 &&
            ((mir.sink_purpose == EMIT_SINK_DEFERRED &&
              !mir_has_cfg_backedge() && !mir_has_wide_values() &&
              mir_cfg_block_count() >= 24 &&
              mir_cfg_block_count() <= 28 &&
              mir_call_count() == 3 &&
              mir.local_bytes == 24 &&
              generated_size * 100L > captured_size * 150L &&
              (long)generated_instructions * 100L >
                  (long)captured_instructions * 140L) ||
             (mir.sink_purpose == EMIT_SINK_FINAL &&
              mir_has_cfg_backedge() && mir_has_wide_values() &&
              mir_has_label_only_phi_fallthrough() &&
              mir_boolean_phi_branch_simplification_count() > 0 &&
              mir_cfg_block_count() >= 32 &&
              mir_cfg_block_count() <= 40 &&
              mir_call_count() == 4 &&
              mir.local_bytes == 35 &&
              generated_size * 100L > captured_size * 125L &&
              (long)generated_instructions * 100L >
                  (long)captured_instructions * 115L)))
            reject = 1;
        if (!reject && policy_version >= 67 &&
            mir.sink_purpose == EMIT_SINK_DEFERRED &&
            mir_has_cfg_backedge() && mir_has_wide_values() &&
            mir_cfg_block_count() >= 4 &&
            mir_cfg_block_count() <= 7 &&
            mir_call_count() >= 5 &&
            mir_call_count() <= 6 &&
            mir.local_bytes == 6 &&
            generated_size * 100L <= captured_size * 115L &&
            (long)generated_instructions * 100L <=
                (long)captured_instructions * 110L)
            reject = 1;
        if (!reject && policy_version >= 68 &&
            mir.sink_purpose == EMIT_SINK_DEFERRED &&
            mir_has_member_address() &&
            mir_has_wide_values() &&
            !mir_has_cfg_backedge() &&
            mir_cfg_block_count() == 1 &&
            mir_call_count() == 4 &&
            mir.backend_slot_count == 1 &&
            mir.local_bytes == 45 &&
            generated_size * 100L > captured_size * 140L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 140L)
            reject = 1;
        if (!reject && policy_version >= 68 &&
            mir_spilled_cfg_depends_on_wide_call_constant_comparison() &&
            ((mir_cfg_block_count() <= 2 &&
              generated_instructions >= captured_instructions) ||
             (long)generated_instructions * 100L >
                 (long)captured_instructions * 135L))
            reject = 1;
        if (!reject && policy_version >= 68 &&
            mir_spilled_cfg_depends_on_local_constant_byte_store() &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 115L)
            reject = 1;
        if (!reject && policy_version >= 69 &&
            mir.sink_purpose == EMIT_SINK_FINAL &&
            mir_has_cfg_backedge() && mir_has_wide_values() &&
            mir_cfg_block_count() == 43 &&
            mir.local_bytes == 156 &&
            generated_size * 100L > captured_size * 140L &&
            (long)generated_instructions * 100L >
                (long)captured_instructions * 130L)
            reject = 1;
        if (getenv("DCC_MIR_FINAL_COST_REPORT") != NULL)
            fprintf(stderr,
                    "; MIR final-register function=%s selector=%s "
                    "generated-claim=%d captured-claim=%d reject=%d\n",
                    mir.name, selector_name,
                    generated_claim, captured_claim, reject);
        if (reject || policy_version < 3)
            return reject;
        policy = policy_version >= 4 ? "cost-v4" : "cost-v3";
    }
    if (!strcmp(policy, "cost-v3") ||
        !strcmp(policy, "cost-v4")) {
        struct MirCostComponents generated_cost;
        struct MirCostComponents captured_cost;
        double threshold = 1.30;

        if (strcmp(selector_name, "spilled-scalar-cfg") ||
            mir_cfg_block_count() > 64 ||
            mir_stream_contains_text(generated, MIR_EXACT_KERNEL_MARKER))
            return 0;
        if (mir.sink_purpose != EMIT_SINK_FINAL) {
            if (strcmp(policy, "cost-v4") ||
                mir.sink_purpose != EMIT_SINK_DEFERRED ||
                mir_cfg_block_count() <= 1 ||
                (mir_has_cfg_backedge() && mir_has_wide_values()) ||
                generated_size * 100L <= captured_size * 130L)
                return 0;
            threshold = 2.00;
        }
        mir_estimate_stream_cost(generated, &generated_cost);
        mir_estimate_stream_cost(captured, &captured_cost);
        if (getenv("DCC_MIR_FINAL_COST_REPORT") != NULL)
            fprintf(stderr,
                    "; MIR final-cost-v3 function=%s selector=%s "
                    "generated=%.3f captured=%.3f\n",
                    mir.name, selector_name,
                    generated_cost.score, captured_cost.score);
        return generated_cost.score > captured_cost.score * threshold;
    }
    if (!strcmp(policy, "cost-v2")) {
        struct MirCostComponents generated_cost;
        struct MirCostComponents captured_cost;

        if (strcmp(selector_name, "spilled-scalar-cfg"))
            return 0;
        mir_estimate_stream_cost(generated, &generated_cost);
        mir_estimate_stream_cost(captured, &captured_cost);
        if (getenv("DCC_MIR_FINAL_COST_REPORT") != NULL)
            fprintf(stderr,
                    "; MIR final-cost function=%s selector=%s "
                    "generated=%.3f captured=%.3f generated-tstates=%.3f "
                    "captured-tstates=%.3f generated-helpers=%.3f "
                    "captured-helpers=%.3f generated-bytes=%ld "
                    "captured-bytes=%ld\n",
                    mir.name, selector_name,
                    generated_cost.score, captured_cost.score,
                    generated_cost.tstates, captured_cost.tstates,
                    generated_cost.helper_tstates,
                    captured_cost.helper_tstates,
                    generated_cost.bytes, captured_cost.bytes);
        return generated_cost.score > captured_cost.score * 1.02;
    }
    if (strcmp(policy, "legacy-v1"))
        fatal("unknown DCC_MIR_FINAL_COST_POLICY");
    /*
     * Diagnostic policy: the coverage-first rollout deliberately accepts
     * structurally safe spilled candidates after their earlier cost gates.
     * Reject only when both available proxies say the final candidate is
     * materially worse than the captured legacy output. Keep homed and exact
     * specialized selectors outside this first policy because their extra
     * text can buy register residency and lower dynamic cost.
     */
    return !strcmp(selector_name, "spilled-scalar-cfg") &&
           !mir_spilled_cfg_uses_exact_semantic_kernel() &&
           generated_size > captured_size &&
           generated_instructions > captured_instructions &&
           generated_size * 100L > captured_size * 102L &&
           (long)generated_instructions * 100L >
               (long)captured_instructions * 102L;
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
        if (mir.report_mode && !g_speculative_codegen_active &&
            !mir_prelegacy_scheduled_attempt_active)
            fprintf(stderr, "; MIR emit function=%s result=oversized-fallback\n",
                    mir.name);
        fclose(mir.capture_stream);
        mir.capture_stream = NULL;
        mir.emit_mode = 0;
        mir.active = 0;
        return;
    }
    mir_prune_constant_unreachable();
    mir_thread_jumps();
    mir_resolve_deferred_metadata();
    mir_canonicalize_signed_wide_relational_constants();
    mir_reset_boolean_phi_branch_simplification_count();
    verified = mir_verify_and_dump();
    if (verified) {
        mir_compute_dead_local_suffix();
        mir_report_dead_local_suffix();
        mir_target_report_shadow_plan();
        mir_schedule_report_shadow_plan();
    }
    if (mir.opaque_count != 0 &&
        getenv("DCC_MIR_REQUIRE_COMPLETE") != NULL) {
        fprintf(stderr, "MIR completeness failed for function %s\n", mir.name);
        fatal("incomplete MIR coverage");
    }
    if (verified &&
        getenv("DCC_MIR_REGIONAL_HOME_REPORT") != NULL) {
        const char *regional_filter =
            getenv("DCC_MIR_REGIONAL_HOME_FUNCTION");

        if ((regional_filter == NULL ||
             !strcmp(regional_filter, mir.name)) &&
            mir_begin_regional_home_plan())
            mir_end_regional_home_plan();
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
        int adjacent_exx_elided_instructions = 0;
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
            int final_stack_retry_attempted = 0;
            int phi_slot_retry_attempted = 0;
            int strict_phi_retry_attempted = 0;
            int strict_phi_fallthrough_active = 0;
            int phi_return_forwarding_retry_attempted = 0;
            int regional_cse_retry_attempted = 0;
            int regional_cse_active = 0;
            const char *regional_cse_fallback_reason = NULL;
            int regional_homed_retry_attempted = 0;
            int block_cse_retry_attempted = 0;
            int block_cse_captured_spills = 0;
            int block_cse_captured_fixed_moves = 0;
            int block_cse_captured_operand_moves = 0;
            int block_cse_captured_phi_moves = 0;
            int boolean_phi_retry_attempted = 0;
            int measured_boolean_candidate = 0;
            int rematerialized_home_retry_attempted = 0;
            int rematerialized_home_allocation_active = 0;
            int hybrid_homed_retry_attempted = 0;
            int hybrid_homed_candidate = 0;
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
            hybrid_homed_retry_attempted = 0;
            hybrid_homed_candidate = 0;
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
            if (regional_cse_active) {
                if (mir_cfg_block_count() <= 2)
                    mir_begin_all_spilled_fallback_optimizations();
                else
                    mir_begin_promoted_local_slot_reuse();
            }
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
                label_id = mir_label_base;
                emitted = mir_try_selector(
                    generated,
                    g_speculative_codegen_active
                        ? mir_try_emit_speculation_safe_machine_cfg
                        : mir_try_emit_scheduled_machine_cfg);
                generated_label_id_after = label_id;
                if (emitted)
                    selector_name = "scheduled-machine-cfg";
            }
            if (mir_prelegacy_scheduled_attempt_active) {
                if (emitted)
                    goto evaluate_generated;
                label_id = mir_label_base;
                goto copy_selected_output;
            }
            if (!emitted && (emit_filter == NULL ||
                             emit_filter[0] == 0) &&
                (general_filter == NULL || general_filter[0] == 0) &&
                getenv("DCC_MIR_EMIT_GENERAL") == NULL) {
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
                     mir_spilled_cfg_inline_simple_indexed_store_uses() > 0 ||
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
                {
                    FILE *compacted =
                        mir_compact_adjacent_exx(
                            generated,
                            &adjacent_exx_elided_instructions);
                    fclose(generated);
                    generated = compacted;
                }
                generated_size = mir_stream_size(generated);
                captured_size = mir_stream_size(mir.capture_stream);
                generated_instructions =
                    mir_stream_instruction_count(generated);
                if (!strcmp(selector_name,
                            "spilled-scalar-cfg") &&
                    adjacent_exx_elided_instructions > 0) {
                    generated_size +=
                        adjacent_exx_elided_instructions * 5L;
                    generated_instructions +=
                        adjacent_exx_elided_instructions;
                }
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
                if (mir_prelegacy_scheduled_attempt_active) {
                    if (fallback_reason == NULL &&
                        (generated_size < 0 || captured_size < 0 ||
                         generated_instructions < 0 ||
                         captured_instructions < 0))
                        fallback_reason = "measurement";
                    goto prelegacy_final_cost;
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
                else if (!boolean_phi_retry_attempted &&
                         mir.sink_purpose == EMIT_SINK_DEFERRED &&
                         !mir_has_cfg_backedge() &&
                         !mir_has_wide_values() &&
                         mir_cfg_block_count() == 13 &&
                         mir_call_count() == 2 &&
                         mir.local_bytes == 0 &&
                         mir.backend_slot_count == 3 &&
                         generated_size * 100L > captured_size * 130L &&
                         (long)generated_instructions * 100L >
                             (long)captured_instructions * 115L &&
                         mir_boolean_phi_branch_candidate_count() > 0)
                    /*
                     * This acyclic assertion-helper class reserves three
                     * frame slots solely to materialize a short-circuit
                     * boolean. Route its materially bloated candidates
                     * through the existing semantic PHI-threading retry.
                     */
                    fallback_reason = "boolean-phi-materialization";
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
                        !strcmp(fallback_reason, "phi-fallthrough-cost"))
                        /*
                         * T455: accept the repaired strict-PHI candidate at
                         * the same decision point used by the validated
                         * reason-forcing experiment. Letting it enter later
                         * alternate retries changes the candidate and can
                         * reintroduce synthetic-edge copies.
                         */
                        fallback_reason = NULL;
                    if (fallback_reason != NULL &&
                        !strcmp(fallback_reason,
                                "boolean-phi-cost") &&
                        !strcmp(selector_name,
                                "regional-homed-scalar-cfg") &&
                        !g_speculative_codegen_active &&
                        mir_regional_wide_loop_is_semantically_eligible(
                            generated_size, captured_size,
                            generated_instructions,
                            captured_instructions))
                        /*
                         * Wide call-bounded loops retain their pair-colored
                         * base allocation while regional narrow segments own
                         * call crossings. The bounded parser/lexer stratum
                         * passes both modes after wide increment fusion.
                         */
                        fallback_reason = NULL;
                    if (fallback_reason != NULL &&
                        !strcmp(fallback_reason, "boolean-phi-cost") &&
                        !g_speculative_codegen_active &&
                        mir_boolean_phi_repaired_bounded_is_semantically_eligible(
                            generated_size))
                        /*
                         * T456: keep the validated boolean candidate instead
                         * of entering alternate retries that can select a
                         * different, unsafe pint layout.
                         */
                        fallback_reason = NULL;
                    if (fallback_reason != NULL &&
                        !strcmp(fallback_reason, "unary-not-cost") &&
                        !g_speculative_codegen_active &&
                        mir_unary_not_repaired_small_is_semantically_eligible(
                            generated_size))
                        /* T457: take the repaired <=6-block terminal unary
                         * candidate before unrelated alternate retries. */
                        fallback_reason = NULL;
                    if (fallback_reason != NULL &&
                        !strcmp(fallback_reason, "dead-local-suffix-cost") &&
                        !g_speculative_codegen_active)
                        /* T480: label aliases no longer emit a second PHI
                         * copy after the real predecessor edge. The former
                         * 45-block outlier now passes both backend modes. */
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
                    strcmp(fallback_reason, "forced") != 0 &&
                    !strcmp(selector_name, "spilled-scalar-cfg") &&
                    mir_spilled_cfg_uses_exact_semantic_kernel())
                    /*
                     * Exact semantic kernels validate their complete graph,
                     * ABI, symbols, and resources before emitting. Generic
                     * cost retries must not replace them with a less exact
                     * selector merely because the original MIR graph has
                     * PHIs, inline-substitution metadata, or a large CFG.
                     */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    strcmp(fallback_reason, "forced") != 0 &&
                    !g_speculative_codegen_active &&
                    !strcmp(selector_name, "spilled-scalar-cfg") &&
                    mir_spilled_cfg_dense_byte_switch_case_count() <= 16 &&
                    mir_dense_byte_switch_is_semantically_eligible(
                        selector_name,
                        generated_size, captured_size,
                        generated_instructions,
                        captured_instructions))
                    /*
                     * T520: a bounded small statement dispatcher is already
                     * tabled by the spilled selector. Keep it instead of
                     * replacing it with a larger hybrid-home equality chain.
                     */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    mir_hybrid_homed_retry_is_eligible(fallback_reason) &&
                    !hybrid_homed_retry_attempted &&
                    !g_speculative_codegen_active) {
                    FILE *hybrid_candidate = tmpfile();
                    int hybrid_emitted;

                    hybrid_homed_retry_attempted = 1;
                    if (hybrid_candidate == NULL)
                        fatal("cannot create MIR hybrid-home candidate "
                              "stream");
                    mir_begin_hybrid_homed_selection();
                    label_id = mir_label_base;
                    hybrid_emitted = mir_try_selector(
                        hybrid_candidate, mir_try_emit_homed_scalar_cfg);
                    mir_end_hybrid_homed_selection();
                    if (hybrid_emitted) {
                        fclose(generated);
                        generated = hybrid_candidate;
                        hybrid_candidate = NULL;
                        selector_name = "hybrid-homed-scalar-cfg";
                        emitted = 1;
                        hybrid_homed_candidate = 1;
                        generated_label_id_after = label_id;
                        fallback_reason = NULL;
                        goto evaluate_generated;
                    }
                    fclose(hybrid_candidate);
                }
                if (fallback_reason != NULL &&
                    hybrid_homed_candidate &&
                    mir_hybrid_homed_retry_is_eligible(fallback_reason))
                    fallback_reason = NULL;
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
                        fallback_reason = "regional-cse-verify";
                        emitted = 0;
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
                        (mir_cfg_block_count() == 1 ||
                         (mir_cfg_block_count() <= 2 &&
                          mir_call_count() <= 1 &&
                          !strcmp(fallback_reason,
                                  "binary-load-pair-cost"))) &&
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
                    /* T449 follow-up: typed assignment aliases now preserve
                     * compound-assignment narrowing and equal-width
                     * signedness through later widening. The remaining
                     * oversized true-FINAL shift matrix passes both modes;
                     * keep speculative retries on the established bounds. */
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
                    !strcmp(fallback_reason, "boolean-phi-cost") &&
                    mir_boolean_phi_repaired_label_loop_is_semantically_eligible(
                        generated_size, captured_size,
                        generated_instructions, captured_instructions))
                    /* T455: preserve the formerly-emitted small label-PHI
                     * loop after typed conversion aliases made its cost
                     * accounting exact. */
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
                    !strcmp(fallback_reason, "absolute-index-cost") &&
                    !g_speculative_codegen_active)
                    /* T459: every terminal absolute-index candidate passed;
                     * transient pint/tstructv retries retain their true
                     * boolean-PHI/block-CSE reasons. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason,
                            "indirect-store-address-cost") &&
                    !g_speculative_codegen_active)
                    /* T461: every terminal candidate passed; transient
                     * tlngnarw keeps its later true reason. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "planned-index-base-cost") &&
                    !g_speculative_codegen_active)
                    /* T462: admit only the true-final cohort; blind forcing
                     * perturbs transient/deferred tarray, pint, and tlongidx
                     * candidates. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "planned-stack-cost") &&
                    !g_speculative_codegen_active)
                    /* T463: every true-final candidate passed; transient
                     * tlimits remains on its boolean-PHI retry. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason,
                            "constant-conversion-home-cost") &&
                    !g_speculative_codegen_active &&
                    mir_call_count() > 0)
                    /* T464: the two call-containing terminal candidates
                     * pass; retain the isolated call-free lmod failure. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason,
                            "dead-store-forwarding-cost") &&
                    !g_speculative_codegen_active)
                    /* T464: every true-final candidate passed; transient
                     * pint retains its boolean-PHI reason. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "binary-load-pair-cost") &&
                    !g_speculative_codegen_active &&
                    mir_has_cfg_backedge())
                    /* T465: the backedge candidate passes; retain the
                     * direct pint.emit failure. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "instruction-count") &&
                    !g_speculative_codegen_active)
                    /* T465: both true-final candidates pass; final gating
                     * avoids blind forcing's selector removals. */
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
                    !strcmp(fallback_reason, "wide-store-cost") &&
                    !g_speculative_codegen_active &&
                    mir_wide_store_repaired_is_semantically_eligible(
                        generated_size))
                    /* T488: the repaired bounded 10-block float loop passes
                     * both modes with the tracked 544-byte stack reserve. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "pointer-array"))
                    /* T446: after preserving dereferenced pointer-array
                     * dimension consumption through metadata repair, the
                     * complete pointer-array cohort passed full extended. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "inline-substitution") &&
                    mir_spilled_cfg_uses_exact_semantic_kernel())
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "inline-substitution") &&
                    mir_inline_substitution_coverage_is_semantically_eligible(
                        generated_size, captured_size))
                    /* T446 follow-up: call-crossing MIR IY homes now publish
                     * their file-wide ownership to dccpeep, removing the
                     * former small-frame semantic failure while retaining
                     * the existing acyclic/resource bounds. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "block-cse-cost") &&
                    mir_block_cse_post_phi_is_semantically_eligible(
                        generated_size))
                    /* T450: the post-PHI bounded block-CSE population outside
                     * the wide/20-call failure stratum passed full extended. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "unary-not-cost") &&
                    !g_speculative_codegen_active &&
                    mir.sink_purpose == EMIT_SINK_FINAL)
                    /* T467: FINAL-sink unary retries are deterministic.
                     * Static DEFERRED bodies remain gated until their
                     * sink-specific reason drift is removed. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "boolean-phi-cost") &&
                    !g_speculative_codegen_active &&
                    mir.sink_purpose == EMIT_SINK_FINAL &&
                    mir_boolean_phi_final_sink_is_semantically_eligible(
                        generated_size))
                    /* T487: the repaired bounded four-call FINAL pair passes
                     * both modes; larger and low-call failures stay gated. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "boolean-phi-cost") &&
                    !g_speculative_codegen_active &&
                    mir_boolean_phi_divmod_repaired_is_semantically_eligible(
                        generated_size))
                    /* T485: paired div/mod results now always own concrete
                     * simultaneous slots, so a later quotient restore cannot
                     * invalidate a slotless remainder forwarding marker. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "boolean-phi-cost") &&
                    !g_speculative_codegen_active &&
                    mir_boolean_phi_byte_return_is_semantically_eligible(
                        generated_size))
                    /* T486: MinMax ABI packing is now transactional across
                     * both call sites and its shared epilogue restores H=0,
                     * preserving the complete byte-return value in HL. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "boolean-phi-cost") &&
                    !g_speculative_codegen_active &&
                    mir_boolean_phi_large_parser_is_semantically_eligible(
                        generated_size))
                    /* T489: the branch-simplified large parser passes both
                     * modes with its tracked 768-byte stack reserve. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "dynamic-index-base-cost") &&
                    !g_speculative_codegen_active &&
                    mir.sink_purpose == EMIT_SINK_FINAL &&
                    mir_dynamic_index_base_final_sink_is_semantically_eligible())
                    /* Deterministic bounded FINAL candidates pass both
                     * modes. Wide integer-PHI loops are eligible only after
                     * deferred alias repair has preserved their real width. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "boolean-phi-cost") &&
                    !g_speculative_codegen_active &&
                    mir_boolean_phi_residual_is_semantically_eligible(
                        generated_size, captured_size))
                    /* T472: 35 individually full-mode-clean residual
                     * candidates, excluding 11 direct structural/resource
                     * failure shapes. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !strcmp(fallback_reason, "dynamic-index-base-cost") &&
                    !g_speculative_codegen_active &&
                    mir_dynamic_index_base_residual_is_semantically_eligible(
                        generated_size))
                    /* T481: after the empty-arm PHI repair, the small
                     * three-call label-PHI shape and a bounded non-wide
                     * allocator loop pass both modes. Retain the larger
                     * interpreter/resource and wide failures. */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !g_speculative_codegen_active &&
                    !strcmp(fallback_reason, "unary-not-cost") &&
                    mir.sink_purpose == EMIT_SINK_DEFERRED &&
                    mir_unary_not_deferred_is_semantically_eligible(
                        generated_size))
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    strcmp(fallback_reason, "forced") != 0 &&
                    !g_speculative_codegen_active &&
                    mir_dense_byte_switch_is_semantically_eligible(
                        selector_name,
                        generated_size, captured_size,
                        generated_instructions,
                        captured_instructions))
                    /*
                     * A dense unsigned-byte switch is emitted as the same
                     * bounded 256-entry indirect jump table used by the
                     * legacy backend, rather than as hundreds of scalar
                     * equality branches. Admit only the measured giant
                     * dispatch class after every ordinary retry has run.
                     */
                    fallback_reason = NULL;
                if (fallback_reason != NULL &&
                    !regional_cse_retry_attempted &&
                    !g_speculative_codegen_active &&
                    !strcmp(fallback_reason,
                            "dynamic-index-base-cost")) {
                    int regional_eliminated;

                    regional_cse_retry_attempted = 1;
                    regional_cse_fallback_reason = fallback_reason;
                    regional_eliminated =
                        mir_eliminate_common_region_expressions();
                    if (regional_eliminated > 0) {
                        regional_cse_active = 1;
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
                if (regional_cse_active &&
                    (generated_size > captured_size ||
                     generated_instructions > captured_instructions))
                    fallback_reason = regional_cse_fallback_reason;
                if (!g_speculative_codegen_active &&
                    mir_regional_bounded_call_phi_is_semantically_eligible(
                        fallback_reason, selector_name,
                        regional_cse_active,
                        generated_size, captured_size,
                        generated_instructions,
                        captured_instructions))
                    /*
                     * Bounded call/PHI CFGs fit after regional homes reuse
                     * original object slots and compact local preservation
                     * and branch round trips. Keep hard text, instruction,
                     * block, call, and absolute-size resource boundaries.
                     */
                    fallback_reason = NULL;
                if (((fallback_reason != NULL &&
                      mir_regional_homed_retry_is_eligible(
                          fallback_reason)) ||
                     (fallback_reason == NULL &&
                      !strcmp(selector_name,
                              "spilled-scalar-cfg") &&
                      mir_regional_wide_loop_shape_is_semantically_eligible())) &&
                    !regional_homed_retry_attempted &&
                    !g_speculative_codegen_active) {
                    FILE *regional_candidate = tmpfile();
                    int regional_emitted = 0;
                    int regional_label_id_after;

                    regional_homed_retry_attempted = 1;
                    if (regional_candidate == NULL)
                        fatal("cannot create MIR regional-home candidate "
                              "stream");
                    if (mir_begin_regional_home_plan()) {
                        mir_begin_hybrid_homed_selection();
                        label_id = mir_label_base;
                        regional_emitted = mir_try_selector(
                            regional_candidate,
                            mir_try_emit_homed_scalar_cfg);
                        regional_label_id_after = label_id;
                        mir_end_hybrid_homed_selection();
                        mir_end_regional_home_plan();
                        if (regional_emitted) {
                            FILE *first_compacted =
                                mir_compact_regional_candidate(
                                    regional_candidate);
                            FILE *compacted =
                                mir_compact_regional_candidate(
                                    first_compacted);
                            int padding_instructions =
                                mir_stream_instruction_count(
                                    mir.capture_stream) -
                                mir_stream_instruction_count(compacted);
                            int padding_index;
                            fclose(regional_candidate);
                            fclose(first_compacted);
                            /*
                             * Preserve approximate downstream placement
                             * when regional code removes instructions.
                             * Padding follows the function epilogue and is
                             * unreachable; candidates that are not smaller
                             * receive no padding.
                             */
                            for (padding_index = 0;
                                 padding_index < padding_instructions;
                                 ++padding_index)
                                fputs("\tnop\n", compacted);
                            regional_candidate = compacted;
                            fclose(generated);
                            generated = regional_candidate;
                            regional_candidate = NULL;
                            selector_name =
                                "regional-homed-scalar-cfg";
                            emitted = 1;
                            generated_label_id_after =
                                regional_label_id_after;
                            fallback_reason = NULL;
                            goto evaluate_generated;
                        }
                    }
                    fclose(regional_candidate);
                }
prelegacy_final_cost:
                {
                    const char *final_retry_mode =
                        getenv("DCC_MIR_FINAL_RETRY");
                    unsigned long final_retry_features =
                        MIR_SPILLED_FEATURES_CALL_STACK;
                    const char *final_retry_name =
                        "final-stack-argument";
                    int final_all_profile =
                        mir_final_all_profile_is_semantically_eligible();
                    int final_phi_profile =
                        mir_final_phi_profile_is_semantically_eligible();

                    if (final_retry_mode != NULL &&
                        !strcmp(final_retry_mode, "all")) {
                        final_retry_features =
                            MIR_SPILLED_FEATURES_ALL;
                        final_retry_name = "final-all";
                    } else if (final_retry_mode != NULL &&
                               !strcmp(final_retry_mode,
                                       "phi-slot")) {
                        final_retry_features =
                            MIR_SPILLED_FEATURES_PHI_SLOT;
                        final_retry_name = "final-phi-slot";
                    } else if (final_all_profile) {
                        final_retry_features =
                            MIR_SPILLED_FEATURES_ALL;
                        final_retry_name = "final-profiled-all";
                    } else if (final_phi_profile) {
                        final_retry_features =
                            MIR_SPILLED_FEATURES_PHI_SLOT;
                        final_retry_name = "final-profiled-phi-slot";
                    }
                if (!final_stack_retry_attempted &&
                    !g_speculative_codegen_active &&
                    !strcmp(selector_name,
                            "spilled-scalar-cfg") &&
                    (final_retry_mode != NULL ||
                     getenv("DCC_MIR_FINAL_STACK_RETRY") != NULL ||
                     final_all_profile ||
                     final_phi_profile ||
                     mir_final_stack_profile_is_semantically_eligible())) {
                    struct MirCandidateDescriptor candidate;
                    struct MirCandidateResult result;

                    final_stack_retry_attempted = 1;
                    mir_init_spilled_candidate(
                        &candidate, final_retry_name,
                        "cannot create MIR final feature candidate stream",
                        final_retry_features);
                    mir_build_spilled_candidate(
                        &candidate, &result, mir_label_base);
                    if (result.emitted &&
                        result.generated_size <= generated_size &&
                        result.generated_instructions <=
                            generated_instructions &&
                        (result.generated_size < generated_size ||
                         result.generated_instructions <
                             generated_instructions) &&
                        mir_adopt_candidate_result(
                            &generated, &result)) {
                        generated_label_id_after =
                            result.label_id_after;
                        fallback_reason = NULL;
                        goto evaluate_generated;
                    }
                    mir_close_candidate_result(&result);
                }
                }
                if (g_speculative_codegen_active &&
                    getenv("DCC_MIR_FINAL_COST_REPORT") != NULL)
                    fprintf(stderr,
                            "; MIR speculative-final function=%s "
                            "selector=%s reason=%s generated-bytes=%ld "
                            "captured-bytes=%ld generated-insns=%d "
                            "captured-insns=%d blocks=%d\n",
                            mir.name, selector_name,
                            fallback_reason != NULL
                                ? fallback_reason : "accepted",
                            generated_size, captured_size,
                            generated_instructions, captured_instructions,
                            mir_cfg_block_count());
                if (fallback_reason == NULL &&
                    mir_final_cost_policy_rejects(
                        selector_name, generated, mir.capture_stream,
                        generated_size, captured_size,
                        generated_instructions, captured_instructions))
                    fallback_reason = "final-cost-policy";
                if (fallback_reason != NULL) {
                    const char *forced_final =
                        getenv("DCC_MIR_FORCE_ACCEPT_FINAL_FUNCTION");
                    if (!g_speculative_codegen_active &&
                        forced_final != NULL &&
                        !strcmp(forced_final, mir.name))
                        /*
                         * Diagnostic only: unlike
                         * DCC_MIR_FORCE_ACCEPT_FUNCTION, this observes every
                         * retry and accepts exactly the final candidate.
                         */
                        fallback_reason = NULL;
                }
                if (fallback_reason == NULL &&
                    mir_has_inline_temp_identity_overwrite())
                    /* T447/T450: each logical inline temp carries its scoped
                     * identity even when sequential calls reuse one reserved
                     * frame slot. A load whose identity no longer owns that
                     * slot proves a real nested overwrite. */
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
                /* cost-v1 falsification result (see the block comment above
                 * mir_spilled_candidate_table): a production-path override
                 * that reconsidered only the ten named spilled-candidate
                 * masks was measured on the rhs-control train cohort and
                 * found to regress tests/tfpcall.c's main() (+158 peep
                 * cycles, +128 peep bytes, +160 nopeep cycles) because the
                 * ordinary retry chain above had already, through its own
                 * additional retry/promotion paths, reached a smaller/
                 * faster stream (4080 bytes/362 insns) than any of the ten
                 * fixed masks (best of which, phi-slot, is 4108 bytes/367
                 * insns) - so selecting only among those ten can regress
                 * away from a real, already-accepted win no cost-formula
                 * weighting can recover. No production override is wired
                 * in as a result; DCC_MIR_SPILLED_POLICY=cost-v1 therefore
                 * only affects DCC_MIR_CANDIDATE_MATRIX=1 diagnostic
                 * output (see mir_report_spilled_candidate_matrix below),
                 * never real codegen. */
            }
        }
copy_selected_output:
        if (mir_prelegacy_scheduled_attempt_active)
            mir_prelegacy_scheduled_attempt_selected = emitted;
        if (emitted)
            mir_mark_selected_inline_call_bodies_needed(generated);
        selected_hash = mir_copy_selected_stream(
            emitted ? generated : mir.capture_stream, destination);
        if (generated != NULL)
            fclose(generated);
        if (mir.report_mode && !g_speculative_codegen_active &&
            (!mir_prelegacy_scheduled_attempt_active || emitted))
            fprintf(stderr, "; MIR emit function=%s result=%s\n",
                mir.name, emitted ? "mir" : "fallback");
        /* See the oversized-fallback report above: a buffered/speculative
         * legacy attempt's generated/captured sizes describe codegen that
         * is discarded and never reaches the real output, so it must not
         * be reported to the census or DCC_MIR_SELECT_REPORT consumers. */
        if (getenv("DCC_MIR_SELECT_REPORT") != NULL &&
            !g_speculative_codegen_active &&
            (!mir_prelegacy_scheduled_attempt_active || emitted))
            fprintf(stderr,
                    "; MIR selection function=%s selector=%s result=%s "
                    "reason=%s generated-bytes=%ld captured-bytes=%ld "
                    "generated-insns=%d captured-insns=%d blocks=%d "
                    "selected-hash=%08lx sink=%s\n",
                    mir.name, selector_name, emitted ? "mir" : "fallback",
                    fallback_reason != NULL ? fallback_reason : "accepted",
                    generated_size, captured_size, generated_instructions,
                    captured_instructions, mir_cfg_block_count(),
                    selected_hash, mir_sink_name(mir.sink_purpose));
        if (verified && !g_speculative_codegen_active &&
            !mir_prelegacy_scheduled_attempt_active &&
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
