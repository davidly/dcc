#include <stdio.h>

static unsigned int oracle_hash = 193U;
static int failures;

static int alias_sum(char *p, int n)
{
    char **pp;
    int i;
    int total;

    pp = &p;
    total = 0;
    for (i = 0; i < n; i++)
        total = total + (*pp)[i];
    return total;
}

static int reference_sum(const char *p, int n)
{
    int total = 0;

    while (n > 0) {
        total += *p++;
        --n;
    }
    return total;
}

static void mix(unsigned int value)
{
    oracle_hash =
        (unsigned int)(oracle_hash * 109U + value + 37U);
}

static void check_case(
    const char *name, char *p, int n, int want,
    int left_guard, int right_guard)
{
    int got = alias_sum(p, n);
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
        91, -128, -7, 0, 1, 2, 126, 63, -32, 5, -11, -87
    };
    char positive_values[10] = {
        73, 1, 2, 3, 4, 5, 6, 7, 8, -74
    };
    char *view;

    view = signed_values + 1;
    check_case("alias-full", view, 10, 19,
               signed_values[0], signed_values[11]);
    view = signed_values + 2;
    check_case("alias-sub", view, 5, 122,
               signed_values[1], signed_values[7]);
    view = signed_values + 4;
    check_case("alias-zero", view, 0, 0,
               signed_values[3], signed_values[4]);
    view = signed_values + 6;
    check_case("alias-negative", view, -3, 0,
               signed_values[5], signed_values[6]);
    view = signed_values + 1;
    check_case("alias-min", view, 1, -128,
               signed_values[0], signed_values[2]);
    view = positive_values + 1;
    check_case("bounds-positive", view, 8, 36,
               positive_values[0], positive_values[9]);

    printf("ALSUM5 oracle hash=%u failures=%d guards=%d,%d,%d,%d\n",
           oracle_hash, failures,
           signed_values[0], signed_values[11],
           positive_values[0], positive_values[9]);
    return failures != 0;
}
