#include <stdio.h>

static int failures;

static void check(const char *name, long actual, long expected)
{
    if (actual != expected) {
        printf("FAIL %s: %ld expected %ld\n", name, actual, expected);
        ++failures;
    }
}

static long initialized(int count, long initial)
{
    long value = initial;
    while (count > 0) {
        value = value * 3 + count;
        --count;
    }
    return value;
}

static long body_defined(int count)
{
    long value;
    while (count > 0) {
        value = count * 11L;
        --count;
    }
    return value;
}

static long successive(int first, int second)
{
    long sum = 0;
    int index;
    for (index = 0; index < first; ++index) {
        long value = index + 1L;
        sum += value;
    }
    for (index = 0; index < second; ++index) {
        long value = 2L * (index + 1);
        sum += value;
    }
    return sum;
}

static long branch_loop(int count, int flag)
{
    long value = flag ? 7 : 13;
    while (count > 0) {
        if (count & 1)
            value += count;
        else
            value *= 2;
        --count;
    }
    return value;
}

int main(void)
{
    check("zero iterations", initialized(0, 17), 17);
    check("one iteration", initialized(1, 17), 52);
    check("three iterations", initialized(3, 2), 88);
    check("body one", body_defined(1), 11);
    check("body several", body_defined(4), 11);
    check("both empty", successive(0, 0), 0);
    check("first empty", successive(0, 3), 12);
    check("second empty", successive(3, 0), 6);
    check("both nonempty", successive(3, 3), 18);
    check("branch zero", branch_loop(0, 1), 7);
    check("branch loop", branch_loop(3, 0), 33);
    printf("MIR dominance failures=%d\n", failures);
    return failures != 0;
}