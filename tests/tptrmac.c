/* Compiler-provided pointer-difference type and GNU diagnostic marker,
 * adapted from SDCC's GCC torture execute test 930930-1.c. */
#include <stdio.h>

__extension__ typedef __PTRDIFF_TYPE__ compiler_ptrdiff_t;

static compiler_ptrdiff_t distance(int *left, int *right)
{
    return left - right;
}

int main(void)
{
    int values[6];

    if (sizeof(compiler_ptrdiff_t) != sizeof(int) ||
        distance(values + 5, values + 1) != 4 ||
        distance(values + 1, values + 5) != -4) {
        printf("tptrmac failed\n");
        return 1;
    }

    printf("tptrmac completed with great success\n");
    return 0;
}
