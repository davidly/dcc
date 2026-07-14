/*
 * tlogfsp.c - C99 7.12.6.7 domain/special-value regression coverage for
 * dccrtl.mac's logf (and log10f, a thin logf(x)*const wrapper that
 * inherits the fix with no code changes of its own). The frexpf-based
 * Taylor series has no self-correction for anything other than a
 * positive finite x: logf(x<0) returned a finite -3.4e38f approximation
 * instead of NaN, logf(0) returned that same approximation instead of a
 * real -Infinity, and logf(+Inf) silently fell through the whole series
 * and landed on a wrong finite value instead of +Infinity.
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
    float pinf = INFINITY, ninf = -INFINITY, nan1 = NAN, negzero = -0.0f;

    okb("logf(-1) isnan", isnan(logf(-1.0f)), 1);
    okb("logf(-4) isnan", isnan(logf(-4.0f)), 1);
    okb("logf(-inf) isnan", isnan(logf(ninf)), 1);
    okb("logf(nan) isnan", isnan(logf(nan1)), 1);
    okb("logf(+0) isinf", isinf(logf(0.0f)), 1);
    okb("logf(+0) sign", signbit(logf(0.0f)), 1);
    okb("logf(-0) isinf", isinf(logf(negzero)), 1);
    okb("logf(-0) sign", signbit(logf(negzero)), 1);
    okb("logf(+inf) isinf", isinf(logf(pinf)), 1);
    okb("logf(+inf) sign", signbit(logf(pinf)), 0);
    okf("logf(1)", logf(1.0f), 0.0f);

    okb("log10f(-1) isnan", isnan(log10f(-1.0f)), 1);
    okb("log10f(0) isinf", isinf(log10f(0.0f)), 1);
    okb("log10f(0) sign", signbit(log10f(0.0f)), 1);
    okb("log10f(+inf) isinf", isinf(log10f(pinf)), 1);
    okb("log10f(nan) isnan", isnan(log10f(nan1)), 1);
    okf("log10f(100)", log10f(100.0f), 2.0f);
    okf("log10f(1000)", log10f(1000.0f), 3.0f);

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
