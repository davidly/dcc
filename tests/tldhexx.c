/* A byte loop counter passed as a 16-bit argument after a wider argument.
 * dcc builds the second argument in the alternate registers (exx; ld l,..;
 * ld h,0; push hl; exx) right after zero-extending the first in HL. The
 * peephole pass that drops a redundant "ld h,0" did not count exx as
 * changing H, so it deleted the zero-extend and pushed H' (left nonzero by
 * printf) as the high byte: 3 > 127 came out true. */
#include <stdio.h>
#include <stdint.h>

int fails;
uint16_t wide = 0x1234;

struct S1 { uint32_t m0[4]; uint16_t m1 : 13; int16_t m2 : 2; };
int16_t a1[8] = { 7, 127, 32766, 4336, -1, 8, 32767, 100 };
static uint8_t a5[4][4] = { { 255, 253, 129, 208 }, { 189, 239, 57, 95 },
                            { 69, 189, 85, 255 }, { 134, 42, 2, 128 } };
static int8_t a6[8];
static struct S1 s2[4] = { { { 1 }, 564, 1 }, { { 5 }, 3360, -1 },
                           { { 5 }, 4749, -2 }, { { 3 }, 5340, 0 } };

static int8_t above(int32_t x0, uint16_t x1)
{
    return x1 > ((uint16_t)a1[1]);
}

uint16_t member_cmp(uint16_t x0)
{
    return ((uint16_t)1) >= (x0 = s2[((uint32_t)257) & 3].m2);
}

uint32_t mark(void)
{
    for (uint8_t i = 3; i < 6; i++) {
        a6[above(a5[3][1], i) & 7] = ((uint32_t)1);
    }
    return 1;
}

int main(void)
{
    int i;

    printf("member_cmp %u\n", member_cmp(16));
    if (above(a5[3][1], wide) != 1)
        ++fails;
    mark();
    for (i = 0; i < 8; i++)
        if (a6[i] != (i == 0)) {
            printf("a6[%d] = %d, expected %d\n", i, a6[i], i == 0);
            ++fails;
        }
    if (fails)
        printf("tldhexx failed %d\n", fails);
    else
        printf("tldhexx passed\n");
    return 0;
}
