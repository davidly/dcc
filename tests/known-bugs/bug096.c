/* BUG-096: an enum declared inside a nested block is rejected as a
 * "duplicate enum constant", because the block's declarations are parsed
 * again after the frame-sizing scan and enum constants have no block scope. */
#include <stdio.h>

static int in_case(int x)
{
    switch (x) {
    case 1: {
        enum { A = 4, B } e = B;
        x = A + e;
        break;
    }
    }
    return x;
}

static int in_block(int x)
{
    {
        enum { C = 4, D } e = D;
        x = C + e;
    }
    return x;
}

int main(void)
{
    printf("%d %d\n", in_case(1), in_block(0));
    return 0;
}
