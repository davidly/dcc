/*
 * tpeepop.c - redundant pop/push register-read regression.
 * Generated archive case: batch9/c8914.
 */
#include <stdio.h>

static unsigned isqrt(unsigned value)
{
    unsigned guess = value ? value : 1;
    unsigned next;

    do {
        next = (guess + value / guess) / 2;
        if (next >= guess)
            return guess;
        guess = next;
    } while (1);
}

int main(void)
{
    unsigned first = isqrt(1000U);
    unsigned second = isqrt(1024U);
    int ok;

    printf("tpeepop isqrt=%u,%u\n", first, second);
    ok = first == 31U && second == 32U;
    if (ok)
        printf("tpeepop passed with great success\n");
    return !ok;
}
