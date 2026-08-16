#include <stdio.h>

long sum_nonzero(char *p, int n)
{
    int total;
    int i;

    total = 0;
    for (i = 0; i < n; i++)
        if (p[i] != 0)
            total = total + p[i];
    return (long)total + 65536L;
}

int main(void)
{
    char buf[8];
    int i;

    for (i = 0; i < 8; i++)
        buf[i] = i + 1;
    buf[3] = 0;

    printf("sum=%ld\n", sum_nonzero(buf, 8));
    return 0;
}
