/* Prefix decrement of an unsigned byte must promote its wrapped value. */
#include <stdio.h>

typedef unsigned char u8;
typedef unsigned long u32;

static u32 step(u8 d, u32 e, u32 g)
{
    do {
        e += g + 1;
        --d;
    } while (d >= (u8)e);
    return e;
}

int main(void)
{
    unsigned int shifted;
    signed char c = 0;

    shifted = ((unsigned int)(c ^ -1)) >> 9;
    if (step(1, -0x378704L, ~0xba64fcL) != 0xd93190d0UL ||
        shifted != (-1U >> 9)) {
        printf("tprewrap failed\n");
        return 1;
    }
    printf("tprewrap completed with great success\n");
    return 0;
}
