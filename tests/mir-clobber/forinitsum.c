/* Exhaustive exact-schedule fixture for a for-initializer sum loop. */
#include <stdio.h>

#ifdef FORINITSUM_RENAMED
#define fixture_for_init_sum fixture_for_init_sum_renamed
#endif

#ifdef FORINITSUM_UNSIGNED_PARAMETER
static int fixture_for_init_sum(unsigned int n)
#else
static int fixture_for_init_sum(int n)
#endif
{
#ifdef FORINITSUM_LONG_SUM
    long t = 0;
#elif defined(FORINITSUM_VOLATILE_SUM)
    volatile int t = 0;
#else
    int t = 0;
#endif
    int i = 0;

#ifdef FORINITSUM_EXTRA_CFG
    if (n == 12345)
        return -1;
#endif
#ifdef FORINITSUM_ZERO_START
    for (i = 1; i <= n; i++)
#else
    for (++i; i <= n; ++i)
#endif
        t += i;
    return t;
}

static int expected_sum(int limit)
{
    if (limit < 1)
        return 0;
    return limit * (limit + 1) / 2;
}

static int check_case(int limit)
{
    int expected = expected_sum(limit);

    return fixture_for_init_sum(limit) != expected;
}

int main(void)
{
    int failures = 0;
    int checksum = 0;

#ifndef FORINITSUM_UNSIGNED_PARAMETER
    failures += check_case(-3);
    checksum += fixture_for_init_sum(-3) * 3;
#endif
    failures += check_case(0);
    failures += check_case(1);
    failures += check_case(5);
    failures += check_case(10);
    checksum += fixture_for_init_sum(1) * 5;
    checksum += fixture_for_init_sum(5) * 7;
    checksum += fixture_for_init_sum(10) * 11;
    printf("for init sum failures=%d checksum=%d\n", failures, checksum);
    return failures != 0;
}
