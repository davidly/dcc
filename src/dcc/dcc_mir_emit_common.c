/* dcc_mir_emit_common.c - shared scalar-value emission helpers used by
 * more than one MIR selector (homed prologue/epilogue, home<->HL/DE
 * moves, PHI copies, comparison fusion helpers) plus the
 * mir_try_emit_scalar_dag / mir_try_emit_homed_scalar_dag selectors
 * that are built directly on top of them.
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

void mir_emit_hl_and_const(FILE *out, unsigned int mask)
{
    fprintf(out,
            "\tld a,l\n\tand %u\n\tld l,a\n"
            "\tld a,h\n\tand %u\n\tld h,a\n",
            mask & 0xffU, (mask >> 8) & 0xffU);
}

void mir_emit_hl_or_const(FILE *out, unsigned int mask)
{
    fprintf(out,
            "\tld a,l\n\tor %u\n\tld l,a\n"
            "\tld a,h\n\tor %u\n\tld h,a\n",
            mask & 0xffU, (mask >> 8) & 0xffU);
}

void mir_emit_bitfield_extract(FILE *out, const struct MirInsn *insn)
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
        fprintf(out, "\tjp z, L%d\n", sign_label);
        mir_emit_hl_or_const(out, (~value_mask) & 0xffffU);
        fprintf(out, "L%d:\n", sign_label);
    }
}

/* Centralized resolver for MIR address values rooted in a named scalar
 * location. Walks one value-definition chain of MEMBER_ADDRESS and fixed-
 * stride constant INDEX_ADDRESS steps back to its MIR_ADDRESS root, recording
 * the root symbol, total byte offset, whether any INDEX_ADDRESS occurred, and
 * the first (outermost) member name. Dynamic/non-constant index expressions
 * remain unresolved: callers that need an exact byte offset (absolute
 * addressing, isolated global-field loads) must decline those. */
int mir_resolve_named_address(
    int value, struct MirResolvedNamedAddress *out)
{
    const struct MirInsn *definition;
    int memory_type;
    int memory_storage;
    int memory_offset;
    long member_offset = 0;

    memset(out, 0, sizeof(*out));
    out->storage = -1;
    definition = mir_definition(value);
    while (definition != NULL) {
        if (definition->opcode == MIR_MEMBER_ADDRESS) {
            if (out->member_depth == 0 && definition->name[0] != 0) {
                strncpy(out->leaf_member_name, definition->name,
                        sizeof(out->leaf_member_name) - 1);
                out->leaf_member_name[
                    sizeof(out->leaf_member_name) - 1] = 0;
            }
            ++out->member_depth;
            member_offset += definition->immediate;
            definition = mir_definition(definition->src1);
            continue;
        }
        if (definition->opcode == MIR_INDEX_ADDRESS) {
            const struct MirInsn *index = mir_definition(definition->src2);
            out->has_index = 1;
            if (definition->base_name[0] != 0 || index == NULL ||
                index->opcode != MIR_CONST)
                return 0;
            member_offset += index->immediate * definition->immediate;
            definition = mir_definition(definition->src1);
            continue;
        }
        break;
    }
    if (definition == NULL || definition->opcode != MIR_ADDRESS ||
        !mir_scalar_memory_location(definition, &memory_type,
                                    &memory_storage, &memory_offset))
        return 0;

    out->root = definition;
    out->storage = memory_storage;
    out->offset = member_offset + memory_offset;
    strncpy(out->base_name, definition->name, sizeof(out->base_name) - 1);
    out->base_name[sizeof(out->base_name) - 1] = 0;
    return 1;
}

/* Resolve the narrow absolute-addressing shape already optimized by the
 * legacy AST backend: a global/extern object's address followed by constant
 * member offsets and/or fixed-stride constant indexes. Genuine external
 * references with a nonzero addend are excluded because Link-80 mis-relocates
 * EXTRN+offset. */
static int mir_resolve_constant_absolute_address(
    int value, const struct MirInsn **base_out, int *storage_out,
    long *offset_out)
{
    struct MirResolvedNamedAddress resolved;
    struct Sym *global;

    if (!mir_resolve_named_address(value, &resolved) ||
        (resolved.storage != SC_GLOBAL && resolved.storage != SC_EXTERN))
        return 0;

    global = find_global(resolved.base_name);
    if (resolved.storage == SC_EXTERN && resolved.offset != 0 &&
        (global == NULL || (global->needs_extrn && !global->is_defined)))
        return 0;

    *base_out = resolved.root;
    *storage_out = resolved.storage;
    *offset_out = resolved.offset;
    return 1;
}

/* An exact, isolated static-global field address: direct `base.field`
 * (no index steps) rooted at a file-local static global whose own address and
 * the field's address are never taken anywhere in the translation unit. That
 * proof means indirect writes/calls cannot alias the field unless the field
 * itself is textually assigned, so block-local value numbering may treat the
 * field as an exact scalar location with explicit kills instead of a generic
 * opaque pointer dereference. */
int mir_resolve_isolated_global_field_address(
    int value, struct MirResolvedNamedAddress *out)
{
    struct Sym *base_symbol;

    if (!mir_resolve_named_address(value, out) ||
        out->storage != SC_GLOBAL || out->member_depth != 1 ||
        out->has_index || out->leaf_member_name[0] == 0)
        return 0;
    base_symbol = find_sym(out->base_name);
    if (base_symbol == NULL || !base_symbol->is_static)
        return 0;
    if (global_text_addr_taken_count(out->base_name) != 0)
        return 0;
    if (global_text_field_addr_taken_count(
            out->base_name, out->leaf_member_name) != 0)
        return 0;
    return 1;
}

int mir_constant_absolute_access_supported(const struct MirInsn *insn)
{
    const struct MirInsn *base;
    int storage;
    long offset;

    return insn != NULL &&
        (insn->opcode == MIR_LOAD_INDIRECT ||
         insn->opcode == MIR_STORE_INDIRECT) &&
        insn->bit_width == 0 &&
        (insn->memory_size == 1 || insn->memory_size == 2) &&
        mir_resolve_constant_absolute_address(
            insn->src1, &base, &storage, &offset);
}

int mir_constant_absolute_address_has_index(int value)
{
    struct MirResolvedNamedAddress resolved;

    return mir_resolve_named_address(value, &resolved) && resolved.has_index;
}

/* T383 (mir-text-size-plan.md): true only when this value's own chain of
 * constant MEMBER_ADDRESS/INDEX_ADDRESS steps rooted at a global/extern
 * resolves to one compile-time absolute address, exposed for
 * mir_value_only_used_by_absolute_access's recursive base-liveness check
 * below and for direct emission-time materialization
 * (mir_emit_constant_absolute_address_value in dcc_mir_spilled_cfg.c). */
int mir_value_is_constant_absolute_address(int value)
{
    const struct MirInsn *base;
    int storage;
    long offset;

    return mir_resolve_constant_absolute_address(
        value, &base, &storage, &offset);
}

/* True only when every use of an address value is another constant member
 * step or a supported direct absolute load/store. This lets slot preparation
 * and emission remove the complete dead address chain, not just optimize the
 * final dereference while retaining its frame traffic.
 *
 * A MEMBER_ADDRESS/INDEX_ADDRESS consumer whose own value is itself a
 * resolved constant absolute address is accepted unconditionally, not only
 * when its further uses recursively qualify: mir_emit_constant_absolute_-
 * address_value fires for such a consumer regardless of how its result is
 * later used (dereferenced, stored as data, or passed as an argument), so
 * this value's base is provably never read at emission time either way. */
int mir_value_only_used_by_absolute_access(
    int value, int (*access_supported)(const struct MirInsn *))
{
    int instruction;
    int found_use = 0;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->src2 == value || mir_call_uses_value(insn, value))
            return 0;
        if (insn->src1 != value)
            continue;
        found_use = 1;
        if ((insn->opcode == MIR_MEMBER_ADDRESS ||
             insn->opcode == MIR_INDEX_ADDRESS) &&
            (mir_value_is_constant_absolute_address(insn->dst) ||
             mir_value_only_used_by_absolute_access(
                 insn->dst, access_supported)))
            continue;
        if (access_supported(insn))
            continue;
        return 0;
    }
    return found_use;
}

int mir_value_only_used_by_constant_absolute_address(int value)
{
    return mir_value_only_used_by_absolute_access(
        value, mir_constant_absolute_access_supported);
}

int mir_prepare_constant_absolute_operand(
    FILE *out, int value, char *operand, size_t operand_size)
{
    const struct MirInsn *base;
    struct Sym *global;
    const char *assembly_name;
    int storage;
    long offset;
    int written;

    if (!mir_resolve_constant_absolute_address(
            value, &base, &storage, &offset))
        return 0;
    global = find_global(base->name);
    assembly_name = asm_name_for(
        global != NULL ? sym_asm_name(global)
                       : mir_declared_link_name(base->name));
    if (storage == SC_EXTERN && mir_extrn_should_emit(global))
        fprintf(out, "\textrn %s\n", assembly_name);
    written = offset == 0
        ? snprintf(operand, operand_size, "%s", assembly_name)
        : snprintf(operand, operand_size, "%s%+ld", assembly_name, offset);
    return written >= 0 && (size_t)written < operand_size;
}

void mir_emit_prologue(FILE *out)
{
    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
}

void mir_emit_iy_prologue(FILE *out)
{
    fputs("\tpush iy\n", out);
    mir_emit_prologue(out);
}

int mir_emit_load_param(FILE *out, const struct MirInsn *param)
{
    const struct MirObject *object;

    if (param == NULL || param->opcode != MIR_PARAM || param->object < 0 ||
        param->object >= mir.object_count)
        return 0;
    object = &mir.objects[param->object];
    if (object->storage != SC_PARAM || type_size(object->type) != 2)
        return 0;
    fprintf(out, "\tld l,(ix%+d)\n", object->offset);
    fprintf(out, "\tld h,(ix%+d)\n", object->offset + 1);
    return 1;
}

int mir_emit_load_param_de(FILE *out, const struct MirInsn *param)
{
    const struct MirObject *object;

    if (param == NULL || param->opcode != MIR_PARAM || param->object < 0 ||
        param->object >= mir.object_count)
        return 0;
    object = &mir.objects[param->object];
    if (object->storage != SC_PARAM || type_size(object->type) != 2)
        return 0;
    fprintf(out, "\tld e,(ix%+d)\n", object->offset);
    fprintf(out, "\tld d,(ix%+d)\n", object->offset + 1);
    return 1;
}

/* Item T57 (mir-text-size-plan.md): wide (4-byte, `long`) counterpart of
 * mir_emit_load_param/_de for mir_try_emit_comparison_branch - that
 * selector's whole-function shape only ever needs a parameter's value
 * loaded straight from its own fixed ix-relative home (no slot, no
 * general value-forwarding machinery), exactly like the narrow case,
 * just twice as wide. Mirrors mir_param_value_is_direct's wide-load
 * ascending offset convention (offset/offset+1 = low word into HL,
 * offset+2/offset+3 = high word into DE) rather than the descending
 * backend-slot convention used elsewhere. Returns 0 (declining to
 * emit anything) when the fixed ix-relative offset does not fit in a
 * single signed byte, so the caller can fall back cleanly instead of
 * emitting a wrong or oversized load. */
