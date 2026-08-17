/*
 * Batch 6 MT-F2..MT-F5 coverage for powf special values and parity.
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

static void okf(const char *name, float got, float want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s: got=%f want=%f\n", name, got, want);
    }
}

static void oknan(const char *name, float got)
{
    int v = isnan(got);
    okb(name, v, 1);
}

static void okinf(const char *name, float got, int sign)
{
    int v = isinf(got);
    okb(name, v, 1);
    v = signbit(got);
    okb(name, v, sign);
}

static void okzero(const char *name, float got, int sign)
{
    int v = (got == 0.0f);
    okb(name, v, 1);
    v = signbit(got);
    okb(name, v, sign);
}

int main(void)
{
    float pinf = INFINITY;
    float ninf = -INFINITY;
    float nan1 = NAN;
    float nz = -0.0f;
    float r;

    r = powf(-2.0f, 0.5f);
    oknan("negative fractional is nan", r);

    r = powf(nan1, 0.5f);
    oknan("nan base propagates", r);
    r = powf(-2.0f, nan1);
    oknan("nan exponent propagates", r);
    r = powf(0.0f, nan1);
    oknan("zero nan is nan", r);
    r = powf(1.0f, nan1);
    okf("one nan is one", r, 1.0f);
    r = powf(nan1, 0.0f);
    okf("nan zero is one", r, 1.0f);
    r = powf(nan1, -0.0f);
    okf("nan negative zero is one", r, 1.0f);

    r = powf(-2.0f, pinf);
    okinf("-2 +inf", r, 0);
    r = powf(-1.0f, pinf);
    okf("-1 +inf is one", r, 1.0f);
    r = powf(-1.0f, ninf);
    okf("-1 -inf is one", r, 1.0f);

    r = powf(-0.5f, pinf);
    okzero("-.5 +inf", r, 0);
    r = powf(-0.5f, ninf);
    okinf("-.5 -inf", r, 0);

    r = powf(-1.0f, 2147483648.0f);
    okf("-1 huge even is one", r, 1.0f);
    r = powf(-2.0f, 2147483648.0f);
    okinf("-2 huge even", r, 0);
    r = powf(-1.0f, -2147483648.0f);
    okf("-1 huge negative is one", r, 1.0f);
    r = powf(-2.0f, -2147483648.0f);
    okzero("-2 huge negative", r, 0);

    r = powf(-1.0f, 8388609.0f);
    okf("-1 large odd", r, -1.0f);
    r = powf(-1.0f, 8388610.0f);
    okf("-1 large even", r, 1.0f);
    r = powf(-1.0f, 4194304.5f);
    oknan("-1 large fractional", r);

    r = powf(nz, 3.0f);
    okzero("-0 odd", r, 1);
    r = powf(nz, 2.0f);
    okzero("-0 even", r, 0);
    r = powf(nz, -3.0f);
    okinf("-0 negative odd", r, 1);
    r = powf(nz, -2.0f);
    okinf("-0 negative even", r, 0);
    r = powf(nz, 0.5f);
    okzero("-0 fractional", r, 0);
    r = powf(nz, -0.5f);
    okinf("-0 negative fractional", r, 0);

    r = powf(ninf, 3.0f);
    okinf("-inf odd", r, 1);
    r = powf(ninf, -3.0f);
    okzero("-inf negative odd", r, 1);
    r = powf(ninf, 2.5f);
    okinf("-inf fractional", r, 0);

    r = powf(-2.0f, 3.0f);
    okf("ordinary negative odd", r, -8.0f);
    r = powf(-2.0f, 2.0f);
    okf("ordinary negative even", r, 4.0f);

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
