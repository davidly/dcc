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

int main(void)
{
    float zero = 0.0f;
    float negzero = -0.0f;
    float one = 1.0f;
    float negone = -1.0f;
    float inf = INFINITY;
    float ninf = -INFINITY;
    float nan = NAN;

    okb("isnan(nan)", isnan(nan), 1);
    okb("isnan(inf)", isnan(inf), 0);
    okb("isnan(1.0)", isnan(one), 0);
    okb("isnan(0.0)", isnan(zero), 0);

    okb("isinf(inf)", isinf(inf), 1);
    okb("isinf(-inf)", isinf(ninf), 1);
    okb("isinf(nan)", isinf(nan), 0);
    okb("isinf(1.0)", isinf(one), 0);

    okb("isfinite(1.0)", isfinite(one), 1);
    okb("isfinite(0.0)", isfinite(zero), 1);
    okb("isfinite(inf)", isfinite(inf), 0);
    okb("isfinite(-inf)", isfinite(ninf), 0);
    okb("isfinite(nan)", isfinite(nan), 0);

    okb("signbit(1.0)", signbit(one), 0);
    okb("signbit(-1.0)", signbit(negone), 1);
    okb("signbit(0.0)", signbit(zero), 0);
    okb("signbit(-0.0)", signbit(negzero), 1);
    okb("signbit(inf)", signbit(inf), 0);
    okb("signbit(-inf)", signbit(ninf), 1);

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
