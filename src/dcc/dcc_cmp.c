/*
 * dcc_cmp.c - comparison and conditional-branch code generation.
 *
 * Relational/equality comparison codegen (signed and unsigned, 16- and 32-bit)
 * and the direct condition-to-branch lowering used by if/while/for. Includes
 * the byte-operand fast comparators that emit a single cp + conditional jump.
 * struct ByteOperand is declared in dcc.h.
 *
 * MODULE: compiled as its own translation unit; shared declarations are in dcc.h.
 * Source provenance: monolith src/ddc.c lines 8842-9235 and 9242-10184
 * (the struct ByteOperand definition at 9236-9241 is hoisted to dcc.h).
 */

#include "dcc.h"
void gen_cmp(int op)
{
    int lt;
    int le;

    lt = new_label();
    le = new_label();

    if (op == '<' || op == TOK_GE || op == '>' || op == TOK_LE) {
        emit("\tld a,h\n");
        emit("\txor 80h\n");
        emit("\tld h,a\n");
        emit("\tld a,d\n");
        emit("\txor 80h\n");
        emit("\tld d,a\n");
    }    

    emit("\tor a\n\tsbc hl,de\n");

    if (op == TOK_EQ) emit_jp_label("jp z,", lt);
    else if (op == TOK_NE) emit_jp_label("jp nz,", lt);
    else if (op == '<') emit_jp_label("jp c,", lt);
    else if (op == TOK_GE) emit_jp_label("jp nc,", lt);
    else if (op == '>') {
        int lfalse_gt = new_label();
        emit_jp_label("jp z,", lfalse_gt);
        emit_jp_label("jp nc,", lt);
        emit_label(lfalse_gt);
    } else if (op == TOK_LE) {
        emit_jp_label("jp z,", lt);
        emit_jp_label("jp c,", lt);
    }

    emit("\tld hl,0\n");
    emit_jp_label("jp", le);
    emit_label(lt);
    emit("\tld hl,1\n");
    emit_label(le);
}

void gen_cmp_typed(int op, int lhs_type)
{
    int lt;
    int le;

    lt = new_label();
    le = new_label();

    if (!(lhs_type & TYPE_UNSIGNED) &&
        (op == '<' || op == TOK_GE || op == '>' || op == TOK_LE)) {
        emit("\tld a,h\n");
        emit("\txor 80h\n");
        emit("\tld h,a\n");
        emit("\tld a,d\n");
        emit("\txor 80h\n");
        emit("\tld d,a\n");
    }

    emit("\tor a\n\tsbc hl,de\n");

    if (op == TOK_EQ) emit_jp_label("jp z,", lt);
    else if (op == TOK_NE) emit_jp_label("jp nz,", lt);
    else if (op == '<') emit_jp_label("jp c,", lt);
    else if (op == TOK_GE) emit_jp_label("jp nc,", lt);
    else if (op == '>') {
        int lfalse_gt = new_label();
        emit_jp_label("jp z,", lfalse_gt);
        emit_jp_label("jp nc,", lt);
        emit_label(lfalse_gt);
    } else if (op == TOK_LE) {
        emit_jp_label("jp z,", lt);
        emit_jp_label("jp c,", lt);
    }

    emit("\tld hl,0\n");
    emit_jp_label("jp", le);
    emit_label(lt);
    emit("\tld hl,1\n");
    emit_label(le);
}

void emit_signed_bias_for_relop(int op)
{
    if (op == '<' || op == TOK_GE || op == '>' || op == TOK_LE) {
        emit("\tld a,h\n");
        emit("\txor 80h\n");
        emit("\tld h,a\n");
        emit("\tld a,d\n");
        emit("\txor 80h\n");
        emit("\tld d,a\n");
    }
}

void emit_cmp_branch_false(int op, int lfalse)
{
    int ltrue;

    emit_signed_bias_for_relop(op);
    emit("\tor a\n\tsbc hl,de\n");

    if (op == TOK_EQ) {
        emit_jp_label("jp nz,", lfalse);
    } else if (op == TOK_NE) {
        emit_jp_label("jp z,", lfalse);
    } else if (op == '<') {
        emit_jp_label("jp nc,", lfalse);
    } else if (op == TOK_GE) {
        emit_jp_label("jp c,", lfalse);
    } else if (op == '>') {
        emit_jp_label("jp z,", lfalse);
        emit_jp_label("jp c,", lfalse);
    } else if (op == TOK_LE) {
        ltrue = new_label();
        emit_jp_label("jp z,", ltrue);
        emit_jp_label("jp c,", ltrue);
        emit_jp_label("jp", lfalse);
        emit_label(ltrue);
    }
}

