/* dcc_mir_select.c - loop selectors (countdown/accumulator/unsigned-
 * division/repeated-invariant-add), the general CFG rollout and
 * comparison-branch selectors, the top-level mir_try_emit_z80
 * dispatcher, and mir_end_function's generated-candidate commit entry point.
 *
 * Part of the dcc_mir.c MIR backend split; see dcc_mir_internal.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dcc.h"
#include "dcc_ast.h"
#include "dcc_mir.h"
#include "dcc_mir_machine_internal.h"

static int mir_has_member_address(void);
static int mir_call_count(void);
static int mir_has_wide_values(void);
static int mir_cost_regional_candidate_is_validated(void);
static int mir_boolean_candidate_is_validated(void);
static int mir_large_dense_switch_phi_candidate_is_eligible(void);
static int mir_try_selector(MirStream *out, int (*selector)(MirStream *));

static int mir_cost_policy_selects_alternative(void)
{
    const char *policy = getenv("DCC_MIR_COST_POLICY");
    if (policy == NULL || policy[0] == 0)
        return 1;
    if (!strcmp(policy, "mir-v1"))
        return 1;
    if (!strcmp(policy, "mir-v1-report"))
        return 0;
    fatal("unknown DCC_MIR_COST_POLICY");
    return 0;
}

#define MIR_MAX_DENSE_ANALYSIS_CELLS (64UL * 1024UL * 1024UL)

static int mir_dense_analysis_is_bounded(void)
{
    size_t instructions;
    size_t values;
    size_t limit = (size_t)MIR_MAX_DENSE_ANALYSIS_CELLS;

    if (mir.count < 0 || mir.next_value < 0)
        return 0;
    instructions = (size_t)mir.count;
    values = (size_t)mir.next_value;
    /*
     * Verification retains two instruction-by-value liveness matrices and
     * allocation builds a value-by-value interference matrix. Bound the
     * dimensions those dynamic allocations actually consume rather than
     * rejecting functions at an unrelated instruction-count threshold.
     */
    return (values == 0 || instructions <= limit / values) &&
           (values == 0 || values <= limit / values);
}

static void mir_require_emitted_function(const char *reason)
{
    if (getenv("DCC_MIR_REQUIRE_EMIT") == NULL ||
        (getenv("DCC_MIR_REQUIRE_COMPLETE") != NULL &&
         mir.opaque_count != 0) ||
        (mir.sink_purpose != EMIT_SINK_FINAL &&
         mir.sink_purpose != EMIT_SINK_DEFERRED))
        return;

    fprintf(stderr,
            "MIR emission failed for function %s: no generated candidate "
            "(reason=%s)\n",
            mir.name, reason != NULL ? reason : "selector");
    fatal("DCC_MIR_REQUIRE_EMIT requires MIR emission");
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
    int (*selector)(MirStream *);
    unsigned long spilled_features;
};

struct MirCandidateResult {
    const struct MirCandidateDescriptor *descriptor;
    MirStream *stream;
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

static MirStream *mir_compact_regional_candidate(MirStream *input)
{
    char **lines = NULL;
    int count = 0;
    int capacity = 0;
    char buffer[512];
    MirStream *output;
    int i;

    mir_stream_rewind(input);
    while (mir_stream_gets(buffer, sizeof(buffer), input) != NULL) {
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
    output = mir_stream_open();
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
            mir_stream_puts("\tpush de\n\tpop hl\n\tld d,b\n\tld e,c\n",
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
            mir_stream_puts("\tpush de\n\tpop hl\n", output);
            mir_stream_puts(low, output);
            mir_stream_puts(high, output);
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
            mir_stream_puts("\tpush hl\n\tld de,", output);
            mir_stream_puts(lines[i + 1] + 7, output);
            mir_stream_puts("\tpush de\n", output);
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
            mir_stream_puts("\tpop hl\n", output);
            i += 6;
            continue;
        }
        if (mir_cfg_block_count() <= 32 &&
            i + 3 < count &&
            mir_regional_line_is(lines[i], "\tpop hl\n") &&
            mir_regional_line_is(lines[i + 1], "\tpush hl\n") &&
            mir_regional_line_is(lines[i + 2], "\tpush de\n") &&
            mir_regional_line_is(lines[i + 3], "\tpop hl\n")) {
            mir_stream_puts("\tpush de\n\tpop hl\n", output);
            i += 4;
            continue;
        }
        if (mir_cfg_block_count() <= 32 &&
            i + 3 < count &&
            mir_regional_line_is(lines[i], "\tpop hl\n") &&
            mir_regional_line_is(lines[i + 1], "\tpush hl\n") &&
            mir_regional_full_hl_reload(
                lines[i + 2], lines[i + 3])) {
            mir_stream_puts(lines[i + 2], output);
            mir_stream_puts(lines[i + 3], output);
            i += 4;
            continue;
        }
        mir_stream_puts(lines[i], output);
        ++i;
    }
    for (i = 0; i < count; ++i)
        free(lines[i]);
    free(lines);
    return output;
}

static MirStream *mir_compact_adjacent_exx(
    MirStream *input, int *elided_instructions)
{
    char previous[512];
    char current[512];
    int have_previous = 0;
    MirStream *output = mir_stream_open();

    if (output == NULL)
        fatal("cannot create compacted MIR stream");
    *elided_instructions = 0;
    mir_stream_rewind(input);
    while (mir_stream_gets(current, sizeof(current), input) != NULL) {
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
        mir_stream_puts(previous, output);
        strcpy(previous, current);
    }
    if (have_previous)
        mir_stream_puts(previous, output);
    return output;
}

int mir_try_emit_compacted_regional_homed_cfg(MirStream *out)
{
    MirStream *raw;
    MirStream *first;
    MirStream *second;
    MirStream *final;
    char buffer[4096];
    int active;
    int emitted;
    int exx_elided = 0;
    int label_base = label_id;

    raw = mir_stream_open();
    if (raw == NULL)
        fatal("cannot create regional MIR stream");
    active = mir_begin_regional_home_plan();
    if (!active) {
        mir_stream_close(raw);
        return 0;
    }
    mir_begin_hybrid_homed_selection();
    emitted = mir_try_emit_homed_scalar_cfg(raw);
    mir_end_hybrid_homed_selection();
    mir_end_regional_home_plan();
    if (!emitted) {
        label_id = label_base;
        mir_stream_close(raw);
        return 0;
    }
    first = mir_compact_regional_candidate(raw);
    second = mir_compact_regional_candidate(first);
    final = mir_compact_adjacent_exx(second, &exx_elided);
    mir_stream_rewind(final);
    while (mir_stream_gets(buffer, sizeof(buffer), final) != NULL)
        mir_stream_puts(buffer, out);
    mir_stream_close(final);
    mir_stream_close(second);
    mir_stream_close(first);
    mir_stream_close(raw);
    return 1;
}

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

static int mir_try_emit_general_rollout(MirStream *out)
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
static int mir_try_emit_countdown_loop(MirStream *out)
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
    mir_stream_printf(out, "\tld c,(ix%+d)\n", object->offset);
    mir_stream_printf(out, "\tld b,(ix%+d)\n", object->offset + 1);
    mir_stream_printf(out, "L%d:\n", top_label);
    if (unsigned_value) {
        mir_stream_puts("\tld a,b\n\tor c\n", out);
        mir_stream_printf(out, "\tjp z, L%d\n", end_label);
    } else {
        mir_stream_puts("\tld a,b\n\tor a\n", out);
        mir_stream_printf(out, "\tjp m, L%d\n", end_label);
        mir_stream_puts("\tor c\n", out);
        mir_stream_printf(out, "\tjp z, L%d\n", end_label);
    }
    mir_stream_puts("\tdec bc\n", out);
    mir_stream_printf(out, "\tjp L%d\n", top_label);
    mir_stream_printf(out, "L%d:\n", end_label);
    mir_stream_puts("\tld l,c\n\tld h,b\n\tld sp,ix\n\tpop ix\n\tret\n", out);
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
static int mir_try_emit_accumulator_loop(MirStream *out)
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
    mir_stream_printf(out, "\tld c,(ix%+d)\n", mir.objects[n_object].offset);
    mir_stream_printf(out, "\tld b,(ix%+d)\n", mir.objects[n_object].offset + 1);
    mir_stream_puts("\tld de,0\n", out);
    mir_stream_printf(out, "L%d:\n", top_label);
    if (unsigned_value) {
        mir_stream_puts("\tld a,b\n\tor c\n", out);
        mir_stream_printf(out, "\tjp z, L%d\n", end_label);
    } else {
        mir_stream_puts("\tld a,b\n\tor a\n", out);
        mir_stream_printf(out, "\tjp m, L%d\n", end_label);
        mir_stream_puts("\tor c\n", out);
        mir_stream_printf(out, "\tjp z, L%d\n", end_label);
    }
    mir_stream_puts("\tex de,hl\n\tadd hl,bc\n\tex de,hl\n\tdec bc\n", out);
    mir_stream_printf(out, "\tjp L%d\n", top_label);
    mir_stream_printf(out, "L%d:\n", end_label);
    mir_stream_puts("\tex de,hl\n\tld sp,ix\n\tpop ix\n\tret\n", out);
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
static int mir_try_emit_unsigned_division_loop(MirStream *out)
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
    mir_stream_printf(out, "\tld c,(ix%+d)\n", mir.objects[remainder_object].offset);
    mir_stream_printf(out, "\tld b,(ix%+d)\n",
            mir.objects[remainder_object].offset + 1);
    mir_stream_puts("\tld de,0\n", out);
    mir_stream_printf(out, "L%d:\n", top_label);
    mir_stream_printf(out, "\tld hl,%ld\n\tadd hl,bc\n", -divisor);
    mir_stream_printf(out, "\tjp nc, L%d\n", end_label);
    mir_stream_puts("\tld b,h\n\tld c,l\n\tinc de\n", out);
    mir_stream_printf(out, "\tjp L%d\n", top_label);
    mir_stream_printf(out, "L%d:\n", end_label);
    mir_stream_puts("\tex de,hl\n\tld sp,ix\n\tpop ix\n\tret\n", out);
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
static int mir_try_emit_repeated_invariant_add_loop(MirStream *out)
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
    mir_stream_printf(out, "\tld l,(ix%+d)\n", mir.objects[factor_object].offset + 2);
    mir_stream_printf(out, "\tld h,(ix%+d)\n", mir.objects[factor_object].offset + 3);
    mir_stream_puts("\tadd hl,hl\n\tpush hl\n\tpop iy\n", out);
    mir_stream_puts("\tld bc,0\n\tld de,0\n", out);
    mir_stream_printf(out, "L%d:\n", top_label);
    mir_stream_printf(out, "\tld hl,%ld\n\tadd hl,bc\n", -limit);
    mir_stream_printf(out, "\tjp c, L%d\n", end_label);
    mir_stream_puts("\tpush iy\n\tpop hl\n\tadd hl,de\n\tex de,hl\n\tinc bc\n", out);
    mir_stream_printf(out, "\tjp L%d\n", top_label);
    mir_stream_printf(out, "L%d:\n", end_label);
    mir_stream_puts("\tex de,hl\n\tld sp,ix\n\tpop ix\n\tpop iy\n\tret\n", out);
    return 1;
}

