/*
 * tfdf.c - regression coverage for dccrtl.mac's __fdf, the fastcall
 * division variant (a via one stack push, b live in DE:HL, computes
 * a/b) added to match __faf/__fsf/__fmf. Not reachable from portable C
 * (no C source calls it directly), so exercised via #asm the same way
 * tfpraw.c tests other internal-only primitives.
 *
 * __fdf's first implementation attempt mirrored __fmf's exact call
 * order (unpack the live operand via FUNB immediately, unpack the
 * stack operand via FUNA second) and silently computed b/a instead of
 * a/b: the shared division loop (FDBODY onward) has an undocumented
 * register-level dependency beyond the AEXP, BEXP, and mantissa-byte
 * memory state - whichever of FUNA/FUNB runs LAST leaves its own B register holding
 * that operand's mantissa byte, read directly by the loop rather than
 * reloaded from memory. Multiply has no such dependency, so __fmf's
 * order is fine there; division's own __fdiv always calls FUNA then
 * FUNB, and __fdf must preserve that same order (via the shadow
 * register bank, not an extra stack push, to keep the fastcall
 * benefit) to get the right answer. Several asymmetric a/b pairs below
 * (20/5 vs 5/20, etc.) guard against a regression back to that bug.
 */
#include <stdio.h>
#include <math.h>

extern long fdf_test(long a_bits, long b_bits);

#asm
    extrn __fdf
    public _fdf_test
_fdf_test:
    push ix
    ld ix,0
    add ix,sp
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
    call __fdf
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
        printf("FAIL %s: got=%f want=%f\n", name, got, want);
    }
}
static void okb(const char *name, int got, int want)
{
    checks++;
    if ((got!=0)!=(want!=0)) { failures++; printf("FAIL %s: got %d want %d\n", name, got, want); }
}

static float fdf(float a, float b)
{
    union { float f; long l; } ua, ub, ur;
    ua.f = a; ub.f = b;
    ur.l = fdf_test(ua.l, ub.l);
    return ur.f;
}

int main(void)
{
    float pinf = INFINITY, ninf = -INFINITY, nan1 = NAN;

    okf("20/5", fdf(20.0f, 5.0f), 4.0f);
    okf("5/20", fdf(5.0f, 20.0f), 0.25f);
    okf("7/2", fdf(7.0f, 2.0f), 3.5f);
    okf("-7/2", fdf(-7.0f, 2.0f), -3.5f);
    okf("7/-2", fdf(7.0f, -2.0f), -3.5f);
    okf("-7/-2", fdf(-7.0f, -2.0f), 3.5f);
    okf("100/3", fdf(100.0f, 3.0f), 33.333332f);
    okb("1/1000000 close", fabsf(fdf(1.0f, 1000000.0f) - 0.000001f) < 0.0000001f, 1);

    okb("5/0 isinf", isinf(fdf(5.0f, 0.0f)), 1);
    okb("0/0 isnan", isnan(fdf(0.0f, 0.0f)), 1);
    okb("inf/inf isnan", isnan(fdf(pinf, pinf)), 1);
    okb("inf/5 isinf", isinf(fdf(pinf, 5.0f)), 1);
    okb("5/inf iszero", fdf(5.0f, pinf) == 0.0f, 1);
    okb("nan/5 isnan", isnan(fdf(nan1, 5.0f)), 1);
    okb("5/nan isnan", isnan(fdf(5.0f, nan1)), 1);

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
