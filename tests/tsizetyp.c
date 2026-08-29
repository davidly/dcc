/* Compiler-provided size type, exercised by several SDCC GCC-torture imports. */
#include <stdio.h>

typedef __SIZE_TYPE__ compiler_size_t;

static compiler_size_t identity_size(compiler_size_t size)
{
    return size;
}

int main(void)
{
    int values[3];
    compiler_size_t high = (compiler_size_t)-1;

    if (sizeof(compiler_size_t) != sizeof(unsigned int) ||
        high != 65535U || identity_size(sizeof(values)) != 6U) {
        printf("tsizetyp failed\n");
        return 1;
    }

    printf("tsizetyp completed with great success\n");
    return 0;
}
