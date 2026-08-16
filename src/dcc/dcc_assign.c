/**
 * @file dcc_assign.c
 * @brief Supplies small target helpers shared by assignment-related paths.
 *
 * @par Role
 * Materializes a 32-bit float bit pattern in DE:HL and computes the address of
 * an indexed global byte-array element for constant or frame-local indices.
 *
 * @par Key entry points
 * emit_load_float_bits() and emit_global_byte_array_index_addr().
 *
 * @par Boundary
 * This file does not own assignment semantics or candidate selection.
 * Production function-body output is committed only through selected MIR
 * candidates.
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
         * "base into HL, index into DE" order used below for a byte index. */
        fprintf(g_emit_sink.stream, "\tld l,(ix%+d)\n", idx_sym->offset);
        fprintf(g_emit_sink.stream, "\tld h,(ix%+d)\n", idx_sym->offset + 1);
        fprintf(g_emit_sink.stream, "\tld de,%s\n", asm_name_for(sym_asm_name(arr)));
        emit("\tadd hl,de\n");
    } else {
        fprintf(g_emit_sink.stream, "\tld hl,%s\n", asm_name_for(sym_asm_name(arr)));
        fprintf(g_emit_sink.stream, "\tld e,(ix%+d)\n", idx_sym->offset);
        emit("\tld d,0\n");
        emit("\tadd hl,de\n");
    }
}
