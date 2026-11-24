/* cosf, tanf, asinf and acosf against the true values to the documented
 * 5-6 significant digits. cosf used a polynomial through x^6 up to pi/2 and
 * was off by 5e-4 there; tanf used the [3/2] Pade approximant, off by 2e-4
 * near pi/4; asinf used four tuned series terms, off by 1.2e-4 near 0.5. */
#include <stdio.h>
#include <math.h>

struct trig_case { int fn; float x; float want; };

static const struct trig_case cases[] = {
    { 0, 0.5f, 0.877582562f },
    { 0, 1.0f, 0.540302306f },
    { 0, 1.20000005f, 0.36235771f },
    { 0, 1.5f, 0.0707372017f },
    { 0, 1.54999995f, 0.0207948755f },
    { 0, 1.60000002f, -0.0291995461f },
    { 0, 2.0f, -0.416146837f },
    { 0, -1.5f, 0.0707372017f },
    { 0, 3.0f, -0.989992497f },
    { 0, 5.0f, 0.283662185f },
    { 0, 1.57079637f, -4.37113883e-08f },
    { 0, 4.71238899f, 1.19248806e-08f },
    { 0, -32.9799995f, 0.00672326982f },
    { 0, 252.898209f, -4.18570689e-09f },
    { 1, 0.5f, 0.54630249f },
    { 1, 0.75f, 0.93159646f },
    { 1, 0.800000012f, 1.02963858f },
    { 1, 1.0f, 1.55740772f },
    { 1, 1.29999995f, 3.60210178f },
    { 1, 2.3499999f, -1.01246656f },
    { 1, -0.800000012f, -1.02963858f },
    { 1, 4.0f, 1.15782128f },
    { 2, 0.25f, 0.252680255f },
    { 2, 0.5f, 0.523598776f },
    { 2, 0.504499972f, 0.528802737f },
    { 2, 0.50999999f, 0.535184779f },
    { 2, -0.519999981f, -0.546850928f },
    { 2, 0.75f, 0.848062079f },
    { 2, 0.99000001f, 1.42925692f },
    { 3, 0.5f, 1.04719755f },
    { 3, 0.50999999f, 1.03561155f },
    { 3, -0.519999981f, 2.11764726f },
    { 3, 0.899999976f, 0.451026866f },
};

static const char *const names[] = { "cosf", "tanf", "asinf", "acosf" };

static float eval(int fn, float x)
{
    switch (fn) {
    case 0: return cosf(x);
    case 1: return tanf(x);
    case 2: return asinf(x);
    default: return acosf(x);
    }
}

int main(void)
{
    int i;
    int fails = 0;

    for (i = 0; i < (int)(sizeof cases / sizeof cases[0]); i++) {
        float got = eval(cases[i].fn, cases[i].x);
        float want = cases[i].want;
        float err = fabsf(got - want);

        if (!(err <= 2e-5f * fabsf(want))) {
            printf("%s(%f) = %f, expected %f\n", names[cases[i].fn], cases[i].x, got, want);
            ++fails;
        }
    }
    if (fails)
        printf("ttrigacc failed %d\n", fails);
    else
        printf("ttrigacc passed\n");
    return 0;
}
