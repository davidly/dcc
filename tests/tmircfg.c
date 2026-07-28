#include <stdio.h>

static int sum_to(int n)
{
    int sum = 0;

    while (n > 0) {
        sum += n;
        n -= 2;
    }
    return sum;
}

int main(void)
{
    printf("tmircfg %d %d\n", sum_to(5), sum_to(10));
    return 0;
}
