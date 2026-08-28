#include <stdio.h>

static int add(left, right)
int left;
int right;
{
    return left + right;
}

static int apply(callback, left, right)
int (*callback)(int, int);
int left;
int right;
{
    return (*callback)(left, right);
}

int main(void)
{
    if (apply(add, 17, 25) != 42)
        return 1;
    printf("tkrfnptr completed with great success\n");
    return 0;
}
