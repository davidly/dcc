#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#ifdef PIDIGIT_RENAMED
#define PI_DIGIT_FUNCTION renamed_pi_digit_fixture
#else
#define PI_DIGIT_FUNCTION pi_digit_fixture
#endif

float nmfpart(float value)
{
    float result;

    assert(value < 2.0);
    assert(value >= 0.0);
    if (value >= 1.0)
        result = value - 1.0;
    else
        result = value;
    return result;
}

float fpart(float value)
{
    float result = fmodf(value, 1.0);

    if (result < 0.0)
        result += 1.0;
    return result;
}

float eps(float value)
{
    return nextafterf(value, FLT_MAX) - value;
}

uint16_t powermod16(uint16_t exponent, uint16_t modulus)
{
    uint16_t result;
    uint16_t base;

    if (modulus == 1)
        return 0;
    if (exponent == 0)
        return 1;
    result = 1;
    base = 16 % modulus;
    do {
        if (exponent & 1)
            result =
                ((uint32_t)result * (uint32_t)base) % modulus;
        exponent >>= 1;
        if (exponent == 0)
            return result;
        base = ((uint32_t)base * (uint32_t)base) % modulus;
    } while (1);
}

float fun(uint16_t n, uint16_t j)
{
    float sum = 0.0;
    uint16_t denominator = j;
    uint16_t k;
    float numerator;
    float float_denominator;
    float fraction;

    for (k = 0; k <= n; ++k) {
        uint16_t power = powermod16(n - k, denominator);
        float next =
            sum + ((float)power / (float)denominator);

        sum = nmfpart(next);
        denominator += 8;
    }
    numerator = 1.0 / 16.0;
    float_denominator = (float)denominator;
    while ((fraction = numerator / float_denominator) > eps(sum)) {
        sum += fraction;
        numerator /= 16.0;
        float_denominator += 8.0;
    }
    return nmfpart(sum);
}

int PI_DIGIT_FUNCTION(uint16_t n)
{
    float sum =
        (4.0 * fun(n, 1)) - (2.0 * fun(n, 4)) -
        fun(n, 5) - fun(n, 6);
    float f = fpart(sum);
    float r = 16.0 * f;
    int x = (int)r;

#ifdef PIDIGIT_ALTERNATE_BOUND
    assert(x <= 15 && x >= 0);
#else
    assert(x >= 0 && x <= 15);
#endif
    return x;
}

int main(void)
{
    static const char expected[] = "243f6a88";
    char actual[9];
    int failures = 0;
    uint16_t index;

    for (index = 0; index < 8; ++index) {
        int digit = PI_DIGIT_FUNCTION(index);

        actual[index] = digit < 10
            ? (char)('0' + digit)
            : (char)('a' + digit - 10);
        if (actual[index] != expected[index])
            ++failures;
    }
    actual[8] = 0;
    printf(
        "pi digit failures=%d digits=%s\n",
        failures, actual);
    return failures;
}
