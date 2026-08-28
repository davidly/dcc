#include <stdio.h>

static int scaled(float value)
{
    return value * 4.9f;
}

int main(void)
{
    if (scaled(10.0f) != 49)
        return 1;
    if (scaled(-2.0f) != -9)
        return 2;
    printf("tfret completed with great success\n");
    return 0;
}
