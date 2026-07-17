/*
 * tfmodfsp.c - C99 7.12.10.1 domain/special-value regression coverage
 * for dccrtl.mac's fmodf. The q=x/y -> (long)q -> x - q*y pipeline had
 * no self-correction for Inf/NaN operands: fmodf(Inf, y) truncated
 * (long)(Inf/y) to some finite value (long can't represent infinity)
 * and silently produced Inf back out instead of the required NaN;
 * fmodf(x, Inf) computed prod = 0*Inf = NaN (correctly, from an earlier
 * fix this session) and that NaN propagated through the final
 * subtraction instead of returning x unchanged as C99 requires.
 *
 * This also fixes sinf/cosf/tanf's domain handling for infinite input
 * indirectly, since they all reduce their argument via fmodf first -
 * sinf(Inf) already happened to give NaN by accident (some intermediate
 * inf-inf cancellation in its polynomial), but cosf(Inf) did not before
 * this fix.
 */
#include <stdio.h>
#include <math.h>

#ifdef _DCC_
typedef unsigned long raw32_t;
#else
typedef unsigned int raw32_t;
#endif

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
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s: got=%f want=%f\n", name, got, want);
    }
}

static float frombits(raw32_t raw)
{
    union { float f; raw32_t u; } value;
    value.u = raw;
    return value.f;
}

static void okbits(const char *name, float got, raw32_t want)
{
    union { float f; raw32_t u; } value;
    value.f = got;
    checks++;
    if (value.u != want) {
        failures++;
        printf("FAIL %s: got=%08lX want=%08lX\n", name,
               (unsigned long)value.u, (unsigned long)want);
    }
}

int main(void)
{
    float pinf = INFINITY, ninf = -INFINITY, nan1 = NAN;

    okb("fmodf(inf,2pi) isnan", isnan(fmodf(pinf, 6.28318531f)), 1);
    okb("fmodf(-inf,2pi) isnan", isnan(fmodf(ninf, 6.28318531f)), 1);
    okb("fmodf(nan,2pi) isnan", isnan(fmodf(nan1, 6.28318531f)), 1);
    okb("fmodf(5,0) isnan", isnan(fmodf(5.0f, 0.0f)), 1);
    okf("fmodf(5,inf)", fmodf(5.0f, pinf), 5.0f);
    okf("fmodf(-5,inf)", fmodf(-5.0f, pinf), -5.0f);
    okf("fmodf(5,3)", fmodf(5.0f, 3.0f), 2.0f);

        /* Exact payload checks exercise the normalized-significand reduction,
         * signed-zero rule, signed internal exponents, and normal/subnormal
         * result packing. These values are derived from the host C99 fmodf. */
        okbits("negative divisor", fmodf(5.0f, -3.0f), 0x40000000UL);
        okbits("negative exact zero", fmodf(-6.0f, 3.0f), 0x80000000UL);
        okbits("large exponent gap", fmodf(1.0e30f, 7.0f), 0x3f800000UL);
        okbits("subnormal 3 mod 2",
            fmodf(frombits(0x00000003UL), frombits(0x00000002UL)),
            0x00000001UL);
        okbits("subnormal 5 mod 2",
            fmodf(frombits(0x00000005UL), frombits(0x00000002UL)),
            0x00000001UL);
        okbits("equal subnormals",
            fmodf(frombits(0x00000002UL), frombits(0x00000002UL)),
            0x00000000UL);
        okbits("subnormal x less than y",
            fmodf(frombits(0x00000001UL), frombits(0x00000004UL)),
            0x00000001UL);
        okbits("normal mod max subnormal",
            fmodf(1.0f, frombits(0x007fffffUL)), 0x00000800UL);
        okbits("max subnormal mod normal",
            fmodf(frombits(0x007fffffUL), 1.0f), 0x007fffffUL);
        okbits("subnormal result boundary",
            fmodf(frombits(0x00c00000UL), frombits(0x00800000UL)),
            0x00400000UL);
        okbits("negative subnormal result",
            fmodf(frombits(0x80000005UL), frombits(0x00000002UL)),
            0x80000001UL);
        okbits("max subnormal reduction",
            fmodf(frombits(0x007fffffUL), frombits(0x00000003UL)),
            0x00000001UL);
        okbits("smallest normal exact zero",
            fmodf(frombits(0x00800000UL), frombits(0x00400000UL)),
            0x00000000UL);
        okbits("maximum exponent gap",
            fmodf(frombits(0x7f7fffffUL), frombits(0x00000001UL)),
            0x00000000UL);
        okbits("negative maximum exponent gap",
            fmodf(frombits(0xff7fffffUL), frombits(0x00000001UL)),
            0x80000000UL);

    okb("sinf(inf) isnan", isnan(sinf(pinf)), 1);
    okb("cosf(inf) isnan", isnan(cosf(pinf)), 1);
    okb("tanf(inf) isnan", isnan(tanf(pinf)), 1);
    okb("sinf(1) close", fabsf(sinf(1.0f) - 0.841471f) < 0.001f, 1);
    okf("cosf(0)", cosf(0.0f), 1.0f);

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
