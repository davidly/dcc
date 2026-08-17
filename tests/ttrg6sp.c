/*
 * ttrg6sp.c - MT-F6..MT-F9 focused trig and signed-zero regressions.
 */
#include <stdio.h>
#include <math.h>

#ifdef _DCC_
typedef unsigned long raw32_t;
#else
typedef unsigned int raw32_t;
#endif

static int checks;
static int failures;

static float frombits(raw32_t raw)
{
    union { float f; raw32_t u; } value;
    value.u = raw;
    return value.f;
}

static void okb(const char *name, int got, int want)
{
    checks++;
    if ((got != 0) != (want != 0)) {
        failures++;
        printf("FAIL %s: got=%d want=%d\n", name, got, want);
    }
}

static void okbits(const char *name, float got, raw32_t want)
{
    union { float f; raw32_t u; } value;
    value.f = got;
    checks++;
    if (value.u != want) {
        failures++;
        printf("FAIL %s: got=%08lX want=%08lX\n", name,
               (unsigned long)value.u, (unsigned long)want);
    }
}

int main(void)
{
    float halfpi = frombits(0x3fc90fdbUL);
    float nzero = frombits(0x80000000UL);
    float t;

    t = tanf(halfpi);
    okb("tanf(+halfpi) finite", !isnan(t) && !isinf(t), 1);
    okb("tanf(+halfpi) negative", signbit(t), 1);
    okb("tanf(+halfpi) large", fabsf(t) > 10000000.0f, 1);
    okbits("tanf(+halfpi) bits", t, 0xcbae8a4aUL);

    t = tanf(-halfpi);
    okb("tanf(-halfpi) finite", !isnan(t) && !isinf(t), 1);
    okb("tanf(-halfpi) positive", signbit(t), 0);
    okb("tanf(-halfpi) large", fabsf(t) > 10000000.0f, 1);
    okbits("tanf(-halfpi) bits", t, 0x4bae8a4aUL);

    okb("sinf cutoff", isnan(sinf(32768.0f)), 1);
    okb("cosf cutoff", isnan(cosf(32768.0f)), 1);
    okb("tanf cutoff", isnan(tanf(32768.0f)), 1);
    okb("sinf huge cutoff", isnan(sinf(1.0e20f)), 1);
    okb("sinf below cutoff", !isnan(sinf(32767.0f)), 1);

    okbits("sinf(-0)", sinf(nzero), 0x80000000UL);
    okbits("atanf(-0)", atanf(nzero), 0x80000000UL);
    okbits("atan2f(-0,+1)", atan2f(nzero, 1.0f), 0x80000000UL);
    okbits("atan2f(-0,-1)", atan2f(nzero, -1.0f), 0xc0490fdbUL);

    okb("sinf(1) ordinary",
        fabsf(sinf(1.0f) - 0.841471f) < 0.001f, 1);
    okb("tanf(1) ordinary",
        fabsf(tanf(1.0f) - 1.557408f) < 0.001f, 1);

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
