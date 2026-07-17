/*
 * tfloorsp.c - C99 7.12.9.1/7.12.9.2/7.12.6.12 special-value regression
 * coverage for dccrtl.mac's floorf/ceilf/modff.
 *
 * floorf/ceilf round-trip x through (long) and back, with no self-
 * correction for Inf/NaN: converting either to long is undefined in C,
 * and whatever finite substitute __ffl produced silently replaced the
 * original value instead of floor/ceil returning x unmodified as C99
 * requires.
 *
 * modff = (x<=0 ? floorf(x) : ceilf(x)) for *iptr, then x - *iptr for
 * the fractional part. Fixing floorf/ceilf's Inf handling made *iptr
 * correct on its own, but then "x - *iptr" became Inf-Inf (an invalid
 * operation, correctly NaN from an earlier fix this session) instead
 * of the signed zero C99 7.12.6.12 requires for infinite x - modff
 * needed its own explicit Inf case for the return value.
 */
#include <stdio.h>
#include <math.h>

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

int main(void)
{
    float pinf = INFINITY, ninf = -INFINITY, nan1 = NAN;
    float ip, fp;

    okb("floorf(inf) isinf", isinf(floorf(pinf)), 1);
    okb("floorf(-inf) isinf", isinf(floorf(ninf)), 1);
    okb("floorf(nan) isnan", isnan(floorf(nan1)), 1);
    okb("ceilf(inf) isinf", isinf(ceilf(pinf)), 1);
    okb("ceilf(-inf) isinf", isinf(ceilf(ninf)), 1);
    okb("ceilf(nan) isnan", isnan(ceilf(nan1)), 1);
    okf("floorf(3.7)", floorf(3.7f), 3.0f);
    okf("ceilf(3.2)", ceilf(3.2f), 4.0f);
    okf("floorf(-3.7)", floorf(-3.7f), -4.0f);
    okf("ceilf(-3.2)", ceilf(-3.2f), -3.0f);

    /* Values with |x| < 1 exercise the sub-1.0 magnitude path, where the
     * result is a fixed +/-0 or +/-1 rather than a truncated significand.
     * A malformed M80 hex literal (bf80h instead of 0bf80h, parsed as an
     * undefined symbol = 0) once made floorf of a negative fraction return
     * +0.0 instead of -1.0; the old |x|>1 cases above never touched it. */
    okf("floorf(0.25)", floorf(0.25f), 0.0f);
    okf("ceilf(0.25)", ceilf(0.25f), 1.0f);
    okf("floorf(-0.25)", floorf(-0.25f), -1.0f);
    okf("floorf(-0.75)", floorf(-0.75f), -1.0f);
    okf("ceilf(0.75)", ceilf(0.75f), 1.0f);
    okf("ceilf(-0.25)", ceilf(-0.25f), 0.0f);
    okb("ceilf(-0.25) is -0", signbit(ceilf(-0.25f)), 1);
    okb("floorf(0.25) is +0", signbit(floorf(0.25f)), 0);
    okb("floorf(+0) is +0", signbit(floorf(0.0f)), 0);
    okb("ceilf(+0) is +0", signbit(ceilf(0.0f)), 0);
    okb("floorf(-0) is -0", signbit(floorf(-0.0f)), 1);
    okb("ceilf(-0) is -0", signbit(ceilf(-0.0f)), 1);

    fp = modff(pinf, &ip);
    okb("modff(inf) ip isinf", isinf(ip), 1);
    okf("modff(inf) fp", fp, 0.0f);
    fp = modff(ninf, &ip);
    okb("modff(-inf) ip isinf", isinf(ip), 1);
    okb("modff(-inf) ip sign", signbit(ip), 1);
    okf("modff(-inf) fp", fp, 0.0f);
    okb("modff(-inf) fp sign", signbit(fp), 1);
    fp = modff(nan1, &ip);
    okb("modff(nan) ip isnan", isnan(ip), 1);
    okb("modff(nan) fp isnan", isnan(fp), 1);
    fp = modff(3.75f, &ip);
    okf("modff(3.75) ip", ip, 3.0f);
    okf("modff(3.75) fp", fp, 0.75f);

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
