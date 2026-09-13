#include <stdio.h>

static unsigned long sink(unsigned long value)
{
    return value ^ 0x55aa33ccUL;
}

static unsigned long passthru(unsigned long value)
{
    return value;
}

static unsigned long callit(unsigned long value)
{
    return sink(value);
}

int main(void)
{
    unsigned long first = passthru(0x12345678UL);
    unsigned long second = callit(0x89abcdefUL);

    if (first != 0x12345678UL ||
        second != (0x89abcdefUL ^ 0x55aa33ccUL)) {
        printf("lazy wide failed %lu %lu\n", first, second);
        return 1;
    }
    printf("lazy wide passed\n");
    return 0;
}
