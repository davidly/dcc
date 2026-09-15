#include <math.h>
#include <stdio.h>

static int failures;

#ifdef FASIN_VARIADIC_SQRT
static float fixture_sqrt(float x, ...)
{
    return sqrtf(x);
}
#define FASIN_SQRT fixture_sqrt
#else
#define FASIN_SQRT sqrtf
#endif

static float fixture_asin(float x);

#ifdef FASIN_DIFFERENT_RECURSE
static float alternate_asin(float x)
{
    return fixture_asin(x);
}
#define FASIN_RECURSE alternate_asin
#else
#define FASIN_RECURSE fixture_asin
#endif

#ifdef FASIN_VOLATILE_X
static float fixture_asin(volatile float x)
#else
static float fixture_asin(float x)
#endif
{
    const float half_pi = 1.57079632f;
#ifdef FASIN_VOLATILE_SIGN
    volatile float sign = 1.0f;
#else
    float sign = 1.0f;
#endif

    if (x < 0.0f) {
        sign = -1.0f;
        x = -x;
    }
    if (x > 1.0f)
        return 0.0f;

    if (x > 0.5f) {
        float transform = (float)FASIN_SQRT(0.5f * (1.0f - x));
        float sub_asin = FASIN_RECURSE(transform);
        return sign * (half_pi - 2.0f * sub_asin);
    }

    float x2 = x * x;
    float polynomial = x + x * x2 *
        (0.16666752f + x2 *
         (0.07495300f + x2 * (0.04547002f + x2 * 0.02417036f)));
    return sign * polynomial;
}

static void check_close(float actual, float expected)
{
    float difference = actual - expected;

    if (difference < 0.0f)
        difference = -difference;
    if (difference > 0.004f)
        ++failures;
}

int main(void)
{
    check_close(fixture_asin(-1.0f), -1.57079633f);
    check_close(fixture_asin(-0.9f), -1.11976951f);
    check_close(fixture_asin(-0.75f), -0.84806208f);
    check_close(fixture_asin(-0.5f), -0.52359878f);
    check_close(fixture_asin(-0.25f), -0.25268026f);
    check_close(fixture_asin(0.0f), 0.0f);
    check_close(fixture_asin(0.25f), 0.25268026f);
    check_close(fixture_asin(0.5f), 0.52359878f);
    check_close(fixture_asin(0.75f), 0.84806208f);
    check_close(fixture_asin(0.9f), 1.11976951f);
    check_close(fixture_asin(1.0f), 1.57079633f);
    check_close(fixture_asin(1.25f), 0.0f);
    printf("float asin failures=%d\n", failures);
    return failures != 0;
}
