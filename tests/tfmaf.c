/*
 * tfmaf.c - direct __fmaf coverage via raw bit patterns. __fmaf is the
 * fastcall entry point added alongside __fmadd (b arrives live in DE:HL,
 * only a and c are pushed - 2 pushes instead of 3, mirroring __faf/__fsf's
 * relationship to __fadd), sharing __fmadd's entire body via a jump to
 * FMADD_UNPACKED once unpacked. The two entry points disagree on which ix
 * offset holds 'c' (ix+12 for __fmadd's 3-push cdecl frame, ix+8 for
 * __fmaf's 2-push fastcall frame - an ix-shift trick to reconcile them was
 * tried and reverted, since it breaks the shared FARET epilogue's "ld
 * sp,ix" unwind), resolved at runtime via the FMAFAST flag each entry
 * point's own prologue sets/clears. This file's job is exercising that
 * flag path specifically: same values as tests/tfmaddr.c (so results are
 * directly comparable - __fmaf must agree with __fmadd on every one, since
 * both compute the same thing), plus an alternating __fmadd/__fmaf/__fmadd
 * sequence guarding against FMAFAST leaking stale state between calls.
 */
#include <stdio.h>

extern long fmaf_test(long a_bits, long b_bits, long c_bits);
extern long fmadd_test(long a_bits, long b_bits, long c_bits);

#asm
    extrn __fmaf
    extrn __fmadd
    public _fmaf_test
_fmaf_test:
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
    call __fmaf
    pop bc
    pop bc
    pop bc
    pop bc
    ld sp,ix
    pop ix
    ret

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

static void okb(const char *name, int got, int want)
{
    checks++;
    if ((got != 0) != (want != 0)) {
        failures++;
        printf("FAIL %s: got %d want %d\n", name, got, want);
    }
}

static float fmaf_(float a, float b, float c)
{
    union { float f; long l; } ua, ub, uc, ur;
    ua.f = a; ub.f = b; uc.f = c;
    ur.l = fmaf_test(ua.l, ub.l, uc.l);
    return ur.f;
}

static float fmadd_(float a, float b, float c)
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

static int is_nan_bits(float f)
{
    unsigned long b = *(unsigned long *)&f;
    return ((b >> 23) & 0xFF) == 0xFF && (b & 0x7FFFFF) != 0;
}

int main(void)
{
    /* Same cases as tests/tfmaddr.c, via __fmaf instead of __fmadd - must
     * agree bit-for-bit, since both compute the same fused product. */
    okf("fast_both_zero", fmaf_(1.5f, 1.25f, 1.0f), bits(0x40380000UL));
    okf("general_4term", fmaf_(bits(0x40490fdbUL), bits(0x402df854UL), 1.0f),
        bits(0x4118a2c0UL));
    okf("a_nz_b_z", fmaf_(bits(0x40490fdbUL), 1.5f, 1.0f), bits(0x40b6cbe4UL));
    okf("a_z_b_nz", fmaf_(1.5f, bits(0x40490fdbUL), 1.0f), bits(0x40b6cbe4UL));
    okf("neg_general", fmaf_(bits(0xc0490fdbUL), bits(0x402df854UL), -1.0f),
        bits(0xc118a2c0UL));
    okf("asym_swap1", fmaf_(bits(0x40e00001UL), bits(0x3dcccd02UL), 2.0f),
        bits(0x402cccd9UL));
    okf("asym_swap2", fmaf_(bits(0x3dcccd02UL), bits(0x40e00001UL), 2.0f),
        bits(0x402cccd9UL));
    okf("zero_product", fmaf_(0.0f, bits(0x40490fdbUL), 5.0f), 5.0f);
    okf("zero_addend", fmaf_(bits(0x40490fdbUL), bits(0x402df854UL), 0.0f),
        bits(0x4108a2c0UL));

    /* Inf/NaN dispatch, reached via FQ_ASPEC/FQ_BSPEC before FQBRIDGE_R -
     * exercises the flag on a path that never reaches the c-fetch branch
     * that differs between entry points, confirming FMAFAST doesn't need
     * to be checked (or cause trouble) there either. */
    okf("inf_times_finite", fmaf_(bits(0x7f800000UL), 2.0f, 1.0f),
        bits(0x7f800000UL));
    okb("zero_times_inf_is_nan",
        is_nan_bits(fmaf_(0.0f, bits(0x7f800000UL), 1.0f)), 1);

    /* Alternating __fmadd/__fmaf/__fmadd - guards against FMAFAST leaking
     * stale state from one entry point's prologue into the other's
     * FQBRIDGE_R fetch on a subsequent, differently-entered call. */
    okf("alt1_fmadd", fmadd_(2.0f, 3.0f, 4.0f), 10.0f);
    okf("alt2_fmaf", fmaf_(bits(0x40490fdbUL), 1.5f, 1.0f), bits(0x40b6cbe4UL));
    okf("alt3_fmadd", fmadd_(bits(0x40490fdbUL), 1.5f, 1.0f),
        bits(0x40b6cbe4UL));
    okf("alt4_fmaf", fmaf_(2.0f, 3.0f, 4.0f), 10.0f);

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