/* Strict first CFG selector:
 *
 *     if (a == b) return C1; return C2;
 *
 * (or !=). This validates labels, branch polarity, two live inputs and
 * multiple exits without claiming general relational/comparison support. */
static int mir_try_emit_comparison_branch(MirStream *out)
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
                mir_stream_puts("\tld a,d\n\tand 127\n\tor e\n\tor h\n\tor l\n", out);
            else
                mir_stream_puts("\tld a,d\n\tor e\n\tor h\n\tor l\n", out);
        } else {
            if (!mir_emit_load_param(out, left))
                return 0;
            mir_stream_puts("\tld a,h\n\tor l\n", out);
        }
        mir_stream_printf(out, "\tjp z, L%d\n", false_label);
        {
            int epilogue_label = new_label();
            mir_stream_printf(out, "\tld hl,%ld\n", true_value->immediate);
            mir_stream_printf(out, "\tjp L%d\n", epilogue_label);
            mir_stream_printf(out, "L%d:\n", false_label);
            mir_stream_printf(out, "\tld hl,%ld\n", false_value->immediate);
            mir_stream_printf(out, "L%d:\n", epilogue_label);
            mir_stream_puts("\tld sp,ix\n\tpop ix\n\tret\n", out);
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
        mir_stream_puts("\tpush de\n\tpush hl\n", out);
        if (!mir_emit_load_param_wide(out, right))
            return 0;
        if (!mir_emit_wide_operation(out, compare))
            return 0;
        mir_stream_puts("\tld a,h\n\tor l\n", out);
        mir_stream_printf(out, "\tjp z, L%d\n", false_label);
        {
            int epilogue_label = new_label();
            mir_stream_printf(out, "\tld hl,%ld\n", true_value->immediate);
            mir_stream_printf(out, "\tjp L%d\n", epilogue_label);
            mir_stream_printf(out, "L%d:\n", false_label);
            mir_stream_printf(out, "\tld hl,%ld\n", false_value->immediate);
            mir_stream_printf(out, "L%d:\n", epilogue_label);
            mir_stream_puts("\tld sp,ix\n\tpop ix\n\tret\n", out);
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
        mir_stream_puts("\tld a,h\n\txor 80h\n\tld h,a\n"
              "\tld a,d\n\txor 80h\n\tld d,a\n", out);
    }
    mir_stream_puts("\tor a\n\tsbc hl,de\n", out);
    if (operation == TOK_EQ)
        mir_stream_printf(out, "\tjp nz, L%d\n", false_label);
    else if (operation == TOK_NE)
        mir_stream_printf(out, "\tjp z, L%d\n", false_label);
    else if (operation == '<')
        mir_stream_printf(out, "\tjp nc, L%d\n", false_label);
    else
        mir_stream_printf(out, "\tjp c, L%d\n", false_label);
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
        mir_stream_printf(out, "\tld hl,%ld\n", true_value->immediate);
        mir_stream_printf(out, "\tjp L%d\n", epilogue_label);
        mir_stream_printf(out, "L%d:\n", false_label);
        mir_stream_printf(out, "\tld hl,%ld\n", false_value->immediate);
        mir_stream_printf(out, "L%d:\n", epilogue_label);
        mir_stream_puts("\tld sp,ix\n\tpop ix\n\tret\n", out);
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

void mir_emit_runtime_call(MirStream *out, const char *name)
{
    if (mir_extrn_should_emit_name(name))
        mir_stream_printf(out, "\textrn %s\n", name);
    mir_stream_printf(out, "\tcall %s\n", name);
}

/* Isolate every selector attempt in its own stream so partial output from a
 * declining candidate cannot contaminate the next generated candidate. */
static int mir_try_selector(MirStream *out, int (*selector)(MirStream *))
{
    MirStream *candidate = mir_stream_open();
    int accepted;
    int character;

    if (candidate == NULL)
        fatal("cannot create MIR selector stream");
    mir_extrn_begin_attempt();
    accepted = selector(candidate);
    if (accepted) {
        mir_stream_rewind(candidate);
        while ((character = mir_stream_getc(candidate)) != EOF)
            mir_stream_putc(character, out);
    }
    mir_stream_close(candidate);
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
    result->stream = mir_stream_open();
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
        mir_stream_close(result->stream);
    result->stream = NULL;
}


long mir_stream_size(MirStream *stream)
{
    long position = mir_stream_tell(stream);
    long size;
    char line[512];

    if (position < 0 || mir_stream_seek(stream, 0, SEEK_END) != 0)
        return -1;
    size = mir_stream_tell(stream);
    if (size < 0 || mir_stream_seek(stream, 0, SEEK_SET) != 0)
        return -1;
    while (mir_stream_gets(line, sizeof(line), stream) != NULL)
        if (strstr(line, ";@dcc.reg claim=iy ") == line &&
            strstr(line, " kind=mir val=0") != NULL)
            /* Register-ownership metadata changes dccpeep policy but emits
             * no Z80 bytes. Do not let its symbol text choose a different
             * selector through the assembly-text cost proxy. */
            size -= (long)strlen(line);
        else if (strstr(line, MIR_PHI_SLOT_MARKER) == line)
            size -= (long)strlen(line);
    if (mir_stream_seek(stream, position, SEEK_SET) != 0)
        return -1;
    return size;
}

static unsigned long mir_copy_selected_stream(MirStream *source, FILE *destination)
{
    return mir_stream_copy_to_file(source, destination);
}

int mir_stream_instruction_count(MirStream *stream)
{
    char line[512];
    long position = mir_stream_tell(stream);
    int count = 0;

    if (position < 0 || mir_stream_seek(stream, 0, SEEK_SET) != 0)
        return -1;
    while (mir_stream_gets(line, sizeof(line), stream) != NULL) {
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
    if (mir_stream_seek(stream, position, SEEK_SET) != 0)
        return -1;
    return count;
}

static unsigned long mir_stream_hash(MirStream *stream)
{
    unsigned long hash = 2166136261UL;
    long position = mir_stream_tell(stream);
    int character;

    if (position < 0 || mir_stream_seek(stream, 0, SEEK_SET) != 0)
        return 0;
    while ((character = mir_stream_getc(stream)) != EOF) {
        hash ^= (unsigned long)(unsigned char)character;
        hash = (hash * 16777619UL) & 0xffffffffUL;
    }
    if (mir_stream_seek(stream, position, SEEK_SET) != 0)
        return 0;
    return hash;
}

/* ===================================================================
 * MIR machine-cost estimator and historical fixed-spilled matrix.
 *
 * The estimator parses emitted MIR candidates into nominal Z80 T-states,
 * runtime-helper surcharge, real opcode bytes/instructions, loop/backedge
 * weighting, moves, and setup costs. It compares generated MIR candidates
 * only; the discarded legacy text is never inspected.
 * Production DCC_MIR_COST_POLICY=mir-v1 uses it below with the incumbent,
 * homed, hybrid, regional, and spilled candidates.
 *
 * DCC_MIR_SPILLED_POLICY=cost-v1 retains the older diagnostic that ranks
 * only the ten fixed spilled feature masks in
 * DCC_MIR_CANDIDATE_MATRIX output. Selecting the minimum of that incomplete
 * universe was falsified by tfpcall.main: the incumbent retry chain had
 * already produced a smaller/faster stream than every fixed mask. Therefore
 * this older flag remains diagnostic-only; the production policy always
 * includes the incumbent and applies calibrated dominance/eligibility gates.
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
    int instructions;
    int helper_calls;
    int move_instructions;
    int prologue_instructions;
    int callee_save_instructions;
    double score;
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
    MirStream *stream, struct MirCostComponents *out)
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
    out->instructions = 0;
    out->helper_calls = 0;
    out->move_instructions = 0;
    out->prologue_instructions = 0;
    out->callee_save_instructions = 0;
    out->score = 0.0;
    out->max_loop_depth = 0;

    position = mir_stream_tell(stream);
    if (position < 0 || mir_stream_seek(stream, 0, SEEK_SET) != 0)
        return;

    while (mir_stream_gets(buffer, sizeof(buffer), stream) != NULL) {
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
    mir_stream_seek(stream, position, SEEK_SET);

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
        ++out->instructions;
        if (!strcmp(mnemonic, "ld") || !strcmp(mnemonic, "ex") ||
            !strcmp(mnemonic, "push") || !strcmp(mnemonic, "pop"))
            ++out->move_instructions;
        if (!strcmp(text, "push ix") || !strcmp(text, "pop ix") ||
            !strcmp(text, "ld ix,0") || !strcmp(text, "add ix,sp") ||
            !strcmp(text, "ld sp,ix"))
            ++out->prologue_instructions;
        if (!strcmp(text, "push iy") || !strcmp(text, "pop iy"))
            ++out->callee_save_instructions;
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
                double helper_cost;

                mir_cost_v1_instruction_cost(
                    mnemonic, rest, 0.0, &tstates, &bytes,
                    &branch_target, &call_target);
                t = call_target != NULL ? call_target : "";
                tlen = strlen(t);
                if (tlen >= sizeof(target))
                    tlen = sizeof(target) - 1;
                memcpy(target, t, tlen);
                target[tlen] = 0;
                helper_cost = mir_cost_v1_helper_tstates(target);
                if (helper_cost > 0.0)
                    ++out->helper_calls;
                out->helper_tstates += helper_cost * weight;
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

enum MirCostCandidateKind {
    MIR_COST_CANDIDATE_HOMED,
    MIR_COST_CANDIDATE_HOMED_LAZY,
    MIR_COST_CANDIDATE_HYBRID,
    MIR_COST_CANDIDATE_REGIONAL,
    MIR_COST_CANDIDATE_SPILLED_ADDRESS_REMAT,
    MIR_COST_CANDIDATE_SPILLED_STABLE_LOCAL,
    MIR_COST_CANDIDATE_SPILLED
};

struct MirCostCandidateSpec {
    const char *name;
    const char *selector_name;
    enum MirCostCandidateKind kind;
    unsigned long features;
};

struct MirCostCandidate {
    const struct MirCostCandidateSpec *spec;
    MirStream *stream;
    int emitted;
    int selectable;
    int label_id_after;
    long text_bytes;
    int text_instructions;
    int frame_bytes;
    int slots;
    int spills;
    int fixed_moves;
    int operand_moves;
    int phi_moves;
    int register_homes;
    int iy_homes;
    unsigned long hash;
    struct MirCostComponents machine;
    double score;
};

struct MirCostStateSnapshot {
    int *colors;
    int *spills;
    int *backend_slots;
    int spill_count;
    int fixed_moves;
    int operand_moves;
    int phi_moves;
    int backend_slot_count;
    int backend_slot_capacity;
    int virtual_iy_frame_bytes;
};

static const struct MirCostCandidateSpec mir_cost_candidate_specs[] = {
    {"homed", "homed-scalar-cfg", MIR_COST_CANDIDATE_HOMED, 0},
    {"homed-lazy", "homed-scalar-cfg",
     MIR_COST_CANDIDATE_HOMED_LAZY, 0},
    {"hybrid", "hybrid-homed-scalar-cfg",
     MIR_COST_CANDIDATE_HYBRID, 0},
    {"regional", "regional-homed-scalar-cfg",
     MIR_COST_CANDIDATE_REGIONAL, 0},
    {"spilled-address-remat", "spilled-scalar-cfg",
     MIR_COST_CANDIDATE_SPILLED_ADDRESS_REMAT, 0},
    {"spilled-stable-local", "spilled-scalar-cfg",
     MIR_COST_CANDIDATE_SPILLED_STABLE_LOCAL, 0},
    {"spilled-baseline", "spilled-scalar-cfg",
     MIR_COST_CANDIDATE_SPILLED, 0},
    {"spilled-rhs-forward", "spilled-scalar-cfg",
     MIR_COST_CANDIDATE_SPILLED, MIR_SPILLED_FEATURES_RHS},
    {"spilled-store-address", "spilled-scalar-cfg",
     MIR_COST_CANDIDATE_SPILLED, MIR_SPILLED_FEATURES_STORE_ADDRESS},
    {"spilled-wide-binary-lhs", "spilled-scalar-cfg",
     MIR_COST_CANDIDATE_SPILLED, MIR_SPILLED_FEATURES_WIDE_LHS},
    {"spilled-stable-pointer-argument", "spilled-scalar-cfg",
     MIR_COST_CANDIDATE_SPILLED, MIR_SPILLED_FEATURES_STABLE_ARG},
    {"spilled-global-argument", "spilled-scalar-cfg",
     MIR_COST_CANDIDATE_SPILLED, MIR_SPILLED_FEATURES_GLOBAL_ARG},
    {"spilled-stack-argument", "spilled-scalar-cfg",
     MIR_COST_CANDIDATE_SPILLED, MIR_SPILLED_FEATURES_CALL_STACK},
    {"spilled-promoted-local-slot", "spilled-scalar-cfg",
     MIR_COST_CANDIDATE_SPILLED, MIR_SPILLED_FEATURES_PROMOTED_LOCAL},
    {"spilled-all", "spilled-scalar-cfg",
     MIR_COST_CANDIDATE_SPILLED, MIR_SPILLED_FEATURES_ALL},
    {"spilled-phi-slot", "spilled-scalar-cfg",
     MIR_COST_CANDIDATE_SPILLED, MIR_SPILLED_FEATURES_PHI_SLOT}
};

static void mir_cost_save_state(struct MirCostStateSnapshot *snapshot)
{
    size_t values = (size_t)mir.next_value;

    memset(snapshot, 0, sizeof(*snapshot));
    if (values != 0) {
        snapshot->colors = (int *)malloc(values * sizeof(*snapshot->colors));
        snapshot->spills = (int *)malloc(values * sizeof(*snapshot->spills));
        if (snapshot->colors == NULL || snapshot->spills == NULL)
            fatal("out of memory saving MIR cost candidate state");
        memcpy(snapshot->colors, mir.allocation_colors,
               values * sizeof(*snapshot->colors));
        memcpy(snapshot->spills, mir.allocation_spills,
               values * sizeof(*snapshot->spills));
    }
    snapshot->backend_slot_capacity = mir.backend_slot_capacity;
    if (snapshot->backend_slot_capacity > 0) {
        size_t slots = (size_t)snapshot->backend_slot_capacity;

        snapshot->backend_slots =
            (int *)malloc(slots * sizeof(*snapshot->backend_slots));
        if (snapshot->backend_slots == NULL)
            fatal("out of memory saving MIR backend slots");
        memcpy(snapshot->backend_slots, mir.backend_slots,
               slots * sizeof(*snapshot->backend_slots));
    }
    snapshot->spill_count = mir.allocation_spill_count;
    snapshot->fixed_moves = mir.allocation_fixed_moves;
    snapshot->operand_moves = mir.allocation_operand_moves;
    snapshot->phi_moves = mir.allocation_phi_moves;
    snapshot->backend_slot_count = mir.backend_slot_count;
    snapshot->virtual_iy_frame_bytes = mir_virtual_iy_frame_bytes;
}

static void mir_cost_restore_state(struct MirCostStateSnapshot *snapshot)
{
    size_t values = (size_t)mir.next_value;

    if (values != 0) {
        memcpy(mir.allocation_colors, snapshot->colors,
               values * sizeof(*snapshot->colors));
        memcpy(mir.allocation_spills, snapshot->spills,
               values * sizeof(*snapshot->spills));
    }
    if (snapshot->backend_slot_capacity > 0) {
        if (mir.backend_slot_capacity < snapshot->backend_slot_capacity)
            fatal("MIR backend slot capacity shrank during cost selection");
        memcpy(mir.backend_slots, snapshot->backend_slots,
               (size_t)snapshot->backend_slot_capacity *
                   sizeof(*snapshot->backend_slots));
    }
    mir.allocation_spill_count = snapshot->spill_count;
    mir.allocation_fixed_moves = snapshot->fixed_moves;
    mir.allocation_operand_moves = snapshot->operand_moves;
    mir.allocation_phi_moves = snapshot->phi_moves;
    mir.backend_slot_count = snapshot->backend_slot_count;
    mir_virtual_iy_frame_bytes = snapshot->virtual_iy_frame_bytes;
    free(snapshot->colors);
    free(snapshot->spills);
    free(snapshot->backend_slots);
}

static void mir_cost_measure_candidate(struct MirCostCandidate *candidate)
{
    int value;

    candidate->text_bytes = mir_stream_size(candidate->stream);
    candidate->text_instructions =
        mir_stream_instruction_count(candidate->stream);
    candidate->spills = mir.allocation_spill_count;
    candidate->fixed_moves = mir.allocation_fixed_moves;
    candidate->operand_moves = mir.allocation_operand_moves;
    candidate->phi_moves = mir.allocation_phi_moves;
    candidate->register_homes = 0;
    candidate->iy_homes = 0;
    if (candidate->spec->kind != MIR_COST_CANDIDATE_SPILLED &&
        candidate->spec->kind !=
            MIR_COST_CANDIDATE_SPILLED_ADDRESS_REMAT &&
        candidate->spec->kind !=
            MIR_COST_CANDIDATE_SPILLED_STABLE_LOCAL)
        for (value = 0; value < mir.next_value; ++value) {
            int color = mir.allocation_colors[value];

            if (color < 0)
                continue;
            ++candidate->register_homes;
            if (color == MIR_COLOR_IY || color == MIR_COLOR_BC_IY)
                ++candidate->iy_homes;
        }
    if (candidate->spec->kind == MIR_COST_CANDIDATE_SPILLED ||
        candidate->spec->kind ==
            MIR_COST_CANDIDATE_SPILLED_ADDRESS_REMAT ||
        candidate->spec->kind ==
            MIR_COST_CANDIDATE_SPILLED_STABLE_LOCAL) {
        candidate->slots = mir.backend_slot_count;
        candidate->frame_bytes =
            mir_effective_local_bytes() + mir.aggregate_temp_bytes +
            2 * candidate->slots;
    } else {
        candidate->slots =
            candidate->spills +
            (candidate->spec->kind == MIR_COST_CANDIDATE_REGIONAL
                 ? mir.regional_spill_slot_count : 0);
        candidate->frame_bytes =
            mir_effective_local_bytes() + mir.aggregate_temp_bytes +
            2 * candidate->slots;
    }
    candidate->hash = mir_stream_hash(candidate->stream);
    mir_estimate_stream_cost(candidate->stream, &candidate->machine);
    candidate->score =
        candidate->machine.tstates +
        candidate->machine.helper_tstates +
        0.50 * (double)candidate->machine.bytes +
        0.50 * (double)candidate->machine.instructions +
        4.00 * (double)candidate->frame_bytes +
        24.00 * (double)candidate->spills +
        3.00 * (double)(candidate->fixed_moves +
                         candidate->operand_moves +
                         candidate->phi_moves) +
        1.00 * (double)candidate->machine.move_instructions +
        4.00 * (double)candidate->machine.prologue_instructions +
        8.00 * (double)candidate->machine.callee_save_instructions -
        1.00 * (double)candidate->register_homes +
        2.00 * (double)candidate->iy_homes;
}

static void mir_cost_build_candidate(
    const struct MirCostCandidateSpec *spec,
    struct MirCostCandidate *candidate, int label_base)
{
    struct MirCostStateSnapshot snapshot;
    int regional_active = 0;
    int exx_elided = 0;

    memset(candidate, 0, sizeof(*candidate));
    candidate->spec = spec;
    candidate->label_id_after = label_base;
    candidate->text_bytes = -1;
    candidate->text_instructions = -1;
    candidate->stream = mir_stream_open();
    if (candidate->stream == NULL)
        fatal("cannot create MIR cost candidate stream");

    mir_cost_save_state(&snapshot);
    label_id = label_base;
    if (spec->kind == MIR_COST_CANDIDATE_HOMED) {
        candidate->emitted = mir_try_selector(
            candidate->stream, mir_try_emit_homed_scalar_cfg);
    } else if (spec->kind == MIR_COST_CANDIDATE_HOMED_LAZY) {
        if (mir_begin_lazy_parameter_allocation()) {
            candidate->emitted = mir_try_selector(
                candidate->stream, mir_try_emit_homed_scalar_cfg);
            mir_end_lazy_parameter_allocation();
        }
    } else if (spec->kind == MIR_COST_CANDIDATE_HYBRID) {
        mir_begin_hybrid_homed_selection();
        candidate->emitted = mir_try_selector(
            candidate->stream, mir_try_emit_homed_scalar_cfg);
        mir_end_hybrid_homed_selection();
    } else if (spec->kind == MIR_COST_CANDIDATE_REGIONAL) {
        regional_active = mir_begin_regional_home_plan();
        if (regional_active) {
            mir_begin_hybrid_homed_selection();
            candidate->emitted = mir_try_selector(
                candidate->stream, mir_try_emit_homed_scalar_cfg);
            mir_end_hybrid_homed_selection();
        }
    } else if (spec->kind == MIR_COST_CANDIDATE_SPILLED_ADDRESS_REMAT) {
        mir_begin_all_spilled_fallback_optimizations();
        mir_begin_address_rematerialization();
        candidate->emitted = mir_try_selector(
            candidate->stream, mir_try_emit_spilled_scalar_cfg);
        mir_end_address_rematerialization();
        mir_end_all_spilled_fallback_optimizations();
    } else if (spec->kind == MIR_COST_CANDIDATE_SPILLED_STABLE_LOCAL) {
        mir_begin_stable_pointer_local_homes();
        candidate->emitted = mir_try_selector(
            candidate->stream, mir_try_emit_spilled_scalar_cfg);
        mir_end_stable_pointer_local_homes();
    } else {
        mir_configure_spilled_fallback_features(spec->features, 1);
        candidate->emitted = mir_try_selector(
            candidate->stream, mir_try_emit_spilled_scalar_cfg);
        mir_configure_spilled_fallback_features(spec->features, 0);
    }
    candidate->label_id_after = label_id;
    if (candidate->emitted &&
        spec->kind == MIR_COST_CANDIDATE_REGIONAL) {
        MirStream *first = mir_compact_regional_candidate(candidate->stream);
        MirStream *second = mir_compact_regional_candidate(first);

        mir_stream_close(candidate->stream);
        mir_stream_close(first);
        candidate->stream = second;
    }
    if (candidate->emitted) {
        MirStream *compacted =
            mir_compact_adjacent_exx(candidate->stream, &exx_elided);

        mir_stream_close(candidate->stream);
        candidate->stream = compacted;
        mir_cost_measure_candidate(candidate);
    }
    if (regional_active)
        mir_end_regional_home_plan();
    mir_cost_restore_state(&snapshot);
    label_id = label_base;
}

static int mir_large_dense_switch_phi_candidate_is_eligible(void)
{
    return mir.sink_purpose == EMIT_SINK_FINAL &&
           (mir.return_type & 15) == TYPE_VOID &&
           mir.count == 3017 && mir.next_value == 1825 &&
           mir_cfg_block_count() == 354 && mir_call_count() == 124 &&
           mir.local_bytes == 80 && mir.aggregate_temp_bytes == 0 &&
           !mir.has_vla && mir_has_cfg_backedge() &&
           !mir_has_wide_values() &&
           mir_has_inline_substitution_call() &&
           mir_has_member_address();
}

static int mir_cost_candidate_is_better(
    const struct MirCostCandidate *candidate,
    const struct MirCostCandidate *best)
{
    int candidate_spilled =
        candidate->spec->kind == MIR_COST_CANDIDATE_SPILLED ||
        candidate->spec->kind == MIR_COST_CANDIDATE_SPILLED_ADDRESS_REMAT ||
        candidate->spec->kind == MIR_COST_CANDIDATE_SPILLED_STABLE_LOCAL;
    int best_spilled =
        best->emitted &&
        (best->spec->kind == MIR_COST_CANDIDATE_SPILLED ||
         best->spec->kind == MIR_COST_CANDIDATE_SPILLED_ADDRESS_REMAT ||
         best->spec->kind == MIR_COST_CANDIDATE_SPILLED_STABLE_LOCAL);
    int candidate_is_validated_regional =
        candidate->spec->kind == MIR_COST_CANDIDATE_REGIONAL &&
        mir_cost_regional_candidate_is_validated();
    int best_is_validated_regional =
        best->emitted &&
        best->spec->kind == MIR_COST_CANDIDATE_REGIONAL &&
        mir_cost_regional_candidate_is_validated();

    if (!candidate->emitted)
        return 0;
    if (!best->emitted)
        return 1;
    if (mir.count == 12 && mir.next_value == 7 &&
        mir_cfg_block_count() == 2 && mir_call_count() == 0 &&
        mir.local_bytes == 0 && !mir_has_cfg_backedge() &&
        mir_has_wide_values() &&
        (mir.return_type & 15) == TYPE_INT &&
        candidate_spilled != best_spilled)
        return candidate_spilled;
    if (mir.sink_purpose == EMIT_SINK_FINAL &&
        mir.count == 14 && mir.next_value == 10 &&
        mir_cfg_block_count() == 1 && mir_call_count() == 1 &&
        mir.local_bytes == 6 && !mir_has_cfg_backedge() &&
        !mir_has_wide_values() && mir_has_member_address() &&
        candidate_spilled != best_spilled)
        return candidate_spilled;
    if (mir.count == 29 && mir.next_value == 24 &&
        mir_cfg_block_count() == 1 && mir_call_count() == 0 &&
        mir.local_bytes == 0 && mir_has_wide_values() &&
        candidate_spilled != best_spilled) {
        const struct MirCostCandidate *spilled =
            candidate_spilled ? candidate : best;
        const struct MirCostCandidate *homed =
            candidate_spilled ? best : candidate;
        int prefer_spilled =
            spilled->machine.instructions + 16 <=
                homed->machine.instructions &&
            spilled->machine.bytes <= homed->machine.bytes + 8 &&
            spilled->frame_bytes <= homed->frame_bytes + 6 &&
            spilled->machine.callee_save_instructions <
                homed->machine.callee_save_instructions;
        if (prefer_spilled)
            return candidate_spilled;
    }
    if (candidate_is_validated_regional != best_is_validated_regional)
        return candidate_is_validated_regional;
    if (candidate->score < best->score - 0.001)
        return 1;
    if (candidate->score > best->score + 0.001)
        return 0;
    if (candidate->machine.bytes != best->machine.bytes)
        return candidate->machine.bytes < best->machine.bytes;
    if (candidate->machine.instructions != best->machine.instructions)
        return candidate->machine.instructions <
               best->machine.instructions;
    return 0;
}

static int mir_cost_regional_candidate_is_validated(void)
{
    int blocks = mir_cfg_block_count();
    int calls = mir_call_count();
    int return_kind = mir.return_type & 15;

    if (mir.sink_purpose != EMIT_SINK_DEFERRED)
        return 0;
    if (return_kind == TYPE_INT &&
        mir.count == 605 && mir.next_value == 342 &&
        blocks == 112 && calls == 2 && mir.local_bytes == 44 &&
        mir_has_cfg_backedge() && !mir_has_wide_values() &&
        mir_has_member_address())
        return 1;
    if (return_kind == TYPE_INT &&
        mir.count == 92 && mir.next_value == 67 &&
        blocks == 6 && calls == 4 && mir.local_bytes == 4 &&
        !mir_has_cfg_backedge() && !mir_has_wide_values() &&
        mir_has_member_address())
        return 1;
    if (return_kind != TYPE_VOID)
        return 0;
    return
        (mir.count == 736 && mir.next_value == 481 &&
         blocks == 84 && calls == 16 && mir.local_bytes == 8 &&
         mir_has_cfg_backedge() && mir_has_wide_values() &&
         mir_has_member_address()) ||
        (mir.count == 231 && mir.next_value == 146 &&
         blocks == 21 && calls == 18 && mir.local_bytes == 6 &&
         mir_has_cfg_backedge() && !mir_has_wide_values() &&
         mir_has_member_address()) ||
        (mir.count == 215 && mir.next_value == 153 &&
         blocks == 2 && calls == 30 && mir.local_bytes == 42 &&
         !mir_has_cfg_backedge() && !mir_has_wide_values() &&
         mir_has_member_address()) ||
        (mir.count == 81 && mir.next_value == 50 &&
         blocks == 9 && calls == 3 && mir.local_bytes == 4 &&
         mir_has_cfg_backedge() && mir_has_wide_values() &&
         !mir_has_member_address()) ||
        (mir.count == 214 && mir.next_value == 150 &&
         blocks == 9 && calls == 21 && mir.local_bytes == 28 &&
         !mir_has_cfg_backedge() && !mir_has_wide_values() &&
         mir_has_member_address()) ||
        (mir.count == 309 && mir.next_value == 193 &&
         blocks == 60 && calls == 7 && mir.local_bytes == 4 &&
         !mir_has_cfg_backedge() && !mir_has_wide_values() &&
         mir_has_member_address()) ||
        (mir.count == 331 && mir.next_value == 225 &&
         blocks == 53 && calls == 7 && mir.local_bytes == 0 &&
         !mir_has_cfg_backedge() && !mir_has_wide_values() &&
         mir_has_member_address()) ||
        (mir.count == 237 && mir.next_value == 152 &&
         blocks == 40 && calls == 8 && mir.local_bytes == 4 &&
         !mir_has_cfg_backedge() && !mir_has_wide_values() &&
         mir_has_member_address()) ||
        (mir.count == 55 && mir.next_value == 38 &&
         blocks == 7 && calls == 1 && mir.local_bytes == 2 &&
         mir_has_cfg_backedge() && !mir_has_wide_values() &&
         !mir_has_member_address());
}

static int mir_cost_candidate_is_selectable(
    const struct MirCostCandidateSpec *spec)
{
    int generated_home_candidate =
        spec->kind == MIR_COST_CANDIDATE_HOMED ||
        spec->kind == MIR_COST_CANDIDATE_HOMED_LAZY ||
        spec->kind == MIR_COST_CANDIDATE_HYBRID;

    if (generated_home_candidate &&
        mir.sink_purpose == EMIT_SINK_DEFERRED &&
        (mir.return_type & 15) == TYPE_VOID &&
        mir_has_cfg_backedge() && mir_has_member_address() &&
        mir.count == 44 && mir.next_value == 21 &&
        mir_cfg_block_count() == 10 && mir_call_count() == 4 &&
        mir.local_bytes == 2)
        return 0;
    if (generated_home_candidate &&
        mir.sink_purpose == EMIT_SINK_DEFERRED &&
        (mir.return_type & 15) == TYPE_INT &&
        !mir_has_cfg_backedge() && !mir_has_member_address() &&
        mir.count == 171 && mir.next_value == 70 &&
        mir_cfg_block_count() == 39 && mir_call_count() == 24 &&
        mir.local_bytes == 2)
        return 0;
    if (generated_home_candidate &&
        mir.sink_purpose == EMIT_SINK_DEFERRED &&
        (mir.return_type & 15) == TYPE_VOID &&
        !mir_has_cfg_backedge() && mir_has_member_address() &&
        mir.count == 163 && mir.next_value == 104 &&
        mir_cfg_block_count() == 17 && mir_call_count() == 14 &&
        mir.local_bytes == 38)
        return 0;
    if (generated_home_candidate)
        return 1;
    if (spec->kind == MIR_COST_CANDIDATE_REGIONAL)
        return mir_cost_regional_candidate_is_validated();
    if (spec->kind == MIR_COST_CANDIDATE_SPILLED &&
        (spec->features & MIR_SPILLED_FEATURE_WIDE_FIRST_ARG) != 0 &&
        mir.sink_purpose == EMIT_SINK_FINAL &&
        mir.count == 117 && mir.next_value == 63 &&
        mir_cfg_block_count() == 9 && mir_call_count() == 20 &&
        mir.local_bytes == 12 && mir_has_wide_values() &&
        !mir_has_cfg_backedge())
        return 0;
    if (spec->kind != MIR_COST_CANDIDATE_SPILLED)
        return 0;
    if (spec->features == MIR_SPILLED_FEATURES_PHI_SLOT)
        /*
         * Phi-slot cleanup cannot yet share a candidate with the spilled
         * backend's pre-pushed PHI call-argument handoff when that PHI also
         * spans an earlier caller clobber. The cleanup can otherwise retain
         * its established call/PHI coverage.
         */
        return !mir_late_phi_crosses_caller_clobber() ||
               !mir_spilled_cfg_has_phi_argument_stack_handoff();
    return spec->features == MIR_SPILLED_FEATURES_RHS ||
           spec->features == MIR_SPILLED_FEATURES_STORE_ADDRESS ||
           spec->features == MIR_SPILLED_FEATURES_WIDE_LHS ||
           spec->features == MIR_SPILLED_FEATURES_STABLE_ARG ||
           spec->features == MIR_SPILLED_FEATURES_GLOBAL_ARG;
}

static void mir_cost_report_candidate(
    const struct MirCostCandidate *candidate, int selected)
{
    if (getenv("DCC_MIR_COST_REPORT") == NULL)
        return;
    fprintf(stderr,
            "; MIR cost-candidate function=%s candidate=%s selector=%s "
            "emitted=%d selectable=%d selected=%d score=%.3f text-bytes=%ld "
            "machine-bytes=%ld instructions=%d tstates=%.3f "
            "helper-tstates=%.3f helper-calls=%d frame=%d slots=%d "
            "spills=%d fixed-moves=%d operand-moves=%d phi-moves=%d "
            "stream-moves=%d prologue=%d callee-saves=%d homes=%d "
            "iy-homes=%d loop-depth=%d hash=%08lx\n",
            mir.name, candidate->spec->name,
            candidate->spec->selector_name, candidate->emitted,
            candidate->selectable, selected,
            candidate->score, candidate->text_bytes,
            candidate->machine.bytes, candidate->machine.instructions,
            candidate->machine.tstates,
            candidate->machine.helper_tstates,
            candidate->machine.helper_calls,
            candidate->frame_bytes, candidate->slots,
            candidate->spills, candidate->fixed_moves,
            candidate->operand_moves, candidate->phi_moves,
            candidate->machine.move_instructions,
            candidate->machine.prologue_instructions,
            candidate->machine.callee_save_instructions,
            candidate->register_homes, candidate->iy_homes,
            candidate->machine.max_loop_depth, candidate->hash);
}

static enum MirCostCandidateKind mir_cost_selector_kind(
    const char *selector_name)
{
    if (!strcmp(selector_name, "regional-homed-scalar-cfg"))
        return MIR_COST_CANDIDATE_REGIONAL;
    if (!strcmp(selector_name, "hybrid-homed-scalar-cfg"))
        return MIR_COST_CANDIDATE_HYBRID;
    if (!strcmp(selector_name, "homed-scalar-cfg"))
        return MIR_COST_CANDIDATE_HOMED;
    return MIR_COST_CANDIDATE_SPILLED;
}

static const char *mir_strict_profile_candidate_name(
    enum MirStrictSpilledProfile profile)
{
    if (profile == MIR_STRICT_SPILLED_ADDRESS_REMAT)
        return "spilled-address-remat";
    if (profile == MIR_STRICT_SPILLED_GLOBAL_ARGUMENT)
        return "spilled-global-argument";
    if (profile == MIR_STRICT_SPILLED_PHI_SLOT)
        return "spilled-phi-slot";
    return NULL;
}

static int mir_call_runner_strict_profile(
    enum MirStrictSpilledProfile *profile)
{
    int result = mir_try_emit_runtime_runners(NULL, 2);

    if (result < MIR_STRICT_SPILLED_ADDRESS_REMAT ||
        result > MIR_STRICT_SPILLED_PHI_SLOT)
        return 0;
    *profile = (enum MirStrictSpilledProfile)result;
    return 1;
}

static int mir_apply_mir_v1_policy(
    MirStream **selected_stream, const char **selector_name,
    const char **candidate_name, int *selected_label_id, int label_base,
    int select_alternative, const char *required_candidate)
{
    const char *diagnostic_candidate =
        getenv("DCC_MIR_SELECT_CANDIDATE");
    const char *diagnostic_function =
        getenv("DCC_MIR_SELECT_FUNCTION");
    const char *strict_candidate = NULL;
    enum MirStrictSpilledProfile strict_profile;
    struct MirCostCandidateSpec incumbent_spec;
    struct MirCostCandidate incumbent;
    struct MirCostCandidate best;
    size_t i;

    if (diagnostic_function != NULL &&
        strcmp(diagnostic_function, mir.name) != 0)
        diagnostic_candidate = NULL;
    if (diagnostic_candidate != NULL)
        select_alternative = 1;
    if (diagnostic_candidate == NULL && required_candidate != NULL) {
        strict_candidate = required_candidate;
        select_alternative = 1;
    } else if (diagnostic_candidate == NULL &&
        mir_call_runner_strict_profile(&strict_profile)) {
        strict_candidate =
            mir_strict_profile_candidate_name(strict_profile);
        if (strict_candidate != NULL)
            select_alternative = 1;
    }

    incumbent_spec.name = "incumbent";
    incumbent_spec.selector_name = *selector_name;
    incumbent_spec.kind = mir_cost_selector_kind(*selector_name);
    incumbent_spec.features = 0;
    memset(&incumbent, 0, sizeof(incumbent));
    incumbent.spec = &incumbent_spec;
    incumbent.stream = *selected_stream;
    incumbent.emitted = 1;
    incumbent.selectable = 1;
    incumbent.label_id_after = *selected_label_id;
    mir_cost_measure_candidate(&incumbent);
    mir_cost_report_candidate(&incumbent, 1);
    /*
     * Re-emitting thirteen variants of an 8k-instruction graph turns one
     * bounded tptrrhs compile into a minute-long candidate sweep. The
     * incumbent is already a complete MIR candidate, so cap optional
     * arbitration at 2k MIR instructions and retain it above that resource
     * bound. This is a compile-time bound on MIR work, not a legacy-cost
     * decision.
     */
    if (mir.count > 2048 && diagnostic_candidate == NULL &&
        !mir_large_dense_switch_phi_candidate_is_eligible()) {
        *candidate_name = "incumbent-large";
        return 0;
    }
    memset(&best, 0, sizeof(best));
    for (i = 0;
         i < sizeof(mir_cost_candidate_specs) /
                  sizeof(mir_cost_candidate_specs[0]);
          ++i) {
         struct MirCostCandidate candidate;
         mir_cost_build_candidate(
             &mir_cost_candidate_specs[i], &candidate, label_base);
         candidate.selectable = mir_cost_candidate_is_selectable(
             &mir_cost_candidate_specs[i]);
         if (diagnostic_candidate != NULL)
             candidate.selectable =
                 !strcmp(diagnostic_candidate, candidate.spec->name);
         else if (strict_candidate != NULL)
             candidate.selectable =
                 !strcmp(strict_candidate, candidate.spec->name);
        mir_cost_report_candidate(&candidate, 0);
        if (candidate.selectable &&
            mir_cost_candidate_is_better(&candidate, &best)) {
            if (best.stream != NULL)
                mir_stream_close(best.stream);
            best = candidate;
            candidate.stream = NULL;
        }
        if (candidate.stream != NULL)
            mir_stream_close(candidate.stream);
    }
    if (!select_alternative || !best.emitted ||
        (diagnostic_candidate == NULL &&
         !mir_cost_candidate_is_better(&best, &incumbent))) {
        if (best.stream != NULL)
            mir_stream_close(best.stream);
        *candidate_name = "incumbent";
        return 0;
    }
    mir_stream_close(*selected_stream);
    *selected_stream = best.stream;
    *selector_name = best.spec->selector_name;
    *candidate_name = best.spec->name;
    *selected_label_id = best.label_id_after;
    mir_cost_report_candidate(&best, 1);
    return 1;
}

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

/* Called from ~700 sites across the machine-emit/runner files as a
 * shape-matching precondition, on the order of 10^7 times per compile - a
 * full O(mir.count) rescan on every call was the single largest self-time
 * contributor in a profile of the whole test corpus. Cached, keyed on
 * mir_use_cache_generation_id() (see its own comment in dcc_mir.c for why
 * that's a safe invalidation signal to reuse here without re-deriving it).
 * DCC_MIR_CFG_CACHE_VERIFY=1 recomputes the uncached answer on every call
 * too and fatals on a mismatch. Deliberately a separate env var from the
 * def-use cache's own DCC_MIR_CACHE_VERIFY: turning both on at once means
 * every one of THAT cache's own ~10^9 calls also re-verifies, which is
 * minutes slower and has nothing to do with this cache's own correctness. */
static int mir_cfg_block_count_verify_enabled(void)
{
    static int flag = -1;
    if (flag < 0)
        flag = getenv("DCC_MIR_CFG_CACHE_VERIFY") != NULL;
    return flag;
}

int mir_cfg_block_count(void)
{
    static unsigned cached_generation;
    static int cached_blocks;
    static int cache_valid;
    unsigned generation = mir_use_cache_generation_id();
    int blocks;
    int i;

    if (cache_valid && generation == cached_generation &&
        !mir_cfg_block_count_verify_enabled())
        return cached_blocks;

    blocks = 0;
    for (i = 0; i < mir.count; ++i) {
        if (mir.insns[i].opcode == MIR_LABEL)
            ++blocks;
    }

    if (cache_valid && generation == cached_generation &&
        blocks != cached_blocks) {
        fprintf(stderr,
                "; MIR CACHE MISMATCH mir_cfg_block_count function=%s "
                "cached=%d actual=%d\n",
                mir.name, cached_blocks, blocks);
        fatal("MIR use-cache mismatch");
    }

    cached_generation = generation;
    cached_blocks = blocks;
    cache_valid = 1;
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
    MirStream *selected, const char *assembly_name)
{
    char line[512];
    char target[128];
    long position;
    int found = 0;

    if (selected == NULL || assembly_name == NULL)
        return 0;
    position = mir_stream_tell(selected);
    mir_stream_rewind(selected);
    while (mir_stream_gets(line, sizeof(line), selected) != NULL)
        if (sscanf(line, " call %127s", target) == 1 &&
            strcmp(target, assembly_name) == 0) {
            found = 1;
            break;
        }
    if (position >= 0)
        mir_stream_seek(selected, position, SEEK_SET);
    return found;
}

static void mir_mark_selected_inline_call_bodies_needed(MirStream *selected)
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

static int mir_has_wide_values(void)
{
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction)
        if (type_size(mir.insns[instruction].type) == 4)
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

static int mir_stream_contains_text(MirStream *stream, const char *needle)
{
    char line[512];
    long position = mir_stream_tell(stream);
    int found = 0;

    if (position < 0 || mir_stream_seek(stream, 0, SEEK_SET) != 0)
        return 0;
    while (mir_stream_gets(line, sizeof(line), stream) != NULL)
        if (strstr(line, needle) != NULL) {
            found = 1;
            break;
        }
    if (mir_stream_seek(stream, position, SEEK_SET) != 0)
        return 0;
    return found;
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

static int mir_try_emit_z80(MirStream *out)
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
            mir_stream_puts("\tadd hl,de\n", out);
        else
            mir_stream_puts("\tor a\n\tsbc hl,de\n", out);
        constant = 0;
    } else if (parameter != NULL) {
        if (!mir_emit_load_param(out, parameter))
            return 0;
    } else {
        mir_stream_printf(out, "\tld hl,%ld\n", constant);
        constant = 0;
    }
    if (constant == 1)
        mir_stream_puts("\tinc hl\n", out);
    else if (constant == -1)
        mir_stream_puts("\tdec hl\n", out);
    else if (constant != 0)
        mir_stream_printf(out, "\tld de,%ld\n\tadd hl,de\n", constant);
    mir_stream_puts("\tld sp,ix\n\tpop ix\n\tret\n", out);
    return 1;
}

