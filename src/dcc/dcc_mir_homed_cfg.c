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

int mir_try_emit_homed_scalar_cfg(FILE *out)
{
    int *labels;
    int uses_iy;
    int frameless;
    int return_count = 0;
    int last_insn_is_return;
    int shared_epilogue_label = -1;
    int i;
    int accepted = 0;
    /* Item 20d (mir-migration-plan-to-100pct.md): whether this function
     * contains any wide (4-byte long) value, restricted below to only
     * MIR_CONST dst and a wide long return. If set, acceptance is
     * conditional on mir_probe_wide_colors_for_homed() succeeding
     * (single wide value fits in HL:DE with zero spills) - see that
     * function's comment for why MIR_COLOR_BC_IY is excluded from this
     * first slice, and why MIR_PARAM/MIR_BINARY wide operands remain
     * deferred (no wide move/frame-offset helpers exist for them yet). */
    int has_wide = 0;
    int wide_return = type_is_long(mir.return_type) &&
                      type_size(mir.return_type) == 4;

    /* Phase 1 (mir-migration-plan-to-100pct.md), Item 8: a corpus-wide
     * zero-spill-fallback survey found "return-type" (base type != int)
     * is by far the single largest homed-scalar-cfg rejection cause
     * (997 of 1861 zero-spill functions surveyed). void is the safest
     * subset to add first: no calling-convention width change, no new
     * return-value register to track, and MIR_CALL's own void handling
     * below (skip storing a result to home when the callee's type is
     * void) already establishes the pattern MIR_RETURN reuses. */
    if (((mir.return_type & 15) != TYPE_INT &&
         (mir.return_type & 15) != TYPE_VOID && !wide_return) ||
        (!wide_return && type_size(mir.return_type) > 2) ||
        mir.allocation_spill_count != 0)
        return 0;
    /* Item 21 fix (mir-migration-plan-to-100pct.md): this selector's
     * prologue (mir_emit_home_prologue -> mir_emit_prologue) never
     * reserves any stack space for memory-resident locals - it only
     * knows how to push/pop iy and set up ix, exactly as needed for
     * purely register-homed scalars. Widening MIR_ADDRESS/MIR_LOAD
     * acceptance to admit SC_LOCAL objects (Items 9/16) silently let
     * through functions with a real memory-backed local object (e.g. a
     * char array whose address is taken and passed to a callee) whose
     * frame slot was never allocated at all: mir.local_bytes bytes of
     * "local" storage that legacy always subtracts from sp are simply
     * never subtracted here, so any MIR_ADDRESS of such an object
     * computes an ix-relative pointer into unreserved (and later
     * clobbered by push/call activity) stack memory. Found via
     * tests/tptrixld.c's `main` (a local `char buf[32]` passed to two
     * callees) silently corrupting its contents. Reject outright until
     * this selector grows real frame-space reservation/restore support -
     * mir.local_bytes is always 0 for pure scalar-only frames (the
     * previously-exercised population), so this only newly excludes the
     * unsafe aggregate-local shape, not the existing scalar coverage. */
    if (mir.local_bytes != 0)
        return 0;
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->dst >= 0 && type_size(insn->type) > 2) {
            if (insn->opcode == MIR_CONST && type_is_long(insn->type) &&
                type_size(insn->type) == 4)
                has_wide = 1;
            else
                return 0;
        }
        if (insn->opcode == MIR_BINARY &&
            type_size(insn->secondary_offset) > 2)
            return 0;
        if (insn->dst >= 0 && mir.allocation_colors[insn->dst] < 0)
            return 0;
        switch (insn->opcode) {
        case MIR_NOP: case MIR_LABEL: case MIR_PARAM: case MIR_CONST:
        case MIR_PHI: case MIR_JUMP: case MIR_BRANCH_FALSE:
            break;
        case MIR_STORE:
            if (!mir_object_is_fully_promoted(insn->object))
                return 0;
            break;
        case MIR_ADDRESS:
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
                    return 0;
                if (memory_storage != SC_LOCAL && memory_storage != SC_PARAM &&
                    memory_storage != SC_GLOBAL &&
                    memory_storage != SC_EXTERN && memory_storage != SC_FUNC)
                    return 0;
                if (mir_declared_is_vla_object(insn->name))
                    return 0;
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
            /* Item 22: only the constant-index subset is accepted here -
             * the same narrow slice mir_try_emit_spilled_scalar_cfg's own
             * fast path special-cases (index_definition->opcode ==
             * MIR_CONST). A variable (runtime) index needs a __mulu
             * runtime call for the stride multiply, and insn->base_name
             * set means a VLA whose stride itself is a memory-resident
             * value - both are real, separate scope creep from this first
             * step (mirroring Item 9's own narrow-first-slice precedent
             * for MIR_LOAD). Emission reuses the same mir_emit_pointer_
             * offset_address_to_home helper as MIR_MEMBER_ADDRESS, with
             * the constant byte offset folded at selection time exactly
             * like the spilled-scalar-cfg selector already does. */
            {
                const struct MirInsn *index_definition =
                    mir_definition(insn->src2);
                if (insn->base_name[0] != 0)
                    return 0;
                if (index_definition == NULL ||
                    index_definition->opcode != MIR_CONST)
                    return 0;
            }
            break;
        case MIR_LOAD_INDIRECT:
            /* Item 22: dereferencing an arbitrary already-homed pointer
             * value (e.g. `*p`), as opposed to MIR_LOAD's fixed ix/global
             * memory location. Narrowest safe slice, mirroring MIR_LOAD's
             * own Item 9 restriction: a plain 2-byte scalar, no bitfield
             * extraction and no 1-byte (char) sign/zero-extend or bool-
             * normalization logic (those need the same extra machinery
             * mir_try_emit_spilled_scalar_cfg's own MIR_LOAD_INDIRECT case
             * carries - real, but separate scope creep). 4-byte (long)
             * loads are also deferred (no wide-value support here yet). */
            if (type_is_struct_object(insn->type) ||
                type_size(insn->type) != 2 || insn->bit_width > 0 ||
                (insn->memory_size != 0 && insn->memory_size != 2))
                return 0;
            break;
        case MIR_STORE_INDIRECT:
            /* Item 23 (mir-migration-plan-to-100pct.md): writing through
             * an arbitrary already-homed pointer value (e.g. `*p = v`),
             * the write-side mirror of Item 22's MIR_LOAD_INDIRECT.
             * Restricted to the identical narrow slice for the same
             * reason: a plain 2-byte scalar, no bitfield packing (that
             * needs a read-modify-write sequence, real but separate
             * scope), no 1-byte (char) store and no 4-byte (long) store
             * (neither has a homed emission path here yet). */
            if (insn->bit_width > 0 ||
                (insn->memory_size != 0 && insn->memory_size != 2))
                return 0;
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
                return 0;
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
                    return 0;
                if (memory_storage != SC_LOCAL && memory_storage != SC_PARAM &&
                    memory_storage != SC_GLOBAL &&
                    memory_storage != SC_EXTERN && memory_storage != SC_FUNC)
                    return 0;
                if (type_is_struct_object(memory_type) ||
                    type_is_struct_object(insn->type))
                    return 0;
                if (type_size(memory_type) != 2 || type_size(insn->type) != 2)
                    return 0;
                if (mir_general_comparison_count() > 1)
                    return 0;
            }
            break;
        case MIR_UNARY:
            if (insn->immediate != 0 && insn->immediate != '+' &&
                insn->immediate != '-' && insn->immediate != '~' &&
                insn->immediate != '!')
                return 0;
            break;
        case MIR_BINARY:
            if (insn->immediate != '+' && insn->immediate != '-' &&
                insn->immediate != '&' && insn->immediate != '|' &&
                insn->immediate != '^' && insn->immediate != TOK_EQ &&
                insn->immediate != TOK_NE && insn->immediate != '<' &&
                insn->immediate != '>' && insn->immediate != TOK_LE &&
                insn->immediate != TOK_GE)
                return 0;
            break;
        case MIR_RETURN:
            ++return_count;
            break;
        case MIR_ARG:
            if (type_is_struct_object(insn->type) || type_size(insn->type) > 2)
                return 0;
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
                    return 0;
                if ((insn->memory_flags & (32 | 64)) != 0)
                    return 0;
            }
            break;
        default:
            return 0;
        }
    }
    /* A void function may legitimately fall off the end with no explicit
     * "return;" at all - mir_try_emit_spilled_scalar_cfg's own preflight
     * (the "implicit-return" reject reason) already treats this as valid
     * only for TYPE_VOID; mirror that here instead of requiring at least
     * one MIR_RETURN unconditionally. */
    if (return_count == 0 && (mir.return_type & 15) != TYPE_VOID)
        return 0;
    /* Item 20d: a wide long return with no wide value at all (e.g. an
     * implicit-int-promoted narrow expression) still needs the probe, so
     * gate on wide_return too, not just has_wide. */
    if ((has_wide || wide_return) && !mir_probe_wide_colors_for_homed())
        return 0;

    labels = (int *)malloc((size_t)mir.next_label * sizeof(*labels));
    if (labels == NULL)
        fatal("out of memory selecting homed MIR CFG labels");
    for (i = 0; i < mir.next_label; ++i)
        labels[i] = new_label();

    uses_iy = mir_home_uses_iy();
    frameless = !uses_iy;
    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_PARAM &&
            (mir.insns[i].object < 0 ||
             type_size(mir.objects[mir.insns[i].object].type) != 2)) {
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
            fputs("\textrn __stchk\n\tcall __stchk\n", out);
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
        case MIR_NOP: case MIR_PHI: case MIR_STORE:
            break;
        case MIR_LOAD:
            {
                int memory_type, memory_storage, memory_offset;
                int instruction = (int)(insn - mir.insns);

                if (!mir_scalar_memory_location(insn, &memory_type,
                                                &memory_storage,
                                                &memory_offset))
                    goto done;
                preserve_hl = mir.allocation_colors[insn->dst] != MIR_COLOR_HL &&
                    mir_home_color_live_across(instruction, MIR_COLOR_HL);
                if (preserve_hl)
                    fputs("\tpush hl\n", out);
                if (memory_storage == SC_LOCAL ||
                    memory_storage == SC_PARAM) {
                    if (memory_offset >= -128 &&
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
                    if (memory_storage == SC_EXTERN ||
                        (memory_storage == SC_FUNC && global != NULL &&
                         global->needs_extrn))
                        fprintf(out, "\textrn %s\n", assembly_name);
                    fprintf(out, "\tld hl,(%s)\n", assembly_name);
                }
                if (!mir_emit_hl_to_home(out, insn->dst))
                    goto done;
                if (preserve_hl)
                    fputs("\tpop hl\n", out);
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
                if ((global != NULL && global->storage == SC_FUNC) ||
                    memory_storage == SC_GLOBAL ||
                    memory_storage == SC_EXTERN ||
                    memory_storage == SC_FUNC) {
                    const char *assembly_name = asm_name_for(
                        global != NULL ? sym_asm_name(global)
                                       : mir_declared_link_name(insn->name));
                    if (memory_storage == SC_EXTERN ||
                        (global != NULL && global->storage == SC_FUNC &&
                         global->needs_extrn))
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
                sprintf(label, "S%ld", insn->immediate);
                if (!mir_emit_label_address_to_home(out, insn->dst, label))
                    goto done;
            }
            break;
        case MIR_MEMBER_ADDRESS:
            if (!mir_emit_pointer_offset_address_to_home(
                    out, insn->dst, insn->src1, insn->immediate))
                goto done;
            break;
        case MIR_INDEX_ADDRESS:
            {
                const struct MirInsn *index_definition =
                    mir_definition(insn->src2);
                long byte_offset;
                if (index_definition == NULL ||
                    index_definition->opcode != MIR_CONST)
                    goto done;
                byte_offset = index_definition->immediate * insn->immediate;
                if (!mir_emit_pointer_offset_address_to_home(
                        out, insn->dst, insn->src1, byte_offset))
                    goto done;
            }
            break;
        case MIR_LOAD_INDIRECT:
            {
                int instruction = (int)(insn - mir.insns);
                int preserve_hl =
                    mir.allocation_colors[insn->dst] != MIR_COLOR_HL &&
                    mir_home_color_live_across(instruction, MIR_COLOR_HL);
                if (preserve_hl) fputs("\tpush hl\n", out);
                if (!mir_emit_home_to_hl(out, insn->src1))
                    goto done;
                fputs("\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n", out);
                if (mir.allocation_colors[insn->dst] != MIR_COLOR_HL &&
                    !mir_emit_hl_to_home(out, insn->dst))
                    goto done;
                if (preserve_hl) fputs("\tpop hl\n", out);
            }
            break;
        case MIR_STORE_INDIRECT:
            {
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
                int instruction = (int)(insn - mir.insns);
                int preserve_hl =
                    mir_home_color_live_across(instruction, MIR_COLOR_HL);
                int preserve_de =
                    mir_home_color_live_across(instruction, MIR_COLOR_DE);
                if (preserve_hl) fputs("\tpush hl\n", out);
                if (preserve_de) fputs("\tpush de\n", out);
                if (!mir_emit_home_to_hl(out, insn->src1))
                    goto done;
                fputs("\tpush hl\n", out);
                if (!mir_emit_home_to_hl(out, insn->src2))
                    goto done;
                fputs("\tex de,hl\n\tpop hl\n\tld (hl),e\n", out);
                if (insn->memory_size != 1)
                    fputs("\tinc hl\n\tld (hl),d\n", out);
                if (preserve_de) fputs("\tpop de\n", out);
                if (preserve_hl) fputs("\tpop hl\n", out);
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
                int preserve_hl =
                    mir_home_color_live_across(instruction, MIR_COLOR_HL);
                int preserve_bc =
                    mir_home_color_live_across(instruction, MIR_COLOR_BC);
                if (preserve_hl) fputs("\tpush hl\n", out);
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
                if (preserve_hl) fputs("\tpop hl\n", out);
            }
            break;
        case MIR_LABEL:
            if (insn->label < 0 || insn->label >= mir.next_label)
                goto done;
            fprintf(out, "L%d:\n", labels[insn->label]);
            break;
        case MIR_PARAM:
            if (!mir_value_has_use(insn->dst))
                break;
            object = &mir.objects[insn->object];
            if (!(frameless
                  ? mir_emit_stack_word_param_to_home(
                        out, insn->dst, object->offset)
                  : mir_emit_word_param_to_home(
                        out, insn->dst, object->offset + 2)))
                goto done;
            break;
        case MIR_CONST:
            /* mir-text-size Item T19: skip materializing a constant whose
             * sole use is the dead index-address shape described above
             * mir_index_only_constant's definition. Mirrors MIR_UNARY's
             * own dead-result skip just below (Item T12) and
             * dcc_mir_spilled_cfg.c's identical MIR_CONST check (Item
             * T18). */
            if (mir_index_only_constant(insn->dst))
                break;
            /* Item 20d: dst may be wide (long) only if mir_probe_wide_
             * colors_for_homed accepted this function - dispatch on the
             * value's own type rather than a separate has_wide flag so
             * this stays correct even if more wide-eligible opcodes are
             * added above later. */
            if (type_is_long(insn->type) && type_size(insn->type) == 4) {
                if (!mir_emit_wide_constant_to_home(out, insn->dst,
                                                    insn->immediate))
                    goto done;
            } else if (!mir_emit_constant_to_home(out, insn->dst,
                                                  insn->immediate)) {
                goto done;
            }
            break;
        case MIR_UNARY:
            /* mir-text-size Item T12: same dead-result skip as the
             * spilled-scalar-cfg selector's MIR_UNARY case - see
             * dcc_mir_spilled_cfg.c for the full rationale. */
            if (!mir_value_has_use(insn->dst))
                break;
            if (!mir_emit_homed_unary_instruction(out, insn))
                goto done;
            break;
        case MIR_BINARY:
            if (mir_direct_branch_for_comparison(i) >= 0)
                break;
            if (!mir_emit_homed_binary_instruction(out, insn, 1))
                goto done;
            break;
        case MIR_JUMP:
            target = mir_find_label(insn->label);
            if (target < 0 || !mir_emit_homed_phi_copies(out, i, target))
                goto done;
            /* mir-text-size Item T8: same fallthrough-jump elision as the
             * spilled-scalar-cfg selector - see dcc_mir_spilled_cfg.c's
             * MIR_JUMP case for the full rationale. */
            if (target != i + 1)
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
            true_label = new_label();
            preserve_hl = mir.allocation_colors[insn->src1] != MIR_COLOR_HL;
            if (preserve_hl)
                fputs("\tpush hl\n", out);
            if (!mir_emit_home_to_hl(out, insn->src1))
                goto done;
            fputs("\tld a,h\n\tor l\n", out);
            if (preserve_hl)
                fputs("\tpop hl\n", out);
            fprintf(out, "\tjp nz, L%d\n", true_label);
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
                    fputs("\tpop bc\n\tpop de\n\tpop hl\n"
                          "\textrn __msf\n\tcall __msf\n", out);
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
                    fputs("\textrn __slf\n\tcall __slf\n", out);
                    if (!mir_emit_hl_to_home(out, insn->dst))
                        goto done;
                    break;
                }
                if (mir_call_is_strchr_fastcall(i, &s_value, &c_value)) {
                    if (!mir_emit_home_push(out, s_value) ||
                        !mir_emit_home_push(out, c_value))
                        goto done;
                    fputs("\tpop hl\n\tld a,l\n\tpop hl\n"
                          "\textrn __chf\n\tcall __chf\n", out);
                    if (!mir_emit_hl_to_home(out, insn->dst))
                        goto done;
                    break;
                }
                if (mir_call_is_strrchr_fastcall(i, &s_value, &c_value)) {
                    if (!mir_emit_home_push(out, s_value) ||
                        !mir_emit_home_push(out, c_value))
                        goto done;
                    fputs("\tpop hl\n\tld a,l\n\tpop hl\n"
                          "\textrn __rcf\n\tcall __rcf\n", out);
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
                    fputs("\tpop bc\n\tpop de\n\tpop hl\n"
                          "\textrn __mhf\n\tcall __mhf\n", out);
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
                    fputs("\tpop bc\n\tpop hl\n\tpop de\n"
                          "\textrn __cmpf\n\tcall __cmpf\n", out);
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
                    fputs("\tpop bc\n\tpop hl\n\tpop de\n"
                          "\textrn __mcf\n\tcall __mcf\n", out);
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
                    fprintf(out, "\textrn %s\n\tcall %s\n", rtl_name,
                            rtl_name);
                    if (!mir_emit_hl_to_home(out, insn->dst))
                        goto done;
                    break;
                }
                if (mir_call_is_bdos_family_fastcall(i, &rtl_name, &fn_value,
                                                    &dearg_value)) {
                    if (!mir_emit_home_push(out, fn_value) ||
                        !mir_emit_home_push(out, dearg_value))
                        goto done;
                    fprintf(out, "\tpop de\n\tpop hl\n\tld c,l\n"
                            "\textrn %s\n\tcall %s\n", rtl_name, rtl_name);
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
                    if (arg->opcode != MIR_ARG ||
                        arg->secondary_offset != insn->secondary_offset)
                        continue;
                    if (arg->immediate != argument--)
                        goto done;
                    if (!mir_emit_home_push(out, arg->src1))
                        goto done;
                    argument_bytes += 2;
                }
                if (argument != -1)
                    goto done;
                if (callee->needs_extrn)
                    fprintf(out, "\textrn %s\n", assembly_name);
                fprintf(out, "\tcall %s\n", assembly_name);
                for (argument = 0; argument < argument_bytes / 2; ++argument)
                    fputs("\tpop bc\n", out);
                if (type_ptr_depth(insn->type) > 0 ||
                    (insn->type & 15) != TYPE_VOID) {
                    if (!mir_emit_hl_to_home(out, insn->dst))
                        goto done;
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
            if ((mir.return_type & 15) != TYPE_VOID) {
                if (type_is_long(mir.return_type) &&
                    type_size(mir.return_type) == 4) {
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
        if (insn->opcode != MIR_JUMP && insn->opcode != MIR_BRANCH_FALSE &&
            insn->opcode != MIR_RETURN && i + 1 < mir.count &&
            mir_edge_phi_names_predecessor(i, i + 1) &&
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
    accepted = 1;
done:
    if (!accepted && getenv("DCC_MIR_SELECT_REPORT") != NULL)
        fprintf(stderr, "; MIR home-cfg reject function=%s insn=%d opcode=%s\n",
                mir.name, i,
                i >= 0 && i < mir.count
                    ? mir_opcode_name(mir.insns[i].opcode) : "preflight");
    free(labels);
    return accepted;
}
