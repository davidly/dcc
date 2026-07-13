/*
 * tasinfsp.c - C99 7.12.4.2/7.12.4.1 domain regression coverage for
 * dccrtl.mac's asinf/acosf. "if (x > 1.0f) return 0.0f" (after negating
 * for x<0) was a bare 0.0f instead of the required NaN for |x|>1 -
 * |x|==1.0 exactly already took the ordinary polynomial path and was
 * already correct, so this was purely a wrong return value on an
 * already-correctly-triggered branch, not a wrong threshold. acosf =
 * HALF_PI - asinf(x) inherits the fix with no code of its own.
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
    float nan1 = NAN;

    okb("asinf(1.01) isnan", isnan(asinf(1.01f)), 1);
    okb("asinf(2) isnan", isnan(asinf(2.0f)), 1);
    okb("asinf(-1.01) isnan", isnan(asinf(-1.01f)), 1);
    okb("asinf(-2) isnan", isnan(asinf(-2.0f)), 1);
    okb("asinf(nan) isnan", isnan(asinf(nan1)), 1);
    okb("asinf(1.0) close", fabsf(asinf(1.0f) - 1.570796f) < 0.001f, 1);
    okb("asinf(-1.0) close", fabsf(asinf(-1.0f) + 1.570796f) < 0.001f, 1);
    okf("asinf(0)", asinf(0.0f), 0.0f);

    okb("acosf(2) isnan", isnan(acosf(2.0f)), 1);
    okb("acosf(-2) isnan", isnan(acosf(-2.0f)), 1);
    okb("acosf(nan) isnan", isnan(acosf(nan1)), 1);
    okf("acosf(1.0)", acosf(1.0f), 0.0f);

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
