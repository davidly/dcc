#include <stdio.h>

static inline int helper_add(int a, int b)
{
    return a + b;
}

static inline int helper_sub(int a, int b)
{
    return a - b;
}

static inline int scale3(int x);

static inline int scale3(int x)
{
    return helper_add(x, helper_add(x, x));
}

int main(void)
{
    printf("static inline: %d %d\n", scale3(7), helper_sub(helper_add(10, 5), 3));
    return 0;
}