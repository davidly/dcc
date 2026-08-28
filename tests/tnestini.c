#include <stdio.h>

struct Pair {
    short first;
    short second;
};

struct Nested {
    unsigned flag : 1;
    char grid[2][2];
    struct Pair pairs[2];
};

static struct Nested value = {
    1,
    {{2, 3}, {4, 5}},
    {{6, 7}, {8, 9}}
};

int main(void)
{
    if (value.flag != 1)
        return 1;
    if (value.grid[0][0] != 2 || value.grid[0][1] != 3)
        return 2;
    if (value.grid[1][0] != 4 || value.grid[1][1] != 5)
        return 3;
    if (value.pairs[0].first != 6 || value.pairs[0].second != 7)
        return 4;
    if (value.pairs[1].first != 8 || value.pairs[1].second != 9)
        return 5;
    printf("tnestini completed with great success\n");
    return 0;
}
#include <stdio.h>
