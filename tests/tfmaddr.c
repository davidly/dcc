/*
 * tfmaddr.c - direct __fmadd coverage via raw bit patterns, targeting the
 * general-case fallback (FQFB1/FQANBZ/FQFB2) that was reworked from a
 * memory-scratch (AM0-2/BM0-2) design to a fully register-resident one,
 * mirroring __fmul's own FMBODY redesign. tfmadd.c already covers __fmadd
 * through ordinary C codegen, but its mostly-small-integer operands hit the
 * fast path (both operands' low 16 mantissa bits zero) almost every time and
 * barely touch the fallback this file targets.
 *
 * Cases below are chosen (see mantissa hi/lo annotations) to hit each of the
 * fallback's four sub-paths: both-zero (fast path, kept as a baseline),
 * both-nonzero (the full four-term product), a-nonzero/b-zero, and
 * a-zero/b-nonzero - plus a pair of a/b-swapped asymmetric cases, in the
 * spirit of tfdf.c's asymmetric coverage, to guard against an operand mixup
 * in the register reshuffle. Expected values were computed independently in
 * Python via exact Fraction arithmetic (a single correctly-rounded
 * fused multiply-add, not two separately-rounded float32 operations).
 */
#include <stdio.h>
#include <math.h>

extern long fmadd_test(long a_bits, long b_bits, long c_bits);

#asm
    extrn __fmadd
    public _fmadd_test
_fmadd_test:
    push ix
    ld ix,0
    add ix,sp
    ld l,(ix+12)
    ld h,(ix+13)
    ld e,(ix+14)
    ld d,(ix+15)
    push de
    push hl
    ld l,(ix+4)
    ld h,(ix+5)
    ld e,(ix+6)
    ld d,(ix+7)
    push de
    push hl
    ld l,(ix+8)
    ld h,(ix+9)
    ld e,(ix+10)
    ld d,(ix+11)
    push de
    push hl
    call __fmadd
    pop bc
    pop bc
    pop bc
    pop bc
    pop bc
    pop bc
    ld sp,ix
    pop ix
    ret
#endasm

static int checks = 0;
static int failures = 0;

static void okf(const char *name, float got, float want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s: got=%08lX want=%08lX\n", name,
               *(unsigned long *)&got, *(unsigned long *)&want);
    }
}

static float fmaddr(float a, float b, float c)
{
    union { float f; long l; } ua, ub, uc, ur;
    ua.f = a; ub.f = b; uc.f = c;
    ur.l = fmadd_test(ua.l, ub.l, uc.l);
    return ur.f;
}

static float bits(unsigned long b)
{
    union { float f; unsigned long l; } u;
    u.l = b;
    return u.f;
}

int main(void)
{
    /* fast path: Ahi=0xc0,Alo=0x0000; Bhi=0xa0,Blo=0x0000 */
    okf("fast_both_zero", fmaddr(1.5f, 1.25f, 1.0f), bits(0x40380000UL));

    /* general 4-term: Ahi=0xc9,Alo=0x0fdb; Bhi=0xad,Blo=0xf854 */
    okf("general_4term", fmaddr(bits(0x40490fdbUL), bits(0x402df854UL), 1.0f),
        bits(0x4118a2c0UL));

    /* Alo!=0, Blo==0 (FQANBZ) */
    okf("a_nz_b_z", fmaddr(bits(0x40490fdbUL), 1.5f, 1.0f), bits(0x40b6cbe4UL));

    /* Alo==0, Blo!=0 (FQFB2) */
    okf("a_z_b_nz", fmaddr(1.5f, bits(0x40490fdbUL), 1.0f), bits(0x40b6cbe4UL));

    /* negative operands through the general path */
    okf("neg_general", fmaddr(bits(0xc0490fdbUL), bits(0x402df854UL), -1.0f),
        bits(0xc118a2c0UL));

    /* asymmetric a/b, swapped both ways - guards against an operand mixup.
     * Expected value is the independently computed, correctly rounded
     * single-rounding result. */
    okf("asym_swap1", fmaddr(bits(0x40e00001UL), bits(0x3dcccd02UL), 2.0f),
        bits(0x402cccd9UL));
    okf("asym_swap2", fmaddr(bits(0x3dcccd02UL), bits(0x40e00001UL), 2.0f),
        bits(0x402cccd9UL));

    /* zero product (a==0) */
    okf("zero_product", fmaddr(0.0f, bits(0x40490fdbUL), 5.0f), 5.0f);

    /* zero addend, general path */
    okf("zero_addend", fmaddr(bits(0x40490fdbUL), bits(0x402df854UL), 0.0f),
        bits(0x4108a2c0UL));

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
