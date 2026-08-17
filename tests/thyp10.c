/*
 * thyp10.c - MT-F10 signed-zero and tiny hyperbolic regressions.
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
    float nzero = frombits(0x80000000UL);
    float tiny = frombits(0x322bcc77UL);
    float ntiny = frombits(0xb22bcc77UL);
    float minsub = frombits(0x00000001UL);

    okbits("sinhf(-0)", sinhf(nzero), 0x80000000UL);
    okbits("tanhf(-0)", tanhf(nzero), 0x80000000UL);
    okbits("sinhf(tiny)", sinhf(tiny), 0x322bcc77UL);
    okbits("sinhf(-tiny)", sinhf(ntiny), 0xb22bcc77UL);
    okbits("tanhf(tiny)", tanhf(tiny), 0x322bcc77UL);
    okbits("tanhf(-tiny)", tanhf(ntiny), 0xb22bcc77UL);
    okbits("sinhf(minsub)", sinhf(minsub), 0x00000001UL);
    okbits("tanhf(minsub)", tanhf(minsub), 0x00000001UL);

    okb("sinhf(.001) ordinary",
        fabsf(sinhf(0.001f) - 0.001f) < 0.000001f, 1);
    okb("tanhf(.001) ordinary",
        fabsf(tanhf(0.001f) - 0.001f) < 0.000001f, 1);
    okb("sinhf(1) ordinary",
        fabsf(sinhf(1.0f) - 1.175201f) < 0.001f, 1);
    okb("tanhf(1) ordinary",
        fabsf(tanhf(1.0f) - 0.761594f) < 0.001f, 1);

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
