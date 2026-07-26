/*
 * dcc_assign.c - shared assignment/float emit helpers for the AST emitter.
 *
 * Low-level primitives the AST emitter calls while lowering assignments and
 * float values: emit_load_float_bits materialises a 32-bit float constant into
 * the DE:HL register pair, and emit_global_byte_array_index_addr computes the
 * address of a byte-array element at a global symbol (constant or ix-relative
 * index). Assignment statement/expression lowering itself lives in the AST
 * emitter (dcc_ast_gen*.c).
 *
 * MODULE: compiled as its own translation unit; shared declarations are in dcc.h.
 * Source provenance: monolith src/ddc.c lines 11521-12417.
 */

#include "dcc.h"
#include "dcc_ast.h"
void emit_load_float_bits(unsigned long bits)
{
    if (!scan_mode) {
        fprintf(g_emit_sink.stream, "\tld hl,%lu\n", bits & 0xffffUL);
        fprintf(g_emit_sink.stream, "\tld de,%lu\n", (bits >> 16) & 0xffffUL);
    }
}


void emit_global_byte_array_index_addr(struct Sym *arr, struct Sym *idx_sym, long idx_const, int has_const)
{
    emit_extrn_if_needed(arr);
    if (has_const) {
        if (idx_const == 0)
            fprintf(g_emit_sink.stream, "\tld hl,%s\n", asm_name_for(sym_asm_name(arr)));
        else
            fprintf(g_emit_sink.stream, "\tld hl,%s+%ld\n", asm_name_for(sym_asm_name(arr)), idx_const & 0xffffL);
    } else if (type_size(idx_sym->type) == 2) {
        /* Matches the canonical "index into HL, base into DE" shape that
         * structural peephole passes (e.g. the LDIR-memset loop rewrite)
         * recognise from loops indexed by a plain int, rather than the
         * "base into HL, index into DE" order used below for a byte index.
         * A REG_BC-resident index (dcc_loop_regalloc.c) reads from BC
         * instead of the frame slot directly, same as emit_load_sym_value_
         * direct's own REG_BC fast path (dcc_symbols.c) - the caller
         * (ast_global_byte_array_fast_store) already restricts idx_sym to
         * sym_can_ix_direct(idx_sym), which a reg_alloc'd symbol always
         * fails, so this branch previously never saw one; now that Phase
         * 4b lets a loop's own index variable be BC-resident even while
         * used in its own condition, this is reachable and the raw ix-load
         * below would silently read the wrong (possibly stale, for a write
         * candidate) value. */
        if (idx_sym->reg_alloc == REG_BC) {
            emit("\tld l,c\n\tld h,b\n");
        } else {
            fprintf(g_emit_sink.stream, "\tld l,(ix%+d)\n", idx_sym->offset);
            fprintf(g_emit_sink.stream, "\tld h,(ix%+d)\n", idx_sym->offset + 1);
        }
        fprintf(g_emit_sink.stream, "\tld de,%s\n", asm_name_for(sym_asm_name(arr)));
        emit("\tadd hl,de\n");
    } else {
        fprintf(g_emit_sink.stream, "\tld hl,%s\n", asm_name_for(sym_asm_name(arr)));
        fprintf(g_emit_sink.stream, "\tld e,(ix%+d)\n", idx_sym->offset);
        emit("\tld d,0\n");
        emit("\tadd hl,de\n");
    }
}

