#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

struct CpuState {
    uint8_t a, x, y, sp;
    uint16_t pc;
    bool fNegative, fOverflow, fDecimal, fInterruptDisable, fZero, fCarry;
};

#ifdef BMW9_VOLATILE_STATE
static volatile struct CpuState cpu;
#else
struct CpuState cpu;
#endif

#define set_nz(x) cpu.fNegative = ((x) & 0x80), cpu.fZero = !(x)

#ifdef BMW9_FASTCALL_COMPARE
extern void __fastcall op_cmp(uint8_t lhs, uint8_t rhs);
#elif defined(BMW9_ANSI_HELPERS)
static inline void op_cmp(uint8_t lhs, uint8_t rhs)
{
    set_nz((uint8_t)((uint16_t)lhs - (uint16_t)rhs));
    cpu.fCarry = (lhs >= rhs);
}
#else
static inline void op_cmp(lhs, rhs) uint8_t lhs; uint8_t rhs;
{
    set_nz((uint8_t)((uint16_t)lhs - (uint16_t)rhs));
    cpu.fCarry = (lhs >= rhs);
}
#endif

#ifdef BMW9_ANSI_HELPERS
static void op_bcd_math(uint8_t math, uint8_t rhs)
#else
static void op_bcd_math(math, rhs) uint8_t math; uint8_t rhs;
#endif
{
    uint8_t alo, ahi, rlo, rhi, ad, rd, result;

    alo = cpu.a & 0xf;
    ahi = cpu.a >> 4;
    rlo = rhs & 0xf;
    rhi = rhs >> 4;
    cpu.fZero = false;
    if (alo > 9 || ahi > 9 || rlo > 9 || rhi > 9)
        return;
    ad = ahi * 10 + alo;
    rd = rhi * 10 + rlo;
    if (0xe0 == math) {
        if (!cpu.fCarry)
            rd += 1;
        if (ad >= rd) {
            result = ad - rd;
            cpu.fCarry = true;
        } else {
            result = 100 + ad - rd;
            cpu.fCarry = false;
        }
    } else {
        result = ad + rd + cpu.fCarry;
        if (result > 99) {
            result -= 100;
            cpu.fCarry = true;
        } else
            cpu.fCarry = false;
    }
    cpu.a = ((result / 10) << 4) + (result % 10);
}

static void op_math(op, rhs)
#ifdef BMW9_VOLATILE_PARAMS
volatile uint8_t op; volatile uint8_t rhs;
#else
uint8_t op; uint8_t rhs;
#endif
{
    uint16_t res16;
    uint8_t result;
#ifdef BMW9_ALIAS_RHS
    uint8_t *rhs_alias = &rhs;
#endif

    op &= 0xe0;
    if (0xc0 == op) {
        op_cmp(cpu.a, rhs);
        return;
    }
#ifdef BMW9_BRANCH_FORM
    if (cpu.fDecimal) {
        if (0xe0 == op || 0x60 == op) {
            op_bcd_math(op, rhs);
            return;
        }
    }
#else
    if (cpu.fDecimal && (0xe0 == op || 0x60 == op)) {
        op_bcd_math(op, rhs);
        return;
    }
#endif
    if (0xe0 == op) {
#ifdef BMW9_ALIAS_RHS
        *rhs_alias = 255 - *rhs_alias;
#else
        rhs = 255 - rhs;
#endif
        op = 0x60;
    }
    if (0x60 == op) {
        res16 = (uint16_t)cpu.a + (uint16_t)rhs +
                (uint16_t)cpu.fCarry;
        result = (uint8_t)res16;
        cpu.fCarry = (0 != (res16 & 0xff00));
        cpu.fOverflow = (!((cpu.a ^ rhs) & 0x80)) &&
                        ((cpu.a ^ result) & 0x80);
        cpu.a = result;
    } else if (0 == op)
        cpu.a |= rhs;
    else if (0x20 == op)
        cpu.a &= rhs;
    else
        cpu.a ^= rhs;
    set_nz(cpu.a);
#ifdef BMW9_FINAL_RETURN
    return;
#endif
}

static void reference_math(
    struct CpuState *state, uint8_t op, uint8_t rhs)
{
    unsigned int lhs;
    unsigned int wide;
    unsigned int alo, ahi, rlo, rhi, ad, rd, result;

