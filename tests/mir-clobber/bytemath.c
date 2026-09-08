#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

struct CpuState {
    uint8_t a, x, y, sp;
    uint16_t pc;
    bool fNegative, fOverflow, fDecimal, fInterruptDisable, fZero, fCarry;
};

static struct CpuState cpu;

#define set_nz(x) cpu.fNegative = ((x) & 0x80), cpu.fZero = !(x)

static inline void op_cmp(lhs, rhs) uint8_t lhs; uint8_t rhs;
{
    set_nz((uint8_t)((uint16_t)lhs - (uint16_t)rhs));
    cpu.fCarry = (lhs >= rhs);
}

static void op_bcd_math(math, rhs) uint8_t math; uint8_t rhs;
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

static void op_math(op, rhs) uint8_t op; uint8_t rhs;
{
    uint16_t res16;
    uint8_t result;

    op &= 0xe0;
    if (0xc0 == op) {
#ifdef MIR_CLOBBER_BYTE_MATH_SWAP
        op_cmp(rhs, cpu.a);
#else
        op_cmp(cpu.a, rhs);
#endif
        return;
    }
    if (cpu.fDecimal && (0xe0 == op || 0x60 == op)) {
        op_bcd_math(op, rhs);
        return;
    }
    if (0xe0 == op) {
        rhs = 255 - rhs;
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
}

static int check(
    const char *name, int a, int negative, int zero, int carry)
{
    if (cpu.a == a && !!cpu.fNegative == negative &&
        !!cpu.fZero == zero && !!cpu.fCarry == carry)
        return 0;
    printf("%s failed a=%u n=%u z=%u c=%u\n", name, cpu.a,
           !!cpu.fNegative, !!cpu.fZero, !!cpu.fCarry);
    return 1;
}

int main(void)
{
    int failures = 0;

    cpu.a = 0x40; cpu.fCarry = false; cpu.fDecimal = false;
    op_math(0x00, 0x02);
    failures += check("or", 0x42, 0, 0, 0);
    op_math(0x20, 0x02);
    failures += check("and", 0x02, 0, 0, 0);
    op_math(0x40, 0x82);
    failures += check("xor", 0x80, 1, 0, 0);

    cpu.a = 0xff; cpu.fCarry = true;
    op_math(0x60, 0);
    failures += check("adc", 0, 0, 1, 1);
    cpu.a = 5; cpu.fCarry = true;
    op_math(0xe0, 3);
    failures += check("sbc", 2, 0, 0, 1);

    cpu.a = 7; cpu.fCarry = false;
    op_math(0xc0, 5);
#ifdef MIR_CLOBBER_BYTE_MATH_SWAP
    failures += check("cmp", 7, 1, 0, 0);
#else
    failures += check("cmp", 7, 0, 0, 1);
#endif

    cpu.a = 0x49; cpu.fCarry = true; cpu.fDecimal = true;
    op_math(0x60, 0x50);
#ifdef MIR_CLOBBER_BYTE_MATH_SWAP
    failures += check("bcd", 0, 1, 0, 1);
#else
    failures += check("bcd", 0, 0, 0, 1);
#endif
    printf("byte math failures=%d\n", failures);
    return failures != 0;
}
