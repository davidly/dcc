/**
 * @file dcc_cmp.c
 * @brief Implements shared Z80 comparison and conditional-branch primitives.
 *
 * @par Role
 * Materializes 16-bit comparison results, emits signed and unsigned branches,
 * supports direct byte operands, and provides small-constant local-variable
 * branch fast paths.
 *
 * @par Key entry points
 * gen_cmp_typed(), emit_cmp_branch_true(), emit_cmp_branch_false(),
 * emit_byte_cmp_branch_after_cp(), and
 * emit_cmp_const_branch_for_signed_local16().
 *
 * @par Boundary
 * Callers establish operand types and registers. This module supplies target
 * primitives but does not parse expressions or select production body
 * candidates; final function bodies are MIR-selected.
 */

#include "dcc.h"

/* Shared 16-bit compare tail used by gen_cmp / gen_cmp_typed after any signed
 * bias has been applied: subtract, branch to a private true-label per relop,
 * then materialize 0/1 into HL. Label allocation happens here (lt/le, plus the
 * extra false label the `>` case needs) exactly as the two callers did inline,
 * so the emitted text and label numbering are unchanged. */
static void emit_cmp_subtract_to_bool(int op)
{
    int lt;
    int le;

    lt = new_label();
    le = new_label();

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

void gen_cmp(int op)
{
    /* gen_cmp always compares as signed, so the bias applies whenever the
     * relop is order-sensitive (emit_signed_bias_for_relop filters on op). */
    emit_signed_bias_for_relop(op);
    emit_cmp_subtract_to_bool(op);
}

void gen_cmp_typed(int op, int lhs_type)
{
    if (!(lhs_type & TYPE_UNSIGNED))
        emit_signed_bias_for_relop(op);
    emit_cmp_subtract_to_bool(op);
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
        fprintf(g_emit_sink.stream, "\tld a,(ix%+d)\n", op->sym->offset);
    } else if (op->kind == 2) {
        fprintf(g_emit_sink.stream, "\tld a,%ld\n", op->val & 255);
    } else if (op->kind == 3) {
        emit_extrn_if_needed(op->sym);
        if (op->idx_sym) {
            fprintf(g_emit_sink.stream, "\tld hl,%s\n", asm_name_for(sym_asm_name(op->sym)));
            fprintf(g_emit_sink.stream, "\tld e,(ix%+d)\n", op->idx_sym->offset);
            emit("\tld d,0\n");
            emit("\tadd hl,de\n");
            emit("\tld a,(hl)\n");
        } else {
            fprintf(g_emit_sink.stream, "\tld a,(%s+%ld)\n", asm_name_for(sym_asm_name(op->sym)), op->val & 0xffffL);
        }
    } else if (op->kind == 4) {
        fprintf(g_emit_sink.stream, "\tld l,(ix%+d)\n", op->sym->offset);
        fprintf(g_emit_sink.stream, "\tld h,(ix%+d)\n", op->sym->offset + 1);
        emit("\tld a,(hl)\n");
    } else if (op->kind == 5) {
        fprintf(g_emit_sink.stream, "\tld l,(ix%+d)\n", op->sym->offset);
        fprintf(g_emit_sink.stream, "\tld h,(ix%+d)\n", op->sym->offset + 1);
        if (op->idx_sym) {
            fprintf(g_emit_sink.stream, "\tld e,(ix%+d)\n", op->idx_sym->offset);
            if (type_size(op->idx_sym->type) == 1)
                emit("\tld d,0\n");
            else
                fprintf(g_emit_sink.stream, "\tld d,(ix%+d)\n", op->idx_sym->offset + 1);
            emit("\tadd hl,de\n");
        } else if (op->val != 0) {
            fprintf(g_emit_sink.stream, "\tld de,%ld\n", op->val & 0xffffL);
            emit("\tadd hl,de\n");
        }
        emit("\tld a,(hl)\n");
    } else if (op->kind == 6) {
        fprintf(g_emit_sink.stream, "\tld a,(ix%+d)\n", op->sym->offset);
        if (op->idx_sym)
            fprintf(g_emit_sink.stream, "\tadd a,(ix%+d)\n", op->idx_sym->offset);
        if ((op->val & 255L) != 0)
            fprintf(g_emit_sink.stream, "\tadd a,%ld\n", op->val & 255L);
    }
}

