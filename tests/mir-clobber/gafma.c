#include <stdio.h>

#ifdef GAFMA_VOLATILE_ARRAY
static volatile float values[6];
#else
static float values[6];
#endif

#ifdef GAFMA_RENAMED
#define global_array_fma global_array_fma_renamed
#endif

#if defined(GAFMA_NARROW_INDEX)
typedef char index_type;
#elif defined(GAFMA_UNSIGNED_INDEX)
typedef unsigned int index_type;
#else
typedef int index_type;
#endif

float global_array_fma(float left, float right, float addend,
                       index_type index)
{
#ifdef GAFMA_EXTRA_CFG
    if (index < 0)
        return addend;
#endif
    values[index] = addend;
    values[index] += left * right;
    return values[index];
}

static int failures;

static void check_case(float left, float right, float addend,
                       index_type index, float expected)
{
    float result;

    values[0] = 101.0f;
    values[1] = 102.0f;
    values[2] = 103.0f;
    values[3] = 104.0f;
    values[4] = 105.0f;
    values[5] = 106.0f;
    result = global_array_fma(left, right, addend, index);
    if (result != expected || values[index] != expected ||
        values[0] != 101.0f || values[5] != 106.0f)
        ++failures;
}

int main(void)
{
    check_case(2.0f, 3.0f, 4.0f, 1, 10.0f);
    check_case(-2.0f, 3.0f, 1.0f, 2, -5.0f);
    check_case(0.5f, 8.0f, -1.0f, 3, 3.0f);
    check_case(1.25f, -4.0f, 7.0f, 4, 2.0f);
    printf("global array fma failures=%d\n", failures);
    return failures;
}
