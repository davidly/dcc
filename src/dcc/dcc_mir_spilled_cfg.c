/* dcc_mir_spilled_cfg.c - the mir_try_emit_spilled_scalar_cfg selector
 * (the dominant selector, covering most of the corpus) plus its
 * exclusive helpers: virtual IY/offset addressing, HL/stack value
 * forwarding, comparison fusion, fastcall shape detection, and
 * backend-slot preparation.
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

static int mir_virtual_offset(int value)
{
    int slot = value;
    if (value >= 0 && value < mir.next_value && mir.backend_slots != NULL &&
        mir.backend_slots[value] >= 0)
        slot = mir.backend_slots[value];
    return -mir.local_bytes - mir.aggregate_temp_bytes - 2 * (slot + 1);
}

static int mir_virtual_iy_offset(int value)
{
    return mir_virtual_offset(value) + mir.local_bytes +
           mir.aggregate_temp_bytes;
}

static int mir_function_has_any_call(void)
{
    int i;

    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_CALL ||
            mir.insns[i].opcode == MIR_CALL_AGGREGATE)
            return 1;
    return 0;
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
static int mir_forward_skip_target(int instruction)
{
    int next_instruction = instruction + 1;
    int skipped_label = 0;

    for (;;) {
        while (next_instruction < mir.count &&
               mir.insns[next_instruction].opcode == MIR_NOP)
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
    return next_instruction;
}

static int mir_can_forward_hl_to_next(int value)
{
    const struct MirInsn *definition = mir_definition(value);
    const struct MirInsn *next;
    int next_instruction;
    int instruction;

    if (mir_emit_instruction_index < 0 ||
        mir_emit_instruction_index + 1 >= mir.count)
        return 0;
    if (definition != NULL &&
        (definition->opcode == MIR_CALL ||
         definition->opcode == MIR_CALL_AGGREGATE))
        return 0;
    next_instruction = mir_forward_skip_target(mir_emit_instruction_index);
    if (next_instruction >= mir.count)
        return 0;
    next = &mir.insns[next_instruction];
    if (next_instruction != mir_emit_instruction_index + 1 &&
        next->opcode != MIR_RETURN)
        return 0;
    if (next->opcode == MIR_RETURN &&
        (mir.has_vla || mir_function_has_any_call()))
        return 0;
    if (next->opcode == MIR_INDEX_ADDRESS) {
        if (next->src2 != value)
            return 0;
    } else if (next->src1 != value)
            return 0;
    switch (next->opcode) {
    case MIR_INDEX_ADDRESS:
    case MIR_MEMBER_ADDRESS: case MIR_LOAD_INDIRECT: case MIR_UNARY:
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
            if (mir_object_is_fully_promoted(next->object) ||
                (producer_opcode != MIR_LOAD_INDIRECT &&
                 producer_opcode != MIR_BINARY &&
                 producer_opcode != MIR_UNARY) ||
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
    return 1;
}

static int mir_can_forward_stack_to_index(int value)
{
    const struct MirInsn *middle;
    const struct MirInsn *index;
    int instruction;

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
    if (mir_emit_instruction_index < 0 ||
        mir_emit_instruction_index + 2 >= mir.count)
        return 0;
    middle = &mir.insns[mir_emit_instruction_index + 1];
    index = &mir.insns[mir_emit_instruction_index + 2];
    if (middle->opcode != MIR_CONST || index->opcode != MIR_INDEX_ADDRESS ||
        index->src1 != value || index->src2 != middle->dst)
        return 0;
    for (instruction = mir_emit_instruction_index + 3;
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
    int is_unsigned = (operand_type & TYPE_UNSIGNED) != 0;
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
static int mir_mul_const_fast_path_eligible(unsigned long multiplier, int dst)
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
static void mir_emit_mul_hl_const(FILE *out, unsigned long multiplier)
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
        fprintf(out, "\tld de,%lu\n\textrn __mulu\n\tcall __mulu\n",
                multiplier);
    }
}

static int mir_emit_rematerialized_argument(FILE *out, int value, int size)
{
    const struct MirInsn *definition = mir_definition(value);
    unsigned long bits;

    if ((size == 2 || size == 4) &&
        mir_load_is_single_call_argument(value, size)) {
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

static int mir_emit_cached_call_argument(FILE *out, int value)
{
    if (mir_cached_call_value != value ||
        mir_cached_call_instruction != mir_emit_instruction_index)
        return 0;
    fputs("\tld l,c\n\tld h,b\n", out);
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
    if (mir_cached_wide_call_value != value ||
        mir_cached_wide_call_instruction != mir_emit_instruction_index)
        return 0;
    fputs("\texx\n", out);
    mir_cached_wide_call_value = -1;
    mir_cached_wide_call_instruction = -1;
    return 1;
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

static int mir_object_address_taken(int object)
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
static int mir_store_is_dead(int instruction)
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

static int mir_call_argument_cache_target(int value)
{
    int argument_instruction = -1;
    int call_id = -1;
    int call_instruction = -1;
    int instruction;

    if ((mir_value_is_wide(value) && mir_cached_wide_call_value >= 0) ||
        (!mir_value_is_wide(value) && mir_cached_call_value >= 0))
        return -1;
    for (instruction = mir_emit_instruction_index + 1;
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
    for (instruction = mir_emit_instruction_index + 1;
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

int mir_load_is_single_call_argument(int value, int size)
{
    const struct MirInsn *definition = mir_definition(value);
    int argument_count = 0;
    int call_id = -1;
    int call_argument_count = 0;
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
        if (insn->opcode != MIR_ARG || type_size(insn->type) != size ||
            ++argument_count > 1)
            return 0;
        call_id = insn->secondary_offset;
    }
    if (argument_count != 1)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_ARG &&
            mir.insns[instruction].secondary_offset == call_id)
            ++call_argument_count;
    return call_argument_count <= 3;
}

static int mir_binary_is_fusable_comparison(int i);

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
 * one-instruction gap) whenever the forward target isn't itself a MIR_STORE;
 * mir_prepare_backend_slots was still handing out a slot number/frame byte
 * for it regardless. Reuse mir_can_forward_hl_to_next itself (rather than a
 * second copy of the same forwarding predicate) so the accounting pass and
 * the real emission-time skip can never drift apart - the repo's own Item 19
 * caution. Only 1-unit (narrow, HL-sized) values ever reach
 * mir_emit_virtual_store/mir_can_forward_hl_to_next; wide values use the
 * separate _wide store path and must still get a slot. */
