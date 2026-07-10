#include <stdio.h>

static int true_calls;
static int false_calls;

static void hit_true(void) { true_calls++; }
static void hit_false(void) { false_calls++; }

static long choose_sum(long condition, long a, long b)
{
    long result;
    result = condition ? a + b : 0;
    return result;
}

static int choose_int(long condition)
{
    return condition ? 7 : -3;
}

static void choose_void(long condition)
{
    condition ? hit_true() : hit_false();
}

int main(void)
{
    long high_only = 65536L;
    long low_only = 1L;
    long zero = 0L;

    choose_void(high_only);
    choose_void(zero);
    choose_void(-65536L);
    printf("tlngcond high=%ld low=%ld zero=%ld neg=%d calls=%d,%d\n",
           choose_sum(high_only, 70000L, 9L),
           choose_sum(low_only, -80000L, 3L),
           choose_sum(zero, 11L, 12L), choose_int(-1L),
           true_calls, false_calls);
    return 0;
}
