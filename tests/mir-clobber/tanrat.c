#include <stdio.h>
#include <math.h>

static int failures;

#ifdef TANRAT_VARIADIC_REMAINDER
static float tanrat_remainder(float x, float y, ...)
{
    return fmodf(x, y);
}
#define TANRAT_REMAINDER tanrat_remainder
#else
#define TANRAT_REMAINDER fmodf
#endif

static void check_close(float actual, float expected, float tolerance)
{
    float difference = actual - expected;

    if (difference < 0.0f)
        difference = -difference;
    if (difference > tolerance)
        ++failures;
}

float tanrat(float x)
{
    const float PI = 3.14159265f;
    const float HALF_PI = 1.57079632f;
    const float QUARTER_PI = 0.78539816f;
    int invert = 0;

    float x_reduced = TANRAT_REMAINDER(x, PI);
#ifdef TANRAT_OPCODE_NEAR_MATCH
    x_reduced += 0.0f;
#endif
    if (x_reduced > HALF_PI) {
        x_reduced -= PI;
    } else if (x_reduced < -HALF_PI) {
        x_reduced += PI;
    }

    if (x_reduced > QUARTER_PI) {
        x_reduced = HALF_PI - x_reduced;
        invert = 1;
    } else if (x_reduced < -QUARTER_PI) {
        x_reduced = -HALF_PI - x_reduced;
        invert = 1;
    }

    float x2 = x_reduced * x_reduced;
    float num = x_reduced * (15.0f - x2);
    float den = 15.0f - 6.0f * x2;

    if (den == 0.0f) return 0.0f;
    float result = num / den;

    if (invert) {
        if (result == 0.0f) return 0.0f;
        result = 1.0f / result;
    }

    return result;
}

int main(void)
{
    check_close(tanrat(0.0f), 0.0f, 0.0001f);
    check_close(tanrat(0.5f), 0.54630249f, 0.001f);
    check_close(tanrat(-0.5f), -0.54630249f, 0.001f);
    check_close(tanrat(1.0f), 1.55740772f, 0.003f);
    check_close(tanrat(-1.0f), -1.55740772f, 0.003f);
    check_close(tanrat(2.0f), -2.18503986f, 0.006f);
    check_close(tanrat(3.5f), 0.37458564f, 0.002f);
    printf("float tangent failures=%d\n", failures);
    return failures != 0;
}
