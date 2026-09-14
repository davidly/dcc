#include <stdio.h>

typedef long wide_value_t;

static unsigned int random_state;
static int failures;

#ifdef RNDWIDE_RENAMED_HELPER
#define next_value renamed_value_source
#endif

#ifdef RNDWIDE_LONG_HELPER
static long next_value(void)
#else
static int next_value(void)
#endif
{
    random_state = random_state * 25173U + 13849U;
    return random_state & 0x7fffU;
}

#ifdef RNDWIDE_UNSIGNED_COUNT
typedef unsigned int fill_count_t;
#else
typedef int fill_count_t;
#endif

#ifdef RNDWIDE_VOLATILE_DESTINATION
static void fill_wide_values(
    volatile wide_value_t *values, fill_count_t count)
#else
static void fill_wide_values(wide_value_t *values, fill_count_t count)
#endif
{
#ifdef RNDWIDE_VOLATILE_INDEX
    volatile int i;
#else
    int i;
#endif
#ifdef RNDWIDE_VOLATILE_TEMP
    volatile int value;
#else
    int value;
#endif

    for (i = 0; i < count; ++i) {
        value = (next_value() & 255) - 128;
        values[i] = (wide_value_t)value * 256L;
    }
}

static void check_value(int index, long actual, long expected)
{
    if (actual != expected) {
        ++failures;
        printf("rndwide[%d]=%ld expected=%ld\n",
               index, actual, expected);
    }
}

int main(void)
{
    wide_value_t values[8];
    static const long expected[8] = {
        7168L, 25856L, -24064L, -7424L,
        30720L, -3840L, 7680L, 3840L
    };
    int i;

    random_state = 887U;
    fill_wide_values(values, 8);
    for (i = 0; i < 8; ++i)
        check_value(i, values[i], expected[i]);

    printf("random wide fill failures=%d\n", failures);
    return failures != 0;
}
