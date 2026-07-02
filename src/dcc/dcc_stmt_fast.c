/*
 * dcc_stmt_fast.c - in-place increment/decrement address-form emit helper.
 *
 * emit_incdec_addr emits an in-place ++/-- on the lvalue whose address is
 * already in HL, sized for the operand type: a single inc/dec (hl) for bytes,
 * or a multi-byte ripple with early-out for 16- and 32-bit operands. Called by
 * the AST emitter when lowering pre/post increment and decrement.
 *
 * MODULE: compiled as its own translation unit; shared declarations are in dcc.h.
 * Source provenance: monolith src/ddc.c lines 12418-13127.
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