void emit_cp_byte_operand(struct ByteOperand *op)
{
    if (op->kind == 1) {
        fprintf(g_emit_sink.stream, "\tcp (ix%+d)\n", op->sym->offset);
    } else if (op->kind == 2) {
        fprintf(g_emit_sink.stream, "\tcp %ld\n", op->val & 255);
    } else if (op->kind == 3) {
        emit_extrn_if_needed(op->sym);
        if (op->idx_sym) {
            /* Like kind 4 below: the hl/de address math here never touches
             * A, so the value already loaded there (the LHS) survives
             * untouched with no save/restore needed. */
            fprintf(g_emit_sink.stream, "\tld hl,%s\n", asm_name_for(sym_asm_name(op->sym)));
            fprintf(g_emit_sink.stream, "\tld e,(ix%+d)\n", op->idx_sym->offset);
            emit("\tld d,0\n");
            emit("\tadd hl,de\n");
            emit("\tcp (hl)\n");
        } else {
            fprintf(g_emit_sink.stream, "\tcp (%s+%ld)\n", asm_name_for(sym_asm_name(op->sym)), op->val & 0xffffL);
        }
    } else if (op->kind == 4) {
        /* Unlike kind 3 (which needs the hl,de address math to reach an
         * indexed global), loading the pointer's own value via ix-direct
         * addressing touches only L and H, so A (already holding the LHS
         * value) survives untouched with no save/restore needed. */
        fprintf(g_emit_sink.stream, "\tld l,(ix%+d)\n", op->sym->offset);
        fprintf(g_emit_sink.stream, "\tld h,(ix%+d)\n", op->sym->offset + 1);
        emit("\tcp (hl)\n");
    } else if (op->kind == 5) {
        /* Like kind 4: this address math (e/d/hl only) never touches A, so
         * the LHS value already loaded there survives with no save/restore
         * needed. */
        fprintf(g_emit_sink.stream, "\tld l,(ix%+d)\n", op->sym->offset);
        fprintf(g_emit_sink.stream, "\tld h,(ix%+d)\n", op->sym->offset + 1);
        if (op->idx_sym) {
            fprintf(g_emit_sink.stream, "\tld e,(ix%+d)\n", op->idx_sym->offset);
            if (type_size(op->idx_sym->type) == 1)
                emit("\tld d,0\n");
            else
                fprintf(g_emit_sink.stream, "\tld d,(ix%+d)\n", op->idx_sym->offset + 1);
            emit("\tadd hl,de\n");
        } else if (op->val != 0) {
            fprintf(g_emit_sink.stream, "\tld de,%ld\n", op->val & 0xffffL);
            emit("\tadd hl,de\n");
        }
        emit("\tcp (hl)\n");
    } else if (op->kind == 6) {
        emit("\tld b,a\n");
        fprintf(g_emit_sink.stream, "\tld a,(ix%+d)\n", op->sym->offset);
        if (op->idx_sym)
            fprintf(g_emit_sink.stream, "\tadd a,(ix%+d)\n", op->idx_sym->offset);
        if ((op->val & 255L) != 0)
            fprintf(g_emit_sink.stream, "\tadd a,%ld\n", op->val & 255L);
        emit("\tld c,a\n\tld a,b\n\tcp c\n");
    }
}

int byte_operand_can_be_lhs(struct ByteOperand *op)
{
    return op->kind == 1 || op->kind == 3 || op->kind == 4 || op->kind == 5 || op->kind == 6;
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
            fprintf(g_emit_sink.stream, "\tld a,(ix%+d)\n", s->offset + 1);
            emit("\tor a\n");
            emit_jp_label("jp m,", ldone);   /* negative: not >= 0, skip */
            emit_jp_label("jp", label);
            emit_label(ldone);
        } else {
            /* branch to label when var < 0 (negative) */
            fprintf(g_emit_sink.stream, "\tld a,(ix%+d)\n", s->offset + 1);
            emit("\tor a\n");
            emit_jp_label("jp m,", label);   /* negative: branch */
        }
        return 1;
    }

    if (op != '<')
        return 0;

    if (branch_when_true) {
        ldone = new_label();
        fprintf(g_emit_sink.stream, "\tld a,(ix%+d)\n", s->offset + 1);
        emit("\tor a\n");
        emit_jp_label("jp m,", label);     /* negative < c */
        emit_jp_label("jp nz,", ldone);    /* positive >= 256 */
        fprintf(g_emit_sink.stream, "\tld a,(ix%+d)\n", s->offset);
        fprintf(g_emit_sink.stream, "\tcp %ld\n", c & 255);
        emit_jp_label("jp c,", label);
        emit_label(ldone);
    } else {
        ldone = new_label();
        fprintf(g_emit_sink.stream, "\tld a,(ix%+d)\n", s->offset + 1);
        emit("\tor a\n");
        emit_jp_label("jp m,", ldone);     /* negative => true, so not false */
        emit_jp_label("jp nz,", label);    /* positive >= 256 => false */
        fprintf(g_emit_sink.stream, "\tld a,(ix%+d)\n", s->offset);
        fprintf(g_emit_sink.stream, "\tcp %ld\n", c & 255);
        emit_jp_label("jp nc,", label);
        emit_label(ldone);
    }

    return 1;
}