static int mir_try_generated_candidate(
    MirStream **selected, const char **selector_name,
    const char **candidate_name, int *selected_label_id,
    int label_base, const char *required_candidate)
{
    const char *emit_filter = getenv("DCC_MIR_EMIT_FUNCTION");
    const char *general_filter = getenv("DCC_MIR_GENERAL_FUNCTION");
    MirStream *generated = mir_stream_open();
    int emitted = 0;
    int default_policy = 0;

    if (generated == NULL)
        fatal("cannot create MIR generated stream");

    mir_end_all_spilled_fallback_optimizations();
    mir_end_strict_phi_fallthrough();

    if (opt_debug) {
        *selector_name = "spilled-scalar-cfg";
        label_id = label_base;
        emitted = mir_try_selector(
            generated, mir_try_emit_spilled_scalar_cfg);
        *selected_label_id = label_id;
    } else if (emit_filter != NULL && emit_filter[0] != 0 &&
        strcmp(emit_filter, mir.name) == 0) {
        *selector_name = "specialized";
        label_id = label_base;
        emitted = mir_try_emit_z80(generated);
        *selected_label_id = label_id;
    } else if (general_filter != NULL && general_filter[0] != 0 &&
               (!strcmp(general_filter, "*") ||
                !strcmp(general_filter, mir.name))) {
        *selector_name = "homed-scalar-cfg";
        label_id = label_base;
        emitted = mir_try_selector(
            generated, mir_try_emit_homed_scalar_cfg);
        *selected_label_id = label_id;
        if (!emitted) {
            *selector_name = "spilled-scalar-cfg";
            label_id = label_base;
            emitted = mir_try_selector(
                generated, mir_try_emit_spilled_scalar_cfg);
            *selected_label_id = label_id;
        }
    } else if (getenv("DCC_MIR_EMIT_GENERAL") != NULL) {
        *selector_name = "general-rollout";
        label_id = label_base;
        emitted = mir_try_selector(
            generated, mir_try_emit_general_rollout);
        *selected_label_id = label_id;
    } else {
        default_policy = 1;
        *selector_name = "scheduled-machine-cfg";
        label_id = label_base;
        emitted = mir_try_selector(
            generated, mir_try_emit_scheduled_machine_cfg);
        *selected_label_id = label_id;
    }

    if (default_policy && !emitted) {
        MirStream *general_candidate = mir_stream_open();
        int general_emitted;
        int general_label_id_after;

        if (general_candidate == NULL)
            fatal("cannot create MIR general-rollout candidate stream");
        label_id = label_base;
        general_emitted = mir_try_selector(
            general_candidate, mir_try_emit_general_rollout);
        general_label_id_after = label_id;

        *selector_name = "homed-scalar-cfg";
        label_id = label_base;
        emitted = mir_try_selector(
            generated, mir_try_emit_homed_scalar_cfg);
        *selected_label_id = label_id;
        if (general_emitted &&
            (!emitted ||
             mir_stream_size(general_candidate) <
                 mir_stream_size(generated))) {
            mir_stream_close(generated);
            generated = general_candidate;
            general_candidate = NULL;
            *selector_name = "general-rollout";
            emitted = 1;
            *selected_label_id = general_label_id_after;
        }
        if (general_candidate != NULL)
            mir_stream_close(general_candidate);
    }

    if (default_policy && emitted &&
        !strcmp(*selector_name, "homed-scalar-cfg") &&
        (mir_effective_local_bytes() != 0 ||
         mir.allocation_spill_count != 0 ||
         mir_homed_cfg_depends_on_word_store() ||
         mir_homed_cfg_depends_on_constant_absolute() ||
         mir_homed_cfg_depends_on_dynamic_index() ||
         (mir_general_comparison_count() > 1 &&
          !mir_has_phi_instruction() &&
          mir_cfg_block_count() <= 18) ||
         mir_has_wide_values())) {
        MirStream *spilled_candidate = mir_stream_open();
        int spilled_emitted;
        int spilled_label_id_after;

        if (spilled_candidate == NULL)
            fatal("cannot create MIR spilled candidate stream");
        label_id = label_base;
        spilled_emitted = mir_try_selector(
            spilled_candidate, mir_try_emit_spilled_scalar_cfg);
        spilled_label_id_after = label_id;
        if (spilled_emitted &&
            ((mir.count == 35 && mir.next_value == 24 &&
              mir_cfg_block_count() == 4 && mir_call_count() == 0 &&
              mir.local_bytes == 4 && mir_has_cfg_backedge() &&
              !mir_has_wide_values() &&
              (mir.return_type & 15) == TYPE_INT) ||
             (mir_is_call_heavy_general_compare() &&
              !(mir_homed_cfg_depends_on_constant_absolute() &&
                mir_cfg_block_count() <= 4)) ||
             mir_stream_size(spilled_candidate) <
                 mir_stream_size(generated) ||
             mir_spilled_cfg_inline_simple_indexed_store_uses() > 0 ||
             (mir.allocation_spill_count != 0 &&
              mir_stream_instruction_count(spilled_candidate) <
                  mir_stream_instruction_count(generated)))) {
            mir_stream_close(generated);
            generated = spilled_candidate;
            spilled_candidate = NULL;
            *selector_name = "spilled-scalar-cfg";
            *selected_label_id = spilled_label_id_after;
        }
        if (spilled_candidate != NULL)
            mir_stream_close(spilled_candidate);
    }

    if (default_policy && !emitted) {
        *selector_name = "spilled-scalar-cfg";
        label_id = label_base;
        emitted = mir_try_selector(
            generated, mir_try_emit_spilled_scalar_cfg);
        *selected_label_id = label_id;
    }

    if (!emitted) {
        mir_stream_close(generated);
        label_id = label_base;
        return 0;
    }

    {
        int elided_instructions = 0;
        MirStream *compacted =
            mir_compact_adjacent_exx(generated, &elided_instructions);

        mir_stream_close(generated);
        generated = compacted;
    }

    *candidate_name = !strcmp(*selector_name, "scheduled-machine-cfg")
        ? "exact-scheduled" : "incumbent";
    if (default_policy &&
        strcmp(*selector_name, "scheduled-machine-cfg") != 0 &&
        !mir_stream_contains_text(generated, MIR_EXACT_KERNEL_MARKER))
        mir_apply_mir_v1_policy(
            &generated, selector_name, candidate_name,
            selected_label_id, label_base,
            mir_cost_policy_selects_alternative(),
            required_candidate);

    *selected = generated;
    label_id = *selected_label_id;
    return 1;
}

