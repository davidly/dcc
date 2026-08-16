#include <stdio.h>

static int vreduce(int n)
{
    int a[n];
    int *p = a;
    int *q = a + (n - 1);

    a[0] = 5;
    a[n - 1] = 9;
    return (int)(q - p) + *q - *p;
}

int main(void)
{
    printf("small=%d,%d\n", vreduce(1), vreduce(4));
    printf("large=%d\n", vreduce(30000));
    return 0;
}
