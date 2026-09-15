#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#if defined(MIR_CLOBBER_BYTE_MATH_COMPARE_VARIADIC) || \
    defined(MIR_CLOBBER_BYTE_MATH_DECIMAL_VARIADIC)
#include <stdarg.h>
#endif

struct CpuState {
    uint8_t a, x, y, sp;
    uint16_t pc;
    bool fNegative, fOverflow, fDecimal, fInterruptDisable, fZero, fCarry;
};

static struct CpuState cpu;

#define set_nz(x) cpu.fNegative = ((x) & 0x80), cpu.fZero = !(x)

#ifdef MIR_CLOBBER_BYTE_MATH_COMPARE_VARIADIC
static inline void op_cmp(uint8_t lhs, ...)
{
    va_list args;
    uint8_t rhs;

    va_start(args, lhs);
    rhs = (uint8_t)va_arg(args, int);
    va_end(args);
    set_nz((uint8_t)((uint16_t)lhs - (uint16_t)rhs));
    cpu.fCarry = (lhs >= rhs);
}
#elif defined(MIR_CLOBBER_BYTE_MATH_ANSI_HELPERS)
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

#ifdef MIR_CLOBBER_BYTE_MATH_DECIMAL_VARIADIC
static void op_bcd_math(uint8_t math, ...)
#elif defined(MIR_CLOBBER_BYTE_MATH_ANSI_HELPERS)
static void op_bcd_math(uint8_t math, uint8_t rhs)
#else
static void op_bcd_math(math, rhs) uint8_t math; uint8_t rhs;
#endif
{
    uint8_t alo, ahi, rlo, rhi, ad, rd, result;
#ifdef MIR_CLOBBER_BYTE_MATH_DECIMAL_VARIADIC
    va_list args;
    uint8_t rhs;

    va_start(args, math);
    rhs = (uint8_t)va_arg(args, int);
    va_end(args);
#endif

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

#ifdef MIR_CLOBBER_BYTE_MATH_RETURN_INT
#define BYTE_MATH_RETURN_TYPE int
#define BYTE_MATH_RETURN(value) return (value)
#define BYTE_MATH_FINISH(value) return (value)
#else
#define BYTE_MATH_RETURN_TYPE void
#define BYTE_MATH_RETURN(value) return
#define BYTE_MATH_FINISH(value)
#endif

#ifdef MIR_CLOBBER_BYTE_MATH_COMPARE_INDIRECT
static void (*byte_math_compare)(uint8_t lhs, uint8_t rhs) = op_cmp;
#endif

#ifdef MIR_CLOBBER_BYTE_MATH_DECIMAL_INDIRECT
static void (*byte_math_decimal)(uint8_t math, uint8_t rhs) = op_bcd_math;
#endif

static BYTE_MATH_RETURN_TYPE op_math(op, rhs) uint8_t op; uint8_t rhs;
{
    uint16_t res16;
    uint8_t result;
#ifdef MIR_CLOBBER_BYTE_MATH_VLA
    int byte_math_vla_extent = 2 + (op == 0xff && rhs == 0xff);
    uint8_t byte_math_vla_scratch[byte_math_vla_extent];
    int byte_math_vla_index;
#endif

#ifdef MIR_CLOBBER_BYTE_MATH_VLA
    for (byte_math_vla_index = 0;
         byte_math_vla_index < byte_math_vla_extent;
         ++byte_math_vla_index)
        byte_math_vla_scratch[byte_math_vla_index] = 0;
    if (byte_math_vla_scratch[byte_math_vla_extent - 1])
        cpu.a ^= byte_math_vla_scratch[0];
#endif

#ifdef MIR_CLOBBER_BYTE_MATH_MASK
    op &= 0xf0;
#else
    op &= 0xe0;
#endif
#ifdef MIR_CLOBBER_BYTE_MATH_OPCODE
    if (0xa0 == op) {
#else
    if (0xc0 == op) {
#endif
#ifdef MIR_CLOBBER_BYTE_MATH_SWAP
        op_cmp(rhs, cpu.a);
#elif defined(MIR_CLOBBER_BYTE_MATH_COMPARE_INDIRECT)
        byte_math_compare(cpu.a, rhs);
#else
        op_cmp(cpu.a, rhs);
#endif
        BYTE_MATH_RETURN(0);
    }
    if (cpu.fDecimal && (0xe0 == op || 0x60 == op)) {
#ifdef MIR_CLOBBER_BYTE_MATH_DECIMAL_INDIRECT
        byte_math_decimal(op, rhs);
#else
        op_bcd_math(op, rhs);
#endif
        BYTE_MATH_RETURN(0);
    }
    if (0xe0 == op) {
#ifdef MIR_CLOBBER_BYTE_MATH_COMPLEMENT
        rhs = 511 - rhs;
#else
        rhs = 255 - rhs;
#endif
        op = 0x60;
    }
    if (0x60 == op) {
#ifdef MIR_CLOBBER_BYTE_MATH_ADD_ORDER
        res16 = (uint16_t)rhs + (uint16_t)cpu.a +
                (uint16_t)cpu.fCarry;
#else
        res16 = (uint16_t)cpu.a + (uint16_t)rhs +
                (uint16_t)cpu.fCarry;
#endif
        result = (uint8_t)res16;
        cpu.fCarry = (0 != (res16 & 0xff00));
#ifdef MIR_CLOBBER_BYTE_MATH_OVERFLOW_ORDER
        cpu.fOverflow = (!((rhs ^ cpu.a) & 0x80)) &&
                        ((result ^ cpu.a) & 0x80);
#else
        cpu.fOverflow = (!((cpu.a ^ rhs) & 0x80)) &&
                        ((cpu.a ^ result) & 0x80);
#endif
        cpu.a = result;
#ifdef MIR_CLOBBER_BYTE_MATH_LOGIC_ORDER
    } else if (0x20 == op)
        cpu.a &= rhs;
    else if (0 == op)
        cpu.a |= rhs;
#else
    } else if (0 == op)
        cpu.a |= rhs;
    else if (0x20 == op)
        cpu.a &= rhs;
#endif
    else
        cpu.a ^= rhs;
    set_nz(cpu.a);
    BYTE_MATH_FINISH(0);
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
#ifdef MIR_CLOBBER_BYTE_MATH_OPCODE
    op_math(0xa0, 5);
#else
    op_math(0xc0, 5);
#endif
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
