/* Full-range sqrtf regression, including the asinf/acosf endpoint transform.
 * Reference bits are host-libm binary32 square roots. The exponent sweep
 * checks five significands at every normal exponent, within one ULP.
 */
#include <stdio.h>
#include <math.h>
#include <errno.h>

#ifdef _DCC_
typedef unsigned long raw32_t;
#else
typedef unsigned int raw32_t;
#endif

static int checks, failures;

static float frombits(raw32_t raw)
{
    union { float f; raw32_t u; } v;
    v.u = raw;
    return v.f;
}

static void root(raw32_t input, raw32_t want, raw32_t tolerance)
{
    union { float f; raw32_t u; } got;
    raw32_t diff;
    errno = 0;
    got.f = sqrtf(frombits(input));
    diff = got.u > want ? got.u - want : want - got.u;
    checks++;
    if (diff > tolerance || errno != 0) {
        failures++;
        printf("FAIL sqrt %08lX: got %08lX want %08lX errno %d\n",
               (unsigned long)input, (unsigned long)got.u,
               (unsigned long)want, errno);
    }
}

static void nanroot(raw32_t input, int error)
{
    float got;
    errno = 0;
    got = sqrtf(frombits(input));
    checks++;
    if (!isnan(got) || ((math_errhandling & MATH_ERRNO) && errno != error)) {
        failures++;
        printf("FAIL sqrt %08lX: expected NaN, errno %d want %d\n",
               (unsigned long)input, errno, error);
    }
}

static void closef(const char *name, float got, float want, float tolerance)
{
    checks++;
    if (!(fabsf(got - want) <= tolerance)) {
        failures++;
        printf("FAIL %s: got %f want %f\n", name, got, want);
    }
}

static const raw32_t fractions[] = {
    0x00000000UL, 0x00000001UL, 0x00155555UL, 0x00400000UL, 0x007fffffUL
};
static const raw32_t roots[2][5] = {
    { 0x3f800000UL, 0x3f800000UL, 0x3f8a417cUL, 0x3f9cc471UL, 0x3fb504f3UL },
    { 0x3fb504f3UL, 0x3fb504f4UL, 0x3fc385f2UL, 0x3fddb3d7UL, 0x3fffffffUL },
};
static const raw32_t cases[][2] = {
    { 0x461c4000UL, 0x42c80000UL },
    { 0x4cbebc20UL, 0x461c4000UL },
    { 0x38d1b717UL, 0x3c23d70aUL },
    { 0x322bcc77UL, 0x38d1b717UL },
    { 0x0da24260UL, 0x26901d7dUL },
    { 0x7149f2caUL, 0x58635fa9UL },
    { 0x007fffffUL, 0x1fffffffUL },
    { 0x00400001UL, 0x1fb504f5UL },
    { 0x00000003UL, 0x1a9cc471UL },
    { 0x00012345UL, 0x1e411636UL },
    { 0x00555555UL, 0x1fd105ebUL },
    { 0x00000001UL, 0x1a3504f3UL },
    { 0x00000002UL, 0x1a800000UL },
    { 0x00000004UL, 0x1ab504f3UL },
    { 0x00000008UL, 0x1b000000UL },
    { 0x00000010UL, 0x1b3504f3UL },
    { 0x00000020UL, 0x1b800000UL },
    { 0x00000040UL, 0x1bb504f3UL },
    { 0x00000080UL, 0x1c000000UL },
    { 0x00000100UL, 0x1c3504f3UL },
    { 0x00000200UL, 0x1c800000UL },
    { 0x00000400UL, 0x1cb504f3UL },
    { 0x00000800UL, 0x1d000000UL },
    { 0x00001000UL, 0x1d3504f3UL },
    { 0x00002000UL, 0x1d800000UL },
    { 0x00004000UL, 0x1db504f3UL },
    { 0x00008000UL, 0x1e000000UL },
    { 0x00010000UL, 0x1e3504f3UL },
    { 0x00020000UL, 0x1e800000UL },
    { 0x00040000UL, 0x1eb504f3UL },
    { 0x00080000UL, 0x1f000000UL },
    { 0x00100000UL, 0x1f3504f3UL },
    { 0x00200000UL, 0x1f800000UL },
    { 0x00400000UL, 0x1fb504f3UL },
};

int main(void)
{
    int exponent, j;
    raw32_t want;
    float x;

    /* Exact results for the reported large squares. */
    for (j = 0; j < 2; j++)
        root(cases[j][0], cases[j][1], 0);
    for (j = 2; j < (int)(sizeof(cases) / sizeof(cases[0])); j++)
        root(cases[j][0], cases[j][1], 1);

    for (exponent = 1; exponent < 255; exponent++) {
        for (j = 0; j < 5; j++) {
            want = roots[(exponent & 1) == 0][j] - 0x3f800000UL
                 + ((raw32_t)((exponent + 127) / 2) << 23);
            root(((raw32_t)exponent << 23) | fractions[j], want, 1);
        }
    }

    /* Raw classification must distinguish negative subnormals from -0
     * and propagate either sign of signaling NaN without a domain error. */
    nanroot(0x80000001UL, EDOM);
    nanroot(0x7f800001UL, 0);
    nanroot(0xff800001UL, 0);

    /* The nearest floats below +1 and above -1 reduce to sqrt(2^-25).
     * Include less extreme endpoints that also defeated g=x after 8 steps. */
    x = frombits(0x3f7fffffUL);
    closef("asinf(+endpoint)", asinf(x), 1.57045105981018f, 0.0000004f);
    closef("asinf(-endpoint)", asinf(-x), -1.57045105981018f, 0.0000004f);
    closef("acosf(+endpoint)", acosf(x), 0.000345266984716204f, 0.0000004f);
    closef("acosf(-endpoint)", acosf(-x), 3.14124738660508f, 0.0000004f);
    x = frombits(0x3f7ffffeUL);
    closef("asinf(+endpoint)", asinf(x), 1.57030804554005f, 0.0000004f);
    closef("asinf(-endpoint)", asinf(-x), -1.57030804554005f, 0.0000004f);
    closef("acosf(+endpoint)", acosf(x), 0.000488281254850639f, 0.0000004f);
    closef("acosf(-endpoint)", acosf(-x), 3.14110437233494f, 0.0000004f);
    x = frombits(0x3f7fff00UL);
    closef("asinf(+endpoint)", asinf(x), 1.56527204804238f, 0.0000004f);
    closef("asinf(-endpoint)", asinf(-x), -1.56527204804238f, 0.0000004f);
    closef("acosf(+endpoint)", acosf(x), 0.00552427875251877f, 0.0000004f);
    closef("acosf(-endpoint)", acosf(-x), 3.13606837483727f, 0.0000004f);
    x = frombits(0x3f7ff000UL);
    closef("asinf(+endpoint)", asinf(x), 1.54869879029174f, 0.0000004f);
    closef("asinf(-endpoint)", asinf(-x), -1.54869879029174f, 0.0000004f);
    closef("acosf(+endpoint)", acosf(x), 0.0220975365031609f, 0.0000004f);
    closef("acosf(-endpoint)", acosf(-x), 3.11949511708663f, 0.0000004f);

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
