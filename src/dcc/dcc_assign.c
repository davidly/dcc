/*
 * dcc_assign.c - shared assignment/float emit helpers for the AST emitter.
 *
 * Low-level primitives the AST emitter calls while lowering assignments and
 * float values: emit_load_float_bits materialises a 32-bit float constant into
 * the HL:DE register pair, and emit_global_byte_array_index_addr computes the
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
        fprintf(outf, "\tld hl,%lu\n", bits & 0xffffUL);
        fprintf(outf, "\tld de,%lu\n", (bits >> 16) & 0xffffUL);
    }
}


void emit_global_byte_array_index_addr(struct Sym *arr, struct Sym *idx_sym, long idx_const, int has_const)
{
    emit_extrn_if_needed(arr);
    if (has_const) {
        if (idx_const == 0)
            fprintf(outf, "\tld hl,%s\n", asm_name_for(sym_asm_name(arr)));
        else
            fprintf(outf, "\tld hl,%s+%ld\n", asm_name_for(sym_asm_name(arr)), idx_const & 0xffffL);
    } else if (type_size(idx_sym->type) == 2) {
        /* Matches the canonical "index into HL, base into DE" shape that
         * structural peephole passes (e.g. the LDIR-memset loop rewrite)
         * recognise from loops indexed by a plain int, rather than the
         * "base into HL, index into DE" order used below for a byte index. */
        fprintf(outf, "\tld l,(ix%+d)\n", idx_sym->offset);
        fprintf(outf, "\tld h,(ix%+d)\n", idx_sym->offset + 1);
        fprintf(outf, "\tld de,%s\n", asm_name_for(sym_asm_name(arr)));
        emit("\tadd hl,de\n");
    } else {
        fprintf(outf, "\tld hl,%s\n", asm_name_for(sym_asm_name(arr)));
        fprintf(outf, "\tld e,(ix%+d)\n", idx_sym->offset);
        emit("\tld d,0\n");
        emit("\tadd hl,de\n");
    }
}

