/* Byte and word values merged from opposite if/else arms remain distinct
 * through byte widening and negation, including two byte PHIs and an arm
 * assigning both values. No-cast and 16-bit controls cover adjacent PHIs. */
#include <stdint.h>
#include <stdio.h>

static int16_t sel;

static uint16_t minus(uint16_t x)
{
    int8_t l = 0;
    if (sel) {
        x = 1;
    } else {
        l = 100;
    }
    return (uint16_t)((int16_t)l - x);
}

static uint16_t plus_char(uint16_t x)
{
    char c = 0;
    if (sel) {
        c = '-';
        x = 5;
    } else {
        c = '+';
    }
    return (int16_t)c + x;
}

static uint16_t two_bytes(void)
{
    int8_t l1 = 0;
    int8_t l2 = 5;
    if (sel) {
        l2 = 1;
    } else {
        l1 = 100;
    }
    return (uint16_t)((int16_t)l1 - (int16_t)l2);
}

static uint16_t negated(uint16_t x)
{
    int8_t l = 0;
    if (sel) {
        x = 1;
    } else {
        l = 100;
    }
    return (uint16_t)(-l - x);
}

/* correct: no cast */
static uint16_t no_cast(uint16_t x)
{
    int8_t l = 0;
    if (sel) {
        x = 1;
    } else {
        l = 100;
    }
    return (uint16_t)(l - x);
}

/* correct: a 16-bit local */
static uint16_t sixteen_bits(uint16_t x)
{
    int16_t l = 0;
    if (sel) {
        x = 1;
    } else {
        l = 100;
    }
    return (uint16_t)((int16_t)l - x);
}

int main(void)
{
    for (sel = 0; sel < 2; sel++) {
        printf("sel %d: minus %u plus %u two bytes %u negated %u\n", (int)sel,
               (unsigned)minus(32767), (unsigned)plus_char(32767),
               (unsigned)two_bytes(), (unsigned)negated(32767));
        printf("sel %d: no cast %u 16 bits %u\n", (int)sel,
               (unsigned)no_cast(32767), (unsigned)sixteen_bits(32767));
    }
    return 0;
}
