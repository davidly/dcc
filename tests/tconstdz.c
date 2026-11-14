/* Guarded constant-zero division and remainder retain runtime operations. */
#include <stdint.h>
#include <stdio.h>

static int16_t never = 0;
static int16_t out;

static int16_t in_return(void)
{
    if (never)
        return 1000 / 0;
    return 1;
}

static void in_assignment(void)
{
    if (never)
        out = 7 % 0;
    else
        out = 2;
}

int main(void)
{
    in_assignment();
    printf("%d %d\n", in_return(), out);
    return 0;
}
