/*
 * tsqrtsp.c - C99 7.12.7.5 domain/special-value regression coverage for
 * dccrtl.mac's sqrtf. Newton's method (g = (g + x/g) * 0.5f, starting
 * from g = x) has no self-correction for anything other than a positive
 * finite x: sqrtf(-1) silently returned 0 instead of NaN, and sqrtf(+Inf)
 * fed Inf/Inf into the iteration's own division and landed on NaN
 * instead of +Inf after 8 wasted iterations.
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

    okb("sqrtf(-1) isnan", isnan(sqrtf(-1.0f)), 1);
    okb("sqrtf(-4) isnan", isnan(sqrtf(-4.0f)), 1);
    okb("sqrtf(-inf) isnan", isnan(sqrtf(ninf)), 1);
    okb("sqrtf(nan) isnan", isnan(sqrtf(nan1)), 1);
    okb("sqrtf(+inf) isinf", isinf(sqrtf(pinf)), 1);
    okb("sqrtf(+inf) sign", signbit(sqrtf(pinf)), 0);
    okb("sqrtf(+0) sign", signbit(sqrtf(0.0f)), 0);
    okb("sqrtf(-0) sign", signbit(sqrtf(negzero)), 1);
    okf("sqrtf(+0) val", sqrtf(0.0f), 0.0f);
    okf("sqrtf(-0) val", sqrtf(negzero), 0.0f);

    /* ordinary cases must be unaffected */
    okf("sqrtf(4)", sqrtf(4.0f), 2.0f);
    okf("sqrtf(9)", sqrtf(9.0f), 3.0f);
    okf("sqrtf(100)", sqrtf(100.0f), 10.0f);
    okf("sqrtf(0.25)", sqrtf(0.25f), 0.5f);

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
