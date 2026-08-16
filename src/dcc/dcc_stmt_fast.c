/**
 * @file dcc_stmt_fast.c
 * @brief Emits an in-place increment or decrement through an HL address.
 *
 * @par Role
 * Updates byte, 16-bit, or 32-bit lvalues with compact Z80 sequences, using
 * carry/borrow ripple and early exits for multi-byte objects.
 *
 * @par Key entry points
 * emit_incdec_addr().
 *
 * @par Boundary
 * Callers resolve the lvalue, type, and pre/post value semantics. This target
 * helper neither analyzes statements nor selects a production MIR candidate.
 */

#include "dcc.h"
void emit_incdec_addr(int type, int op)
{
    int done;

    if (type_size(type) == 1) {
        if (op == TOK_INC)
            emit("\tinc (hl)\n");
        else
            emit("\tdec (hl)\n");
        return;
    }

    done = new_label();

    if (type_size(type) == 4) {
        /* 4-byte (long) ripple increment/decrement */
        if (op == TOK_INC) {
            emit("\tinc (hl)\n");
            emit_jp_label("jp nz,", done);
            emit("\tinc hl\n");
            emit("\tinc (hl)\n");
            emit_jp_label("jp nz,", done);
            emit("\tinc hl\n");
            emit("\tinc (hl)\n");
            emit_jp_label("jp nz,", done);
            emit("\tinc hl\n");
            emit("\tinc (hl)\n");
        } else {
            emit("\tld a,(hl)\n");
            emit("\tdec (hl)\n");
            emit("\tor a\n");
            emit_jp_label("jp nz,", done);
            emit("\tinc hl\n");
            emit("\tld a,(hl)\n");
            emit("\tdec (hl)\n");
            emit("\tor a\n");
            emit_jp_label("jp nz,", done);
            emit("\tinc hl\n");
            emit("\tld a,(hl)\n");
            emit("\tdec (hl)\n");
            emit("\tor a\n");
            emit_jp_label("jp nz,", done);
            emit("\tinc hl\n");
            emit("\tdec (hl)\n");
        }
    } else {
        /* 2-byte (int) ripple increment/decrement */
        if (op == TOK_INC) {
            emit("\tinc (hl)\n");
            emit_jp_label("jp nz,", done);
            emit("\tinc hl\n");
            emit("\tinc (hl)\n");
        } else {
            emit("\tld a,(hl)\n");
            emit("\tdec (hl)\n");
            emit("\tor a\n");
            emit_jp_label("jp nz,", done);
            emit("\tinc hl\n");
            emit("\tdec (hl)\n");
        }
    }

    emit_label(done);
}



void skip_initializer_or_decl_tail(void);

