/* Predefined target byte width, adapted from SDCC's GCC torture execute test
 * 20060102-1.c. */
#include <stdio.h>

#if __CHAR_BIT__ != 8
#error __CHAR_BIT__ must describe dcc's eight-bit bytes
#endif

static int sign_result(int value)
{
    return (value >> (sizeof(value) * __CHAR_BIT__ - 1)) ? -1 : 1;
}

int main(void)
{
    volatile int one = 1;

    if (sign_result(one) == sign_result(-one)) {
        printf("tcharbit failed\n");
        return 1;
    }

    printf("tcharbit completed with great success\n");
    return 0;
}