int mir_emit_load_param_wide(FILE *out, const struct MirInsn *param)
{
    const struct MirObject *object;

    if (param == NULL || param->opcode != MIR_PARAM || param->object < 0 ||
        param->object >= mir.object_count)
        return 0;
    object = &mir.objects[param->object];
    if (object->storage != SC_PARAM || type_size(object->type) != 4)
        return 0;
    if (object->offset < -128 || object->offset + 3 > 127)
        return 0;
    fprintf(out, "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            object->offset, object->offset + 1);
    fprintf(out, "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
            object->offset + 2, object->offset + 3);
    return 1;
}

/* Recognize VALUE as one parameter plus a constant. PARAM is NULL for a pure
 * constant. This is intentionally not a general expression selector; it is a
 * proof that promoted local temporaries can disappear end-to-end before the
 * backend grows arbitrary register/stack expression handling. */
int mir_affine_value(int value, const struct MirInsn **parameter,
                            long *constant, int depth)
{
    const struct MirInsn *definition;
    const struct MirInsn *left_parameter;
    const struct MirInsn *right_parameter;
    long left_constant;
    long right_constant;

    if (depth > 64)
        return 0;
    definition = mir_definition(value);
    if (definition == NULL)
        return 0;
    if (definition->opcode == MIR_PARAM) {
        *parameter = definition;
        *constant = 0;
        return 1;
    }
    if (definition->opcode == MIR_CONST) {
        *parameter = NULL;
        *constant = definition->immediate;
        return 1;
    }
    if (definition->opcode != MIR_BINARY ||
        (definition->immediate != '+' && definition->immediate != '-'))
        return 0;
    if (!mir_affine_value(definition->src1, &left_parameter, &left_constant,
                          depth + 1) ||
        !mir_affine_value(definition->src2, &right_parameter, &right_constant,
                          depth + 1))
        return 0;
    if (definition->immediate == '+') {
        if (left_parameter != NULL && right_parameter != NULL)
            return 0;
        *parameter = left_parameter != NULL ? left_parameter : right_parameter;
        *constant = left_constant + right_constant;
        return 1;
    }
    /* PARAM-or-constant minus a parameter needs coefficient -1, outside the
     * first affine subset. */
    if (right_parameter != NULL)
        return 0;
    *parameter = left_parameter;
    *constant = left_constant - right_constant;
    return 1;
}

/* mir-text-size Item T21 (mir-text-size-plan.md): a signed 1-byte load's
 * sign extension into H was previously always done with a conditional
 * branch (`ld h,0 / bit 7,l / jp z,LN / dec h / LN:`, 8 bytes across a
 * taken/not-taken split), duplicated identically across five call sites
 * in dcc_mir_spilled_cfg.c and dcc_mir_emit_common.c. Legacy's own backend
 * instead uses the standard branchless Z80 idiom for this: `rlca` rotates
 * bit 7 into the carry flag (leaving L's own bits unmodified, since the
 * rotated copy lives in A, not L), then `sbc a,a` turns that carry into a
 * full 0x00/0xFF byte (A = A-A-carry = -carry) with no branch at all -
 * both smaller (4 bytes) and free of a taken-or-not-taken execution-path
 * split. Found via tests/tatof.c's chk_end(), which newly crossed the
 * text-size threshold as a side effect of Item T20's call-argument
 * rematerialization work and briefly regressed nopeep cycles by a few
 * dozen until this shared helper replaced the branchy sequence at every
 * call site. */
void mir_emit_signed_byte_extend(FILE *out)
{
    fputs("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n", out);
}

void mir_emit_scalar_compare(FILE *out, int operation, int is_unsigned)
{
    int true_label = new_label();
    int end_label = new_label();

    if (operation == '>' || operation == TOK_LE) {
        fputs("\tex de,hl\n", out);
        operation = operation == '>' ? '<' : TOK_GE;
    }
    if (!is_unsigned && operation != TOK_EQ && operation != TOK_NE)
        fputs("\tld a,h\n\txor 128\n\tld h,a\n"
              "\tld a,d\n\txor 128\n\tld d,a\n", out);
    fputs("\tor a\n\tsbc hl,de\n\tld hl,0\n", out);
    if (operation == TOK_EQ)
        fprintf(out, "\tjp z, L%d\n", true_label);
    else if (operation == TOK_NE)
        fprintf(out, "\tjp nz, L%d\n", true_label);
    else if (operation == '<')
        fprintf(out, "\tjp c, L%d\n", true_label);
    else
        fprintf(out, "\tjp nc, L%d\n", true_label);
    fprintf(out, "\tjp L%d\nL%d:\n\tinc l\nL%d:\n",
            end_label, true_label, end_label);
}

int mir_type_uses_unsigned_comparison(int type)
{
    return (type & TYPE_UNSIGNED) != 0 || type_ptr_depth(type) > 0;
}

void mir_emit_scalar_compare_biased_right(FILE *out, int operation)
{
    int true_label = new_label();
    int end_label = new_label();

    fputs("\tld a,h\n\txor 128\n\tld h,a\n"
          "\tsbc hl,de\n\tld hl,0\n", out);
    if (operation == '<')
        fprintf(out, "\tjp c, L%d\n", true_label);
    else
        fprintf(out, "\tjp nc, L%d\n", true_label);
    fprintf(out, "\tjp L%d\nL%d:\n\tinc l\nL%d:\n",
            end_label, true_label, end_label);
}

/* Item T44 (mir-text-size-plan.md): a shift whose amount operand is a
 * compile-time constant previously still went through the same runtime
 * bit-at-a-time loop as a variable shift count (`ld b,e / ld a,b / or a /
 * jp z,Lend / Lloop: srl h/rr l (or add hl,hl) / djnz Lloop / Lend:`),
 * even though the exact iteration count is already known at code-
 * generation time. Found via tests/tarray.c's aHexWord(), where legacy
 * recognizes `(val >> 8) & 0xff` as nothing more than a byte move (the
 * high byte of a 16-bit value moved into the low byte, high byte
 * zeroed) while the MIR path paid for the full generic loop (6-byte
 * setup plus one djnz-guarded iteration) for every shift whose count
 * happens to be a literal in the source.
 *
 * For any shift count known to be in [0,15] (the only meaningful range
 * for a 16-bit value - counts >= 16 are undefined behavior in C and are
 * left on the unmodified runtime-loop path below, matching prior
 * behavior exactly), unrolling into that many straight-line shift
 * instructions is *always* smaller AND faster than the loop: the loop
 * costs a 6-byte/~22-T-state setup plus count * (shift-body + 2-byte/
 * ~13-T-state djnz), while unrolling costs only count * shift-body with
 * no setup or per-iteration branch overhead at all. A shift by exactly
 * 8 is further special-cased as a single register move (plus zero/sign
 * extension of the vacated byte), mirroring legacy's recognition of
 * this exact shape. Not static: also called directly by Item T49's
 * scalar unsigned div/mod-by-power-of-2 fast path (both in this file's
 * mir_emit_scalar_value and dcc_mir_spilled_cfg.c's scalar binary-
 * operation caller), which computes the shift count as log2 of the
 * divisor rather than receiving a MIR value ID to inspect, so it calls
 * this helper directly instead of going through mir_emit_scalar_shift's
 * MIR_CONST-detection wrapper. */
void mir_emit_scalar_shift_by_constant(FILE *out, int operation,
                                       int is_unsigned, long count)
{
    long i;

    if (count == 0)
        return;
    if (count == 8) {
        if (operation == TOK_SHL) {
            fputs("\tld h,l\n\tld l,0\n", out);
        } else if (is_unsigned) {
            fputs("\tld l,h\n\tld h,0\n", out);
        } else {
            fputs("\tld l,h\n", out);
            mir_emit_signed_byte_extend(out);
        }
        return;
    }
    for (i = 0; i < count; ++i) {
        if (operation == TOK_SHL)
            fputs("\tadd hl,hl\n", out);
        else if (is_unsigned)
            fputs("\tsrl h\n\trr l\n", out);
        else
            fputs("\tsra h\n\trr l\n", out);
    }
}

void mir_emit_scalar_shift(FILE *out, int operation, int is_unsigned,
                           int count_value)
{
    const struct MirInsn *count_definition = mir_definition(count_value);
    int loop_label;
    int end_label;

    if (count_definition != NULL && count_definition->opcode == MIR_CONST) {
        long count = count_definition->immediate & 0xffffL;
        if (count < 16) {
            mir_emit_scalar_shift_by_constant(out, operation, is_unsigned,
                                              count);
            return;
        }
    }
    loop_label = new_label();
    end_label = new_label();
    fputs("\tld b,e\n\tld a,b\n\tor a\n", out);
    fprintf(out, "\tjp z, L%d\nL%d:\n", end_label, loop_label);
    if (operation == TOK_SHL)
        fputs("\tadd hl,hl\n", out);
    else if (is_unsigned)
        fputs("\tsrl h\n\trr l\n", out);
    else
        fputs("\tsra h\n\trr l\n", out);
    fprintf(out, "\tdjnz L%d\nL%d:\n", loop_label, end_label);
}

static int mir_emit_scalar_value(FILE *out, int value, int depth)
{
    const struct MirInsn *definition;
    const struct MirObject *object;
    int false_label;
    int end_label;

    if (depth > 256)
        return 0;
    definition = mir_definition(value);
    if (definition == NULL || type_size(definition->type) > 2)
        return 0;
    switch (definition->opcode) {
    case MIR_PARAM:
        if (definition->object < 0 || definition->object >= mir.object_count)
            return 0;
        object = &mir.objects[definition->object];
        if (object->storage != SC_PARAM || type_size(object->type) > 2)
            return 0;
        if (type_size(object->type) == 1) {
            fprintf(out, "\tld l,(ix%+d)\n", object->offset);
            if (type_is_bool(object->type)) {
                end_label = new_label();
                fputs("\tld a,l\n\tor a\n\tld hl,0\n", out);
                fprintf(out, "\tjp z, L%d\n\tinc hl\nL%d:\n",
                        end_label, end_label);
            } else if ((object->type & TYPE_UNSIGNED) != 0)
                fputs("\tld h,0\n", out);
            else
                mir_emit_signed_byte_extend(out);
        } else {
            fprintf(out, "\tld l,(ix%+d)\n", object->offset);
            fprintf(out, "\tld h,(ix%+d)\n", object->offset + 1);
        }
        return 1;
    case MIR_CONST:
        fprintf(out, "\tld hl,%ld\n", definition->immediate & 0xffffL);
        return 1;
    case MIR_UNARY:
        if (!mir_emit_scalar_value(out, definition->src1, depth + 1))
            return 0;
        if (definition->immediate == 0 || definition->immediate == '+')
            return 1;
        if (definition->immediate == '-') {
            fputs("\txor a\n\tsub l\n\tld l,a\n\tsbc a,a\n\tsub h\n\tld h,a\n", out);
            return 1;
        }
        if (definition->immediate == '~') {
            fputs("\tld a,l\n\tcpl\n\tld l,a\n\tld a,h\n\tcpl\n\tld h,a\n", out);
            return 1;
        }
        if (definition->immediate == '!') {
            false_label = new_label();
            end_label = new_label();
            fputs("\tld a,h\n\tor l\n\tld hl,0\n", out);
            fprintf(out, "\tjp nz, L%d\n\tinc hl\nL%d:\n", false_label,
                    false_label);
            (void)end_label;
            return 1;
        }
        return 0;
    case MIR_BINARY:
        if (!mir_emit_scalar_value(out, definition->src1, depth + 1))
            return 0;
        fputs("\tpush hl\n", out);
        if (!mir_emit_scalar_value(out, definition->src2, depth + 1))
            return 0;
        fputs("\tex de,hl\n\tpop hl\n", out);
        switch ((int)definition->immediate) {
        case '+': fputs("\tadd hl,de\n", out); return 1;
        case '-': fputs("\tor a\n\tsbc hl,de\n", out); return 1;
        case '&':
            {
                /* Item T48 (mir-text-size-plan.md): `int_expr &
                 * <compile-time constant>` mirrors legacy's
                 * emit_and_hl_const via the shared
                 * mir_emit_word_and_constant helper (Item T47) - a byte
                 * that is all-ones in the mask is left untouched, an
                 * all-zero byte collapses to a single immediate load,
                 * only a genuinely mixed byte needs a real `and`. The
                 * shared prologue above still unconditionally evaluates
                 * src2 (the constant) and pushes/pops it through DE -
                 * that redundant materialization is the same
                 * deliberately deferred residual already documented for
                 * the spilled-cfg selector's Items T44-T47, left as-is
                 * here for the same reason (blast radius). */
                const struct MirInsn *right_definition =
                    mir_definition(definition->src2);
                if (right_definition != NULL &&
                    right_definition->opcode == MIR_CONST) {
                    mir_emit_word_and_constant(
                        out, 'h', 'l',
                        (unsigned int)(right_definition->immediate & 0xffffL));
                    return 1;
                }
            }
            fputs("\tld a,h\n\tand d\n\tld h,a\n\tld a,l\n\tand e\n\tld l,a\n", out);
            return 1;
        case '|':
            fputs("\tld a,h\n\tor d\n\tld h,a\n\tld a,l\n\tor e\n\tld l,a\n", out);
            return 1;
        case '^':
            fputs("\tld a,h\n\txor d\n\tld h,a\n\tld a,l\n\txor e\n\tld l,a\n", out);
            return 1;
        case '*':
            mir_emit_runtime_call(out, "__mulu");
            return 1;
        case '/':
            {
                /* Item T49 (mir-text-size-plan.md): unsigned `int_expr /
                 * <compile-time power-of-2 constant>` mirrors legacy's
                 * fast path in ast_gen_binary_ast (dcc_ast_gen_expr.c
                 * ~1551) - `emit_logical_shift_right_hl_const` instead of
                 * a __divu runtime call. Signed division is intentionally
                 * left alone (matches legacy's exact scope): a plain
                 * right-shift is not equivalent to signed division's
                 * round-toward-zero for negative dividends. */
                const struct MirInsn *right_definition =
                    mir_definition(definition->src2);
                if ((definition->type & TYPE_UNSIGNED) != 0 &&
                    right_definition != NULL &&
                    right_definition->opcode == MIR_CONST) {
                    int shift = mir_ulong_log2_pow2(
                        (unsigned long)right_definition->immediate & 0xffffUL);
                    if (shift >= 0) {
                        mir_emit_scalar_shift_by_constant(out, TOK_SHR, 1,
                                                          shift);
                        return 1;
                    }
                }
            }
            mir_emit_runtime_call(
                out, (definition->type & TYPE_UNSIGNED) != 0 ? "__divu" : "__divs");
            return 1;
        case '%':
            {
                /* Item T49: unsigned `int_expr % <compile-time power-of-2
                 * constant>` mirrors legacy's fast path in the same
                 * function - `emit_and_hl_const(divisor - 1)` instead of
                 * a __modu runtime call, reusing Item T47/T48's
                 * mir_emit_word_and_constant helper. */
                const struct MirInsn *right_definition =
                    mir_definition(definition->src2);
                if ((definition->type & TYPE_UNSIGNED) != 0 &&
                    right_definition != NULL &&
                    right_definition->opcode == MIR_CONST) {
                    unsigned long divisor =
                        (unsigned long)right_definition->immediate & 0xffffUL;
                    if (mir_ulong_log2_pow2(divisor) >= 0) {
                        mir_emit_word_and_constant(
                            out, 'h', 'l',
                            (unsigned int)((divisor - 1) & 0xffffUL));
                        return 1;
                    }
                }
            }
            mir_emit_runtime_call(
                out, (definition->type & TYPE_UNSIGNED) != 0 ? "__modu" : "__mods");
            return 1;
        case TOK_EQ: case TOK_NE: case '<': case '>': case TOK_LE: case TOK_GE:
            {
                const struct MirInsn *left = mir_definition(definition->src1);
                const struct MirInsn *right = mir_definition(definition->src2);
                int is_unsigned =
                    (left != NULL &&
                     mir_type_uses_unsigned_comparison(left->type)) ||
                    (right != NULL &&
                     mir_type_uses_unsigned_comparison(right->type));
                mir_emit_scalar_compare(out, (int)definition->immediate,
                                        is_unsigned);
            }
            return 1;
        case TOK_SHL: case TOK_SHR:
            {
                const struct MirInsn *left = mir_definition(definition->src1);
                mir_emit_scalar_shift(out, (int)definition->immediate,
                                      left != NULL &&
                                      (left->type & TYPE_UNSIGNED) != 0,
                                      definition->src2);
            }
            return 1;
        default:
            return 0;
        }
    default:
        return 0;
    }
}

int mir_try_emit_scalar_dag(FILE *out)
{
    const struct MirInsn *return_insn = NULL;
    int i;

    if ((mir.return_type & 15) != TYPE_INT || type_size(mir.return_type) > 2)
        return 0;
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->opcode == MIR_RETURN) {
            if (return_insn != NULL)
                return 0;
            return_insn = insn;
        } else if (insn->opcode != MIR_NOP && insn->opcode != MIR_LABEL &&
                   insn->opcode != MIR_PARAM && insn->opcode != MIR_CONST &&
                   insn->opcode != MIR_UNARY && insn->opcode != MIR_BINARY &&
                   !(insn->opcode == MIR_STORE && insn->object >= 0)) {
            return 0;
        }
    }
    if (return_insn == NULL || return_insn->src1 < 0)
        return 0;
    mir_emit_prologue(out);
    if (!mir_emit_scalar_value(out, return_insn->src1, 0))
        return 0;
    fputs("\tld sp,ix\n\tpop ix\n\tret\n", out);
    return 1;
}