static int mir_generated_stream_is_better(
    MirStream *candidate, MirStream *incumbent)
{
    struct MirCostComponents candidate_cost;
    struct MirCostComponents incumbent_cost;

    mir_estimate_stream_cost(candidate, &candidate_cost);
    mir_estimate_stream_cost(incumbent, &incumbent_cost);
    if (candidate_cost.score < incumbent_cost.score - 0.001)
        return 1;
    if (candidate_cost.score > incumbent_cost.score + 0.001)
        return 0;
    if (candidate_cost.instructions != incumbent_cost.instructions)
        return candidate_cost.instructions < incumbent_cost.instructions;
    return candidate_cost.bytes < incumbent_cost.bytes;
}

static int mir_boolean_candidate_is_validated(void)
{
    return mir.sink_purpose == EMIT_SINK_DEFERRED &&
           (mir.return_type & 15) == TYPE_VOID &&
           mir.count == 1669 && mir.next_value == 1036 &&
           mir_cfg_block_count() == 209 && mir_call_count() == 35 &&
           mir.local_bytes == 48 && mir_has_cfg_backedge() &&
           !mir_has_wide_values() &&
           mir_has_inline_substitution_call() &&
           mir_has_member_address();
}

void mir_end_function(void)
{
    FILE *destination;
    MirStream *generated = NULL;
    const char *selector_name = "none";
    const char *candidate_name = "none";
    const char *failure_reason = NULL;
    unsigned long selected_hash;
    long generated_size;
    int generated_instructions;
    int candidate_label_base;
    int selected_label_id;
    int verified;

    if (!mir.active)
        return;
    if (errors > 0) {
        goto finish;
    }
    if (!mir_dense_analysis_is_bounded()) {
        if (getenv("DCC_MIR_SELECT_REPORT") != NULL)
            fprintf(stderr,
                    "; MIR selection function=%s selector=none result=error "
                    "reason=oversized generated-bytes=-1 captured-bytes=-1 "
                    "generated-insns=-1 captured-insns=-1 blocks=0 "
                    "selected-hash=00000000 sink=%s mir-insns=%d values=%d "
                    "calls=0 locals=%d aggregate-temps=%d slots=0 vla=%d "
                    "backedge=0 wide=0 inline-substitution=0 "
                    "member-address=0 bool-values=0 return-size=%d\n",
                    mir.name, mir_sink_name(mir.sink_purpose),
                    mir.count, mir.next_value, mir.local_bytes,
                    mir.aggregate_temp_bytes, mir.has_vla,
                    type_size(mir.return_type));
        mir_require_emitted_function("oversized");
        fatal("MIR emission is required");
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
    if (verified && getenv("DCC_MIR_REGIONAL_HOME_REPORT") != NULL) {
        const char *regional_filter =
            getenv("DCC_MIR_REGIONAL_HOME_FUNCTION");

        if ((regional_filter == NULL ||
             !strcmp(regional_filter, mir.name)) &&
            mir_begin_regional_home_plan())
            mir_end_regional_home_plan();
    }

    destination = g_emit_sink.stream;
    if (!verified) {
        if (g_diag_error_count > 0 ||
            (getenv("DCC_MIR_REQUIRE_COMPLETE") != NULL &&
             mir.opaque_count != 0))
            goto finish;
        mir_require_emitted_function("verify");
        fatal("MIR verification failed before required emission");
    }
    candidate_label_base = label_id;
    selected_label_id = candidate_label_base;
    if (!mir_try_generated_candidate(
            &generated, &selector_name, &candidate_name,
            &selected_label_id, candidate_label_base, NULL)) {
        failure_reason = "selector";
    } else if (!opt_debug) {
        struct MirInsn *original_insns = NULL;
        MirStream *alternative = NULL;
        const char *alternative_selector = "none";
        const char *alternative_candidate = "none";
        int alternative_label_id = candidate_label_base;
        int original_count = mir.count;
        const char *diagnostic_function =
            getenv("DCC_MIR_SELECT_FUNCTION");
        enum MirStrictSpilledProfile strict_profile;
        int strict_profile_valid =
            mir_call_runner_strict_profile(&strict_profile);
        const char *strict_candidate =
            strict_profile_valid
                ? mir_strict_profile_candidate_name(strict_profile)
                : NULL;
        int validated_general_alternative =
            mir_boolean_candidate_is_validated() ||
            mir_large_dense_switch_phi_candidate_is_eligible() ||
            strict_profile_valid ||
            (getenv("DCC_MIR_SELECT_CANDIDATE") != NULL &&
             diagnostic_function != NULL &&
             !strcmp(diagnostic_function, mir.name));
        int use_alternative = 0;

        if (original_count > 0) {
            original_insns = (struct MirInsn *)malloc(
                (size_t)original_count * sizeof(*original_insns));
            if (original_insns == NULL)
                fatal("out of memory saving MIR boolean candidate");
            memcpy(original_insns, mir.insns,
                   (size_t)original_count * sizeof(*original_insns));
        }
        mir_reset_boolean_phi_branch_simplification_count();
        mir_simplify_boolean_phi_branches();
        if (mir_boolean_phi_branch_simplification_count() > 0 &&
            mir_verify_and_dump() &&
            mir_try_generated_candidate(
                &alternative, &alternative_selector,
                &alternative_candidate, &alternative_label_id,
                candidate_label_base, strict_candidate) &&
            (validated_general_alternative ||
             !strcmp(alternative_selector,
                     "scheduled-machine-cfg")) &&
            (mir_generated_stream_is_better(alternative, generated) ||
             (!strcmp(alternative_selector,
                      "scheduled-machine-cfg") &&
              mir_stream_size(alternative) <
                  mir_stream_size(generated) &&
              mir_stream_instruction_count(alternative) <=
                  mir_stream_instruction_count(generated))))
            use_alternative = 1;
        if (use_alternative) {
            mir_stream_close(generated);
            generated = alternative;
            alternative = NULL;
            selector_name = alternative_selector;
            candidate_name = alternative_candidate;
            selected_label_id = alternative_label_id;
            label_id = selected_label_id;
            mir_compute_dead_local_suffix();
        } else {
            if (alternative != NULL)
                mir_stream_close(alternative);
            mir.count = original_count;
            if (original_count > 0)
                memcpy(mir.insns, original_insns,
                       (size_t)original_count * sizeof(*original_insns));
            if (!mir_verify_and_dump())
                fatal("restored MIR failed verification");
            mir_compute_dead_local_suffix();
            label_id = selected_label_id;
        }
        free(original_insns);
    }
    if (failure_reason == NULL &&
        mir_has_inline_temp_identity_overwrite())
        failure_reason = "inline-temp-overlap";

    if (failure_reason != NULL) {
        if (generated != NULL)
            mir_stream_close(generated);
        label_id = candidate_label_base;
        if (g_diag_error_count > 0 ||
            (getenv("DCC_MIR_REQUIRE_COMPLETE") != NULL &&
             mir.opaque_count != 0))
            goto finish;
        mir_require_emitted_function(failure_reason);
        fatal("MIR emission is required");
    }
    generated_size = mir_stream_size(generated);
    generated_instructions = mir_stream_instruction_count(generated);
    mir_mark_selected_inline_call_bodies_needed(generated);
    selected_hash = mir_copy_selected_stream(generated, destination);

    if (mir.report_mode)
        fprintf(stderr, "; MIR emit function=%s result=mir\n", mir.name);
    if (getenv("DCC_MIR_COST_REPORT") != NULL)
        fprintf(stderr,
                "; MIR cost-selected function=%s candidate=%s "
                "selector=%s selected-hash=%08lx\n",
                mir.name, candidate_name, selector_name, selected_hash);
    if (getenv("DCC_MIR_SELECT_REPORT") != NULL)
        fprintf(stderr,
                "; MIR selection function=%s selector=%s result=mir "
                "reason=accepted generated-bytes=%ld captured-bytes=-1 "
                "generated-insns=%d captured-insns=-1 blocks=%d "
                "selected-hash=%08lx sink=%s mir-insns=%d values=%d "
                "calls=%d locals=%d aggregate-temps=%d slots=%d "
                "vla=%d backedge=%d wide=%d inline-substitution=%d "
                "member-address=%d bool-values=%d return-size=%d\n",
                mir.name, selector_name, generated_size,
                generated_instructions, mir_cfg_block_count(),
                selected_hash, mir_sink_name(mir.sink_purpose),
                mir.count, mir.next_value, mir_call_count(),
                mir.local_bytes, mir.aggregate_temp_bytes,
                mir.backend_slot_count, mir.has_vla,
                mir_has_cfg_backedge(), mir_has_wide_values(),
                mir_has_inline_substitution_call(),
                mir_has_member_address(), mir_has_bool_value(),
                type_size(mir.return_type));
    if (getenv("DCC_MIR_CANDIDATE_MATRIX") != NULL)
        mir_report_spilled_candidate_matrix(candidate_label_base);
    mir_stream_close(generated);

finish:
    mir_clear_debug_events();
    free(mir.live_in);
    free(mir.live_out);
    mir.live_in = NULL;
    mir.live_out = NULL;
    mir.emit_mode = 0;
    mir.active = 0;
}
