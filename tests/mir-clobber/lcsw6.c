#include <stdio.h>

#define MAXLEN 8

static int lcs_wave6(const char *a, const char *b)
{
    int table[MAXLEN + 1][MAXLEN + 1];
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
            table[i][j] = a[i - 1] == b[j - 1]
                ? table[i - 1][j - 1] + 1
                : (table[i - 1][j] > table[i][j - 1]
                    ? table[i - 1][j]
                    : table[i][j - 1]);

    return table[na][nb];
}

static int run_case(
    const char *name, char *left, char *right, int expected, int weight,
    int *hash)
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
    char empty1[11] = {'L', 0, 0, 0, 0, 0, 0, 0, 0, 0, 'R'};
    char empty2[11] = {'L', 0, 0, 0, 0, 0, 0, 0, 0, 0, 'R'};
    char equal1[11] = {'L', 'Z', '8', '0', 'C', 'P', 'M', 0, 0, 0, 'R'};
    char equal2[11] = {'L', 'Z', '8', '0', 'C', 'P', 'M', 0, 0, 0, 'R'};
    char disjoint1[11] = {'L', 'A', 'B', 'C', 'D', 0, 0, 0, 0, 0, 'R'};
    char disjoint2[11] = {'L', 'W', 'X', 'Y', 'Z', 0, 0, 0, 0, 0, 'R'};
    char sample1[11] = {'L', 'A', 'B', 'C', 'D', 'G', 'H', 0, 0, 0, 'R'};
    char sample2[11] = {'L', 'A', 'E', 'D', 'F', 'H', 'R', 0, 0, 0, 'R'};
    char mixed1[11] = {'L', 'X', 'M', 'J', 'Y', 'A', 'U', 'Z', 0, 0, 'R'};
    char mixed2[11] = {'L', 'M', 'Z', 'J', 'A', 'W', 'X', 'U', 0, 0, 'R'};
    int failures = 0;
    int hash = 0;

    failures += run_case("empty", empty1, empty2, 0, 1, &hash);
    failures += run_case("equal", equal1, equal2, 6, 3, &hash);
    failures += run_case("disjoint", disjoint1, disjoint2, 0, 5, &hash);
    failures += run_case("sample", sample1, sample2, 3, 7, &hash);
    failures += run_case("mixed", mixed1, mixed2, 4, 11, &hash);
    printf("LCSW6 oracle hash=%d failures=%d\n", hash, failures);
    return failures != 0;
}