int mir_home_uses_iy(void)
{
    int value;
    for (value = 0; value < mir.next_value; ++value)
        if (mir.allocation_colors[value] == MIR_COLOR_IY ||
            mir.allocation_colors[value] == MIR_COLOR_BC_IY)
            return 1;
    return 0;
}

int mir_homed_string_call_argument(int value)
{
    const struct MirInsn *definition = mir_definition(value);
    int uses = 0;
    int instruction;

    /* Argument zero is pushed last, so loading its label into HL cannot
     * clobber another argument waiting in an HL home. Larger CFGs remain
     * excluded after their newly admitted candidates regressed full-mode
     * performance despite improving the assembly-text proxy. */
    if (definition == NULL || definition->opcode != MIR_STRING_ADDRESS ||
        mir_cfg_block_count() >= 4)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->src1 != value && insn->src2 != value)
            continue;
        if (insn->opcode != MIR_ARG || insn->src1 != value ||
            insn->immediate != 0)
            return 0;
        ++uses;
    }
    return uses == 1;
}

static int mir_home_spill_width(int value)
{
    const struct MirInsn *definition = mir_definition(value);

    return definition != NULL && type_size(definition->type) == 4 ? 4 : 2;
}

static int mir_home_spill_slot_width(int spill)
{
    int value;
    int width = 0;

    for (value = 0; value < mir.next_value; ++value)
        if (mir.allocation_spills[value] == spill &&
            mir_home_spill_width(value) > width)
            width = mir_home_spill_width(value);
    return width;
}

int mir_home_spill_bytes(void)
{
    int bytes = 0;
    int spill;

    for (spill = 0; spill < mir.allocation_spill_count; ++spill) {
        int width = mir_home_spill_slot_width(spill);
        if (width != 0)
            bytes += width;
    }
    return bytes;
}

int mir_home_spill_offset(int value, int *offset)
{
    int spill;
    int bytes;
    int current;

    if (value < 0 || value >= mir.next_value ||
        mir.allocation_spills == NULL)
        return 0;
    spill = mir.allocation_spills[value];
    if (spill < 0)
        return 0;
    bytes = mir_effective_local_bytes();
    for (current = 0; current <= spill; ++current) {
        int width = mir_home_spill_slot_width(current);
        if (width == 0)
            return 0;
        bytes += width;
    }
    if (offset != NULL)
        *offset = -bytes;
    return 1;
}

static void mir_emit_byte_extension(FILE *out, int color, int type);

static int mir_emit_lazy_parameter_to_color(FILE *out, int value, int color)
{
    int offset;
    int type;

    if (!mir_lazy_parameter_offset(value, &offset, &type))
        return 0;
    if (mir_home_uses_iy())
        offset += 2;
    if (color == MIR_COLOR_HL) {
        fprintf(out, "\tld l,(ix%+d)\n", offset);
        if (type_size(type) == 2)
            fprintf(out, "\tld h,(ix%+d)\n", offset + 1);
    } else if (color == MIR_COLOR_DE) {
        fprintf(out, "\tld e,(ix%+d)\n", offset);
        if (type_size(type) == 2)
            fprintf(out, "\tld d,(ix%+d)\n", offset + 1);
    } else {
        return 0;
    }
    if (type_size(type) == 1)
        mir_emit_byte_extension(out, color, type);
    return 1;
}

static int mir_emit_lazy_wide_parameter_to_hl_de(FILE *out, int value)
{
    int offset;
    int type;

    if (!mir_lazy_parameter_offset(value, &offset, &type) ||
        type_size(type) != 4)
        return 0;
    if (mir_home_uses_iy())
        offset += 2;
    fprintf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
            offset, offset + 1, offset + 2, offset + 3);
    return 1;
}

static int mir_emit_lazy_wide_parameter_to_stack(FILE *out, int value)
{
    int offset;
    int type;

    if (!mir_lazy_parameter_offset(value, &offset, &type) ||
        type_size(type) != 4)
        return 0;
    if (mir_home_uses_iy())
        offset += 2;
    fputs("\texx\n", out);
    fprintf(out,
            "\tld c,(ix%+d)\n\tld b,(ix%+d)\n\tpush bc\n"
            "\tld c,(ix%+d)\n\tld b,(ix%+d)\n\tpush bc\n",
            offset + 2, offset + 3, offset, offset + 1);
    fputs("\texx\n", out);
    return 1;
}

int mir_emit_home_to_hl(FILE *out, int value)
{
    int offset;

    if (mir_is_lazy_parameter(value))
        return mir_emit_lazy_parameter_to_color(out, value, MIR_COLOR_HL);
    switch (mir.allocation_colors[value]) {
    case MIR_COLOR_HL: return 1;
    case MIR_COLOR_DE: fputs("\tpush de\n\tpop hl\n", out); return 1;
    case MIR_COLOR_BC: fputs("\tld h,b\n\tld l,c\n", out); return 1;
    case MIR_COLOR_IY: fputs("\tpush iy\n\tpop hl\n", out); return 1;
    default:
        if (!mir_home_spill_offset(value, &offset))
            return 0;
        fprintf(out, "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
                offset, offset + 1);
        return 1;
    }
}

static int mir_emit_home_to_de(FILE *out, int value)
{
    int offset;

    if (mir_is_lazy_parameter(value))
        return mir_emit_lazy_parameter_to_color(out, value, MIR_COLOR_DE);
    switch (mir.allocation_colors[value]) {
    case MIR_COLOR_DE: return 1;
    case MIR_COLOR_BC: fputs("\tld d,b\n\tld e,c\n", out); return 1;
    case MIR_COLOR_IY: fputs("\tpush iy\n\tpop de\n", out); return 1;
    default:
        if (!mir_home_spill_offset(value, &offset))
            return 0;
        fputs("\tpush hl\n", out);
        fprintf(out, "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
                offset, offset + 1);
        fputs("\tex de,hl\n\tpop hl\n", out);
        return 1;
    }
}

int mir_emit_hl_to_home(FILE *out, int value)
{
    int offset;

    switch (mir.allocation_colors[value]) {
    case MIR_COLOR_HL: return 1;
    case MIR_COLOR_DE: fputs("\tex de,hl\n", out); return 1;
    case MIR_COLOR_BC: fputs("\tld b,h\n\tld c,l\n", out); return 1;
    case MIR_COLOR_IY: fputs("\tpush hl\n\tpop iy\n", out); return 1;
    default:
        if (!mir_home_spill_offset(value, &offset))
            return 0;
        fprintf(out, "\tld (ix%+d),l\n\tld (ix%+d),h\n",
                offset, offset + 1);
        return 1;
    }
}

/* Wide values use low-word:first ordering: HL:DE or BC:IY. Keep all pair
 * transfers here so constants, parameters, operations, and returns share
 * one representation and cannot drift independently. */
