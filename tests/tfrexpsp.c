/*
 * tfrexpsp.c - Inf/NaN regression coverage for dccrtl.mac's frexpf and
 * ldexpf (IEEE-754 convention: frexp(Inf/NaN, e) and ldexp(Inf/NaN, n)
 * both return x unmodified - not explicitly in the C99 base text, but
 * universal libm practice). Neither had any check for exponent 255: the
 * raw exponent bits were extracted and manipulated exactly like an
 * ordinary finite value, so Inf's zero mantissa came back looking like
 * the ordinary value 0.5 and NaN's payload got the same treatment.
 * ldexpf's separate overflow-clamp path is also upgraded here from a
 * finite 3.4e38f saturation constant to a real signed Infinity, matching
 * the convention expf's earlier fix in this session established.
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
    int e;

    okb("frexpf(inf) isinf", isinf(frexpf(pinf, &e)), 1);
    okb("frexpf(-inf) isinf", isinf(frexpf(ninf, &e)), 1);
    okb("frexpf(-inf) sign", signbit(frexpf(ninf, &e)), 1);
    okb("frexpf(nan) isnan", isnan(frexpf(nan1, &e)), 1);
    okb("ldexpf(inf,2) isinf", isinf(ldexpf(pinf, 2)), 1);
    okb("ldexpf(-inf,2) isinf", isinf(ldexpf(ninf, 2)), 1);
    okb("ldexpf(-inf,2) sign", signbit(ldexpf(ninf, 2)), 1);
    okb("ldexpf(nan,2) isnan", isnan(ldexpf(nan1, 2)), 1);
    okb("ldexpf(1,1000) isinf", isinf(ldexpf(1.0f, 1000)), 1);   /* overflow */
    okb("ldexpf(1,1000) sign", signbit(ldexpf(1.0f, 1000)), 0);
    okb("ldexpf(-1,1000) sign", signbit(ldexpf(-1.0f, 1000)), 1);

    okf("frexpf(8) m", frexpf(8.0f, &e), 0.5f);
    frexpf(8.0f, &e);
    checks++;
    if (e != 4) { failures++; printf("FAIL frexpf(8) e: got %d want 4\n", e); }
    okf("ldexpf(0.5,4)", ldexpf(0.5f, 4), 8.0f);

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
