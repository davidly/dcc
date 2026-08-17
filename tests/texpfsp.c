/*
 * texpfsp.c - C99 7.12.6.19 special-value regression coverage for
 * dccrtl.mac's expf. NaN propagation and real Infinity overflow results
 * were added before Batch 6; the current implementation also gives +Inf
 * its own path and uses the finite overflow boundary near 88.72284f.
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
    okb("expf(-150) iszero", expf(-150.0f) == 0.0f, 1);
    okf("expf(0)", expf(0.0f), 1.0f);

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
