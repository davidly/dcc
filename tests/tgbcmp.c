/*
 * tgbcmp.c - arithmetic compound assignment to a global byte.
 *
 * GCC torture execute test 20030128-1 exposed that dcc admitted global-byte
 * bitwise compound assignments but rejected the arithmetic operators even
 * though they share the same promoted combine and narrowing-store emitter.
 */
#include <stdio.h>

static unsigned char value;
static volatile short operand;
static int fails;

static void check(const char *name, int got, int expected)
{
    if (got != expected) {
        printf("FAIL %s got %d expected %d\n", name, got, expected);
        ++fails;
    }
}

int main(void)
{
    fails = 0;

    value = 50;
    operand = -5;
    value /= operand;
    check("signed divide and narrow", value, (unsigned char)-10);

    value = 250;
    operand = 10;
    value += operand;
    check("addition wraps", value, 4);

    value = 50;
    operand = -7;
    value %= operand;
    check("signed remainder", value, 1);

    if (fails) {
        printf("tgbcmp failed: %d\n", fails);
        return 1;
    }
    printf("tgbcmp completed with great success\n");
    return 0;
}