int mir_emit_wide_home_to_hl_de(FILE *out, int value)
{
    int offset;

    if (mir_is_lazy_parameter(value))
        return mir_emit_lazy_wide_parameter_to_hl_de(out, value);
    switch (mir.allocation_colors[value]) {
    case MIR_COLOR_HL_DE: return 1;
    case MIR_COLOR_BC_IY:
        fputs("\tld l,c\n\tld h,b\n\tpush iy\n\tpop de\n", out);
        return 1;
    default:
        if (!mir_home_spill_offset(value, &offset))
            return 0;
        fprintf(out,
                "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
                "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
                offset, offset + 1, offset + 2, offset + 3);
        return 1;
    }
}

int mir_emit_hl_de_to_wide_home(FILE *out, int value)
{
    int offset;

    switch (mir.allocation_colors[value]) {
    case MIR_COLOR_HL_DE: return 1;
    case MIR_COLOR_BC_IY:
        fputs("\tld c,l\n\tld b,h\n\tpush de\n\tpop iy\n", out);
        return 1;
    default:
        if (!mir_home_spill_offset(value, &offset))
            return 0;
        fprintf(out,
                "\tld (ix%+d),l\n\tld (ix%+d),h\n"
                "\tld (ix%+d),e\n\tld (ix%+d),d\n",
                offset, offset + 1, offset + 2, offset + 3);
        return 1;
    }
}

int mir_emit_wide_home_to_stack(FILE *out, int value)
{
    int offset;

    if (mir_is_lazy_parameter(value))
        return mir_emit_lazy_wide_parameter_to_stack(out, value);
    switch (mir.allocation_colors[value]) {
    case MIR_COLOR_HL_DE:
        fputs("\tpush de\n\tpush hl\n", out);
        return 1;
    case MIR_COLOR_BC_IY:
        fputs("\tpush iy\n\tpush bc\n", out);
        return 1;
    default:
        if (!mir_home_spill_offset(value, &offset))
            return 0;
        fputs("\texx\n", out);
        fprintf(out,
                "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n"
                "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n",
                offset + 2, offset + 3, offset, offset + 1);
        fputs("\texx\n", out);
        return 1;
    }
}

/* Push a homed value's register pair verbatim. Used only for narrow
 * (<=2 byte) call arguments in mir_try_emit_homed_scalar_cfg: HL/DE/BC/IY
 * are all directly pushable, so no intermediate move is needed. */
int mir_emit_home_push(FILE *out, int value)
{
    const struct MirInsn *definition = mir_definition(value);
    int offset;

    if (mir_homed_string_call_argument(value)) {
        fprintf(out, "\tld hl,S%ld\n\tpush hl\n", definition->immediate);
        return 1;
    }
    if (mir_is_lazy_parameter(value)) {
        fputs("\tpush hl\n", out);
        if (!mir_emit_lazy_parameter_to_color(out, value, MIR_COLOR_HL))
            return 0;
        fputs("\tex (sp),hl\n", out);
        return 1;
    }
    switch (mir.allocation_colors[value]) {
    case MIR_COLOR_HL: fputs("\tpush hl\n", out); return 1;
    case MIR_COLOR_DE: fputs("\tpush de\n", out); return 1;
    case MIR_COLOR_BC: fputs("\tpush bc\n", out); return 1;
    case MIR_COLOR_IY: fputs("\tpush iy\n", out); return 1;
    default:
        if (!mir_home_spill_offset(value, &offset))
            return 0;
        fputs("\tpush hl\n", out);
        fprintf(out, "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
                offset, offset + 1);
        fputs("\tex (sp),hl\n", out);
        return 1;
    }
}

/* Item 16 (mir-migration-plan-to-100pct.md): load a label's address
 * directly into a value's own home color. Z80's "ld <pair>,nn" immediate
 * form works identically for hl/de/bc/iy, so no HL/DE scratch is needed
 * at all for this case - unlike Item 14's original (reverted) attempt,
 * which always routed through HL first and could clobber another
 * still-live homed value. */
int mir_emit_label_address_to_home(FILE *out, int value,
                                           const char *assembly_name)
{
    int offset;

    switch (mir.allocation_colors[value]) {
    case MIR_COLOR_HL: fprintf(out, "\tld hl,%s\n", assembly_name); return 1;
    case MIR_COLOR_DE: fprintf(out, "\tld de,%s\n", assembly_name); return 1;
    case MIR_COLOR_BC: fprintf(out, "\tld bc,%s\n", assembly_name); return 1;
    case MIR_COLOR_IY: fprintf(out, "\tld iy,%s\n", assembly_name); return 1;
    default:
        if (!mir_home_spill_offset(value, &offset))
            return 0;
        fputs("\tpush hl\n", out);
        fprintf(out, "\tld hl,%s\n\tld (ix%+d),l\n\tld (ix%+d),h\n",
                assembly_name, offset, offset + 1);
        fputs("\tpop hl\n", out);
        return 1;
    }
}

/* Item 16: compute the address of an ix-relative (local/param) scalar
 * directly into a value's own home color. The zero-offset case (the
 * object sits exactly at ix, e.g. a first local/param) is address==ix,
 * transferable to any register pair via push ix/pop <reg> with no
 * scratch at all. A non-zero offset needs an "add hl,de"-style
 * computation, which does require HL and DE as scratch - so that path
 * conservatively preserves whichever of HL/DE isn't the destination
 * color via push/pop, protecting any other still-live homed value
 * (the same defensive pattern used by Item 13's MIR_STORE widening). */
int mir_emit_ix_offset_address_to_home(FILE *out, int value,
                                               int offset)
{
    int color = mir.allocation_colors[value];
    const struct MirInsn *definition = mir_definition(value);
    int instruction = definition != NULL
        ? (int)(definition - mir.insns) : -1;
    int spill_offset;
    if (offset == 0) {
        switch (color) {
        case MIR_COLOR_HL: fputs("\tpush ix\n\tpop hl\n", out); return 1;
        case MIR_COLOR_DE: fputs("\tpush ix\n\tpop de\n", out); return 1;
        case MIR_COLOR_BC: fputs("\tpush ix\n\tpop bc\n", out); return 1;
        case MIR_COLOR_IY: fputs("\tpush ix\n\tpop iy\n", out); return 1;
        default:
            if (!mir_home_spill_offset(value, &spill_offset))
                return 0;
            fputs("\tpush hl\n\tpush ix\n\tpop hl\n", out);
            fprintf(out, "\tld (ix%+d),l\n\tld (ix%+d),h\n",
                    spill_offset, spill_offset + 1);
            fputs("\tpop hl\n", out);
            return 1;
        }
    }
    if (color != MIR_COLOR_HL && color != MIR_COLOR_DE &&
        color != MIR_COLOR_BC && color != MIR_COLOR_IY &&
        !mir_home_spill_offset(value, NULL))
        return 0;
    {
        /* Keep the measured straight-line selection stable; CFG liveness
         * matters here when mutually exclusive paths share a color. */
        int use_cfg_liveness = mir_cfg_block_count() > 1;
        int preserve_hl_de = use_cfg_liveness &&
            mir_home_color_live_across(instruction, MIR_COLOR_HL_DE);
        int preserve_hl = !preserve_hl_de && color != MIR_COLOR_HL &&
            (!use_cfg_liveness ||
             mir_home_color_live_across(instruction, MIR_COLOR_HL));
        int preserve_de = !preserve_hl_de && color != MIR_COLOR_DE &&
            (!use_cfg_liveness ||
             mir_home_color_live_across(instruction, MIR_COLOR_DE));
        if (preserve_hl_de) fputs("\tpush de\n\tpush hl\n", out);
        if (preserve_hl) fputs("\tpush hl\n", out);
        if (preserve_de) fputs("\tpush de\n", out);
        fputs("\tpush ix\n\tpop hl\n", out);
        fprintf(out, "\tld de,%d\n\tadd hl,de\n", offset);
        if (color != MIR_COLOR_HL && !mir_emit_hl_to_home(out, value))
            return 0;
        if (preserve_de) fputs("\tpop de\n", out);
        if (preserve_hl) fputs("\tpop hl\n", out);
        if (preserve_hl_de) fputs("\tpop hl\n\tpop de\n", out);
    }
    return 1;
}

/* Item 22 (mir-migration-plan-to-100pct.md): compute (base value +
 * constant byte offset) directly into a value's own home color, for
 * MIR_MEMBER_ADDRESS (a struct/union member's address, base=src1) and
 * MIR_INDEX_ADDRESS's constant-index case (base=src1, offset already
 * reduced to a byte count by the caller). Mirrors mir_emit_ix_offset_
 * address_to_home's proven push/pop discipline exactly, substituting
 * "load base value into hl" for that helper's "push ix/pop hl": preserve
 * hl/de around the computation whenever the destination isn't that color
 * (the register allocator guarantees dst's own color slot has nothing
 * else live there needing preservation), so this also correctly restores
 * base's own value afterward when base's home is hl or de and base still
 * has a use later in the function. */
int mir_emit_pointer_offset_address_to_home(FILE *out, int dst,
                                                    int base, long offset)
{
    int dst_color = mir.allocation_colors[dst];
    const struct MirInsn *definition = mir_definition(dst);
    int instruction = definition != NULL
        ? (int)(definition - mir.insns) : -1;
    int preserve_hl_de =
        mir_home_color_live_across(instruction, MIR_COLOR_HL_DE);
    int preserve_hl = !preserve_hl_de && dst_color != MIR_COLOR_HL &&
        mir_home_color_live_across(instruction, MIR_COLOR_HL);
    int preserve_de = !preserve_hl_de && offset != 0 &&
        dst_color != MIR_COLOR_DE &&
        mir_home_color_live_across(instruction, MIR_COLOR_DE);

    if (offset == 0 &&
        dst_color == mir.allocation_colors[base])
        return 1;
    if (preserve_hl_de) fputs("\tpush de\n\tpush hl\n", out);
    if (preserve_hl) fputs("\tpush hl\n", out);
    if (preserve_de) fputs("\tpush de\n", out);
    if (!mir_emit_home_to_hl(out, base))
        return 0;
    if (offset != 0)
        fprintf(out, "\tld de,%ld\n\tadd hl,de\n", offset);
    if (dst_color != MIR_COLOR_HL && !mir_emit_hl_to_home(out, dst))
        return 0;
    if (preserve_de) fputs("\tpop de\n", out);
    if (preserve_hl) fputs("\tpop hl\n", out);
    if (preserve_hl_de) fputs("\tpop hl\n\tpop de\n", out);
    return 1;
}

int mir_emit_constant_to_home(FILE *out, int value, long immediate)
{
    int offset;

    switch (mir.allocation_colors[value]) {
    case MIR_COLOR_HL: fprintf(out, "\tld hl,%ld\n", immediate & 0xffffL); return 1;
    case MIR_COLOR_DE: fprintf(out, "\tld de,%ld\n", immediate & 0xffffL); return 1;
    case MIR_COLOR_BC: fprintf(out, "\tld bc,%ld\n", immediate & 0xffffL); return 1;
    case MIR_COLOR_IY: fprintf(out, "\tld iy,%ld\n", immediate & 0xffffL); return 1;
    default:
        if (!mir_home_spill_offset(value, &offset))
            return 0;
        fputs("\tpush hl\n", out);
        fprintf(out,
                "\tld hl,%ld\n\tld (ix%+d),l\n\tld (ix%+d),h\n",
                immediate & 0xffffL, offset, offset + 1);
        fputs("\tpop hl\n", out);
        return 1;
    }
}

/* Materialize both words directly in the allocated pair home. */
int mir_emit_wide_constant_to_home(FILE *out, int value, long immediate)
{
    long lo = immediate & 0xffffL;
    long hi = (immediate >> 16) & 0xffffL;
    switch (mir.allocation_colors[value]) {
    case MIR_COLOR_HL_DE:
        fprintf(out, "\tld hl,%ld\n\tld de,%ld\n", lo, hi);
        return 1;
    case MIR_COLOR_BC_IY:
        fprintf(out, "\tld bc,%ld\n\tld iy,%ld\n", lo, hi);
        return 1;
    default:
        return 0;
    }
}

