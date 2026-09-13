#include <stdio.h>

typedef union {
    float value;
    unsigned long bits;
} FloatBits;

#ifdef FNORM16_UNSIGNED
#define EXP_TYPE unsigned int
#elif defined(FNORM16_VOLATILE)
#define EXP_TYPE volatile int
#else
#define EXP_TYPE int
#endif

static float fnorm16(float value, EXP_TYPE *exponent)
{
    *exponent = 0;
    if (value == 0.0f) {
        return 0.0f;
    }
    if (value < 0.0f) {
        return -fnorm16(-value, exponent);
    }
    while (value >= 1.0f) {
        value *= 0.5f;
        (*exponent)++;
    }
    while (value < 0.5f) {
        value *= 2.0f;
        (*exponent)--;
    }
    return value;
}

int main(void)
{
    FloatBits input;
    FloatBits output;
    float value;
    EXP_TYPE exponent;
    int failures = 0;

    value = fnorm16(15.75f, &exponent);
    if (value != 0.984375f || exponent != 4)
        ++failures;
    value = fnorm16(-15.75f, &exponent);
    if (value != -0.984375f || exponent != 4)
        ++failures;
    value = fnorm16(0.25f, &exponent);
    if (value != 0.5f || exponent != -1)
        ++failures;
    value = fnorm16(1.0f, &exponent);
    if (value != 0.5f || exponent != 1)
        ++failures;

    input.bits = 0x7f7fffffUL;
    output.value = fnorm16(input.value, &exponent);
    if (output.bits != 0x3f7fffffUL || exponent != 128)
        ++failures;
    input.bits = 0x00800000UL;
    output.value = fnorm16(input.value, &exponent);
    if (output.bits != 0x3f000000UL || exponent != -125)
        ++failures;
    input.bits = 0x80000000UL;
    output.value = fnorm16(input.value, &exponent);
    if (output.bits != 0 || exponent != 0)
        ++failures;
    input.bits = 0x7fc12345UL;
    output.value = fnorm16(input.value, &exponent);
    if (output.value == output.value || exponent != 0)
        ++failures;

    printf("FNORM16 failures=%d\n", failures);
    return failures != 0;
}
