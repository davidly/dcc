#include <stdio.h>

int test_1(int x)
{
    return x + 1;
}

int test_2(int x)
{
    return x + 2;
}

int safe_53(void)
{
    return 53;
}

int safe_60(void)
{
    return 60;
}

int main(void)
{
    int x;

    x = test_1(10) + test_2(20) + safe_53() + safe_60();
    printf("asm collision total %d\n", x);
    return 0;
}