int mir_emit_cast(FILE *out, int source_type, int target_type)
{
    const char *helper;
    if (type_is_bool(target_type) && !type_is_bool(source_type)) {
        int nonzero_label = new_label();
        int end_label = new_label();
        if (type_size(source_type) > 2) {
            if (type_is_float(source_type))
                fputs("\tld a,d\n\tand 127\n\tor e\n\tor h\n\tor l\n",
                      out);
            else
                fputs("\tld a,d\n\tor e\n\tor h\n\tor l\n", out);
        } else {
            fputs("\tld a,h\n\tor l\n", out);
        }
        fputs("\tld hl,0\n", out);
        fprintf(out, "\tjp nz, L%d\n\tjp L%d\nL%d:\n\tinc hl\nL%d:\n",
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
        mir_emit_runtime_call(out, helper);
        return 1;
    }
    if (type_is_float(source_type) && !type_is_float(target_type)) {
        if (type_is_long(target_type))
            helper = (target_type & TYPE_UNSIGNED) != 0 ? "__fful" : "__ffl";
        else
            helper = ((target_type & TYPE_UNSIGNED) != 0 ||
                      type_ptr_depth(target_type) > 0) ? "__ffu" : "__ffi";
        mir_emit_runtime_call(out, helper);
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
    }
    return 1;
}

int mir_emit_word_param_to_home(FILE *out, int value, int offset)
{
    int spill_offset;

    switch (mir.allocation_colors[value]) {
    case MIR_COLOR_HL:
        fprintf(out, "\tld l,(ix%+d)\n\tld h,(ix%+d)\n", offset, offset + 1);
        return 1;
    case MIR_COLOR_DE:
        fprintf(out, "\tld e,(ix%+d)\n\tld d,(ix%+d)\n", offset, offset + 1);
        return 1;
    case MIR_COLOR_BC:
        fprintf(out, "\tld c,(ix%+d)\n\tld b,(ix%+d)\n", offset, offset + 1);
        return 1;
    case MIR_COLOR_IY:
        fputs("\tpush hl\n", out);
        fprintf(out, "\tld l,(ix%+d)\n\tld h,(ix%+d)\n", offset, offset + 1);
        fputs("\tpush hl\n\tpop iy\n\tpop hl\n", out);
        return 1;
    default:
        if (!mir_home_spill_offset(value, &spill_offset))
            return 0;
        fputs("\tpush hl\n", out);
        fprintf(out,
                "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
                "\tld (ix%+d),l\n\tld (ix%+d),h\n",
                offset, offset + 1, spill_offset, spill_offset + 1);
        fputs("\tpop hl\n", out);
        return 1;
    }
}

static void mir_emit_byte_extension(FILE *out, int color, int type)
{
    int end_label;

    if (type_is_bool(type)) {
        end_label = new_label();
        switch (color) {
        case MIR_COLOR_HL:
            fputs("\tld a,l\n\tor a\n\tld hl,0\n", out);
            fprintf(out, "\tjp z, L%d\n\tinc hl\nL%d:\n",
                    end_label, end_label);
            return;
        case MIR_COLOR_DE:
            fputs("\tld a,e\n\tor a\n\tld de,0\n", out);
            fprintf(out, "\tjp z, L%d\n\tinc de\nL%d:\n",
                    end_label, end_label);
            return;
        case MIR_COLOR_BC:
            fputs("\tld a,c\n\tor a\n\tld bc,0\n", out);
            fprintf(out, "\tjp z, L%d\n\tinc bc\nL%d:\n",
                    end_label, end_label);
            return;
        }
    }
    if ((type & TYPE_UNSIGNED) != 0) {
        if (color == MIR_COLOR_HL) fputs("\tld h,0\n", out);
        else if (color == MIR_COLOR_DE) fputs("\tld d,0\n", out);
        else fputs("\tld b,0\n", out);
    } else if (color == MIR_COLOR_HL) {
        mir_emit_signed_byte_extend(out);
    } else if (color == MIR_COLOR_DE) {
        fputs("\tld a,e\n\tadd a,a\n\tsbc a,a\n\tld d,a\n", out);
    } else {
        fputs("\tld a,c\n\tadd a,a\n\tsbc a,a\n\tld b,a\n", out);
    }
}

int mir_emit_byte_param_to_home(FILE *out, int value, int offset, int type)
{
    int spill_offset;

    switch (mir.allocation_colors[value]) {
    case MIR_COLOR_HL:
        fprintf(out, "\tld l,(ix%+d)\n", offset);
        mir_emit_byte_extension(out, MIR_COLOR_HL, type);
        return 1;
    case MIR_COLOR_DE:
        fprintf(out, "\tld e,(ix%+d)\n", offset);
        mir_emit_byte_extension(out, MIR_COLOR_DE, type);
        return 1;
    case MIR_COLOR_BC:
        fprintf(out, "\tld c,(ix%+d)\n", offset);
        mir_emit_byte_extension(out, MIR_COLOR_BC, type);
        return 1;
    case MIR_COLOR_IY:
        fputs("\tpush hl\n", out);
        fprintf(out, "\tld l,(ix%+d)\n", offset);
        mir_emit_byte_extension(out, MIR_COLOR_HL, type);
        fputs("\tpush hl\n\tpop iy\n\tpop hl\n", out);
        return 1;
    default:
        if (!mir_home_spill_offset(value, &spill_offset))
            return 0;
        fputs("\tpush hl\n", out);
        fprintf(out, "\tld l,(ix%+d)\n", offset);
        mir_emit_byte_extension(out, MIR_COLOR_HL, type);
        fprintf(out, "\tld (ix%+d),l\n\tld (ix%+d),h\n",
                spill_offset, spill_offset + 1);
        fputs("\tpop hl\n", out);
        return 1;
    }
}

static int mir_emit_push_home(FILE *out, int value)
{
    const struct MirInsn *definition = mir_definition(value);
    int offset;

    if (definition != NULL && type_size(definition->type) == 4)
        return mir_emit_wide_home_to_stack(out, value);
    if (mir_is_lazy_parameter(value)) {
        fputs("\tpush hl\n", out);
        if (!mir_emit_lazy_parameter_to_color(out, value, MIR_COLOR_HL))
            return 0;
        fputs("\tex (sp),hl\n", out);
        return 1;
    }
    switch (mir.allocation_colors[value]) {
    case MIR_COLOR_HL: fputs("\tpush hl\n", out); return 1;
    case MIR_COLOR_DE: fputs("\tpush de\n", out); return 1;
    case MIR_COLOR_BC: fputs("\tpush bc\n", out); return 1;
    case MIR_COLOR_IY: fputs("\tpush iy\n", out); return 1;
    default:
        if (!mir_home_spill_offset(value, &offset))
            return 0;
        fputs("\texx\n", out);
        fprintf(out, "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n",
                offset, offset + 1);
        fputs("\texx\n", out);
        return 1;
    }
}

static int mir_emit_pop_home(FILE *out, int value)
{
    const struct MirInsn *definition = mir_definition(value);
    int offset;

    if (definition != NULL && type_size(definition->type) == 4) {
        switch (mir.allocation_colors[value]) {
        case MIR_COLOR_HL_DE:
            fputs("\tpop hl\n\tpop de\n", out);
            return 1;
        case MIR_COLOR_BC_IY:
            fputs("\tpop bc\n\tpop iy\n", out);
            return 1;
        default:
            if (!mir_home_spill_offset(value, &offset))
                return 0;
            fputs("\texx\n", out);
            fprintf(out,
                    "\tpop hl\n\tld (ix%+d),l\n\tld (ix%+d),h\n"
                    "\tpop hl\n\tld (ix%+d),l\n\tld (ix%+d),h\n",
                    offset, offset + 1, offset + 2, offset + 3);
            fputs("\texx\n", out);
            return 1;
        }
    }
    switch (mir.allocation_colors[value]) {
    case MIR_COLOR_HL: fputs("\tpop hl\n", out); return 1;
    case MIR_COLOR_DE: fputs("\tpop de\n", out); return 1;
    case MIR_COLOR_BC: fputs("\tpop bc\n", out); return 1;
    case MIR_COLOR_IY: fputs("\tpop iy\n", out); return 1;
    default:
        if (!mir_home_spill_offset(value, &offset))
            return 0;
        fputs("\texx\n", out);
        fprintf(out, "\tpop hl\n\tld (ix%+d),l\n\tld (ix%+d),h\n",
                offset, offset + 1);
        fputs("\texx\n", out);
        return 1;
    }
}

static int mir_homed_values_share_home(int left, int right)
{
    int left_spill;
    int right_spill;

    if (mir.allocation_colors[left] >= 0 ||
        mir.allocation_colors[right] >= 0)
        return mir.allocation_colors[left] == mir.allocation_colors[right];
    if (!mir_home_spill_offset(left, NULL) ||
        !mir_home_spill_offset(right, NULL))
        return 0;
    left_spill = mir.allocation_spills[left];
    right_spill = mir.allocation_spills[right];
    return left_spill == right_spill;
}

int mir_phi_source_for_edge(const struct MirInsn *phi,
                                   int predecessor_label, int edge_label,
                                   int successor, int phi_instruction)
{
    int instruction;
    if (predecessor_label == phi->phi_pred1 || edge_label == phi->phi_pred1)
        return phi->src1;
    if (predecessor_label == phi->phi_pred2 || edge_label == phi->phi_pred2)
        return phi->src2;
    for (instruction = successor;
         instruction >= 0 && instruction < phi_instruction;
         ++instruction)
        if (mir.insns[instruction].opcode == MIR_LABEL) {
            if (mir.insns[instruction].label == phi->phi_pred1)
                return phi->src1;
            if (mir.insns[instruction].label == phi->phi_pred2)
                return phi->src2;
        }
    return -1;
}

int mir_emit_homed_phi_copies(FILE *out, int predecessor,
                                     int successor)
{
    int sources[256];
    int destinations[256];
    int count = 0;
    int predecessor_label = mir_block_label_before(predecessor);
    int edge_label = -1;
    int instruction = mir_first_phi_or_block_end(successor);
    int i;

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
        if (!mir_homed_values_share_home(source, phi->dst)) {
            if (count >= 256)
                return 0;
            sources[count] = source;
            destinations[count] = phi->dst;
            ++count;
        }
        ++instruction;
    }
    for (i = 0; i < count; ++i)
        if (!mir_emit_push_home(out, sources[i]))
            return 0;
    for (i = count - 1; i >= 0; --i)
        if (!mir_emit_pop_home(out, destinations[i]))
            return 0;
    return 1;
}

int mir_edge_phi_names_predecessor(int predecessor, int successor)
{
    int instruction = mir_first_phi_or_block_end(successor);
    int predecessor_label = mir_block_label_before(predecessor);
    int source;

    if (instruction < 0 || instruction >= mir.count ||
        mir.insns[instruction].opcode != MIR_PHI)
        return 0;
    /*
     * Consecutive labels before a phi are aliases for one entry position.
     * The phi can name a later alias (for example AST_COND's else-exit
     * immediately followed by its merge label), while the real fallthrough
     * edge arrives before the first label. Resolve the whole alias chain so
     * strict emission places the copy on that real edge instead of dropping
     * it together with the synthetic label-to-label edge.
     */
    source = mir_phi_source_for_edge(
        &mir.insns[instruction], predecessor_label, -1,
        successor, instruction);
    return source >= 0;
}

static int mir_strict_phi_fallthrough_enabled;
static int mir_strict_phi_fallthrough_used;

void mir_begin_strict_phi_fallthrough(void)
{
    mir_strict_phi_fallthrough_enabled = 1;
    mir_strict_phi_fallthrough_used = 0;
}

void mir_end_strict_phi_fallthrough(void)
{
    mir_strict_phi_fallthrough_enabled = 0;
}

int mir_strict_phi_fallthrough_was_used(void)
{
    return mir_strict_phi_fallthrough_enabled &&
           mir_strict_phi_fallthrough_used;
}

