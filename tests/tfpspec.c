/*
 * tfpspec.c - IEEE-754 invalid-operation and special-value regression
 * coverage for dccrtl.mac's __fadd/__fsub/__fmul/__fdiv (and their
 * fastcall siblings __faf/__fsf/__fmf), plus __fmadd (dcc's automatic
 * "addend + a*b" fusion).
 *
 * 0/0, Inf-Inf (and Inf+(-Inf)), and Inf*0 are all "invalid operation"
 * per IEEE-754 and must produce NaN, not garbage or a silently wrong
 * finite/zero value. Also covers the other exponent-255 combinations
 * (Inf+/-finite, Inf*finite, finite/Inf, Inf/Inf, Inf/finite) that share
 * the same detection code path in dccrtl.mac and NaN propagation through
 * each operator; sign-of-zero preservation in __fmul/__fdiv's zero
 * results; ordinary finite-finite addition overflowing past FLT_MAX
 * (must clamp to a signed Infinity, not land on a NaN-shaped bit pattern
 * via unchecked 8-bit exponent-byte wraparound); and __fmadd's own
 * multiply half, a hand-optimized register-resident duplicate of
 * __fmul's logic that needs (and, as of this test, has) the identical
 * Inf/NaN/signed-zero fixes applied independently.
 */
#include <stdio.h>
#include <math.h>
#include <float.h>

static int checks = 0;
static int failures = 0;

static void okb(const char *name, int got, int want)
{
    checks++;
    if ((got != 0) != (want != 0)) {
        failures++;
        printf("FAIL %s: got %d want %d\n", name, got, want);
    }
}

static void okf(const char *name, float got, float want)
{
    int ok;
    checks++;
    if (isnan(want)) {
        ok = isnan(got);
    } else if (isinf(want)) {
        ok = isinf(got) && (signbit(got) == signbit(want));
    } else {
        ok = (got == want) && (signbit(got) == signbit(want) || got != 0.0f);
    }
    if (!ok) {
        failures++;
        printf("FAIL %s: got=%.6f isnan=%d isinf=%d sign=%d\n",
               name, got, isnan(got), isinf(got), signbit(got));
    }
}

/* Kept in its own function (not inlined at each call site) so the
 * "addend + x*y" shape dcc's optimizer fuses into a single __fmadd call
 * is unambiguous and reused identically across every check below. */
static float madd(float addend, float x, float y) { return addend + x * y; }

int main(void)
{
    float zero = 0.0f, negzero = -0.0f;
    float five = 5.0f, negfive = -5.0f, three = 3.0f;
    float pinf = INFINITY, ninf = -INFINITY, nan1 = NAN;

    /* division: the three IEEE invalid-operation cases plus propagation */
    okb("0/0 isnan", isnan(zero / zero), 1);
    okb("inf/inf isnan", isnan(pinf / pinf), 1);
    okb("inf/-inf isnan", isnan(pinf / ninf), 1);
    okf("5/0", five / zero, pinf);
    okf("-5/0", negfive / zero, ninf);
    okf("inf/5", pinf / five, pinf);
    okf("5/inf", five / pinf, zero);
    okf("0/inf", zero / pinf, zero);
    okf("nan/5", nan1 / five, nan1);
    okf("5/1", five / 1.0f, five);
    okf("10/4", 10.0f / 4.0f, 2.5f);

    /* addition/subtraction: Inf+(-Inf) and Inf-Inf, plus propagation */
    okb("inf+-inf isnan", isnan(pinf + ninf), 1);
    okb("inf-inf isnan", isnan(pinf - pinf), 1);
    okf("inf+inf", pinf + pinf, pinf);
    okf("-inf+-inf", ninf + ninf, ninf);
    okf("inf-(-inf)", pinf - ninf, pinf);
    okf("inf+5", pinf + five, pinf);
    okf("5-inf", five - pinf, ninf);
    okf("nan+5", nan1 + five, nan1);
    okf("5+3", five + three, 8.0f);
    okf("5-3", five - three, 2.0f);
    okf("5+(-5)", five + negfive, zero);

    /* multiplication: 0*Inf (both operand orders), plus propagation */
    okb("inf*0 isnan", isnan(pinf * zero), 1);
    okb("0*inf isnan", isnan(zero * pinf), 1);
    okb("-inf*0 isnan", isnan(ninf * zero), 1);
    okf("inf*inf", pinf * pinf, pinf);
    okf("inf*-inf", pinf * ninf, ninf);
    okf("inf*5", pinf * five, pinf);
    okf("inf*-5", pinf * negfive, ninf);
    okf("nan*5", nan1 * five, nan1);
    okf("5*3", five * three, 15.0f);
    okf("0*5", zero * five, zero);

    /* sign-of-zero: __fmul/__fdiv's zero results must carry sign=A^B,
     * not always +0 (a pre-existing gap fixed alongside the Inf/NaN work
     * above, sharing the same FMZERO/FDZRO exit points). */
    okb("0*5 sign", signbit(zero * five), 0);
    okb("0*-5 sign", signbit(zero * negfive), 1);
    okb("-0*5 sign", signbit(negzero * five), 1);
    okb("-0*-5 sign", signbit(negzero * negfive), 0);
    okb("0/5 sign", signbit(zero / five), 0);
    okb("0/-5 sign", signbit(zero / negfive), 1);
    okb("-0/5 sign", signbit(negzero / five), 1);
    okb("5/inf sign", signbit(five / pinf), 0);
    okb("-5/inf sign", signbit(negfive / pinf), 1);
    okb("5/-inf sign", signbit(five / ninf), 1);

    /* ordinary finite-finite addition overflowing past FLT_MAX must clamp
     * to a signed Infinity, not silently walk the mantissa-carry
     * renormalize step's 8-bit exponent byte past 255 into a NaN-shaped
     * bit pattern (neither operand here is itself Inf or NaN). */
    okf("FLT_MAX+FLT_MAX", FLT_MAX + FLT_MAX, pinf);
    okf("-FLT_MAX-FLT_MAX", (-FLT_MAX) + (-FLT_MAX), ninf);
    okf("FLT_MAX-(-FLT_MAX)", FLT_MAX - (-FLT_MAX), pinf);
    okf("FLT_MAX+1", FLT_MAX + 1.0f, FLT_MAX);      /* negligible addend: no overflow */
    okf("100+200", 100.0f + 200.0f, 300.0f);         /* ordinary case, unaffected */

    /* __fmadd: dcc fuses "addend + x*y" into one call (see madd() above);
     * its multiply half is a separate hand-optimized copy of __fmul's
     * logic and needs the same Inf/NaN checks applied independently -
     * verified here as regression coverage, not just via __fmul directly. */
    okb("madd inf+0*5 isnan", isnan(madd(pinf, zero, five)), 0);     /* 0*5=0, inf+0=inf */
    okb("madd 0+inf*0 isnan", isnan(madd(zero, pinf, zero)), 1);     /* inf*0=nan */
    okb("madd 0+-inf*0 isnan", isnan(madd(zero, ninf, zero)), 1);
    okb("madd nan+3*4 isnan", isnan(madd(nan1, three, 4.0f)), 1);
    okb("madd 5+nan*4 isnan", isnan(madd(five, nan1, 4.0f)), 1);
    okf("madd 5+3*4", madd(five, three, 4.0f), 17.0f);
    okf("madd 5+inf*3", madd(five, pinf, three), pinf);
    okb("madd -inf+inf*3 isnan", isnan(madd(ninf, pinf, three)), 1);
    okf("madd FLT_MAX+FLT_MAX*1", madd(FLT_MAX, FLT_MAX, 1.0f), pinf);

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
