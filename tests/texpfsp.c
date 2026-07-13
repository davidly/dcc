/*
 * texpfsp.c - C99 7.12.6.19 special-value regression coverage for
 * dccrtl.mac's expf. Its two range comparisons (x<-88 underflow-to-0,
 * x>=88 overflow) already returned the right ANSWER for -Inf (correctly
 * < -88) and +Inf (correctly not < 88, so it fell into the same branch
 * as ordinary overflow) - the real gap was NaN, which compares false
 * against everything and fell through to the overflow branch, silently
 * returning a finite 3.4e38f approximation instead of propagating NaN.
 * That same branch's return value is now also a real +Infinity instead
 * of the 3.4e38f approximation, for both the NaN-adjacent +Inf case and
 * ordinary large-finite overflow (e.g. expf(100)).
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

    okb("expf(nan) isnan", isnan(expf(nan1)), 1);
    okb("expf(+inf) isinf", isinf(expf(pinf)), 1);
    okb("expf(+inf) sign", signbit(expf(pinf)), 0);
    okb("expf(-inf) iszero", expf(ninf) == 0.0f, 1);
    okb("expf(100) isinf", isinf(expf(100.0f)), 1);
    okb("expf(-100) iszero", expf(-100.0f) == 0.0f, 1);
    okf("expf(0)", expf(0.0f), 1.0f);

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
