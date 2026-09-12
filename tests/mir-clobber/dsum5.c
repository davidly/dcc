#include <stdio.h>

static unsigned int oracle_hash = 811U;
static int failures;

static int direct_sum(char *p, int n)
{
    int total;
    int i;

    total = 0;
    for (i = 0; i < n; i++)
        if (p[i] != 0)
            total = total + p[i];
    return total;
}

static int reference_sum(const char *p, int n)
{
    int total = 0;

    while (n > 0) {
        if (*p != 0)
            total += *p;
        ++p;
        --n;
    }
    return total;
}

static void mix(unsigned int value)
{
    oracle_hash =
        (unsigned int)(oracle_hash * 113U + value + 41U);
}

static void check_case(
    const char *name, char *p, int n, int want,
    int left_guard, int right_guard)
{
    int got = direct_sum(p, n);
    int reference = reference_sum(p, n);

    printf("%s got=%d ref=%d want=%d guards=%d,%d\n",
           name, got, reference, want, left_guard, right_guard);
    mix((unsigned int)got);
    mix((unsigned int)reference);
    mix((unsigned int)want);
    mix((unsigned int)n);
    mix((unsigned int)left_guard);
    mix((unsigned int)right_guard);
    if (got != want || reference != want)
        ++failures;
}

int main(void)
{
    char signed_values[12] = {
        91, -128, 0, -7, 1, 0, 126, 63, -32, 5, -11, -87
    };
    char positive_values[10] = {
        73, 1, 0, 2, 0, 3, 4, 0, 5, -74
    };

    check_case("direct-full", signed_values + 1, 10, 17,
               signed_values[0], signed_values[11]);
    check_case("direct-sub", signed_values + 2, 5, 120,
               signed_values[1], signed_values[7]);
    check_case("direct-zero", signed_values + 4, 0, 0,
               signed_values[3], signed_values[4]);
    check_case("direct-negative", signed_values + 6, -3, 0,
               signed_values[5], signed_values[6]);
    check_case("direct-min", signed_values + 1, 1, -128,
               signed_values[0], signed_values[2]);
    check_case("direct-sparse", positive_values + 1, 8, 15,
               positive_values[0], positive_values[9]);

    printf("DSUM5 oracle hash=%u failures=%d guards=%d,%d,%d,%d\n",
           oracle_hash, failures,
           signed_values[0], signed_values[11],
           positive_values[0], positive_values[9]);
    return failures != 0;
}
