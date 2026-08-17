/*
 * tpowfsp.c - special-value regression coverage for dccrtl.mac's powf.
 * This original special-value smoke test remains alongside tb6pow.c,
 * which covers the complete Batch 6 NaN, infinite-exponent, direct
 * integer-parity, and signed-zero corrections.
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
    float pinf = INFINITY, nan1 = NAN;

    okb("powf(0,-1) isinf", isinf(powf(0.0f, -1.0f)), 1);
    okb("powf(0,-2) isinf", isinf(powf(0.0f, -2.0f)), 1);
    okb("powf(-0,-1) isinf", isinf(powf(-0.0f, -1.0f)), 1);
    okf("powf(0,0)", powf(0.0f, 0.0f), 1.0f);
    okf("powf(0,5)", powf(0.0f, 5.0f), 0.0f);
    okf("powf(5,0)", powf(5.0f, 0.0f), 1.0f);
    okb("powf(2,inf) isinf", isinf(powf(2.0f, pinf)), 1);
    okb("powf(inf,2) isinf", isinf(powf(pinf, 2.0f)), 1);
    okb("powf(nan,2) isnan", isnan(powf(nan1, 2.0f)), 1);
    okb("powf(2,nan) isnan", isnan(powf(2.0f, nan1)), 1);
    okf("powf(-2,3)", powf(-2.0f, 3.0f), -8.0f);
    okf("powf(-2,2)", powf(-2.0f, 2.0f), 4.0f);
    okf("powf(2,10)", powf(2.0f, 10.0f), 1024.0f);

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