static int mir_phi_fallthrough_owned_by_entry_edge(int instruction,
                                                   int successor)
{
    int block_label = mir_block_label_before(instruction);
    int block_start;
    int phi_instruction;
    int predecessor;

    if (block_label < 0)
        return 0;
    block_start = mir_find_label(block_label);
    if (block_start < 0)
        return 0;
    phi_instruction = mir_first_phi_or_block_end(successor);
    if (phi_instruction < 0 || phi_instruction >= mir.count ||
        mir.insns[phi_instruction].opcode != MIR_PHI ||
        mir_first_phi_or_block_end(block_start) != phi_instruction)
        return 0;

    /*
     * A branch into a NOP/label-only arm already emits this downstream phi
     * copy on its explicit target edge. Emitting it again when that empty
     * arm falls into the merge overwrites the value chosen by the other arm.
     * A substantive arm does not satisfy the shared-phi test above, so its
     * real exit instruction remains responsible for the copy.
     */
    for (predecessor = 0; predecessor < mir.count; ++predecessor)
        if ((mir.insns[predecessor].opcode == MIR_JUMP ||
             mir.insns[predecessor].opcode == MIR_BRANCH_FALSE)) {
            int target = mir_find_label(mir.insns[predecessor].label);
            int cursor;

            if (target < 0 || target > block_start)
                continue;
            for (cursor = target; cursor <= block_start; ++cursor)
                if (mir.insns[cursor].opcode != MIR_LABEL &&
                    mir.insns[cursor].opcode != MIR_NOP)
                    break;
            if (cursor > block_start)
                return 1;
        }
    return 0;
}

int mir_instruction_has_phi_fallthrough(int instruction,
                                        int require_next_label)
{
    int opcode;

    if (instruction < 0 || instruction + 1 >= mir.count)
        return 0;
    opcode = mir.insns[instruction].opcode;
    if (opcode == MIR_LABEL && mir_strict_phi_fallthrough_enabled) {
        if (mir.insns[instruction + 1].opcode == MIR_LABEL &&
            mir_edge_phi_names_predecessor(instruction, instruction + 1))
            mir_strict_phi_fallthrough_used = 1;
        return 0;
    }
    if (opcode == MIR_JUMP ||
        opcode == MIR_BRANCH_FALSE || opcode == MIR_RETURN)
        return 0;
    if (require_next_label &&
        mir.insns[instruction + 1].opcode != MIR_LABEL)
        return 0;
    return !mir_phi_fallthrough_owned_by_entry_edge(
               instruction, instruction + 1) &&
           mir_edge_phi_names_predecessor(instruction, instruction + 1);
}

int mir_direct_branch_for_comparison(int instruction)
{
    const struct MirInsn *compare;
    int use_count = 0;
    int branch = -1;
    int i;

    if (instruction < 0 || instruction >= mir.count)
        return -1;
    compare = &mir.insns[instruction];
    if (compare->opcode != MIR_BINARY ||
        (compare->immediate != TOK_EQ && compare->immediate != TOK_NE &&
         compare->immediate != '<' && compare->immediate != '>' &&
         compare->immediate != TOK_LE && compare->immediate != TOK_GE))
        return -1;
    for (i = 0; i < mir.count; ++i) {
        if (mir.insns[i].src1 == compare->dst ||
            mir.insns[i].src2 == compare->dst) {
            ++use_count;
            if (mir.insns[i].opcode == MIR_BRANCH_FALSE &&
                mir.insns[i].src1 == compare->dst)
                branch = i;
        }
    }
    return use_count == 1 ? branch : -1;
}

int mir_compare_definition_for_branch(int instruction)
{
    const struct MirInsn *definition;
    int index;
    if (instruction < 0 || instruction >= mir.count ||
        mir.insns[instruction].opcode != MIR_BRANCH_FALSE)
        return -1;
    definition = mir_definition(mir.insns[instruction].src1);
    if (definition == NULL)
        return -1;
    index = (int)(definition - mir.insns);
    return mir_direct_branch_for_comparison(index) == instruction ? index : -1;
}

int mir_direct_branch_for_unary_not(int instruction)
{
    const struct MirInsn *unary;
    int use_count = 0;
    int branch = -1;
    int i;

    if (instruction < 0 || instruction >= mir.count)
        return -1;
    unary = &mir.insns[instruction];
    if (unary->opcode != MIR_UNARY || unary->immediate != '!')
        return -1;
    for (i = 0; i < mir.count; ++i) {
        if (mir.insns[i].src1 == unary->dst ||
            mir.insns[i].src2 == unary->dst) {
            ++use_count;
            if (mir.insns[i].opcode == MIR_BRANCH_FALSE &&
                mir.insns[i].src1 == unary->dst)
                branch = i;
        }
    }
    return use_count == 1 ? branch : -1;
}

int mir_unary_not_definition_for_branch(int instruction)
{
    const struct MirInsn *definition;
    int index;

    if (instruction < 0 || instruction >= mir.count ||
        mir.insns[instruction].opcode != MIR_BRANCH_FALSE)
        return -1;
    definition = mir_definition(mir.insns[instruction].src1);
    if (definition == NULL)
        return -1;
    index = (int)(definition - mir.insns);
    return mir_direct_branch_for_unary_not(index) == instruction ? index : -1;
}

int mir_emit_stack_word_param_to_home(FILE *out, int value, int offset)
{
    const struct MirInsn *definition = mir_definition(value);
    int instruction = definition != NULL ? (int)(definition - mir.insns) : -1;
    /* Keep scalar parameter output stable; pointer arithmetic is the
     * measured case where avoiding this save clears the rollout cost gate. */
    int preserve_hl =
        instruction < 0 || type_ptr_depth(definition->type) == 0 ||
        mir_home_color_live_across(instruction, MIR_COLOR_HL);
    int stack_offset = offset - 2;
    switch (mir.allocation_colors[value]) {
    case MIR_COLOR_HL:
        fprintf(out, "\tld hl,%d\n\tadd hl,sp\n", stack_offset);
        fputs("\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n", out);
        return 1;
    case MIR_COLOR_DE:
        if (preserve_hl)
            fputs("\tpush hl\n", out);
        fprintf(out, "\tld hl,%d\n\tadd hl,sp\n",
                stack_offset + (preserve_hl ? 2 : 0));
        fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n", out);
        if (preserve_hl)
            fputs("\tpop hl\n", out);
        return 1;
    case MIR_COLOR_BC:
        if (preserve_hl)
            fputs("\tpush hl\n", out);
        fprintf(out, "\tld hl,%d\n\tadd hl,sp\n",
                stack_offset + (preserve_hl ? 2 : 0));
        fputs("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n", out);
        if (preserve_hl)
            fputs("\tpop hl\n", out);
        return 1;
    default:
        return 0;
    }
}

int mir_emit_stack_byte_param_to_home(FILE *out, int value, int offset,
                                      int type)
{
    int stack_offset = offset - 2;

    switch (mir.allocation_colors[value]) {
    case MIR_COLOR_HL:
        fprintf(out, "\tld hl,%d\n\tadd hl,sp\n\tld l,(hl)\n",
                stack_offset);
        mir_emit_byte_extension(out, MIR_COLOR_HL, type);
        return 1;
    case MIR_COLOR_DE:
        fputs("\tpush hl\n", out);
        fprintf(out, "\tld hl,%d\n\tadd hl,sp\n\tld e,(hl)\n",
                stack_offset + 2);
        mir_emit_byte_extension(out, MIR_COLOR_DE, type);
        fputs("\tpop hl\n", out);
        return 1;
    case MIR_COLOR_BC:
        fputs("\tpush hl\n", out);
        fprintf(out, "\tld hl,%d\n\tadd hl,sp\n\tld c,(hl)\n",
                stack_offset + 2);
        mir_emit_byte_extension(out, MIR_COLOR_BC, type);
        fputs("\tpop hl\n", out);
        return 1;
    default:
        return 0;
    }
}

void mir_emit_home_prologue(FILE *out, int uses_iy)
{
    int frame_bytes = mir_effective_local_bytes() +
        mir_home_spill_bytes();

    if (uses_iy) {
        if (mir_iy_home_live_across_caller_clobber())
            /* MIR's IY home is callee-saved, but dccpeep's file-wide IY
             * borrowing is not. Publish the same ownership protocol as the
             * legacy allocator so no optimized callee can clobber a value
             * retained across its call. */
            fprintf(out,
                    ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n",
                    mir.name);
        fputs("\tpush iy\n", out);
    }
    if (frame_bytes == 0) {
        mir_emit_prologue(out);
        return;
    }
    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    fprintf(out, "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n", frame_bytes);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
}

void mir_emit_home_epilogue(FILE *out, int uses_iy)
{
    fputs("\tld sp,ix\n\tpop ix\n", out);
    if (uses_iy)
        fputs("\tpop iy\n", out);
    fputs("\tret\n", out);
}

static int mir_value_is_normalized_byte(int value, int type, int depth)
{
    const struct MirInsn *definition;
    unsigned long bits;

    if (depth > 16)
        return 0;
    definition = mir_definition(value);
    if (definition == NULL)
        return 0;
    if (definition->opcode == MIR_CONST) {
        bits = (unsigned long)definition->immediate & 0xffffUL;
        if ((type & TYPE_UNSIGNED) != 0)
            return (bits & 0xff00UL) == 0;
        return (bits & 0xff80UL) == 0 ||
               (bits & 0xff80UL) == 0xff80UL;
    }
    if (definition->opcode == MIR_PHI)
        return mir_value_is_normalized_byte(
                   definition->src1, type, depth + 1) &&
               mir_value_is_normalized_byte(
                   definition->src2, type, depth + 1);
    return 0;
}

int mir_float_identity_unary(const struct MirInsn *insn)
{
    const struct MirInsn *source;

    if (insn == NULL || insn->opcode != MIR_UNARY ||
        (insn->immediate != 0 && insn->immediate != '+'))
        return 0;
    source = mir_definition(insn->src1);
    return source != NULL && type_is_float(source->type) &&
           type_is_float(insn->type) && type_size(source->type) == 4 &&
           type_size(insn->type) == 4;
}

