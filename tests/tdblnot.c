/*
 * tdblnot.c - double logical-not lifetime regression.
 * Generated archive case: batch10/c8919.
 */
#include <stdio.h>

static unsigned hash1(const char *text)
{
    unsigned hash = 0;

    while (*text)
        hash = hash * 31U + (unsigned char)*text++;
    return hash;
}

static unsigned hash2(const char *text)
{
    unsigned hash = 7;

    while (*text)
        hash = hash * 17U ^ (unsigned char)*text++;
    return hash;
}

static void add_word(unsigned char *bits, const char *text)
{
    unsigned first = hash1(text) % 32U;
    unsigned second = hash2(text) % 32U;

    bits[first / 8] |= (unsigned char)(1U << (first % 8));
    bits[second / 8] |= (unsigned char)(1U << (second % 8));
}

static int maybe_has_word(const unsigned char *bits, const char *text)
{
    unsigned first = hash1(text) % 32U;
    unsigned second = hash2(text) % 32U;

    return !!(bits[first / 8] & (1U << (first % 8))) &&
           !!(bits[second / 8] & (1U << (second % 8)));
}

int main(void)
{
    unsigned char bits[4] = { 0, 0, 0, 0 };
    int gamma;
    int delta;
    int ok;

    add_word(bits, "ALPHA");
    add_word(bits, "GAMMA");
    add_word(bits, "OMEGA");
    gamma = maybe_has_word(bits, "GAMMA");
    delta = maybe_has_word(bits, "DELTA");
    printf("tdblnot bloom=%d,%d,%02x%02x%02x%02x\n",
           gamma, delta, (unsigned)bits[0], (unsigned)bits[1],
           (unsigned)bits[2], (unsigned)bits[3]);
    ok = gamma && !delta && bits[0] == 0x8a && bits[1] == 0x00 &&
         bits[2] == 0x41 && bits[3] == 0x40;
    if (ok)
        printf("tdblnot passed with great success\n");
    return !ok;
}