static int mir_backend_slot_forward_target_is_store(int instruction)
{
    int next_instruction = mir_forward_skip_target(instruction);
    return next_instruction < mir.count &&
           mir.insns[next_instruction].opcode == MIR_STORE &&
           next_instruction == instruction + 1;
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
    forwardable = mir_can_forward_hl_to_next(value) &&
                  !mir_backend_slot_forward_target_is_store(instruction);
    mir_emit_instruction_index = saved_index;
    return forwardable;
}

static int mir_divmod_partner(int instruction);

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
    static int cached_result = -1;
    static const FILE *cached_stream = NULL;
    static const char needle[] = "push ix";
    int character;
    int matched;
    long saved_position;

    if (mir.capture_stream == NULL)
        return 1;
    if (cached_stream == mir.capture_stream && cached_result >= 0)
        return cached_result;
    cached_stream = mir.capture_stream;
    saved_position = ftell(mir.capture_stream);
    rewind(mir.capture_stream);
    cached_result = 0;
    matched = 0;
    while ((character = fgetc(mir.capture_stream)) != EOF) {
        if (character == needle[matched]) {
            ++matched;
            if (needle[matched] == '\0') {
                cached_result = 1;
                break;
            }
        } else {
            matched = (character == needle[0]) ? 1 : 0;
        }
    }
    if (saved_position >= 0)
        fseek(mir.capture_stream, saved_position, SEEK_SET);
    return cached_result;
}

/* mir-migration-plan-next10 (post Item 3): a function-parameter value never
 * needs its own dedicated backend slot when the underlying parameter object
 * is never reassigned anywhere in the function (no MIR_STORE targets it) -
 * every later use can simply re-read the same stable incoming ix+N slot the
 * parameter already lives in, instead of copying it into a fresh local slot
 * at the MIR_PARAM definition site and reloading from there. This removes
 * both the copy (store) instructions and the frame-byte growth for every
 * multi-use scalar parameter. Restricted to plain scalars (2 or 4 byte,
 * non-struct, non-aggregate) so the load/store emitters below can reuse the
 * exact same in-range/out-of-range addressing forms the original MIR_PARAM
 * binding already uses - this must stay purely additive: any value this
 * returns false for keeps its prior slot-based behavior unchanged. */
static int mir_param_value_is_direct(int value)
{
    const struct MirInsn *definition;
    int object;
    int i;

    if (value < 0 || value >= mir.next_value)
        return 0;
    definition = mir_definition(value);
    if (definition == NULL || definition->opcode != MIR_PARAM)
        return 0;
    if (type_is_struct_object(definition->type) ||
        (type_size(definition->type) != 2 && type_size(definition->type) != 4))
        return 0;
    object = definition->object;
    if (object < 0 || object >= mir.object_count)
        return 0;
    if (mir.is_variadic_function)
        return 0;
    if (mir.has_vla)
        return 0;
    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_STORE && mir.insns[i].object == object)
            return 0;
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
    return 1;
}

static int mir_prepare_backend_slots(void)
{
    int *first;
    int *last;
    int *slot_end;
    char *fused_away = NULL;
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
    first = (int *)malloc((size_t)mir.next_value * sizeof(*first));
    last = (int *)malloc((size_t)mir.next_value * sizeof(*last));
    slot_end = (int *)malloc((size_t)mir.next_value * 2 * sizeof(*slot_end));
    if (first == NULL || last == NULL || slot_end == NULL)
        fatal("out of memory computing MIR backend intervals");
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
        }
    }
    for (value = 0; value < mir.next_value; ++value) {
        first[value] = mir.count;
        last[value] = -1;
        mir.backend_slots[value] = -1;
        slot_end[value] = -1;
        slot_end[mir.next_value + value] = -1;
    }
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
                    int target = mir_first_nonlabel_successor(
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
    mir.backend_slot_count = 0;
    mir_slot_report_requested_count = 0;
    mir_slot_report_assigned_count = 0;
    mir_slot_report_param_assigned_count = 0;
    for (i = 0; i < mir.count; ++i)
        for (value = 0; value < mir.next_value; ++value)
            if (first[value] == i) {
                int slot;
                const struct MirInsn *definition = mir_definition(value);
                int units = mir_definition_is_wide(definition) ? 2 : 1;
                int reusable_source = -1;
                ++mir_slot_report_requested_count;
                if (last[value] <= first[value] ||
                                        mir_call_only_constant(value) ||
                                        mir_multiply_by_small_constant(value) ||
                                        (fused_away != NULL && fused_away[value]) ||
                                        mir_value_is_selfstore_incdec(value) ||
                                        ((type_size(definition->type) == 2 ||
                                            type_size(definition->type) == 4) &&
                                         mir_load_is_single_call_argument(value,
                                                                                                            type_size(definition->type))) ||
                                        mir_backend_slot_forwardable(value, units, i) ||
                                        mir_param_value_is_direct(value))
                    continue;
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
                    for (unit = 0; unit < units; ++unit)
                        slot_end[slot + unit] = last[value];
                    continue;
                }
                for (slot = 0; slot + units <= mir.backend_slot_count; ++slot) {
                    int unit;
                    int available = 1;
                    for (unit = 0; unit < units; ++unit)
                        if (slot_end[slot + unit] >= i) {
                            available = 0;
                            break;
                        }
                    if (available)
                        break;
                }
                if (slot + units > mir.backend_slot_count) {
                    slot = mir.backend_slot_count;
                    mir.backend_slot_count += units;
                }
                mir.backend_slots[value] = slot;
                {
                    int unit;
                    for (unit = 0; unit < units; ++unit)
                        slot_end[slot + unit] = last[value];
                }
            }
    free(fused_away);
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
    return mir.backend_slot_count;
}

