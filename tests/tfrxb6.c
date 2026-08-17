#include <stdio.h>
#include <math.h>

typedef union {
    float f;
    unsigned long u;
} FU;

static int checks;
static int failures;

static void oku(const char *name, unsigned long got, unsigned long want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s got=%lx want=%lx\n", name, got, want);
    }
}

static void oki(const char *name, int got, int want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s got=%d want=%d\n", name, got, want);
    }
}

static void ok(const char *name, int pass)
{
    checks++;
    if (!pass) {
        failures++;
        printf("FAIL %s\n", name);
    }
}

int main(void)
{
    FU x, y;
    float ip;
    int e;

    x.u = 0x00000001UL;
    y.f = frexpf(x.f, &e);
    oku("frexp min mantissa", y.u, 0x3f000000UL);
    oki("frexp min exponent", e, -148);

    x.u = 0x007fffffUL;
    y.f = frexpf(x.f, &e);
    oku("frexp maxsub mantissa", y.u, 0x3f7ffffeUL);
    oki("frexp maxsub exponent", e, -126);

    x.u = 0x80000001UL;
    y.f = frexpf(x.f, &e);
    oku("frexp negative min", y.u, 0xbf000000UL);
    oki("frexp negative exponent", e, -148);

    x.u = 0x00000001UL;
    y.f = ldexpf(x.f, 0);
    oku("ldexp min identity", y.u, 0x00000001UL);
    y.f = ldexpf(x.f, 23);
    oku("ldexp min to normal", y.u, 0x00800000UL);

    y.f = ldexpf(1.0f, -127);
    oku("ldexp exact subnormal", y.u, 0x00400000UL);
    y.f = ldexpf(1.0f, -149);
    oku("ldexp minimum subnormal", y.u, 0x00000001UL);
    y.f = ldexpf(1.0f, -150);
    oku("ldexp half tie", y.u, 0x00000000UL);

    x.u = 0x3f800001UL;
    y.f = ldexpf(x.f, -150);
    oku("ldexp above half", y.u, 0x00000001UL);
    x.u = 0x3ffffffeUL;
    y.f = ldexpf(x.f, -127);
    oku("ldexp max subnormal", y.u, 0x007fffffUL);
    x.u = 0x3fffffffUL;
    y.f = ldexpf(x.f, -127);
    oku("ldexp carry to normal", y.u, 0x00800000UL);

    ok("ldexp huge positive", isinf(ldexpf(1.0f, 32767)));
    y.f = ldexpf(-1.0f, -32768);
    oku("ldexp huge negative", y.u, 0x80000000UL);
    ok("logf minsub finite", isfinite(logf(1.40129846e-45f)));
    ok("logf minsub value",
       logf(1.40129846e-45f) > -103.4f &&
       logf(1.40129846e-45f) < -103.1f);

    y.f = frexpf(-0.0f, &e);
    oku("frexp negative zero", y.u, 0x80000000UL);
    oki("frexp zero exponent", e, 0);
    y.f = ldexpf(-0.0f, 17);
    oku("ldexp negative zero", y.u, 0x80000000UL);
    y.f = modff(-3.0f, &ip);
    oku("modf integer fraction", y.u, 0x80000000UL);
    y.f = modff(-0.0f, &ip);
    oku("modf negative zero", y.u, 0x80000000UL);

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
