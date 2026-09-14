#include <stdio.h>

static int failures;

#ifdef FATAN2_VARIADIC_ATAN
static float fixture_atan(float x, ...)
#else
static float fixture_atan(float x)
#endif
{
    float sign = 1.0f;
    float bias = 0.0f;
    float x2;
    float polynomial;

    if (x < 0.0f) {
        sign = -1.0f;
        x = -x;
    }
    if (x > 2.41421356f) {
        bias = 1.57079632f;
        x = -1.0f / x;
    } else if (x > 0.41421356f) {
        bias = 0.78539816f;
        x = (x - 1.0f) / (x + 1.0f);
    }
    x2 = x * x;
    polynomial = x + x * x2 *
        (-0.33333145f + x2 *
         (0.19993550f + x2 * (-0.14208899f + x2 * 0.10656263f)));
    return sign * (bias + polynomial);
}

#ifdef FATAN2_DIFFERENT_ATAN
static float alternate_atan(float x)
{
    return fixture_atan(x);
}
#define FATAN2_NEGATIVE_ATAN alternate_atan
#else
#define FATAN2_NEGATIVE_ATAN fixture_atan
#endif

#ifdef FATAN2_VOLATILE_X
static float fixture_atan2(float y, volatile float x)
#else
static float fixture_atan2(float y, float x)
#endif
{
    const float pi = 3.14159265f;
    const float half_pi = 1.57079632f;

    if (x == 0.0f) {
        if (y > 0.0f)
            return half_pi;
        if (y < 0.0f)
            return -half_pi;
        return 0.0f;
    }

#ifdef FATAN2_VOLATILE_RATIO
    volatile float ratio = y / x;
#else
    float ratio = y / x;
#endif

    if (x > 0.0f) {
        return fixture_atan(ratio);
    } else {
        if (y >= 0.0f) {
            return fixture_atan(ratio) + pi;
        } else {
            return FATAN2_NEGATIVE_ATAN(ratio) - pi;
        }
    }
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
    check_close(fixture_atan2(0.0f, 0.0f), 0.0f);
    check_close(fixture_atan2(1.0f, 0.0f), 1.57079633f);
    check_close(fixture_atan2(-1.0f, 0.0f), -1.57079633f);
    check_close(fixture_atan2(1.0f, 1.0f), 0.78539816f);
    check_close(fixture_atan2(-1.0f, 1.0f), -0.78539816f);
    check_close(fixture_atan2(1.0f, -1.0f), 2.35619449f);
    check_close(fixture_atan2(-1.0f, -1.0f), -2.35619449f);
    check_close(fixture_atan2(2.0f, 3.0f), 0.58800260f);
    check_close(fixture_atan2(2.0f, -3.0f), 2.55359005f);
    printf("float atan2 failures=%d\n", failures);
    return failures != 0;
}
