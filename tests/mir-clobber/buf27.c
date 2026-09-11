#ifdef BUF27_GENERIC

#include <stdio.h>

static volatile unsigned char console_bytes[3];

static int emit_byte(unsigned char value)
{
    return putchar((int)value);
}

int main(void)
{
    int (*writer)(unsigned char) = emit_byte;
    unsigned char index;

    console_bytes[0] = 'O';
    console_bytes[1] = 'K';
    console_bytes[2] = '\n';
    for (index = 0; index < sizeof(console_bytes); ++index)
        if ((*writer)(console_bytes[index]) < 0)
            return 1;
    puts("buffered generic passed");
    return 0;
}

#else

#include "../tsvbuf2.c"

#endif