void emit_cmp_branch_true(int op, int ltrue)
{
    int ldone;

    emit_signed_bias_for_relop(op);
    emit("\tor a\n\tsbc hl,de\n");

    if (op == TOK_EQ) {
        emit_jp_label("jp z,", ltrue);
    } else if (op == TOK_NE) {
        emit_jp_label("jp nz,", ltrue);
    } else if (op == '<') {
        emit_jp_label("jp c,", ltrue);
    } else if (op == TOK_GE) {
        emit_jp_label("jp nc,", ltrue);
    } else if (op == '>') {
        ldone = new_label();
        emit_jp_label("jp z,", ldone);
        emit_jp_label("jp nc,", ltrue);
        emit_label(ldone);
    } else if (op == TOK_LE) {
        emit_jp_label("jp z,", ltrue);
        emit_jp_label("jp c,", ltrue);
    }
}


void emit_cmp_branch_false_unsigned(int op, int lfalse)
{
    int ltrue;

    emit("\tor a\n\tsbc hl,de\n");

    if (op == '<') {
        emit_jp_label("jp nc,", lfalse);
    } else if (op == TOK_GE) {
        emit_jp_label("jp c,", lfalse);
    } else if (op == '>') {
        emit_jp_label("jp z,", lfalse);
        emit_jp_label("jp c,", lfalse);
    } else if (op == TOK_LE) {
        ltrue = new_label();
        emit_jp_label("jp z,", ltrue);
        emit_jp_label("jp c,", ltrue);
        emit_jp_label("jp", lfalse);
        emit_label(ltrue);
    } else if (op == TOK_EQ) {
        emit_jp_label("jp nz,", lfalse);
    } else if (op == TOK_NE) {
        emit_jp_label("jp z,", lfalse);
    }
}

void emit_cmp_branch_true_unsigned(int op, int ltrue)
{
    int ldone;

    emit("\tor a\n\tsbc hl,de\n");

    if (op == '<') {
        emit_jp_label("jp c,", ltrue);
    } else if (op == TOK_GE) {
        emit_jp_label("jp nc,", ltrue);
    } else if (op == '>') {
        ldone = new_label();
        emit_jp_label("jp z,", ldone);
        emit_jp_label("jp nc,", ltrue);
        emit_label(ldone);
    } else if (op == TOK_LE) {
        emit_jp_label("jp z,", ltrue);
        emit_jp_label("jp c,", ltrue);
    } else if (op == TOK_EQ) {
        emit_jp_label("jp z,", ltrue);
    } else if (op == TOK_NE) {
        emit_jp_label("jp nz,", ltrue);
    }
}


int invert_relop_for_swap(int op)
{
    if (op == '<') return '>';
    if (op == '>') return '<';
    if (op == TOK_LE) return TOK_GE;
    if (op == TOK_GE) return TOK_LE;
    return op;
}



void emit_byte_operand_to_a(struct ByteOperand *op)
{
    if (op->kind == 1) {
        fprintf(outf, "\tld a,(ix%+d)\n", op->sym->offset);
    } else if (op->kind == 2) {
        fprintf(outf, "\tld a,%ld\n", op->val & 255);
    } else if (op->kind == 3) {
        emit_extrn_if_needed(op->sym);
        if (op->idx_sym) {
            fprintf(outf, "\tld hl,%s\n", asm_name_for(sym_asm_name(op->sym)));
            fprintf(outf, "\tld e,(ix%+d)\n", op->idx_sym->offset);
            emit("\tld d,0\n");
            emit("\tadd hl,de\n");
            emit("\tld a,(hl)\n");
        } else {
            fprintf(outf, "\tld a,(%s+%ld)\n", asm_name_for(sym_asm_name(op->sym)), op->val & 0xffffL);
        }
    }
}

void emit_cp_byte_operand(struct ByteOperand *op)
{
    if (op->kind == 1) {
        fprintf(outf, "\tcp (ix%+d)\n", op->sym->offset);
    } else if (op->kind == 2) {
        fprintf(outf, "\tcp %ld\n", op->val & 255);
    } else if (op->kind == 3) {
        emit_extrn_if_needed(op->sym);
        if (op->idx_sym) {
            emit("\tld b,a\n");
            fprintf(outf, "\tld hl,%s\n", asm_name_for(sym_asm_name(op->sym)));
            fprintf(outf, "\tld e,(ix%+d)\n", op->idx_sym->offset);
            emit("\tld d,0\n");
            emit("\tadd hl,de\n");
            emit("\tld a,b\n");
            emit("\tcp (hl)\n");
        } else {
            fprintf(outf, "\tcp (%s+%ld)\n", asm_name_for(sym_asm_name(op->sym)), op->val & 0xffffL);
        }
    }
}

