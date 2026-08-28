/*
 * A block-scope extern declaration has external linkage even when it shadows
 * an automatic object in an enclosing block.  Found via GCC torture scope-1.
 */
#include <stdio.h>

int value = 3;

static int read_value(void)
{
    int value = 4;
    {
        extern int value;
        return value;
    }
}

int main(void)
{
    int got = read_value();
    if (got != 3) {
        printf("tscpext failed: got=%d\n", got);
        return 1;
    }
    printf("tscpext completed with great success\n");
    return 0;
}
