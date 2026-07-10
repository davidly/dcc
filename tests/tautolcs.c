#include <stdio.h>

#define MAXLEN 8

static int lcs(const char *a, const char *b)
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

int main(void)
{
    int actual = lcs("ABCDGH", "AEDFHR");
    printf("tautolcs lcs=%d\n", actual);
    return actual != 3;
}