int byte_operand_can_be_lhs(struct ByteOperand *op)
{
    return op->kind == 1 || op->kind == 3;
}

void emit_byte_cmp_branch_after_cp(int op, int label, int branch_when_true)
{
    int ldone;

    if (branch_when_true) {
        if (op == TOK_EQ) {
            emit_jp_label("jp z,", label);
        } else if (op == TOK_NE) {
            emit_jp_label("jp nz,", label);
        } else if (op == '<') {
            emit_jp_label("jp c,", label);
        } else if (op == TOK_GE) {
            emit_jp_label("jp nc,", label);
        } else if (op == '>') {
            ldone = new_label();
            emit_jp_label("jp z,", ldone);
            emit_jp_label("jp nc,", label);
            emit_label(ldone);
        } else if (op == TOK_LE) {
            emit_jp_label("jp z,", label);
            emit_jp_label("jp c,", label);
        }
    } else {
        if (op == TOK_EQ) {
            emit_jp_label("jp nz,", label);
        } else if (op == TOK_NE) {
            emit_jp_label("jp z,", label);
        } else if (op == '<') {
            emit_jp_label("jp nc,", label);
        } else if (op == TOK_GE) {
            emit_jp_label("jp c,", label);
        } else if (op == '>') {
            emit_jp_label("jp z,", label);
            emit_jp_label("jp c,", label);
        } else if (op == TOK_LE) {
            ldone = new_label();
            emit_jp_label("jp z,", ldone);
            emit_jp_label("jp c,", ldone);
            emit_jp_label("jp", label);
            emit_label(ldone);
        }
    }
}

void emit_branch_on_bool_hl(int label, int branch_when_true)
{
    emit("\tld a,h\n\tor l\n");
    if (branch_when_true)
        emit_jp_label("jp nz,", label);
    else
        emit_jp_label("jp z,", label);
}




int emit_cmp_const_branch_for_signed_local16(struct Sym *s, int op, long c,
                                                    int label, int branch_when_true)
{
    int ldone;

    if (!sym_can_ix_direct(s))
        return 0;
    if (type_size(s->type) != 2)
        return 0;
    if (s->type & TYPE_UNSIGNED)
        return 0;
    if (c < 0 || c > 255)
        return 0;

    /*
     * Fast signed 16-bit local/param compare against a small non-negative
     * constant.  This intentionally starts with the hot loop form only:
     *
     *     for (i = 0; i < 20; i++)
     *
     * Signed correctness is preserved:
     *   negative lhs       => lhs < c is true
     *   positive hi != 0   => lhs >= 256, so lhs < c is false
     *   hi == 0            => compare low byte with c
     */
    if (op == TOK_GE) {
        /*
         * var >= c  — only c == 0 is handled here.
         * var >= 0  ↔  var is non-negative  ↔  sign bit clear in hi byte.
         * This catches  for (i = N; 0 <= i; i--)  written as  const <= var.
         */
        if (c != 0)
            return 0;
        if (branch_when_true) {
            /* branch to label when var >= 0 (non-negative) */
            ldone = new_label();
            fprintf(outf, "\tld a,(ix%+d)\n", s->offset + 1);
            emit("\tor a\n");
            emit_jp_label("jp m,", ldone);   /* negative: not >= 0, skip */
            emit_jp_label("jp", label);
            emit_label(ldone);
        } else {
            /* branch to label when var < 0 (negative) */
            fprintf(outf, "\tld a,(ix%+d)\n", s->offset + 1);
            emit("\tor a\n");
            emit_jp_label("jp m,", label);   /* negative: branch */
        }
        return 1;
    }

    if (op != '<')
        return 0;

    if (branch_when_true) {
        ldone = new_label();
        fprintf(outf, "\tld a,(ix%+d)\n", s->offset + 1);
        emit("\tor a\n");
        emit_jp_label("jp m,", label);     /* negative < c */
        emit_jp_label("jp nz,", ldone);    /* positive >= 256 */
        fprintf(outf, "\tld a,(ix%+d)\n", s->offset);
        fprintf(outf, "\tcp %ld\n", c & 255);
        emit_jp_label("jp c,", label);
        emit_label(ldone);
    } else {
        ldone = new_label();
        fprintf(outf, "\tld a,(ix%+d)\n", s->offset + 1);
        emit("\tor a\n");
        emit_jp_label("jp m,", ldone);     /* negative => true, so not false */
        emit_jp_label("jp nz,", label);    /* positive >= 256 => false */
        fprintf(outf, "\tld a,(ix%+d)\n", s->offset);
        fprintf(outf, "\tcp %ld\n", c & 255);
        emit_jp_label("jp nc,", label);
        emit_label(ldone);
    }

    return 1;
}