int mir_emit_homed_unary_instruction(FILE *out,
                                            const struct MirInsn *insn)
{
    const struct MirInsn *source = mir_definition(insn->src1);
    int source_type = source != NULL ? source->type : 0;
    int source_wide = type_size(source_type) == 4;
    int target_wide = type_size(insn->type) == 4;
    int instruction = (int)(insn - mir.insns);
    /* Preserve any HL-homed value that is live on both sides of this
     * instruction, including a still-live src1 whose own load is a no-op.
     * Use retained CFG liveness rather than a textual future-use scan so
     * mutually exclusive branch values do not manufacture saves. */
    int preserve_hl_de =
        !source_wide && !target_wide &&
        mir_home_color_live_across(instruction, MIR_COLOR_HL_DE);
    int preserve_hl = !preserve_hl_de &&
                      mir.allocation_colors[insn->dst] != MIR_COLOR_HL &&
                      mir_home_color_live_across(
                          instruction, MIR_COLOR_HL);
    int label;

    if (source_wide || target_wide) {
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

        if ((type_is_float(source_type) || type_is_float(insn->type)) &&
            !mir_float_identity_unary(insn) &&
            insn->immediate != 0)
            return 0;
        if (preserve_hl_de) fputs("\tpush de\n\tpush hl\n", out);
        if (preserve_bc_iy) fputs("\tpush iy\n\tpush bc\n", out);
        if (preserve_hl) fputs("\tpush hl\n", out);
        if (preserve_de) fputs("\tpush de\n", out);
        if (preserve_bc) fputs("\tpush bc\n", out);
        if (source_wide) {
            if (!mir_emit_wide_home_to_hl_de(out, insn->src1))
                return 0;
        } else if (!mir_emit_home_to_hl(out, insn->src1)) {
            return 0;
        }
        if (mir_float_identity_unary(insn)) {
            /* The value already has the target representation. */
        } else if (insn->immediate == 0) {
            if (!mir_emit_cast(out, source_type, insn->type))
                return 0;
        } else if (insn->immediate == '+') {
            if (!source_wide || !target_wide)
                return 0;
        } else if (insn->immediate == '-' && source_wide && target_wide) {
            int carry_label = new_label();
            fputs("\tld a,l\n\tcpl\n\tld l,a\n"
                  "\tld a,h\n\tcpl\n\tld h,a\n"
                  "\tld a,e\n\tcpl\n\tld e,a\n"
                  "\tld a,d\n\tcpl\n\tld d,a\n"
                  "\tinc hl\n\tld a,h\n\tor l\n", out);
            fprintf(out, "\tjp nz, L%d\n\tinc de\nL%d:\n",
                    carry_label, carry_label);
        } else if (insn->immediate == '~' && source_wide && target_wide) {
            fputs("\tld a,l\n\tcpl\n\tld l,a\n"
                  "\tld a,h\n\tcpl\n\tld h,a\n"
                  "\tld a,e\n\tcpl\n\tld e,a\n"
                  "\tld a,d\n\tcpl\n\tld d,a\n", out);
        } else if (insn->immediate == '!' && source_wide && !target_wide) {
            label = new_label();
            fputs("\tld a,d\n\tor e\n\tor h\n\tor l\n\tld hl,0\n", out);
            fprintf(out, "\tjp nz, L%d\n\tinc hl\nL%d:\n", label, label);
        } else {
            return 0;
        }
        if (!(target_wide
              ? mir_emit_hl_de_to_wide_home(out, insn->dst)
              : mir_emit_hl_to_home(out, insn->dst)))
            return 0;
        if (preserve_bc) fputs("\tpop bc\n", out);
        if (preserve_de) fputs("\tpop de\n", out);
        if (preserve_hl) fputs("\tpop hl\n", out);
        if (preserve_bc_iy) fputs("\tpop bc\n\tpop iy\n", out);
        if (preserve_hl_de) fputs("\tpop hl\n\tpop de\n", out);
        return 1;
    }

    if (preserve_hl_de)
        fputs("\tpush de\n\tpush hl\n", out);
    if (preserve_hl)
        fputs("\tpush hl\n", out);
    if (!mir_emit_home_to_hl(out, insn->src1))
        return 0;
    if (insn->immediate == 0) {
        /* Cast in the 16-bit home subset: usually a no-op, except a cast to
         * _Bool must normalize any nonzero value to exactly 1 (C requires
         * every _Bool object to hold only 0 or 1). Matches mir_emit_cast's
         * spilled-scalar-cfg handling of the same case (MIR_UNARY op=0
         * with a bool destination and non-bool source). */
        if (type_is_bool(insn->type) &&
            !type_is_bool(source != NULL ? source->type : 0)) {
            label = new_label();
            fputs("\tld a,h\n\tor l\n\tld hl,0\n", out);
            fprintf(out, "\tjp z, L%d\n\tinc hl\nL%d:\n", label, label);
        } else if (type_size(insn->type) == 1 &&
                   !mir_value_is_normalized_byte(insn->src1, insn->type, 0)) {
            mir_emit_byte_extension(out, MIR_COLOR_HL, insn->type);
        }
    } else if (insn->immediate == '+') {
        /* Unary plus: no-op. */
    } else if (insn->immediate == '-') {
        fputs("\txor a\n\tsub l\n\tld l,a\n\tsbc a,a\n\tsub h\n\tld h,a\n", out);
    } else if (insn->immediate == '~') {
        fputs("\tld a,l\n\tcpl\n\tld l,a\n\tld a,h\n\tcpl\n\tld h,a\n", out);
    } else if (insn->immediate == '!') {
        label = new_label();
        fputs("\tld a,h\n\tor l\n\tld hl,0\n", out);
        fprintf(out, "\tjp nz, L%d\n\tinc hl\nL%d:\n", label, label);
    } else {
        return 0;
    }
    if (!mir_emit_hl_to_home(out, insn->dst))
        return 0;
    if (preserve_hl)
        fputs("\tpop hl\n", out);
    if (preserve_hl_de)
        fputs("\tpop hl\n\tpop de\n", out);
    return 1;
}

int mir_emit_homed_binary_instruction(FILE *out,
                                      const struct MirInsn *insn,
                                      int allow_comparison)
{
    int instruction = (int)(insn - mir.insns);
    int left = insn->src1;
    int right = insn->src2;
    int commutative = insn->immediate == '+' || insn->immediate == '&' ||
                      insn->immediate == '|' || insn->immediate == '^' ||
                      insn->immediate == TOK_EQ || insn->immediate == TOK_NE ||
                      insn->immediate == '*';
    const char *runtime_helper =
        insn->immediate == '*' ? "__mulu" :
        insn->immediate == '/' ?
            ((insn->type & TYPE_UNSIGNED) != 0 ? "__divu" : "__divs") :
        insn->immediate == '%' ?
            ((insn->type & TYPE_UNSIGNED) != 0 ? "__modu" : "__mods") : NULL;
    int preserve_hl_de;
    int preserve_hl;
    int preserve_de;
    const struct MirInsn *left_definition;
    const struct MirInsn *right_definition;
    int comparison_unsigned;
    int biased_right_constant;

    if (mir.allocation_colors[right] == MIR_COLOR_HL) {
        int temporary;
        if (!commutative && runtime_helper == NULL)
            return 0;
        if (commutative) {
            temporary = left;
            left = right;
            right = temporary;
        }
    }
        preserve_hl_de =
            mir_home_color_live_across(instruction, MIR_COLOR_HL_DE);
        preserve_hl = !preserve_hl_de &&
            mir.allocation_colors[insn->dst] != MIR_COLOR_HL &&
            !(mir.allocation_colors[left] == MIR_COLOR_HL &&
              !mir_value_has_use_after(left, instruction));
        preserve_de = !preserve_hl_de &&
            mir.allocation_colors[insn->dst] != MIR_COLOR_DE &&
            !(mir.allocation_colors[right] == MIR_COLOR_DE &&
              !mir_value_has_use_after(right, instruction));
        left_definition = mir_definition(left);
        right_definition = mir_definition(right);
        comparison_unsigned =
            (left_definition != NULL &&
             mir_type_uses_unsigned_comparison(left_definition->type)) ||
            (right_definition != NULL &&
             mir_type_uses_unsigned_comparison(right_definition->type));
        biased_right_constant = allow_comparison && !comparison_unsigned &&
                                (insn->immediate == '<' ||
                                 insn->immediate == TOK_GE) &&
                                right_definition != NULL &&
                                right_definition->opcode == MIR_CONST;
    if (preserve_hl_de)
        fputs("\tpush de\n\tpush hl\n", out);
    if (preserve_hl)
        fputs("\tpush hl\n", out);
    if (preserve_de)
        fputs("\tpush de\n", out);
    if (runtime_helper != NULL &&
        mir.allocation_colors[right] == MIR_COLOR_HL &&
        !commutative) {
        if (!mir_emit_home_push(out, right) ||
            !mir_emit_home_to_hl(out, left))
            return 0;
        fputs("\tpop de\n", out);
    } else if (!mir_emit_home_to_hl(out, left)) {
        return 0;
    } else if (biased_right_constant) {
        fprintf(out, "\tld de,%ld\n",
                (right_definition->immediate ^ 0x8000L) & 0xffffL);
    } else if (!mir_emit_home_to_de(out, right)) {
        return 0;
    }
    if (runtime_helper != NULL)
        mir_emit_runtime_call(out, runtime_helper);
    else if (insn->immediate == '+')
        fputs("\tadd hl,de\n", out);
    else if (insn->immediate == '-')
        fputs("\tor a\n\tsbc hl,de\n", out);
    else if (insn->immediate == '&') {
        /* Item T48 (mir-text-size-plan.md): same byte-skip mask
         * optimization as the other three scalar/wide '&' call sites
         * (mir_emit_scalar_value above, and the two dcc_mir_spilled_cfg.c
         * sites) - this is the homed-scalar-cfg selector's own copy,
         * needed because it is tried before spilled-scalar-cfg and
         * so intercepts small straight-line functions like `x & 0xFF`
         * first. DE already holds right's materialized value from the
         * unconditional mir_emit_home_to_de call above (that redundant
         * load is the same deliberately deferred residual already
         * documented at the other call sites) - only the actual AND
         * instruction sequence is improved here. */
        if (right_definition != NULL && right_definition->opcode == MIR_CONST)
            mir_emit_word_and_constant(
                out, 'h', 'l',
                (unsigned int)(right_definition->immediate & 0xffffL));
        else
            fputs("\tld a,h\n\tand d\n\tld h,a\n\tld a,l\n\tand e\n\tld l,a\n", out);
    }
    else if (insn->immediate == '|')
        fputs("\tld a,h\n\tor d\n\tld h,a\n\tld a,l\n\tor e\n\tld l,a\n", out);
    else if (insn->immediate == '^')
        fputs("\tld a,h\n\txor d\n\tld h,a\n\tld a,l\n\txor e\n\tld l,a\n", out);
    else if (allow_comparison &&
             (insn->immediate == TOK_EQ || insn->immediate == TOK_NE ||
              insn->immediate == '<' || insn->immediate == '>' ||
              insn->immediate == TOK_LE || insn->immediate == TOK_GE)) {
        if (biased_right_constant)
            mir_emit_scalar_compare_biased_right(
                out, (int)insn->immediate);
        else
            mir_emit_scalar_compare(out, (int)insn->immediate,
                                    comparison_unsigned);
    } else {
        return 0;
    }
    if (!mir_emit_hl_to_home(out, insn->dst))
        return 0;
    if (preserve_de)
        fputs("\tpop de\n", out);
    if (preserve_hl)
        fputs("\tpop hl\n", out);
    if (preserve_hl_de)
        fputs("\tpop hl\n\tpop de\n", out);
    return 1;
}

int mir_emit_homed_constant_binary_instruction(FILE *out,
                                                const struct MirInsn *insn,
                                                int operation, long value)
{
    int instruction = (int)(insn - mir.insns);
    const struct MirInsn *source = mir_definition(insn->src1);
    unsigned long multiplier = (unsigned long)value & 0xffffUL;
    int uses_de = operation == '*' && multiplier != 0 &&
                  (multiplier & (multiplier - 1)) != 0;
    int preserve_hl_de =
        mir_home_color_live_across(instruction, MIR_COLOR_HL_DE);
    int preserve_hl =
        !preserve_hl_de &&
        mir.allocation_colors[insn->dst] != MIR_COLOR_HL &&
        mir_home_color_live_across(instruction, MIR_COLOR_HL);
    int preserve_de =
        !preserve_hl_de &&
        uses_de && mir.allocation_colors[insn->dst] != MIR_COLOR_DE &&
        mir_home_color_live_across(instruction, MIR_COLOR_DE);

    if (preserve_hl_de)
        fputs("\tpush de\n\tpush hl\n", out);
    if (preserve_hl)
        fputs("\tpush hl\n", out);
    if (preserve_de)
        fputs("\tpush de\n", out);
    if (!mir_emit_home_to_hl(out, insn->src1))
        return 0;
    if (operation == '*')
        mir_emit_mul_hl_const(out, multiplier);
    else
        mir_emit_scalar_shift_by_constant(
            out, operation,
            source != NULL && (source->type & TYPE_UNSIGNED) != 0, value);
    if (!mir_emit_hl_to_home(out, insn->dst))
        return 0;
    if (preserve_de)
        fputs("\tpop de\n", out);
    if (preserve_hl)
        fputs("\tpop hl\n", out);
    if (preserve_hl_de)
        fputs("\tpop hl\n\tpop de\n", out);
    return 1;
}

/* Count comparisons that require the general two-operand path rather than
 * the direct zero-test form. The selector uses this structural signal when
 * arbitrating homed and spilled candidates. */
static int mir_compare_is_general_form(int compare_index)
{
    const struct MirInsn *compare = &mir.insns[compare_index];
    const struct MirInsn *right_definition = mir_definition(compare->src2);
    if (right_definition != NULL && right_definition->opcode == MIR_CONST &&
        right_definition->immediate == 0 &&
        mir.allocation_colors[compare->src1] == MIR_COLOR_HL)
        return 0;
    return 1;
}