void mir_emit_virtual_load(FILE *out, int value)
{
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
    if (mir_param_value_is_direct(value)) {
        /* mir-migration-plan-next10: read straight from the parameter's own
         * stable incoming ix+N home instead of a duplicated backend slot -
         * see mir_param_value_is_direct's comment. Mirrors exactly the
         * in-range/out-of-range forms the original MIR_PARAM binding site
         * uses for a narrow scalar, including the IY-relative fast path
         * hot loops rely on (skipping it caused a measurable, if tiny,
         * cycle regression in tsnprtf's call_vsnprintf). */
        int object_offset = mir.objects[mir_definition(value)->object].offset;
        int object_iy_offset = object_offset + mir.local_bytes +
                                mir.aggregate_temp_bytes;
        if (mir_virtual_iy_base && object_iy_offset >= -128 &&
            object_iy_offset + 1 <= 127) {
            fprintf(out, "\tld l,(iy%+d)\n\tld h,(iy%+d)\n",
                    object_iy_offset, object_iy_offset + 1);
        } else if (object_offset >= -128 && object_offset + 1 <= 127) {
            fprintf(out, "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
                    object_offset, object_offset + 1);
        } else {
            fputs("\tpush ix\n\tpop hl\n", out);
            fprintf(out, "\tld de,%d\n\tadd hl,de\n"
                         "\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n",
                    object_offset);
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

static void mir_emit_virtual_store(FILE *out, int value)
{
    int has_slot;
    int forward_instruction;
    if (mir_param_value_is_direct(value))
        /* mir-migration-plan-next10: nothing to store - later uses of this
         * value re-read it directly from its stable parameter home (see
         * mir_emit_virtual_load); the loaded HL is simply not persisted. */
        return;
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
        }
        return;
    }
    int offset = mir_virtual_offset(value);
    int iy_offset = mir_virtual_iy_offset(value);
    int forward_to_store = mir_can_forward_hl_to_next(value) &&
        forward_instruction < mir.count &&
        mir.insns[forward_instruction].opcode == MIR_STORE;
    if (!forward_to_store && mir_can_forward_hl_to_next(value)) {
        mir_forwarded_hl_value = value;
        mir_forwarded_hl_instruction = forward_instruction - 1;
        return;
    }
    if (mir_can_forward_stack_to_index(value)) {
        fputs("\tpush hl\n", out);
        mir_forwarded_stack_value = value;
        mir_forwarded_stack_instruction = mir_emit_instruction_index;
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
    mir_forwarded_hl_value = -1;
    mir_forwarded_hl_instruction = -1;
    if (mir_virtual_iy_base && iy_offset >= -128 && iy_offset + 1 <= 127) {
        fprintf(out, "\tld (iy%+d),l\n\tld (iy%+d),h\n",
                iy_offset, iy_offset + 1);
        if (forward_to_store) {
            mir_forwarded_hl_value = value;
            mir_forwarded_hl_instruction = forward_instruction - 1;
        }
        return;
    }
    if (offset >= -128 && offset + 1 <= 127) {
        fprintf(out, "\tld (ix%+d),l\n\tld (ix%+d),h\n",
                offset, offset + 1);
    } else {
        fputs("\tex de,hl\n\tpush ix\n\tpop hl\n", out);
        fprintf(out, "\tld bc,%d\n\tadd hl,bc\n"
                     "\tld (hl),e\n\tinc hl\n\tld (hl),d\n",
                offset);
    }
    if (forward_to_store) {
        mir_forwarded_hl_value = value;
        mir_forwarded_hl_instruction = forward_instruction - 1;
    }
}

int mir_value_is_wide(int value)
{
    return mir_definition_is_wide(mir_definition(value));
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
    if (mir_param_value_is_direct(value)) {
        /* mir-migration-plan-next10: wide (4-byte) parameter counterpart
         * of mir_emit_virtual_load - object storage for a wide parameter is
         * ascending (offset..offset+3), unlike the descending backend-slot
         * convention below, so this mirrors the original MIR_PARAM binding
         * site's load form exactly rather than reusing mir_virtual_offset.
         * Also mirrors the IY-relative fast path hot loops rely on. */
        int object_offset = mir.objects[definition->object].offset;
        int object_iy_offset = object_offset + mir.local_bytes +
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
    int has_slot = value >= 0 && value < mir.next_value &&
                   mir.backend_slots != NULL && mir.backend_slots[value] >= 0;
    if (!has_slot)
        return;
    int offset = mir_virtual_offset(value);
    int iy_offset = mir_virtual_iy_offset(value);
    int call_instruction = mir_call_argument_cache_target(value);
    if (call_instruction >= 0) {
        fputs("\texx\n", out);
        mir_cached_wide_call_value = value;
        mir_cached_wide_call_instruction = call_instruction;
        return;
    }
    mir_forwarded_hl_value = -1;
    mir_forwarded_hl_instruction = -1;
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
    if (mir.local_bytes + mir.aggregate_temp_bytes != 0)
        fprintf(out, "\tld bc,-%d\n\tadd iy,bc\n",
                mir.local_bytes + mir.aggregate_temp_bytes);
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
    case '*': fputs("\textrn __mulu\n\tcall __mulu\n", out); return 1;
    case '/':
        fprintf(out, "\textrn %s\n\tcall %s\n",
                (insn->type & TYPE_UNSIGNED) != 0 ? "__divu" : "__divs",
                (insn->type & TYPE_UNSIGNED) != 0 ? "__divu" : "__divs");
        return 1;
    case '%':
        fprintf(out, "\textrn %s\n\tcall %s\n",
                (insn->type & TYPE_UNSIGNED) != 0 ? "__modu" : "__mods",
                (insn->type & TYPE_UNSIGNED) != 0 ? "__modu" : "__mods");
        return 1;
    case TOK_EQ: case TOK_NE: case '<': case '>': case TOK_LE: case TOK_GE:
        {
            const struct MirInsn *left = mir_definition(insn->src1);
            const struct MirInsn *right = mir_definition(insn->src2);
            int is_unsigned = (left != NULL &&
                               (left->type & TYPE_UNSIGNED) != 0) ||
                              (right != NULL &&
                               (right->type & TYPE_UNSIGNED) != 0);
            mir_emit_scalar_compare(out, (int)insn->immediate, is_unsigned);
        }
        return 1;
    case TOK_SHL: case TOK_SHR:
        {
            const struct MirInsn *left = mir_definition(insn->src1);
            mir_emit_scalar_shift(out, (int)insn->immediate,
                                  left != NULL &&
                                  (left->type & TYPE_UNSIGNED) != 0);
        }
        return 1;
    default:
        return 0;
    }
}

static int mir_emit_spilled_phi_copies(FILE *out, int predecessor,
                                       int successor);

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

    if (insn->opcode != MIR_BINARY || type_is_float(insn->secondary_offset))
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
    if ((left != NULL && (left->type & TYPE_UNSIGNED) != 0) ||
        (right != NULL && (right->type & TYPE_UNSIGNED) != 0))
        return 0;
    if (right == NULL || right->opcode != MIR_CONST ||
        (right->immediate & 0xffffL) != 0)
        return 0;
    return 1;
}

static int mir_emit_fused_comparison_branch(FILE *out, const int *labels,
                                             int compare_index, int negate)
{
    const struct MirInsn *compare = &mir.insns[compare_index];
    const struct MirInsn *branch = &mir.insns[compare_index + 1 + negate];
    const struct MirInsn *left = mir_definition(compare->src1);
    const struct MirInsn *right = mir_definition(compare->src2);
    int is_unsigned = (left != NULL && (left->type & TYPE_UNSIGNED) != 0) ||
                       (right != NULL && (right->type & TYPE_UNSIGNED) != 0);
    int operation = negate ? mir_negate_comparison_operator(
                                 (int)compare->immediate)
                           : (int)compare->immediate;
    int target;
    int fallthrough_label;
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
        fallthrough_label = new_label();
        fprintf(out, "\tjp %s,L%d\n", true_condition, fallthrough_label);
        if (!mir_emit_spilled_phi_copies(out, compare_index + 1 + negate,
                                          target))
            return 0;
        fprintf(out, "\tjp L%d\nL%d:\n", labels[branch->label],
                fallthrough_label);
        return 1;
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
    fallthrough_label = new_label();
    fprintf(out, "\tjp %s,L%d\n", true_condition, fallthrough_label);
    if (!mir_emit_spilled_phi_copies(out, compare_index + 1 + negate, target))
        return 0;
    fprintf(out, "\tjp L%d\nL%d:\n", labels[branch->label], fallthrough_label);
    return 1;
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
 * logical-not (mir_binary_is_fusable_comparison's existing negate signal). */
static int mir_emit_fused_wide_comparison_branch(FILE *out, const int *labels,
                                                  int compare_index,
                                                  int negate)
{
    const struct MirInsn *branch = &mir.insns[compare_index + 1 + negate];
    int target;
    int fallthrough_label;
    const char *true_condition;

    if (branch->label < 0 || branch->label >= mir.next_label)
        return 0;
    target = mir_find_label(branch->label);
    if (target < 0)
        return 0;
    fputs("\tld a,h\n\tor l\n", out);
    true_condition = negate ? "z" : "nz";
    fallthrough_label = new_label();
    fprintf(out, "\tjp %s,L%d\n", true_condition, fallthrough_label);
    if (!mir_emit_spilled_phi_copies(out, compare_index + 1 + negate, target))
        return 0;
    fprintf(out, "\tjp L%d\nL%d:\n", labels[branch->label], fallthrough_label);
    return 1;
}

static void mir_emit_hl_and_const(FILE *out, unsigned int mask)
{
    fprintf(out,
            "\tld a,l\n\tand %u\n\tld l,a\n"
            "\tld a,h\n\tand %u\n\tld h,a\n",
            mask & 0xffU, (mask >> 8) & 0xffU);
}

static void mir_emit_hl_or_const(FILE *out, unsigned int mask)
{
    fprintf(out,
            "\tld a,l\n\tor %u\n\tld l,a\n"
            "\tld a,h\n\tor %u\n\tld h,a\n",
            mask & 0xffU, (mask >> 8) & 0xffU);
}

static void mir_emit_bitfield_extract(FILE *out, const struct MirInsn *insn)
{
    int shift;
    int sign_label;
    unsigned int value_mask;

    for (shift = 0; shift < insn->bit_shift; ++shift)
        fputs("\tsrl h\n\trr l\n", out);
    value_mask = insn->bit_width >= 16
        ? 0xffffU : (1U << insn->bit_width) - 1U;
    mir_emit_hl_and_const(out, value_mask);
    if ((insn->type & TYPE_UNSIGNED) == 0 && insn->bit_width > 0 &&
        insn->bit_width < 16) {
        sign_label = new_label();
        if (insn->bit_width <= 8)
            fprintf(out, "\tbit %d,l\n", insn->bit_width - 1);
        else
            fprintf(out, "\tbit %d,h\n", insn->bit_width - 9);
        fprintf(out, "\tjp z,L%d\n", sign_label);
        mir_emit_hl_or_const(out, (~value_mask) & 0xffffU);
        fprintf(out, "L%d:\n", sign_label);
    }
}

static int mir_emit_wide_operation(FILE *out, const struct MirInsn *insn)
{
    const char *helper = NULL;
    int operation = (int)insn->immediate;
    int operand_type = insn->secondary_offset;
    if (type_is_float(operand_type)) {
        if (operation == '+' || operation == '-' || operation == '*' ||
            operation == '/') {
            helper = operation == '+' ? "__faf" : operation == '-' ? "__fsf" :
                     operation == '*' ? "__fmf" : "__fdf";
        } else if (operation == TOK_EQ || operation == TOK_NE ||
                   operation == '<' || operation == '>' ||
                   operation == TOK_LE || operation == TOK_GE) {
            helper = operation == TOK_EQ ? "__feqf" :
                     operation == TOK_NE ? "__fnef" :
                     operation == '<' ? "__fgtf" :
                     operation == '>' ? "__fltf" :
                     operation == TOK_LE ? "__fgef" : "__flef";
        } else {
            return 0;
        }
        fprintf(out, "\textrn %s\n\tcall %s\n\tpop bc\n\tpop bc\n",
                helper, helper);
        return 1;
    }
    if (operation == TOK_EQ || operation == TOK_NE) {
        int true_label = new_label();
        int end_label = new_label();
        fputs("\tpop bc\n\tld a,c\n\txor l\n\tld l,a\n"
              "\tld a,b\n\txor h\n\tor l\n\tld l,a\n"
              "\tpop bc\n\tld a,c\n\txor e\n\tor l\n\tld l,a\n"
              "\tld a,b\n\txor d\n\tor l\n", out);
        fprintf(out, operation == TOK_EQ ? "\tjp z,L%d\n" : "\tjp nz,L%d\n",
                true_label);
        fprintf(out, "\tld hl,0\n\tjp L%d\nL%d:\n\tld hl,1\nL%d:\n",
                end_label, true_label, end_label);
        return 1;
    }
    if (operation == '<' || operation == '>' || operation == TOK_LE ||
        operation == TOK_GE) {
        int is_unsigned = (operand_type & TYPE_UNSIGNED) != 0;
        helper = operation == '<' ? (is_unsigned ? "__ltu" : "__lts") :
                 operation == TOK_LE ? (is_unsigned ? "__leu" : "__les") :
                 operation == '>' ? (is_unsigned ? "__lgu" : "__lgs") :
                 (is_unsigned ? "__lku" : "__lks");
        fprintf(out, "\tpush de\n\tpush hl\n\textrn %s\n\tcall %s\n"
                     "\tex de,hl\n\tld hl,8\n\tadd hl,sp\n"
                     "\tld sp,hl\n\tex de,hl\n",
                helper, helper);
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
            int loop_label = new_label();
            int done_label = new_label();
            fputs("\tld a,l\n\tpop hl\n\tpop de\n\tld b,a\n", out);
            fprintf(out, "L%d:\n\tld a,b\n\tor a\n\tjp z,L%d\n",
                    loop_label, done_label);
            if (insn->immediate == TOK_SHL)
                fputs("\tadd hl,hl\n\trl e\n\trl d\n", out);
            else if ((operand_type & TYPE_UNSIGNED) != 0)
                fputs("\tsrl d\n\trr e\n\trr h\n\trr l\n", out);
            else
                fputs("\tsra d\n\trr e\n\trr h\n\trr l\n", out);
            fprintf(out, "\tdec b\n\tjp L%d\nL%d:\n",
                    loop_label, done_label);
        }
        return 1;
    case '*': helper = "__lmul"; break;
    case '/': helper = (insn->type & TYPE_UNSIGNED) != 0 ? "__ldu" : "__lds"; break;
    case '%': helper = (insn->type & TYPE_UNSIGNED) != 0 ? "__lmu" : "__lms"; break;
    default: return 0;
    }
    fprintf(out, "\textrn %s\n\tcall %s\n\tpop bc\n\tpop bc\n",
            helper, helper);
    return 1;
}

static int mir_emit_cast(FILE *out, int source_type, int target_type)
{
    const char *helper;
    if (type_is_bool(target_type) && !type_is_bool(source_type)) {
        int nonzero_label = new_label();
        int end_label = new_label();
        if (type_size(source_type) > 2)
            fputs("\tld a,d\n\tor e\n\tor h\n\tor l\n", out);
        else
            fputs("\tld a,h\n\tor l\n", out);
        fputs("\tld hl,0\n", out);
        fprintf(out, "\tjp nz,L%d\n\tjp L%d\nL%d:\n\tinc hl\nL%d:\n",
                nonzero_label, end_label, nonzero_label, end_label);
        return 1;
    }
    if (source_type == 0 || target_type == 0 || source_type == target_type)
        return 1;
    if (type_is_float(target_type) && !type_is_float(source_type)) {
        if (type_is_long(source_type))
            helper = (source_type & TYPE_UNSIGNED) != 0 ? "__fulf" : "__flf";
        else
            helper = ((source_type & TYPE_UNSIGNED) != 0 ||
                      type_ptr_depth(source_type) > 0) ? "__fuf" : "__fif";
        fprintf(out, "\textrn %s\n\tcall %s\n", helper, helper);
        return 1;
    }
    if (type_is_float(source_type) && !type_is_float(target_type)) {
        if (type_is_long(target_type))
            helper = (target_type & TYPE_UNSIGNED) != 0 ? "__fful" : "__ffl";
        else
            helper = ((target_type & TYPE_UNSIGNED) != 0 ||
                      type_ptr_depth(target_type) > 0) ? "__ffu" : "__ffi";
        fprintf(out, "\textrn %s\n\tcall %s\n", helper, helper);
        if (type_size(target_type) == 1) {
            if ((target_type & TYPE_UNSIGNED) != 0)
                fputs("\tld h,0\n", out);
            else
                fputs("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n", out);
        }
        return 1;
    }
    if (type_size(target_type) == 4 && type_size(source_type) <= 2) {
        if ((source_type & TYPE_UNSIGNED) != 0 ||
            type_ptr_depth(source_type) > 0)
            fputs("\tld de,0\n", out);
        else
            fputs("\tld a,h\n\trlca\n\tsbc a,a\n\tld d,a\n\tld e,a\n", out);
        return 1;
    }
    if (type_size(target_type) == 1) {
        if ((target_type & TYPE_UNSIGNED) != 0)
            fputs("\tld h,0\n", out);
        else
            fputs("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n", out);
        return 1;
    }
    if (type_size(target_type) <= 2 && type_size(source_type) == 4)
        return 1;
    return 1;
}

static int mir_emit_spilled_phi_copies(FILE *out, int predecessor,
                                       int successor)
{
    int predecessor_label = mir_block_label_before(predecessor);
    int edge_label = -1;
    int instruction = mir_first_nonlabel_successor(successor);
    int sources[MAX_FLOW];
    int destinations[MAX_FLOW];
    int copy_count = 0;
    int copy;

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
            return 0;
        if (copy_count >= (int)(sizeof(sources) / sizeof(sources[0])))
            return 0;
        sources[copy_count] = source;
        destinations[copy_count] = phi->dst;
        ++copy_count;
        ++instruction;
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
 * Returns 1 and sets *store_index to the sole store instruction re-writing
 * the result back to the same frame slot iff every one of the following
 * holds: the operator is '+' or '-' against the exact constant 1; the
 * left operand's value is bound to a 16-bit non-pointer local/parameter
 * memory location; and the result value (insn->dst) has exactly one use
 * anywhere in the function, which is a plain MIR_STORE writing to that
 * identical memory location.
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
        (memory_storage != SC_LOCAL && memory_storage != SC_PARAM) ||
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
        fprintf(out, "\tjp nz,L%d\n", done);
        fprintf(out, "\tinc (ix%+d)\n", offset + 1);
    } else {
        fprintf(out, "\tld a,(ix%+d)\n", offset);
        fprintf(out, "\tdec (ix%+d)\n", offset);
        fputs("\tor a\n", out);
        fprintf(out, "\tjp nz,L%d\n", done);
        fprintf(out, "\tdec (ix%+d)\n", offset + 1);
    }
    fprintf(out, "L%d:\n", done);
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
    return mir.local_bytes + mir.aggregate_temp_bytes +
           2 * mir_prepare_backend_slots();
}

int mir_try_emit_spilled_scalar_cfg(FILE *out)
{
    int *labels;
    int frame_bytes;
    int i;
    int accepted = 0;

    mir_spilled_scalar_cfg_elided_epilogue_bytes = 0;
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
    frame_bytes = mir_current_frame_bytes();
    mir_backend_slots_skip_fused_comparisons = 0;
    if (getenv("DCC_MIR_SELECT_REPORT") != NULL)
        fprintf(stderr,
                "; MIR scalar-cfg frame function=%s locals=%d slots=%d bytes=%d\n",
                mir.name, mir.local_bytes + mir.aggregate_temp_bytes,
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
    mir_forwarded_stack_value = -1;
    mir_forwarded_stack_instruction = -1;
    mir_cached_call_value = -1;
    mir_cached_call_instruction = -1;
    mir_cached_wide_call_value = -1;
    mir_cached_wide_call_instruction = -1;
    if (opt_stack_check)
        fputs("\textrn __stchk\n\tcall __stchk\n", out);
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        int end_label;

        mir_emit_instruction_index = i;

        switch (insn->opcode) {
        case MIR_NOP:
            break;
        case MIR_LABEL:
            if (insn->label < 0 || insn->label >= mir.next_label)
                goto done;
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
            if (insn->opcode == MIR_PARAM &&
                mir_param_value_is_direct(insn->dst))
                /* mir-migration-plan-next10: this parameter value never
                 * gets a backend slot (see mir_param_value_is_direct) -
                 * every real use reloads directly from the parameter's own
                 * ix+N home (mir_emit_virtual_load[_wide]), so the load
                 * this MIR_PARAM instruction would otherwise perform here
                 * is dead work; skip it entirely rather than load-then-
                 * discard. */
                break;
            if (insn->dst >= 0 && mir.backend_slots != NULL &&
                mir.backend_slots[insn->dst] < 0 &&
                !mir_value_has_use(insn->dst))
                break;
            if ((type_size(insn->type) == 2 || type_size(insn->type) == 4) &&
                mir_load_is_single_call_argument(insn->dst,
                                                  type_size(insn->type)))
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
                    if (memory_storage == SC_EXTERN)
                        fprintf(out, "\textrn %s\n", assembly_name);
                    fprintf(out, "\tld hl,%s\n", assembly_name);
                } else {
                    fputs("\tpush ix\n\tpop hl\n", out);
                    if (memory_offset != 0)
                        fprintf(out, "\tld de,%d\n\tadd hl,de\n",
                                memory_offset);
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
                        fprintf(out, "\tjp z,L%d\n\tinc hl\nL%d:\n",
                                bool_label, bool_label);
                    } else if ((memory_type & TYPE_UNSIGNED) != 0)
                        fputs("\tld l,a\n\tld h,0\n", out);
                    else {
                        int sign_label = new_label();
                        fputs("\tld l,a\n\tld h,0\n\tbit 7,l\n", out);
                        fprintf(out, "\tjp z,L%d\n\tdec h\nL%d:\n",
                                sign_label, sign_label);
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
                if (memory_storage == SC_EXTERN ||
                    (memory_storage == SC_FUNC && global != NULL &&
                     global->needs_extrn))
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
                    fprintf(out, "\tjp z,L%d\n\tinc hl\nL%d:\n",
                            bool_label, bool_label);
                } else if ((memory_type & TYPE_UNSIGNED) != 0) {
                    fputs("\tld h,0\n", out);
                } else {
                    end_label = new_label();
                    fputs("\tld h,0\n\tbit 7,l\n", out);
                    fprintf(out, "\tjp z,L%d\n\tdec h\nL%d:\n",
                            end_label, end_label);
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
                mir_call_only_constant(insn->dst) ||
                mir_binary_only_constant(insn->dst) ||
                mir_multiply_by_small_constant(insn->dst))
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
            if (mir_call_only_constant(insn->dst))
                break;
            fprintf(out, "\tld hl,%lu\n\tld de,%lu\n",
                    (unsigned long)insn->immediate & 0xffffUL,
                    ((unsigned long)insn->immediate >> 16) & 0xffffUL);
            mir_emit_virtual_store_wide(out, insn->dst);
            break;
        case MIR_STRING_ADDRESS:
            if (mir_call_only_constant(insn->dst))
                break;
            fprintf(out, "\tld hl,S%ld\n", insn->immediate);
            mir_emit_virtual_store(out, insn->dst);
            break;
        case MIR_ADDRESS:
            {
            int memory_type;
            int memory_storage;
            int memory_offset;
            struct Sym *global = find_global(insn->name);
            if (!mir_scalar_memory_location(insn, &memory_type,
                                            &memory_storage, &memory_offset))
                goto done;
            if ((global != NULL && global->storage == SC_FUNC) ||
                memory_storage == SC_GLOBAL || memory_storage == SC_EXTERN ||
                memory_storage == SC_FUNC) {
                const char *assembly_name = asm_name_for(
                    global != NULL ? sym_asm_name(global)
                                   : mir_declared_link_name(insn->name));
                if (memory_storage == SC_EXTERN ||
                    (global != NULL && global->storage == SC_FUNC &&
                     global->needs_extrn))
                    fprintf(out, "\textrn %s\n", assembly_name);
                fprintf(out, "\tld hl,%s\n", assembly_name);
            } else if (mir_declared_is_vla_object(insn->name)) {
                fprintf(out, "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
                        memory_offset, memory_offset + 1);
            } else {
                fputs("\tpush ix\n\tpop hl\n", out);
                if (memory_offset != 0)
                    fprintf(out, "\tld de,%d\n\tadd hl,de\n", memory_offset);
            }
            mir_emit_virtual_store(out, insn->dst);
            break;
            }
        case MIR_COMPOUND_ADDRESS:
            fputs("\tpush ix\n\tpop hl\n", out);
            if (insn->immediate != 0)
                fprintf(out, "\tld de,%ld\n\tadd hl,de\n", insn->immediate);
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
            mir_emit_virtual_load(out, insn->src1);
            if (insn->immediate != 0)
                fprintf(out, "\tld de,%ld\n\tadd hl,de\n", insn->immediate);
            mir_emit_virtual_store(out, insn->dst);
            break;
        case MIR_INDEX_ADDRESS:
            {
            const struct MirInsn *index_definition =
                mir_definition(insn->src2);
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
                fputs("\tpop hl\n\textrn __mulu\n\tcall __mulu\n", out);
                if (insn->secondary_offset == 2)
                    fputs("\tadd hl,hl\n", out);
                else if (insn->secondary_offset > 2) {
                    fprintf(out, "\tld de,%d\n\textrn __mulu\n"
                                 "\tcall __mulu\n",
                            insn->secondary_offset);
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
                if (mir_forwarded_stack_value == insn->src1 &&
                    mir_forwarded_stack_instruction + 2 == i) {
                    fputs("\tpop hl\n", out);
                    mir_forwarded_stack_value = -1;
                    mir_forwarded_stack_instruction = -1;
                } else
                    mir_emit_virtual_load(out, insn->src1);
                if (byte_offset != 0)
                    fprintf(out, "\tld de,%ld\n\tadd hl,de\n",
                            byte_offset & 0xffffL);
            } else {
                mir_emit_virtual_load(out, insn->src2);
                if (insn->immediate != 1) {
                    fprintf(out,
                            "\tld de,%ld\n\textrn __mulu\n\tcall __mulu\n",
                            insn->immediate);
                }
                if (mir_forwarded_stack_value == insn->src1 &&
                    mir_forwarded_stack_instruction + 2 == i) {
                    fputs("\tpop de\n\tadd hl,de\n", out);
                    mir_forwarded_stack_value = -1;
                    mir_forwarded_stack_instruction = -1;
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
                    fprintf(out, "\tjp z,L%d\n\tinc hl\nL%d:\n",
                            end_label, end_label);
                } else if ((insn->type & TYPE_UNSIGNED) != 0) {
                    fputs("\tld h,0\n", out);
                } else {
                    end_label = new_label();
                    fputs("\tld h,0\n\tbit 7,l\n", out);
                    fprintf(out, "\tjp z,L%d\n\tdec h\nL%d:\n",
                            end_label, end_label);
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
                int byte;
                int size = type_size(memory_type);
                mir_emit_virtual_load(out, insn->src1);
                fputs("\tex de,hl\n", out);
                if (memory_storage == SC_GLOBAL ||
                    memory_storage == SC_EXTERN) {
                    struct Sym *global = find_global(insn->name);
                    const char *assembly_name = asm_name_for(
                        global != NULL ? sym_asm_name(global)
                                       : mir_declared_link_name(insn->name));
                    if (memory_storage == SC_EXTERN)
                        fprintf(out, "\textrn %s\n", assembly_name);
                    fprintf(out, "\tld hl,%s\n", assembly_name);
                } else {
                    fputs("\tpush ix\n\tpop hl\n", out);
                    if (memory_offset != 0)
                        fprintf(out, "\tld bc,%d\n\tadd hl,bc\n",
                                memory_offset);
                }
                for (byte = 0; byte < size; ++byte) {
                    fputs("\tld a,(de)\n\tld (hl),a\n", out);
                    if (byte + 1 < size)
                        fputs("\tinc de\n\tinc hl\n", out);
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
            mir_emit_virtual_load(out, insn->src1);
            if (memory_storage == SC_GLOBAL || memory_storage == SC_EXTERN) {
                struct Sym *global = find_global(insn->name);
                const char *assembly_name = asm_name_for(
                    global != NULL ? sym_asm_name(global)
                                   : mir_declared_link_name(insn->name));
                if (memory_storage == SC_EXTERN)
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
                else
                    fprintf(out, "\tld (%s),hl\n", assembly_name);
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
                if (type_size(memory_type) == 2)
                    fprintf(out, "\tld (ix%+d),h\n", memory_offset + 1);
            }
            break;
            }
        case MIR_STORE_INDIRECT:
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
                fputs("\textrn __stchk\n\tcall __stchk\n", out);
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
                int byte;
                if (insn->memory_size <= 0 || insn->memory_size > 1024)
                    goto done;
                mir_emit_virtual_load(out, insn->src1);
                fputs("\tpush hl\n", out);
                mir_emit_virtual_load(out, insn->src2);
                fputs("\tld b,h\n\tld c,l\n\tpop hl\n", out);
                for (byte = 0; byte < insn->memory_size; ++byte) {
                    fputs("\tld a,(bc)\n\tld (hl),a\n", out);
                    if (byte + 1 < insn->memory_size)
                        fputs("\tinc bc\n\tinc hl\n", out);
                }
            }
            break;
        case MIR_UNARY:
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
                      fprintf(out, "\tjp nz,L%d\n\tinc de\nL%d:\n",
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
                fprintf(out, "\tjp z,L%d\n\tjp L%d\nL%d:\n\tinc hl\nL%d:\n",
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
            if (mir_binary_is_selfstore_incdec(i, &selfstore_store_index)) {
                int memory_type, memory_storage, memory_offset;
                mir_scalar_memory_location(mir_definition(insn->src1),
                                            &memory_type, &memory_storage,
                                            &memory_offset);
                mir_emit_selfstore_incdec(out, memory_offset,
                                          insn->immediate == '+');
                break;
            }
            }
            if (type_size(insn->secondary_offset) == 4) {
                int fuse_skip = mir_binary_is_fusable_comparison(i);
                mir_emit_virtual_load_wide(out, insn->src1);
                fputs("\tpush de\n\tpush hl\n", out);
                mir_emit_virtual_load_wide(out, insn->src2);
                if (!mir_emit_wide_operation(out, insn))
                    goto done;
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
                if (mir_binary_only_constant(insn->src1)) {
                    const struct MirInsn *constant =
                        mir_definition(insn->src1);
                    fprintf(out, "\tld hl,%ld\n",
                            constant->immediate & 0xffffL);
                } else
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
                        fputs("\textrn __udivmod\n\tcall __udivmod\n", out);
                    else
                        fputs("\textrn __sdivmod\n\tcall __sdivmod\n", out);
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
                /* A constant divisor/modulus needs no dividend-preserving
                 * push/pop dance: the dividend is already sitting in HL
                 * from src1's evaluation above and a constant load cannot
                 * clobber it, so the divisor can be materialized directly
                 * into DE (Item 16). Every other operator keeps the
                 * existing push/ex/pop sequence, since src2 there may be
                 * evaluated via a call or memory access that does clobber
                 * HL.
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
                if ((insn->immediate == '/' || insn->immediate == '%') &&
                    !mir.has_vla &&
                    mir_binary_only_constant(insn->src2)) {
                    const struct MirInsn *constant =
                        mir_definition(insn->src2);
                    fprintf(out, "\tld de,%ld\n",
                            constant->immediate & 0xffffL);
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
                } else {
                    fputs("\tpush hl\n", out);
                    if (mir_binary_only_constant(insn->src2)) {
                        const struct MirInsn *constant =
                            mir_definition(insn->src2);
                        fprintf(out, "\tld hl,%ld\n",
                                constant->immediate & 0xffffL);
                    } else
                        mir_emit_virtual_load(out, insn->src2);
                    fputs("\tex de,hl\n\tpop hl\n", out);
                }
                {
                    int fuse_skip = mir_binary_is_fusable_comparison(i);
                    if (fuse_skip > 0) {
                        if (!mir_emit_fused_comparison_branch(
                                out, labels, i, fuse_skip - 1))
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
                if (!mir_emit_scalar_operation(out, insn))
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
                    fputs("\tld b,h\n\tld c,l\n\tpop de\n\tpop hl\n"
                          "\textrn __msf\n\tcall __msf\n", out);
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
                    fputs("\textrn __slf\n\tcall __slf\n", out);
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
                    fputs("\tld a,l\n\tpop hl\n"
                          "\textrn __chf\n\tcall __chf\n", out);
                    mir_emit_virtual_store(out, insn->dst);
                    break;
                }
                if (!is_indirect &&
                    mir_call_is_strrchr_fastcall(i, &s_value, &c_value)) {
                    mir_emit_spilled_arg_to_hl(out, s_value);
                    fputs("\tpush hl\n", out);
                    mir_emit_spilled_arg_to_hl(out, c_value);
                    fputs("\tld a,l\n\tpop hl\n"
                          "\textrn __rcf\n\tcall __rcf\n", out);
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
                    fputs("\tld b,h\n\tld c,l\n\tpop de\n\tpop hl\n"
                          "\textrn __mhf\n\tcall __mhf\n", out);
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
                    fputs("\tld b,h\n\tld c,l\n\tpop hl\n\tpop de\n"
                          "\textrn __cmpf\n\tcall __cmpf\n", out);
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
                    fputs("\tld b,h\n\tld c,l\n\tpop hl\n\tpop de\n"
                          "\textrn __mcf\n\tcall __mcf\n", out);
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
                    mir_emit_spilled_arg_to_hl(out, s1_value);
                    fputs("\tpush hl\n", out);
                    mir_emit_spilled_arg_to_hl(out, s2_value);
                    fputs("\tpop de\n", out);
                    fprintf(out, "\textrn %s\n\tcall %s\n", rtl_name,
                            rtl_name);
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
                    fprintf(out, "\textrn %s\n\tcall %s\n", rtl_name,
                            rtl_name);
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
                        fputs("\tpush de\n\tpush hl\n", out);
                        if (cached)
                            fputs("\texx\n", out);
                        argument_bytes += 4;
                    } else {
                        if (!mir_emit_cached_call_argument(out, arg->src1) &&
                            !mir_emit_rematerialized_argument(
                                out, arg->src1, size))
                            mir_emit_virtual_load(out, arg->src1);
                        fputs("\tpush hl\n", out);
                        argument_bytes += 2;
                    }
                }
                if (argument != -1)
                    goto done;
                if ((insn->memory_flags & 32) != 0)
                    fputs("\textrn __pfehx\n\tcall __pfehx\n", out);
                if ((insn->memory_flags & 64) != 0)
                    fputs("\textrn __pfeoc\n\tcall __pfeoc\n", out);
                if (is_indirect) {
                    int return_label = new_label();
                    mir_emit_virtual_load(out, insn->src1);
                    fprintf(out, "\tld de,L%d\n\tpush de\n\tjp (hl)\nL%d:\n",
                        return_label, return_label);
                } else {
                    if (callee == NULL || callee->needs_extrn)
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
                        fputs("\tpush de\n\tpush hl\n", out);
                        if (cached)
                            fputs("\texx\n", out);
                        argument_bytes += 4;
                    } else {
                        if (!mir_emit_cached_call_argument(out, arg->src1) &&
                            !mir_emit_rematerialized_argument(
                                out, arg->src1, size))
                            mir_emit_virtual_load(out, arg->src1);
                        fputs("\tpush hl\n", out);
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
                    if (destination != NULL && destination->needs_extrn)
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
                    fprintf(out, "\tld de,%ld\n\tadd hl,de\n",
                            insn->immediate);
                fputs("\tpush hl\n", out);
                if (callee == NULL || callee->needs_extrn)
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
                    fprintf(out, "\tld de,%ld\n\tadd hl,de\n",
                            insn->immediate);
                mir_emit_virtual_store(out, insn->dst);
            }
            break;
        case MIR_VA_START:
            if (insn->immediate < -128 || insn->immediate + 1 > 127)
                goto done;
            fputs("\tpush ix\n\tpop hl\n", out);
            if (insn->secondary_offset != 0)
                fprintf(out, "\tld de,%d\n\tadd hl,de\n",
                        insn->secondary_offset);
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
            }
            fprintf(out, "\tjp L%d\n", labels[insn->label]);
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
                    fprintf(out, "\tjp z,L%d\n", labels[insn->label]);
                } else {
                    int fallthrough_label = new_label();
                    char buf[256];
                    int remaining = phi_bytes;
                    fprintf(out, "\tjp nz,L%d\n", fallthrough_label);
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
            mir_emit_virtual_iy_epilogue(out);
            break;
        default:
            goto done;
        }
        if (insn->opcode != MIR_JUMP && insn->opcode != MIR_BRANCH_FALSE &&
            insn->opcode != MIR_RETURN && i + 1 < mir.count &&
            mir.insns[i + 1].opcode == MIR_LABEL) {
            int first = mir_first_nonlabel_successor(i + 1);
            int predecessor_label = mir_block_label_before(i);
            if (first >= 0 && first < mir.count &&
                mir.insns[first].opcode == MIR_PHI &&
                (mir.insns[first].phi_pred1 == predecessor_label ||
                 mir.insns[first].phi_pred2 == predecessor_label) &&
                !mir_emit_spilled_phi_copies(out, i, i + 1))
                goto done;
        }
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
    accepted = 1;
done:
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
    mir_forwarded_stack_value = -1;
    mir_forwarded_stack_instruction = -1;
    mir_cached_call_value = -1;
    mir_cached_call_instruction = -1;
    if (!accepted && getenv("DCC_MIR_SELECT_REPORT") != NULL)
        fprintf(stderr, "; MIR scalar-cfg reject function=%s insn=%d opcode=%s\n",
                mir.name, i,
                i >= 0 && i < mir.count
                    ? mir_opcode_name(mir.insns[i].opcode) : "preflight");
    free(labels);
    return accepted;
}
