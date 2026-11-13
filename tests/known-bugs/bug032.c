/* BUG-032: with dccpeep, a 16-bit local plus a constant from 2 to 255 that
 * is then copied to another slot loses the sum: the copy stores whatever HL
 * held before. dccpeep's small_const_incr_carry_skip pass turns the add into
 * an 8-bit add on the stack slot, which no longer leaves the result in HL,
 * but the following `ld (ix-N),l` / `ld (ix-M),h` still store HL. The
 * program is correct without dccpeep (dcc-peep=false). */
#include <stdint.h>
#include <stdio.h>

static uint32_t g6 = 256;
static uint16_t g16 = 256;
static uint8_t g8 = 69;
static int16_t sink;

static uint16_t conditional(int16_t x)
{
    x = x;
    return (g6 <= 1) ? g8 : (uint16_t)(x + 200);
}

static uint16_t if_else(int16_t x)
{
    uint16_t r;
    x = x;
    if ((1 * g6) <= 1)
        r = g8;
    else
        r = (uint16_t)(x + 200);
    return r;
}

static uint16_t add_255(int16_t x)
{
    x = x;
    return ((g6 * g6) <= 1) ? g8 : (uint16_t)(x + 255);
}

/* the sum is the operand of a helper call (a division by a constant) that
 * expects it in HL; it needs the loop and a global destination */
static int16_t compound_then_divide(void)
{
    int16_t l = 1;
    uint8_t w = 0;
    while ((w++) < 1)
        sink = (l += 225) / 121;
    return sink;
}

static int16_t compound_then_modulo(void)
{
    int16_t l = 1;
    uint8_t w = 0;
    while ((w++) < 1)
        sink = (l += 225) % 121;
    return sink;
}

/* correct: 256 does not fit the 8-bit add */
static uint16_t add_256(int16_t x)
{
    x = x;
    return ((g6 * g6) <= 1) ? g8 : (uint16_t)(x + 256);
}

/* correct: a 16-bit comparison leaves no helper call before the add */
static uint16_t narrow_test(int16_t x)
{
    x = x;
    return (g16 <= 1) ? g8 : (uint16_t)(x + 200);
}

int main(void)
{
    printf("conditional %u %u\n", (unsigned)conditional(1), (unsigned)conditional(100));
    printf("if else %u %u\n", (unsigned)if_else(1), (unsigned)if_else(100));
    printf("add 255 %u %u\n", (unsigned)add_255(1), (unsigned)add_255(100));
    printf("add 256 %u %u\n", (unsigned)add_256(1), (unsigned)add_256(100));
    printf("narrow test %u %u\n", (unsigned)narrow_test(1), (unsigned)narrow_test(100));
    printf("compound divide %d\n", (int)compound_then_divide());
    printf("compound modulo %d\n", (int)compound_then_modulo());
    return 0;
}
