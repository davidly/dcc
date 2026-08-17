/*
 * Batch 6 MT-F1 coverage for expf's finite-overflow boundary.
 */
#include <stdio.h>
#include <math.h>

static int checks;
static int failures;

static void okb(const char *name, int got, int want)
{
    checks++;
    if ((got != 0) != (want != 0)) {
        failures++;
        printf("FAIL %s: got %d want %d\n", name, got, want);
    }
}

int main(void)
{
    float pinf = INFINITY;
    float r;
    int v;

    r = expf(88.5f);
    v = isfinite(r);
    okb("expf(88.5) finite", v, 1);
    v = r > 2.70e38f && r < 2.75e38f;
    okb("expf(88.5) magnitude", v, 1);

    r = expf(88.72283f);
    v = isfinite(r);
    okb("expf(prev threshold) finite", v, 1);

    r = expf(88.72284f);
    v = isinf(r);
    okb("expf(threshold) inf", v, 1);

    r = expf(pinf);
    v = isinf(r);
    okb("expf(+inf) inf", v, 1);
    v = signbit(r);
    okb("expf(+inf) positive", v, 0);

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