int mir_has_phi_instruction(void)
{
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_PHI)
            return 1;
    return 0;
}

int mir_general_comparison_count(void)
{
    int count = 0;
    int instruction;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        int compare_index;
        if (mir.insns[instruction].opcode != MIR_BRANCH_FALSE)
            continue;
        compare_index = mir_compare_definition_for_branch(instruction);
        if (compare_index >= 0 && mir_compare_is_general_form(compare_index))
            ++count;
    }

    return count;
}

int mir_target_is_noop_fallthrough(int instruction, int target)
{
    int scan;

    if (target <= instruction)
        return 0;
    for (scan = instruction + 1; scan < target; ++scan)
        if (mir.insns[scan].opcode != MIR_NOP &&
            mir.insns[scan].opcode != MIR_LABEL)
            return 0;
    return 1;
}

int mir_emit_homed_compare_false(FILE *out,
                                        const struct MirInsn *compare,
                                        int false_label)
{    int left = compare->src1;    int right = compare->src2;
    int instruction = (int)(compare - mir.insns);
    int operation = (int)compare->immediate;
    const struct MirInsn *left_definition;
    const struct MirInsn *right_definition;
    int preserve_hl_de;
    int preserve_hl;
    int preserve_de;
    int is_unsigned;

    right_definition = mir_definition(right);
    left_definition = mir_definition(left);
    if (right_definition != NULL && right_definition->opcode == MIR_CONST &&
        right_definition->immediate == 0 &&
        mir.allocation_colors[left] == MIR_COLOR_HL) {
        is_unsigned =
            left_definition != NULL &&
            mir_type_uses_unsigned_comparison(left_definition->type);
        if (operation == '>') {
            if (is_unsigned) {
                fputs("\tld a,h\n\tor l\n", out);
                fprintf(out, "\tjp z, L%d\n", false_label);
            } else {
                fputs("\tld a,h\n\tor a\n", out);
                fprintf(out, "\tjp m, L%d\n", false_label);
                fputs("\tor l\n", out);
                fprintf(out, "\tjp z, L%d\n", false_label);
            }
            return 1;
        }
        if (operation == TOK_GE) {
            if (!is_unsigned) {
                fputs("\tbit 7,h\n", out);
                fprintf(out, "\tjp nz, L%d\n", false_label);
            }
            return 1;
        }
        if (operation == '<') {
            if (is_unsigned) {
                fprintf(out, "\tjp L%d\n", false_label);
            } else {
                fputs("\tbit 7,h\n", out);
                fprintf(out, "\tjp z, L%d\n", false_label);
            }
            return 1;
        }
        if (operation == TOK_EQ || operation == TOK_NE) {
            fputs("\tld a,h\n\tor l\n", out);
            fprintf(out, operation == TOK_EQ ? "\tjp nz, L%d\n"
                                             : "\tjp z, L%d\n",
                    false_label);
            return 1;
        }
    }

    if (operation == '>' || operation == TOK_LE) {
        int temporary = left;
        left = right;
        right = temporary;
        operation = operation == '>' ? '<' : TOK_GE;
    }
    left_definition = mir_definition(left);
    right_definition = mir_definition(right);
    is_unsigned =
        (left_definition != NULL &&
         mir_type_uses_unsigned_comparison(left_definition->type)) ||
        (right_definition != NULL &&
         mir_type_uses_unsigned_comparison(right_definition->type));

    /* Preserve only values that actually span this comparison. The operands
     * themselves may be clobbered when this is their final use. */
    preserve_hl_de =
        mir_home_color_live_across(instruction, MIR_COLOR_HL_DE);
    preserve_hl = !preserve_hl_de &&
        mir_home_color_live_across(instruction, MIR_COLOR_HL);
    preserve_de = !preserve_hl_de &&
        mir_home_color_live_across(instruction, MIR_COLOR_DE);
    if (preserve_hl_de)
        fputs("\tpush de\n\tpush hl\n", out);
    if (preserve_hl)
        fputs("\tpush hl\n", out);
    if (preserve_de)
        fputs("\tpush de\n", out);
    if (!mir_emit_push_home(out, right) ||
        !mir_emit_home_to_hl(out, left))
        return 0;
    fputs("\tpop de\n", out);
    if (!is_unsigned && operation != TOK_EQ && operation != TOK_NE)
        fputs("\tld a,h\n\txor 128\n\tld h,a\n"
              "\tld a,d\n\txor 128\n\tld d,a\n", out);
    fputs("\tor a\n\tsbc hl,de\n", out);
    if (preserve_de)
        fputs("\tpop de\n", out);
    if (preserve_hl)
        fputs("\tpop hl\n", out);
    if (preserve_hl_de)
        fputs("\tpop hl\n\tpop de\n", out);
    if (operation == TOK_EQ)
        fprintf(out, "\tjp nz, L%d\n", false_label);
    else if (operation == TOK_NE)
        fprintf(out, "\tjp z, L%d\n", false_label);
    else if (operation == '<')
        fprintf(out, "\tjp nc, L%d\n", false_label);
    else
        fprintf(out, "\tjp c, L%d\n", false_label);
    return 1;
}

int mir_try_emit_homed_scalar_dag(FILE *out)
{
    int uses_iy;
    int frameless;
    int return_value = -1;
    int parameter_count = 0;
    int operation_count = 0;
    int i;

    if ((mir.return_type & 15) != TYPE_INT || type_size(mir.return_type) > 2 ||
        mir.allocation_spill_count != 0)
        return 0;
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if ((insn->dst >= 0 && type_size(insn->type) > 2) ||
            (insn->opcode == MIR_BINARY &&
             type_size(insn->secondary_offset) > 2))
            return 0;
        if (insn->dst >= 0 && mir.allocation_colors[insn->dst] < 0)
            return 0;
        if (insn->opcode == MIR_STORE && insn->object < 0)
            return 0;
        switch (insn->opcode) {
        case MIR_NOP: case MIR_LABEL: case MIR_CONST:
            break;
        case MIR_PARAM:
            ++parameter_count;
            break;
        case MIR_UNARY:
            ++operation_count;
            if (insn->immediate != 0 && insn->immediate != '+' &&
                insn->immediate != '-' && insn->immediate != '~' &&
                insn->immediate != '!')
                return 0;
            break;
        case MIR_BINARY:
            ++operation_count;
            if (insn->immediate != '+' && insn->immediate != '-' &&
                insn->immediate != '&' && insn->immediate != '|' &&
                insn->immediate != '^')
                return 0;
            if (mir.allocation_colors[insn->src2] == MIR_COLOR_HL)
                return 0;
            break;
        case MIR_RETURN:
            if (return_value >= 0)
                return 0;
            return_value = insn->src1;
            break;
        default:
            return 0;
        }
    }
    if (return_value < 0)
        return 0;
    if (parameter_count == 0 && operation_count == 0)
        return 0;

    uses_iy = mir_home_uses_iy();
    frameless = !uses_iy && mir_effective_local_bytes() == 0;
    if (frameless) {
        for (i = 0; i < mir.count; ++i)
            if (mir.insns[i].opcode == MIR_PARAM &&
                mir.insns[i].object >= 0 &&
                type_size(mir.objects[mir.insns[i].object].type) != 2)
                frameless = 0;
    }
    if (frameless) {
        if (opt_stack_check)
            mir_emit_runtime_call(out, "__stchk");
    } else {
        mir_emit_home_prologue(out, uses_iy);
    }
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        const struct MirObject *object;
        int parameter_offset;
        int true_label;

        switch (insn->opcode) {
        case MIR_NOP: case MIR_LABEL:
            break;
        case MIR_PARAM:
            if (insn->object < 0 || insn->object >= mir.object_count)
                return 0;
            object = &mir.objects[insn->object];
            parameter_offset = object->offset + (uses_iy ? 2 : 0);
            if (type_size(object->type) == 1) {
                int preserve_hl = mir.allocation_colors[insn->dst] != MIR_COLOR_HL;
                if (preserve_hl)
                    fputs("\tpush hl\n", out);
                fprintf(out, "\tld l,(ix%+d)\n", parameter_offset);
                if (type_is_bool(object->type)) {
                    true_label = new_label();
                    fputs("\tld a,l\n\tor a\n\tld hl,0\n", out);
                    fprintf(out, "\tjp z, L%d\n\tinc hl\nL%d:\n",
                            true_label, true_label);
                } else if ((object->type & TYPE_UNSIGNED) != 0) {
                    fputs("\tld h,0\n", out);
                } else {
                    mir_emit_signed_byte_extend(out);
                }
                if (!mir_emit_hl_to_home(out, insn->dst))
                    return 0;
                if (preserve_hl)
                    fputs("\tpop hl\n", out);
                break;
            } else {
                if (!(frameless
                    ? mir_emit_stack_word_param_to_home(
                        out, insn->dst, object->offset)
                    : mir_emit_word_param_to_home(
                        out, insn->dst, parameter_offset)))
                    return 0;
                break;
            }
            break;
        case MIR_CONST:
            if (!mir_emit_constant_to_home(out, insn->dst, insn->immediate))
                return 0;
            break;
        case MIR_UNARY:
            {
            int preserve_hl = mir.allocation_colors[insn->src1] != MIR_COLOR_HL &&
                              mir.allocation_colors[insn->dst] != MIR_COLOR_HL;
            if (preserve_hl)
                fputs("\tpush hl\n", out);
            if (!mir_emit_home_to_hl(out, insn->src1))
                return 0;
            if (insn->immediate == '-')
                fputs("\txor a\n\tsub l\n\tld l,a\n\tsbc a,a\n\tsub h\n\tld h,a\n", out);
            else if (insn->immediate == '~')
                fputs("\tld a,l\n\tcpl\n\tld l,a\n\tld a,h\n\tcpl\n\tld h,a\n", out);
            else if (insn->immediate == '!') {
                true_label = new_label();
                fputs("\tld a,h\n\tor l\n\tld hl,0\n", out);
                fprintf(out, "\tjp nz, L%d\n\tinc hl\nL%d:\n",
                        true_label, true_label);
            }
            if (!mir_emit_hl_to_home(out, insn->dst))
                return 0;
            if (preserve_hl)
                fputs("\tpop hl\n", out);
            break;
            }
        case MIR_BINARY:
            {
            int preserve_hl = mir.allocation_colors[insn->src1] != MIR_COLOR_HL &&
                              mir.allocation_colors[insn->dst] != MIR_COLOR_HL;
            int preserve_de = mir.allocation_colors[insn->src2] != MIR_COLOR_DE &&
                              mir.allocation_colors[insn->dst] != MIR_COLOR_DE;
            if (preserve_hl)
                fputs("\tpush hl\n", out);
            if (preserve_de)
                fputs("\tpush de\n", out);
            if (!mir_emit_home_to_hl(out, insn->src1) ||
                !mir_emit_home_to_de(out, insn->src2))
                return 0;
            if (insn->immediate == '+')
                fputs("\tadd hl,de\n", out);
            else if (insn->immediate == '-')
                fputs("\tor a\n\tsbc hl,de\n", out);
            else if (insn->immediate == '&')
                fputs("\tld a,h\n\tand d\n\tld h,a\n\tld a,l\n\tand e\n\tld l,a\n", out);
            else if (insn->immediate == '|')
                fputs("\tld a,h\n\tor d\n\tld h,a\n\tld a,l\n\tor e\n\tld l,a\n", out);
            else if (insn->immediate == '^')
                fputs("\tld a,h\n\txor d\n\tld h,a\n\tld a,l\n\txor e\n\tld l,a\n", out);
            if (!mir_emit_hl_to_home(out, insn->dst))
                return 0;
            if (preserve_de)
                fputs("\tpop de\n", out);
            if (preserve_hl)
                fputs("\tpop hl\n", out);
            break;
            }
        case MIR_RETURN:
            if (!mir_emit_home_to_hl(out, insn->src1))
                return 0;
            if (frameless)
                fputs("\tret\n", out);
            else
                mir_emit_home_epilogue(out, uses_iy);
            break;
        default:
            return 0;
        }
    }
    return 1;
}