    op &= 0xe0;
    if (op == 0xc0) {
        result = (unsigned int)state->a - rhs;
        state->fNegative = (result & 0x80) != 0;
        state->fZero = ((uint8_t)result == 0);
        state->fCarry = state->a >= rhs;
        return;
    }
    if (state->fDecimal && (op == 0xe0 || op == 0x60)) {
        alo = state->a & 15;
        ahi = state->a >> 4;
        rlo = rhs & 15;
        rhi = rhs >> 4;
        state->fZero = false;
        if (alo > 9 || ahi > 9 || rlo > 9 || rhi > 9)
            return;
        ad = ahi * 10 + alo;
        rd = rhi * 10 + rlo;
        if (op == 0xe0) {
            if (!state->fCarry)
                ++rd;
            if (ad >= rd) {
                result = ad - rd;
                state->fCarry = true;
            } else {
                result = 100 + ad - rd;
                state->fCarry = false;
            }
        } else {
            result = ad + rd + state->fCarry;
            if (result > 99) {
                result -= 100;
                state->fCarry = true;
            } else
                state->fCarry = false;
        }
        state->a = (uint8_t)(((result / 10) << 4) + result % 10);
        return;
    }
    if (op == 0xe0) {
        rhs = (uint8_t)(255 - rhs);
        op = 0x60;
    }
    if (op == 0x60) {
        lhs = state->a;
        wide = lhs + rhs + state->fCarry;
        result = (uint8_t)wide;
        state->fCarry = (wide & 0xff00) != 0;
        state->fOverflow = !((lhs ^ rhs) & 0x80) &&
                           ((lhs ^ result) & 0x80);
        state->a = (uint8_t)result;
    } else if (op == 0)
        state->a |= rhs;
    else if (op == 0x20)
        state->a &= rhs;
    else
        state->a ^= rhs;
    state->fNegative = (state->a & 0x80) != 0;
    state->fZero = state->a == 0;
}

static int same_state(const struct CpuState *right)
{
    return cpu.a == right->a && cpu.x == right->x &&
        cpu.y == right->y && cpu.sp == right->sp &&
        cpu.pc == right->pc &&
        !!cpu.fNegative == !!right->fNegative &&
        !!cpu.fOverflow == !!right->fOverflow &&
        !!cpu.fDecimal == !!right->fDecimal &&
        !!cpu.fInterruptDisable == !!right->fInterruptDisable &&
        !!cpu.fZero == !!right->fZero &&
        !!cpu.fCarry == !!right->fCarry;
}

int main(void)
{
    static uint8_t values[] = { 0, 1, 9, 0x49, 0x7f, 0x80, 0x99, 0xff };
    static uint8_t ops[] = {
        0x00, 0x1f, 0x20, 0x5f, 0x60, 0x7f, 0xc0, 0xff
    };
    struct CpuState expected;
    unsigned long hash = 2166136261UL;
    int oi, ai, ri, carry, decimal;
    int checks = 0;
    int failures = 0;

    for (oi = 0; oi < 8; ++oi)
        for (ai = 0; ai < 8; ++ai)
            for (ri = 0; ri < 8; ++ri)
                for (carry = 0; carry < 2; ++carry)
                    for (decimal = 0; decimal < 2; ++decimal) {
                        cpu.a = values[ai];
                        cpu.x = 0x35;
                        cpu.y = 0xca;
                        cpu.sp = 0x5a;
                        cpu.pc = 0xa55a;
                        cpu.fNegative = (ai & 1) != 0;
                        cpu.fOverflow = (ri & 1) != 0;
                        cpu.fDecimal = decimal != 0;
                        cpu.fInterruptDisable = true;
                        cpu.fZero = (oi & 1) != 0;
                        cpu.fCarry = carry != 0;
                        expected = cpu;
                        reference_math(&expected, ops[oi], values[ri]);
                        op_math(ops[oi], values[ri]);
                        if (!same_state(&expected))
                            ++failures;
                        hash = hash * 33UL + cpu.a;
                        hash = hash * 33UL + !!cpu.fNegative;
                        hash = hash * 33UL + !!cpu.fOverflow;
                        hash = hash * 33UL + !!cpu.fZero;
                        hash = hash * 33UL + !!cpu.fCarry;
                        ++checks;
                    }
    printf("BMW9 oracle checks=%d failures=%d hash=%lu\n",
           checks, failures, hash);
    return failures != 0;
}

#if defined(BMW9_FASTCALL_COMPARE) && defined(_DCC_)
#asm
_op_cmp:
        ld      a,l
        sub     e
        ld      b,a
        rlca
        and     1
        ld      (_cpu+6),a
        ld      a,b
        or      a
        ld      a,0
        jp      nz,bmw9_cmp_nonzero
        inc     a
bmw9_cmp_nonzero:
        ld      (_cpu+10),a
        ld      a,l
        cp      e
        ld      a,0
        jp      c,bmw9_cmp_borrow
        inc     a
bmw9_cmp_borrow:
        ld      (_cpu+11),a
        ret
#endasm
#endif
