/* dcc_mir_spilled_cfg.c - the mir_try_emit_spilled_scalar_cfg selector
 * (the dominant selector, covering most of the corpus) plus its
 * exclusive helpers: virtual IY/offset addressing, HL/stack value
 * forwarding, comparison fusion, fastcall shape detection, and
 * backend-slot preparation.
 *
 * Part of the dcc_mir.c MIR backend split; see dcc_mir_internal.h.
 */

/* fmemopen() (used below for elided-epilogue byte accounting) is a POSIX
 * extension: under strict ISO C mode, glibc's <stdio.h> doesn't declare it
 * unless a feature-test macro asks for it (_POSIX_C_SOURCE 200809L is
 * enough for fmemopen specifically). Without a prototype in scope, the
 * implicit-int rule assumes it returns int, silently truncating the real
 * FILE* pointer it returns - the same undefined-behavior bug class already
 * documented and fixed for realpath()/similar POSIX calls in dcc.c and
 * dcc_func.c. Must be defined before any system header is first included
 * in this translation unit. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dcc.h"
#include "dcc_ast.h"
#include "dcc_mir.h"
#include "dcc_mir_internal.h"

static int mir_binary_is_fusable_comparison(int i);
static int mir_value_is_nested_truth_comparison_input(int value);
static int mir_float_madd_match(int add_index, int *multiply_index,
                                int *addend_value);
static int mir_unary_is_fusable_not_branch(int i);
static int mir_forwarded_stack_target_instruction = -1;
static int mir_fused_compare_is_const_zero_rhs(int compare_index);
static int mir_fused_compare_is_signed_zero_sign_test(int compare_index);
static const char *mir_wide_runtime_helper(const struct MirInsn *insn);
static int mir_value_is_selfstore_incdec_source(int value);
int mir_store_is_dead(int instruction);
static int mir_divmod_partner(int instruction);
static int mir_binary_only_constant(int value);
static int mir_stack_forward_target(int value, int *dynamic_index);
static int mir_stack_backend_slot_forwardable(
    int value, int units, int instruction);
static void mir_emit_hl_offset_from_ix(FILE *out, int offset);
static int mir_forward_skip_last_skipped_dead_store;
static int mir_spilled_cfg_used_dead_store_forwarding;
static int mir_spilled_cfg_used_constant_absolute;
static int mir_spilled_cfg_used_constant_index_absolute;
static int mir_spilled_cfg_used_dynamic_index_base_forwarding;
static int mir_spilled_cfg_used_wide_constant_rematerialization;
static int mir_spilled_cfg_used_unsigned_wide_constant_relational;
static int mir_spilled_cfg_used_signed_wide_constant_relational;
static int mir_spilled_cfg_used_unary_not_branch_fusion;
static int mir_spilled_cfg_used_planned_stack_handoff;
static int mir_spilled_cfg_used_planned_index_base_handoff;
static int mir_spilled_cfg_used_stable_pointer_local_home;
static int mir_spilled_cfg_used_stable_pointer_local_slot;
static int mir_spilled_cfg_used_general_rhs_stack_forwarding;
static int mir_spilled_cfg_used_binary_load_pair_forwarding;
static int mir_address_rematerialization_enabled;
static int mir_spilled_cfg_indirect_store_value_forwarding_count;
static int mir_spilled_cfg_branch_condition_forwarding_count;
static int mir_spilled_cfg_indirect_store_address_forwarding_count;
static int mir_planned_stack_handoffs_enabled;
static int mir_stable_pointer_local_homes_enabled;
static int mir_general_rhs_stack_forwarding_enabled;
static int mir_indirect_store_value_forwarding_enabled;
static int mir_branch_condition_forwarding_enabled;
static int mir_indirect_store_address_forwarding_enabled;
static int mir_wide_binary_lhs_forwarding_enabled;
static int mir_wide_binary_rhs_forwarding_enabled;
static int mir_wide_binary_rhs_forwarding_uses;
static int mir_wide_store_forwarding_enabled;
static int mir_spilled_cfg_used_wide_store_forwarding;
static int mir_stable_pointer_argument_rematerialization_enabled;
static int mir_global_argument_rematerialization_enabled;
static int mir_wide_first_argument_stack_cache_enabled;
static int mir_narrow_argument_direct_push_enabled;
static int mir_constant_argument_prepacking_enabled;
static int mir_prepacked_call_instruction = -1;
static int mir_prepacked_after_argument = -1;
static int mir_prepacked_result_value = -1;
static int mir_constant_argument_prepack_count;
static int mir_promoted_local_slot_reuse_enabled;
static int mir_planned_stack_emit_count;
static int mir_planned_stack_consume_count;
static int mir_planned_stack_invalid;
static int mir_forwarded_wide_stack_value = -1;
static int mir_forwarded_wide_stack_consumer = -1;
static unsigned char *mir_backend_slot_accessed;
static int *mir_backend_slot_offsets;
static int mir_backend_slot_offset_capacity;
static int mir_backend_slot_pool_count;
static int mir_backend_frame_slot_count;
static int mir_backend_local_slot_count;
static int mir_spilled_cfg_used_promoted_local_slot;
#define MIR_BACKEND_SLOT_CALL_CACHE (-2)
#define MIR_BACKEND_SLOT_WIDE_ARGUMENT_STACK_CACHE (-3)
#define MIR_BACKEND_SLOT_NARROW_ARGUMENT_DIRECT_PUSH (-4)

static int mir_virtual_offset(int value)
{
    int slot = value;
    if (mir_backend_slot_accessed != NULL &&
        value >= 0 && value < mir.next_value)
        mir_backend_slot_accessed[value] = 1;
    if (value >= 0 && value < mir.next_value && mir.backend_slots != NULL &&
        mir.backend_slots[value] >= 0)
        slot = mir.backend_slots[value];
    if (slot >= 0 && slot < mir_backend_slot_pool_count &&
        mir_backend_slot_offsets != NULL)
        return mir_backend_slot_offsets[slot];
    return -mir_effective_local_bytes() - mir.aggregate_temp_bytes -
           2 * (slot + 1);
}

static int mir_emit_address_to_hl(FILE *out, const struct MirInsn *insn)
{
    int memory_type;
    int memory_storage;
    int memory_offset;
    struct Sym *global = find_global(insn->name);

    if (!mir_scalar_memory_location(insn, &memory_type,
                                    &memory_storage, &memory_offset))
        return 0;
    if ((global != NULL && global->storage == SC_FUNC) ||
        memory_storage == SC_GLOBAL || memory_storage == SC_EXTERN ||
        memory_storage == SC_FUNC) {
        const char *assembly_name = asm_name_for(
            global != NULL ? sym_asm_name(global)
                           : mir_declared_link_name(insn->name));
        if ((memory_storage == SC_EXTERN ||
             (global != NULL && global->storage == SC_FUNC &&
              global->needs_extrn)) &&
            mir_extrn_should_emit(global))
            fprintf(out, "\textrn %s\n", assembly_name);
        fprintf(out, "\tld hl,%s\n", assembly_name);
    } else if (mir_declared_is_vla_object(insn->name)) {
        fprintf(out, "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
                memory_offset, memory_offset + 1);
    } else {
        fputs("\tpush ix\n\tpop hl\n", out);
        mir_emit_hl_offset_from_ix(out, memory_offset);
    }
    return 1;
}

static int mir_virtual_iy_offset(int value)
{
    return mir_virtual_offset(value) + mir_effective_local_bytes() +
           mir.aggregate_temp_bytes;
}

/* Item 15 (mir-migration-plan-100): count exact CFG predecessors of a label
 * instruction using the successors[]/successor_count arrays mir_verify_and_dump
 * already computes for every function (jump targets, branch-false
 * fallthrough+target, plain fallthrough). Reused instead of re-deriving the
 * CFG so this stays consistent with liveness/allocation, which trust the
 * same arrays. */
static int mir_label_predecessor_count(int label_instruction)
{
    int predecessor;
    int successor;
    int count = 0;

    for (predecessor = 0; predecessor < mir.count; ++predecessor) {
        const struct MirInsn *insn = &mir.insns[predecessor];
        for (successor = 0; successor < insn->successor_count; ++successor)
            if (insn->successors[successor] == label_instruction)
                ++count;
    }
    return count;
}

/* Item 15 (mir-migration-plan-100): a value's "next instruction" for
 * HL-forwarding purposes may legitimately sit just past a MIR_LABEL that is
 * textually adjacent but has exactly one CFG predecessor (i.e. it is not a
 * real merge point - the label exists only because some other emitter
 * concern, such as a goto target or loop back-edge landing elsewhere,
 * required a name for this position). Skip at most one such label, in
 * addition to any NOPs, so straight-line code that happens to be split by a
 * label still gets the same forwarding Item 13 already applies across NOPs.
 * Skipping more than one label at a time is deliberately not supported: it
 * would require reasoning about a chain of merges instead of a single,
 * locally-verifiable non-merge point. */

/* Item T30 (mir-text-size-plan.md): a MIR_CONST whose only consumer is the
 * immediately-following MIR_BINARY as a fusable const-zero-RHS (or
 * signed-zero-sign-test) comparison emits no code of its own at all - the
 * binary's own emission (see the "Item 25"/"Item 27" comments further down
 * this file) skips materializing that 0 into DE entirely and tests HL
 * directly. Such a MIR_CONST is therefore just as transparent for
 * forwarding purposes as a MIR_NOP: nothing runs between the forwarded
 * value's own definition and the comparison that actually consumes it. This
 * was previously invisible to mir_forward_skip_target_ex, which only looked
 * through MIR_NOP/single-predecessor MIR_LABEL, so a call result (or any
 * other HL-forwarding candidate) immediately followed by "compare != 0"
 * always fell back to a full backend-slot store/reload round trip even
 * though the constant in between never touches HL. */
static int mir_const_is_transparent_zero_rhs_operand(int instruction)
{
    const struct MirInsn *constant;
    const struct MirInsn *binary;

    if (instruction < 0 || instruction + 1 >= mir.count)
        return 0;
    constant = &mir.insns[instruction];
    binary = &mir.insns[instruction + 1];
    if (constant->opcode != MIR_CONST || binary->opcode != MIR_BINARY ||
        binary->src2 != constant->dst)
        return 0;
    return mir_binary_is_fusable_comparison(instruction + 1) > 0 &&
        (mir_fused_compare_is_const_zero_rhs(instruction + 1) ||
         mir_fused_compare_is_signed_zero_sign_test(instruction + 1));
}

/* Item T74 (mir-text-size-plan.md): a MIR_CONST whose only consumer is the
 * immediately-following MIR_INDEX_ADDRESS, where the constant is the index
 * operand (src2) and folds to a zero byte offset (index value * element
 * stride == 0, i.e. a compile-time-constant `arr[0]`-shaped access), is
 * just as transparent for forwarding purposes as the const-zero-rhs-
 * comparison case above: mir_can_forward_stack_to_index's own const-index
 * emission branch (see the MIR_INDEX_ADDRESS case further down this file)
 * already special-cases this exact shape to skip the `add hl,de` entirely
 * when byte_offset is 0, so the base address's value is never touched by
 * anything between its own definition and the index-address instruction
 * that reuses it unchanged. Before this item, mir_forward_skip_target_ex
 * did not know this, so mir_can_forward_hl_to_next always declined (the
 * intervening real MIR_CONST instruction was never skipped), leaving
 * mir_can_forward_stack_to_index (a strictly more expensive push-then-pop
 * round trip, needed only when a real `add hl,de` follows) to win by
 * default even for this zero-offset case where no register value is ever
 * disturbed at all. Found via tlngfptr.c's main (`operations[0]`, a
 * function-pointer table lookup), newly MIR-reachable once Item T74's own
 * MIR_UNARY forwarding widened mir_can_forward_hl_de_to_next - which
 * exposed this pre-existing, unrelated gap as a real (not just static-
 * metric) regression once the function actually reached MIR emission. */
static int mir_const_is_transparent_zero_index_operand(int instruction)
{
    const struct MirInsn *constant;
    const struct MirInsn *index;

    if (instruction < 0 || instruction + 1 >= mir.count)
        return 0;
    constant = &mir.insns[instruction];
    index = &mir.insns[instruction + 1];
    if (constant->opcode != MIR_CONST || index->opcode != MIR_INDEX_ADDRESS ||
        index->src2 != constant->dst || index->base_name[0] != 0)
        return 0;
    return constant->immediate * index->immediate == 0;
}

/* Item T83 (mir-text-size-plan.md): a MIR_STORE this selector itself
 * proves dead (mir_store_is_dead, or a fully-promoted object needing no
 * physical storage at all) emits zero machine instructions - exactly
 * the same "safe to look through, nothing here" property MIR_NOP
 * already has for forwarding-adjacency purposes. Without this, a value
 * whose immediately-following use (through the intervening dead store)
 * is a single-operand consumer (return, the next binary/unary operand,
 * a call argument, ...) never gets to forward at all: the dead store
 * still occupies a real instruction slot in mir.insns, so every existing
 * adjacency check anchored on it saw a non-NOP, non-transparent
 * instruction and gave up, forcing a genuinely redundant slot reload
 * even though nothing at all executes between the value's own
 * definition and this later read. Found via tests/tc89decl.c's timpreg
 * (`register c; c = a + b; return c;` - "c"'s own store is dead since
 * nothing ever loads "c" as an object again, every later use reads the
 * binary's own SSA value directly) and tests/tmirslot.c's cross_call
 * (`saved = a + b; return scale(saved) - saved;`, the same shape with an
 * intervening call). */
static int mir_instruction_is_transparent_dead_store(int instruction)
{
    int transparent =
        mir.insns[instruction].opcode == MIR_STORE &&
        (mir_object_is_fully_promoted(mir.insns[instruction].object) ||
         mir_store_is_dead(instruction));

    if (transparent)
        mir_forward_skip_last_skipped_dead_store = 1;
    return transparent;
}

static int mir_forward_skip_target_ex(int instruction, int *out_skipped_label)
{
    int next_instruction = instruction + 1;
    int skipped_label = 0;

    mir_forward_skip_last_skipped_dead_store = 0;
    for (;;) {
        while (next_instruction < mir.count &&
               (mir.insns[next_instruction].opcode == MIR_NOP ||
                mir_const_is_transparent_zero_rhs_operand(next_instruction) ||
                mir_const_is_transparent_zero_index_operand(
                    next_instruction) ||
                mir_instruction_is_transparent_dead_store(next_instruction)))
            ++next_instruction;
        if (!skipped_label && next_instruction < mir.count &&
            mir.insns[next_instruction].opcode == MIR_LABEL &&
            mir_label_predecessor_count(next_instruction) == 1) {
            skipped_label = 1;
            ++next_instruction;
            continue;
        }
        break;
    }
    if (out_skipped_label != NULL)
        *out_skipped_label = skipped_label;
    return next_instruction;
}

static int mir_forward_skip_target(int instruction)
{
    return mir_forward_skip_target_ex(instruction, NULL);
}

static int mir_forward_note_success(void)
{
    if (mir_forward_skip_last_skipped_dead_store)
        mir_spilled_cfg_used_dead_store_forwarding = 1;
    return 1;
}

static int mir_is_general_rhs_stack_forward(int value, int consumer)
{
    const struct MirInsn *binary;

    if (consumer < 0 || consumer >= mir.count)
        return 0;
    binary = &mir.insns[consumer];
    return binary->opcode == MIR_BINARY && binary->src2 == value &&
           !mir_binary_only_constant(binary->src1);
}

static int mir_is_indirect_store_value_stack_forward(int value, int consumer)
{
    const struct MirInsn *store;

    if (consumer < 0 || consumer >= mir.count)
        return 0;
    store = &mir.insns[consumer];
    return store->opcode == MIR_STORE_INDIRECT && store->src2 == value &&
           store->bit_width == 0 && store->memory_size > 0 &&
           store->memory_size <= 2;
}

static int mir_is_branch_condition_forward(int value, int consumer)
{
    return consumer >= 0 && consumer < mir.count &&
           mir.insns[consumer].opcode == MIR_BRANCH_FALSE &&
           mir.insns[consumer].src1 == value;
}

/* Item T40 (mir-text-size-plan.md): the wide (32-bit, HL:DE) analog of
 * mir_can_forward_hl_to_next below. Items 1-32 built a rich forwarding
 * predicate for 16-bit scalar values (this function plus its
 * MIR_STORE/call-argument/stack-index siblings), but mir_emit_virtual_
 * store_wide had no equivalent at all until this item - every wide value
 * with an assigned backend slot was unconditionally spilled and reloaded,
 * even when its single next use could consume it directly from HL:DE.
 * Deliberately started with only the single narrowest, most-certain
 * consumer shape (MIR_RETURN, mirroring this function's own MIR_RETURN
 * case and VLA guard exactly) rather than the full consumer switch below -
 * per SKILL.md's staging discipline, generalize to MIR_STORE/MIR_BINARY
 * consumers only after this narrow slice is validated end to end.
 *
 * Item T74 (mir-text-size-plan.md): add MIR_UNARY as a second recognized
 * immediate consumer, mirroring mir_can_forward_hl_to_next's own
 * already-proven-safe MIR_UNARY whitelist entry for the narrow (16-bit)
 * case (see the `case MIR_INDEX_ADDRESS: case MIR_MEMBER_ADDRESS: case
 * MIR_LOAD_INDIRECT: case MIR_UNARY: break;` group below). Found via a
 * forced-accept diff on tlongreg.c's use_after_long_return: a wide call
 * result (`x = ret_high_only();`) whose sole use is an immediately
 * following identity-cast MIR_UNARY (the implicit assignment-conversion
 * dcc inserts even when the declared and source types already match) was
 * always spilled to its own backend slot and reloaded one instruction
 * later, purely because this predicate only recognized MIR_RETURN -
 * duplicating work mir_emit_virtual_store_wide's own MIR_CALL result
 * homing had already covered. Kept to a plain adjacency shape identical
 * to MIR_RETURN's own (no additional skipped-label allowance - the
 * existing `skipped_label && next->opcode != MIR_RETURN` guard just
 * below already rejects a label-crossed MIR_UNARY, so this stays exactly
 * as conservative as the existing VLA-guarded MIR_RETURN path). */
static int mir_wide_binary_rhs_is_commutative(
    const struct MirInsn *binary)
{
    const struct MirInsn *left;

    if (binary == NULL || binary->opcode != MIR_BINARY ||
        type_size(binary->secondary_offset) != 4 ||
        type_is_float(binary->secondary_offset))
        return 0;
    switch ((int)binary->immediate) {
    case '+':
    case '|':
    case '^':
    case TOK_EQ:
    case TOK_NE:
        return 1;
    case '&':
    case '*':
        left = mir_definition(binary->src1);
        return left == NULL || left->opcode != MIR_CONST;
    }
    return 0;
}

static int mir_wide_binary_rhs_pair_supported(
    int value, const struct MirInsn *binary)
{
    const struct MirInsn *definition = mir_definition(value);

    if (definition == NULL || !mir_wide_binary_rhs_is_commutative(binary))
        return 0;
    if (definition->opcode == MIR_UNARY)
        return 1;
    return definition->opcode == MIR_BINARY && binary->immediate == '*';
}

static int mir_can_forward_hl_de_to_next(int value)
{
    const struct MirInsn *next;
    int next_instruction;
    int instruction;
    int skipped_label;

    if (mir_emit_instruction_index < 0 ||
        mir_emit_instruction_index + 1 >= mir.count)
        return 0;
    next_instruction = mir_forward_skip_target_ex(mir_emit_instruction_index,
                                                   &skipped_label);
    if (next_instruction >= mir.count)
        return 0;
    next = &mir.insns[next_instruction];
    if (skipped_label && next->opcode != MIR_RETURN)
        return 0;
    if (next->opcode == MIR_RETURN) {
        if (next->src1 != value)
            return 0;
        if (mir.has_vla)
            return 0;
    } else if (next->opcode == MIR_UNARY) {
        if (next->src1 != value)
            return 0;
    } else if (mir_wide_binary_lhs_forwarding_enabled &&
               next->opcode == MIR_BINARY &&
               next->src1 == value &&
               type_size(next->secondary_offset) == 4) {
        /* The wide binary emitter consumes src1 first and immediately
         * pushes DE:HL before materializing src2, so an adjacent producer
         * can remain resident exactly as it can for MIR_UNARY/RETURN. */
    } else if (mir_wide_store_forwarding_enabled &&
               next->opcode == MIR_STORE && next->src1 == value &&
               (next->memory_size == 4 || type_size(next->type) == 4)) {
        /* The named store consumes DE:HL before any other value is formed. */
    } else if (mir_wide_binary_rhs_forwarding_enabled &&
               next_instruction == mir_emit_instruction_index + 1 &&
               mir_wide_binary_rhs_pair_supported(value, next) &&
               next->src2 == value) {
        /* The producer pushes src2 once. The binary then loads src1 into
         * DE:HL and uses the same stack/current-register convention with
         * physically swapped operands. */
    } else {
        return 0;
    }
    for (instruction = next_instruction + 1;
         instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->src1 == value || insn->src2 == value)
            return 0;
        if ((insn->opcode == MIR_CALL ||
             insn->opcode == MIR_CALL_AGGREGATE) &&
            mir_call_uses_value(insn, value))
            return 0;
    }
    return mir_forward_note_success();
}

static int mir_can_forward_hl_to_next(int value)
{
    const struct MirInsn *definition = mir_definition(value);
    const struct MirInsn *next;
    int next_instruction;
    int instruction;
    int skipped_label;

    if (mir_emit_instruction_index < 0 ||
        mir_emit_instruction_index + 1 >= mir.count)
        return 0;
    if (definition != NULL && definition->opcode == MIR_CALL_AGGREGATE)
        return 0;
    next_instruction = mir_forward_skip_target_ex(mir_emit_instruction_index,
                                                   &skipped_label);
    if (next_instruction >= mir.count)
        return 0;
    next = &mir.insns[next_instruction];
    /* A pure MIR_NOP skip (skipped_label == 0) never crosses a CFG edge -
     * MIR_NOP is a same-block rename/metadata marker that emits no code and
     * has no live-range implications, so it is safe to look through for
     * every consuming opcode, not just MIR_RETURN. Only a skipped LABEL
     * (a real block boundary) still needs the narrower MIR_RETURN-only
     * carve-out below, since that is the one hazard actually analyzed
     * (VLA frame reuse across a skipped-to return). Item T29
     * (mir-text-size-plan.md): the old check required exact physical
     * adjacency for every opcode except MIR_RETURN, so a single
     * intervening MIR_NOP (e.g. a global/local rename marker emitted
     * immediately before a MIR_STORE) defeated forwarding even though
     * mir_forward_skip_target already looked straight through it. */
    if (skipped_label && next->opcode != MIR_RETURN)
        return 0;
    /* MIR_RETURN may be reached via a non-adjacent skip (see
     * mir_forward_skip_target above), so the forwarded value's home must
     * still be live at that point. VLA frames can reuse/shrink stack space
     * between the definition and a skipped-to return, so forwarding across
     * a VLA-bearing function is unsafe; that is the only known hazard.
     * (Item T13 / mir-text-size-plan.md: this used to also require
     * !mir_function_has_any_call(), a broad whole-function gate with no
     * call-adjacency link to the forwarded value; removing it, validated
     * against the whole corpus plus full-mode runs, uncovered no
     * correctness issue and only unlocked more RETURN-adjacent forwards.) */
    if (next->opcode == MIR_RETURN && mir.has_vla)
        return 0;
    if (next->opcode == MIR_INDEX_ADDRESS) {
        if (next->src2 == value) {
            /* Existing case: forward the index operand. */
        } else if (next->src1 == value && next->base_name[0] == 0) {
            /* Item T74 (mir-text-size-plan.md): forward the base operand
             * too, but only for the fixed-stride (base_name[0] == 0)
             * const-index shape, where mir_emit_virtual_load(out,
             * insn->src1) runs first (loading the base, consuming this
             * exact forward) and any byte-offset add is a separate,
             * independent step afterward - see the const-index emission
             * branch further down this file. The runtime-stride/non-const-
             * index shapes both load the index operand into HL first (to
             * multiply/combine it with the base), which would clobber a
             * forwarded base value before it is ever used, so this branch
             * must not accept those. Restricting to a MIR_CONST index
             * definition mirrors mir_const_is_transparent_zero_index_
             * operand's own scope (found via tlngfptr.c's main,
             * `operations[0]`, a function-pointer table lookup newly
             * MIR-reachable once this item's own MIR_UNARY wide-forwarding
             * fix widened coverage elsewhere in the same function). */
            const struct MirInsn *index_definition =
                mir_definition(next->src2);
            if (index_definition == NULL ||
                index_definition->opcode != MIR_CONST)
                return 0;
        } else {
            return 0;
        }
    } else if (next->opcode == MIR_STORE_INDIRECT &&
               next->src2 == value &&
               mir_constant_absolute_access_supported(next)) {
        /* A direct absolute store consumes its value without first loading
         * the address into HL, so an immediately preceding producer can
         * forward HL exactly like it can for MIR_STORE. */
    } else if (next->opcode == MIR_BINARY && next->src2 == value &&
               mir_forwarded_stack_value == next->src1 &&
               mir_forwarded_stack_target_instruction ==
                   next_instruction) {
        /* The binary's left operand is already below SP. Keep this
         * adjacent right operand in HL; the binary emitter moves it to DE
         * before popping the left operand back into HL. */
    } else if (next->src1 != value)
            return 0;
    switch (next->opcode) {
    case MIR_INDEX_ADDRESS:
    case MIR_MEMBER_ADDRESS: case MIR_LOAD_INDIRECT: case MIR_UNARY:
        break;
    case MIR_BRANCH_FALSE:
        {
            int target;
            int first_phi;
            if (!mir_branch_condition_forwarding_enabled)
                return 0;
            if (next->label < 0 || next->label >= mir.next_label)
                return 0;
            target = mir_find_label(next->label);
            if (target < 0)
                return 0;
            first_phi = mir_first_phi_or_block_end(target);
            if (first_phi < mir.count &&
                mir.insns[first_phi].opcode == MIR_PHI)
                return 0;
        }
        break;
    case MIR_BINARY:
        if (type_size(next->secondary_offset) == 4)
            return 0;
        break;
    case MIR_STORE_INDIRECT:
        if (next->bit_width > 0 || next->memory_size > 2)
            return 0;
        break;
    case MIR_RETURN:
        break;
    case MIR_STORE:
        {
            int memory_type;
            int memory_storage;
            int memory_offset;
            int producer_opcode = mir.insns[mir_emit_instruction_index].opcode;
            /* Item T31 (mir-text-size-plan.md): MIR_CALL was never in this
             * whitelist because, until Item T30, mir_can_forward_hl_to_next
             * unconditionally excluded every call result before reaching
             * this switch at all - the omission was never reachable, not a
             * deliberate safety exclusion (see Items 6/7/8, which added this
             * whitelist for binary/unary/const producers only, the set that
             * *was* reachable at the time). A call result forwarded directly
             * into its destination's store needs nothing beyond what any
             * other producer here needs: the value sits in HL right after
             * the call returns, and the store's own address computation
             * below is a fixed ix-relative offset unaffected by whatever
             * else the call clobbered.
             *
             * Item T37 (mir-text-size-plan.md): MIR_ADDRESS was missing
             * from this same whitelist for the same reason - not a
             * deliberate exclusion, just never added. A MIR_ADDRESS result
             * (a local/global's own address, e.g. `int *p = &x;`) is a pure,
             * side-effect-free computation exactly like MIR_CONST - its
             * value sits in HL right after computing it, and nothing about
             * the following store's own fixed ix-relative destination
             * offset depends on how the stored value was produced. Found
             * via a forced-accept diff on tmirfast.c's inc_observe (`int
             * *p = &x;`), which crossed the text-size acceptance threshold
             * as a side effect of Item T36 and exposed a real (not just
             * static-metric) cycle-count regression from this exact dead
             * round trip - store-to-temp-slot, reload, store-to-p's-real-
             * slot - for &x's address.
             *
             * Item T38 (mir-text-size-plan.md): MIR_LOAD (a plain,
             * non-indirect load of a named object/global, e.g. `y = x;`)
             * was missing too, even though MIR_LOAD_INDIRECT (`y = *p;`)
             * was already whitelisted - an inconsistency with no safety
             * rationale, since a plain load is if anything simpler than an
             * indirect one (no pointer dereference at all). Found via a
             * synthetic `g2 = g1;` (both plain globals) test after auditing
             * every MIR_* opcode against this whitelist post-T37: the load
             * still spilled to a temporary slot and reloaded before the
             * following store, an identical dead round trip to the T31/T37
             * cases. The value sits in HL right after the load completes,
             * exactly like every other whitelisted producer here.
             *
             * Item T39 (mir-text-size-plan.md): the rest of the "address"
             * family - MIR_STRING_ADDRESS (a string literal's address, e.g.
             * `gp = "hello";`), MIR_MEMBER_ADDRESS (`gp = &s.field;`), and
             * MIR_INDEX_ADDRESS (`gp = &arr[i];`) - were missing for the
             * exact same reason MIR_ADDRESS was (Item T37): none are a
             * deliberate exclusion, all are pure, side-effect-free address
             * computations whose value sits in HL right after computing it,
             * with no dependency on the following store's own fixed
             * ix-relative destination offset. Confirmed via three synthetic
             * tests (mirroring T38's methodology), each showing the
             * identical dead round trip. MIR_COMPOUND_ADDRESS was checked
             * too but a synthetic trigger for it was not found this
             * session - left out until a concrete motivating case
             * confirms it needs the same treatment. */
            if (mir_object_is_fully_promoted(next->object) ||
                (producer_opcode != MIR_LOAD_INDIRECT &&
                 producer_opcode != MIR_LOAD &&
                 producer_opcode != MIR_BINARY &&
                 producer_opcode != MIR_UNARY &&
                 producer_opcode != MIR_CONST &&
                 producer_opcode != MIR_CALL &&
                 producer_opcode != MIR_ADDRESS &&
                 producer_opcode != MIR_STRING_ADDRESS &&
                 producer_opcode != MIR_MEMBER_ADDRESS &&
                 producer_opcode != MIR_INDEX_ADDRESS) ||
                !mir_scalar_memory_location(next, &memory_type,
                                            &memory_storage, &memory_offset) ||
                type_is_struct_object(memory_type) ||
                type_size(memory_type) > 2)
                return 0;
        }
        break;
    default:
        return 0;
    }
    for (instruction = next_instruction + 1;
         instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->src1 == value || insn->src2 == value)
            return 0;
        if ((insn->opcode == MIR_CALL ||
             insn->opcode == MIR_CALL_AGGREGATE) &&
            mir_call_uses_value(insn, value))
            return 0;
    }
    return mir_forward_note_success();
}

static int mir_dynamic_index_base_forward_target(int value)
{
    const struct MirInsn *index;
    const struct MirInsn *index_definition;
    int target;
    int instruction;

    if (mir_emit_instruction_index < 0 ||
        mir_emit_instruction_index + 1 >= mir.count)
        return -1;
    target = mir_emit_instruction_index + 1;
    while (target < mir.count && mir.insns[target].opcode == MIR_NOP)
        ++target;
    if (target >= mir.count)
        return -1;
    index = &mir.insns[target];
    index_definition = mir_definition(index->src2);
    if (index->opcode != MIR_INDEX_ADDRESS ||
        index->base_name[0] != 0 || index->src1 != value ||
        (index_definition != NULL &&
         index_definition->opcode == MIR_CONST))
        return -1;
    for (instruction = target + 1;
         instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->src1 == value || insn->src2 == value)
            return -1;
        if ((insn->opcode == MIR_CALL ||
             insn->opcode == MIR_CALL_AGGREGATE) &&
            mir_call_uses_value(insn, value))
            return -1;
    }
    return target;
}

static int mir_stack_index_forward_target(int value)
{
    const struct MirInsn *middle;
    const struct MirInsn *index;
    int instruction;
    int target;

    /* Item T4 (mir-text-size-plan.md): mir_virtual_iy_base was a constant
     * 0 (dead scaffolding) when this helper and its gate were introduced
     * in 938c45b, so this optimization was dead on arrival - it only
     * started firing, for large frames only, once a later session gave
     * mir_virtual_iy_base a real value. The push/pop handoff below is a
     * self-contained physical-stack round-trip over a fixed 2-instruction
     * window with no intervening branch or call, unrelated to whether the
     * eventual store destination would use ix- or iy-relative addressing,
     * so the gate is removed the same way Item T3 removed the analogous
     * dead gate from mir_can_forward_hl_to_next. */
    if (mir_emit_instruction_index >= 0 &&
        mir_emit_instruction_index + 2 < mir.count) {
        middle = &mir.insns[mir_emit_instruction_index + 1];
        index = &mir.insns[mir_emit_instruction_index + 2];
        if (middle->opcode == MIR_CONST &&
            index->opcode == MIR_INDEX_ADDRESS &&
            index->base_name[0] == 0 &&
            index->src1 == value && index->src2 == middle->dst) {
            target = mir_emit_instruction_index + 2;
            for (instruction = target + 1;
                 instruction < mir.count; ++instruction) {
                const struct MirInsn *insn = &mir.insns[instruction];
                if (insn->src1 == value || insn->src2 == value ||
                    ((insn->opcode == MIR_CALL ||
                      insn->opcode == MIR_CALL_AGGREGATE) &&
                     mir_call_uses_value(insn, value)))
                    return -1;
            }
            return target;
        }
    }
    return mir_dynamic_index_base_forward_target(value);
}

static int mir_call_only_constant(int value)
{
    const struct MirInsn *definition = mir_definition(value);
    int argument_count = 0;
    int instruction;

    if (definition == NULL ||
        (definition->opcode != MIR_CONST &&
         definition->opcode != MIR_FLOAT_CONST &&
         definition->opcode != MIR_STRING_ADDRESS))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->opcode == MIR_ARG && insn->src1 == value) {
            ++argument_count;
            continue;
        }

        if (insn->src1 == value || insn->src2 == value)
            return 0;
    }
    return argument_count == 1;
}

static int mir_scalar_constant_is_rematerializable(int value)
{
    const struct MirInsn *definition = mir_definition(value);

    return definition != NULL && definition->opcode == MIR_CONST &&
           type_size(definition->type) <= 2 &&
           (mir_cfg_block_count() == 1 || mir.has_vla);
}

static int mir_string_address_is_rematerializable(int value)
{
    const struct MirInsn *definition = mir_definition(value);

    return definition != NULL &&
           definition->opcode == MIR_STRING_ADDRESS;
}

static int mir_wide_constant_is_rematerializable(int value)
{
    const struct MirInsn *definition = mir_definition(value);

    return definition != NULL &&
           (definition->opcode == MIR_CONST ||
            definition->opcode == MIR_FLOAT_CONST) &&
           type_size(definition->type) == 4 &&
           mir_cfg_block_count() == 1;
}

void mir_begin_address_rematerialization(void)
{
    mir_address_rematerialization_enabled = 1;
}

void mir_end_address_rematerialization(void)
{
    mir_address_rematerialization_enabled = 0;
}

static int mir_address_is_rematerializable_candidate(int value)
{
    const struct MirInsn *definition = mir_definition(value);
    int memory_type;
    int memory_storage;
    int memory_offset;
    int uses;

    if (definition == NULL || definition->opcode != MIR_ADDRESS ||
        mir_declared_is_vla_object(definition->name) ||
        !mir_scalar_memory_location(definition, &memory_type,
                                    &memory_storage, &memory_offset))
        return 0;
    uses = mir_value_use_count(value);
    return uses > 0 && uses <= 2;
}

static int mir_address_is_rematerializable(int value)
{
    return mir_address_rematerialization_enabled &&
           mir_address_is_rematerializable_candidate(value);
}

int mir_address_rematerialization_candidate_count(void)
{
    int count = 0;
    int value;

    for (value = 0; value < mir.next_value; ++value)
        if (mir_address_is_rematerializable_candidate(value))
            ++count;
    return count;
}

/* Evaluates a scalar binary arithmetic/bitwise/shift operation over two
 * compile-time constant operands, truncated and sign-extended to match
 * `operand_type` (16-bit int or 32-bit long, per its TYPE_UNSIGNED flag),
 * so a later Item 13 fold sees the exact result the target Z80 code would
 * compute at runtime. Deliberately excludes relational/equality operators
 * (Item 14's scope, a separate fold) and refuses to fold a division or
 * modulo by a zero divisor, leaving that case to emit its normal runtime
 * instruction sequence unchanged (matching prior behavior for this
 * already-undefined-in-C case). Assumes the host `long` is at least
 * 32 bits wide, true for every host this project builds on. */
int mir_fold_constant_binary(int op, long left, long right,
                                    int operand_type, long *result)
{
    int type_bytes = type_size(operand_type);
    int is_unsigned = (operand_type & TYPE_UNSIGNED) != 0;
    long value;
    unsigned long mask;

    switch (op) {
    case '+': value = left + right; break;
    case '-': value = left - right; break;
    case '*': value = left * right; break;
    case '/':
        if (right == 0)
            return 0;
        value = left / right;
        break;
    case '%':
        if (right == 0)
            return 0;
        value = left % right;
        break;
    case '&': value = left & right; break;
    case '|': value = left | right; break;
    case '^': value = left ^ right; break;
    case TOK_SHL:
        if (right < 0 || right >= type_bytes * 8)
            return 0;
        value = left << right;
        break;
    case TOK_SHR:
        if (right < 0 || right >= type_bytes * 8)
            return 0;
        value = left >> right;
        break;
    default:
        return 0;
    }
    mask = type_bytes == 2 ? 0xffffUL
         : type_bytes == 4 ? 0xffffffffUL : (unsigned long)-1L;
    value = (long)((unsigned long)value & mask);
    if (!is_unsigned) {
        if (type_bytes == 2 && (value & 0x8000L) != 0)
            value -= 0x10000L;
        else if (type_bytes == 4 && (value & 0x80000000L) != 0)
            value -= 0x100000000L;
    }
    *result = value;
    return 1;
}

/* Evaluates a scalar relational/equality operation over two compile-time
 * constant operands, comparing them per `operand_type`'s width and
 * signedness (the common type both operands were already converted to by
 * the caller), and produces the boolean 0/1 result the target Z80 compare
 * sequence would compute at runtime. A separate fold from Item 13's
 * arithmetic/bitwise/shift fold above since the result here is always a
 * width-independent boolean rather than a value truncated to
 * `operand_type`. */
int mir_fold_constant_compare(int op, long left, long right,
                                     int operand_type, long *result)
{
    int type_bytes = type_size(operand_type);
    int is_unsigned = mir_type_uses_unsigned_comparison(operand_type);
    int cmp;

    if (is_unsigned) {
        unsigned long mask = type_bytes == 2 ? 0xffffUL
                            : type_bytes == 4 ? 0xffffffffUL
                                               : (unsigned long)-1L;
        unsigned long uleft = (unsigned long)left & mask;
        unsigned long uright = (unsigned long)right & mask;
        cmp = uleft < uright ? -1 : uleft > uright ? 1 : 0;
    } else {
        cmp = left < right ? -1 : left > right ? 1 : 0;
    }
    switch (op) {
    case TOK_EQ: *result = cmp == 0; break;
    case TOK_NE: *result = cmp != 0; break;
    case '<':    *result = cmp < 0; break;
    case '>':    *result = cmp > 0; break;
    case TOK_LE: *result = cmp <= 0; break;
    case TOK_GE: *result = cmp >= 0; break;
    default: return 0;
    }
    return 1;
}

static int mir_binary_only_constant(int value)
{
    const struct MirInsn *definition = mir_definition(value);
    int binary_count = 0;
    int binary_result = -1;
    int instruction;

    if (definition == NULL ||
        definition->opcode != MIR_CONST)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->opcode == MIR_BINARY &&
            (insn->src1 == value || insn->src2 == value) &&
            type_size(insn->secondary_offset) <= 2) {
            if (mir.has_vla &&
                (insn->immediate == TOK_EQ || insn->immediate == TOK_NE ||
                 insn->immediate == '<' || insn->immediate == '>' ||
                 insn->immediate == TOK_LE || insn->immediate == TOK_GE))
                return 0;
            ++binary_count;
            binary_result = insn->dst;
            continue;
        }
        if (insn->src1 == value || insn->src2 == value)
            return 0;
    }
    if (binary_count != 1)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_VLA_ALLOC &&
            mir.insns[instruction].src1 == binary_result)
            return 0;
    return 1;
}

/* Item T18 (mir-text-size-plan.md): MIR_INDEX_ADDRESS's fixed-stride case
 * (insn->base_name[0] == 0, i.e. not the runtime-variable-stride path)
 * resolves a constant index entirely at compile time - byte_offset =
 * index_definition->immediate * insn->immediate, materialized directly
 * via `ld de,<byte_offset>` - and never reads the index MIR_CONST's own
 * runtime value at all (mir_emit_virtual_load(out, insn->src2) is only
 * called in the *other* two MIR_INDEX_ADDRESS shapes: the runtime-stride
 * base_name case, and the non-constant-index case). A MIR_CONST whose
 * sole use is exactly this shape is therefore genuinely dead: its own
 * `ld hl,<const>` materialization (and any spill store) computes a value
 * nothing ever reads, the same class of defect mir_binary_only_constant/
 * mir_call_only_constant/mir_multiply_by_small_constant already prevent
 * for their respective consumer shapes. */
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

/* Caps how many Z80 instructions the shift/add decomposition below may
 * unroll to before falling back to __mulu. Mirrors MUL_CONST_MAX_OPS in
 * dcc_ops.c's emit_mul_hl_const, which this routine is a port of, so the
 * MIR backend reaches the same code-size/quality tradeoff as the legacy
 * AST backend for constant multiplication. */
#define MIR_MUL_CONST_MAX_OPS 10

/* Item 30 (mir-migration-plan-100): a contiguous run of set bits at the
 * bottom of the multiplier (uv == (1 << k) - 1 for some k, e.g. 7, 15, 31,
 * 63, 127...) is cheaper to build as "(x << k) - x" (k doublings plus one
 * 16-bit subtract) than as the naive per-bit shift/add decomposition below
 * (k-1 doublings plus k-1 adds). Returns the run length k and 1 if uv is
 * such a run (k in [2,16] - k=1 would be uv=1, already handled as the
 * trivial multiply-by-one case before this path is ever reached), 0
 * otherwise. */
static int mir_mul_const_is_ones_run(unsigned long uv, int *shift_count)
{
    int k;

    for (k = 2; k <= 16; ++k)
        if (uv == (1uL << (unsigned)k) - 1uL) {
            *shift_count = k;
            return 1;
        }
    return 0;
}

/* Number of instructions mir_emit_mul_hl_const_general would emit for uv (a
 * 16-bit unsigned pattern, uv != 0 and not already a single power of two):
 * one "add hl,hl" per bit position below the highest set bit (the
 * doublings), plus one "add hl,de" per OTHER set bit (the highest bit
 * itself is free - it's the starting value). */
static int mir_mul_const_naive_op_count(unsigned long uv)
{
    int bit;
    int highest = -1;
    int adds = 0;

    for (bit = 15; bit >= 0; --bit) {
        if (uv & (1uL << (unsigned)bit)) {
            if (highest < 0)
                highest = bit;
            else
                ++adds;
        }
    }
    if (highest <= 0)
        return 0;
    return highest + adds;
}

/* Single source of truth for the instruction cost of a constant multiply
 * that isn't already a plain power of two: the cheaper of the naive
 * per-bit decomposition and the Item 30 shift-and-subtract form for a
 * bottom-aligned run of ones, so a caller never has to duplicate the
 * "which form is cheaper" comparison mir_emit_mul_hl_const_general also
 * makes. */
static int mir_mul_const_op_count(unsigned long uv)
{
    int naive = mir_mul_const_naive_op_count(uv);
    int shift_count;

    if (mir_mul_const_is_ones_run(uv, &shift_count) &&
        shift_count + 1 < naive)
        return shift_count + 1;
    return naive;
}

/* True if value 'v' is the size operand feeding a VLA allocation.
 * A general (non-power-of-two) strength-reduced multiply that both
 * sizes a VLA allocation *and* shares the function with a later integer
 * division/modulo drives a runtime stack-pointer adjustment whose
 * surrounding VLA frame slot traffic is undercounted by the static
 * byte/instruction cost gate: it is cheap in bytes yet not free in
 * cycles, and only the peephole optimizer (dccpeep) currently cleans it
 * up. Restrict that specific combination to the existing __mulu path;
 * VLA-alloc-sizing multiplies without a subsequent division (e.g. a
 * pure sizeof expression) are unaffected and still benefit below. */
static int mir_value_feeds_vla_alloc(int v)
{
    int i;

    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_VLA_ALLOC && mir.insns[i].src1 == v)
            return 1;
    return 0;
}

static int mir_has_integer_division(void)
{
    int i;

    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_BINARY &&
            (mir.insns[i].immediate == '/' || mir.insns[i].immediate == '%'))
            return 1;
    return 0;
}

/* Single source of truth for whether a constant multiply may use the
 * unrolled shift/add fast path (mir_emit_mul_hl_const) instead of __mulu.
 * Both the frame slot-count accounting (mir_multiply_by_small_constant)
 * and the actual emission site in mir_try_emit_spilled_scalar_cfg call
 * this, so they cannot fall out of sync the way they once did. */
int mir_mul_const_fast_path_eligible(unsigned long multiplier, int dst)
{
    return multiplier == 0 ||
           (multiplier & (multiplier - 1)) == 0 ||
           (mir_mul_const_op_count(multiplier) <= MIR_MUL_CONST_MAX_OPS &&
            !(mir_value_feeds_vla_alloc(dst) && mir_has_integer_division()));
}

static int mir_multiply_by_small_constant(int value)
{
    const struct MirInsn *definition = mir_definition(value);
    int uses = 0;
    int instruction;

    if (definition == NULL || definition->opcode != MIR_CONST)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->src1 == value)
            return 0;
        if (insn->src2 != value)
            continue;
        if (insn->opcode != MIR_BINARY || insn->immediate != '*')
            return 0;
        ++uses;
    }
    if (uses != 1)
        return 0;
    {
        unsigned long multiplier =
            (unsigned long)definition->immediate & 0xffffUL;
        int multiply_instruction;
        int multiply_dst = -1;
        for (multiply_instruction = 0; multiply_instruction < mir.count;
             ++multiply_instruction)
            if (mir.insns[multiply_instruction].src2 == value) {
                multiply_dst = mir.insns[multiply_instruction].dst;
                break;
            }
        return mir_mul_const_fast_path_eligible(multiplier, multiply_dst);
    }
}

/* HL = HL * uv via a fully unrolled left-to-right binary-method shift/add
 * sequence - no runtime loop, unlike __mulu. Port of emit_mul_hl_const_general
 * in dcc_ops.c (the legacy AST backend); keeping the MIR backend's constant
 * multiplication at the same quality avoids MIR losing the cost-gate race
 * against captured legacy output for any function that multiplies by a
 * compile-time constant (array/struct element sizes, VLA row strides, etc).
 * Caller guarantees uv is nonzero, fits 16 bits, and is not a single power
 * of two (those are handled separately by the caller with plain shifts). */
static void mir_emit_mul_hl_const_general(FILE *out, unsigned long uv)
{
    int bit;
    int highest = -1;
    int shift_count;

    if (mir_mul_const_is_ones_run(uv, &shift_count) &&
        shift_count + 1 < mir_mul_const_naive_op_count(uv)) {
        /* Item 30: "(x << shift_count) - x" beats the per-bit add
         * decomposition for a bottom-aligned run of ones. */
        fputs("\tld d,h\n\tld e,l\n", out);
        for (bit = 0; bit < shift_count; ++bit)
            fputs("\tadd hl,hl\n", out);
        fputs("\tor a\n\tsbc hl,de\n", out);
        return;
    }
    for (bit = 15; bit >= 0; --bit) {
        if (uv & (1uL << (unsigned)bit)) {
            highest = bit;
            break;
        }
    }
    fputs("\tld d,h\n\tld e,l\n", out);
    for (bit = highest - 1; bit >= 0; --bit) {
        fputs("\tadd hl,hl\n", out);
        if (uv & (1uL << (unsigned)bit))
            fputs("\tadd hl,de\n", out);
    }
}

/* HL = HL * v for any compile-time constant multiplier, matching the
 * legacy AST backend's emit_mul_hl_const: exact power-of-two constants
 * become plain shifts, other constants that stay within the instruction
 * budget use the general shift/add decomposition above, and anything else
 * still falls back to a runtime __mulu call. */
void mir_emit_mul_hl_const(FILE *out, unsigned long multiplier)
{
    if (multiplier == 0) {
        fputs("\tld hl,0\n", out);
    } else if ((multiplier & (multiplier - 1)) == 0) {
        unsigned long remaining = multiplier;
        while (remaining > 1) {
            fputs("\tadd hl,hl\n", out);
            remaining >>= 1;
        }
    } else if (mir_mul_const_op_count(multiplier) <= MIR_MUL_CONST_MAX_OPS) {
        mir_emit_mul_hl_const_general(out, multiplier);
    } else {
        fprintf(out, "\tld de,%lu\n", multiplier);
        mir_emit_runtime_call(out, "__mulu");
    }
}

static int mir_address_is_single_call_argument(int value);
static int mir_load_is_single_indirect_call_target(int value, int size);
static int mir_global_load_is_single_call_argument(int value, int size);
static int mir_stable_pointer_argument_address(
    int value, const struct MirInsn **root_out, int *storage_out,
    long *member_offset_out);
static int mir_indirect_load_is_single_stable_pointer_call_argument(
    int value, int size);
static int mir_value_only_used_by_stable_pointer_argument(int value);

/* Item T76 (mir-text-size-plan.md): every "push ix\n\tpop hl\n" address-of-
 * local/param computation below unconditionally follows with "ld de,<off>/
 * add hl,de" for a non-zero offset, even for a magnitude of 1-3 - legacy's
 * own emit_load_sym_addr (dcc_symbols.c, ~line 845) special-cases exactly
 * this range with a straight-line inc/dec chain instead (cheaper in both
 * bytes and T-states: two `dec hl`s is 2 bytes/12 T-states vs. `ld de,-2`
 * + `add hl,de` at 4 bytes/21 T-states). This was invisible until a MIR-
 * accepted function actually contained a small-offset local whose address
 * is taken this way - found via tests/tptrlhs.c's main once an unrelated
 * fix (Item T74/T75) pushed it over the acceptance threshold and exposed
 * this pre-existing, unrelated gap as a genuine (if tiny) cycle-count
 * regression. Mirrors legacy's exact threshold (|offset| <= 3) rather
 * than inventing a new one. */
static void mir_emit_hl_offset_from_ix(FILE *out, int offset)
{
    int n;
    if (offset == 0)
        return;
    if (offset > 0 && offset <= 3) {
        for (n = 0; n < offset; ++n)
            fputs("\tinc hl\n", out);
    } else if (offset < 0 && offset >= -3) {
        for (n = 0; n < -offset; ++n)
            fputs("\tdec hl\n", out);
    } else {
        fprintf(out, "\tld de,%d\n\tadd hl,de\n", offset);
    }
}

static int mir_emit_rematerialized_argument(FILE *out, int value, int size)
{
    const struct MirInsn *definition = mir_definition(value);
    unsigned long bits;

    if (mir_global_load_is_single_call_argument(value, size)) {
        struct Sym *global;
        const char *assembly_name;
        int memory_type;
        int memory_storage;
        int memory_offset;

        if (!mir_scalar_memory_location(definition, &memory_type,
                                        &memory_storage, &memory_offset))
            return 0;
        global = find_global(definition->name);
        assembly_name = asm_name_for(
            global != NULL ? sym_asm_name(global)
                           : mir_declared_link_name(definition->name));
        if (memory_storage == SC_EXTERN && mir_extrn_should_emit(global))
            fprintf(out, "\textrn %s\n", assembly_name);
        if (memory_offset == 0)
            fprintf(out, "\tld hl,(%s)\n", assembly_name);
        else
            fprintf(out, "\tld hl,(%s%+d)\n",
                    assembly_name, memory_offset);
        return 1;
    }

    if (mir_indirect_load_is_single_stable_pointer_call_argument(
            value, size)) {
        const struct MirInsn *root;
        struct Sym *global;
        const char *assembly_name;
        int memory_storage;
        long member_offset;

        if (!mir_stable_pointer_argument_address(
                definition->src1, &root, &memory_storage, &member_offset))
            return 0;
        global = find_global(root->name);
        assembly_name = asm_name_for(
            global != NULL ? sym_asm_name(global)
                           : mir_declared_link_name(root->name));
        if (memory_storage == SC_EXTERN && mir_extrn_should_emit(global))
            fprintf(out, "\textrn %s\n", assembly_name);
        fprintf(out, "\tld hl,(%s)\n", assembly_name);
        mir_emit_hl_offset_from_ix(out, (int)member_offset);
        fputs("\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n", out);
        return 1;
    }

    if ((size == 2 || size == 4) &&
        (mir_load_is_single_call_argument(value, size) ||
         (size == 2 &&
          mir_load_is_single_indirect_call_target(value, size)))) {
        int memory_type;
        int memory_storage;
        int memory_offset;
        if (!mir_scalar_memory_location(definition, &memory_type,
                                        &memory_storage, &memory_offset))
            return 0;
        fprintf(out, "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
                memory_offset, memory_offset + 1);
        if (size == 4)
            fprintf(out, "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
                    memory_offset + 2, memory_offset + 3);
        return 1;
    }

    if (size == 2 && mir_address_is_single_call_argument(value)) {
        int memory_type;
        int memory_storage;
        int memory_offset;
        if (!mir_scalar_memory_location(definition, &memory_type,
                                        &memory_storage, &memory_offset))
            return 0;
        fputs("\tpush ix\n\tpop hl\n", out);
        mir_emit_hl_offset_from_ix(out, memory_offset);
        return 1;
    }

    if (!mir_call_only_constant(value))
        return 0;
    if (definition->opcode == MIR_STRING_ADDRESS) {
        fprintf(out, "\tld hl,S%ld\n", definition->immediate);
        return 1;
    }
    bits = (unsigned long)definition->immediate;
    if (size == 4)
        fprintf(out, "\tld hl,%lu\n\tld de,%lu\n",
                bits & 0xffffUL, (bits >> 16) & 0xffffUL);
    else
        fprintf(out, "\tld hl,%lu\n", bits & 0xffffUL);
    return 1;
}

static void mir_report_call_cache(int value, int width)
{
    const struct MirInsn *call;
    const struct MirInsn *definition;
    int argument_count = 0;
    int argument_index = -1;
    int later_constant_arguments = 0;
    int later_arguments = 0;
    int instruction;

    if (getenv("DCC_MIR_CALL_CACHE_REPORT") == NULL)
        return;
    call = &mir.insns[mir_emit_instruction_index];
    definition = mir_definition(value);
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->opcode != MIR_ARG ||
            insn->secondary_offset != call->secondary_offset)
            continue;
        ++argument_count;
        if (insn->src1 == value)
            argument_index = (int)insn->immediate;
    }
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->opcode != MIR_ARG ||
            insn->secondary_offset != call->secondary_offset ||
            insn->immediate <= argument_index)
            continue;
        ++later_arguments;
        if (mir_call_only_constant(insn->src1))
            ++later_constant_arguments;
    }
    fprintf(stderr,
            "; MIR call-cache function=%s value=%d width=%d "
            "definition=%s type=%d call-insn=%d call=%s "
            "arg-index=%d arg-count=%d later-const=%d later-count=%d\n",
            mir.name, value, width,
            definition != NULL
                ? mir_opcode_name(definition->opcode) : "none",
            definition != NULL ? definition->type : 0,
            mir_emit_instruction_index, call->name,
            argument_index, argument_count,
            later_constant_arguments, later_arguments);
}

static int mir_emit_cached_call_argument(FILE *out, int value)
{
    if (mir_cached_call_value != value ||
        mir_cached_call_instruction != mir_emit_instruction_index)
        return 0;
    mir_report_call_cache(value, 2);
    fputs("\tld l,c\n\tld h,b\n", out);
    mir_cached_call_value = -1;
    mir_cached_call_instruction = -1;
    mir_cached_wide_call_value = -1;
    mir_cached_wide_call_instruction = -1;
    return 1;
}

static int mir_emit_cached_call_argument_to_stack(FILE *out, int value)
{
    if (mir_cached_call_value != value ||
        mir_cached_call_instruction != mir_emit_instruction_index ||
        mir.backend_slots == NULL ||
        mir.backend_slots[value] !=
            MIR_BACKEND_SLOT_NARROW_ARGUMENT_DIRECT_PUSH)
        return 0;
    mir_report_call_cache(value, 2);
    fputs("\tpush bc\n", out);
    mir_cached_call_value = -1;
    mir_cached_call_instruction = -1;
    mir_cached_wide_call_value = -1;
    mir_cached_wide_call_instruction = -1;
    return 1;
}

/* Item 17 (mir-migration-plan-to-100pct.md): load a narrow argument value
 * into HL for spilled-scalar-cfg's fastcall emission, using the exact
 * same cached/rematerialized/virtual-load fallback chain the generic
 * MIR_CALL argument loop below already uses per argument - factored out
 * since the strlen/strchr/memcmp/bdos-family fastcalls each need this
 * several times. */
static void mir_emit_spilled_arg_to_hl(FILE *out, int value)
{
    if (!mir_emit_cached_call_argument(out, value) &&
        !mir_emit_rematerialized_argument(out, value, 2))
        mir_emit_virtual_load(out, value);
}

static int mir_emit_cached_wide_call_argument(FILE *out, int value)
{
    int stack_cached;

    if (mir_cached_wide_call_value != value ||
        mir_cached_wide_call_instruction != mir_emit_instruction_index)
        return 0;
    mir_report_call_cache(value, 4);
    stack_cached = mir.backend_slots != NULL &&
        mir.backend_slots[value] ==
            MIR_BACKEND_SLOT_WIDE_ARGUMENT_STACK_CACHE;
    if (!stack_cached)
        fputs("\texx\n", out);
    mir_cached_wide_call_value = -1;
    mir_cached_wide_call_instruction = -1;
    return stack_cached ? 2 : 1;
}

/* Item 15 (mir-migration-plan-to-100pct.md): mirrors dcc_ast_gen_expr.c's
 * legacy AST "fastcall" recognition of memset(dest,c,count) - DCCRTL's
 * __msf takes dest in HL, the fill byte in E, count in BC directly,
 * skipping both the general push-3-args/call/pop-3 convention MIR_CALL's
 * generic emission otherwise always uses and __mset's own ~10-instruction
 * stack-marshaling prologue. Item 14 found this gap costs a genuine
 * both-mode performance regression once a memset call becomes MIR-
 * reachable (mir_try_emit_homed_scalar_cfg's MIR_ADDRESS support unlocked
 * trw's clear_buf, a `memset` trampoline, but its MIR_CALL emission had no
 * knowledge of the specialized runtime convention the legacy backend
 * already exploits). This is shared, selector-independent detection logic
 * so it benefits every MIR_CALL emitter (spilled- and homed-scalar-cfg
 * alike), not just newly-unlocked functions - closing the gap for any
 * memset call already reachable via MIR today too. Returns 1 and fills
 * `*dest_value`/`*fill_value`/`*count_value` with the three arguments'
 * value indices if `call_index` is a MIR_CALL to memset with exactly
 * three non-struct, non-4-byte arguments (the same shape the legacy
 * fastcall requires); returns 0 otherwise (any struct/wide/int-promoted
 * argument, or an argument count other than 3, falls back to the generic
 * convention rather than risk a mismatched register width). */
/* Item 17 (mir-migration-plan-to-100pct.md): shared core for every
 * MIR_CALL fastcall detector below. Matches a call named exactly `name`
 * with exactly `argc` narrow (non-struct, non-4-byte) arguments, and
 * fills `values[0..argc-1]` with each argument's defining value index in
 * source-argument order (not push order). This is the same matching
 * shape Item 15's mir_call_is_memset_fastcall originally used, factored
 * out so strlen/strchr/memcmp/bdos-family don't each re-derive it. */
static int mir_call_matches_fastcall_shape(int call_index, const char *name,
                                          int argc, int *values)
{
    const struct MirInsn *call = &mir.insns[call_index];
    int scan;

    for (scan = 0; scan < argc; ++scan)
        values[scan] = -1;
    if (strcmp(call->name, name) != 0)
        return 0;
    for (scan = 0; scan < call_index; ++scan) {
        const struct MirInsn *arg = &mir.insns[scan];
        int index;
        if (arg->opcode != MIR_ARG ||
            arg->secondary_offset != call->secondary_offset)
            continue;
        index = (int)arg->immediate;
        if (index < 0 || index >= argc)
            return 0;
        if (type_is_struct_object(arg->type) || type_size(arg->type) == 4)
            return 0;
        values[index] = arg->src1;
    }
    for (scan = 0; scan < argc; ++scan)
        if (values[scan] < 0)
            return 0;
    return 1;
}

int mir_call_is_memset_fastcall(int call_index, int *dest_value,
                                       int *fill_value, int *count_value)
{
    int values[3];
    if (!mir_call_matches_fastcall_shape(call_index, "memset", 3, values))
        return 0;
    *dest_value = values[0];
    *fill_value = values[1];
    *count_value = values[2];
    return 1;
}

/* dcc_ast_gen_expr.c's legacy fastcall for strlen(s): DCCRTL's __slf takes
 * s directly in HL and returns the length in HL. */
int mir_call_is_strlen_fastcall(int call_index, int *s_value)
{
    int values[1];
    if (!mir_call_matches_fastcall_shape(call_index, "strlen", 1, values))
        return 0;
    *s_value = values[0];
    return 1;
}

/* Legacy fastcall for strchr(s,c): DCCRTL's __chf takes s in HL and c's
 * low byte in A, returning the match (or 0) in HL. */
int mir_call_is_strchr_fastcall(int call_index, int *s_value,
                                      int *c_value)
{
    int values[2];
    if (!mir_call_matches_fastcall_shape(call_index, "strchr", 2, values))
        return 0;
    *s_value = values[0];
    *c_value = values[1];
    return 1;
}

/* Item 18 (mir-migration-plan-to-100pct.md): strrchr(s,c) has the exact
 * same DCCRTL argument shape as strchr above (HL=s, A=low byte of c),
 * differing only in the entry point (__rcf vs __chf). */
int mir_call_is_strrchr_fastcall(int call_index, int *s_value,
                                       int *c_value)
{
    int values[2];
    if (!mir_call_matches_fastcall_shape(call_index, "strrchr", 2, values))
        return 0;
    *s_value = values[0];
    *c_value = values[1];
    return 1;
}

/* Item 18: memchr(s,c,n) has the exact same DCCRTL argument shape as
 * memset above (HL=s, E=low byte of c, BC=n), differing only in the
 * entry point (__mhf vs __msf). */
int mir_call_is_memchr_fastcall(int call_index, int *s_value,
                                      int *c_value, int *n_value)
{
    int values[3];
    if (!mir_call_matches_fastcall_shape(call_index, "memchr", 3, values))
        return 0;
    *s_value = values[0];
    *c_value = values[1];
    *n_value = values[2];
    return 1;
}

/* Item 18: memcpy(dst,src,n) has the exact same DCCRTL argument shape as
 * memcmp above (DE=arg0, HL=arg1, BC=n), differing only in the entry
 * point (__mcf vs __cmpf) and copy-not-compare semantics. */
int mir_call_is_memcpy_fastcall(int call_index, int *dst_value,
                                      int *src_value, int *n_value)
{
    int values[3];
    if (!mir_call_matches_fastcall_shape(call_index, "memcpy", 3, values))
        return 0;
    *dst_value = values[0];
    *src_value = values[1];
    *n_value = values[2];
    return 1;
}

/* Item 18: strcpy(dst,src)->__scf, strstr(haystack,needle)->__ssf, and
 * stricmp(s1,s2)->__icf all share one 2-argument shape: arg1 ends up in
 * HL (the last-evaluated value needs no move) and arg0 is pushed then
 * popped into DE. */
int mir_call_is_de_hl_fastcall(int call_index, const char **rtl_name,
                                     int *arg0_value, int *arg1_value)
{
    static const struct { const char *name; const char *rtl; } family[] = {
        { "strcpy", "__scf" }, { "strstr", "__ssf" }, { "stricmp", "__icf" }
    };
    size_t member;
    int values[2];
    for (member = 0; member < sizeof(family) / sizeof(family[0]); ++member) {
        if (mir_call_matches_fastcall_shape(call_index, family[member].name,
                                            2, values)) {
            *rtl_name = family[member].rtl;
            *arg0_value = values[0];
            *arg1_value = values[1];
            return 1;
        }
    }
    return 0;
}

/* Legacy fastcall for memcmp(s1,s2,n): DCCRTL's __cmpf takes s1 in DE,
 * s2 in HL, n in BC directly. */
int mir_call_is_memcmp_fastcall(int call_index, int *s1_value,
                                       int *s2_value, int *n_value)
{
    int values[3];
    if (!mir_call_matches_fastcall_shape(call_index, "memcmp", 3, values))
        return 0;
    *s1_value = values[0];
    *s2_value = values[1];
    *n_value = values[2];
    return 1;
}

/* Legacy fastcall for bdos(fn,dearg)/bdoshl(fn,dearg)/bios(fn,dearg)/
 * bioshl(fn,dearg): all four share the same DE=dearg, C=fn-low-byte
 * argument convention, differing only in which DCCRTL entry point is
 * called (__bdosf/__bhlf/__biosf/__bhf respectively - see
 * dcc_ast_gen_expr.c). */
int mir_call_is_bdos_family_fastcall(int call_index,
                                           const char **rtl_name,
                                           int *fn_value, int *dearg_value)
{
    static const struct { const char *name; const char *rtl; } family[] = {
        { "bdos", "__bdosf" }, { "bdoshl", "__bhlf" }, { "bios", "__biosf" },
        { "bioshl", "__bhf" }
    };
    size_t i;
    int values[2];
    for (i = 0; i < sizeof(family) / sizeof(family[0]); ++i) {
        if (mir_call_matches_fastcall_shape(call_index, family[i].name, 2,
                                            values)) {
            *rtl_name = family[i].rtl;
            *fn_value = values[0];
            *dearg_value = values[1];
            return 1;
        }
    }
    return 0;
}

int mir_object_is_fully_promoted(int object)
{
    int instruction;

    if (object < 0 || object >= mir.object_count)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_LOAD &&
            mir.insns[instruction].object == object)
            return 0;
    return 1;
}

int mir_object_address_taken(int object)
{
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_ADDRESS &&
            mir.insns[instruction].object == object)
            return 1;
    return 0;
}

/* True if the value written by the MIR_STORE at `instruction` can never be
 * observed: no real MIR_LOAD of the same object is reachable along any
 * successor path before the object is next stored or the function ends.
 * A backward liveness fixed point mirrors the per-value computation in
 * mir_verify_and_dump, specialised so a MIR_LOAD of the object is a "use"
 * (forces liveness) and a MIR_STORE of the object is a "kill" (the store's
 * own liveness does not depend on anything reachable before it). This
 * single mechanism covers both a dead initialisation store (never read
 * before the function ends) and a store overwritten by a later store
 * before any intervening read. Bails conservatively if the object's
 * address is ever taken, since a store could then be observed through an
 * escaped pointer that a static scan of MIR_LOAD instructions cannot see. */
int mir_store_is_dead(int instruction)
{
    int object;
    unsigned char *live_in;
    unsigned char *live_out;
    int i;
    int changed;
    int dead;

    if (mir.insns[instruction].opcode != MIR_STORE)
        return 0;
    object = mir.insns[instruction].object;
    if (object < 0 || object >= mir.object_count ||
        mir_object_address_taken(object))
        return 0;
    live_in = (unsigned char *)calloc((size_t)mir.count, 1);
    live_out = (unsigned char *)calloc((size_t)mir.count, 1);
    if (mir.count && (live_in == NULL || live_out == NULL))
        fatal("out of memory computing MIR store liveness");
    do {
        changed = 0;
        for (i = mir.count - 1; i >= 0; --i) {
            const struct MirInsn *insn = &mir.insns[i];
            int successor;
            int next_out = 0;
            int next_in;

            for (successor = 0; successor < insn->successor_count; ++successor)
                next_out |= live_in[insn->successors[successor]];
            if (insn->opcode == MIR_LOAD && insn->object == object)
                next_in = 1;
            else if (insn->opcode == MIR_STORE && insn->object == object)
                next_in = 0;
            else
                next_in = next_out;
            if (live_out[i] != next_out || live_in[i] != next_in) {
                live_out[i] = (unsigned char)next_out;
                live_in[i] = (unsigned char)next_in;
                changed = 1;
            }
        }
    } while (changed);
    dead = !live_out[instruction];
    free(live_in);
    free(live_out);
    return dead;
}

static int mir_ranges_overlap(int offset1, int size1, int offset2, int size2)
{
    return offset1 < offset2 + size2 && offset2 < offset1 + size1;
}

static int mir_dead_local_candidate(int object, int *store_instruction,
                                    int *source_value)
{
    const struct MirObject *candidate = &mir.objects[object];
    int size = type_size(candidate->type);
    int store = -1;
    int instruction;

    if (candidate->storage != SC_LOCAL || (size != 2 && size != 4) ||
        candidate->offset >= 0)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode == MIR_STORE && insn->object == object) {
            if (store >= 0)
                return 0;
            store = instruction;
        }
    }
    if (store < 0 || type_size(mir.insns[store].type) != size ||
        !mir_object_is_fully_promoted(object))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        int memory_type;
        int memory_storage;
        int memory_offset;
        int memory_size;

        if (insn->opcode != MIR_PARAM && insn->opcode != MIR_LOAD &&
            insn->opcode != MIR_STORE && insn->opcode != MIR_ADDRESS)
            continue;
        if (!mir_scalar_memory_location(insn, &memory_type, &memory_storage,
                                        &memory_offset))
            return 0;
        memory_size = type_size(memory_type);
        if (memory_storage == SC_LOCAL && memory_size > 0 &&
            mir_ranges_overlap(candidate->offset, size, memory_offset,
                               memory_size) &&
            instruction != store)
            return 0;
    }
    *source_value = mir.insns[store].src1;
    if (*source_value < 0)
        return 0;
    *store_instruction = store;
    return 1;
}

static int mir_dead_local_suffix_allowed(void)
{
    int instruction;

    if (opt_debug || mir.has_vla || mir.aggregate_temp_bytes != 0)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        switch (mir.insns[instruction].opcode) {
        case MIR_ADDRESS:
        case MIR_COMPOUND_ADDRESS:
        case MIR_COPY_AGGREGATE:
        case MIR_VLA_SIZE:
        case MIR_VLA_SAVE:
        case MIR_VLA_ALLOC:
        case MIR_VLA_RESTORE:
        case MIR_CALL_AGGREGATE:
        case MIR_VA_START:
        case MIR_VA_END:
        case MIR_VA_ARG:
        case MIR_OPAQUE:
            return 0;
        default:
            break;
        }
    }
    return 1;
}

static int mir_dead_local_range_is_unique(int candidate)
{
    int candidate_size = type_size(mir.objects[candidate].type);
    int object;

    for (object = 0; object < mir.object_count; ++object) {
        int size;

        if (object == candidate || mir.objects[object].storage != SC_LOCAL)
            continue;
        size = type_size(mir.objects[object].type);
        if (size > 0 &&
            mir_ranges_overlap(mir.objects[candidate].offset, candidate_size,
                               mir.objects[object].offset, size))
            return 0;
    }
    return 1;
}

void mir_compute_dead_local_suffix(void)
{
    int reclaimed = 0;

    mir.dead_local_suffix_bytes = 0;
    if (!mir_dead_local_suffix_allowed())
        return;
    for (;;) {
        int boundary = -mir.local_bytes + reclaimed;
        int found = -1;
        int store;
        int value;
        int object;

        for (object = 0; object < mir.object_count; ++object)
            if (mir.objects[object].offset == boundary &&
                mir_dead_local_candidate(object, &store, &value) &&
                mir_dead_local_range_is_unique(object)) {
                if (found >= 0)
                    return;
                found = object;
            }
        if (found < 0)
            break;
        reclaimed += type_size(mir.objects[found].type);
    }
    mir.dead_local_suffix_bytes = reclaimed;
}

int mir_effective_local_bytes(void)
{
    return mir.local_bytes - mir.dead_local_suffix_bytes;
}

void mir_report_dead_local_suffix(void)
{
    int effective = mir_effective_local_bytes();
    int object;

    if (getenv("DCC_MIR_DEAD_LOCAL_REPORT") == NULL ||
        mir.dead_local_suffix_bytes == 0)
        return;
    for (object = 0; object < mir.object_count; ++object) {
        const struct MirObject *candidate = &mir.objects[object];
        int size = type_size(candidate->type);
        int store;
        int value;

        if (candidate->storage != SC_LOCAL ||
            candidate->offset < -mir.local_bytes ||
            candidate->offset + size > -effective ||
            !mir_dead_local_candidate(object, &store, &value))
            continue;
        fprintf(stderr,
                "; MIR dead-local function=%s object=%s offset=%d size=%d"
                " store=%d value=%d locals=%d effective=%d"
                " reclaimable=%d\n",
                mir.name, candidate->name, candidate->offset,
                size, store, value, mir.local_bytes, effective,
                mir.dead_local_suffix_bytes);
    }
}

/* Item T10 (mir-text-size-plan.md): a value-defining instruction (const,
 * float const, address materialisation, ...) unconditionally checks
 * mir_value_has_use(dst) before deciding whether it needs to write its
 * result into dst's own backend home slot at all - but "has a use" only
 * asks whether some later instruction's src operand names this value, not
 * whether that later instruction will actually emit any code. A MIR_STORE
 * consuming this value can itself turn out to be fully dead (see
 * mir_object_is_fully_promoted/mir_store_is_dead above) and emit nothing,
 * in which case materialising the value into its home was pure waste - a
 * write that is now provably never read by any surviving instruction.
 * This mirrors the exact two conditions the MIR_STORE case itself already
 * uses to decide it will emit nothing, so a value is only ever treated as
 * "unused" here when every one of its uses is a store this selector has
 * independently already proven dead - never a broader guess. */
int mir_value_only_used_by_dead_stores(int value)
{
    int instruction;
    int found_use = 0;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->src1 != value && insn->src2 != value &&
            !mir_call_uses_value(insn, value))
            continue;
        found_use = 1;
        if (insn->opcode != MIR_STORE || insn->src1 != value)
            return 0;
        if (!(mir_object_is_fully_promoted(insn->object) ||
              mir_store_is_dead(instruction)))
            return 0;
    }
    return found_use;
}

/* Item T12 (mir-text-size-plan.md): analogous to
 * mir_value_only_used_by_dead_stores above, but for a value whose only
 * use is as the operand of a MIR_UNARY instruction (cast, +, -, ~, !)
 * whose own destination in turn has no use - the common "(void)param;"
 * idiom used to silence unused-parameter warnings in callback/visitor
 * signatures. The MIR_UNARY emission case already skips itself entirely
 * once its own dst is unused, so loading a value that only ever feeds
 * such a dead unary is pure waste: it is loaded into hl and immediately
 * discarded, never observed by anything. */
static int mir_value_only_used_by_dead_unary(int value)
{
    int instruction;
    int found_use = 0;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->src1 != value && insn->src2 != value &&
            !mir_call_uses_value(insn, value))
            continue;
        found_use = 1;
        if (insn->opcode != MIR_UNARY || insn->src1 != value ||
            mir_value_has_use(insn->dst))
            return 0;
    }
    return found_use;
}

/* mir-text-size Item T20: when a value's own definition is immediately
 * followed by its sole use as a MIR_ARG, and that ARG is in turn
 * immediately followed by its own MIR_CALL/MIR_CALL_AGGREGATE (nothing
 * at all in between - not even another argument's own value
 * definition), this is provably the *last*-defined argument for that
 * call in MIR-stream order. The generic MIR_CALL argument loop below
 * always walks a call's own MIR_ARG instructions *backward* (matching
 * strictly decreasing positional index starting from
 * call_arg_count - 1), so the last-MIR-stream-order argument is always
 * the *first* one physically processed/pushed - nothing can touch HL
 * between this value's own computation and the moment it is pushed.
 * mir_call_argument_cache_target's more general BC-cache mechanism
 * (below) exists precisely to preserve a value across such a gap; when
 * there is provably no gap at all, caching (store to bc, reload from
 * bc) is pure overhead - the value can flow straight through HL via
 * the same mir_forwarded_hl_value/_instruction mechanism
 * mir_can_forward_hl_to_next already uses for its own recognized
 * consumer opcodes, anchored to the ARG instruction's own index: since
 * MIR_ARG's own emission case is a no-op (it contributes zero machine
 * instructions), mir_emit_virtual_load's existing forwarded-hl check
 * naturally fires when the emit loop later reaches the CALL
 * instruction one position after the ARG, exactly the position this
 * predicate requires be adjacent. Found via tests/pint.c's
 * while_stmt: `patch(jz, cp)`'s second argument (a fresh global read of
 * `cp`) was round-tripped through bc for no reason - nothing runs
 * between the load and the call at all. */
/* Item T82 (mir-text-size-plan.md): the value's own MIR_ARG use can be
 * separated from its definition by an intervening MIR_NOP (a same-block
 * rename/metadata marker that emits no code - see mir_forward_skip_target's
 * own comment on why these are always safe to look through), the same gap
 * Item T29 already closed for mir_can_forward_hl_to_next. This predicate
 * used a raw "+1" index instead, so a value like tests/tmirfast.c's
 * dec_dead/inc_dead `x` parameter (whose MIR_PARAM definition is followed
 * by one such NOP before its ARG use) never matched at all, regardless of
 * how many other uses it had. Skipping only plain MIR_NOP (never a LABEL -
 * an ARG/CALL pair can never legitimately cross a block boundary) keeps
 * this exactly as safe as the original adjacent-index check. */
static int mir_call_argument_after_nops(int instruction)
{
    int index = instruction + 1;

    mir_forward_skip_last_skipped_dead_store = 0;
    while (index < mir.count &&
           (mir.insns[index].opcode == MIR_NOP ||
            mir_instruction_is_transparent_dead_store(index)))
        ++index;
    return index;
}

static int mir_can_forward_hl_to_call_argument(int value)
{
    const struct MirInsn *arg_insn;
    const struct MirInsn *call_insn;
    int instruction;
    int arg_instruction;

    if (mir_emit_instruction_index < 0)
        return 0;
    arg_instruction = mir_call_argument_after_nops(mir_emit_instruction_index);
    if (arg_instruction + 1 >= mir.count)
        return 0;
    arg_insn = &mir.insns[arg_instruction];
    call_insn = &mir.insns[arg_instruction + 1];
    if (arg_insn->opcode != MIR_ARG || arg_insn->src1 != value ||
        type_size(arg_insn->type) > 2)
        return 0;
    if ((call_insn->opcode != MIR_CALL &&
         call_insn->opcode != MIR_CALL_AGGREGATE) ||
        call_insn->secondary_offset != arg_insn->secondary_offset)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        if (instruction == arg_instruction)
            continue;
        if (mir.insns[instruction].src1 == value ||
            mir.insns[instruction].src2 == value)
            return 0;
    }
    return mir_forward_note_success();
}

/* Item T82 (mir-text-size-plan.md): sibling to mir_can_forward_hl_to_call_
 * argument above, but for a value that also has a *later* use elsewhere
 * (so it still genuinely needs a real backend slot - the sole-use
 * predicate above correctly declines it). The store into that slot still
 * runs unchanged so every later reload keeps working; this only proves
 * that the value's very first consumer, immediately following its own
 * definition (through any intervening MIR_NOP), is this exact ARG+CALL
 * adjacency with nothing at all between the store and that first use -
 * so the immediately-following reload of the bytes just written is pure,
 * provably redundant round-tripping and can reuse HL directly instead.
 * Found via tests/tmirfast.c's dec_dead/inc_dead: `x` is used once as
 * side_effect's sole call argument and again afterward by `x--`/`x++`,
 * so it needs a slot, but its first use directly follows its own
 * MIR_PARAM definition (through one MIR_NOP) with nothing in between. */
static int mir_can_forward_hl_to_call_argument_first_use(int value)
{
    const struct MirInsn *arg_insn;
    const struct MirInsn *call_insn;
    int arg_instruction;

    if (mir_emit_instruction_index < 0)
        return 0;
    arg_instruction = mir_call_argument_after_nops(mir_emit_instruction_index);
    if (arg_instruction + 1 >= mir.count)
        return 0;
    arg_insn = &mir.insns[arg_instruction];
    call_insn = &mir.insns[arg_instruction + 1];
    if (arg_insn->opcode != MIR_ARG || arg_insn->src1 != value ||
        type_size(arg_insn->type) > 2)
        return 0;
    if ((call_insn->opcode != MIR_CALL &&
         call_insn->opcode != MIR_CALL_AGGREGATE) ||
        call_insn->secondary_offset != arg_insn->secondary_offset)
        return 0;
    return mir_forward_note_success();
}

static int mir_call_has_odd_argument_bytes(const struct MirInsn *call)
{
    int argument_bytes = 0;
    int instruction;

    if (call == NULL || call->opcode != MIR_CALL)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *arg = &mir.insns[instruction];
        int size;
        if (arg->opcode != MIR_ARG ||
            arg->secondary_offset != call->secondary_offset)
            continue;
        size = type_size(arg->type);
        argument_bytes += type_is_struct_object(arg->type)
            ? size : (size == 4 ? 4 : 2);
    }
    return (argument_bytes & 1) != 0;
}

static int mir_call_argument_cache_target_for_state(
    int value, int definition_instruction,
    int narrow_cache_occupied, int wide_cache_occupied)
{
    int argument_instruction = -1;
    int call_id = -1;
    int call_instruction = -1;
    int instruction;

    if ((mir_value_is_wide(value) && wide_cache_occupied) ||
        (!mir_value_is_wide(value) && narrow_cache_occupied))
        return -1;
    {
        const struct MirInsn *definition = mir_definition(value);
        if (definition == NULL || definition->opcode == MIR_PHI ||
            definition->opcode == MIR_PARAM ||
            mir_divmod_partner((int)(definition - mir.insns)) >= 0)
            return -1;
        /*
         * Generic calls with an odd aggregate-argument byte count store a
         * scalar result once around SP cleanup and again afterward. Such a
         * result cannot be cache-only because its second store is real.
         */
        if (mir_call_has_odd_argument_bytes(definition))
            return -1;
    }
    for (instruction = definition_instruction + 1;
         instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->opcode == MIR_ARG && insn->src1 == value) {
            if (argument_instruction >= 0)
                return -1;
            argument_instruction = instruction;
            call_id = insn->secondary_offset;
        } else if (insn->src1 == value || insn->src2 == value) {
            if (!(insn->opcode == MIR_STORE &&
                  mir_object_is_fully_promoted(insn->object)))
                return -1;
        }
    }
    if (argument_instruction < 0)
        return -1;
    if ((type_size(mir.insns[argument_instruction].type) > 2) !=
        mir_value_is_wide(value))
        return -1;
    for (instruction = argument_instruction + 1;
         instruction < mir.count; ++instruction)
        if ((mir.insns[instruction].opcode == MIR_CALL ||
             mir.insns[instruction].opcode == MIR_CALL_AGGREGATE) &&
            mir.insns[instruction].secondary_offset == call_id) {
            call_instruction = instruction;
            break;
        }
    if (call_instruction < 0)
        return -1;
    for (instruction = definition_instruction + 1;
         instruction < call_instruction; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->opcode == MIR_NOP || insn->opcode == MIR_ARG)
            continue;
        if ((insn->opcode == MIR_CONST ||
             insn->opcode == MIR_FLOAT_CONST ||
             insn->opcode == MIR_STRING_ADDRESS) &&
            mir_call_only_constant(insn->dst))
            continue;
        if (insn->opcode == MIR_STORE && insn->src1 == value &&
            mir_object_is_fully_promoted(insn->object))
            continue;
        return -1;
    }
    return call_instruction;
}

static int mir_call_argument_cache_target(int value)
{
    return mir_call_argument_cache_target_for_state(
        value, mir_emit_instruction_index,
        mir_cached_call_value >= 0,
        mir_cached_wide_call_value >= 0);
}

static int mir_wide_call_argument_is_first_pushed(
    int value, int call_instruction)
{
    const struct MirInsn *call;
    int argument_count = 0;
    int argument_index = -1;
    int instruction;

    if (!mir_wide_first_argument_stack_cache_enabled ||
        !mir_value_is_wide(value) || call_instruction < 0 ||
        call_instruction >= mir.count)
        return 0;
    call = &mir.insns[call_instruction];
    if (call->opcode != MIR_CALL &&
        call->opcode != MIR_CALL_AGGREGATE)
        return 0;
    for (instruction = 0; instruction < call_instruction; ++instruction) {
        const struct MirInsn *arg = &mir.insns[instruction];
        if (arg->opcode != MIR_ARG ||
            arg->secondary_offset != call->secondary_offset)
            continue;
        ++argument_count;
        if (arg->src1 == value)
            argument_index = (int)arg->immediate;
    }
    return argument_count > 0 &&
           argument_index == argument_count - 1;
}

static int mir_planned_call_argument_cache_target(int value, int wide)
{
    int call_instruction = mir_call_argument_cache_target(value);

    if (call_instruction < 0) {
        char message[256];
        const struct MirInsn *definition = mir_definition(value);
        snprintf(message, sizeof(message),
                 "planned %sMIR call-argument cache unavailable in %s "
                 "for value %d (%s) at instruction %d (cached value %d)",
                 wide ? "wide " : "", mir.name, value,
                 definition != NULL
                    ? mir_opcode_name(definition->opcode) : "none",
                 mir_emit_instruction_index,
                 wide ? mir_cached_wide_call_value : mir_cached_call_value);
        fatal(message);
    }
    return call_instruction;
}

static int mir_call_uses_generic_stack_arguments(int instruction)
{
    const char *rtl_name;
    int a, b, c;

    return !mir_call_is_memset_fastcall(instruction, &a, &b, &c) &&
           !mir_call_is_strlen_fastcall(instruction, &a) &&
           !mir_call_is_strchr_fastcall(instruction, &a, &b) &&
           !mir_call_is_strrchr_fastcall(instruction, &a, &b) &&
           !mir_call_is_memchr_fastcall(instruction, &a, &b, &c) &&
           !mir_call_is_memcmp_fastcall(instruction, &a, &b, &c) &&
           !mir_call_is_memcpy_fastcall(instruction, &a, &b, &c) &&
           !mir_call_is_de_hl_fastcall(instruction, &rtl_name, &a, &b) &&
           !mir_call_is_bdos_family_fastcall(
               instruction, &rtl_name, &a, &b);
}

static void mir_emit_prepacked_constant_arguments(
    FILE *out, int trigger_instruction)
{
    const struct MirInsn *outer_call = NULL;
    int argument_count = 0;
    int cached_argument = -1;
    int definition_instruction;
    int outer_instruction = -1;
    int argument;
    int instruction;
    int value = -1;

    if (!mir_constant_argument_prepacking_enabled || mir.has_vla ||
        mir.backend_slots == NULL || mir_prepacked_call_instruction >= 0)
        return;
    for (definition_instruction = trigger_instruction;
         definition_instruction < mir.count; ++definition_instruction) {
        const struct MirInsn *definition =
            &mir.insns[definition_instruction];
        int previous_argument_instruction = -1;

        value = definition->dst;
        if (definition->opcode != MIR_CALL || value < 0 ||
            value >= mir.next_value ||
            (mir.backend_slots[value] != MIR_BACKEND_SLOT_CALL_CACHE &&
             mir.backend_slots[value] !=
                 MIR_BACKEND_SLOT_NARROW_ARGUMENT_DIRECT_PUSH))
            continue;
        outer_instruction = mir_call_argument_cache_target_for_state(
            value, definition_instruction, 0, 0);
        if (outer_instruction < 0 ||
            !mir_call_uses_generic_stack_arguments(outer_instruction))
            continue;
        outer_call = &mir.insns[outer_instruction];
        if (outer_call->opcode != MIR_CALL)
            continue;
        argument_count = 0;
        cached_argument = -1;
        for (instruction = 0;
             instruction < outer_instruction; ++instruction) {
            const struct MirInsn *arg = &mir.insns[instruction];
            if (arg->opcode != MIR_ARG ||
                arg->secondary_offset != outer_call->secondary_offset)
                continue;
            ++argument_count;
            if (arg->src1 == value)
                cached_argument = (int)arg->immediate;
        }
        if (cached_argument <= 0 ||
            cached_argument + 1 >= argument_count)
            continue;
        for (instruction = 0;
             instruction < definition_instruction; ++instruction) {
            const struct MirInsn *arg = &mir.insns[instruction];
            if (arg->opcode == MIR_ARG &&
                arg->secondary_offset == outer_call->secondary_offset &&
                arg->immediate == cached_argument - 1)
                previous_argument_instruction = instruction;
        }
        if (previous_argument_instruction + 1 != trigger_instruction)
            continue;
        break;
    }
    if (definition_instruction >= mir.count || outer_call == NULL ||
        outer_instruction < 0)
        return;
    for (argument = cached_argument + 1;
         argument < argument_count; ++argument) {
        int found = 0;
        for (instruction = 0;
             instruction < outer_instruction; ++instruction) {
            const struct MirInsn *arg = &mir.insns[instruction];
            if (arg->opcode != MIR_ARG ||
                arg->secondary_offset != outer_call->secondary_offset ||
                arg->immediate != argument)
                continue;
            if (!mir_call_only_constant(arg->src1))
                return;
            found = 1;
            break;
        }
        if (!found)
            return;
    }
    /*
     * The ABI pushes arguments in descending source-index order. Emit the
     * constant suffix before evaluating the nested call, then push that
     * call's result. The outer call later emits only the lower-index prefix,
     * producing exactly the same stack order as its ordinary reverse scan.
     * Triggering before the nested call's own arguments is essential: they
     * may use the same forwarding state, but their call restores SP before
     * the staged outer arguments are consumed.
     */
    for (argument = argument_count - 1;
         argument > cached_argument; --argument) {
        for (instruction = outer_instruction - 1;
             instruction >= 0; --instruction) {
            const struct MirInsn *arg = &mir.insns[instruction];
            int size;
            if (arg->opcode != MIR_ARG ||
                arg->secondary_offset != outer_call->secondary_offset ||
                arg->immediate != argument)
                continue;
            size = type_size(arg->type);
            if (!mir_emit_rematerialized_argument(
                    out, arg->src1, size))
                fatal("cannot prepack constant MIR call argument");
            if (size == 4)
                fputs("\tpush de\n\tpush hl\n", out);
            else
                fputs("\tpush hl\n", out);
            break;
        }
    }
    mir_prepacked_call_instruction = outer_instruction;
    mir_prepacked_after_argument = cached_argument;
    mir_prepacked_result_value = value;
    ++mir_constant_argument_prepack_count;
}

static int mir_definition_is_wide(const struct MirInsn *definition)
{
    if (definition == NULL)
        return 0;
    if (definition->opcode == MIR_ADDRESS ||
        definition->opcode == MIR_COMPOUND_ADDRESS ||
        definition->opcode == MIR_INDEX_ADDRESS ||
        definition->opcode == MIR_MEMBER_ADDRESS ||
        definition->opcode == MIR_CALL_AGGREGATE)
        return 0;
    if ((definition->opcode == MIR_LOAD_INDIRECT ||
         definition->opcode == MIR_INDEX_LOAD) &&
        definition->memory_size > 2)
        return 1;
    return type_size(definition->type) > 2;
}

static int mir_value_is_single_call_argument(int value, int size)
{
    int argument_count = 0;
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->src2 == value)
            return 0;
        if (insn->src1 != value)
            continue;
        if (insn->opcode != MIR_ARG || type_size(insn->type) != size ||
            ++argument_count > 1)
            return 0;
    }
    /* Item T42 (mir-text-size-plan.md): this used to also require the
     * call's *total* argument count to be <=3 (a second scan counting
     * every MIR_ARG sharing this value's own call_id) - a restriction
     * this function's own sibling, mir_address_is_single_call_argument
     * just below, never had (it only ever checks this *value's* own use
     * count, never the call's total arity). No comment anywhere
     * explained the cap, and nothing about recomputing a value fresh
     * from its own fixed ix-relative offset at push time depends on how
     * many *other* arguments the same call has - IX never moves during
     * argument evaluation regardless of arity. Found via
     * tests/t2darr.c's check() (`printf("FAIL %s got %d expected %d\n",
     * name, got, expected)`, a 4-argument call): `name`'s value,
     * otherwise eligible for this exact rematerialization, was falling
     * through to mir_call_argument_cache_target's costlier BC-cache
     * round trip purely because the call had one argument more than
     * this arbitrary cap allowed. */
    return argument_count == 1;
}

int mir_load_is_single_call_argument(int value, int size)
{
    const struct MirInsn *definition = mir_definition(value);
    int memory_type;
    int memory_storage;
    int memory_offset;

    return definition != NULL && definition->opcode == MIR_LOAD &&
        mir_scalar_memory_location(definition, &memory_type,
                                   &memory_storage, &memory_offset) &&
        type_size(memory_type) == size &&
        (memory_storage == SC_LOCAL || memory_storage == SC_PARAM) &&
        memory_offset >= -128 && memory_offset + size - 1 <= 127 &&
        mir_value_is_single_call_argument(value, size);
}

static int mir_global_load_is_single_call_argument(int value, int size)
{
    const struct MirInsn *definition = mir_definition(value);
    int memory_type;
    int memory_storage;
    int memory_offset;

    return mir_global_argument_rematerialization_enabled &&
        size == 2 && definition != NULL &&
        definition->opcode == MIR_LOAD &&
        mir_scalar_memory_location(definition, &memory_type,
                                   &memory_storage, &memory_offset) &&
        type_size(memory_type) == size &&
        (memory_storage == SC_GLOBAL || memory_storage == SC_EXTERN) &&
        (memory_storage != SC_EXTERN || memory_offset == 0) &&
        mir_value_is_single_call_argument(value, size);
}

static int mir_stable_pointer_argument_address(
    int value, const struct MirInsn **root_out, int *storage_out,
    long *member_offset_out)
{
    const struct MirInsn *definition = mir_definition(value);
    int memory_type;
    int memory_storage;
    int memory_offset;
    long member_offset = 0;

    while (definition != NULL &&
           definition->opcode == MIR_MEMBER_ADDRESS) {
        member_offset += definition->immediate;
        definition = mir_definition(definition->src1);
    }
    if (definition == NULL || definition->opcode != MIR_LOAD ||
        !mir_scalar_memory_location(definition, &memory_type,
                                    &memory_storage, &memory_offset) ||
        (memory_storage != SC_GLOBAL && memory_storage != SC_EXTERN) ||
        type_size(memory_type) != 2 || type_ptr_depth(memory_type) == 0)
        return 0;
    if (root_out != NULL)
        *root_out = definition;
    if (storage_out != NULL)
        *storage_out = memory_storage;
    if (member_offset_out != NULL)
        *member_offset_out = member_offset;
    return 1;
}

static int mir_indirect_load_is_single_stable_pointer_call_argument(
    int value, int size)
{
    const struct MirInsn *definition = mir_definition(value);
    int argument_count = 0;
    int instruction;

    if (!mir_stable_pointer_argument_rematerialization_enabled ||
        size != 2 || definition == NULL ||
        definition->opcode != MIR_LOAD_INDIRECT ||
        definition->memory_size != 2 || definition->bit_width != 0 ||
        !mir_stable_pointer_argument_address(
            definition->src1, NULL, NULL, NULL))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->src2 == value)
            return 0;
        if (insn->src1 != value)
            continue;
        if (insn->opcode != MIR_ARG || type_size(insn->type) != size ||
            ++argument_count > 1)
            return 0;
    }
    return argument_count == 1;
}

static int mir_value_only_used_by_stable_pointer_argument(int value)
{
    int uses = 0;
    int instruction;

    if (!mir_stable_pointer_argument_rematerialization_enabled)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->src2 == value || mir_call_uses_value(insn, value))
            return 0;
        if (insn->src1 != value)
            continue;
        if (++uses > 1)
            return 0;
        if (insn->opcode == MIR_MEMBER_ADDRESS &&
            mir_value_only_used_by_stable_pointer_argument(insn->dst))
            continue;
        if (insn->opcode == MIR_LOAD_INDIRECT &&
            mir_indirect_load_is_single_stable_pointer_call_argument(
                insn->dst, 2))
            continue;
        return 0;
    }
    return uses == 1;
}

/* mir-text-size Item T20 (mir-text-size-plan.md): sibling to
 * mir_load_is_single_call_argument above, but for a MIR_ADDRESS
 * (address-of a local/parameter) whose sole use is exactly one
 * MIR_ARG. Unlike a MIR_LOAD's memory *value*, a MIR_ADDRESS's own
 * emission for the non-VLA, non-global local/param shape (this
 * selector's own MIR_ADDRESS case, dcc_mir_spilled_cfg.c) is nothing
 * but `push ix/pop hl` plus a fixed compile-time-constant offset add -
 * a pure, side-effect-free function of ix (which never moves once the
 * prologue runs) that is exactly as cheap to recompute again later as
 * it was to compute the first time. When such a value can't be
 * forwarded straight through HL (typically because another argument's
 * own computation needs HL first), the generic store-to-slot-then-
 * reload fallback wastes two `ld (ix+n),r` stores legacy's own emitter
 * never needed - legacy simply recomputes the address fresh right at
 * the point of use instead of ever storing it. Found via
 * tests/tbcgcol.c's main(): the array-pointer argument to
 * global_bc_across_pointer_loop was needlessly spilled and reloaded
 * even though nothing about it needs preserving across the gap - it
 * can just be recomputed for free. */
static int mir_address_is_single_call_argument(int value)
{
    const struct MirInsn *definition = mir_definition(value);
    int argument_count = 0;
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    if (definition == NULL || definition->opcode != MIR_ADDRESS ||
        mir_declared_is_vla_object(definition->name) ||
        !mir_scalar_memory_location(definition, &memory_type,
                                    &memory_storage, &memory_offset) ||
        (memory_storage != SC_LOCAL && memory_storage != SC_PARAM) ||
        memory_offset < -128 || memory_offset > 127)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->src2 == value)
            return 0;
        if (insn->src1 != value)
            continue;
        if (insn->opcode != MIR_ARG || type_size(insn->type) != 2 ||
            ++argument_count > 1)
            return 0;
    }
    return argument_count == 1;
}

/* mir-text-size Item T25 (mir-text-size-plan.md): sibling to
 * mir_load_is_single_call_argument above, but recognizing a completely
 * different use position - a value whose sole use is the *callee*
 * operand of an indirect MIR_CALL (`insn->src1` when the call's own
 * `name` is the sentinel string "<indirect>"), not an ordinary
 * MIR_ARG. Neither mir_load_is_single_call_argument nor
 * mir_address_is_single_call_argument recognize this use at all - a
 * function pointer read once and called through immediately still
 * takes a full spill-and-reload round-trip through its backend slot
 * even though nothing else in the function ever touches it, exactly
 * the same wasted-slot-traffic shape those two predicates already
 * fixed for the MIR_ARG position. Found via tests/tc89core.c's
 * main(): `fp(41)` (fp a local function-pointer variable) reloads fp
 * from its own backend slot instead of reading it fresh from its
 * stable ix-relative home the way legacy does. */
static int mir_load_is_single_indirect_call_target(int value, int size)
{
    const struct MirInsn *definition = mir_definition(value);
    int use_count = 0;
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    if (definition == NULL || definition->opcode != MIR_LOAD ||
        !mir_scalar_memory_location(definition, &memory_type,
                                    &memory_storage, &memory_offset) ||
        type_size(memory_type) != size ||
        (memory_storage != SC_LOCAL && memory_storage != SC_PARAM) ||
        memory_offset < -128 || memory_offset + size - 1 > 127)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->src2 == value)
            return 0;
        if (insn->src1 != value)
            continue;
        if (insn->opcode != MIR_CALL ||
            strcmp(insn->name, "<indirect>") != 0 || ++use_count > 1)
            return 0;
    }
    return use_count == 1;
}

/* Item 9 (mir-migration-plan-100): DCC_MIR_FUSE_REPORT=1 prints, per function,
 * how many scalar comparisons the Item 1/4 fusion caught versus how many
 * still fell through to mir_emit_scalar_compare's materialize-then-store
 * path, so future regressions in fusion coverage are visible without
 * re-reading assembly. */
static int mir_fuse_report_fused_count;
static int mir_fuse_report_materialized_count;

/* mir-migration-plan-next10 Item 3: mir_try_emit_spilled_scalar_cfg no
 * longer emits a second, unreachable copy of the function epilogue after a
 * function whose last IR instruction is already a MIR_RETURN (that case's
 * own emission already wrote one). That is a pure win for every function
 * already accepted through this selector - real dead code, never executed,
 * removed. But letting the resulting few saved bytes decide accept/reject
 * for a function that was NOT already accepted before this fix would widen
 * the acceptance gate as an unreviewed side effect of a dead-code cleanup,
 * which is exactly what skill rule 1 warns against ("never widen a fallback
 * gate without identifying the exact affected functions first"). Measured:
 * of 6 functions this fix newly promoted, 3 (tmirslot's dead_store_elision,
 * tvla's vla_sizeof_op_add/mullhs/sub) showed real cycle/byte regressions in
 * -Mode full despite passing the static cost gate - the byte savings here
 * is exactly the kind of static-metric improvement skill rule 4 warns is not
 * proof of real speed/size. So: the accept/reject gate below adds this
 * elided text back to `generated_size` before comparing against
 * `captured_size` (restoring the exact pre-fix gate outcome, and therefore
 * the exact pre-fix accepted-function set), while the function's real,
 * already-deduplicated emitted text is what actually gets written for any
 * function that clears the gate on its own unrelated merits. */
long mir_spilled_scalar_cfg_elided_epilogue_bytes = 0;

/* Item 8 (mir-migration-plan-100): when set, mir_prepare_backend_slots must
 * not allocate a frame slot for a comparison result (or intervening '!'
 * result) that mir_try_emit_spilled_scalar_cfg's Items 1/4 fusion consumes
 * entirely in registers - only that selector actually skips the store/load
 * for such values, so this stays off for every other caller. */
static int mir_backend_slots_skip_fused_comparisons = 0;

/* Item 20 (mir-migration-plan-100): DCC_MIR_SLOT_REPORT=1 prints, per
 * function, how many values mir_prepare_backend_slots considered for a
 * frame slot ("requested" - every value reaching its own first-definition
 * point that was not already unconditionally excluded, i.e. still a live
 * candidate) versus how many actually received one ("assigned" - the rest
 * were elided by Items 13-18's dead-value/forwarding/fusion/reuse skip
 * predicates). A large requested/assigned gap on a function flags it as a
 * good candidate for further slot-elision work; a small one means most of
 * that function's slot traffic is already necessary. */
static int mir_slot_report_requested_count;
static int mir_slot_report_assigned_count;
/* mir-migration-plan-next10 parameter-rehoming investigation: how many
 * assigned slots are for a value whose sole definition is MIR_PARAM (i.e.
 * a function parameter re-copied into a fresh backend slot instead of
 * being re-read from its own stable incoming ix+N offset each time it is
 * needed again). Diagnostic-only counter; not read by any acceptance
 * gate. */
static int mir_slot_report_param_assigned_count;

/* Item 13 (mir-migration-plan-100): a definition whose single use is the
 * immediately following instruction (no intervening label/call/aliasing
 * store) never needs a backend slot at all - mir_emit_virtual_store already
 * skips storing it via mir_can_forward_hl_to_next (kept in HL across the
 * one-instruction gap). Reuse mir_can_forward_hl_to_next itself (rather than
 * a second copy of the same forwarding predicate) so the accounting pass and
 * the real emission-time skip can never drift apart - the repo's own Item 19
 * caution. Only 1-unit (narrow, HL-sized) values ever reach
 * mir_emit_virtual_store/mir_can_forward_hl_to_next; wide values use the
 * separate _wide store path and must still get a slot.
 *
 * mir-text-size Item T28 (mir-text-size-plan.md): this elision used to be
 * withheld whenever the forward target was itself a MIR_STORE (via a
 * now-removed mir_backend_slot_forward_target_is_store helper), forcing
 * mir_prepare_backend_slots to still hand out a real slot for such values.
 * But mir_can_forward_hl_to_next's own MIR_STORE case (above) already fully
 * validates that forwarding into a store is safe - resolvable non-struct
 * <=2-byte memory location, no other use anywhere in the function - and
 * mir_emit_virtual_store's has_slot branch already forwards the value via
 * mir_forwarded_hl_value for exactly this case (see forward_to_store
 * there). The only effect of the withheld elision was that the value's own
 * slot got persisted (via an unconditional ld (ix+d),l/h pair, or
 * (iy+d),l/h) and then never read back by anyone, since the real consuming
 * MIR_STORE already takes the value straight from HL. Found via
 * tests/tclit.c's pick_pair(), whose compound-literal field constants were
 * each stored twice: once into this now-dead slot, once into the real
 * per-field destination the closing ldir reads from. */

/* mir-text-size Item T22 (mir-text-size-plan.md): when a value's sole use
 * is an immediately-following 1-byte MIR_STORE (mir_can_forward_hl_to_next's
 * MIR_STORE case, e.g. an int/char constant initializing one element of a
 * byte array), mir_emit_virtual_store still persists BOTH bytes of the
 * value's own backend slot even though only L is ever read back for a
 * 1-byte store - H's persisted byte is never read by anyone, proven by the
 * same mir_can_forward_hl_to_next scan that already showed nothing
 * references the value after the forwarded store. Skipping just that
 * H-byte store (kept narrowly scoped to the forward-to-1-byte-store case,
 * not the general slot-elision Item 13 left alone for stores) closes this
 * gap safely. Found via tests/tc89core.c's main(): its `char msg[] =
 * "core"` initializer stored a wasted high byte for every one of its five
 * constant elements. */
static int mir_forward_store_target_is_narrow(int forward_instruction)
{
    int memory_type;
    int memory_storage;
    int memory_offset;

    if (forward_instruction < 0 || forward_instruction >= mir.count)
        return 0;
    return mir_scalar_memory_location(&mir.insns[forward_instruction],
                                      &memory_type, &memory_storage,
                                      &memory_offset) &&
           type_size(memory_type) == 1;
}

static int mir_backend_slot_forwardable(int value, int units, int instruction)
{
    int saved_index;
    int forwardable;

    if (units != 1)
        return 0;
    /* A MIR_PHI destination is never written by the normal linear emission
     * loop at its own instruction index - mir_emit_spilled_phi_copies writes
     * it from each predecessor's jump/branch instruction instead, with
     * mir_emit_instruction_index left at that unrelated predecessor index.
     * Evaluating mir_can_forward_hl_to_next() there would check the wrong
     * "next instruction" entirely (found live during Item 13 validation via
     * tvla.c's fixed_cast_bounds regression), so phi destinations must always
     * keep a real slot. */
    if (mir.insns[instruction].opcode == MIR_PHI)
        return 0;
    saved_index = mir_emit_instruction_index;
    mir_emit_instruction_index = instruction;
    forwardable = mir_can_forward_hl_to_next(value);
    mir_emit_instruction_index = saved_index;
    return forwardable;
}

/* Item T79 (mir-text-size-plan.md): mir_prepare_backend_slots' reservation
 * pass only ever consulted mir_can_forward_hl_to_next (via
 * mir_backend_slot_forwardable above, which explicitly requires
 * units == 1, i.e. narrow/16-bit values only) to decide whether a value
 * can skip a real slot. Item T40 built the wide (32-bit HL:DE) equivalent,
 * mir_can_forward_hl_de_to_next, and wired it into mir_emit_virtual_
 * store_wide's emission-time decision - but never added a matching
 * reservation-time check, so a wide value that emission will always
 * forward directly (e.g. a wide binary result whose sole use is the very
 * next MIR_RETURN, the plain `return a + b;` shape) still gets a real,
 * dead backend slot reserved for it: 2 units/4 bytes of frame space that
 * the emitted body never once references. Found via tests/tlong.c's
 * lsum (`long lsum(long a, long b) { return a + b; }`): its only gap vs.
 * legacy is exactly this unused `ld hl,-4/add hl,sp/ld sp,hl` prologue
 * pair - once it is skipped, the rest of the body (already using
 * mir_can_forward_hl_de_to_next's push/pop handoff to carry the first
 * wide operand across evaluating the second, matching legacy instruction-
 * for-instruction) already matches legacy byte-for-byte. This is the
 * exact same reservation/emission mismatch class as Item T59's
 * mir_call_argument_slot_forwardable fix, one level up for wide values.
 *
 * Item T86 narrows the reservation skip to measured-profitable direct
 * returns: every integer-long producer, plus float MIR_BINARY results.
 * Float conversions and wide intermediates consumed by MIR_UNARY were
 * correct but slower under the checked emulator, so they retain a slot
 * and stay behind the cost gate. */
static int mir_wide_backend_slot_forwardable(int value, int units,
                                              int instruction)
{
    const struct MirInsn *definition;
    const struct MirInsn *next;
    int next_instruction;
    int saved_index;
    int forwardable;

    if (units != 2)
        return 0;
    if (mir.insns[instruction].opcode == MIR_PHI)
        return 0;
    next_instruction = mir_forward_skip_target(instruction);
    if (next_instruction >= mir.count)
        return 0;
    next = &mir.insns[next_instruction];
    definition = mir_definition(value);
    if (definition == NULL)
        return 0;
    if (!((mir_wide_binary_lhs_forwarding_enabled &&
           next->opcode == MIR_BINARY && next->src1 == value &&
           type_size(next->secondary_offset) == 4) ||
          (mir_wide_store_forwarding_enabled &&
           next->opcode == MIR_STORE && next->src1 == value &&
           (next->memory_size == 4 || type_size(next->type) == 4)) ||
          (mir_wide_binary_rhs_forwarding_enabled &&
           mir_wide_binary_rhs_pair_supported(value, next) &&
           next->src2 == value))) {
        if ((!type_is_long(definition->type) &&
             !(type_is_float(definition->type) &&
               definition->opcode == MIR_BINARY)) ||
            next->opcode != MIR_RETURN || next->src1 != value)
            return 0;
    }
    saved_index = mir_emit_instruction_index;
    mir_emit_instruction_index = instruction;
    forwardable = mir_can_forward_hl_de_to_next(value);
    mir_emit_instruction_index = saved_index;
    return forwardable;
}

static int mir_wide_helper_lhs_consumer(int value, int instruction,
                                        int *consumer_out)
{
    const struct MirInsn *definition;
    int consumer_index;

    if (value < 0 || instruction < 0 || instruction >= mir.count ||
        mir_value_use_count(value) != 1)
        return 0;
    definition = mir_definition(value);
    if (definition == NULL || definition->opcode == MIR_PHI ||
        !mir_definition_is_wide(definition))
        return 0;
    for (consumer_index = instruction + 1;
         consumer_index < mir.count; ++consumer_index) {
        const struct MirInsn *consumer = &mir.insns[consumer_index];
        if (consumer->src1 != value && consumer->src2 != value &&
            !mir_call_uses_value(consumer, value))
            continue;
        if (consumer->opcode != MIR_BINARY || consumer->src1 != value ||
            type_size(consumer->secondary_offset) != 4 ||
            mir_wide_runtime_helper(consumer) == NULL)
            return 0;
        break;
    }
    if (consumer_index >= mir.count)
        return 0;
    if (consumer_out != NULL)
        *consumer_out = consumer_index;
    return 1;
}

static int mir_wide_helper_lhs_span_is_safe(int producer, int consumer)
{
    int instruction;

    for (instruction = producer + 1;
         instruction < consumer; ++instruction)
        switch (mir.insns[instruction].opcode) {
        case MIR_NOP:
        case MIR_CONST:
        case MIR_LOAD:
        case MIR_UNARY:
            break;
        default:
            return 0;
        }
    return 1;
}

static int mir_wide_helper_handoff_supported(const struct MirInsn *consumer)
{
    int operation;

    if (consumer == NULL || consumer->opcode != MIR_BINARY)
        return 0;
    operation = (int)consumer->immediate;
    if (type_is_float(consumer->secondary_offset))
        return operation == '+' || operation == '-' ||
               operation == '*' || operation == '/';
    return operation == '*' || operation == '/' || operation == '%';
}

static int mir_wide_helper_lhs_slot_forwardable(int value, int units,
                                                 int instruction)
{
    int consumer;

    return units == 2 &&
        mir_wide_helper_lhs_consumer(value, instruction, &consumer) &&
        mir_wide_helper_handoff_supported(&mir.insns[consumer]) &&
        mir_wide_helper_lhs_span_is_safe(instruction, consumer);
}

/* Item T59 (mir-text-size-plan.md): mir_prepare_backend_slots' own
 * reservation pass only recognized mir_load_is_single_call_argument (a
 * MIR_LOAD whose sole use is exactly one call argument, restricted to
 * SC_LOCAL/SC_PARAM memory so re-reading it at push time is a cheap
 * ix-relative reload) as grounds to skip a slot for a would-be call
 * argument. mir_emit_virtual_store's own emission-time logic is strictly
 * more capable: mir_can_forward_hl_to_call_argument accepts *any*
 * single-use, adjacent-to-call value regardless of how it was defined
 * (a MIR_LOAD from a global, a MIR_BINARY result, etc.) and simply
 * leaves it resident in HL/queues it for direct forwarding, so it is
 * never actually stored to the slot mir_prepare_backend_slots reserved
 * for it. This mismatch reserves genuinely dead frame space - 2 bytes
 * nothing ever writes to or reads from - for any such value whose
 * defining opcode is not itself a MIR_LOAD-from-local/param (e.g. a
 * MIR_LOAD of a global like `errno`). Found via tests/terrno.c's
 * expect_ok_fd (`printf(..., errno)` on its else-branch): the reserved
 * slot forces an entire IX-relative frame lifecycle
 * (push ix/ld ix,0/add ix,sp/... teardown) for a function whose actual
 * emitted body never references any (ix-N) backend-slot offset at all.
 * Reusing the exact same predicate the emitter already trusts to elide
 * the store closes this gap with no new mechanism. */
static int mir_call_argument_slot_forwardable(int value, int units,
                                               int instruction)
{
    int saved_index;
    int forwardable;

    if (units != 1)
        return 0;
    if (mir.insns[instruction].opcode == MIR_PHI)
        return 0;
    saved_index = mir_emit_instruction_index;
    mir_emit_instruction_index = instruction;
    forwardable = mir_can_forward_hl_to_call_argument(value);
    mir_emit_instruction_index = saved_index;
    return forwardable;
}

/* mir-migration-plan-next10 (leaf frame-convention safety): legacy sometimes
 * emits a function with no `ix` frame at all - reading parameters directly
 * off `sp` with a leading `add hl,sp` - for sufficiently trivial leaf
 * bodies. `mir_try_emit_spilled_scalar_cfg` has no equivalent frameless
 * path: it always pays for `push ix`/`ld ix,0`/`add ix,sp` and the matching
 * teardown. Ordinarily that fixed overhead keeps such functions out of the
 * text-size gate entirely, but direct-object-forwarding a parameter can
 * shrink the rest of the body just enough to tip the gate anyway, even
 * though the resulting machine code pays real extra frame-setup cost
 * legacy never incurred (skill rule 4). Detected by inspecting the already-
 * captured legacy replay stream for this function: if it never emits
 * `push ix`, treat the function as using a lighter-weight convention MIR
 * cannot match yet, and keep every parameter on the ordinary slot path so
 * this optimization cannot be the deciding factor for that gate. Confirmed
 * via tc89fnty's mulb full-mode regression. */
static int mir_capture_stream_uses_frame(void)
{
    /* Item T34 (mir-text-size-plan.md): this used to cache cached_result
     * keyed only on `cached_stream == mir.capture_stream` (a raw FILE*
     * pointer comparison). mir_begin_function calls tmpfile() fresh for
     * every single function and closes the previous one, so the C
     * library is free to (and in practice routinely does) hand back the
     * exact same FILE* address for the next function's stream once the
     * old one is closed - the two "static" cache variables then silently
     * carried the *first* function's frame/frameless verdict forward and
     * applied it to every later function that happened to reuse that
     * address, regardless of that function's own captured output. This
     * starved mir_value_has_direct_named_home of ever firing for any function
     * unlucky enough to share a reused tmpfile() address with an earlier
     * frameless one - a real, cross-function correctness bug in the
     * cache, not a deliberate memoization tradeoff. Recomputing fresh on
     * every call removes the whole bug class; the scan itself is bounded
     * by one function's own captured-assembly length and already restores
     * the stream's read position afterward, so there is no correctness
     * or ordering hazard in dropping the cache. */
    static const char needle[] = "push ix";
    int character;
    int matched;
    long saved_position;
    int result;

    if (mir.capture_stream == NULL)
        return 1;
    saved_position = ftell(mir.capture_stream);
    rewind(mir.capture_stream);
    result = 0;
    matched = 0;
    while ((character = fgetc(mir.capture_stream)) != EOF) {
        if (character == needle[matched]) {
            ++matched;
            if (needle[matched] == '\0') {
                result = 1;
                break;
            }
        } else {
            matched = (character == needle[0]) ? 1 : 0;
        }
    }
    if (saved_position >= 0)
        fseek(mir.capture_stream, saved_position, SEEK_SET);
    return result;
}

static int mir_direct_named_home_location(const struct MirInsn *definition,
                                          int *storage, int *offset)
{
    int memory_type;

    if (definition == NULL)
        return 0;
    if (definition->object >= 0 && definition->object < mir.object_count) {
        const struct MirObject *object = &mir.objects[definition->object];
        *storage = object->storage;
        *offset = object->offset;
        return 1;
    }
    return mir_scalar_memory_location(definition, &memory_type,
                                      storage, offset) &&
        (*storage == SC_PARAM || *storage == SC_LOCAL) &&
        type_size(memory_type) == type_size(definition->type);
}

static int mir_pointer_value_is_aggregate_address(int value)
{
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode == MIR_COPY_AGGREGATE &&
            (insn->src1 == value || insn->src2 == value))
            return 1;
        if (insn->opcode == MIR_CALL_AGGREGATE && insn->src1 == value)
            return 1;
    }
    return 0;
}

/* A value does not need a backend slot while its named IX-relative location
 * remains unchanged. Parameters are stable for the complete function after
 * the no-reassignment proof. A pointer local is stable after its MIR_LOAD when
 * no later store, alias, or backedge can change its slot. Scalar locals remain
 * slot-based: the measured prototype added only one further function and does
 * not justify widening this first local slice. */
static int mir_value_has_direct_named_home(int value)
{
    const struct MirInsn *definition;
    int has_object;
    int object;
    int home_storage;
    int home_offset;
    int definition_index;
    int local_home;
    int i;

    if (value < 0 || value >= mir.next_value)
        return 0;
    definition = mir_definition(value);
    if (definition == NULL ||
        (definition->opcode != MIR_PARAM && definition->opcode != MIR_LOAD))
        return 0;
    if (type_is_struct_object(definition->type) ||
        (type_size(definition->type) != 1 &&
         type_size(definition->type) != 2 &&
         type_size(definition->type) != 4))
        return 0;
    object = definition->object;
    has_object = object >= 0 && object < mir.object_count;
    if (!mir_direct_named_home_location(definition, &home_storage,
                                        &home_offset))
        return 0;
    local_home = home_storage == SC_LOCAL;
    definition_index = (int)(definition - mir.insns);
    if (local_home &&
        (!mir_stable_pointer_local_homes_enabled ||
         definition->opcode != MIR_LOAD ||
         (definition->memory_flags & 1) != 0 ||
         type_ptr_depth(definition->type) == 0 ||
         local_name_address_taken_in_function(definition->name) ||
         mir_has_cfg_backedge()))
        return 0;
    if (!local_home && !has_object &&
        !mir_pointer_value_is_aggregate_address(value))
        return 0;
    if (mir.has_vla)
        return 0;
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        int memory_type;
        int memory_storage;
        int memory_offset;

        if (insn->opcode != MIR_STORE)
            continue;
        if (has_object && insn->object == object) {
            if (local_home && i < definition_index)
                continue;
            return 0;
        }
        if (!has_object &&
            mir_scalar_memory_location(insn, &memory_type,
                                       &memory_storage, &memory_offset) &&
            memory_storage == home_storage &&
            memory_offset == home_offset) {
            if (local_home && i < definition_index)
                continue;
            return 0;
        }
    }
    if (!local_home && definition->opcode == MIR_LOAD) {
        int has_param = 0;
        for (i = 0; i < mir.count; ++i) {
            const struct MirInsn *insn = &mir.insns[i];
            int memory_type;
            int memory_storage;
            int memory_offset;

            if (insn->opcode != MIR_PARAM)
                continue;
            if ((has_object && insn->object == object) ||
                (!has_object &&
                 mir_scalar_memory_location(insn, &memory_type,
                                            &memory_storage,
                                            &memory_offset) &&
                 memory_storage == SC_PARAM &&
                 memory_offset == home_offset)) {
                has_param = 1;
                break;
            }
        }
        if (!has_param)
            return 0;
    }
    if (!mir_capture_stream_uses_frame())
        return 0;
    /* mir-migration-plan-next10 (divmod-fusion safety): a fused divmod pair
     * (mir_divmod_partner) must eagerly materialize BOTH its quotient and
     * remainder results into two simultaneous backend slots at whichever
     * operator is encountered first, unlike legacy's serial one-slot-
     * reused-in-turn replay - this needs strictly more frame bytes than
     * legacy for that pair alone. Direct-object-forwarding one of the
     * pair's own parameter operands then quietly shrinks the *rest* of the
     * function's frame just enough to tip its text-size accept/reject
     * gate over, even though the fused pair's own extra frame bytes make
     * the real (stack-check-instrumented) machine code slower, not
     * faster - confirmed via tdmfuse's sdm_pair/sdm_pair_r full-mode
     * regression (skill rule 4: static byte counts are not proof of real
     * cost). Keep any parameter that is a live operand of a fused
     * divmod pair on its ordinary backend-slot path so this optimization
     * cannot influence that gate decision for these functions, while
     * still applying normally to every parameter not involved in such a
     * pair. */
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->opcode == MIR_BINARY &&
            (insn->immediate == '/' || insn->immediate == '%') &&
            (insn->src1 == value || insn->src2 == value) &&
            mir_divmod_partner(i) >= 0)
            return 0;
    }
    if (local_home)
        mir_spilled_cfg_used_stable_pointer_local_home = 1;
    return 1;
}

void mir_begin_stable_pointer_local_homes(void)
{
    mir_stable_pointer_local_homes_enabled = 1;
}

void mir_end_stable_pointer_local_homes(void)
{
    mir_stable_pointer_local_homes_enabled = 0;
}

void mir_begin_general_rhs_stack_forwarding(void)
{
    mir_general_rhs_stack_forwarding_enabled = 1;
}

void mir_end_general_rhs_stack_forwarding(void)
{
    mir_general_rhs_stack_forwarding_enabled = 0;
}

void mir_begin_indirect_store_value_forwarding(void)
{
    mir_indirect_store_value_forwarding_enabled = 1;
}

void mir_end_indirect_store_value_forwarding(void)
{
    mir_indirect_store_value_forwarding_enabled = 0;
}

void mir_begin_branch_condition_forwarding(void)
{
    mir_branch_condition_forwarding_enabled = 1;
}

void mir_end_branch_condition_forwarding(void)
{
    mir_branch_condition_forwarding_enabled = 0;
}

void mir_begin_indirect_store_address_forwarding(void)
{
    mir_indirect_store_address_forwarding_enabled = 1;
}

void mir_end_indirect_store_address_forwarding(void)
{
    mir_indirect_store_address_forwarding_enabled = 0;
}

void mir_begin_wide_binary_lhs_forwarding(void)
{
    mir_wide_binary_lhs_forwarding_enabled = 1;
}

void mir_end_wide_binary_lhs_forwarding(void)
{
    mir_wide_binary_lhs_forwarding_enabled = 0;
}

void mir_begin_wide_binary_rhs_forwarding(void)
{
    mir_wide_binary_rhs_forwarding_enabled = 1;
    mir_wide_binary_rhs_forwarding_uses = 0;
}

void mir_end_wide_binary_rhs_forwarding(void)
{
    mir_wide_binary_rhs_forwarding_enabled = 0;
}

int mir_wide_binary_rhs_forwarding_use_count(void)
{
    return mir_wide_binary_rhs_forwarding_uses;
}

void mir_begin_wide_store_forwarding(void)
{
    mir_wide_store_forwarding_enabled = 1;
}

void mir_end_wide_store_forwarding(void)
{
    mir_wide_store_forwarding_enabled = 0;
}

void mir_begin_stable_pointer_argument_rematerialization(void)
{
    mir_stable_pointer_argument_rematerialization_enabled = 1;
}

void mir_end_stable_pointer_argument_rematerialization(void)
{
    mir_stable_pointer_argument_rematerialization_enabled = 0;
}

void mir_begin_global_argument_rematerialization(void)
{
    mir_global_argument_rematerialization_enabled = 1;
}

void mir_end_global_argument_rematerialization(void)
{
    mir_global_argument_rematerialization_enabled = 0;
}

void mir_begin_wide_first_argument_stack_cache(void)
{
    mir_wide_first_argument_stack_cache_enabled = 1;
}

void mir_end_wide_first_argument_stack_cache(void)
{
    mir_wide_first_argument_stack_cache_enabled = 0;
}

void mir_begin_narrow_argument_direct_push(void)
{
    mir_narrow_argument_direct_push_enabled = 1;
}

void mir_end_narrow_argument_direct_push(void)
{
    mir_narrow_argument_direct_push_enabled = 0;
}

void mir_begin_constant_argument_prepacking(void)
{
    mir_constant_argument_prepacking_enabled = 1;
}

void mir_end_constant_argument_prepacking(void)
{
    mir_constant_argument_prepacking_enabled = 0;
}

void mir_begin_promoted_local_slot_reuse(void)
{
    mir_promoted_local_slot_reuse_enabled = 1;
}

void mir_end_promoted_local_slot_reuse(void)
{
    mir_promoted_local_slot_reuse_enabled = 0;
}

static int mir_planned_stack_interval_opcode_safe(int opcode)
{
    switch (opcode) {
    case MIR_NOP:
    case MIR_PARAM:
    case MIR_CONST:
    case MIR_FLOAT_CONST:
    case MIR_STRING_ADDRESS:
    case MIR_ADDRESS:
    case MIR_COMPOUND_ADDRESS:
    case MIR_INDEX_ADDRESS:
    case MIR_MEMBER_ADDRESS:
    case MIR_LOAD:
    case MIR_LOAD_INDIRECT:
    case MIR_INDEX_LOAD:
    case MIR_STORE:
    case MIR_STORE_INDIRECT:
    case MIR_COPY_AGGREGATE:
    case MIR_UNARY:
    case MIR_BINARY:
    case MIR_DECL_PLACEHOLDER:
    case MIR_OBJECT_MERGE:
        return 1;
    default:
        return 0;
    }
}

static int mir_planned_stack_consumer(int value, int producer,
                                      const unsigned char *occupied)
{
    const struct MirInsn *definition;
    const struct MirInsn *consumer_insn;
    int consumer;
    int instruction;

    if (!mir_planned_stack_handoffs_enabled || mir.has_vla ||
        value < 0 || producer < 0 || producer >= mir.count ||
        mir_value_use_count(value) != 1)
        return -1;
    definition = mir_definition(value);
    if (definition == NULL || definition->opcode == MIR_PARAM ||
        definition->opcode == MIR_PHI ||
        definition->opcode == MIR_CALL_AGGREGATE ||
        mir_call_has_odd_argument_bytes(definition) ||
        (type_size(definition->type) != 1 &&
         type_size(definition->type) != 2))
        return -1;
    consumer = -1;
    for (instruction = producer + 1;
         instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->src1 == value || insn->src2 == value ||
            mir_call_uses_value(insn, value)) {
            consumer = instruction;
            break;
        }
    }
    if (consumer < 0 || consumer - producer < 2)
        return -1;
    consumer_insn = &mir.insns[consumer];
    if (!((consumer_insn->opcode == MIR_BINARY &&
           consumer_insn->src1 == value &&
           type_size(consumer_insn->secondary_offset) <= 2) ||
          (consumer_insn->opcode == MIR_INDEX_ADDRESS &&
           consumer_insn->src1 == value &&
           consumer_insn->base_name[0] == 0 &&
           !mir_value_only_used_by_constant_absolute_address(
               consumer_insn->dst)) ||
          (mir_indirect_store_address_forwarding_enabled &&
           consumer - producer == 2 &&
           consumer_insn->opcode == MIR_STORE_INDIRECT &&
           consumer_insn->src1 == value &&
           consumer_insn->bit_width == 0 &&
           consumer_insn->memory_size > 0 &&
           consumer_insn->memory_size <= 2 &&
           mir.insns[consumer - 1].dst == consumer_insn->src2 &&
           mir_value_use_count(consumer_insn->src2) == 1)))
        return -1;
    if (consumer - producer <= 2 &&
        consumer_insn->opcode != MIR_STORE_INDIRECT)
        return -1;
    for (instruction = producer + 1;
         instruction < consumer; ++instruction)
        if (!mir_planned_stack_interval_opcode_safe(
                mir.insns[instruction].opcode))
            return -1;
    for (instruction = producer; instruction <= consumer; ++instruction)
        if (occupied[instruction])
            return -1;
    return consumer;
}

static void mir_verify_planned_stack_handoffs(void)
{
    int last_consumer = -1;
    int instruction;
    int value;

    if (!mir_planned_stack_handoffs_enabled)
        return;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *definition;
        int producer;

        value = mir.planned_stack_values[instruction];
        if (value < 0)
            continue;
        if (value >= mir.next_value ||
            mir.planned_stack_consumers[value] != instruction)
            fatal("inconsistent MIR planned stack handoff");
        definition = mir_definition(value);
        if (definition == NULL)
            fatal("missing MIR planned stack producer");
        producer = (int)(definition - mir.insns);
        /* Batch 22 deliberately selects a stricter-than-laminar policy:
         * planned intervals may neither cross nor nest nor share an
         * endpoint. This guarantees at most one planned value is pending
         * at any instruction; its plan tables and emitted bit remain
         * authoritative independently of ad-hoc stack forwarding. */
        if (producer <= last_consumer || producer >= instruction)
            fatal("overlapping MIR planned stack handoffs");
        last_consumer = instruction;
    }
    for (value = 0; value < mir.next_value; ++value) {
        instruction = mir.planned_stack_consumers[value];
        if (instruction >= 0 &&
            (instruction >= mir.count ||
             mir.planned_stack_values[instruction] != value))
            fatal("inconsistent MIR planned stack consumer");
    }
}

static int mir_promoted_local_slot_hole(int object, int *offset, int *size)
{
    const struct MirObject *candidate;
    int effective_local_bytes;
    int instruction;

    if (object < 0 || object >= mir.object_count)
        return 0;
    candidate = &mir.objects[object];
    *size = type_size(candidate->type);
    *offset = candidate->offset;
    effective_local_bytes = mir_effective_local_bytes();
    if (candidate->storage != SC_LOCAL ||
        (*size != 2 && *size != 4) ||
        *offset < -effective_local_bytes || *offset + *size > 0 ||
        !mir_object_is_fully_promoted(object) ||
        mir_object_address_taken(object))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        int memory_type;
        int memory_storage;
        int memory_offset;
        int memory_size;

        if (insn->opcode != MIR_PARAM && insn->opcode != MIR_LOAD &&
            insn->opcode != MIR_STORE && insn->opcode != MIR_ADDRESS)
            continue;
        if (!mir_scalar_memory_location(insn, &memory_type, &memory_storage,
                                        &memory_offset))
            return 0;
        memory_size = type_size(memory_type);
        if (memory_storage != SC_LOCAL || memory_size <= 0 ||
            !mir_ranges_overlap(*offset, *size, memory_offset, memory_size))
            continue;
        if (insn->object == object && insn->opcode == MIR_STORE)
            continue;
        return 0;
    }
    return 1;
}

static int mir_seed_promoted_local_slots(int *slot_end, int slot_capacity)
{
    int object;
    int slot_count = 0;

    if (!mir_promoted_local_slot_reuse_enabled)
        return 0;
    for (object = 0; object < mir.object_count; ++object) {
        int offset;
        int size;
        int candidate_unit;
        int overlaps = 0;
        int slot;

        if (!mir_promoted_local_slot_hole(object, &offset, &size))
            continue;
        for (candidate_unit = 0; candidate_unit < size / 2;
             ++candidate_unit) {
            int candidate_offset = offset + 2 * candidate_unit;
            for (slot = 0; slot < slot_count; ++slot)
                if (mir_ranges_overlap(candidate_offset, 2,
                                       mir_backend_slot_offsets[slot], 2)) {
                    overlaps = 1;
                    break;
                }
            if (overlaps)
                break;
        }
        if (overlaps || slot_count + size / 2 > slot_capacity)
            continue;
        if (size == 4) {
            mir_backend_slot_offsets[slot_count] = offset + 2;
            mir_backend_slot_offsets[slot_count + 1] = offset;
            slot_end[slot_count] = -1;
            slot_end[slot_count + 1] = -1;
            slot_count += 2;
        } else {
            mir_backend_slot_offsets[slot_count] = offset;
            slot_end[slot_count] = -1;
            ++slot_count;
        }
    }
    return slot_count;
}

static int mir_prepare_backend_slots(void)
{
    int *first;
    int *last;
    int *slot_end;
    char *fused_away = NULL;
    unsigned char *stack_interval_occupied = NULL;
    int planned_narrow_cache_call = -1;
    int planned_wide_cache_call = -1;
    int slot_capacity;
    int value;
    int i;

    if (mir.next_value <= 0) {
        mir.backend_slot_count = 0;
        return 0;
    }

    if (mir.backend_slot_capacity < mir.next_value) {
        int *new_slots = (int *)realloc(
            mir.backend_slots, (size_t)mir.next_value * sizeof(*new_slots));
        if (new_slots == NULL)
            fatal("out of memory allocating MIR backend slots");
        mir.backend_slots = new_slots;
        mir.backend_slot_capacity = mir.next_value;
    }
    if (mir.planned_stack_consumer_capacity < mir.next_value) {
        int *new_consumers = (int *)realloc(
            mir.planned_stack_consumers,
            (size_t)mir.next_value * sizeof(*new_consumers));
        if (new_consumers == NULL)
            fatal("out of memory allocating MIR planned stack consumers");
        mir.planned_stack_consumers = new_consumers;
        mir.planned_stack_consumer_capacity = mir.next_value;
    }
    if (mir.planned_stack_value_capacity < mir.count) {
        int *new_values = (int *)realloc(
            mir.planned_stack_values,
            (size_t)mir.count * sizeof(*new_values));
        if (new_values == NULL)
            fatal("out of memory allocating MIR planned stack values");
        mir.planned_stack_values = new_values;
        mir.planned_stack_value_capacity = mir.count;
    }
    if (mir.planned_stack_emitted_capacity < mir.next_value) {
        unsigned char *new_emitted = (unsigned char *)realloc(
            mir.planned_stack_emitted,
            (size_t)mir.next_value * sizeof(*new_emitted));
        if (new_emitted == NULL)
            fatal("out of memory allocating MIR planned stack state");
        mir.planned_stack_emitted = new_emitted;
        mir.planned_stack_emitted_capacity = mir.next_value;
    }
    for (value = 0; value < mir.next_value; ++value)
        mir.planned_stack_consumers[value] = -1;
    memset(mir.planned_stack_emitted, 0,
           (size_t)mir.next_value * sizeof(*mir.planned_stack_emitted));
    for (i = 0; i < mir.count; ++i)
        mir.planned_stack_values[i] = -1;
    first = (int *)malloc((size_t)mir.next_value * sizeof(*first));
    last = (int *)malloc((size_t)mir.next_value * sizeof(*last));
    slot_capacity = mir.next_value * 2 + mir.object_count * 2;
    slot_end = (int *)malloc((size_t)slot_capacity * sizeof(*slot_end));
    if (first == NULL || last == NULL || slot_end == NULL)
        fatal("out of memory computing MIR backend intervals");
    if (mir_backend_slot_offset_capacity < slot_capacity) {
        int *new_offsets = (int *)realloc(
            mir_backend_slot_offsets,
            (size_t)slot_capacity * sizeof(*new_offsets));
        if (new_offsets == NULL)
            fatal("out of memory computing MIR backend slot offsets");
        mir_backend_slot_offsets = new_offsets;
        mir_backend_slot_offset_capacity = slot_capacity;
    }
    if (mir_planned_stack_handoffs_enabled) {
        stack_interval_occupied =
            (unsigned char *)calloc((size_t)mir.count, 1);
        if (stack_interval_occupied == NULL)
            fatal("out of memory planning MIR stack handoffs");
    }
    if (mir_backend_slots_skip_fused_comparisons) {
        fused_away = (char *)calloc((size_t)mir.next_value, 1);
        if (fused_away == NULL)
            fatal("out of memory computing MIR fused-comparison set");
        for (i = 0; i < mir.count; ++i) {
            int skip = mir_binary_is_fusable_comparison(i);
            if (skip > 0)
                fused_away[mir.insns[i].dst] = 1;
            if (skip == 2)
                fused_away[mir.insns[i + 1].dst] = 1;
            if (mir_unary_is_fusable_not_branch(i))
                fused_away[mir.insns[i].dst] = 1;
            {
                int multiply_index;
                if (mir_float_madd_match(i, &multiply_index, NULL))
                    fused_away[mir.insns[multiply_index].dst] = 1;
            }
        }
    }
    for (value = 0; value < mir.next_value; ++value) {
        first[value] = mir.count;
        last[value] = -1;
        mir.backend_slots[value] = -1;
    }
    for (value = 0; value < slot_capacity; ++value)
        slot_end[value] = -1;
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->dst >= 0) {
            first[insn->dst] = i;
            last[insn->dst] = i;
        }
        if (insn->src1 >= 0 && last[insn->src1] < i)
            last[insn->src1] = i;
        if (insn->src2 >= 0 && last[insn->src2] < i)
            last[insn->src2] = i;
        if (insn->opcode == MIR_CALL ||
            insn->opcode == MIR_CALL_AGGREGATE) {
            int argument;
            for (argument = 0; argument < mir.next_value; ++argument)
                if (mir_call_uses_value(insn, argument) &&
                    last[argument] < i)
                    last[argument] = i;
        }
    }
    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_PHI) {
            int phi_start = i;
            int predecessor;
            while (phi_start > 0 &&
               (mir.insns[phi_start - 1].opcode == MIR_PHI ||
                mir.insns[phi_start - 1].opcode == MIR_NOP))
                --phi_start;
            while (phi_start < i &&
               mir.insns[phi_start].opcode == MIR_NOP)
            ++phi_start;
            for (predecessor = 0; predecessor < mir.count; ++predecessor) {
                int successor;
                int predecessor_label = mir_block_label_before(predecessor);
                for (successor = 0;
                     successor < mir.insns[predecessor].successor_count;
                     ++successor) {
                    int target = mir_first_phi_or_block_end(
                        mir.insns[predecessor].successors[successor]);
                    if (target != phi_start)
                        continue;
                    if (predecessor_label == mir.insns[i].phi_pred1 &&
                        last[mir.insns[i].src1] < predecessor)
                        last[mir.insns[i].src1] = predecessor;
                    if (predecessor_label == mir.insns[i].phi_pred2 &&
                        last[mir.insns[i].src2] < predecessor)
                        last[mir.insns[i].src2] = predecessor;
                }
            }
        }
    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_JUMP) {
            int target = mir_find_label(mir.insns[i].label);
            if (target >= 0 && target < i)
                for (value = 0; value < mir.next_value; ++value)
                    if (first[value] < target && last[value] >= target &&
                        last[value] < i)
                        last[value] = i;
        }
    /* mir-migration-plan-next10 (param-direct follow-up): a fused
     * divmod pair (mir_divmod_partner, emission ~9000) writes BOTH the
     * modulo and division result values eagerly at whichever of the two
     * MIR_BINARY instructions is encountered first in program order, not
     * at each result's own defining instruction - the second instruction
     * only "break"s without emitting anything, trusting the earlier
     * store. first[]/last[] computed above only account for each value's
     * own instruction index, so a result written early this way was
     * still reported as first-live only at its own (later) instruction,
     * understating its true live range by the gap between the two
     * instructions. Any other value's slot reused across exactly that
     * gap can then alias and clobber the early-written result before its
     * real use - a latent hazard nothing forced into the open until the
     * parameter-rehoming change (below) perturbed slot numbering enough
     * for two such values to collide (surfaced as tmuldiv's ui16_test
     * modulo/division mixups). Extend both partners' first[] to the
     * earlier instruction so slot reuse across that gap is impossible. */
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        int partner;
        if (insn->opcode != MIR_BINARY ||
            (insn->immediate != '/' && insn->immediate != '%'))
            continue;
        partner = mir_divmod_partner(i);
        if (partner > i) {
            if (first[insn->dst] > i)
                first[insn->dst] = i;
            if (first[mir.insns[partner].dst] > i)
                first[mir.insns[partner].dst] = i;
        }
    }
    /* Item T51 (mir-text-size-plan.md): mir_binary_only_constant and
     * mir_index_only_constant already make the MIR_CONST case (above,
     * where the value is defined) and the MIR_BINARY/MIR_INDEX_ADDRESS
     * consumer sites (below, in the emitter) both emit zero code for a
     * constant whose sole use qualifies - the definition site skips its
     * own "ld hl,<const>"/store entirely, and the consumer materializes
     * the constant directly as an immediate (e.g. "ld de,<const>")
     * instead of ever calling mir_emit_virtual_load/mir_emit_virtual_store
     * for it. Despite this, mir_prepare_backend_slots never excluded
     * these two predicates from slot assignment - unlike the structurally
     * identical mir_call_only_constant (a sibling predicate for the
     * MIR_CALL-argument case, already excluded here) - so such a value
     * still got a live backend slot reserved for it: 2 bytes of dead
     * frame space nothing ever stores to or reads from. Found via
     * tests/t2darr.c's check() (`failures = failures + 1;`): the
     * constant 1's sole use is the ADD's right-hand operand, qualifying
     * for mir_binary_only_constant, yet a slot was still reserved. This
     * pattern recurs across the whole `check`/`chk`/`okb`/`fail`
     * assertion-helper family (50+ functions sharing the same shape) and
     * is the same class of dead-slot waste as the already-known,
     * narrower Item T33 (wumpus.c's rndix). */
    /* Batch 11 extends the same one-predicate discipline to two more
     * emission/reservation mismatches. Deferred metadata can leave values
     * defined by analysis-only MIR_NOP instructions, which emit no access,
     * and mir_address_is_single_call_argument makes both the MIR_ADDRESS
     * definition and its MIR_ARG consumer rematerialize without a slot.
     * Reuse those existing structural facts here rather than reserving
     * frame space that neither emission path can reference. */
    mir.backend_slot_count =
        mir_seed_promoted_local_slots(slot_end, slot_capacity);
    mir_backend_local_slot_count = mir.backend_slot_count;
    mir_backend_slot_pool_count = mir.backend_slot_count;
    mir_backend_frame_slot_count = 0;
    mir_slot_report_requested_count = 0;
    mir_slot_report_assigned_count = 0;
    mir_slot_report_param_assigned_count = 0;
    for (i = 0; i < mir.count; ++i) {
        if (planned_narrow_cache_call == i)
            planned_narrow_cache_call = -1;
        if (planned_wide_cache_call == i)
            planned_wide_cache_call = -1;
        for (value = 0; value < mir.next_value; ++value)
            if (first[value] == i) {
                int cache_target;
                int stack_consumer;
                int slot;
                const struct MirInsn *definition = mir_definition(value);
                int units = mir_definition_is_wide(definition) ? 2 : 1;
                int reusable_source = -1;
                ++mir_slot_report_requested_count;
                if (last[value] <= first[value] ||
                                        (definition != NULL &&
                                         definition->opcode == MIR_NOP) ||
                                        mir_scalar_constant_is_rematerializable(
                                            value) ||
                                        mir_string_address_is_rematerializable(
                                            value) ||
                                        mir_wide_constant_is_rematerializable(
                                            value) ||
                                        mir_address_is_rematerializable(value) ||
                                        mir_call_only_constant(value) ||
                                        mir_binary_only_constant(value) ||
                                        mir_index_only_constant(value) ||
                                        mir_value_only_used_by_constant_absolute_address(value) ||
                                        mir_multiply_by_small_constant(value) ||
                                        (fused_away != NULL && fused_away[value]) ||
                                        mir_value_is_selfstore_incdec(value) ||
                                        mir_value_is_selfstore_incdec_source(value) ||
                                        ((type_size(definition->type) == 2 ||
                                            type_size(definition->type) == 4) &&
                                         mir_load_is_single_call_argument(value,
                                                                                                            type_size(definition->type))) ||
                                        mir_value_only_used_by_stable_pointer_argument(
                                            value) ||
                                        (type_size(definition->type) == 2 &&
                                         mir_indirect_load_is_single_stable_pointer_call_argument(
                                             value, 2)) ||
                                        (type_size(definition->type) == 2 &&
                                         mir_global_load_is_single_call_argument(
                                             value, 2)) ||
                                        (type_size(definition->type) == 2 &&
                                         mir_load_is_single_indirect_call_target(value, 2)) ||
                                        mir_address_is_single_call_argument(value) ||
                                        mir_backend_slot_forwardable(value, units, i) ||
                                        mir_wide_backend_slot_forwardable(value, units, i) ||
                                        mir_wide_helper_lhs_slot_forwardable(value, units, i) ||
                                        mir_call_argument_slot_forwardable(value, units, i) ||
                                        mir_stack_backend_slot_forwardable(value, units, i) ||
                                        mir_value_is_nested_truth_comparison_input(value) ||
                                        mir_value_only_used_by_dead_stores(value) ||
                                        mir_value_only_used_by_dead_unary(value))
                    continue;
                if (mir_value_has_direct_named_home(value)) {
                    int home_storage;
                    int home_offset;
                    if (mir_direct_named_home_location(
                            definition, &home_storage, &home_offset) &&
                        home_storage == SC_LOCAL) {
                        mir_spilled_cfg_used_stable_pointer_local_home = 1;
                        mir_spilled_cfg_used_stable_pointer_local_slot = 1;
                    }
                    continue;
                }
                stack_consumer = mir_planned_stack_consumer(
                    value, i, stack_interval_occupied);
                if (stack_consumer >= 0) {
                    int instruction;

                    mir.planned_stack_consumers[value] = stack_consumer;
                    mir.planned_stack_values[stack_consumer] = value;
                    for (instruction = i;
                         instruction <= stack_consumer; ++instruction)
                        stack_interval_occupied[instruction] = 1;
                    continue;
                }
                /*
                 * The emitters can retain one pending call argument in BC
                 * or the alternate register set instead of touching its
                 * assigned slot. Plan that choice in the same definition
                 * order as emission so cache occupancy is exact. A
                 * cacheable definition-to-call span
                 * permits no other value-producing instruction, so narrow
                 * and wide cache lifetimes cannot overlap.
                 */
                cache_target = mir_call_argument_cache_target_for_state(
                    value, i, planned_narrow_cache_call >= 0,
                    planned_wide_cache_call >= 0);
                if (cache_target >= 0) {
                    if (units == 2 &&
                        mir_wide_call_argument_is_first_pushed(
                            value, cache_target))
                        mir.backend_slots[value] =
                            MIR_BACKEND_SLOT_WIDE_ARGUMENT_STACK_CACHE;
                    else if (units == 1 &&
                             mir_narrow_argument_direct_push_enabled)
                        mir.backend_slots[value] =
                            MIR_BACKEND_SLOT_NARROW_ARGUMENT_DIRECT_PUSH;
                    else
                        mir.backend_slots[value] =
                            MIR_BACKEND_SLOT_CALL_CACHE;
                    if (units == 2)
                        planned_wide_cache_call = cache_target;
                    else
                        planned_narrow_cache_call = cache_target;
                    continue;
                }
                ++mir_slot_report_assigned_count;
                if (definition != NULL && definition->opcode == MIR_PARAM)
                    ++mir_slot_report_param_assigned_count;
                if (definition != NULL && definition->opcode == MIR_BINARY &&
                    ((units == 1 && type_size(definition->secondary_offset) == 2) ||
                     (units == 2 && type_size(definition->secondary_offset) == 4) ||
                     (units == 1 && type_size(definition->secondary_offset) == 4))) {
                    /* A narrow (16-bit) boolean result from a wide (32-bit)
                     * comparison still has two dying 32-bit operand units
                     * available; match reuse against the operand width, not
                     * the (narrower) result width, and take only the first
                     * unit of a wide operand's slot for the result. */
                    int operand_units =
                        type_size(definition->secondary_offset) == 4 ? 2 : 1;
                    if (definition->src1 >= 0 && last[definition->src1] == i &&
                        mir.backend_slots[definition->src1] >= 0 &&
                        (mir_definition_is_wide(mir_definition(
                             definition->src1)) ? 2 : 1) == operand_units)
                        reusable_source = definition->src1;
                    else if (definition->src2 >= 0 && last[definition->src2] == i &&
                             mir.backend_slots[definition->src2] >= 0 &&
                             (mir_definition_is_wide(mir_definition(
                                  definition->src2)) ? 2 : 1) == operand_units)
                        reusable_source = definition->src2;
                } else if (definition != NULL && definition->opcode == MIR_UNARY &&
                    definition->src1 >= 0 &&
                    (mir_definition_is_wide(mir_definition(
                         definition->src1)) ? 2 : 1) == units &&
                    last[definition->src1] == i &&
                    mir.backend_slots[definition->src1] >= 0) {
                    reusable_source = definition->src1;
                }
                if (reusable_source >= 0) {
                    int unit;
                    slot = mir.backend_slots[reusable_source];
                    mir.backend_slots[value] = slot;
                    if (slot < mir_backend_local_slot_count)
                        mir_spilled_cfg_used_promoted_local_slot = 1;
                    for (unit = 0; unit < units; ++unit)
                        slot_end[slot + unit] = last[value];
                    continue;
                }
                for (slot = 0; slot + units <= mir.backend_slot_count; ++slot) {
                    int unit;
                    int available = 1;
                    if (units == 2 &&
                        mir_backend_slot_offsets[slot + 1] !=
                            mir_backend_slot_offsets[slot] - 2)
                        continue;
                    for (unit = 0; unit < units; ++unit)
                        if (slot_end[slot + unit] >= i) {
                            available = 0;
                            break;
                        }
                    if (available)
                        break;
                }
                if (slot + units > mir.backend_slot_count) {
                    int unit;
                    slot = mir.backend_slot_count;
                    for (unit = 0; unit < units; ++unit)
                        mir_backend_slot_offsets[slot + unit] =
                            -mir_effective_local_bytes() -
                            mir.aggregate_temp_bytes -
                            2 * (mir_backend_frame_slot_count + unit + 1);
                    mir.backend_slot_count += units;
                    mir_backend_frame_slot_count += units;
                    mir_backend_slot_pool_count = mir.backend_slot_count;
                }
                mir.backend_slots[value] = slot;
                if (slot < mir_backend_local_slot_count)
                    mir_spilled_cfg_used_promoted_local_slot = 1;
                {
                    int unit;
                    for (unit = 0; unit < units; ++unit)
                        slot_end[slot + unit] = last[value];
                }
            }
    }
    mir_verify_planned_stack_handoffs();
    if (getenv("DCC_MIR_WIDE_HELPER_REPORT") != NULL)
        for (value = 0; value < mir.next_value; ++value) {
            const struct MirInsn *definition = mir_definition(value);
            int definition_index;
            int consumer;
            const char *helper;

            if (definition == NULL)
                continue;
            definition_index = (int)(definition - mir.insns);
            if (!mir_wide_helper_lhs_consumer(
                    value, definition_index, &consumer))
                continue;
            helper = mir_wide_runtime_helper(&mir.insns[consumer]);
            fprintf(stderr,
                    "; MIR wide-helper function=%s value=%d producer=%s "
                    "consumer=%d helper=%s distance=%d safe=%d "
                    "handoff=%d slot=%d slots=%d\n",
                    mir.name, value, mir_opcode_name(definition->opcode),
                    consumer, helper != NULL ? helper : "none",
                    consumer - definition_index,
                    mir_wide_helper_lhs_span_is_safe(
                        definition_index, consumer),
                    mir_wide_helper_handoff_supported(
                        &mir.insns[consumer]),
                    mir.backend_slots[value], mir.backend_slot_count);
        }
    if (getenv("DCC_MIR_BACKEND_SLOT_REPORT") != NULL)
        for (value = 0; value < mir.next_value; ++value)
            if (mir.backend_slots[value] >= mir_backend_local_slot_count) {
                const struct MirInsn *definition = mir_definition(value);

                fprintf(stderr,
                        "; MIR backend-slot function=%s value=%d opcode=%s "
                        "type=%d first=%d last=%d slot=%d uses=%d\n",
                        mir.name, value,
                        definition != NULL
                            ? mir_opcode_name(definition->opcode) : "none",
                        definition != NULL ? definition->type : 0,
                        first[value], last[value], mir.backend_slots[value],
                        mir_value_use_count(value));
            }
    free(fused_away);
    free(stack_interval_occupied);
    free(slot_end);
    free(last);
    free(first);
    if (getenv("DCC_MIR_SLOT_REPORT") != NULL &&
        mir_slot_report_requested_count > 0)
        fprintf(stderr,
                "; MIR slot-report function=%s requested=%d assigned=%d "
                "param_assigned=%d\n",
                mir.name, mir_slot_report_requested_count,
                mir_slot_report_assigned_count,
                mir_slot_report_param_assigned_count);
    return mir_backend_frame_slot_count;
}

void mir_emit_virtual_load(FILE *out, int value)
{
    const struct MirInsn *definition = mir_definition(value);
    int offset;
    int iy_offset;
    if (mir_forwarded_hl_value == value &&
        mir_forwarded_hl_instruction + 1 == mir_emit_instruction_index) {
        /* This check must precede the param-direct branch below: the
         * register-forwarding optimization elides the reload entirely
         * when the value is already resident in HL from the immediately
         * preceding instruction, regardless of whether this value is
         * slot-based or param-direct. Skipping it here regressed
         * tvla's vla_sizeof_c99_type_bytes (a param read many times in
         * a loop) by re-emitting a full ix-relative reload on every use
         * instead of reusing HL when available. */
        mir_forwarded_hl_value = -1;
        mir_forwarded_hl_instruction = -1;
        return;
    }
    if (mir_scalar_constant_is_rematerializable(value)) {
        fprintf(out, "\tld hl,%ld\n", definition->immediate & 0xffffL);
        return;
    }
    if (mir_string_address_is_rematerializable(value)) {
        fprintf(out, "\tld hl,S%ld\n", definition->immediate);
        return;
    }
    if (mir_address_is_rematerializable(value)) {
        if (!mir_emit_address_to_hl(out, definition))
            fatal("cannot rematerialize MIR address");
        return;
    }
    if (mir_value_has_direct_named_home(value)) {
        /* Read straight from the value's stable named IX-relative home instead
         * of a duplicated backend slot. Mirrors exactly the
         * in-range/out-of-range forms the original MIR_PARAM binding site
         * uses for a narrow scalar, including the IY-relative fast path
         * hot loops rely on (skipping it caused a measurable, if tiny,
         * cycle regression in tsnprtf's call_vsnprintf). */
        const struct MirInsn *definition = mir_definition(value);
        int value_type = definition->type;
        int value_size = type_size(value_type);
        int object_offset;
        int object_storage;
        int object_iy_offset;
        if (!mir_direct_named_home_location(
                definition, &object_storage, &object_offset))
            fatal("missing direct MIR named-home offset");
        object_iy_offset = object_offset + mir_effective_local_bytes() +
                           mir.aggregate_temp_bytes;
        if (mir_virtual_iy_base && object_iy_offset >= -128 &&
            object_iy_offset + value_size - 1 <= 127) {
            fprintf(out, "\tld l,(iy%+d)\n", object_iy_offset);
            if (value_size == 2)
                fprintf(out, "\tld h,(iy%+d)\n", object_iy_offset + 1);
        } else if (object_offset >= -128 &&
                   object_offset + value_size - 1 <= 127) {
            fprintf(out, "\tld l,(ix%+d)\n", object_offset);
            if (value_size == 2)
                fprintf(out, "\tld h,(ix%+d)\n", object_offset + 1);
        } else {
            fputs("\tpush ix\n\tpop hl\n", out);
            fprintf(out, "\tld de,%d\n\tadd hl,de\n\tld a,(hl)\n",
                    object_offset);
            if (value_size == 2)
                fputs("\tinc hl\n\tld h,(hl)\n\tld l,a\n", out);
            else
                fputs("\tld l,a\n", out);
        }
        if (value_size == 1) {
            if (type_is_bool(value_type)) {
                int bool_label = new_label();
                fputs("\tld a,l\n\tor a\n\tld hl,0\n", out);
                fprintf(out, "\tjp z, L%d\n\tinc hl\nL%d:\n",
                        bool_label, bool_label);
            } else if ((value_type & TYPE_UNSIGNED) != 0) {
                fputs("\tld h,0\n", out);
            } else {
                mir_emit_signed_byte_extend(out);
            }
        }
        return;
    }
    offset = mir_virtual_offset(value);
    iy_offset = mir_virtual_iy_offset(value);
    if (mir_virtual_iy_base && iy_offset >= -128 && iy_offset + 1 <= 127) {
        fprintf(out, "\tld l,(iy%+d)\n\tld h,(iy%+d)\n",
                iy_offset, iy_offset + 1);
        return;
    }
    if (offset >= -128 && offset + 1 <= 127) {
        fprintf(out, "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
                offset, offset + 1);
    } else {
        fputs("\tpush ix\n\tpop hl\n", out);
        fprintf(out, "\tld de,%d\n\tadd hl,de\n"
                     "\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n",
                offset);
    }
}

/* Item T15 (mir-text-size-plan.md): a value's definition immediately
 * followed by a MIR_CONST that itself feeds the very next MIR_BINARY as
 * that binary's right-hand operand (value; const; binary(value, const))
 * currently loses mir_can_forward_hl_to_next's forwarding entirely,
 * because the CONST sits between the value's definition and its one real
 * use - mir_can_forward_hl_to_next only recognizes a handful of consumer
 * opcodes as the literal next instruction, and MIR_CONST isn't one of
 * them, so the value is unconditionally stored to its backend slot and
 * immediately reloaded one "logical" instruction later even though
 * nothing between the store and reload could have clobbered it (a
 * MIR_CONST used only as an immediate operand emits no code of its own
 * at that point - see mir_binary_only_constant's callers below). This
 * mirrors mir_can_forward_stack_to_index's existing const-in-the-middle
 * shape (push the value, let the consumer pop it back after evaluating
 * the constant) but for a binary operator's constant right-hand operand
 * instead of an array index's constant stride.
 *
 * Deliberately excludes any shape that takes a different code path for
 * the constant: divmod pairing and the multiply-by-constant fast path
 * bypass the plain "push left, evaluate right, pop, combine" sequence
 * this optimization targets, so forwarding across them is out of scope
 * here (a separate item, not folded in).
 *
 * Item T17 (mir-text-size-plan.md): originally also excluded fused-
 * comparison branches, on the assumption they took a different code
 * path entirely - investigation showed mir_emit_fused_comparison_branch
 * only consumes HL/DE however they got there (the src1/src2-loading
 * code above, including this forwarding mechanism, is shared with the
 * fused-comparison case; only the final action after loading - branch
 * directly vs. materialize+store - differs). The exclusion is removed:
 * a fusable comparison's left operand can be forwarded exactly the same
 * as any other binary operator's. */
static int mir_can_forward_stack_to_binary_const(int value)
{
    const struct MirInsn *middle;
    const struct MirInsn *binary;
    int binary_instruction;
    int instruction;

    if (mir_emit_instruction_index < 0 ||
        mir_emit_instruction_index + 2 >= mir.count)
        return 0;
    middle = &mir.insns[mir_emit_instruction_index + 1];
    binary_instruction = mir_emit_instruction_index + 2;
    binary = &mir.insns[binary_instruction];
    if (middle->opcode != MIR_CONST || binary->opcode != MIR_BINARY ||
        binary->src1 != value || binary->src2 != middle->dst)
        return 0;
    if (type_size(binary->secondary_offset) == 4)
        return 0;
    if (mir_divmod_partner(binary_instruction) >= 0)
        return 0;
    if (binary->immediate == '*' &&
        mir_mul_const_fast_path_eligible(
            (unsigned long)middle->immediate & 0xffffUL, binary->dst))
        return 0;
    /* Item T17 (mir-text-size-plan.md): a fusable comparison whose
     * constant right-hand operand is exactly 0 takes the Item 25/27
     * shortcut, which skips materializing DE (and, critically, skips
     * the pop that would otherwise retrieve this forwarded value) -
     * forwarding here would leave an unbalanced push on the stack.
     * Every other fusable comparison (non-zero-RHS) goes through the
     * ordinary const-materialize-to-DE path below, which does perform
     * the pop, so only this specific pair of shapes needs excluding. */
    if (mir_binary_is_fusable_comparison(binary_instruction) > 0 &&
        (mir_fused_compare_is_const_zero_rhs(binary_instruction) ||
         mir_fused_compare_is_signed_zero_sign_test(binary_instruction)))
        return 0;
    for (instruction = binary_instruction + 1;
         instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->src1 == value || insn->src2 == value)
            return 0;
        if ((insn->opcode == MIR_CALL ||
             insn->opcode == MIR_CALL_AGGREGATE) &&
            mir_call_uses_value(insn, value))
            return 0;
    }
    return 1;
}

static int mir_can_forward_stack_to_binary_lhs(int value)
{
    const struct MirInsn *middle;
    const struct MirInsn *binary;
    int binary_instruction;

    if (!mir_general_rhs_stack_forwarding_enabled ||
        mir_emit_instruction_index < 0 ||
        mir_emit_instruction_index + 2 >= mir.count ||
        mir_value_use_count(value) != 1)
        return 0;
    middle = &mir.insns[mir_emit_instruction_index + 1];
    binary_instruction = mir_emit_instruction_index + 2;
    binary = &mir.insns[binary_instruction];
    if ((middle->opcode != MIR_LOAD && middle->opcode != MIR_PARAM) ||
        type_size(middle->type) > 2 ||
        binary->opcode != MIR_BINARY || binary->src1 != value ||
        binary->src2 != middle->dst ||
        type_size(binary->secondary_offset) == 4 ||
        mir_value_use_count(middle->dst) != 1 ||
        mir_divmod_partner(binary_instruction) >= 0)
        return 0;
    mir_spilled_cfg_used_binary_load_pair_forwarding = 1;
    return 1;
}

/* Item T16 (mir-text-size-plan.md): mir_can_forward_hl_to_next's
 * MIR_BINARY case only ever matches `value` against the binary's src1
 * (left operand) - a value that is instead the very next MIR_BINARY's
* *right*-hand operand (src2) gets no forwarding at all. Push the value
* immediately after computing it and let the binary's emission pop it
* into DE after loading the left operand.
*
* T203 generalizes the original constant-left implementation to any
* single-use narrow right operand. Loading src1 is stack-balanced, so it
* cannot disturb the pending right value. If src1 is itself a planned
* stack handoff, the right value is newer and is popped first, followed by
* the planned left value. The fix is symmetric to
* mir_can_forward_stack_to_index/_binary_const above. It remains
* restricted to the immediately-following instruction only (no intervening
* MIR_CONST skip - that shape is a distinct, not yet needed case) and
* excludes divmod pairing, which uses a different code path entirely.
 *
 * Item T17 (mir-text-size-plan.md): like
 * mir_can_forward_stack_to_binary_const above, this originally also
 * excluded fused-comparison branches - removed for the same reason:
 * mir_emit_fused_comparison_branch only consumes whatever HL/DE already
 * hold, regardless of how they got there. */
static int mir_can_forward_stack_to_binary_rhs(int value)
{
    const struct MirInsn *binary;
    int binary_instruction;
    int instruction;

    if (mir_emit_instruction_index < 0 ||
        mir_emit_instruction_index + 1 >= mir.count)
        return 0;
    binary_instruction = mir_emit_instruction_index + 1;
    binary = &mir.insns[binary_instruction];
    if (binary->opcode != MIR_BINARY || binary->src2 != value)
        return 0;
    if (type_size(binary->secondary_offset) == 4)
        return 0;
    if (mir_value_use_count(value) != 1)
        return 0;
    if (!mir_binary_only_constant(binary->src1) &&
        !mir_general_rhs_stack_forwarding_enabled)
        return 0;
    if (mir_divmod_partner(binary_instruction) >= 0)
        return 0;
    /* Defensive: mirrors the same Item T17 zero-RHS guard as
     * mir_can_forward_stack_to_binary_const above. src2 (the forwarded
     * value here) is not expected to itself be a plain constant in
     * practice, but if it ever were, the Item 25/27 shortcuts would
     * skip the pop this predicate promises the caller will perform. */
    if (mir_binary_is_fusable_comparison(binary_instruction) > 0 &&
        (mir_fused_compare_is_const_zero_rhs(binary_instruction) ||
         mir_fused_compare_is_signed_zero_sign_test(binary_instruction)))
        return 0;
    for (instruction = binary_instruction + 1;
         instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->src1 == value || insn->src2 == value)
            return 0;
        if ((insn->opcode == MIR_CALL ||
             insn->opcode == MIR_CALL_AGGREGATE) &&
            mir_call_uses_value(insn, value))
            return 0;
    }
    return 1;
}

static int mir_can_forward_stack_to_indirect_store_value(int value)
{
    const struct MirInsn *store;

    if (!mir_indirect_store_value_forwarding_enabled ||
        mir_emit_instruction_index < 0 ||
        mir_emit_instruction_index + 1 >= mir.count ||
        mir_value_use_count(value) != 1)
        return 0;
    store = &mir.insns[mir_emit_instruction_index + 1];
    return store->opcode == MIR_STORE_INDIRECT &&
           store->src2 == value && store->bit_width == 0 &&
           store->memory_size > 0 && store->memory_size <= 2 &&
           !mir_constant_absolute_access_supported(store);
}

static int mir_stack_forward_target(int value, int *dynamic_index)
{
    int target;

    if (dynamic_index != NULL)
        *dynamic_index = 0;
    if (mir_emit_instruction_index >= 0 &&
        mir_emit_instruction_index < mir.count &&
        mir.insns[mir_emit_instruction_index].opcode == MIR_PHI)
        return -1;
    target = mir_dynamic_index_base_forward_target(value);
    if (target >= 0) {
        if (dynamic_index != NULL)
            *dynamic_index = 1;
        return target;
    }
    target = mir_stack_index_forward_target(value);
    if (target >= 0)
        return target;
    if (mir_can_forward_stack_to_indirect_store_value(value))
        return mir_emit_instruction_index + 1;
    if (mir_can_forward_stack_to_binary_lhs(value))
        return mir_emit_instruction_index + 2;
    if (mir_can_forward_stack_to_binary_const(value))
        return mir_emit_instruction_index + 2;
    if (mir_can_forward_stack_to_binary_rhs(value))
        return mir_emit_instruction_index + 1;
    return -1;
}

static int mir_stack_backend_slot_forwardable(
    int value, int units, int instruction)
{
    int saved_index;
    int forwardable;

    if (units != 1)
        return 0;
    saved_index = mir_emit_instruction_index;
    mir_emit_instruction_index = instruction;
    forwardable = mir_stack_forward_target(value, NULL) >= 0;
    mir_emit_instruction_index = saved_index;
    return forwardable;
}

static int mir_planned_stack_target(int value)
{
    if (value < 0 || value >= mir.next_value ||
        mir.planned_stack_consumers == NULL)
        return -1;
    return mir.planned_stack_consumers[value];
}

static int mir_planned_stack_matches_consumer(int value, int instruction)
{
    return mir_planned_stack_target(value) == instruction &&
           instruction >= 0 && instruction < mir.count &&
           mir.planned_stack_values != NULL &&
           mir.planned_stack_values[instruction] == value;
}

static int mir_planned_stack_is_emitted(int value)
{
    return value >= 0 && value < mir.next_value &&
           mir.planned_stack_emitted != NULL &&
           mir.planned_stack_emitted[value] != 0;
}

static int mir_pending_planned_stack_consumer(void)
{
    int value;

    for (value = 0; value < mir.next_value; ++value)
        if (mir_planned_stack_is_emitted(value))
            return mir_planned_stack_target(value);
    return -1;
}

static int mir_stack_forward_nests_with_planned_left(
    int value, int consumer)
{
    const struct MirInsn *insn;

    if (consumer < 0 || consumer >= mir.count)
        return 0;
    insn = &mir.insns[consumer];
    if (!mir_planned_stack_matches_consumer(insn->src1, consumer))
        return 0;
    return mir_is_general_rhs_stack_forward(value, consumer) ||
           mir_is_indirect_store_value_stack_forward(value, consumer);
}

static int mir_consume_planned_stack(FILE *out, int value, int instruction,
                                     const char *destination)
{
    if (!mir_planned_stack_matches_consumer(value, instruction) ||
        !mir_planned_stack_is_emitted(value))
        return 0;
    fprintf(out, "\tpop %s\n", destination);
    mir.planned_stack_emitted[value] = 0;
    ++mir_planned_stack_consume_count;
    return 1;
}

static void mir_emit_virtual_store(FILE *out, int value)
{
    int dynamic_index_forward;
    int forward_to_store;
    int has_slot;
    int forward_instruction;
    int offset;
    int iy_offset;
    int pending_planned_consumer;
    if (mir_value_has_direct_named_home(value))
        /* Nothing to store: later uses re-read the stable named home (see
         * mir_emit_virtual_load); the loaded HL is simply not persisted. */
        return;
    if (value == mir_prepacked_result_value) {
        fputs("\tpush hl\n", out);
        return;
    }
    if (value >= 0 && value < mir.next_value &&
        mir.backend_slots != NULL &&
        (mir.backend_slots[value] == MIR_BACKEND_SLOT_CALL_CACHE ||
         mir.backend_slots[value] ==
             MIR_BACKEND_SLOT_NARROW_ARGUMENT_DIRECT_PUSH)) {
        int call_instruction =
            mir_planned_call_argument_cache_target(value, 0);
        fputs("\tld c,l\n\tld b,h\n", out);
        mir_cached_call_value = value;
        mir_cached_call_instruction = call_instruction;
        return;
    }
    forward_instruction = mir_planned_stack_target(value);
    if (forward_instruction >= 0) {
        if (mir_forwarded_stack_value >= 0 &&
            mir_forwarded_stack_target_instruction <
                mir_emit_instruction_index) {
            mir_forwarded_stack_value = -1;
            mir_forwarded_stack_instruction = -1;
            mir_forwarded_stack_target_instruction = -1;
        }
        if (mir_forwarded_stack_value >= 0) {
            if (getenv("DCC_MIR_SELECT_REPORT") != NULL)
                fprintf(stderr,
                        "; MIR planned-stack overlap function=%s "
                        "existing-value=%d existing-consumer=%d "
                        "value=%d producer=%d consumer=%d\n",
                        mir.name, mir_forwarded_stack_value,
                        mir_forwarded_stack_target_instruction,
                        value, mir_emit_instruction_index,
                        forward_instruction);
            mir_planned_stack_invalid = 1;
        }
        if (mir_forwarded_wide_stack_value >= 0 &&
            forward_instruction >= mir_forwarded_wide_stack_consumer)
            mir_planned_stack_invalid = 1;
        fputs("\tpush hl\n", out);
        mir_spilled_cfg_used_planned_stack_handoff = 1;
        if (mir.insns[forward_instruction].opcode == MIR_INDEX_ADDRESS)
            mir_spilled_cfg_used_planned_index_base_handoff = 1;
        mir.planned_stack_emitted[value] = 1;
        ++mir_planned_stack_emit_count;
        return;
    }
    has_slot = value >= 0 && value < mir.next_value &&
                   mir.backend_slots != NULL && mir.backend_slots[value] >= 0;
    forward_instruction = mir_forward_skip_target(mir_emit_instruction_index);
    if (!has_slot) {
        /* Item 13 (mir-migration-plan-100): mir_prepare_backend_slots skips
         * allocating a slot for a value it already proved via
         * mir_backend_slot_forwardable() can only ever be consumed through
         * this same HL-forwarding path (never stored to memory) - still set
         * up the forwarding handoff so the immediately-following use skips
         * its own reload; anything else with no slot is genuinely dead. */
        if (mir_can_forward_hl_to_next(value)) {
            mir_forwarded_hl_value = value;
            mir_forwarded_hl_instruction = forward_instruction - 1;
            if (mir_is_branch_condition_forward(value, forward_instruction))
                ++mir_spilled_cfg_branch_condition_forwarding_count;
        } else if (mir_can_forward_hl_to_call_argument(value)) {
            /* Item T59 (mir-text-size-plan.md): mir_prepare_backend_slots'
             * reservation pass now also skips allocating a slot for values
             * mir_can_forward_hl_to_call_argument alone proves (the
             * has_slot branch below already relied on this same predicate
             * to avoid ever *writing* to an already-reserved slot; this
             * mirrors that for the no-slot case). Without this branch the
             * value has has_slot == 0, mir_can_forward_hl_to_next doesn't
             * recognize an ARG+CALL consumer, no forwarding handoff is
             * armed at all, and the later load falls through to reading
             * mir.backend_slots[value] - an address that was never
             * reserved for this value. Found via tests/tstrcmpi.c's main
             * (nested `sgn(stricmp(...))` calls) once mir_prepare_backend_
             * slots' own skip-list started recognizing this predicate. */
            mir_forwarded_hl_value = value;
            mir_forwarded_hl_instruction =
                mir_call_argument_after_nops(mir_emit_instruction_index);
        } else if ((forward_instruction =
                    mir_stack_forward_target(
                        value, &dynamic_index_forward)) >= 0) {
            pending_planned_consumer =
                mir_pending_planned_stack_consumer();
            if (pending_planned_consumer >= 0 &&
                forward_instruction >= pending_planned_consumer &&
                !mir_stack_forward_nests_with_planned_left(
                    value, forward_instruction))
                mir_planned_stack_invalid = 1;
            fputs("\tpush hl\n", out);
            mir_forwarded_stack_value = value;
            mir_forwarded_stack_instruction = mir_emit_instruction_index;
            mir_forwarded_stack_target_instruction = forward_instruction;
            if (mir_is_general_rhs_stack_forward(value, forward_instruction))
                mir_spilled_cfg_used_general_rhs_stack_forwarding = 1;
            if (mir_is_indirect_store_value_stack_forward(
                    value, forward_instruction))
                ++mir_spilled_cfg_indirect_store_value_forwarding_count;
            if (dynamic_index_forward)
                mir_spilled_cfg_used_dynamic_index_base_forwarding = 1;
        }
        return;
    }
    forward_to_store = mir_can_forward_hl_to_next(value) &&
        forward_instruction < mir.count &&
        mir.insns[forward_instruction].opcode == MIR_STORE;
    if (!forward_to_store && mir_can_forward_hl_to_next(value)) {
        mir_forwarded_hl_value = value;
        mir_forwarded_hl_instruction = forward_instruction - 1;
        if (mir_is_branch_condition_forward(value, forward_instruction))
            ++mir_spilled_cfg_branch_condition_forwarding_count;
        return;
    }
    if (mir_can_forward_hl_to_call_argument(value)) {
        mir_forwarded_hl_value = value;
        mir_forwarded_hl_instruction =
            mir_call_argument_after_nops(mir_emit_instruction_index);
        return;
    }
    if ((forward_instruction = mir_stack_forward_target(
             value, &dynamic_index_forward)) >= 0) {
        pending_planned_consumer = mir_pending_planned_stack_consumer();
        if (pending_planned_consumer >= 0 &&
            forward_instruction >= pending_planned_consumer &&
            !mir_stack_forward_nests_with_planned_left(
                value, forward_instruction))
            mir_planned_stack_invalid = 1;
        fputs("\tpush hl\n", out);
        mir_forwarded_stack_value = value;
        mir_forwarded_stack_instruction = mir_emit_instruction_index;
        mir_forwarded_stack_target_instruction = forward_instruction;
        if (mir_is_general_rhs_stack_forward(value, forward_instruction))
            mir_spilled_cfg_used_general_rhs_stack_forwarding = 1;
        if (mir_is_indirect_store_value_stack_forward(
                value, forward_instruction))
            ++mir_spilled_cfg_indirect_store_value_forwarding_count;
        if (dynamic_index_forward)
            mir_spilled_cfg_used_dynamic_index_base_forwarding = 1;
        return;
    }
    {
        int call_instruction = mir_call_argument_cache_target(value);
        if (call_instruction >= 0) {
            fputs("\tld c,l\n\tld b,h\n", out);
            mir_cached_call_value = value;
            mir_cached_call_instruction = call_instruction;
            return;
        }
    }
    offset = mir_virtual_offset(value);
    iy_offset = mir_virtual_iy_offset(value);
    mir_forwarded_hl_value = -1;
    mir_forwarded_hl_instruction = -1;
    {
    int forward_to_narrow_store = forward_to_store &&
        mir_forward_store_target_is_narrow(forward_instruction);
    if (mir_virtual_iy_base && iy_offset >= -128 && iy_offset + 1 <= 127) {
        fprintf(out, "\tld (iy%+d),l\n", iy_offset);
        if (!forward_to_narrow_store)
            fprintf(out, "\tld (iy%+d),h\n", iy_offset + 1);
        if (forward_to_store) {
            mir_forwarded_hl_value = value;
            mir_forwarded_hl_instruction = forward_instruction - 1;
        } else if (mir_can_forward_hl_to_call_argument_first_use(value)) {
            mir_forwarded_hl_value = value;
            mir_forwarded_hl_instruction =
                mir_call_argument_after_nops(mir_emit_instruction_index);
        }
        return;
    }
    if (offset >= -128 && offset + 1 <= 127) {
        fprintf(out, "\tld (ix%+d),l\n", offset);
        if (!forward_to_narrow_store)
            fprintf(out, "\tld (ix%+d),h\n", offset + 1);
        if (forward_to_store) {
            mir_forwarded_hl_value = value;
            mir_forwarded_hl_instruction = forward_instruction - 1;
        } else if (mir_can_forward_hl_to_call_argument_first_use(value)) {
            /* Item T82 (mir-text-size-plan.md): safe only for this
             * in-range form - the out-of-range branch below moves the
             * value out of HL (into DE via `ex de,hl`) to compute the
             * store address, so HL no longer holds it afterward. */
            mir_forwarded_hl_value = value;
            mir_forwarded_hl_instruction =
                mir_call_argument_after_nops(mir_emit_instruction_index);
        }
    } else {
        fputs("\tex de,hl\n\tpush ix\n\tpop hl\n", out);
        fprintf(out, "\tld bc,%d\n\tadd hl,bc\n"
                     "\tld (hl),e\n\tinc hl\n\tld (hl),d\n",
                offset);
        if (forward_to_store) {
            mir_forwarded_hl_value = value;
            mir_forwarded_hl_instruction = forward_instruction - 1;
        }
    }
    }
}

int mir_value_is_wide(int value)
{
    return mir_definition_is_wide(mir_definition(value));
}

static int mir_wide_constant_uses_new_rematerialization(int value)
{
    const struct MirInsn *definition = mir_definition(value);
    int instruction = definition != NULL
        ? (int)(definition - mir.insns) : -1;

    return mir_wide_constant_is_rematerializable(value) &&
           !mir_call_only_constant(value) &&
           !mir_binary_only_constant(value) &&
           !mir_index_only_constant(value) &&
           !mir_backend_slot_forwardable(value, 2, instruction) &&
           !mir_wide_backend_slot_forwardable(value, 2, instruction) &&
           !mir_wide_helper_lhs_slot_forwardable(value, 2, instruction) &&
           !mir_call_argument_slot_forwardable(value, 2, instruction) &&
           !mir_stack_backend_slot_forwardable(value, 2, instruction) &&
           !mir_value_only_used_by_dead_stores(value) &&
           !mir_value_only_used_by_dead_unary(value);
}

static int mir_divmod_partner(int instruction)
{
    const struct MirInsn *candidate;
    int partner;

    if (instruction < 0 || instruction >= mir.count)
        return -1;
    candidate = &mir.insns[instruction];
    if (candidate->opcode != MIR_BINARY ||
        (candidate->immediate != '/' && candidate->immediate != '%') ||
        type_size(candidate->secondary_offset) > 2)
        return -1;
    for (partner = 0; partner < mir.count; ++partner) {
        const struct MirInsn *other = &mir.insns[partner];
        if (partner == instruction || other->opcode != MIR_BINARY ||
            other->src1 != candidate->src1 || other->src2 != candidate->src2 ||
            other->secondary_offset != candidate->secondary_offset)
            continue;
        if ((candidate->immediate == '/' && other->immediate == '%') ||
            (candidate->immediate == '%' && other->immediate == '/'))
            return partner;
    }
    return -1;
}

static void mir_emit_virtual_load_wide(FILE *out, int value)
{
    const struct MirInsn *definition = mir_definition(value);
    int offset;
    int iy_offset;

    /* Item T40 (mir-text-size-plan.md): consume a value mir_emit_virtual_
     * store_wide already forwarded straight from its producing
     * instruction, exactly mirroring mir_emit_virtual_load's own
     * mir_forwarded_hl_value check - the value is already resident in
     * HL:DE from the immediately preceding instruction, so no reload is
     * needed at all. Must precede every other case below (matching the
     * scalar version's own ordering) since this applies regardless of
     * whether the value is slot-based, param-direct, or anything else. */
    if (mir_forwarded_wide_value == value &&
        mir_forwarded_wide_instruction + 1 == mir_emit_instruction_index) {
        mir_forwarded_wide_value = -1;
        mir_forwarded_wide_instruction = -1;
        return;
    }
    if (mir_wide_constant_is_rematerializable(value)) {
        if (mir_wide_constant_uses_new_rematerialization(value)) {
            mir_spilled_cfg_used_wide_constant_rematerialization = 1;
        }
        fprintf(out, "\tld hl,%lu\n\tld de,%lu\n",
                (unsigned long)definition->immediate & 0xffffUL,
                ((unsigned long)definition->immediate >> 16) & 0xffffUL);
        return;
    }
    if (!mir_definition_is_wide(definition)) {
        mir_emit_virtual_load(out, value);
        if (definition != NULL &&
            ((definition->type & TYPE_UNSIGNED) != 0 ||
             type_ptr_depth(definition->type) > 0 ||
             type_is_bool(definition->type)))
            fputs("\tld de,0\n", out);
        else
            fputs("\tld a,h\n\trlca\n\tsbc a,a\n\tld d,a\n\tld e,a\n",
                  out);
        return;
    }
    if (mir_value_has_direct_named_home(value)) {
        /* mir-migration-plan-next10: wide (4-byte) parameter counterpart
         * of mir_emit_virtual_load - object storage for a wide parameter is
         * ascending (offset..offset+3), unlike the descending backend-slot
         * convention below, so this mirrors the original MIR_PARAM binding
         * site's load form exactly rather than reusing mir_virtual_offset.
         * Also mirrors the IY-relative fast path hot loops rely on. */
        int object_offset = mir.objects[definition->object].offset;
        int object_iy_offset = object_offset + mir_effective_local_bytes() +
                                mir.aggregate_temp_bytes;
        if (mir_virtual_iy_base && object_iy_offset >= -128 &&
            object_iy_offset + 3 <= 127) {
            fprintf(out,
                    "\tld l,(iy%+d)\n\tld h,(iy%+d)\n"
                    "\tld e,(iy%+d)\n\tld d,(iy%+d)\n",
                    object_iy_offset, object_iy_offset + 1,
                    object_iy_offset + 2, object_iy_offset + 3);
        } else if (object_offset >= -128 && object_offset + 3 <= 127) {
            fprintf(out,
                    "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
                    "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
                    object_offset, object_offset + 1,
                    object_offset + 2, object_offset + 3);
        } else {
            fputs("\tpush ix\n\tpop hl\n", out);
            fprintf(out, "\tld de,%d\n\tadd hl,de\n", object_offset);
            fputs("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
                  "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
                  "\tld h,b\n\tld l,c\n", out);
        }
        return;
    }
    offset = mir_virtual_offset(value);
    iy_offset = mir_virtual_iy_offset(value);
    if (mir_virtual_iy_base && iy_offset - 2 >= -128 &&
        iy_offset + 1 <= 127) {
        fprintf(out,
                "\tld l,(iy%+d)\n\tld h,(iy%+d)\n"
                "\tld e,(iy%+d)\n\tld d,(iy%+d)\n",
                iy_offset, iy_offset + 1, iy_offset - 2, iy_offset - 1);
        return;
    }
    if (offset - 2 >= -128 && offset + 1 <= 127) {
        fprintf(out,
                "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
                "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
                offset, offset + 1, offset - 2, offset - 1);
    } else {
        fputs("\tpush ix\n\tpop hl\n", out);
        fprintf(out, "\tld de,%d\n\tadd hl,de\n", offset);
        fputs("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
              "\tdec hl\n\tdec hl\n\tdec hl\n"
              "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
              "\tld h,b\n\tld l,c\n", out);
    }
}

static void mir_emit_virtual_store_wide(FILE *out, int value)
{
    int has_slot;
    int helper_consumer;
    int offset;
    int iy_offset;
    /* Item T35 (mir-text-size-plan.md): mirrors mir_emit_virtual_store's
     * own first-line check, now that Item T35's mir_object_eligible
     * relaxation lets a wide (4-byte) parameter actually have an object
     * at all - nothing to store here, later reads simply re-load
     * directly from the parameter's own stable home (see
     * mir_emit_virtual_load_wide, which already checks this same
     * predicate on the load side). Without this check a direct-eligible
     * wide parameter would still pay a full spill even though nothing
     * downstream would ever read the slot this store writes. */
    if (mir_value_has_direct_named_home(value))
        return;
    if (value == mir_prepacked_result_value) {
        fputs("\tpush de\n\tpush hl\n", out);
        return;
    }
    if (value >= 0 && value < mir.next_value &&
        mir.backend_slots != NULL &&
        (mir.backend_slots[value] == MIR_BACKEND_SLOT_CALL_CACHE ||
         mir.backend_slots[value] ==
             MIR_BACKEND_SLOT_WIDE_ARGUMENT_STACK_CACHE)) {
        int call_instruction =
            mir_planned_call_argument_cache_target(value, 1);
        if (mir.backend_slots[value] ==
            MIR_BACKEND_SLOT_WIDE_ARGUMENT_STACK_CACHE)
            fputs("\tpush de\n\tpush hl\n", out);
        else
            fputs("\texx\n", out);
        mir_cached_wide_call_value = value;
        mir_cached_wide_call_instruction = call_instruction;
        return;
    }
    if (mir_wide_helper_lhs_consumer(
            value, mir_emit_instruction_index, &helper_consumer) &&
        mir_wide_helper_handoff_supported(&mir.insns[helper_consumer]) &&
        mir_wide_helper_lhs_span_is_safe(
            mir_emit_instruction_index, helper_consumer)) {
        int pending_consumer = mir_pending_planned_stack_consumer();
        if (pending_consumer >= 0 && helper_consumer >= pending_consumer)
            mir_planned_stack_invalid = 1;
        fputs("\tpush de\n\tpush hl\n", out);
        mir_forwarded_wide_stack_value = value;
        mir_forwarded_wide_stack_consumer = helper_consumer;
        return;
    }
    if (mir_wide_binary_rhs_forwarding_enabled &&
        mir_emit_instruction_index + 1 < mir.count &&
        mir_wide_binary_rhs_pair_supported(
            value, &mir.insns[mir_emit_instruction_index + 1]) &&
        mir.insns[mir_emit_instruction_index + 1].src2 == value &&
        mir_value_use_count(value) == 1) {
        if (mir_pending_planned_stack_consumer() >= 0 ||
            mir_forwarded_wide_stack_value >= 0) {
            mir_planned_stack_invalid = 1;
            return;
        }
        fputs("\tpush de\n\tpush hl\n", out);
        mir_forwarded_wide_stack_value = value;
        mir_forwarded_wide_stack_consumer = mir_emit_instruction_index + 1;
        ++mir_wide_binary_rhs_forwarding_uses;
        return;
    }
    /* Item T40 (mir-text-size-plan.md): forward a computed wide value
     * straight to its sole next use (currently only a MIR_RETURN, see
     * mir_can_forward_hl_de_to_next) instead of always spilling it to a
     * backend slot first - mirrors mir_emit_virtual_store's own
     * forwarding check, just for HL:DE instead of HL alone. The slot
     * mir_prepare_backend_slots already assigned (it has no wide-
     * forwarding awareness yet) is simply left unused in this case,
     * the same acceptable tradeoff Item 13 documented for the scalar
     * path before mir_prepare_backend_slots grew its own forwarding
     * awareness. */
    if (mir_can_forward_hl_de_to_next(value)) {
        int forward_instruction =
            mir_forward_skip_target(mir_emit_instruction_index);
        mir_forwarded_wide_value = value;
        mir_forwarded_wide_instruction = forward_instruction - 1;
        return;
    }
    has_slot = value >= 0 && value < mir.next_value &&
                   mir.backend_slots != NULL && mir.backend_slots[value] >= 0;
    if (!has_slot)
        return;
    int call_instruction = mir_call_argument_cache_target(value);
    if (call_instruction >= 0) {
        fputs("\texx\n", out);
        mir_cached_wide_call_value = value;
        mir_cached_wide_call_instruction = call_instruction;
        return;
    }
    offset = mir_virtual_offset(value);
    iy_offset = mir_virtual_iy_offset(value);
    mir_forwarded_hl_value = -1;
    mir_forwarded_hl_instruction = -1;
    mir_forwarded_wide_value = -1;
    mir_forwarded_wide_instruction = -1;
    if (mir_virtual_iy_base && iy_offset - 2 >= -128 &&
        iy_offset + 1 <= 127) {
        fprintf(out,
                "\tld (iy%+d),l\n\tld (iy%+d),h\n"
                "\tld (iy%+d),e\n\tld (iy%+d),d\n",
                iy_offset, iy_offset + 1, iy_offset - 2, iy_offset - 1);
        return;
    }
    if (offset - 2 >= -128 && offset + 1 <= 127) {
        fprintf(out,
                "\tld (ix%+d),l\n\tld (ix%+d),h\n"
                "\tld (ix%+d),e\n\tld (ix%+d),d\n",
                offset, offset + 1, offset - 2, offset - 1);
    } else {
        fputs("\tpush de\n\tpush hl\n\tpush ix\n\tpop hl\n", out);
        fprintf(out, "\tld de,%d\n\tadd hl,de\n", offset);
        fputs("\tpop bc\n\tld (hl),c\n\tinc hl\n\tld (hl),b\n"
              "\tdec hl\n\tdec hl\n\tdec hl\n"
              "\tpop bc\n\tld (hl),c\n\tinc hl\n\tld (hl),b\n", out);
    }
}

static void mir_emit_virtual_iy_epilogue(FILE *out)
{
    if (!mir_virtual_iy_base) {
        fputs("\tld sp,ix\n\tpop ix\n\tret\n", out);
        return;
    }
    fputs("\texx\n\tpush ix\n\tpop hl\n", out);
    fprintf(out, "\tld de,-%d\n\tadd hl,de\n\tld sp,hl\n",
            mir_virtual_iy_frame_bytes + 2);
    fputs("\tpop iy\n\tld sp,ix\n\tpop ix\n\texx\n\tret\n", out);
}

static void mir_emit_restore_virtual_iy(FILE *out)
{
    if (!mir_virtual_iy_base)
        return;
    fputs("\tpush ix\n\tpop iy\n", out);
    if (mir_effective_local_bytes() + mir.aggregate_temp_bytes != 0)
        fprintf(out, "\tld bc,-%d\n\tadd iy,bc\n",
                mir_effective_local_bytes() + mir.aggregate_temp_bytes);
}

static void mir_emit_frame_word_store(FILE *out, int offset)
{
    if (offset >= -128 && offset + 1 <= 127) {
        fprintf(out, "\tld (ix%+d),l\n\tld (ix%+d),h\n",
                offset, offset + 1);
    } else {
        fputs("\tex de,hl\n\tpush ix\n\tpop hl\n", out);
        fprintf(out, "\tld bc,%d\n\tadd hl,bc\n"
                     "\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tex de,hl\n",
                offset);
    }
}

static void mir_emit_frame_word_load(FILE *out, int offset)
{
    if (offset >= -128 && offset + 1 <= 127) {
        fprintf(out, "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
                offset, offset + 1);
    } else {
        fputs("\tpush ix\n\tpop hl\n", out);
        fprintf(out, "\tld de,%d\n\tadd hl,de\n"
                     "\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n",
                offset);
    }
}

static int mir_emit_scalar_operation(FILE *out, const struct MirInsn *insn)
{
    switch ((int)insn->immediate) {
    case '+': fputs("\tadd hl,de\n", out); return 1;
    case '-': fputs("\tor a\n\tsbc hl,de\n", out); return 1;
    case '&':
        fputs("\tld a,h\n\tand d\n\tld h,a\n\tld a,l\n\tand e\n\tld l,a\n", out);
        return 1;
    case '|':
        fputs("\tld a,h\n\tor d\n\tld h,a\n\tld a,l\n\tor e\n\tld l,a\n", out);
        return 1;
    case '^':
        fputs("\tld a,h\n\txor d\n\tld h,a\n\tld a,l\n\txor e\n\tld l,a\n", out);
        return 1;
    case '*': mir_emit_runtime_call(out, "__mulu"); return 1;
    case '/':
        mir_emit_runtime_call(
            out, (insn->type & TYPE_UNSIGNED) != 0 ? "__divu" : "__divs");
        return 1;
    case '%':
        mir_emit_runtime_call(
            out, (insn->type & TYPE_UNSIGNED) != 0 ? "__modu" : "__mods");
        return 1;
    case TOK_EQ: case TOK_NE: case '<': case '>': case TOK_LE: case TOK_GE:
        {
            const struct MirInsn *left = mir_definition(insn->src1);
            const struct MirInsn *right = mir_definition(insn->src2);
            int is_unsigned =
                (left != NULL &&
                 mir_type_uses_unsigned_comparison(left->type)) ||
                (right != NULL &&
                 mir_type_uses_unsigned_comparison(right->type));
            mir_emit_scalar_compare(out, (int)insn->immediate, is_unsigned);
        }
        return 1;
    case TOK_SHL: case TOK_SHR:
        {
            const struct MirInsn *left = mir_definition(insn->src1);
            mir_emit_scalar_shift(out, (int)insn->immediate,
                                  left != NULL &&
                                  (left->type & TYPE_UNSIGNED) != 0,
                                  insn->src2);
        }
        return 1;
    default:
        return 0;
    }
}

static int mir_emit_spilled_phi_copies(FILE *out, int predecessor,
                                       int successor);
static int mir_phi_copies_are_empty(int predecessor, int successor);

static const char *mir_invert_z80_condition(const char *condition)
{
    if (strcmp(condition, "z") == 0) return "nz";
    if (strcmp(condition, "nz") == 0) return "z";
    if (strcmp(condition, "c") == 0) return "nc";
    if (strcmp(condition, "nc") == 0) return "c";
    return NULL;
}

/* Item T32 (mir-text-size-plan.md): every fused comparison branch below
 * previously always emitted the three-instruction "branch over a jump"
 * shape - `jp <true_condition>,Lfallthrough` / (phi copies) /
 * `jp Ltarget` / `Lfallthrough:` - even when there were no phi copies to
 * run on the false edge. dccpeep's own pass_branch_over_jump
 * (peep_pass_control_flow.c) already collapses exactly this shape into a
 * single `jp <inverse>,Ltarget` whenever the two instructions between the
 * conditional jump and its target label are just an unconditional jump, so
 * the peeped .COM was never actually paying for the extra jump - only the
 * pre-peephole selector-acceptance byte count was. Emitting the already-
 * collapsed form directly here means the census-visible generated_bytes
 * (which decide MIR text-size acceptance) match what dccpeep would have
 * produced anyway, unlocking acceptance for functions that were only over
 * budget by this redundant jump, with zero change to the final peeped
 * binary. The three-instruction shape is still required, unchanged, when
 * there are phi copies pending on the false edge (they must run
 * conditionally, so they cannot be replaced by a single unconditional
 * branch to `target`). */
static int mir_emit_conditional_branch_with_phi_copies(
    FILE *out, const int *labels, const char *true_condition,
    int predecessor, int target, int branch_label)
{
    int fallthrough_label;
    const char *inverse;

    if (mir_phi_copies_are_empty(predecessor, target)) {
        inverse = mir_invert_z80_condition(true_condition);
        if (inverse != NULL) {
            fprintf(out, "\tjp %s, L%d\n", inverse, labels[branch_label]);
            return 1;
        }
    }
    fallthrough_label = new_label();
    fprintf(out, "\tjp %s, L%d\n", true_condition, fallthrough_label);
    if (!mir_emit_spilled_phi_copies(out, predecessor, target))
        return 0;
    fprintf(out, "\tjp L%d\nL%d:\n", labels[branch_label], fallthrough_label);
    return 1;
}

/* Item 1 (mir-migration-plan-100): when a scalar comparison feeds nothing
 * but the MIR_BRANCH_FALSE that immediately follows it, mir_emit_scalar_compare
 * would otherwise materialize an explicit 0/1 boolean into HL, spill it to a
 * backend slot, and reload it just so the branch can re-test it with
 * `ld a,h / or l`. Test the flags directly instead and jump straight to the
 * branch's targets, eliding the materialize/spill/reload entirely.
 *
 * Item 4 extends this through a single intervening logical-not: `!(a OP b)`
 * feeding a branch is exactly the branch on the complementary operator, so
 * `mir_binary_is_fusable_comparison` returns 2 (skip compare, not, branch)
 * instead of 1 (skip compare, branch) and the caller negates the operator. */
static int mir_negate_comparison_operator(int operation)
{
    switch (operation) {
    case TOK_EQ: return TOK_NE;
    case TOK_NE: return TOK_EQ;
    case '<': return TOK_GE;
    case TOK_GE: return '<';
    case '>': return TOK_LE;
    default: return '>'; /* TOK_LE */
    }
}

static int mir_binary_is_fusable_comparison(int i)
{
    const struct MirInsn *insn = &mir.insns[i];
    const struct MirInsn *next;

    if (insn->opcode != MIR_BINARY)
        return 0;
    switch ((int)insn->immediate) {
    case TOK_EQ: case TOK_NE: case '<': case '>': case TOK_LE: case TOK_GE:
        break;
    default:
        return 0;
    }
    if (mir_value_use_count(insn->dst) != 1 || i + 1 >= mir.count)
        return 0;
    next = &mir.insns[i + 1];
    if (next->opcode == MIR_BRANCH_FALSE && next->src1 == insn->dst)
        return 1;
    if (next->opcode == MIR_UNARY && next->immediate == '!' &&
        next->src1 == insn->dst && !mir_value_is_wide(next->src1) &&
        mir_value_use_count(next->dst) == 1 && i + 2 < mir.count &&
        mir.insns[i + 2].opcode == MIR_BRANCH_FALSE &&
        mir.insns[i + 2].src1 == next->dst)
        return 2;
    return 0;
}

static int mir_truth_parameter_comparison(const struct MirInsn *compare,
                                          int *parameter_value)
{
    const struct MirInsn *left;
    const struct MirInsn *right;

    if (compare == NULL || compare->opcode != MIR_BINARY ||
        compare->immediate != TOK_NE ||
        type_size(compare->secondary_offset) > 2 ||
        mir_value_use_count(compare->dst) != 1)
        return 0;
    left = mir_definition(compare->src1);
    right = mir_definition(compare->src2);
    if (left == NULL || left->opcode != MIR_PARAM ||
        type_size(left->type) > 2 ||
        right == NULL || right->opcode != MIR_CONST ||
        (right->immediate & 0xffffL) != 0)
        return 0;
    *parameter_value = compare->src1;
    return 1;
}

static int mir_nested_truth_comparison(int instruction,
                                       int *left_parameter,
                                       int *right_parameter)
{
    const struct MirInsn *outer;
    const struct MirInsn *left;
    const struct MirInsn *right;

    if (instruction < 0 || instruction >= mir.count ||
        mir_has_phi_instruction())
        return 0;
    outer = &mir.insns[instruction];
    if (outer->opcode != MIR_BINARY || outer->immediate != TOK_NE ||
        type_size(outer->secondary_offset) > 2 ||
        mir_binary_is_fusable_comparison(instruction) != 1)
        return 0;
    left = mir_definition(outer->src1);
    right = mir_definition(outer->src2);
    return mir_truth_parameter_comparison(left, left_parameter) &&
           mir_truth_parameter_comparison(right, right_parameter);
}

/* A pair of one-use `param != 0` booleans consumed by `(left != right)` and
 * an immediate branch can compare the original parameter truth values
 * directly.  Do not allocate backend slots or materialize either inner
 * boolean; mir_emit_nested_truth_comparison_branch emits the complete branch
 * at the outer comparison. */
static int mir_value_is_nested_truth_comparison_input(int value)
{
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        int left_parameter;
        int right_parameter;
        const struct MirInsn *outer = &mir.insns[instruction];

        if ((outer->src1 == value || outer->src2 == value) &&
            mir_nested_truth_comparison(instruction, &left_parameter,
                                        &right_parameter))
            return 1;
    }
    return 0;
}

static int mir_float_madd_match(int add_index, int *multiply_index,
                                int *addend_value)
{
    const struct MirInsn *add;
    const struct MirInsn *multiply;
    int candidate;
    int addend;

    if (add_index <= 0 || add_index >= mir.count)
        return 0;
    add = &mir.insns[add_index];
    if (add->opcode != MIR_BINARY || add->immediate != '+' ||
        !type_is_float(add->type) ||
        !type_is_float(add->secondary_offset) ||
        type_size(add->secondary_offset) != 4)
        return 0;
    candidate = add_index - 1;
    multiply = &mir.insns[candidate];
    addend = add->src1;
    if (multiply->opcode != MIR_BINARY || multiply->immediate != '*' ||
        !type_is_float(multiply->type) ||
        !type_is_float(multiply->secondary_offset) ||
        type_size(multiply->secondary_offset) != 4 ||
        mir_value_use_count(multiply->dst) != 1 ||
        add->src2 != multiply->dst)
        return 0;
    if (multiply_index != NULL)
        *multiply_index = candidate;
    if (addend_value != NULL)
        *addend_value = addend;
    return 1;
}

static int mir_float_multiply_is_fused(int multiply_index)
{
    int matched_multiply;

    return multiply_index + 1 < mir.count &&
           mir_float_madd_match(multiply_index + 1, &matched_multiply, NULL) &&
           matched_multiply == multiply_index;
}

static int mir_unary_is_fusable_not_branch(int i)
{
    const struct MirInsn *candidate;
    const struct MirInsn *candidate_source;
    const struct MirInsn *insn;
    const struct MirInsn *next;
    int instruction;

    if (i < 0 || i + 1 >= mir.count)
        return 0;
    for (instruction = 0; instruction + 1 < mir.count; ++instruction) {
        candidate = &mir.insns[instruction];
        if (candidate->opcode != MIR_UNARY ||
            candidate->immediate != '!' ||
            mir.insns[instruction + 1].opcode != MIR_BRANCH_FALSE ||
            mir.insns[instruction + 1].src1 != candidate->dst)
            continue;
        candidate_source = mir_definition(candidate->src1);
        if (candidate_source != NULL &&
            candidate_source->opcode == MIR_CALL &&
            (candidate_source->memory_flags & MIR_CALL_FLAG_VARIADIC) != 0)
            return 0;
    }
    insn = &mir.insns[i];
    next = &mir.insns[i + 1];
    return insn->opcode == MIR_UNARY && insn->immediate == '!' &&
           !mir_value_is_wide(insn->src1) &&
           (mir_definition(insn->src1) == NULL ||
            mir_definition(insn->src1)->opcode != MIR_UNARY) &&
           mir_value_use_count(insn->dst) == 1 &&
           next->opcode == MIR_BRANCH_FALSE && next->src1 == insn->dst;
}

/* Item 25 (mir-migration-plan-100): an `==`/`!=` comparison against the
 * constant 0 needs neither operand materialized into DE nor a 16-bit
 * `sbc hl,de` - HL already holds the left operand (loaded unconditionally
 * before every MIR_BINARY), and `ld a,h / or l` sets Z/NZ from it directly.
 * This only applies to the right operand being the zero constant (the
 * overwhelmingly common `x == 0` / `x != 0` source shape); a zero constant
 * on the left (`0 == x`) still loads HL with the constant first, so no
 * benefit is available without reordering operand evaluation, which this
 * item does not attempt. */
static int mir_fused_compare_is_const_zero_rhs(int compare_index)
{
    const struct MirInsn *compare = &mir.insns[compare_index];
    const struct MirInsn *right;

    if (compare->immediate != TOK_EQ && compare->immediate != TOK_NE)
        return 0;
    right = mir_definition(compare->src2);
    return right != NULL && right->opcode == MIR_CONST &&
           (right->immediate & 0xffffL) == 0;
}

/* Item 26 (mir-migration-plan-100): an 8-bit-range `cp`-based fast path for
 * "small constant" comparisons was investigated and deferred. MIR reaches
 * MIR_BINARY only after C's usual arithmetic conversions have already
 * promoted narrower operands to a full 16-bit int - there is no tracking of
 * an operand's pre-promotion original type (or a proven small value range)
 * surviving to this point, so proving either operand is guaranteed to fit in
 * a single byte would require new semantic/range-tracking infrastructure,
 * not a small selector tweak. Deferred until such tracking exists (same
 * "defer, don't guess" rationale as Item 6/Item 14). */

/* Item 27 (mir-migration-plan-100): a signed `<`/`>=` comparison against the
 * constant 0 needs only the sign bit of the left operand - `bit 7,h` sets Z
 * from bit 7 of H directly, with no DE materialization or 16-bit `sbc
 * hl,de` (and no xor-128 sign-flip dance, which exists only to make an
 * unsigned `sbc` behave like a signed compare against a non-zero DE). This
 * only applies when the comparison is provably signed: if either operand is
 * unsigned, "x < 0" is always false and "x >= 0" is always true, which is a
 * different (constant-fold) opportunity this item does not attempt. */
static int mir_fused_compare_is_signed_zero_sign_test(int compare_index)
{
    const struct MirInsn *compare = &mir.insns[compare_index];
    const struct MirInsn *left;
    const struct MirInsn *right;

    if (compare->immediate != '<' && compare->immediate != TOK_GE)
        return 0;
    left = mir_definition(compare->src1);
    right = mir_definition(compare->src2);
    if ((left != NULL && mir_type_uses_unsigned_comparison(left->type)) ||
        (right != NULL && mir_type_uses_unsigned_comparison(right->type)))
        return 0;
    if (right == NULL || right->opcode != MIR_CONST ||
        (right->immediate & 0xffffL) != 0)
        return 0;
    return 1;
}

static int mir_emit_fused_comparison_branch(FILE *out, const int *labels,
                                             int compare_index, int negate,
                                             int de_holds_biased_constant)
{
    const struct MirInsn *compare = &mir.insns[compare_index];
    const struct MirInsn *branch = &mir.insns[compare_index + 1 + negate];
    const struct MirInsn *left = mir_definition(compare->src1);
    const struct MirInsn *right = mir_definition(compare->src2);
    int is_unsigned =
        (left != NULL && mir_type_uses_unsigned_comparison(left->type)) ||
        (right != NULL && mir_type_uses_unsigned_comparison(right->type));
    int operation = negate ? mir_negate_comparison_operator(
                                 (int)compare->immediate)
                           : (int)compare->immediate;
    int target;
    const char *true_condition;

    if (branch->label < 0 || branch->label >= mir.next_label)
        return 0;
    target = mir_find_label(branch->label);
    if (target < 0)
        return 0;
    if ((operation == TOK_EQ || operation == TOK_NE) &&
        mir_fused_compare_is_const_zero_rhs(compare_index)) {
        /* Item 25: DE was never loaded for this case (the caller skips it
         * on this same const-zero-rhs test), so test HL directly. */
        fputs("\tld a,h\n\tor l\n", out);
    } else if ((operation == '<' || operation == TOK_GE) &&
               mir_fused_compare_is_signed_zero_sign_test(compare_index)) {
        /* Item 27: DE was never loaded for this case either (the caller
         * skips it on this same signed-zero-sign-test), so the sign bit of
         * HL is tested directly instead of a 16-bit sbc. `bit 7,h` sets Z
         * when the sign bit is clear (value >= 0) and NZ when it is set
         * (value < 0) - the opposite sense of the c/nc pair the sbc path
         * produces, so this case picks its own true_condition below rather
         * than falling into the shared switch. */
        fputs("\tbit 7,h\n", out);
        true_condition = operation == '<' ? "nz" : "z";
        return mir_emit_conditional_branch_with_phi_copies(
            out, labels, true_condition, compare_index + 1 + negate, target,
            branch->label);
    } else if (de_holds_biased_constant &&
               (operation == '<' || operation == TOK_GE)) {
        /* Item T50 (mir-text-size-plan.md): the caller already loaded DE
         * with the compile-time-biased constant (constant ^ 0x8000)
         * instead of the raw value, mirroring
         * mir_emit_homed_binary_instruction's biased_right_constant
         * optimization - only HL's sign bit needs the runtime xor-128
         * flip, not both HL and DE, and the xor's own side effect clears
         * carry so the usual leading `or a` is also unneeded. negate
         * cannot change this eligibility: mir_negate_comparison_operator
         * only ever swaps '<' with TOK_GE (never introduces '>' or
         * TOK_LE from one of these two), so the bias computed for the
         * un-negated compare->immediate remains valid after negation. */
        fputs("\tld a,h\n\txor 128\n\tld h,a\n\tsbc hl,de\n", out);
    } else {
        if (operation == '>' || operation == TOK_LE) {
            fputs("\tex de,hl\n", out);
            operation = operation == '>' ? '<' : TOK_GE;
        }
        if (!is_unsigned && operation != TOK_EQ && operation != TOK_NE)
            fputs("\tld a,h\n\txor 128\n\tld h,a\n"
                  "\tld a,d\n\txor 128\n\tld d,a\n", out);
        fputs("\tor a\n\tsbc hl,de\n", out);
    }
    switch (operation) {
    case TOK_EQ: true_condition = "z"; break;
    case TOK_NE: true_condition = "nz"; break;
    case '<': true_condition = "c"; break;
    default: true_condition = "nc"; break; /* TOK_GE */
    }
    return mir_emit_conditional_branch_with_phi_copies(
        out, labels, true_condition, compare_index + 1 + negate, target,
        branch->label);
}

/* Item T2 (mir-text-size-plan): the 32-bit ("wide") comparison operators
 * previously took the same "materialize 0/1 boolean into HL, spill it to a
 * backend slot, reload it, retest with ld a,h/or l" round trip as the 16-bit
 * path did before Item 1 - mir_binary_is_fusable_comparison only recognized
 * 16-bit operands. Every wide comparison shape (inline xor-compare for ==/!=,
 * and the __ltu/__lts/__leu/__les/__lgu/__lgs/__lku/__lks runtime helpers for
 * relational, and the float helpers) already leaves the boolean result as a
 * concrete 0/1 in HL by construction - there is no flag-based short cut
 * available (unlike the 16-bit path's `sbc hl,de`), but skipping the
 * store/reload/retest is still a direct win, and is a strict superset of
 * what the 16-bit fusion already exploits: HL is always tested with
 * `ld a,h / or l` immediately once it is known to be 0 or 1, whether the
 * branch consumes the comparison directly or through a single intervening
 * logical-not (mir_binary_is_fusable_comparison's existing negate signal).
 *
 * Item T53 (mir-text-size-plan.md): Item T2 initially kept a defensive
 * `type_is_float` exclusion in mir_binary_is_fusable_comparison pending
 * independent verification of the float comparison helpers' HL-return
 * convention. Verified directly against DCCRTL.MAC: __feqf, __fnef, __fltf,
 * __fgtf, __flef, and __fgef all end every code path with an explicit
 * `ld hl,1` or `ld hl,0` immediately before `ret` - exactly the same
 * concrete-0/1-in-HL contract this function already relies on for `long`.
 * This function needed no changes at all; only the gate in
 * mir_binary_is_fusable_comparison did. */
static int mir_emit_fused_wide_comparison_branch(FILE *out, const int *labels,
                                                  int compare_index,
                                                  int negate)
{
    const struct MirInsn *branch = &mir.insns[compare_index + 1 + negate];
    int target;
    const char *true_condition;

    if (branch->label < 0 || branch->label >= mir.next_label)
        return 0;
    target = mir_find_label(branch->label);
    if (target < 0)
        return 0;
    fputs("\tld a,h\n\tor l\n", out);
    true_condition = negate ? "z" : "nz";
    return mir_emit_conditional_branch_with_phi_copies(
        out, labels, true_condition, compare_index + 1 + negate, target,
        branch->label);
}

/* Item T45 (mir-text-size-plan.md): ported directly from
 * emit_shift_const_long (dcc_ops.c, the legacy AST backend) - any wide
 * shift count 1..31 decomposes into a whole-byte register-move (0-3
 * bytes, reusing the same move sequences legacy already validated) plus
 * a 0-7 bit remainder unrolled directly, since the count is already
 * known at compile time and a runtime b-counted loop would only add
 * control overhead for no benefit. A count of exactly 0 emits nothing.
 * The caller guarantees count is in [0,31] - counts >= 32 are handled
 * separately (or left on the runtime-loop path) since they are outside
 * the range this decomposition was validated for. Shared with Item T46's
 * multiply-by-power-of-two-constant fast path below, which is always an
 * `is_left` shift regardless of the multiplicand's signedness. */
void mir_emit_wide_shift_by_constant(FILE *out, int is_left,
                                     int is_unsigned, long count)
{
    int bytes;
    int bits;

    if (count <= 0)
        return;
    bytes = (int)(count / 8);
    bits = (int)(count % 8);

    if (is_left) {
        switch (bytes) {
        case 1: fputs("\tld d,e\n\tld e,h\n\tld h,l\n\tld l,0\n", out); break;
        case 2: fputs("\tld e,l\n\tld d,h\n\tld hl,0\n", out); break;
        case 3: fputs("\tld d,l\n\tld e,0\n\tld hl,0\n", out); break;
        default: break;
        }
        while (bits-- > 0)
            fputs("\tadd hl,hl\n\trl e\n\trl d\n", out);
    } else if (is_unsigned) {
        switch (bytes) {
        case 1: fputs("\tld l,h\n\tld h,e\n\tld e,d\n\tld d,0\n", out); break;
        case 2: fputs("\tld l,e\n\tld h,d\n\tld de,0\n", out); break;
        case 3: fputs("\tld l,d\n\tld h,0\n\tld de,0\n", out); break;
        default: break;
        }
        while (bits-- > 0)
            fputs("\tsrl d\n\trr e\n\trr h\n\trr l\n", out);
    } else {
        if (bytes > 0) {
            fputs("\tld a,d\n\trla\n\tsbc a,a\n", out);
            switch (bytes) {
            case 1: fputs("\tld l,h\n\tld h,e\n\tld e,d\n\tld d,a\n", out); break;
            case 2: fputs("\tld l,e\n\tld h,d\n\tld e,a\n\tld d,a\n", out); break;
            case 3: fputs("\tld l,d\n\tld h,a\n\tld e,a\n\tld d,a\n", out); break;
            default: break;
            }
        }
        while (bits-- > 0)
            fputs("\tsra d\n\trr e\n\trr h\n\trr l\n", out);
    }
}

/* log2 of a power-of-two 32-bit unsigned value; returns -1 if v is 0 or
 * not an exact power of two. Ported from ulong_log2_pow2 (dcc_ops.c). Not
 * static: also used by Item T49's scalar (16-bit) unsigned div/mod-by-
 * power-of-2 fast path, including the call site in
 * dcc_mir_emit_common.c. A 16-bit divisor is simply passed through the
 * same 32-bit-wide check unchanged - the power-of-two test and log2
 * computation are correct for any width. */
int mir_ulong_log2_pow2(unsigned long v)
{
    int n;

    if (v == 0 || (v & (v - 1)) != 0)
        return -1;
    n = 0;
    while (v > 1) {
        v >>= 1;
        ++n;
    }
    return n;
}

/* AND one 16-bit register pair (hi_reg:lo_reg) with a compile-time word
 * mask in place, without a temporary register pair: a byte that is
 * all-ones in the mask is left untouched, a byte that is all-zero
 * collapses to a single immediate load, and anything else gets one
 * immediate `and`. Ported from emit_and_word_const (dcc_ops.c). Not
 * static: also called from dcc_mir_emit_common.c's mir_emit_scalar_value
 * (Item T48) for the trivial-single-return selector's own scalar '&'
 * case, mirroring how mir_emit_scalar_shift is already shared between
 * both files (Item T44). */
void mir_emit_word_and_constant(FILE *out, char hi_reg, char lo_reg,
                                 unsigned int word_mask)
{
    unsigned int hib = (word_mask >> 8) & 0xffU;
    unsigned int lob = word_mask & 0xffU;

    if (word_mask == 0xffffU)
        return;
    if (word_mask == 0) {
        fprintf(out, "\tld %c%c,0\n", hi_reg, lo_reg);
        return;
    }
    if (hib == 0)
        fprintf(out, "\tld %c,0\n", hi_reg);
    else if (hib != 0xffU)
        fprintf(out, "\tld a,%c\n\tand %u\n\tld %c,a\n", hi_reg, hib, hi_reg);
    if (lob == 0)
        fprintf(out, "\tld %c,0\n", lo_reg);
    else if (lob != 0xffU)
        fprintf(out, "\tld a,%c\n\tand %u\n\tld %c,a\n", lo_reg, lob, lo_reg);
}

/* Item T47 (mir-text-size-plan.md): AND the DE:HL long value in place with
 * a compile-time 32-bit mask, byte by byte, instead of the generic
 * push/pop/ex-de-hl AND dance. Ported from emit_and_long_const
 * (dcc_ops.c). */
static void mir_emit_wide_and_constant(FILE *out, unsigned long mask)
{
    mir_emit_word_and_constant(out, 'd', 'e',
                                (unsigned int)((mask >> 16) & 0xffffUL));
    mir_emit_word_and_constant(out, 'h', 'l', (unsigned int)(mask & 0xffffUL));
}

static int mir_emit_nested_truth_comparison_branch(FILE *out,
                                                    const int *labels,
                                                    int instruction)
{
    int left_parameter;
    int right_parameter;
    int left_zero_label;
    int done_label;
    const struct MirInsn *branch;

    if (!mir_nested_truth_comparison(instruction, &left_parameter,
                                     &right_parameter))
        return 0;
    branch = &mir.insns[instruction + 1];
    if (branch->label < 0 || branch->label >= mir.next_label)
        return 0;
    left_zero_label = new_label();
    done_label = new_label();
    mir_emit_virtual_load(out, left_parameter);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", left_zero_label);
    mir_emit_virtual_load(out, right_parameter);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp nz,L%d\n\tjp L%d\nL%d:\n",
            labels[branch->label], done_label, left_zero_label);
    mir_emit_virtual_load(out, right_parameter);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\nL%d:\n",
            labels[branch->label], done_label);
    return 1;
}

/* Checks whether insn (a wide '&') has a MIR_CONST operand and, if so,
 * emits the byte-wise mask-AND fast path and returns 1. Checks src2
 * first (the common `x & CONST` shape - DE:HL already holds src2's
 * now-dead value, discarded via pop hl/pop de to restore src1, the real
 * lhs, from the stack, mirroring Items T45/T46's identical restore
 * sequence), then src1 (the `CONST & x` shape MIR does not canonicalize
 * away - DE:HL already holds src2, the real lhs, needing no restore;
 * the dead constant pushed for src1 is simply discarded via pop bc/pop
 * bc). Returns 0 (emits nothing) if neither operand is a compile-time
 * constant, leaving the caller to fall back to the generic AND path. */
static int mir_emit_wide_and_constant_fastpath(FILE *out,
                                                const struct MirInsn *insn)
{
    const struct MirInsn *src2_definition = mir_definition(insn->src2);
    const struct MirInsn *src1_definition = mir_definition(insn->src1);

    if (src2_definition != NULL && src2_definition->opcode == MIR_CONST) {
        fputs("\tpop hl\n\tpop de\n", out);
        mir_emit_wide_and_constant(out, (unsigned long)src2_definition->immediate);
        return 1;
    }

    if (src1_definition != NULL && src1_definition->opcode == MIR_CONST) {
        fputs("\tpop bc\n\tpop bc\n", out);
        mir_emit_wide_and_constant(out, (unsigned long)src1_definition->immediate);
        return 1;
    }
    return 0;
}

static const char *mir_wide_runtime_helper(const struct MirInsn *insn)
{
    int operation = (int)insn->immediate;
    int operand_type = insn->secondary_offset;

    if (type_is_float(operand_type)) {
        if (operation == '+' || operation == '-' || operation == '*' ||
            operation == '/')
            return operation == '+' ? "__faf" : operation == '-' ? "__fsf" :
                   operation == '*' ? "__fmf" : "__fdf";
        if (operation == TOK_EQ || operation == TOK_NE ||
            operation == '<' || operation == '>' ||
            operation == TOK_LE || operation == TOK_GE)
            return operation == TOK_EQ ? "__feqf" :
                   operation == TOK_NE ? "__fnef" :
                   operation == '<' ? "__fgtf" :
                   operation == '>' ? "__fltf" :
                   operation == TOK_LE ? "__fgef" : "__flef";
        return NULL;
    }
    if (operation == '<' || operation == '>' || operation == TOK_LE ||
        operation == TOK_GE) {
        int is_unsigned = (operand_type & TYPE_UNSIGNED) != 0;
        return operation == '<' ? (is_unsigned ? "__ltu" : "__lts") :
               operation == TOK_LE ? (is_unsigned ? "__leu" : "__les") :
               operation == '>' ? (is_unsigned ? "__lgu" : "__lgs") :
               (is_unsigned ? "__lku" : "__lks");
    }
    if (operation == '*') {
        const struct MirInsn *src2_definition =
            mir_definition(insn->src2);
        const struct MirInsn *src1_definition =
            mir_definition(insn->src1);

        if (src2_definition != NULL &&
            src2_definition->opcode == MIR_CONST &&
            mir_ulong_log2_pow2(
                (unsigned long)src2_definition->immediate) > 0)
            return NULL;
        if (src1_definition != NULL &&
            src1_definition->opcode == MIR_CONST &&
            mir_ulong_log2_pow2(
                (unsigned long)src1_definition->immediate) > 0)
            return NULL;
        return "__lmul";
    }
    if (operation == '/')
        return (insn->type & TYPE_UNSIGNED) != 0 ? "__ldu" : "__lds";
    if (operation == '%')
        return (insn->type & TYPE_UNSIGNED) != 0 ? "__lmu" : "__lms";
    return NULL;
}

int mir_emit_wide_operation(FILE *out, const struct MirInsn *insn)
{
    const char *helper = NULL;
    int operation = (int)insn->immediate;
    int operand_type = insn->secondary_offset;
    if (type_is_float(operand_type)) {
        helper = mir_wide_runtime_helper(insn);
        if (helper == NULL)
            return 0;
        mir_emit_runtime_call(out, helper);
        fputs("\tpop bc\n\tpop bc\n", out);
        return 1;
    }
    if (operation == TOK_EQ || operation == TOK_NE) {
        int true_label = new_label();
        int end_label = new_label();
        fputs("\tpop bc\n\tld a,c\n\txor l\n\tld l,a\n"
              "\tld a,b\n\txor h\n\tor l\n\tld l,a\n"
              "\tpop bc\n\tld a,c\n\txor e\n\tor l\n\tld l,a\n"
              "\tld a,b\n\txor d\n\tor l\n", out);
        fprintf(out, operation == TOK_EQ ? "\tjp z, L%d\n" : "\tjp nz, L%d\n",
                true_label);
        fprintf(out, "\tld hl,0\n\tjp L%d\nL%d:\n\tld hl,1\nL%d:\n",
                end_label, true_label, end_label);
        return 1;
    }
    if (operation == '<' || operation == '>' || operation == TOK_LE ||
        operation == TOK_GE) {
        const struct MirInsn *left = mir_definition(insn->src1);
        const struct MirInsn *right = mir_definition(insn->src2);
        int compares_constant =
            (left != NULL && left->opcode == MIR_CONST) ||
            (right != NULL && right->opcode == MIR_CONST);

        if (compares_constant) {
            if ((operand_type & TYPE_UNSIGNED) != 0)
                mir_spilled_cfg_used_unsigned_wide_constant_relational = 1;
            else
                mir_spilled_cfg_used_signed_wide_constant_relational = 1;
        }
        helper = mir_wide_runtime_helper(insn);
        fputs("\tpush de\n\tpush hl\n", out);
        mir_emit_runtime_call(out, helper);
        fputs("\tex de,hl\n\tld hl,8\n\tadd hl,sp\n"
              "\tld sp,hl\n\tex de,hl\n", out);
        return 1;
    }
    switch ((int)insn->immediate) {
    case '+':
        fputs("\tpop bc\n\tor a\n\tadd hl,bc\n"
              "\tex de,hl\n\tpop bc\n\tadc hl,bc\n\tex de,hl\n", out);
        return 1;
    case '-':
        fputs("\tld b,h\n\tld c,l\n\tpop hl\n\tor a\n\tsbc hl,bc\n"
              "\tld b,h\n\tld c,l\n\tpop hl\n\tsbc hl,de\n"
              "\tld d,b\n\tld e,c\n\tex de,hl\n", out);
        return 1;
    case '&': case '|': case '^':
        /* Item T47 (mir-text-size-plan.md): `long_expr & <compile-time
         * constant mask>` skips the generic push/pop/ex-de-hl AND dance
         * entirely in favor of a byte-wise mask apply, mirroring
         * legacy's emit_and_long_const. Only '&' is special-cased here,
         * matching legacy exactly - there is no equivalent OR/XOR
         * constant-mask optimization in the legacy backend to port. */
        if (insn->immediate == '&' &&
            mir_emit_wide_and_constant_fastpath(out, insn))
            return 1;
        {
            const char *operation = insn->immediate == '&' ? "and" :
                                    insn->immediate == '|' ? "or" : "xor";
            fprintf(out,
                    "\tpop bc\n\tld a,l\n\t%s c\n\tld l,a\n"
                    "\tld a,h\n\t%s b\n\tld h,a\n"
                    "\tex de,hl\n\tpop bc\n"
                    "\tld a,l\n\t%s c\n\tld l,a\n"
                    "\tld a,h\n\t%s b\n\tld h,a\n\tex de,hl\n",
                    operation, operation, operation, operation);
        }
        return 1;
    case TOK_SHL: case TOK_SHR:
        {
            /* Item T45 (mir-text-size-plan.md): the wide (32-bit)
             * counterpart of Item T44's scalar shift unroll - a
             * compile-time-constant shift count still went through the
             * generic runtime bit-loop unconditionally. When insn->src2
             * resolves to a MIR_CONST in the only meaningful range for
             * a 32-bit value (0-31; wider counts are undefined
             * behavior in C and left on the loop path unchanged),
             * decompose into the same whole-byte-move-plus-bit-remainder
             * sequence legacy's emit_shift_const_long already uses (see
             * mir_emit_wide_shift_by_constant above) instead of a
             * generic per-bit unroll - the count is already known, so
             * the loop's `ld a,l` (extracting the runtime count from
             * src2's low byte) and `ld b,a`/`dec b`/`jp` bookkeeping are
             * unneeded entirely, not just the loop body repetition. A
             * shift count of 0 collapses to just the two pops that
             * restore src1 from the stack, with no shift instructions
             * at all. */
            const struct MirInsn *count_definition =
                mir_definition(insn->src2);
            int is_unsigned = (operand_type & TYPE_UNSIGNED) != 0;

            if (count_definition != NULL &&
                count_definition->opcode == MIR_CONST &&
                count_definition->immediate >= 0 &&
                count_definition->immediate < 32) {
                long count = count_definition->immediate;

                fputs("\tpop hl\n\tpop de\n", out);
                mir_emit_wide_shift_by_constant(out,
                                                 insn->immediate == TOK_SHL,
                                                 is_unsigned, count);
            } else {
                int loop_label = new_label();
                int done_label = new_label();
                fputs("\tld a,l\n\tpop hl\n\tpop de\n\tld b,a\n", out);
                fprintf(out, "L%d:\n\tld a,b\n\tor a\n\tjp z, L%d\n",
                        loop_label, done_label);
                if (insn->immediate == TOK_SHL)
                    fputs("\tadd hl,hl\n\trl e\n\trl d\n", out);
                else if (is_unsigned)
                    fputs("\tsrl d\n\trr e\n\trr h\n\trr l\n", out);
                else
                    fputs("\tsra d\n\trr e\n\trr h\n\trr l\n", out);
                fprintf(out, "\tdec b\n\tjp L%d\nL%d:\n",
                        loop_label, done_label);
            }
        }
        return 1;
    case '*':
        {
            /* Item T46 (mir-text-size-plan.md): ported from
             * emit_mul_pow2_long_const (dcc_ops.c) - `long_expr *
             * <compile-time power-of-two constant>` strength-reduces to
             * a left shift instead of a call to the generic __lmul
             * runtime helper, reusing mir_emit_wide_shift_by_constant
             * (Item T45, above). Unlike legacy's AST-level version
             * (which only ever sees the constant in the syntactic right
             * operand position), MIR's lowering does not canonicalize
             * commutative operands - `x * 4L` lowers with the constant
             * as src2, `4L * x` lowers with the constant as src1 - so
             * both positions are checked. When src2 is the constant,
             * DE:HL (already loaded with src2's now-dead value) is
             * discarded and src1 (the real multiplicand) is restored
             * from the stack, mirroring Item T45's shift-fix exactly.
             * When src1 is the constant, DE:HL already holds src2 (the
             * real multiplicand, loaded most recently) and the dead
             * constant left on the stack from src1's evaluation is
             * simply popped off - no register restore needed at all.
             * Declines (falls through to __lmul) for 0, 1, and any
             * non-power-of-two multiplier, matching legacy's exact
             * scope - 0 and 1 are rare enough as literal long
             * multipliers not to be worth special-casing separately. */
            const struct MirInsn *src2_definition =
                mir_definition(insn->src2);
            const struct MirInsn *src1_definition =
                mir_definition(insn->src1);

            if (src2_definition != NULL &&
                src2_definition->opcode == MIR_CONST) {
                int shift = mir_ulong_log2_pow2(
                    (unsigned long)src2_definition->immediate);
                if (shift > 0) {
                    fputs("\tpop hl\n\tpop de\n", out);
                    mir_emit_wide_shift_by_constant(out, 1, 1, shift);
                    return 1;
                }
            }
            if (src1_definition != NULL &&
                src1_definition->opcode == MIR_CONST) {
                int shift = mir_ulong_log2_pow2(
                    (unsigned long)src1_definition->immediate);
                if (shift > 0) {
                    fputs("\tpop bc\n\tpop bc\n", out);
                    mir_emit_wide_shift_by_constant(out, 1, 1, shift);
                    return 1;
                }
            }
        }
        helper = mir_wide_runtime_helper(insn); break;
    case '/': case '%': helper = mir_wide_runtime_helper(insn); break;
    default: return 0;
    }
    mir_emit_runtime_call(out, helper);
    fputs("\tpop bc\n\tpop bc\n", out);
    return 1;
}

/* Item T32 (mir-text-size-plan.md): mir_emit_fused_comparison_branch and its
 * wide/sign-test siblings need to know, before committing to an emission
 * shape, whether a given CFG edge has any phi copies to run at all - see
 * mir_phi_copies_are_empty below. Both that predicate and
 * mir_emit_spilled_phi_copies itself now share this single collection
 * routine so the "what copies does this edge need" logic can never drift
 * between the two callers. */
static int mir_collect_phi_copies_for_edge(int predecessor, int successor,
                                            int *sources, int *destinations)
{
    int predecessor_label = mir_block_label_before(predecessor);
    int edge_label = -1;
    int instruction = mir_first_phi_or_block_end(successor);
    int copy_count = 0;

    if (predecessor >= 0 && predecessor < mir.count &&
        (mir.insns[predecessor].opcode == MIR_JUMP ||
         mir.insns[predecessor].opcode == MIR_BRANCH_FALSE) &&
        mir_find_label(mir.insns[predecessor].label) == successor)
        edge_label = mir.insns[predecessor].label;

    while (instruction >= 0 && instruction < mir.count &&
           (mir.insns[instruction].opcode == MIR_PHI ||
            mir.insns[instruction].opcode == MIR_NOP)) {
        const struct MirInsn *phi = &mir.insns[instruction];
        int source;
        if (phi->opcode == MIR_NOP) {
            ++instruction;
            continue;
        }
        if (!mir_value_has_use(phi->dst)) {
            ++instruction;
            continue;
        }
        source = mir_phi_source_for_edge(phi, predecessor_label, edge_label,
                                         successor, instruction);
        if (source < 0)
            return -1;
        if (copy_count >= MAX_FLOW)
            return -1;
        sources[copy_count] = source;
        destinations[copy_count] = phi->dst;
        ++copy_count;
        ++instruction;
    }
    return copy_count;
}

/* Item T32: true only when the edge from `predecessor` to `successor`
 * definitely has no phi copies to run (a hard-fail collection is treated as
 * "not provably empty" so the caller falls back to the general path, which
 * will itself surface the same failure mir_emit_spilled_phi_copies always
 * has). Callers use this to decide whether a fused comparison branch can
 * skip straight to a single inverted jump - see
 * mir_emit_conditional_branch_with_phi_copies. */
static int mir_phi_copies_are_empty(int predecessor, int successor)
{
    int sources[MAX_FLOW];
    int destinations[MAX_FLOW];
    return mir_collect_phi_copies_for_edge(predecessor, successor, sources,
                                            destinations) == 0;
}

static int mir_emit_spilled_phi_copies(FILE *out, int predecessor,
                                       int successor)
{
    int sources[MAX_FLOW];
    int destinations[MAX_FLOW];
    int copy_count = mir_collect_phi_copies_for_edge(predecessor, successor,
                                                      sources, destinations);
    int copy;

    if (copy_count < 0)
        return 0;
    /* Item T9 (mir-text-size-plan.md): the general push-all-sources-then-
     * pop-all-destinations-in-reverse shape below exists to let several
     * simultaneous phi copies swap through each other safely (a later
     * destination store must not clobber a still-unread source). With
     * exactly one copy pending there is no other copy to clobber or be
     * clobbered by, so the whole push/pop round-trip through the stack is
     * dead weight - a direct load-then-store reaches the identical result. */
    if (copy_count == 1) {
        if (mir_value_is_wide(sources[0])) {
            mir_emit_virtual_load_wide(out, sources[0]);
            mir_emit_virtual_store_wide(out, destinations[0]);
        } else {
            mir_emit_virtual_load(out, sources[0]);
            mir_emit_virtual_store(out, destinations[0]);
        }
        return 1;
    }
    for (copy = 0; copy < copy_count; ++copy) {
        if (mir_value_is_wide(sources[copy])) {
            mir_emit_virtual_load_wide(out, sources[copy]);
            fputs("\tpush de\n\tpush hl\n", out);
        } else {
            mir_emit_virtual_load(out, sources[copy]);
            fputs("\tpush hl\n", out);
        }
    }
    for (copy = copy_count - 1; copy >= 0; --copy) {
        if (mir_value_is_wide(destinations[copy])) {
            fputs("\tpop hl\n\tpop de\n", out);
            mir_emit_virtual_store_wide(out, destinations[copy]);
        } else {
            fputs("\tpop hl\n", out);
            mir_emit_virtual_store(out, destinations[copy]);
        }
    }
    return 1;
}

int mir_scalar_memory_location(const struct MirInsn *insn, int *type,
                                      int *storage, int *offset)
{
    struct Sym *global;
    int instruction;
    if (insn->object >= 0 && insn->object < mir.object_count) {
        const struct MirObject *object = &mir.objects[insn->object];
        *type = object->type;
        *storage = object->storage;
        *offset = object->offset + (int)insn->immediate;
        return 1;
    }
    if (mir_declared_location(insn->name, type, storage, offset)) {
        *offset += (int)insn->immediate;
        return 1;
    }
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_COMPOUND_ADDRESS &&
            strcmp(mir.insns[instruction].name, insn->name) == 0) {
            *type = insn->type;
            *storage = SC_LOCAL;
            *offset = (int)mir.insns[instruction].immediate +
                      (int)insn->immediate;
            return 1;
        }
    global = find_global(insn->name);
    if (global != NULL) {
        *type = global->type;
        *storage = global->storage;
        *offset = global->offset + (int)insn->immediate;
        return 1;
    }
    return 0;
}

/* T383 (mir-text-size-plan.md): mir_emit_constant_absolute_load/store already
 * collapse a global/extern base plus constant MEMBER_ADDRESS/INDEX_ADDRESS
 * offsets into one absolute operand, but only for the instruction that
 * *dereferences* the resulting address (a scalar load/store). When the same
 * fully-constant chain is instead consumed as plain data - stored as a
 * pointer value, passed as a call argument, or returned - the address is
 * still recomputed at runtime through the whole base-load-and-add sequence.
 * mir_prepare_constant_absolute_operand already proves and formats the exact
 * same operand from insn->dst directly (mir_resolve_constant_absolute_address
 * walks from the instruction defining the value, which is this instruction
 * itself when called against its own dst); reuse it here to materialize the
 * value in one `ld hl,<label>+<offset>` instead. Ordered after each site's
 * existing "only used by a supported load/store" skip so the dereferencing
 * consumer keeps folding the address into its own single instruction rather
 * than this value being materialized and then immediately redereferenced. */
static int mir_emit_constant_absolute_address_value(
    FILE *out, const struct MirInsn *insn)
{
    char operand[160];

    if (!mir_prepare_constant_absolute_operand(
            out, insn->dst, operand, sizeof(operand)))
        return 0;
    mir_spilled_cfg_used_constant_absolute = 1;
    if (mir_constant_absolute_address_has_index(insn->dst))
        mir_spilled_cfg_used_constant_index_absolute = 1;
    fprintf(out, "\tld hl,%s\n", operand);
    mir_emit_virtual_store(out, insn->dst);
    return 1;
}

static int mir_emit_constant_absolute_load(
    FILE *out, const struct MirInsn *insn)
{
    char operand[160];

    if (!mir_constant_absolute_access_supported(insn) ||
        !mir_prepare_constant_absolute_operand(
            out, insn->src1, operand, sizeof(operand)))
        return 0;
    mir_spilled_cfg_used_constant_absolute = 1;
    if (mir_constant_absolute_address_has_index(insn->src1))
        mir_spilled_cfg_used_constant_index_absolute = 1;
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
    mir_emit_virtual_store(out, insn->dst);
    return 1;
}

static int mir_emit_constant_absolute_store(
    FILE *out, const struct MirInsn *insn)
{
    char operand[160];

    if (!mir_constant_absolute_access_supported(insn) ||
        !mir_prepare_constant_absolute_operand(
            out, insn->src1, operand, sizeof(operand)))
        return 0;
    mir_spilled_cfg_used_constant_absolute = 1;
    if (mir_constant_absolute_address_has_index(insn->src1))
        mir_spilled_cfg_used_constant_index_absolute = 1;
    mir_emit_virtual_load(out, insn->src2);
    if (insn->memory_size == 1)
        fprintf(out, "\tld a,l\n\tld (%s),a\n", operand);
    else
        fprintf(out, "\tld (%s),hl\n", operand);
    return 1;
}

static void mir_report_constant_absolute_addresses(void)
{
    int instruction;
    int loads = 0;
    int stores = 0;

    if (getenv("DCC_MIR_ABSOLUTE_ADDRESS_REPORT") == NULL)
        return;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (!mir_constant_absolute_access_supported(insn))
            continue;
        if (insn->opcode == MIR_LOAD_INDIRECT)
            ++loads;
        else
            ++stores;
    }
    if (loads != 0 || stores != 0)
        fprintf(stderr,
                "; MIR absolute-address function=%s loads=%d stores=%d\n",
                mir.name, loads, stores);
}

/* Item T85 (mir-text-size-plan.md): storage class + offset alone do not
 * uniquely identify a scalar memory location - two distinct top-level
 * (non-aggregate-field) globals both resolve to storage=SC_GLOBAL,
 * offset=0 (mir_scalar_memory_location's `global->offset` term is only
 * ever nonzero for a struct/array member, not for a whole scalar object),
 * so comparing storage/offset alone can wrongly treat two different
 * globals as "the same location". This wrapper adds the missing identity
 * check: locals/params carry a real per-object index (insn->object >= 0)
 * that already disambiguates them uniquely, so compare that directly;
 * globals/externs (object == -1) fall back to comparing their resolved
 * declared name instead. Callers that key HL-forwarding re-arms on "is
 * this a fresh load of the exact same location a preceding store/fused
 * inc-dec just wrote" must use this instead of a bare storage/offset
 * compare (see the MIR_STORE and MIR_BINARY selfstore-incdec re-arm
 * sites) - found via a synthetic two-global regression test
 * (`ga = 5; return gb;` wrongly forwarding ga's value as gb's). */
static int mir_same_scalar_memory_location(const struct MirInsn *a,
                                            const struct MirInsn *b)
{
    int type_a, storage_a, offset_a;
    int type_b, storage_b, offset_b;
    if (!mir_scalar_memory_location(a, &type_a, &storage_a, &offset_a) ||
        !mir_scalar_memory_location(b, &type_b, &storage_b, &offset_b))
        return 0;
    if (storage_a != storage_b || offset_a != offset_b)
        return 0;
    if (a->object >= 0 || b->object >= 0)
        return a->object == b->object;
    return strcmp(a->name, b->name) == 0;
}

/* Item 31 (mir-migration-plan-100): a bare "x++;"/"x--;" statement on a
 * frame-local scalar lowers (mir_lower_incdec) to load-x / add-or-sub-1 /
 * store-x, going through the general spilled-scalar-cfg load/store path
 * even though the whole operation only ever touches x's own memory. The
 * legacy AST backend already has a dedicated fast path for exactly this
 * shape (emit_incdec_sym_direct, dcc_symbols.c ~line 1322): "inc (ix+n)"
 * (or a carry-checked byte-pair form for 16-bit values) directly against
 * the frame slot, with no register round-trip at all. This mirrors that
 * fast path for the MIR backend, restricted to the same population
 * legacy restricts itself to - non-pointer, frame-local-or-parameter,
 * 16-bit scalars (pointer ++/-- must scale by the pointee size, not a
 * raw +-1, so it is deliberately excluded here exactly as it is in
 * emit_incdec_sym_direct; 8-bit char and 32-bit long operands are also
 * out of scope for this item, left for a future extension since they
 * need their own byte-width-specific carry-chain shape).
 *
 * Item T36 (mir-text-size-plan.md): emit_incdec_sym_direct also has a
 * separate, simpler fast path for a non-pointer 16-bit *global* (or
 * extern) - is_global_word_sym(s) - "ld hl,(name) / inc-or-dec hl /
 * ld (name),hl", since inc/dec hl is an atomic 16-bit operation with no
 * byte-pair carry chain to worry about (unlike the frame-relative form,
 * which must ripple a carry from the low byte into a separate high-byte
 * "inc (ix+n+1)"). Item 31 only ever mirrored the local/parameter half of
 * emit_incdec_sym_direct - this extends the same predicate to recognize
 * the global/extern case too, confirmed via forced-accept-diff on
 * t2darr.c's check() (a `static int failures; ...; failures++;` pattern)
 * that legacy already takes this fast path (3 instructions) while MIR
 * fell back to a full load/push/pop/add/store round trip (7
 * instructions) for the identical global increment.
 *
 * Returns 1 and sets *store_index to the sole store instruction re-writing
 * the result back to the same frame slot iff every one of the following
 * holds: the operator is '+' or '-' against the exact constant 1; the
 * left operand's value is bound to a 16-bit non-pointer local/parameter/
 * global/extern memory location; and the result value (insn->dst) has
 * exactly one use anywhere in the function, which is a plain MIR_STORE
 * writing to that identical memory location.
 *
 * Note on scope: within a single basic block, or across a loop-carrying
 * PHI merge, any later reference to the same source variable reuses this
 * MIR_BINARY's dst value directly (dcc's MIR construction does local value
 * numbering, confirmed while building this item's test coverage), which
 * gives the value a second use and disqualifies it here - that is by
 * design, since the fused instruction only replaces a genuine store-then-
 * never-read-again sequence, not a live loop induction variable (those
 * stay in registers or flow through PHI nodes, and any store back to a
 * frame slot for them is a spill the register allocator itself decided
 * was necessary, not a redundant round-trip this fusion should remove).
 * This makes the fusion's real-world hit population the population of
 * dead-after-increment locals (e.g. "x++;" as the last touch of x before
 * an unrelated return), confirmed narrow by census (see Execution Log). */
static int mir_binary_is_selfstore_incdec(int index, int *store_index)
{
    const struct MirInsn *insn = &mir.insns[index];
    const struct MirInsn *left_definition;
    const struct MirInsn *right_definition;
    int memory_type, memory_storage, memory_offset;
    int store_type, store_storage, store_offset;
    int uses = 0;
    int found_store = -1;
    int scan;

    if (insn->opcode != MIR_BINARY ||
        (insn->immediate != '+' && insn->immediate != '-'))
        return 0;
    if (type_ptr_depth(insn->secondary_offset) > 0 ||
        type_size(insn->secondary_offset) != 2)
        return 0;
    right_definition = mir_definition(insn->src2);
    if (right_definition == NULL || right_definition->opcode != MIR_CONST ||
        right_definition->immediate != 1)
        return 0;
    left_definition = mir_definition(insn->src1);
    if (left_definition == NULL ||
        !mir_scalar_memory_location(left_definition, &memory_type,
                                    &memory_storage, &memory_offset) ||
        (memory_storage != SC_LOCAL && memory_storage != SC_PARAM &&
         memory_storage != SC_GLOBAL && memory_storage != SC_EXTERN) ||
        type_ptr_depth(memory_type) > 0 || type_size(memory_type) != 2)
        return 0;
    for (scan = 0; scan < mir.count; ++scan) {
        const struct MirInsn *use = &mir.insns[scan];
        if (use->src1 != insn->dst && use->src2 != insn->dst)
            continue;
        if (use->opcode != MIR_STORE || use->src1 != insn->dst ||
            !mir_scalar_memory_location(use, &store_type, &store_storage,
                                        &store_offset) ||
            store_storage != memory_storage || store_offset != memory_offset)
            return 0;
        found_store = scan;
        ++uses;
    }
    if (uses != 1)
        return 0;
    *store_index = found_store;
    return 1;
}

/* Value-indexed wrapper around mir_binary_is_selfstore_incdec for slot-
 * assignment callers that only have a value, not its defining instruction
 * index (mirroring how mir_call_only_constant/mir_binary_only_constant are
 * both value-indexed too). */
int mir_value_is_selfstore_incdec(int value)
{
    const struct MirInsn *definition = mir_definition(value);
    int store_index;

    if (definition == NULL || definition->opcode != MIR_BINARY)
        return 0;
    return mir_binary_is_selfstore_incdec((int)(definition - mir.insns),
                                          &store_index);
}

/* Item T36 (mir-text-size-plan.md): true iff `value`'s sole use anywhere
 * in the function is as the left (src1) operand of a MIR_BINARY that
 * itself qualifies as a fused selfstore increment/decrement
 * (mir_binary_is_selfstore_incdec) - in that case the fused emission
 * re-reads the operand's memory location directly (see
 * mir_emit_selfstore_incdec/_global), making `value`'s own defining
 * MIR_LOAD/MIR_PARAM entirely redundant: without this check, that load
 * still ran its normal emission (materializing the value into hl and
 * often staging it via a stack push anticipating the *ordinary* binary
 * form the fusion now bypasses entirely), a load-then-discard exactly
 * like the dead-unary case mir_value_only_used_by_dead_unary already
 * covers for a different consumer shape. Found via a forced-accept
 * diff on t2darr.c's check() after the global selfstore-incdec fusion
 * above was added: `failures++` correctly fused into 3 instructions,
 * but the preceding `ld hl,(_Z0001)` / `push hl` (the now-superseded
 * ordinary load-and-forward for the same read) still ran undisturbed
 * ahead of it. Mirrors the same one-and-only-one-use requirement
 * mir_binary_is_selfstore_incdec already applies to the *result*
 * side. */
static int mir_value_is_selfstore_incdec_source(int value)
{
    int instruction;
    int uses = 0;
    int sole_user = -1;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->src1 == value || insn->src2 == value ||
            mir_call_uses_value(insn, value)) {
            if (insn->opcode != MIR_BINARY || insn->src1 != value)
                return 0;
            sole_user = instruction;
            ++uses;
        }
    }
    if (uses != 1)
        return 0;
    {
        int store_index;
        return mir_binary_is_selfstore_incdec(sole_user, &store_index);
    }
}

/* Emits the Item 31 carry-checked byte-pair increment/decrement directly
 * against a frame slot, mirroring emit_incdec_sym_direct's 2-byte form
 * exactly: "inc (ix+n)" cannot ripple a carry into the high byte on its
 * own, so an explicit "jp nz" skips the high-byte adjustment on the
 * common case where the low byte doesn't wrap. */
static void mir_emit_selfstore_incdec(FILE *out, int offset, int is_inc)
{
    int done = new_label();

    if (is_inc) {
        fprintf(out, "\tinc (ix%+d)\n", offset);
        fprintf(out, "\tjp nz, L%d\n", done);
        fprintf(out, "\tinc (ix%+d)\n", offset + 1);
    } else {
        fprintf(out, "\tld a,(ix%+d)\n", offset);
        fprintf(out, "\tdec (ix%+d)\n", offset);
        fputs("\tor a\n", out);
        fprintf(out, "\tjp nz, L%d\n", done);
        fprintf(out, "\tdec (ix%+d)\n", offset + 1);
    }
    fprintf(out, "L%d:\n", done);
}

/* Item T36 (mir-text-size-plan.md): the global/extern counterpart of
 * mir_emit_selfstore_incdec, mirroring emit_incdec_sym_direct's own
 * is_global_word_sym fast path (dcc_symbols.c) exactly - "ld hl,(name) /
 * inc-or-dec hl / ld (name),hl". A 16-bit inc/dec is a single atomic
 * instruction with no byte-pair carry chain to manage (unlike the frame-
 * relative form above, which must ripple a carry from the low byte into
 * a separate high-byte increment), so this is simpler than the local/
 * parameter form rather than needing its own carry-checked label. */
static void mir_emit_selfstore_incdec_global(FILE *out,
                                              const struct MirInsn *definition,
                                              int storage, int is_inc)
{
    struct Sym *global = find_global(definition->name);
    const char *assembly_name = asm_name_for(
        global != NULL ? sym_asm_name(global)
                       : mir_declared_link_name(definition->name));

    if (storage == SC_EXTERN)
        fprintf(out, "\textrn %s\n", assembly_name);
    fprintf(out, "\tld hl,(%s)\n", assembly_name);
    fputs(is_inc ? "\tinc hl\n" : "\tdec hl\n", out);
    fprintf(out, "\tld (%s),hl\n", assembly_name);
}

static int mir_scalar_cfg_preflight_reject(const char *reason, int instruction)
{
    if (getenv("DCC_MIR_SELECT_REPORT") != NULL)
        fprintf(stderr,
                "; MIR scalar-cfg preflight function=%s reason=%s insn=%d\n",
                mir.name, reason, instruction);
    return 0;
}

/* Item 86: single shared frame-size accounting predicate. Calls
 * mir_prepare_backend_slots() (which has the side effect of assigning
 * backend slots), so call it exactly once per candidate emitter, same as
 * both call sites below did individually before this consolidation.
 * aggregate_temp_bytes is always 0 for callers that structurally exclude
 * MIR_CALL (e.g. mir_try_emit_general_rollout's opcode whitelist), so
 * including it here is always safe and never changes behavior for them.
 */
int mir_current_frame_bytes(void)
{
    return mir_effective_local_bytes() + mir.aggregate_temp_bytes +
           2 * mir_prepare_backend_slots();
}

int mir_spilled_cfg_depends_on_dead_store_forwarding(void)
{
    return mir_spilled_cfg_used_dead_store_forwarding;
}

int mir_spilled_cfg_depends_on_direct_byte_param(void)
{
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->dst >= 0 && type_size(insn->type) == 1 &&
            mir_value_has_use(insn->dst) &&
            mir_value_has_direct_named_home(insn->dst))
            return 1;
    }
    return 0;
}

int mir_spilled_cfg_depends_on_constant_index_absolute(void)
{
    return mir_spilled_cfg_used_constant_index_absolute;
}

int mir_spilled_cfg_depends_on_constant_absolute(void)
{
    return mir_spilled_cfg_used_constant_absolute;
}

int mir_spilled_cfg_depends_on_dynamic_index_base_forwarding(void)
{
    return mir_spilled_cfg_used_dynamic_index_base_forwarding;
}

int mir_spilled_cfg_depends_on_wide_constant_rematerialization(void)
{
    return mir_spilled_cfg_used_wide_constant_rematerialization;
}

/* T394 (mir-text-size-plan.md): unlike signed wide relational compares
 * against a constant (which legacy inlines via the sign-flip + 32-bit
 * subtract trick, with the C+1/C-1 adjustment for '>'/'<='), legacy's own
 * codegen for an UNSIGNED wide relational compare against a constant also
 * calls the matching __ltu/__leu/__gtu/__geu runtime helper - there is no
 * inline shortcut on either side for this sub-case, so MIR's identical
 * call-based codegen cannot be worse on correctness or performance grounds,
 * only on incidental prologue/stack-cleanup byte accounting. Forced
 * full-mode A/B (tlongopt.u_gtbig, tlongopt.u_lebig) confirmed this:
 * both pass with real cycle improvements and zero regressions despite a
 * generated-size that is not strictly smaller than legacy's. This getter
 * reports whether every wide-constant relational compare in the function
 * was this safe unsigned-only shape (no signed wide-constant relational
 * compare present, since that shape is not yet proven safe to admit on
 * static size alone). */
int mir_spilled_cfg_depends_only_on_unsigned_wide_constant_relational(void)
{
    return mir_spilled_cfg_used_unsigned_wide_constant_relational &&
           !mir_spilled_cfg_used_signed_wide_constant_relational;
}

int mir_spilled_cfg_depends_on_unary_not_branch_fusion(void)
{
    return mir_spilled_cfg_used_unary_not_branch_fusion;
}

int mir_spilled_cfg_depends_on_planned_stack_handoff(void)
{
    return mir_spilled_cfg_used_planned_stack_handoff;
}

int mir_spilled_cfg_depends_on_planned_index_base_handoff(void)
{
    return mir_spilled_cfg_used_planned_index_base_handoff;
}

int mir_spilled_cfg_depends_on_stable_pointer_local_home(void)
{
    return mir_spilled_cfg_used_stable_pointer_local_home;
}

int mir_spilled_cfg_depends_on_stable_pointer_local_slot(void)
{
    return mir_spilled_cfg_used_stable_pointer_local_slot;
}

int mir_spilled_cfg_depends_on_rhs_stack_forwarding(void)
{
    return mir_spilled_cfg_used_general_rhs_stack_forwarding;
}

int mir_spilled_cfg_depends_on_binary_load_pair_forwarding(void)
{
    return mir_spilled_cfg_used_binary_load_pair_forwarding;
}

int mir_spilled_cfg_depends_on_indirect_store_value_forwarding(void)
{
    return mir_spilled_cfg_indirect_store_value_forwarding_count != 0;
}

int mir_spilled_cfg_indirect_store_value_forwarding_uses(void)
{
    return mir_spilled_cfg_indirect_store_value_forwarding_count;
}

int mir_spilled_cfg_depends_on_branch_condition_forwarding(void)
{
    return mir_spilled_cfg_branch_condition_forwarding_count != 0;
}

int mir_spilled_cfg_branch_condition_forwarding_uses(void)
{
    return mir_spilled_cfg_branch_condition_forwarding_count;
}

int mir_spilled_cfg_depends_on_indirect_store_address_forwarding(void)
{
    return mir_spilled_cfg_indirect_store_address_forwarding_count != 0;
}

int mir_spilled_cfg_depends_on_promoted_local_slot_reuse(void)
{
    return mir_spilled_cfg_used_promoted_local_slot;
}

int mir_spilled_cfg_depends_on_wide_store_forwarding(void)
{
    return mir_spilled_cfg_used_wide_store_forwarding;
}

int mir_spilled_cfg_indirect_store_address_forwarding_uses(void)
{
    return mir_spilled_cfg_indirect_store_address_forwarding_count;
}

int mir_try_emit_spilled_scalar_cfg(FILE *out)
{
    int *labels;
    int frame_bytes;
    int i;
    int accepted = 0;
    int return_count = 0;
    int last_insn_is_return;
    int shared_epilogue_label = -1;

    mir_spilled_scalar_cfg_elided_epilogue_bytes = 0;
    mir_spilled_cfg_used_dead_store_forwarding = 0;
    mir_spilled_cfg_used_constant_absolute = 0;
    mir_spilled_cfg_used_constant_index_absolute = 0;
    mir_spilled_cfg_used_dynamic_index_base_forwarding = 0;
    mir_spilled_cfg_used_wide_constant_rematerialization = 0;
    mir_spilled_cfg_used_unsigned_wide_constant_relational = 0;
    mir_spilled_cfg_used_signed_wide_constant_relational = 0;
    mir_spilled_cfg_used_unary_not_branch_fusion = 0;
    mir_spilled_cfg_used_planned_stack_handoff = 0;
    mir_spilled_cfg_used_planned_index_base_handoff = 0;
    mir_spilled_cfg_used_stable_pointer_local_home = 0;
    mir_spilled_cfg_used_stable_pointer_local_slot = 0;
    mir_spilled_cfg_used_general_rhs_stack_forwarding = 0;
    mir_spilled_cfg_used_binary_load_pair_forwarding = 0;
    mir_spilled_cfg_indirect_store_value_forwarding_count = 0;
    mir_spilled_cfg_branch_condition_forwarding_count = 0;
    mir_spilled_cfg_indirect_store_address_forwarding_count = 0;
    mir_spilled_cfg_used_promoted_local_slot = 0;
    mir_spilled_cfg_used_wide_store_forwarding = 0;
    mir_planned_stack_handoffs_enabled = 0;
    mir_planned_stack_emit_count = 0;
    mir_planned_stack_consume_count = 0;
    mir_planned_stack_invalid = 0;
    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_RETURN)
            break;
    if (i == mir.count && (mir.return_type & 15) != TYPE_VOID)
        return mir_scalar_cfg_preflight_reject("implicit-return", -1);

    if ((!type_is_struct_object(mir.return_type) &&
            (mir.return_type & 15) != TYPE_VOID &&
         type_size(mir.return_type) > 4) ||
        (type_is_struct_object(mir.return_type) &&
         (type_size(mir.return_type) <= 0 || type_size(mir.return_type) > 1024)))
        return mir_scalar_cfg_preflight_reject("return-type", -1);
    mir_fuse_report_fused_count = 0;
    mir_fuse_report_materialized_count = 0;
    mir_backend_slots_skip_fused_comparisons = 1;
    mir_planned_stack_handoffs_enabled = 1;
    frame_bytes = mir_current_frame_bytes();
    mir_planned_stack_handoffs_enabled = 0;
    mir_backend_slots_skip_fused_comparisons = 0;
    mir_report_constant_absolute_addresses();
    if (getenv("DCC_MIR_SELECT_REPORT") != NULL)
        fprintf(stderr,
                "; MIR scalar-cfg frame function=%s locals=%d original-locals=%d"
                " slots=%d bytes=%d\n",
                mir.name,
                mir_effective_local_bytes() + mir.aggregate_temp_bytes,
                mir.local_bytes + mir.aggregate_temp_bytes,
                mir.backend_slot_count, frame_bytes);
    if (frame_bytes < 0 || frame_bytes > 30000)
        return mir_scalar_cfg_preflight_reject("frame-size", -1);
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
                if (insn->dst >= 0 && type_size(insn->type) > 4 &&
                        !(insn->opcode == MIR_PARAM &&
                            type_is_struct_object(insn->type)))
            return mir_scalar_cfg_preflight_reject("wide-value", i);
        switch (insn->opcode) {
        case MIR_NOP: case MIR_PARAM: case MIR_CONST: case MIR_FLOAT_CONST:
        case MIR_STRING_ADDRESS:
        case MIR_ADDRESS: case MIR_COMPOUND_ADDRESS: case MIR_INDEX_ADDRESS:
        case MIR_MEMBER_ADDRESS: case MIR_VLA_SIZE: case MIR_LOAD:
        case MIR_LOAD_INDIRECT:
        case MIR_STORE: case MIR_STORE_INDIRECT: case MIR_UNARY:
        case MIR_VLA_SAVE: case MIR_VLA_ALLOC: case MIR_VLA_RESTORE:
        case MIR_COPY_AGGREGATE: case MIR_VA_START: case MIR_VA_END:
        case MIR_VA_ARG:
        case MIR_BINARY: case MIR_ARG: case MIR_CALL: case MIR_CALL_AGGREGATE:
        case MIR_LABEL:
        case MIR_JUMP: case MIR_BRANCH_FALSE: case MIR_PHI: case MIR_RETURN:
            break;
        default:
            return mir_scalar_cfg_preflight_reject("opcode", i);
        }

        if (insn->opcode == MIR_LOAD || insn->opcode == MIR_STORE ||
            insn->opcode == MIR_PARAM || insn->opcode == MIR_ADDRESS) {
            int memory_type;
            int memory_storage;
            int memory_offset;
            if (!mir_scalar_memory_location(insn, &memory_type,
                            &memory_storage, &memory_offset) ||
                (memory_storage != SC_LOCAL && memory_storage != SC_PARAM &&
                 memory_storage != SC_GLOBAL && memory_storage != SC_EXTERN &&
                 memory_storage != SC_FUNC)) {
                if (getenv("DCC_MIR_SELECT_REPORT") != NULL)
                    fprintf(stderr,
                            "; MIR unresolved-memory function=%s insn=%d opcode=%s name=%s object=%d\n",
                            mir.name, i, mir_opcode_name(insn->opcode),
                            insn->name, insn->object);
                return mir_scalar_cfg_preflight_reject("memory-location", i);
            }
        }
           if ((insn->opcode == MIR_LOAD_INDIRECT ||
               insn->opcode == MIR_STORE_INDIRECT) &&
            (insn->memory_size <= 0 ||
             (insn->memory_size != 1 && insn->memory_size != 2 &&
              insn->memory_size != 4) ||
             (insn->bit_width > 0 && insn->memory_size != 2)))
            return mir_scalar_cfg_preflight_reject("indirect-width", i);
        if (insn->opcode == MIR_CALL &&
            ((strcmp(insn->name, "<indirect>") == 0 && insn->src1 < 0) ||
             type_size(insn->type) > 4))
            return mir_scalar_cfg_preflight_reject("call-abi", i);
        if (insn->opcode == MIR_CALL_AGGREGATE &&
            (strcmp(insn->name, "<indirect>") == 0 ||
             insn->memory_size <= 0))
            return mir_scalar_cfg_preflight_reject("aggregate-call-abi", i);
        if (insn->opcode == MIR_VA_ARG &&
            (insn->immediate < -128 || insn->immediate + 1 > 127 ||
             (insn->secondary_offset != 2 && insn->secondary_offset != 4)))
            return mir_scalar_cfg_preflight_reject("va-arg", i);
    }
    if ((getenv("DCC_MIR_UNUSED_SLOT_REPORT") != NULL ||
         getenv("DCC_MIR_SLOT_ACCESS_REPORT") != NULL) &&
        mir.next_value > 0) {
        mir_backend_slot_accessed =
            (unsigned char *)calloc((size_t)mir.next_value, 1);
        if (mir_backend_slot_accessed == NULL)
            fatal("out of memory allocating MIR slot access report");
    }
    labels = (int *)malloc((size_t)mir.next_label * sizeof(*labels));
    if (labels == NULL)
        fatal("out of memory selecting MIR labels");
    for (i = 0; i < mir.next_label; ++i)
        labels[i] = new_label();

    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    if (frame_bytes != 0)
        fprintf(out, "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n", frame_bytes);
    /* Item T1 (mir-text-size-plan.md): a frame this large guarantees some
     * local/param/backend-slot offset falls outside the Z80's signed
     * 8-bit (ix+d) range (-128..127) - every access to it would otherwise
     * pay a 6-instruction push-ix/pop-hl/ld-de,N/add-hl,de recompute on
     * EVERY read and write. Establish the virtual-iy frame base (iy =
     * ix - (local_bytes + aggregate_temp_bytes), the boundary between
     * locals and backend spill slots) so those accesses can use a small,
     * bounded iy-relative offset instead. Every read/write site, the
     * defensive post-call restore, and every epilogue already check
     * mir_virtual_iy_base and fall back to the pre-existing behavior when
     * it's unset or when even the iy-relative offset is still out of
     * range - this can only ever help or be neutral for any single
     * access, never regress one. push iy here saves the caller's iy in
     * exactly the stack position mir_emit_virtual_iy_epilogue's own
     * "frame_bytes + 2" math already expects when restoring it. */
    mir_virtual_iy_base = frame_bytes > 140;
    mir_virtual_iy_frame_bytes = frame_bytes;
    if (mir_virtual_iy_base) {
        fputs("\tpush iy\n", out);
        mir_emit_restore_virtual_iy(out);
    }
    mir_forwarded_hl_value = -1;
    mir_forwarded_hl_instruction = -1;
    mir_forwarded_wide_value = -1;
    mir_forwarded_wide_instruction = -1;
    mir_forwarded_stack_value = -1;
    mir_forwarded_stack_instruction = -1;
    mir_forwarded_stack_target_instruction = -1;
    mir_forwarded_wide_stack_value = -1;
    mir_forwarded_wide_stack_consumer = -1;
    mir_cached_call_value = -1;
    mir_cached_call_instruction = -1;
    mir_cached_wide_call_value = -1;
    mir_cached_wide_call_instruction = -1;
    mir_prepacked_call_instruction = -1;
    mir_prepacked_after_argument = -1;
    mir_prepacked_result_value = -1;
    mir_constant_argument_prepack_count = 0;
    /* mir-text-size Item T14: a function with more than one MIR_RETURN
     * currently duplicates the full epilogue (ix/iy/sp restore + ret,
     * several instructions) at every return site, unlike legacy's own
     * backend, which emits the epilogue once and has every early return
     * jump forward to it (a single `jp` is smaller than a duplicated
     * epilogue whenever there is more than one exit). Precompute whether
     * that sharing applies: if the function's last MIR instruction is
     * itself a MIR_RETURN, that occurrence keeps the real epilogue text
     * (as today) and becomes the target every earlier return jumps to;
     * otherwise the already-existing fall-off-the-end epilogue below
     * becomes that target. The label is only allocated (further down,
     * on first actual use) when return_count > 1, so a function with a
     * single return - the common case - emits byte-for-byte identical
     * code to before this change. */
    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_RETURN)
            ++return_count;
    last_insn_is_return =
        mir.count > 0 && mir.insns[mir.count - 1].opcode == MIR_RETURN;
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        int end_label;

        mir_emit_instruction_index = i;
        mir_emit_prepacked_constant_arguments(out, i);

        switch (insn->opcode) {
        case MIR_NOP:
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
        case MIR_PHI:
            break;
        case MIR_PARAM:
        case MIR_LOAD:
            {
            int memory_type;
            int memory_storage;
            int memory_offset;
            if (mir_forwarded_hl_value == insn->dst &&
                mir_forwarded_hl_instruction + 1 == mir_emit_instruction_index) {
                /* Item T84 (mir-text-size-plan.md): the preceding real
                 * MIR_STORE case armed this handoff after writing this
                 * exact object's value to memory - HL still holds it, so
                 * this fresh MIR_LOAD of the same object (e.g. `global +=
                 * rhs; return global;`) is a provably redundant re-fetch.
                 * Skip the memory read entirely and just re-persist
                 * whatever this value needs (a slot, or a further
                 * forwarding handoff), exactly like any other HL-resident
                 * value. */
                mir_forwarded_hl_value = -1;
                mir_forwarded_hl_instruction = -1;
                mir_emit_virtual_store(out, insn->dst);
                break;
            }
            if ((insn->opcode == MIR_PARAM || insn->opcode == MIR_LOAD) &&
                mir_value_has_direct_named_home(insn->dst))
                /* mir-migration-plan-next10 (extended by Item T27 to also
                 * cover MIR_LOAD): this value never gets a backend slot
                 * (see mir_value_has_direct_named_home) - every real use
                 * reloads directly from the stable named IX-relative home
                 * (mir_emit_virtual_load[_wide]), so the load this
                 * instruction would otherwise perform here is dead work;
                 * skip it entirely rather than load-then-discard. */
                break;
            if (insn->dst >= 0 && mir.backend_slots != NULL &&
                mir.backend_slots[insn->dst] < 0 &&
                (!mir_value_has_use(insn->dst) ||
                 mir_value_only_used_by_dead_unary(insn->dst) ||
                 mir_value_is_selfstore_incdec_source(insn->dst)))
                break;
            if ((type_size(insn->type) == 2 || type_size(insn->type) == 4) &&
                mir_load_is_single_call_argument(insn->dst,
                                                  type_size(insn->type)))
                break;
            if (mir_value_only_used_by_stable_pointer_argument(insn->dst))
                break;
            if (type_size(insn->type) == 2 &&
                mir_global_load_is_single_call_argument(insn->dst, 2))
                break;
            if (type_size(insn->type) == 2 &&
                mir_load_is_single_indirect_call_target(insn->dst, 2))
                break;
            if (!mir_scalar_memory_location(insn, &memory_type,
                                            &memory_storage, &memory_offset))
                goto done;
            if (insn->memory_size > 0 &&
                !type_is_struct_object(insn->type))
                memory_type = insn->type;
            if (type_is_struct_object(memory_type)) {
                if (memory_storage == SC_GLOBAL ||
                    memory_storage == SC_EXTERN) {
                    struct Sym *global = find_global(insn->name);
                    const char *assembly_name = asm_name_for(
                        global != NULL ? sym_asm_name(global) : insn->name);
                    if (memory_storage == SC_EXTERN &&
                        mir_extrn_should_emit(global))
                        fprintf(out, "\textrn %s\n", assembly_name);
                    fprintf(out, "\tld hl,%s\n", assembly_name);
                } else {
                    fputs("\tpush ix\n\tpop hl\n", out);
                    mir_emit_hl_offset_from_ix(out, memory_offset);
                }
                mir_emit_virtual_store(out, insn->dst);
                break;
            }
            if ((memory_storage == SC_LOCAL || memory_storage == SC_PARAM) &&
                (memory_offset < -128 ||
                 memory_offset + type_size(memory_type) - 1 > 127)) {
                fputs("\tpush ix\n\tpop hl\n", out);
                fprintf(out, "\tld de,%d\n\tadd hl,de\n", memory_offset);
                if (type_size(memory_type) == 4) {
                    fputs("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
                          "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
                          "\tld h,b\n\tld l,c\n", out);
                    mir_emit_virtual_store_wide(out, insn->dst);
                } else {
                    fputs("\tld a,(hl)\n", out);
                    if (type_size(memory_type) == 2)
                        fputs("\tinc hl\n\tld h,(hl)\n\tld l,a\n", out);
                    else if (type_is_bool(memory_type)) {
                        int bool_label = new_label();
                        fputs("\tor a\n\tld hl,0\n", out);
                        fprintf(out, "\tjp z, L%d\n\tinc hl\nL%d:\n",
                                bool_label, bool_label);
                    } else if ((memory_type & TYPE_UNSIGNED) != 0)
                        fputs("\tld l,a\n\tld h,0\n", out);
                    else {
                        fputs("\tld l,a\n", out);
                        mir_emit_signed_byte_extend(out);
                    }
                    mir_emit_virtual_store(out, insn->dst);
                }
                break;
            }
            if (memory_storage == SC_GLOBAL || memory_storage == SC_EXTERN ||
                memory_storage == SC_FUNC) {
                struct Sym *global = find_global(insn->name);
                const char *assembly_name = asm_name_for(
                    global != NULL ? sym_asm_name(global)
                                   : mir_declared_link_name(insn->name));
                if ((memory_storage == SC_EXTERN ||
                     (memory_storage == SC_FUNC && global != NULL &&
                      global->needs_extrn)) &&
                    mir_extrn_should_emit(global))
                    fprintf(out, "\textrn %s\n", assembly_name);
                if (type_size(memory_type) == 4 &&
                    memory_storage == SC_EXTERN)
                    fprintf(out,
                            "\tld hl,%s\n"
                            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
                            "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
                            "\tld h,b\n\tld l,c\n",
                            assembly_name);
                else if (type_size(memory_type) == 4)
                    fprintf(out, "\tld hl,(%s)\n\tld de,(%s+2)\n",
                            assembly_name, assembly_name);
                else if (type_size(memory_type) == 1)
                    fprintf(out, "\tld a,(%s)\n\tld l,a\n", assembly_name);
                else
                    fprintf(out, "\tld hl,(%s)\n", assembly_name);
            } else {
                fprintf(out, "\tld l,(ix%+d)\n", memory_offset);
            }
            if (type_size(memory_type) == 4) {
                if (memory_storage != SC_GLOBAL && memory_storage != SC_EXTERN)
                    fprintf(out,
                            "\tld h,(ix%+d)\n\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
                            memory_offset + 1, memory_offset + 2,
                            memory_offset + 3);
                mir_emit_virtual_store_wide(out, insn->dst);
                break;
            }
            if (type_size(memory_type) == 1) {
                if (type_is_bool(memory_type)) {
                    int bool_label = new_label();
                    fputs("\tld a,l\n\tor a\n\tld hl,0\n", out);
                    fprintf(out, "\tjp z, L%d\n\tinc hl\nL%d:\n",
                            bool_label, bool_label);
                } else if ((memory_type & TYPE_UNSIGNED) != 0) {
                    fputs("\tld h,0\n", out);
                } else {
                    mir_emit_signed_byte_extend(out);
                }
            } else if (memory_storage != SC_GLOBAL &&
                       memory_storage != SC_EXTERN) {
                fprintf(out, "\tld h,(ix%+d)\n", memory_offset + 1);
            }
            mir_emit_virtual_store(out, insn->dst);
            break;
            }
        case MIR_CONST:
            if (!mir_value_has_use(insn->dst) ||
                mir_scalar_constant_is_rematerializable(insn->dst) ||
                mir_wide_constant_is_rematerializable(insn->dst) ||
                mir_call_only_constant(insn->dst) ||
                mir_binary_only_constant(insn->dst) ||
                mir_multiply_by_small_constant(insn->dst) ||
                mir_value_only_used_by_dead_stores(insn->dst) ||
                mir_value_only_used_by_dead_unary(insn->dst) ||
                mir_index_only_constant(insn->dst))
                break;
            fprintf(out, "\tld hl,%ld\n", insn->immediate & 0xffffL);
            if (type_size(insn->type) == 4) {
                fprintf(out, "\tld de,%lu\n",
                        ((unsigned long)insn->immediate >> 16) & 0xffffUL);
                mir_emit_virtual_store_wide(out, insn->dst);
            } else {
                mir_emit_virtual_store(out, insn->dst);
            }
            break;
        case MIR_FLOAT_CONST:
            if (mir_call_only_constant(insn->dst) ||
                mir_wide_constant_is_rematerializable(insn->dst))
                break;
            fprintf(out, "\tld hl,%lu\n\tld de,%lu\n",
                    (unsigned long)insn->immediate & 0xffffUL,
                    ((unsigned long)insn->immediate >> 16) & 0xffffUL);
            mir_emit_virtual_store_wide(out, insn->dst);
            break;
        case MIR_STRING_ADDRESS:
            if (mir_call_only_constant(insn->dst) ||
                mir_string_address_is_rematerializable(insn->dst))
                break;
            fprintf(out, "\tld hl,S%ld\n", insn->immediate);
            mir_emit_virtual_store(out, insn->dst);
            break;
        case MIR_ADDRESS:
            if (mir_address_is_single_call_argument(insn->dst))
                break;
            if (mir_value_only_used_by_constant_absolute_address(insn->dst))
                break;
            if (mir_address_is_rematerializable(insn->dst))
                break;
            {
            if (!mir_emit_address_to_hl(out, insn))
                goto done;
            mir_emit_virtual_store(out, insn->dst);
            break;
            }
        case MIR_COMPOUND_ADDRESS:
            fputs("\tpush ix\n\tpop hl\n", out);
            mir_emit_hl_offset_from_ix(out, (int)insn->immediate);
            mir_emit_virtual_store(out, insn->dst);
            break;
        case MIR_VLA_SIZE:
            if (insn->immediate < -128 || insn->immediate + 1 > 127)
                goto done;
            fprintf(out, "\tld l,(ix%+ld)\n\tld h,(ix%+ld)\n",
                    insn->immediate, insn->immediate + 1);
            mir_emit_virtual_store(out, insn->dst);
            break;
        case MIR_MEMBER_ADDRESS:
            if (mir_value_only_used_by_constant_absolute_address(insn->dst))
                break;
            if (mir_value_only_used_by_stable_pointer_argument(insn->dst))
                break;
            if (mir_emit_constant_absolute_address_value(out, insn))
                break;
            mir_emit_virtual_load(out, insn->src1);
            if (insn->immediate != 0)
                fprintf(out, "\tld de,%ld\n\tadd hl,de\n", insn->immediate);
            mir_emit_virtual_store(out, insn->dst);
            break;
        case MIR_INDEX_ADDRESS:
            {
            const struct MirInsn *index_definition =
                mir_definition(insn->src2);
            if (mir_value_only_used_by_constant_absolute_address(insn->dst))
                break;
            if (mir_emit_constant_absolute_address_value(out, insn))
                break;
            if (insn->base_name[0] != 0) {
                int stride_type;
                int stride_storage;
                int stride_offset;
                if (!mir_declared_location(insn->base_name, &stride_type,
                                           &stride_storage, &stride_offset))
                    goto done;
                mir_emit_virtual_load(out, insn->src2);
                fputs("\tpush hl\n", out);
                if (stride_storage == SC_GLOBAL ||
                    stride_storage == SC_EXTERN)
                    fprintf(out, "\tld de,(%s)\n", asm_name_for(
                        mir_declared_link_name(insn->base_name)));
                else
                    fprintf(out, "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
                            stride_offset, stride_offset + 1);
                fputs("\tpop hl\n", out);
                mir_emit_runtime_call(out, "__mulu");
                if (insn->secondary_offset == 2)
                    fputs("\tadd hl,hl\n", out);
                else if (insn->secondary_offset > 2) {
                    unsigned long secondary_multiplier =
                        (unsigned long)insn->secondary_offset & 0xffffUL;
                    if (mir_mul_const_fast_path_eligible(
                            secondary_multiplier, insn->dst))
                        mir_emit_mul_hl_const(out, secondary_multiplier);
                    else {
                        fprintf(out, "\tld de,%d\n", insn->secondary_offset);
                        mir_emit_runtime_call(out, "__mulu");
                    }
                }
                fputs("\tpush hl\n", out);
                mir_emit_virtual_load(out, insn->src1);
                fputs("\tpop de\n\tadd hl,de\n", out);
                mir_emit_virtual_store(out, insn->dst);
                break;
            }
            if (index_definition != NULL &&
                index_definition->opcode == MIR_CONST) {
                long byte_offset = index_definition->immediate *
                                   insn->immediate;
                if (mir_forwarded_hl_value == insn->src2 &&
                    mir_forwarded_hl_instruction + 1 == i) {
                    mir_forwarded_hl_value = -1;
                    mir_forwarded_hl_instruction = -1;
                }
                if (mir_consume_planned_stack(
                        out, insn->src1, i, "hl")) {
                    /* The planned base is now in HL. */
                } else if (mir_forwarded_stack_value == insn->src1 &&
                           mir_forwarded_stack_target_instruction == i) {
                    fputs("\tpop hl\n", out);
                    mir_forwarded_stack_value = -1;
                    mir_forwarded_stack_instruction = -1;
                    mir_forwarded_stack_target_instruction = -1;
                } else
                    mir_emit_virtual_load(out, insn->src1);
                if (byte_offset != 0)
                    fprintf(out, "\tld de,%ld\n\tadd hl,de\n",
                            byte_offset & 0xffffL);
            } else {
                mir_emit_virtual_load(out, insn->src2);
                if (insn->immediate != 1) {
                    unsigned long index_multiplier =
                        (unsigned long)insn->immediate & 0xffffUL;
                    if (mir_mul_const_fast_path_eligible(
                            index_multiplier, insn->dst))
                        mir_emit_mul_hl_const(out, index_multiplier);
                    else {
                        fprintf(out, "\tld de,%ld\n", insn->immediate);
                        mir_emit_runtime_call(out, "__mulu");
                    }
                }
                if (mir_consume_planned_stack(
                        out, insn->src1, i, "de")) {
                    fputs("\tadd hl,de\n", out);
                } else if (mir_forwarded_stack_value == insn->src1 &&
                           mir_forwarded_stack_target_instruction == i) {
                    fputs("\tpop de\n\tadd hl,de\n", out);
                    mir_forwarded_stack_value = -1;
                    mir_forwarded_stack_instruction = -1;
                    mir_forwarded_stack_target_instruction = -1;
                } else {
                    fputs("\tpush hl\n", out);
                    mir_emit_virtual_load(out, insn->src1);
                    fputs("\tpop de\n\tadd hl,de\n", out);
                }
            }
            mir_emit_virtual_store(out, insn->dst);
            }
            break;
        case MIR_LOAD_INDIRECT:
            if (mir_indirect_load_is_single_stable_pointer_call_argument(
                    insn->dst, 2))
                break;
            if (mir_emit_constant_absolute_load(out, insn))
                break;
            mir_emit_virtual_load(out, insn->src1);
            if (insn->memory_size == 4) {
                fputs("\tpush hl\n\tld a,(hl)\n\tinc hl\n"
                      "\tld h,(hl)\n\tld l,a\n\tex (sp),hl\n"
                      "\tinc hl\n\tinc hl\n\tld e,(hl)\n\tinc hl\n"
                      "\tld d,(hl)\n\tpop hl\n", out);
            } else if (insn->memory_size == 1) {
                fputs("\tld l,(hl)\n", out);
                if (type_is_bool(insn->type)) {
                    end_label = new_label();
                    fputs("\tld a,l\n\tor a\n\tld hl,0\n", out);
                    fprintf(out, "\tjp z, L%d\n\tinc hl\nL%d:\n",
                            end_label, end_label);
                } else if ((insn->type & TYPE_UNSIGNED) != 0) {
                    fputs("\tld h,0\n", out);
                } else {
                    mir_emit_signed_byte_extend(out);
                }
            } else {
                fputs("\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n", out);
            }
            if (insn->bit_width > 0)
                mir_emit_bitfield_extract(out, insn);
            if (insn->memory_size == 4)
                mir_emit_virtual_store_wide(out, insn->dst);
            else
                mir_emit_virtual_store(out, insn->dst);
            break;
        case MIR_STORE:
            {
            int memory_type;
            int memory_storage;
            int memory_offset;
            const struct MirInsn *producer = mir_definition(insn->src1);
            if (producer != NULL && producer->opcode == MIR_BINARY) {
                int producer_index = (int)(producer - mir.insns);
                int selfstore_store_index;
                if (mir_binary_is_selfstore_incdec(producer_index,
                                                   &selfstore_store_index) &&
                    selfstore_store_index == i)
                    break;
            }
            if (mir_object_is_fully_promoted(insn->object) ||
                mir_store_is_dead(i))
                break;
            if (!mir_scalar_memory_location(insn, &memory_type,
                                            &memory_storage, &memory_offset))
                goto done;
            if (insn->memory_size > 0 &&
                !type_is_struct_object(insn->type))
                memory_type = insn->type;
            if (type_is_struct_object(memory_type)) {
                int size = type_size(memory_type);
                mir_emit_virtual_load(out, insn->src1);
                fputs("\tex de,hl\n", out);
                if (memory_storage == SC_GLOBAL ||
                    memory_storage == SC_EXTERN) {
                    struct Sym *global = find_global(insn->name);
                    const char *assembly_name = asm_name_for(
                        global != NULL ? sym_asm_name(global)
                                       : mir_declared_link_name(insn->name));
                    if (memory_storage == SC_EXTERN &&
                        mir_extrn_should_emit(global))
                        fprintf(out, "\textrn %s\n", assembly_name);
                    fprintf(out, "\tld hl,%s\n", assembly_name);
                } else {
                    fputs("\tpush ix\n\tpop hl\n", out);
                    if (memory_offset != 0)
                        fprintf(out, "\tld bc,%d\n\tadd hl,bc\n",
                                memory_offset);
                }
                /* mir-text-size Item T6: the two `ex de,hl` swaps above
                 * leave DE=source, HL=dest; swap once more (HL=source,
                 * DE=dest, a pure register exchange with no stack/frame
                 * side effects) and copy the whole struct with one
                 * `ldir` instead of an unrolled byte-by-byte loop - the
                 * same fix as Item T5's MIR_RETURN case, applied here to
                 * struct assignment/store. */
                if (size > 0) {
                    fputs("\tex de,hl\n", out);
                    fprintf(out, "\tld bc,%d\n\tldir\n", size);
                }
                break;
            }
            if ((memory_storage == SC_LOCAL || memory_storage == SC_PARAM) &&
                (memory_offset < -128 ||
                 memory_offset + type_size(memory_type) - 1 > 127)) {
                if (type_size(memory_type) == 4) {
                    mir_emit_virtual_load_wide(out, insn->src1);
                    fputs("\tpush de\n\tpush hl\n\tpush ix\n\tpop hl\n", out);
                    fprintf(out, "\tld de,%d\n\tadd hl,de\n", memory_offset);
                    fputs("\tpop bc\n\tld (hl),c\n\tinc hl\n\tld (hl),b\n"
                          "\tinc hl\n\tpop bc\n\tld (hl),c\n\tinc hl\n\tld (hl),b\n",
                          out);
                } else {
                    mir_emit_virtual_load(out, insn->src1);
                    fputs("\tex de,hl\n\tpush ix\n\tpop hl\n", out);
                    fprintf(out, "\tld bc,%d\n\tadd hl,bc\n\tld (hl),e\n",
                            memory_offset);
                    if (type_size(memory_type) == 2)
                        fputs("\tinc hl\n\tld (hl),d\n", out);
                }
                break;
            }
            if (mir_wide_store_forwarding_enabled &&
                type_size(memory_type) == 4)
                mir_spilled_cfg_used_wide_store_forwarding = 1;
            else
                mir_emit_virtual_load(out, insn->src1);
            {
            int narrow_hl_preserving_store = 0;
            if (memory_storage == SC_GLOBAL || memory_storage == SC_EXTERN) {
                struct Sym *global = find_global(insn->name);
                const char *assembly_name = asm_name_for(
                    global != NULL ? sym_asm_name(global)
                                   : mir_declared_link_name(insn->name));
                if (memory_storage == SC_EXTERN &&
                    mir_extrn_should_emit(global))
                    fprintf(out, "\textrn %s\n", assembly_name);
                if (type_size(memory_type) == 4) {
                    mir_emit_virtual_load_wide(out, insn->src1);
                    if (memory_storage == SC_EXTERN)
                        fprintf(out,
                                "\tpush de\n\tpush hl\n\tld hl,%s\n"
                                "\tpop bc\n\tld (hl),c\n\tinc hl\n"
                                "\tld (hl),b\n\tinc hl\n\tpop bc\n"
                                "\tld (hl),c\n\tinc hl\n\tld (hl),b\n",
                                assembly_name);
                    else
                        fprintf(out, "\tld (%s),hl\n\tld (%s+2),de\n",
                                assembly_name, assembly_name);
                } else if (type_size(memory_type) == 1)
                    fprintf(out, "\tld a,l\n\tld (%s),a\n", assembly_name);
                else {
                    fprintf(out, "\tld (%s),hl\n", assembly_name);
                    /* Item T84 (mir-text-size-plan.md): this global/extern
                     * 2-byte store form reads HL out to memory without
                     * disturbing it - the same "narrow store, HL still
                     * resident" property Item T83 already relies on for
                     * the local/param in-range form below. */
                    narrow_hl_preserving_store = (type_size(memory_type) ==
                                                   2);
                }
            } else {
                if (type_size(memory_type) == 4) {
                    mir_emit_virtual_load_wide(out, insn->src1);
                    fprintf(out,
                            "\tld (ix%+d),l\n\tld (ix%+d),h\n"
                            "\tld (ix%+d),e\n\tld (ix%+d),d\n",
                            memory_offset, memory_offset + 1,
                            memory_offset + 2, memory_offset + 3);
                } else {
                    fprintf(out, "\tld (ix%+d),l\n", memory_offset);
                }
                if (type_size(memory_type) == 2) {
                    fprintf(out, "\tld (ix%+d),h\n", memory_offset + 1);
                    narrow_hl_preserving_store = 1;
                }
            }
            if (narrow_hl_preserving_store) {
                /* Item T83 (mir-text-size-plan.md): this in-range,
                 * narrow store never disturbs HL (it only reads L/H out
                 * into memory), so if insn->src1's very next consumer
                 * (immediately following this store, through any
                 * intervening MIR_NOP - the same skip
                 * mir_emit_virtual_store's own store-forwarding already
                 * relies on) is one of mir_can_forward_hl_to_next's
                 * recognized single-operand shapes, re-arm the one-shot
                 * HL forwarding handoff for it instead of forcing a slot
                 * reload. Closes a second-hop gap this store's own
                 * forwarded-into-it value could not reach on its own:
                 * mir_emit_virtual_store already skips *this* store's
                 * own separate write when insn->src1 forwards straight
                 * into it (forward_to_store), but once that forwarding
                 * is consumed here, nothing previously re-armed it for
                 * whatever reads insn->src1 again right after - typically
                 * the same object's own value being returned or passed
                 * as a call argument immediately after its assignment
                 * (`c = a + b; return c;`, `saved = a + b; return
                 * f(saved) - saved;`). Found via tests/tc89decl.c's
                 * timpreg and tests/tmirslot.c's cross_call. */
                if (mir_can_forward_hl_to_next(insn->src1)) {
                    mir_forwarded_hl_value = insn->src1;
                    mir_forwarded_hl_instruction = i;
                } else if (mir_can_forward_hl_to_call_argument_first_use(
                               insn->src1)) {
                    /* Same reasoning, but the next consumer is an
                     * ARG+CALL pair (tests/tmirslot.c's cross_call:
                     * `saved = a + b; return scale(saved) - saved;`)
                     * rather than a single-operand opcode. */
                    mir_forwarded_hl_value = insn->src1;
                    mir_forwarded_hl_instruction =
                        mir_call_argument_after_nops(i);
                } else {
                    /* Item T84 (mir-text-size-plan.md): distinct from the
                     * two re-arm checks above (which forward insn->src1,
                     * the value just written, into its *own* next SSA
                     * use) - this instead looks at whether the very next
                     * real instruction is a fresh MIR_LOAD of the *exact
                     * same memory location* this store just wrote
                     * (tests/tcaslv.c's apply_global_compound_param:
                     * `global_lhs += rhs; return global_lhs;` - the
                     * return reads global_lhs via a brand-new MIR_LOAD,
                     * not via src1's SSA id, since the object was
                     * genuinely reassigned). Globals/externs have no
                     * backend object id (insn->object == -1 for them -
                     * mir_scalar_memory_location falls back to resolving
                     * their location by declared name instead), so this
                     * re-resolves the candidate load's own location via
                     * the same helper used for the store itself and
                     * compares storage class + offset directly, rather
                     * than comparing insn->object (which only locals/
                     * params populate). A narrow HL-preserving store
                     * makes that reload provably redundant; arm
                     * forwarding keyed on the load's own dst value (case
                     * MIR_LOAD's own check consumes this). */
                    int forward_after_store = mir_forward_skip_target(i);
                    if (forward_after_store < mir.count &&
                        mir.insns[forward_after_store].opcode == MIR_LOAD &&
                        mir_same_scalar_memory_location(
                            insn, &mir.insns[forward_after_store])) {
                        mir_forwarded_hl_value =
                            mir.insns[forward_after_store].dst;
                        mir_forwarded_hl_instruction =
                            forward_after_store - 1;
                    }
                }
            }
            }
            break;
            }
        case MIR_STORE_INDIRECT:
            if (mir_emit_constant_absolute_store(out, insn))
                break;
            if (insn->bit_width > 0) {
                int shift;
                mir_emit_virtual_load(out, insn->src2);
                mir_emit_hl_and_const(
                    out, insn->bit_width >= 16
                        ? 0xffffU : (1U << insn->bit_width) - 1U);
                for (shift = 0; shift < insn->bit_shift; ++shift)
                    fputs("\tadd hl,hl\n", out);
                mir_emit_hl_and_const(out, insn->bit_mask);
                fputs("\tpush hl\n", out);
                mir_emit_virtual_load(out, insn->src1);
                fputs("\tpush hl\n\tld a,(hl)\n\tinc hl\n"
                      "\tld h,(hl)\n\tld l,a\n", out);
                mir_emit_hl_and_const(out, (~insn->bit_mask) & 0xffffU);
                fputs("\tpop bc\n\tpop de\n"
                      "\tld a,l\n\tor e\n\tld l,a\n"
                      "\tld a,h\n\tor d\n\tld h,a\n"
                      "\tex de,hl\n\tld h,b\n\tld l,c\n"
                      "\tld (hl),e\n\tinc hl\n\tld (hl),d\n", out);
                break;
            }
            if (insn->memory_size == 4) {
                mir_emit_virtual_load(out, insn->src1);
                fputs("\tpush hl\n", out);
                mir_emit_virtual_load_wide(out, insn->src2);
                fputs("\tpop bc\n\tpush de\n\tex de,hl\n"
                      "\tld h,b\n\tld l,c\n\tld (hl),e\n\tinc hl\n"
                      "\tld (hl),d\n\tinc hl\n\tpop de\n"
                      "\tld (hl),e\n\tinc hl\n\tld (hl),d\n", out);
                break;
            }
            if (mir_forwarded_stack_value == insn->src2 &&
                mir_forwarded_stack_target_instruction == i) {
                fputs("\tpop de\n", out);
                if (mir_consume_planned_stack(
                        out, insn->src1, i, "hl"))
                    ++mir_spilled_cfg_indirect_store_address_forwarding_count;
                else
                    mir_emit_virtual_load(out, insn->src1);
                fputs("\tld (hl),e\n", out);
                if (insn->memory_size > 1)
                    fputs("\tinc hl\n\tld (hl),d\n", out);
                mir_forwarded_stack_value = -1;
                mir_forwarded_stack_instruction = -1;
                mir_forwarded_stack_target_instruction = -1;
                break;
            }
            if (mir_planned_stack_matches_consumer(insn->src1, i) &&
                mir_planned_stack_is_emitted(insn->src1)) {
                mir_emit_virtual_load(out, insn->src2);
                fputs("\tex de,hl\n", out);
                if (!mir_consume_planned_stack(
                        out, insn->src1, i, "hl"))
                    mir_planned_stack_invalid = 1;
                ++mir_spilled_cfg_indirect_store_address_forwarding_count;
                fputs("\tld (hl),e\n", out);
                if (insn->memory_size > 1)
                    fputs("\tinc hl\n\tld (hl),d\n", out);
                break;
            }
            mir_emit_virtual_load(out, insn->src1);
            fputs("\tpush hl\n", out);
            mir_emit_virtual_load(out, insn->src2);
            fputs("\tex de,hl\n\tpop hl\n\tld (hl),e\n", out);
            if (insn->memory_size > 1)
                fputs("\tinc hl\n\tld (hl),d\n", out);
            break;
        case MIR_VLA_SAVE:
            fputs("\tld hl,0\n\tadd hl,sp\n", out);
            mir_emit_frame_word_store(out, (int)insn->immediate);
            break;
        case MIR_VLA_ALLOC:
            mir_emit_virtual_load(out, insn->src1);
            mir_emit_frame_word_store(out, insn->secondary_offset);
            fputs("\tex de,hl\n\tld hl,0\n\tadd hl,sp\n"
                  "\tor a\n\tsbc hl,de\n\tld sp,hl\n", out);
            if (opt_stack_check)
                mir_emit_runtime_call(out, "__stchk");
            fputs("\tld hl,0\n\tadd hl,sp\n", out);
            mir_emit_frame_word_store(out, (int)insn->immediate);
            break;
        case MIR_VLA_RESTORE:
            if (insn->immediate == 0)
                break;
            mir_emit_frame_word_load(out, (int)insn->immediate);
            fputs("\tld sp,hl\n", out);
            break;
        case MIR_COPY_AGGREGATE:
            {
                if (insn->memory_size <= 0 || insn->memory_size > 1024)
                    goto done;
                mir_emit_virtual_load(out, insn->src1);
                fputs("\tpush hl\n", out);
                mir_emit_virtual_load(out, insn->src2);
                /* mir-text-size Item T6: `insn->src2` (source) is already
                 * loaded into HL here - exactly what `ldir` needs - so
                 * just pop the saved destination address straight into
                 * DE (no need for the old ld b,h/ld c,l + second pop hl
                 * dance) and copy the whole struct in one instruction
                 * instead of an unrolled byte-by-byte loop. Same fix
                 * shape as Item T5's MIR_RETURN case. */
                fputs("\tpop de\n", out);
                fprintf(out, "\tld bc,%d\n\tldir\n", insn->memory_size);
            }
            break;
        case MIR_UNARY:
            /* mir-text-size Item T12: every unary op here (cast, +, -, ~,
             * !) is a pure value transform with no side effect beyond
             * producing insn->dst - if that result has no use (the common
             * "(void)param;" idiom used to silence unused-parameter
             * warnings in callback/visitor signatures), skip the whole
             * instruction, including the operand load, instead of loading
             * src1 into hl only to immediately discard it. */
            if (!mir_value_has_use(insn->dst))
                break;
            if (mir_unary_is_fusable_not_branch(i)) {
                const struct MirInsn *branch = &mir.insns[i + 1];
                int target;
                if (branch->label < 0 || branch->label >= mir.next_label)
                    goto done;
                target = mir_find_label(branch->label);
                if (target < 0)
                    goto done;
                mir_emit_virtual_load(out, insn->src1);
                fputs("\tld a,h\n\tor l\n", out);
                mir_spilled_cfg_used_unary_not_branch_fusion = 1;
                if (!mir_emit_conditional_branch_with_phi_copies(
                        out, labels, "z", i + 1, target, branch->label))
                    goto done;
                ++i;
                continue;
            }
            if (mir_value_is_wide(insn->src1))
                mir_emit_virtual_load_wide(out, insn->src1);
            else
                mir_emit_virtual_load(out, insn->src1);
            if (insn->immediate == 0) {
                const struct MirInsn *source = mir_definition(insn->src1);
                if (!mir_emit_cast(out, source != NULL ? source->type : 0,
                                   insn->type))
                    goto done;
            } else if (insn->immediate == '+') {
                /* Unary plus. */
            } else if (insn->immediate == '-') {
                if (type_is_float(insn->type)) {
                    fputs("\tld a,d\n\txor 128\n\tld d,a\n", out);
                } else if (mir_value_is_wide(insn->src1)) {
                    int carry_label = new_label();
                    fputs("\tld a,l\n\tcpl\n\tld l,a\n"
                          "\tld a,h\n\tcpl\n\tld h,a\n"
                          "\tld a,e\n\tcpl\n\tld e,a\n"
                          "\tld a,d\n\tcpl\n\tld d,a\n"
                          "\tinc hl\n\tld a,h\n\tor l\n", out);
                      fprintf(out, "\tjp nz, L%d\n\tinc de\nL%d:\n",
                            carry_label, carry_label);
                    } else
                    fputs("\txor a\n\tsub l\n\tld l,a\n\tsbc a,a\n\tsub h\n\tld h,a\n", out);
            } else if (insn->immediate == '~') {
                fputs("\tld a,l\n\tcpl\n\tld l,a\n\tld a,h\n\tcpl\n\tld h,a\n", out);
                if (mir_value_is_wide(insn->src1))
                    fputs("\tld a,e\n\tcpl\n\tld e,a\n"
                          "\tld a,d\n\tcpl\n\tld d,a\n", out);
            } else if (insn->immediate == '!') {
                int true_label = new_label();
                end_label = new_label();
                if (mir_value_is_wide(insn->src1))
                    fputs("\tld a,d\n\tor e\n\tor h\n\tor l\n\tld hl,0\n", out);
                else
                    fputs("\tld a,h\n\tor l\n\tld hl,0\n", out);
                fprintf(out, "\tjp z, L%d\n\tjp L%d\nL%d:\n\tinc hl\nL%d:\n",
                        true_label, end_label, true_label, end_label);
            } else {
                goto done;
            }
            if (type_size(insn->type) == 4)
                mir_emit_virtual_store_wide(out, insn->dst);
            else
                mir_emit_virtual_store(out, insn->dst);
            break;
        case MIR_BINARY:
            {
            int selfstore_store_index;
            int multiply_index;
            int addend_value;
            if (mir_float_multiply_is_fused(i))
                break;
            if (mir_float_madd_match(i, &multiply_index, &addend_value)) {
                const struct MirInsn *multiply = &mir.insns[multiply_index];
                mir_emit_virtual_load_wide(out, addend_value);
                fputs("\tpush de\n\tpush hl\n", out);
                mir_emit_virtual_load_wide(out, multiply->src1);
                fputs("\tpush de\n\tpush hl\n", out);
                mir_emit_virtual_load_wide(out, multiply->src2);
                mir_emit_runtime_call(out, "__fmaf");
                fputs("\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n", out);
                mir_emit_virtual_store_wide(out, insn->dst);
                break;
            }
            if (mir_binary_is_selfstore_incdec(i, &selfstore_store_index)) {
                int memory_type, memory_storage, memory_offset;
                mir_scalar_memory_location(mir_definition(insn->src1),
                                            &memory_type, &memory_storage,
                                            &memory_offset);
                if (memory_storage == SC_GLOBAL ||
                    memory_storage == SC_EXTERN) {
                    mir_emit_selfstore_incdec_global(
                        out, mir_definition(insn->src1), memory_storage,
                        insn->immediate == '+');
                    /* Item T85 (mir-text-size-plan.md): the global/extern
                     * fused inc/dec form above ends with "ld (name),hl" -
                     * HL still holds the freshly incremented/decremented
                     * value, exactly like T84's narrow global store. A
                     * following fresh MIR_LOAD of the same location
                     * (a static local's storage class is SC_GLOBAL, so
                     * `static int x; ...; x++; total += x;` produces a
                     * brand-new SSA load rather than reusing this
                     * MIR_BINARY's own dst - tests/tforblk.c's
                     * static_sibling_blocks) is provably redundant.
                     * Reuse the exact same forward-skip-and-match-
                     * location logic T84 uses after a narrow store. */
                    {
                        int forward_after_incdec =
                            mir_forward_skip_target(selfstore_store_index);
                        if (forward_after_incdec < mir.count &&
                            mir.insns[forward_after_incdec].opcode ==
                                MIR_LOAD &&
                            mir_same_scalar_memory_location(
                                mir_definition(insn->src1),
                                &mir.insns[forward_after_incdec])) {
                            mir_forwarded_hl_value =
                                mir.insns[forward_after_incdec].dst;
                            mir_forwarded_hl_instruction =
                                forward_after_incdec - 1;
                        }
                    }
                } else
                    mir_emit_selfstore_incdec(out, memory_offset,
                                              insn->immediate == '+');
                break;
            }
            }
            if (type_size(insn->secondary_offset) == 4) {
                int fuse_skip = mir_binary_is_fusable_comparison(i);
                int stack_forwarded_left =
                    mir_forwarded_wide_stack_value == insn->src1 &&
                    mir_forwarded_wide_stack_consumer == i;
                int stack_forwarded_right =
                    mir_forwarded_wide_stack_value == insn->src2 &&
                    mir_forwarded_wide_stack_consumer == i;
                if (stack_forwarded_right) {
                    mir_emit_virtual_load_wide(out, insn->src1);
                } else if (!stack_forwarded_left) {
                    mir_emit_virtual_load_wide(out, insn->src1);
                    fputs("\tpush de\n\tpush hl\n", out);
                }
                if (!stack_forwarded_right)
                    mir_emit_virtual_load_wide(out, insn->src2);
                if (!mir_emit_wide_operation(out, insn))
                    goto done;
                if (stack_forwarded_left || stack_forwarded_right) {
                    mir_forwarded_wide_stack_value = -1;
                    mir_forwarded_wide_stack_consumer = -1;
                }
                if (fuse_skip > 0) {
                    /* Item T2: mir_emit_wide_operation already leaves the
                     * comparison's 0/1 result in HL - skip the
                     * store/reload/retest round trip entirely. */
                    if (!mir_emit_fused_wide_comparison_branch(
                            out, labels, i, fuse_skip - 1))
                        goto done;
                    ++mir_fuse_report_fused_count;
                    i += fuse_skip;
                    continue;
                }
                if (type_size(insn->type) == 4)
                    mir_emit_virtual_store_wide(out, insn->dst);
                else
                    mir_emit_virtual_store(out, insn->dst);
            } else {
                int divmod_partner = mir_divmod_partner(i);
                const struct MirInsn *right_definition =
                    mir_definition(insn->src2);
                unsigned long multiplier = right_definition != NULL
                    ? (unsigned long)right_definition->immediate & 0xffffUL
                    : 0;
                /* Item T15: mir_can_forward_stack_to_binary_const already
                 * pushed the left operand at its definition site (the
                 * push/pop dance is unavoidable here anyway - see below) -
                 * skip the otherwise-redundant store/reload of that value
                 * that mir_emit_virtual_load would perform. */
                int planned_stack_forwarded_left =
                    mir_planned_stack_matches_consumer(insn->src1, i) &&
                    mir_planned_stack_is_emitted(insn->src1);
                int stack_forwarded_left =
                    mir_forwarded_stack_value == insn->src1 &&
                    mir_forwarded_stack_target_instruction == i;
                int planned_stack_left_in_hl = 0;
                /* Item T16: mir_can_forward_stack_to_binary_rhs already
                 * pushed the right operand at its definition site (only
                 * possible when the left operand is a plain constant -
                 * see that predicate) - the constant load below leaves
                 * HL exactly where it needs to be, so the right operand
                 * only needs a single `pop de` once reached, no store/
                 * reload/push/ex-de-hl dance at all. */
                int stack_forwarded_right =
                    mir_forwarded_stack_value == insn->src2 &&
                    mir_forwarded_stack_target_instruction == i;
                /* Item T50: set when the constant-loading logic below
                 * chose to pre-bias the RHS constant instead of loading
                 * it raw - both comparison consumers further down (the
                 * fused-branch emitter and the non-fused materialize
                 * path) need to know this to skip DE's own runtime
                 * xor-128 flip and only flip HL's sign bit. */
                int de_holds_biased_constant = 0;
                if (planned_stack_forwarded_left &&
                    (divmod_partner >= 0 ||
                     (insn->immediate == '*' &&
                      right_definition != NULL &&
                      right_definition->opcode == MIR_CONST &&
                      mir_mul_const_fast_path_eligible(
                          multiplier, insn->dst)) ||
                     (mir_binary_is_fusable_comparison(i) > 0 &&
                      (mir_fused_compare_is_const_zero_rhs(i) ||
                       mir_fused_compare_is_signed_zero_sign_test(i))))) {
                    /* Most binary instructions consume a forwarded lhs
                     * through their existing "load rhs / pop lhs" path.
                     * These direct-HL special cases need the lhs before
                     * that path, so pop the same planned value at this
                     * exact consumer and then continue as though it had
                     * been loaded normally. */
                    if (!mir_consume_planned_stack(
                            out, insn->src1, i, "hl"))
                        mir_planned_stack_invalid = 1;
                    planned_stack_forwarded_left = 0;
                    stack_forwarded_left = 0;
                    planned_stack_left_in_hl = 1;
                }
                if (mir_value_is_nested_truth_comparison_input(insn->dst))
                    break;
                if (mir_emit_nested_truth_comparison_branch(out, labels, i)) {
                    ++mir_fuse_report_fused_count;
                    ++i;
                    continue;
                }
                if (mir_binary_only_constant(insn->src1)) {
                    const struct MirInsn *constant =
                        mir_definition(insn->src1);
                    fprintf(out, "\tld hl,%ld\n",
                            constant->immediate & 0xffffL);
                } else if (!stack_forwarded_left &&
                           !planned_stack_forwarded_left &&
                           !planned_stack_left_in_hl)
                    mir_emit_virtual_load(out, insn->src1);
                if (divmod_partner >= 0) {
                    const struct MirInsn *other = &mir.insns[divmod_partner];
                    int modulo_value = insn->immediate == '%' ? insn->dst
                                                               : other->dst;
                    int division_value = insn->immediate == '/' ? insn->dst
                                                                 : other->dst;
                    int saved_instruction = mir_emit_instruction_index;
                    if (divmod_partner < i)
                        break;
                    fputs("\tpush hl\n", out);
                    mir_emit_virtual_load(out, insn->src2);
                    fputs("\tex de,hl\n\tpop hl\n", out);
                    if ((insn->secondary_offset & TYPE_UNSIGNED) != 0)
                        mir_emit_runtime_call(out, "__udivmod");
                    else
                        mir_emit_runtime_call(out, "__sdivmod");
                    mir_emit_instruction_index = -1;
                    fputs("\tpush hl\n\tex de,hl\n", out);
                    mir_emit_virtual_store(out, modulo_value);
                    fputs("\tpop hl\n", out);
                    mir_emit_virtual_store(out, division_value);
                    mir_emit_instruction_index = saved_instruction;
                    break;
                }
                if (insn->immediate == '*' && right_definition != NULL &&
                    right_definition->opcode == MIR_CONST &&
                    mir_mul_const_fast_path_eligible(multiplier, insn->dst)) {
                    mir_emit_mul_hl_const(out, multiplier);
                    mir_emit_virtual_store(out, insn->dst);
                    break;
                }
                /* Item T48 (mir-text-size-plan.md): `int_expr &
                 * <compile-time constant>` (e.g. `sq & 7`, `flags &
                 * MASK`) - mirrors legacy's emit_and_hl_const, ported
                 * for the wide path as mir_emit_word_and_constant in
                 * Item T47 and reused directly here (a scalar is just
                 * one 16-bit register pair). HL already holds src1's
                 * real value at this point (either freshly loaded above,
                 * or itself the constant in the degenerate `const &
                 * const` case, which is still correct either way) -
                 * unless stack_forwarded_left is set, in which case HL
                 * was deliberately left unloaded (the value is still on
                 * the stack, pending a later pop) and this fast path
                 * must not run. Matches legacy's exact scope: only the
                 * constant-as-syntactic-right-operand shape is
                 * recognized, since that is the only one
                 * emit_and_hl_const's own caller ever special-cases. */
                if (insn->immediate == '&' && !stack_forwarded_left &&
                    !planned_stack_forwarded_left &&
                    right_definition != NULL &&
                    right_definition->opcode == MIR_CONST) {
                    mir_emit_word_and_constant(out, 'h', 'l',
                                                (unsigned int)multiplier);
                    mir_emit_virtual_store(out, insn->dst);
                    break;
                }
                /* Item T49 (mir-text-size-plan.md): unsigned `int_expr /
                 * <compile-time power-of-2 constant>` -> a logical right
                 * shift, and unsigned `int_expr % <compile-time
                 * power-of-2 constant>` -> a mask of (divisor - 1),
                 * mirroring legacy's fast path in ast_gen_binary_ast
                 * (dcc_ast_gen_expr.c ~1551) instead of a __divu/__modu
                 * runtime call. Only applies when there is no fused
                 * divmod partner above (that existing optimization
                 * already takes priority when both a div and a mod of
                 * the same operands appear together) and only for
                 * unsigned types, matching legacy's exact scope: signed
                 * division's round-toward-zero semantics for negative
                 * dividends are not equivalent to a plain arithmetic
                 * shift, so legacy itself never special-cases the signed
                 * case either. HL already holds src1's real value here,
                 * same as the '&' fast path above, with the same
                 * !stack_forwarded_left guard. */
                if ((insn->immediate == '/' || insn->immediate == '%') &&
                    !stack_forwarded_left &&
                    !planned_stack_forwarded_left &&
                    (insn->type & TYPE_UNSIGNED) != 0 &&
                    right_definition != NULL &&
                    right_definition->opcode == MIR_CONST) {
                    int shift = mir_ulong_log2_pow2(multiplier);
                    if (shift >= 0) {
                        if (insn->immediate == '/')
                            mir_emit_scalar_shift_by_constant(out, TOK_SHR,
                                                              1, shift);
                        else
                            mir_emit_word_and_constant(
                                out, 'h', 'l',
                                (unsigned int)((multiplier - 1) & 0xffffUL));
                        mir_emit_virtual_store(out, insn->dst);
                        break;
                    }
                }
                /* A constant right-hand operand needs no dividend/left-
                 * operand-preserving push/pop dance: the left operand is
                 * already sitting in HL from src1's evaluation above and
                 * a constant *load* cannot clobber it (unlike src2 being
                 * evaluated via a call or memory access, which can), so
                 * the constant can be materialized directly into DE for
                 * *any* operator (Item 16 proved this for '/' and '%'
                 * only; Item T11 generalizes it to every operator, since
                 * the "constant load never clobbers HL" reasoning is not
                 * operator-specific).
                 *
                 * Skip this shortcut in functions with a VLA: shaving a
                 * few bytes off this one instruction can tip a borderline
                 * function's byte-size-based accept/fallback gate over to
                 * "mir accepted" (particularly under -fstack-check, whose
                 * extra call-site bytes shift the size comparison), and
                 * VLA frames lean on ix-relative slot traffic that is
                 * byte-cheap but T-state-expensive - the resulting switch
                 * from the legacy path to the MIR path can be a net cycle
                 * regression even though every individual instruction
                 * changed here is unambiguously cheaper in isolation. */
                if (stack_forwarded_right) {
                    /* Item T16: the right operand is already on the
                     * stack from its own definition site. Pop it into DE
                     * after loading the left operand; if that left operand
                     * also has a planned stack handoff, it is directly
                     * beneath the right operand and can now be popped into
                     * HL. */
                    fputs("\tpop de\n", out);
                    if (planned_stack_forwarded_left) {
                        if (!mir_consume_planned_stack(
                                out, insn->src1, i, "hl"))
                            mir_planned_stack_invalid = 1;
                        planned_stack_forwarded_left = 0;
                    }
                } else if (mir_binary_is_fusable_comparison(i) > 0 &&
                    mir_fused_compare_is_const_zero_rhs(i)) {
                    /* Item 25: this comparison will be fused directly into
                     * the following branch (mir_binary_is_fusable_comparison
                     * is the single source of truth for that) against the
                     * constant 0. Skip materializing that 0 into DE
                     * entirely; the fused branch emitter below tests HL
                     * directly with "ld a,h / or l" instead of
                     * "or a / sbc hl,de". */
                } else if (mir_binary_is_fusable_comparison(i) > 0 &&
                           mir_fused_compare_is_signed_zero_sign_test(i)) {
                    /* Item 27: same reasoning as Item 25 above, but for a
                     * signed `<`/`>=` comparison against the constant 0 -
                     * the fused branch emitter tests the sign bit of HL
                     * directly with "bit 7,h" instead of loading 0 into DE
                     * for a 16-bit sbc. */
                } else if (!mir.has_vla && !stack_forwarded_left &&
                           !planned_stack_forwarded_left &&
                           (insn->immediate == '<' ||
                            insn->immediate == TOK_GE) &&
                           right_definition != NULL &&
                           right_definition->opcode == MIR_CONST &&
                           (right_definition->immediate & 0xffffL) != 0 &&
                           (mir_definition(insn->src1) == NULL ||
                            !mir_type_uses_unsigned_comparison(
                                mir_definition(insn->src1)->type)) &&
                           !mir_type_uses_unsigned_comparison(
                               right_definition->type)) {
                    /* Item T50 (mir-text-size-plan.md): a signed `<`/`>=`
                     * comparison against a non-zero compile-time constant
                     * mirrors mir_emit_homed_binary_instruction's
                     * biased_right_constant optimization (previously
                     * MIR-only in that one selector) - pre-bias the
                     * constant at compile time (constant ^ 0x8000) so
                     * only HL's sign bit needs the runtime xor-128 flip
                     * below, not both HL and DE. The RHS==0 case is
                     * already handled more cheaply by Items 25/27 (bit 7
                     * test, no DE load at all) when fusable, or falls
                     * through to the plain path otherwise - excluded here
                     * to avoid overlapping logic. */
                    de_holds_biased_constant = 1;
                    fprintf(out, "\tld de,%ld\n",
                            (right_definition->immediate ^ 0x8000L) &
                                0xffffL);
                } else if (!mir.has_vla && !stack_forwarded_left &&
                           !planned_stack_forwarded_left &&
                           mir_binary_only_constant(insn->src2)) {
                    const struct MirInsn *constant =
                        mir_definition(insn->src2);
                    fprintf(out, "\tld de,%ld\n",
                            constant->immediate & 0xffffL);
                } else {
                    /* Item T15: when stack_forwarded_left, the left
                     * operand was already pushed at its definition site
                     * (mir_can_forward_stack_to_binary_const) instead of
                     * being loaded into HL just above - skip this
                     * redundant push, but still pop it back below. */
                    if (!stack_forwarded_left &&
                        !planned_stack_forwarded_left)
                        fputs("\tpush hl\n", out);
                    if (mir_binary_only_constant(insn->src2)) {
                        const struct MirInsn *constant =
                            mir_definition(insn->src2);
                        fprintf(out, "\tld hl,%ld\n",
                                constant->immediate & 0xffffL);
                    } else
                        mir_emit_virtual_load(out, insn->src2);
                    fputs("\tex de,hl\n", out);
                    if (planned_stack_forwarded_left) {
                        if (!mir_consume_planned_stack(
                                out, insn->src1, i, "hl"))
                            mir_planned_stack_invalid = 1;
                    } else {
                        fputs("\tpop hl\n", out);
                    }
                }
                if (stack_forwarded_left || stack_forwarded_right) {
                    mir_forwarded_stack_value = -1;
                    mir_forwarded_stack_instruction = -1;
                    mir_forwarded_stack_target_instruction = -1;
                }
                {
                    int fuse_skip = mir_binary_is_fusable_comparison(i);
                    if (fuse_skip > 0) {
                        if (!mir_emit_fused_comparison_branch(
                                out, labels, i, fuse_skip - 1,
                                de_holds_biased_constant))
                            goto done;
                        ++mir_fuse_report_fused_count;
                        i += fuse_skip;
                        continue;
                    }
                }
                switch ((int)insn->immediate) {
                case TOK_EQ: case TOK_NE: case '<': case '>':
                case TOK_LE: case TOK_GE:
                    ++mir_fuse_report_materialized_count;
                    break;
                default:
                    break;
                }
                if (de_holds_biased_constant) {
                    /* Item T50: bypass mir_emit_scalar_operation's
                     * generic comparison case (which assumes DE holds
                     * the raw constant and would xor-128 both HL and
                     * DE) - DE already holds the biased value. */
                    mir_emit_scalar_compare_biased_right(
                        out, (int)insn->immediate);
                } else if (!mir_emit_scalar_operation(out, insn))
                    goto done;
                mir_emit_virtual_store(out, insn->dst);
            }
            break;
        case MIR_ARG:
            break;
        case MIR_CALL:
            {
                struct Sym *callee = find_global(insn->name);
                int is_indirect = strcmp(insn->name, "<indirect>") == 0;
                const char *assembly_name = insn->base_name[0] != 0
                    ? insn->base_name
                    : asm_name_for(callee != NULL ? sym_asm_name(callee)
                                                  : insn->name);
                int call_arg_count = 0;
                int argument_bytes = 0;
                int argument;
                int scan;
                int dest_value, fill_value, count_value;
                int s_value, c_value;
                int s1_value, s2_value, n_value;
                int fn_value, dearg_value;
                const char *rtl_name;
                if (!is_indirect &&
                    mir_call_is_memset_fastcall(i, &dest_value, &fill_value,
                                                &count_value)) {
                    if (!mir_emit_cached_call_argument(out, dest_value) &&
                        !mir_emit_rematerialized_argument(out, dest_value, 2))
                        mir_emit_virtual_load(out, dest_value);
                    fputs("\tpush hl\n", out);
                    if (!mir_emit_cached_call_argument(out, fill_value) &&
                        !mir_emit_rematerialized_argument(out, fill_value, 2))
                        mir_emit_virtual_load(out, fill_value);
                    fputs("\tpush hl\n", out);
                    if (!mir_emit_cached_call_argument(out, count_value) &&
                        !mir_emit_rematerialized_argument(out, count_value,
                                                          2))
                        mir_emit_virtual_load(out, count_value);
                    fputs("\tld b,h\n\tld c,l\n\tpop de\n\tpop hl\n", out);
                    mir_emit_runtime_call(out, "__msf");
                    if (type_ptr_depth(insn->type) > 0 ||
                        (insn->type & 15) != TYPE_VOID) {
                        if (type_size(insn->type) == 4)
                            mir_emit_virtual_store_wide(out, insn->dst);
                        else
                            mir_emit_virtual_store(out, insn->dst);
                    }
                    break;
                }
                if (!is_indirect &&
                    mir_call_is_strlen_fastcall(i, &s_value)) {
                    mir_emit_spilled_arg_to_hl(out, s_value);
                    mir_emit_runtime_call(out, "__slf");
                    if (type_size(insn->type) == 4)
                        mir_emit_virtual_store_wide(out, insn->dst);
                    else
                        mir_emit_virtual_store(out, insn->dst);
                    break;
                }
                if (!is_indirect &&
                    mir_call_is_strchr_fastcall(i, &s_value, &c_value)) {
                    mir_emit_spilled_arg_to_hl(out, s_value);
                    fputs("\tpush hl\n", out);
                    mir_emit_spilled_arg_to_hl(out, c_value);
                    fputs("\tld a,l\n\tpop hl\n", out);
                    mir_emit_runtime_call(out, "__chf");
                    mir_emit_virtual_store(out, insn->dst);
                    break;
                }
                if (!is_indirect &&
                    mir_call_is_strrchr_fastcall(i, &s_value, &c_value)) {
                    mir_emit_spilled_arg_to_hl(out, s_value);
                    fputs("\tpush hl\n", out);
                    mir_emit_spilled_arg_to_hl(out, c_value);
                    fputs("\tld a,l\n\tpop hl\n", out);
                    mir_emit_runtime_call(out, "__rcf");
                    mir_emit_virtual_store(out, insn->dst);
                    break;
                }
                if (!is_indirect &&
                    mir_call_is_memchr_fastcall(i, &s_value, &c_value,
                                               &n_value)) {
                    mir_emit_spilled_arg_to_hl(out, s_value);
                    fputs("\tpush hl\n", out);
                    mir_emit_spilled_arg_to_hl(out, c_value);
                    fputs("\tpush hl\n", out);
                    mir_emit_spilled_arg_to_hl(out, n_value);
                    fputs("\tld b,h\n\tld c,l\n\tpop de\n\tpop hl\n", out);
                    mir_emit_runtime_call(out, "__mhf");
                    mir_emit_virtual_store(out, insn->dst);
                    break;
                }
                if (!is_indirect &&
                    mir_call_is_memcmp_fastcall(i, &s1_value, &s2_value,
                                               &n_value)) {
                    mir_emit_spilled_arg_to_hl(out, s1_value);
                    fputs("\tpush hl\n", out);
                    mir_emit_spilled_arg_to_hl(out, s2_value);
                    fputs("\tpush hl\n", out);
                    mir_emit_spilled_arg_to_hl(out, n_value);
                    fputs("\tld b,h\n\tld c,l\n\tpop hl\n\tpop de\n", out);
                    mir_emit_runtime_call(out, "__cmpf");
                    if (type_size(insn->type) == 4)
                        mir_emit_virtual_store_wide(out, insn->dst);
                    else
                        mir_emit_virtual_store(out, insn->dst);
                    break;
                }
                if (!is_indirect &&
                    mir_call_is_memcpy_fastcall(i, &dest_value, &fill_value,
                                               &n_value)) {
                    mir_emit_spilled_arg_to_hl(out, dest_value);
                    fputs("\tpush hl\n", out);
                    mir_emit_spilled_arg_to_hl(out, fill_value);
                    fputs("\tpush hl\n", out);
                    mir_emit_spilled_arg_to_hl(out, n_value);
                    fputs("\tld b,h\n\tld c,l\n\tpop hl\n\tpop de\n", out);
                    mir_emit_runtime_call(out, "__mcf");
                    if (type_ptr_depth(insn->type) > 0 ||
                        (insn->type & 15) != TYPE_VOID) {
                        if (type_size(insn->type) == 4)
                            mir_emit_virtual_store_wide(out, insn->dst);
                        else
                            mir_emit_virtual_store(out, insn->dst);
                    }
                    break;
                }
                if (!is_indirect &&
                    mir_call_is_de_hl_fastcall(i, &rtl_name, &s1_value,
                                              &s2_value)) {
                    /* Item T65c (mir-text-size-plan.md): s2 (the value
                     * that must end up in HL for the call, "the last-
                     * evaluated value needs no move") can already be
                     * sitting there via an earlier forwarding handoff -
                     * mir_emit_virtual_store's mir_can_forward_hl_to_
                     * call_argument path defers a value with no backend
                     * slot at all, leaving it in HL and arming
                     * mir_forwarded_hl_value/mir_forwarded_hl_instruction
                     * to match once this call instruction is reached.
                     * The usual order below (evaluate s1 first, clobbering
                     * HL, then s2) silently destroys that forwarded value
                     * before mir_emit_spilled_arg_to_hl(s2_value) ever
                     * gets to consume it - mir_emit_virtual_load only
                     * clears the forwarding state on a match, not when a
                     * non-matching reload (s1's) clobbers HL instead, so
                     * the later, stale-but-still-"matching" check for s2
                     * wrongly skips its own reload entirely. Confirmed via
                     * tests/adaint.c's var_or_const_decl
                     * (strcpy(names[nn++], G->text)): text's address (s2)
                     * was left dangling in HL, names[nn++]'s address (s1)
                     * silently overwrote it, and strcpy ran with both DE
                     * and HL pointing at the destination - a no-op self-
                     * copy. When s2 is the pending forwarded value, swap
                     * to a preserve-then-restore shape (push it before
                     * s1 is evaluated, then move s1 into DE and pop s2
                     * back into HL) instead of the naive order. */
                    if (mir_forwarded_hl_value == s2_value &&
                        mir_forwarded_hl_instruction + 1 ==
                            mir_emit_instruction_index) {
                        mir_forwarded_hl_value = -1;
                        mir_forwarded_hl_instruction = -1;
                        fputs("\tpush hl\n", out);
                        mir_emit_spilled_arg_to_hl(out, s1_value);
                        fputs("\tex de,hl\n\tpop hl\n", out);
                    } else {
                        mir_emit_spilled_arg_to_hl(out, s1_value);
                        fputs("\tpush hl\n", out);
                        mir_emit_spilled_arg_to_hl(out, s2_value);
                        fputs("\tpop de\n", out);
                    }
                    mir_emit_runtime_call(out, rtl_name);
                    if (type_ptr_depth(insn->type) > 0 ||
                        (insn->type & 15) != TYPE_VOID) {
                        if (type_size(insn->type) == 4)
                            mir_emit_virtual_store_wide(out, insn->dst);
                        else
                            mir_emit_virtual_store(out, insn->dst);
                    }
                    break;
                }
                if (!is_indirect &&
                    mir_call_is_bdos_family_fastcall(i, &rtl_name, &fn_value,
                                                    &dearg_value)) {
                    mir_emit_spilled_arg_to_hl(out, fn_value);
                    fputs("\tpush hl\n", out);
                    mir_emit_spilled_arg_to_hl(out, dearg_value);
                    fputs("\tex de,hl\n\tpop hl\n\tld c,l\n", out);
                    mir_emit_runtime_call(out, rtl_name);
                    if (type_ptr_depth(insn->type) > 0 ||
                        (insn->type & 15) != TYPE_VOID) {
                        if (type_size(insn->type) == 4)
                            mir_emit_virtual_store_wide(out, insn->dst);
                        else
                            mir_emit_virtual_store(out, insn->dst);
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
                    if (mir_prepacked_call_instruction == i &&
                        (arg->immediate >
                             mir_prepacked_after_argument ||
                         arg->src1 == mir_prepacked_result_value)) {
                        /* Already present below SP in normal reverse-ABI
                         * order; retain its bytes for caller cleanup. */
                        argument_bytes += size == 4 ? 4 : 2;
                        continue;
                    }
                    if (type_is_struct_object(arg->type)) {
                        int byte;
                        if (!mir_emit_cached_call_argument(out, arg->src1) &&
                            !mir_emit_rematerialized_argument(
                                out, arg->src1, 2))
                            mir_emit_virtual_load(out, arg->src1);
                        fputs("\tex de,hl\n", out);
                        fprintf(out,
                                "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n",
                                size);
                        /* mir-text-size Item T6: deliberately NOT
                         * switched to `ldir` here (unlike the
                         * MIR_RETURN/MIR_STORE/MIR_COPY_AGGREGATE
                         * sites) - see mir-text-size-plan.md's Item T6
                         * defer note. Byte-for-byte here still
                         * unrolls; this call-argument copy site is
                         * deferred pending a Root-Cause-C fix for a
                         * pre-existing redundant address-recomputation
                         * pattern that a real regression exposed when
                         * this site's byte count was reduced. */
                        for (byte = 0; byte < size; ++byte) {
                            fputs("\tld a,(de)\n\tld (hl),a\n", out);
                            if (byte + 1 < size)
                                fputs("\tinc de\n\tinc hl\n", out);
                        }
                        argument_bytes += size;
                    } else if (size == 4) {
                        int cached = mir_emit_cached_wide_call_argument(
                            out, arg->src1);
                        if (!cached && !mir_emit_rematerialized_argument(
                                out, arg->src1, size))
                            mir_emit_virtual_load_wide(out, arg->src1);
                        if (cached != 2)
                            fputs("\tpush de\n\tpush hl\n", out);
                        argument_bytes += 4;
                    } else {
                        if (!mir_emit_cached_call_argument_to_stack(
                                out, arg->src1)) {
                            if (!mir_emit_cached_call_argument(
                                    out, arg->src1) &&
                                !mir_emit_rematerialized_argument(
                                    out, arg->src1, size))
                                mir_emit_virtual_load(out, arg->src1);
                            fputs("\tpush hl\n", out);
                        }
                        argument_bytes += 2;
                    }
                }
                if (argument != -1)
                    goto done;
                if ((insn->memory_flags & MIR_CALL_FLAG_FORMAT_HEX) != 0)
                    mir_emit_runtime_call(out, "__pfehx");
                if ((insn->memory_flags & MIR_CALL_FLAG_FORMAT_OCTAL) != 0)
                    mir_emit_runtime_call(out, "__pfeoc");
                if (is_indirect) {
                    if (!mir_emit_rematerialized_argument(out, insn->src1, 2))
                        mir_emit_virtual_load(out, insn->src1);
                    mir_emit_runtime_call(out, "__call_hl");
                } else {
                    if ((callee == NULL || callee->needs_extrn) &&
                        mir_extrn_should_emit(callee))
                        fprintf(out, "\textrn %s\n", assembly_name);
                    fprintf(out, "\tcall %s\n", assembly_name);
                }
                if (is_indirect || callee == NULL || !callee->is_defined)
                    mir_emit_restore_virtual_iy(out);
                if ((argument_bytes & 1) != 0) {
                    if (type_ptr_depth(insn->type) > 0 ||
                        (insn->type & 15) != TYPE_VOID) {
                        if (type_size(insn->type) == 4)
                            mir_emit_virtual_store_wide(out, insn->dst);
                        else
                            mir_emit_virtual_store(out, insn->dst);
                    }
                    fprintf(out, "\tld hl,%d\n\tadd hl,sp\n\tld sp,hl\n",
                            argument_bytes);
                    if (type_ptr_depth(insn->type) > 0 ||
                        (insn->type & 15) != TYPE_VOID) {
                        if (type_size(insn->type) == 4)
                            mir_emit_virtual_load_wide(out, insn->dst);
                        else
                            mir_emit_virtual_load(out, insn->dst);
                    }
                } else {
                    for (argument = 0; argument < argument_bytes / 2;
                         ++argument)
                        fputs("\tpop bc\n", out);
                }
                if (type_ptr_depth(insn->type) > 0 ||
                    (insn->type & 15) != TYPE_VOID) {
                    if (type_size(insn->type) == 4)
                        mir_emit_virtual_store_wide(out, insn->dst);
                    else
                        mir_emit_virtual_store(out, insn->dst);
                }
                if (mir_prepacked_call_instruction == i) {
                    mir_prepacked_call_instruction = -1;
                    mir_prepacked_after_argument = -1;
                    mir_prepacked_result_value = -1;
                }
            }
            break;
        case MIR_CALL_AGGREGATE:
            {
                struct Sym *callee = find_global(insn->name);
                const char *assembly_name = asm_name_for(
                    callee != NULL ? sym_asm_name(callee) : insn->name);
                int call_arg_count = 0;
                int argument_bytes = 0;
                int argument;
                int scan;
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
                    if (type_is_struct_object(arg->type)) {
                        int byte;
                        if (size <= 0 || size > 1024)
                            goto done;
                        if (!mir_emit_cached_call_argument(out, arg->src1) &&
                            !mir_emit_rematerialized_argument(
                                out, arg->src1, 2))
                            mir_emit_virtual_load(out, arg->src1);
                        fputs("\tex de,hl\n", out);
                        fprintf(out, "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n",
                                size);
                        /* mir-text-size Item T6: deliberately NOT
                         * switched to `ldir` here - see the matching
                         * defer note in the MIR_CALL struct-argument
                         * case above and mir-text-size-plan.md's Item
                         * T6 defer rationale. */
                        for (byte = 0; byte < size; ++byte) {
                            fputs("\tld a,(de)\n\tld (hl),a\n", out);
                            if (byte + 1 < size)
                                fputs("\tinc de\n\tinc hl\n", out);
                        }
                        argument_bytes += size;
                    } else if (size == 4) {
                        int cached = mir_emit_cached_wide_call_argument(
                            out, arg->src1);
                        if (!cached && !mir_emit_rematerialized_argument(
                                out, arg->src1, size))
                            mir_emit_virtual_load_wide(out, arg->src1);
                        if (cached != 2)
                            fputs("\tpush de\n\tpush hl\n", out);
                        argument_bytes += 4;
                    } else {
                        if (!mir_emit_cached_call_argument_to_stack(
                                out, arg->src1)) {
                            if (!mir_emit_cached_call_argument(
                                    out, arg->src1) &&
                                !mir_emit_rematerialized_argument(
                                    out, arg->src1, size))
                                mir_emit_virtual_load(out, arg->src1);
                            fputs("\tpush hl\n", out);
                        }
                        argument_bytes += 2;
                    }
                }
                if (argument != -1)
                    goto done;
                if (insn->immediate == MIR_AGGREGATE_GLOBAL_DEST_OFFSET) {
                    struct Sym *destination = find_global(insn->base_name);
                    const char *destination_name = asm_name_for(
                        destination != NULL ? sym_asm_name(destination)
                                            : mir_declared_link_name(
                                                insn->base_name));
                    if (destination != NULL && destination->needs_extrn &&
                        mir_extrn_should_emit(destination))
                        fprintf(out, "\textrn %s\n", destination_name);
                    fprintf(out, "\tld hl,%s\n", destination_name);
                } else if (insn->immediate == MIR_AGGREGATE_VALUE_DEST_OFFSET)
                    mir_emit_virtual_load(out, insn->src1);
                else if (insn->immediate == MIR_AGGREGATE_FORWARD_OFFSET)
                    fputs("\tld l,(ix+4)\n\tld h,(ix+5)\n", out);
                else
                    fputs("\tpush ix\n\tpop hl\n", out);
                if (insn->immediate != 0 &&
                    insn->immediate != MIR_AGGREGATE_FORWARD_OFFSET &&
                    insn->immediate != MIR_AGGREGATE_VALUE_DEST_OFFSET &&
                    insn->immediate != MIR_AGGREGATE_GLOBAL_DEST_OFFSET)
                    mir_emit_hl_offset_from_ix(out, (int)insn->immediate);
                fputs("\tpush hl\n", out);
                if ((callee == NULL || callee->needs_extrn) &&
                    mir_extrn_should_emit(callee))
                    fprintf(out, "\textrn %s\n", assembly_name);
                fprintf(out, "\tcall %s\n", assembly_name);
                fprintf(out, "\tld hl,%d\n\tadd hl,sp\n\tld sp,hl\n",
                        argument_bytes + 2);
                if (insn->immediate == MIR_AGGREGATE_GLOBAL_DEST_OFFSET) {
                    struct Sym *destination = find_global(insn->base_name);
                    const char *destination_name = asm_name_for(
                        destination != NULL ? sym_asm_name(destination)
                                            : mir_declared_link_name(
                                                insn->base_name));
                    fprintf(out, "\tld hl,%s\n", destination_name);
                } else if (insn->immediate == MIR_AGGREGATE_VALUE_DEST_OFFSET)
                    mir_emit_virtual_load(out, insn->src1);
                else if (insn->immediate == MIR_AGGREGATE_FORWARD_OFFSET)
                    fputs("\tld l,(ix+4)\n\tld h,(ix+5)\n", out);
                else
                    fputs("\tpush ix\n\tpop hl\n", out);
                if (insn->immediate != 0 &&
                    insn->immediate != MIR_AGGREGATE_FORWARD_OFFSET &&
                    insn->immediate != MIR_AGGREGATE_VALUE_DEST_OFFSET &&
                    insn->immediate != MIR_AGGREGATE_GLOBAL_DEST_OFFSET)
                    mir_emit_hl_offset_from_ix(out, (int)insn->immediate);
                mir_emit_virtual_store(out, insn->dst);
            }
            break;
        case MIR_VA_START:
            if (insn->immediate < -128 || insn->immediate + 1 > 127)
                goto done;
            fputs("\tpush ix\n\tpop hl\n", out);
            mir_emit_hl_offset_from_ix(out, insn->secondary_offset);
            fprintf(out, "\tld (ix%+ld),l\n\tld (ix%+ld),h\n",
                    insn->immediate, insn->immediate + 1);
            fputs("\tld hl,0\n", out);
            mir_emit_virtual_store(out, insn->dst);
            break;
        case MIR_VA_END:
            if (insn->immediate < -128 || insn->immediate + 1 > 127)
                goto done;
            fprintf(out, "\tld (ix%+ld),0\n\tld (ix%+ld),0\n",
                    insn->immediate, insn->immediate + 1);
            fputs("\tld hl,0\n", out);
            mir_emit_virtual_store(out, insn->dst);
            break;
            case MIR_VA_ARG:
                fprintf(out, "\tld l,(ix%+ld)\n\tld h,(ix%+ld)\n",
                    insn->immediate, insn->immediate + 1);
                fputs("\tpush hl\n", out);
                    fprintf(out, "\tld de,%d\n\tadd hl,de\n",
                    insn->secondary_offset);
                fprintf(out, "\tld (ix%+ld),l\n\tld (ix%+ld),h\n",
                    insn->immediate, insn->immediate + 1);
                fputs("\tpop hl\n", out);
                if (insn->secondary_offset == 4) {
                fputs("\tpush hl\n\tld a,(hl)\n\tinc hl\n"
                      "\tld h,(hl)\n\tld l,a\n\tex (sp),hl\n"
                      "\tinc hl\n\tinc hl\n\tld e,(hl)\n\tinc hl\n"
                      "\tld d,(hl)\n\tpop hl\n", out);
                mir_emit_virtual_store_wide(out, insn->dst);
                } else {
                fputs("\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n", out);
                mir_emit_virtual_store(out, insn->dst);
                }
                break;
        case MIR_JUMP:
            if (insn->label < 0 || insn->label >= mir.next_label)
                goto done;
            {
                int target = mir_find_label(insn->label);
                if (target < 0 || !mir_emit_spilled_phi_copies(out, i, target))
                    goto done;
                /* mir-text-size Item T8: a jump whose target label is the
                 * literal next MIR instruction is a pure fallthrough -
                 * control already lands there once any phi copies above
                 * have executed, so the `jp` mnemonic itself is dead
                 * weight. This mirrors legacy, which never emits a jump
                 * to the position immediately following it.
                 * Item T62: a jump that can never be reached at all
                 * (only preceded, after any dead elided labels, by an
                 * unconditional jump/return) is equally dead weight -
                 * see mir_insn_is_reachable's comment for the full
                 * rationale. */
                if (!mir_target_is_noop_fallthrough(i, target) &&
                    mir_insn_is_reachable(i))
                    fprintf(out, "\tjp L%d\n", labels[insn->label]);
            }
            break;
        case MIR_BRANCH_FALSE:
            if (insn->label < 0 || insn->label >= mir.next_label)
                goto done;
            {
                int target = mir_find_label(insn->label);
                const struct MirInsn *condition =
                    mir_definition(insn->src1);
                FILE *phi_probe;
                int phi_ok;
                int phi_bytes;
                if (target < 0)
                    goto done;
                /* mir-migration-plan-next200 Item 1: the general form below
                 * always emits a "skip past the direct jump" branch plus a
                 * separate unconditional jump plus a fallthrough label,
                 * even when there are no PHI copies to guard between them -
                 * the overwhelmingly common case for a plain if-statement
                 * with no live cross-block merge value. That degrades to
                 * two jump instructions where legacy needs only one
                 * (inverted-condition jump straight to the target). Probe
                 * first (mir_emit_spilled_phi_copies is side-effect-free
                 * beyond writing text, so a dry run into a scratch stream
                 * costs nothing but a tmpfile) and take the single-jump
                 * form whenever no copies are needed.
                 *
                 * mir_emit_spilled_phi_copies is NOT safe to call twice:
                 * the virtual load/store helpers it calls update live
                 * register-cache state as a side effect of emitting text,
                 * so a second "real" call after a probe call double-applies
                 * those state transitions and corrupts codegen whenever
                 * copy_count > 0. Call it exactly once (into the probe
                 * stream) and, if it wrote any bytes, copy that captured
                 * text verbatim into the real output instead of invoking
                 * the function again. */
                phi_probe = tmpfile();
                if (phi_probe == NULL)
                    fatal("cannot create MIR phi-copy probe stream");
                phi_ok = mir_emit_spilled_phi_copies(phi_probe, i, target);
                phi_bytes = phi_ok ? (int)ftell(phi_probe) : 0;
                if (!phi_ok) {
                    fclose(phi_probe);
                    goto done;
                }
                if (mir_value_is_wide(insn->src1)) {
                    mir_emit_virtual_load_wide(out, insn->src1);
                    if (condition != NULL && type_is_float(condition->type))
                        fputs("\tld a,d\n\tand 127\n\tor e\n\tor h\n\tor l\n",
                              out);
                    else
                        fputs("\tld a,d\n\tor e\n\tor h\n\tor l\n", out);
                } else {
                    mir_emit_virtual_load(out, insn->src1);
                    fputs("\tld a,h\n\tor l\n", out);
                }
                if (phi_bytes == 0) {
                    fclose(phi_probe);
                    fprintf(out, "\tjp z, L%d\n", labels[insn->label]);
                } else {
                    int fallthrough_label = new_label();
                    char buf[256];
                    int remaining = phi_bytes;
                    fprintf(out, "\tjp nz, L%d\n", fallthrough_label);
                    rewind(phi_probe);
                    while (remaining > 0) {
                        int chunk = remaining < (int)sizeof(buf)
                                        ? remaining
                                        : (int)sizeof(buf);
                        if (fread(buf, 1, (size_t)chunk, phi_probe) !=
                            (size_t)chunk)
                            fatal("cannot replay MIR phi-copy probe stream");
                        fwrite(buf, 1, (size_t)chunk, out);
                        remaining -= chunk;
                    }
                    fclose(phi_probe);
                    fprintf(out, "\tjp L%d\nL%d:\n", labels[insn->label],
                            fallthrough_label);
                }
            }
            break;
        case MIR_RETURN:
            if (type_is_struct_object(mir.return_type)) {
                int size = type_size(mir.return_type);
                if (insn->src1 < 0)
                    goto done;
                mir_emit_virtual_load(out, insn->src1);
                /* mir-text-size Item T5: HL already holds the source
                 * address after mir_emit_virtual_load above. Load DE
                 * directly with the hidden return-buffer pointer (no
                 * need to route it through HL via `ex de,hl` first) and
                 * copy with the Z80 `ldir` block-copy instruction
                 * instead of a fully unrolled byte-by-byte sequence.
                 * Legacy's own struct-return path (emit_copy_de_to_hl_bytes,
                 * dcc_expr.c) already avoids unrolling with a `djnz`
                 * loop; `ldir` is both smaller and faster than either
                 * the old unrolled form or a djnz loop, and is the same
                 * idiom already used pervasively for block copies in
                 * DCCRTL.MAC. */
                if (size > 0) {
                    fputs("\tld e,(ix+4)\n\tld d,(ix+5)\n", out);
                    fprintf(out, "\tld bc,%d\n\tldir\n", size);
                }
            } else if (insn->src1 >= 0) {
                if (mir_value_is_wide(insn->src1))
                    mir_emit_virtual_load_wide(out, insn->src1);
                else
                    mir_emit_virtual_load(out, insn->src1);
            }
            /* mir-text-size Item T14: share one epilogue among every early
             * return instead of duplicating it at each site (see the
             * return_count/last_insn_is_return precomputation above). The
             * owner - whichever return is the function's literal last MIR
             * instruction, or the fall-off-the-end tail below if none is -
             * always keeps the real epilogue text; every other return
             * becomes a plain `jp`, which is both smaller and faster than a
             * second copy of ix/iy/sp restore + ret whenever return_count
             * exceeds one. HL/DE were already loaded with the return value
             * just above, and `jp` does not disturb registers, so this is
             * safe regardless of which return the value came from - the
             * same guarantee legacy's own shared-epilogue `jp` relies on. */
            if (return_count > 1 &&
                !(last_insn_is_return && i == mir.count - 1)) {
                if (shared_epilogue_label < 0)
                    shared_epilogue_label = new_label();
                fprintf(out, "\tjp L%d\n", shared_epilogue_label);
            } else {
                if (shared_epilogue_label >= 0)
                    fprintf(out, "L%d:\n", shared_epilogue_label);
                mir_emit_virtual_iy_epilogue(out);
            }
            break;
        default:
            goto done;
        }
        if (mir_instruction_has_phi_fallthrough(i, 1) &&
            !mir_emit_spilled_phi_copies(out, i, i + 1))
            goto done;
    }
    if (mir_planned_stack_invalid ||
        mir_planned_stack_emit_count != mir_planned_stack_consume_count) {
        if (getenv("DCC_MIR_SELECT_REPORT") != NULL)
            fprintf(stderr,
                    "; MIR planned-stack mismatch function=%s invalid=%d "
                    "emitted=%d consumed=%d pending-value=%d "
                    "pending-consumer=%d\n",
                    mir.name, mir_planned_stack_invalid,
                    mir_planned_stack_emit_count,
                    mir_planned_stack_consume_count,
                    mir_pending_planned_stack_consumer() >= 0
                        ? mir.planned_stack_values[
                              mir_pending_planned_stack_consumer()]
                        : -1,
                    mir_pending_planned_stack_consumer());
        goto done;
    }
    /* mir-migration-plan-next10 Item 3: MIR_RETURN's own case (above) already
     * emits the function epilogue before its `break`. If the function's very
     * last IR instruction was a MIR_RETURN, that epilogue was already
     * written and this unconditional trailing call would duplicate it
     * (dead, unreachable "ld sp,ix / pop ix / ret" after the real return) -
     * every function using this selector paid for this every time. Only
     * emit it here for the true fall-off-the-end case, i.e. when the last
     * instruction was something else (a label, a jump target, etc.). */
    if (mir.count == 0 || mir.insns[mir.count - 1].opcode != MIR_RETURN) {
        /* Item T14: this fall-off-the-end tail is the shared-epilogue
         * owner whenever the function's last MIR instruction isn't
         * itself a MIR_RETURN (see the precomputation above) - define
         * the label here if any earlier return needed it. */
        if (shared_epilogue_label >= 0)
            fprintf(out, "L%d:\n", shared_epilogue_label);
        mir_emit_virtual_iy_epilogue(out);
    } else {
        /* mir-migration-plan-next10 Item 3: the duplicate epilogue this
         * skips would have been counted in generated_size before this
         * fix. Measure its exact text length into a scratch buffer (the
         * epilogue text depends only on module globals, not on `out`,
         * so this has no side effect) and record it so the acceptance
         * gate's cost comparison is unaffected by this dead-code removal
         * - only the real emitted text shrinks, not the accept/reject
         * decision (skill rule 1: never widen a fallback gate as a side
         * effect of an unrelated fix). */
        char elided_buf[128];
        FILE *elided_scratch = fmemopen(elided_buf, sizeof(elided_buf), "w");
        if (elided_scratch != NULL) {
            mir_emit_virtual_iy_epilogue(elided_scratch);
            fflush(elided_scratch);
            mir_spilled_scalar_cfg_elided_epilogue_bytes =
                (int)ftell(elided_scratch);
            fclose(elided_scratch);
        }
    }
    if (getenv("DCC_MIR_PREPACK_REPORT") != NULL &&
        mir_constant_argument_prepack_count != 0)
        fprintf(stderr,
                "; MIR constant-prepack function=%s count=%d\n",
                mir.name, mir_constant_argument_prepack_count);
    if (mir_constant_argument_prepacking_enabled &&
        mir_constant_argument_prepack_count > 0 &&
        mir_constant_argument_prepack_count < 3)
        goto done;
    accepted = 1;
done:
    if (accepted && mir_backend_slot_accessed != NULL)
        for (i = 0; i < mir.next_value; ++i)
            if (mir.backend_slots[i] >= 0 &&
                (getenv("DCC_MIR_SLOT_ACCESS_REPORT") != NULL ||
                 !mir_backend_slot_accessed[i])) {
                const struct MirInsn *definition = mir_definition(i);
                const struct MirInsn *consumer = NULL;
                const char *consumer_operand = "none";
                int consumer_index;
                int definition_index = definition != NULL
                    ? (int)(definition - mir.insns) : -1;

                for (consumer_index = 0;
                     consumer_index < mir.count; ++consumer_index)
                    if (mir.insns[consumer_index].src1 == i ||
                        mir.insns[consumer_index].src2 == i ||
                        mir_call_uses_value(&mir.insns[consumer_index], i)) {
                        consumer = &mir.insns[consumer_index];
                        break;
                    }
                if (consumer != NULL)
                    consumer_operand = consumer->src1 == i ? "src1" :
                        consumer->src2 == i ? "src2" :
                        mir_call_uses_value(consumer, i) ? "call" : "other";
                fprintf(stderr,
                        "; MIR %s function=%s value=%d slot=%d accessed=%d "
                        "definition=%s type-size=%d uses=%d "
                        "consumer=%s operand=%s distance=%d "
                        "definition-immediate=%ld "
                        "consumer-immediate=%ld definition-type=%d "
                        "consumer-type=%d\n",
                        getenv("DCC_MIR_SLOT_ACCESS_REPORT") != NULL
                            ? "slot-access" : "unused-slot",
                        mir.name, i, mir.backend_slots[i],
                        mir_backend_slot_accessed[i] != 0,
                        definition != NULL
                            ? mir_opcode_name(definition->opcode) : "none",
                        definition != NULL
                            ? type_size(definition->type) : 0,
                        mir_value_use_count(i),
                        consumer != NULL
                            ? mir_opcode_name(consumer->opcode) : "none",
                        consumer_operand,
                        consumer != NULL && definition_index >= 0
                            ? consumer_index - definition_index : -1,
                        definition != NULL ? definition->immediate : 0L,
                        consumer != NULL ? consumer->immediate : 0L,
                        definition != NULL ? definition->type : 0,
                        consumer != NULL ? consumer->type : 0);
            }
    free(mir_backend_slot_accessed);
    mir_backend_slot_accessed = NULL;
    if (getenv("DCC_MIR_FUSE_REPORT") != NULL &&
        (mir_fuse_report_fused_count > 0 ||
         mir_fuse_report_materialized_count > 0))
        fprintf(stderr,
                "; MIR fuse-report function=%s fused=%d materialized=%d\n",
                mir.name, mir_fuse_report_fused_count,
                mir_fuse_report_materialized_count);
    mir_virtual_iy_base = 0;
    mir_virtual_iy_frame_bytes = 0;
    mir_emit_instruction_index = -1;
    mir_forwarded_hl_value = -1;
    mir_forwarded_hl_instruction = -1;
    mir_forwarded_wide_value = -1;
    mir_forwarded_wide_instruction = -1;
    mir_forwarded_stack_value = -1;
    mir_forwarded_stack_instruction = -1;
    mir_forwarded_stack_target_instruction = -1;
    mir_forwarded_wide_stack_value = -1;
    mir_forwarded_wide_stack_consumer = -1;
    mir_cached_call_value = -1;
    mir_cached_call_instruction = -1;
    mir_planned_stack_handoffs_enabled = 0;
    mir_planned_stack_emit_count = 0;
    mir_planned_stack_consume_count = 0;
    mir_planned_stack_invalid = 0;
    if (!accepted && getenv("DCC_MIR_SELECT_REPORT") != NULL)
        fprintf(stderr, "; MIR scalar-cfg reject function=%s insn=%d opcode=%s\n",
                mir.name, i,
                i >= 0 && i < mir.count
                    ? mir_opcode_name(mir.insns[i].opcode) : "preflight");
    free(labels);
    return accepted;
}
