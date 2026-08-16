#include <stdio.h>

static int arbiter(int a, int b)
{
    int x;
    int y;
    int total;
    int i;

    x = a + 3;
    y = b - 2;
    total = 0;
    for (i = 0; i < 6; ++i) {
        total += x * y + i;
        x += 2;
        y -= 1;
    }
    return total;
}

int main(void)
{
    int got;

    got = arbiter(7, 12);
    printf("arbiter=%d\n", got);
    return got == 655 ? 0 : 1;
}
