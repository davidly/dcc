/* BUG-034: with dccpeep, `8 == v` can become a test of bit 3. dccpeep's
 * cp8_to_bit3 rewrite turns `ld a,(ix+K) / cp 8 / jp nz,L` into
 * `bit 3,(ix+K) / jp z,L`, which is only the same thing for values 0 to 8
 * (its own comment says so). Any other value with bit 3 set, such as 9, 12
 * or 47, then compares equal to 8. The program is correct without dccpeep
 * (dcc-peep=false). */
#include <stdint.h>
#include <stdio.h>

static uint8_t g8;
static struct { int16_t m : 5; } bf;

static int16_t equal(uint16_t x)
{
    if (8 == (uint16_t)g8) {
        x = 100;
    }
    return x;
}

static int16_t not_equal(uint16_t x)
{
    if (8 != (uint16_t)g8) {
        x = 1;
    }
    return x;
}

/* correct: 16 is not rewritten */
static int16_t sixteen(uint16_t x)
{
    if (16 == (uint16_t)g8) {
        x = 100;
    }
    return x;
}

/* wrong too: a signed bit-field read through a cast */
static int16_t bitfield_equal(uint16_t x)
{
    if (8 == (int16_t)bf.m) {
        x = 100;
    }
    return x;
}

int main(void)
{
    static const uint8_t values[6] = { 8, 7, 9, 12, 47, 16 };
    uint8_t i;
    for (i = 0; i < 6; i++) {
        g8 = values[i];
        bf.m = (int16_t)(values[i] & 15);
        printf("bit-field %d: %d\n", (int)bf.m, bitfield_equal(85));
        printf("%u: equal %d not equal %d sixteen %d\n", (unsigned)g8,
               equal(85), not_equal(85), sixteen(85));
    }
    return 0;
}
