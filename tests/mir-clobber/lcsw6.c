#include <stdio.h>

#ifdef LCSW24_DIMENSION_9
#define MAXLEN 9
#else
#define MAXLEN 8
#endif

#ifdef LCSW24_MIXED_CHAR_SIGNEDNESS
typedef signed char lcsw24_left_char;
typedef unsigned char lcsw24_right_char;
#else
typedef char lcsw24_left_char;
typedef char lcsw24_right_char;
#endif

#ifdef LCSW24_UNSIGNED_RETURN
static unsigned int lcs_wave6(
#else
static int lcs_wave6(
#endif
#ifdef LCSW24_VOLATILE_INPUT
    const volatile lcsw24_left_char *a,
    const volatile lcsw24_right_char *b)
#else
    const lcsw24_left_char *a, const lcsw24_right_char *b)
#endif
{
#ifdef LCSW24_VOLATILE_TABLE
    volatile int table[MAXLEN + 1][MAXLEN + 1];
#elif defined(LCSW24_UNSIGNED_TABLE)
    unsigned int table[MAXLEN + 1][MAXLEN + 1];
#else
    int table[MAXLEN + 1][MAXLEN + 1];
#endif
    int i, j, na = 0, nb = 0;

    while (a[na])
        ++na;
    while (b[nb])
        ++nb;

    for (i = 0; i <= na; ++i)
        table[i][0] = 0;
    for (j = 0; j <= nb; ++j)
        table[0][j] = 0;

    for (i = 1; i <= na; ++i)
        for (j = 1; j <= nb; ++j)
#ifdef LCSW24_COMMUTED_EQUALITY
            table[i][j] = b[j - 1] == a[i - 1]
#else
            table[i][j] = a[i - 1] == b[j - 1]
#endif
                ? table[i - 1][j - 1] + 1
#ifdef LCSW24_GREATER_EQUAL_MAX
                : (table[i - 1][j] >= table[i][j - 1]
#else
                : (table[i - 1][j] > table[i][j - 1]
#endif
                    ? table[i - 1][j]
                    : table[i][j - 1]);

    return table[na][nb];
}

static int run_case(
    const char *name, lcsw24_left_char *left,
    lcsw24_right_char *right, int expected, int weight, int *hash)
{
    int actual = lcs_wave6(left + 1, right + 1);
    int guards = left[0] == 'L' && left[10] == 'R' &&
                 right[0] == 'L' && right[10] == 'R';

    printf("%s got=%d want=%d guards=%d\n",
           name, actual, expected, guards);
    *hash += actual * weight;
    return actual != expected || !guards;
}

int main(void)
{
    lcsw24_left_char empty1[11] =
        {'L', 0, 0, 0, 0, 0, 0, 0, 0, 0, 'R'};
    lcsw24_right_char empty2[11] =
        {'L', 0, 0, 0, 0, 0, 0, 0, 0, 0, 'R'};
    lcsw24_left_char equal1[11] =
        {'L', 'Z', '8', '0', 'C', 'P', 'M', 0, 0, 0, 'R'};
    lcsw24_right_char equal2[11] =
        {'L', 'Z', '8', '0', 'C', 'P', 'M', 0, 0, 0, 'R'};
    lcsw24_left_char disjoint1[11] =
        {'L', 'A', 'B', 'C', 'D', 0, 0, 0, 0, 0, 'R'};
    lcsw24_right_char disjoint2[11] =
        {'L', 'W', 'X', 'Y', 'Z', 0, 0, 0, 0, 0, 'R'};
    lcsw24_left_char sample1[11] =
        {'L', 'A', 'B', 'C', 'D', 'G', 'H', 0, 0, 0, 'R'};
    lcsw24_right_char sample2[11] =
        {'L', 'A', 'E', 'D', 'F', 'H', 'R', 0, 0, 0, 'R'};
    lcsw24_left_char mixed1[11] =
        {'L', 'X', 'M', 'J', 'Y', 'A', 'U', 'Z', 0, 0, 'R'};
    lcsw24_right_char mixed2[11] =
        {'L', 'M', 'Z', 'J', 'A', 'W', 'X', 'U', 0, 0, 'R'};
#ifdef LCSW24_MIXED_CHAR_SIGNEDNESS
    lcsw24_left_char high1[11] =
        {'L', (lcsw24_left_char)0xff, 0, 0, 0, 0, 0, 0, 0, 0, 'R'};
    lcsw24_right_char high2[11] =
        {'L', (lcsw24_right_char)0xff, 0, 0, 0, 0, 0, 0, 0, 0, 'R'};
#endif
    int failures = 0;
    int hash = 0;

    failures += run_case("empty", empty1, empty2, 0, 1, &hash);
    failures += run_case("equal", equal1, equal2, 6, 3, &hash);
    failures += run_case("disjoint", disjoint1, disjoint2, 0, 5, &hash);
    failures += run_case("sample", sample1, sample2, 3, 7, &hash);
    failures += run_case("mixed", mixed1, mixed2, 4, 11, &hash);
#ifdef LCSW24_MIXED_CHAR_SIGNEDNESS
    failures += run_case("high-byte", high1, high2, 0, 13, &hash);
#endif
    printf("LCSW6 oracle hash=%d failures=%d\n", hash, failures);
    return failures != 0;
}
