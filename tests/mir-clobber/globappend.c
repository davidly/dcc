/* Exhaustive exact-schedule fixture for global scalar append operations. */
#include <stdio.h>

#ifdef GLOBAPPEND_RENAMED
#define fixture_global_append_direct fixture_global_append_direct_renamed
#define fixture_global_append_binary fixture_global_append_binary_renamed
#endif

#ifdef GLOBAPPEND_VOLATILE_ARRAY
static volatile int append_values[16];
#else
static int append_values[16];
#endif

#ifdef GLOBAPPEND_VOLATILE_COUNT
static volatile int append_count;
#else
static int append_count;
#endif

#ifdef GLOBAPPEND_UNSIGNED_PARAMETER
static void fixture_global_append_direct(unsigned int value)
#else
static void fixture_global_append_direct(int value)
#endif
{
#ifdef GLOBAPPEND_EXTRA_CFG
    if (value == 12345)
        return;
#endif
#ifdef GLOBAPPEND_INCREMENT_TWO
    append_values[append_count] = value;
    append_count += 2;
#else
    append_values[append_count++] = value;
#endif
}

static void fixture_global_append_binary(int left, int right)
{
#if defined(GLOBAPPEND_BINARY_ADD)
    append_values[append_count++] = left + right;
#elif defined(GLOBAPPEND_BINARY_AND)
    append_values[append_count++] = left & right;
#elif defined(GLOBAPPEND_BINARY_OR)
    append_values[append_count++] = left | right;
#elif defined(GLOBAPPEND_BINARY_XOR)
    append_values[append_count++] = left ^ right;
#elif defined(GLOBAPPEND_BINARY_MULTIPLY)
    append_values[append_count++] = left * right;
#else
    append_values[append_count++] = left - right;
#endif
}

static int binary_expected(int left, int right)
{
#if defined(GLOBAPPEND_BINARY_ADD)
    return left + right;
#elif defined(GLOBAPPEND_BINARY_AND)
    return left & right;
#elif defined(GLOBAPPEND_BINARY_OR)
    return left | right;
#elif defined(GLOBAPPEND_BINARY_XOR)
    return left ^ right;
#elif defined(GLOBAPPEND_BINARY_MULTIPLY)
    return left * right;
#else
    return left - right;
#endif
}

int main(void)
{
    int failures = 0;
    int checksum = 0;
    int expected[6];
    int index;

    append_count = 0;
    fixture_global_append_direct(17);
    fixture_global_append_binary(31, 12);
    fixture_global_append_direct(-9);
    fixture_global_append_binary(-7, 5);
    fixture_global_append_direct(0);
    fixture_global_append_binary(0x55, 0x0f);

    expected[0] = 17;
    expected[1] = binary_expected(31, 12);
    expected[2] = -9;
    expected[3] = binary_expected(-7, 5);
    expected[4] = 0;
    expected[5] = binary_expected(0x55, 0x0f);
    if (append_count != 6)
        failures++;
    for (index = 0; index < 6; ++index) {
        if (append_values[index] != expected[index])
            failures++;
        checksum += append_values[index] * (index + 3);
    }
    printf("global append failures=%d count=%d checksum=%d\n",
           failures, append_count, checksum);
    return failures != 0;
}
