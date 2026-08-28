/*
 * tldref.c - narrowing a long expression through a dereference lvalue.
 *
 * Dcc's generic store already retained the low byte/word correctly, but its
 * AST support gate rejected a long-valued rhs for `*p = rhs`.  The original
 * trigger is GCC torture execute test 20020503-1's integer-to-text loop.
 */
#include <stdio.h>
#include <string.h>

static int fails;

static char *long_to_text(long value, char buffer[16])
{
    unsigned long magnitude = value;
    char *cursor = buffer + 15;

    *cursor = '\0';
    if (value < 0)
        magnitude = -magnitude;
    do
        *--cursor = '0' + magnitude % 10;
    while ((magnitude /= 10) != 0);
    if (value < 0)
        *--cursor = '-';
    return cursor;
}

static void check(long value, const char *expected)
{
    char buffer[16];
    char *got = long_to_text(value, buffer);

    if (strcmp(got, expected) != 0) {
        printf("FAIL got %s expected %s\n", got, expected);
        ++fails;
    }
}

int main(void)
{
    fails = 0;
    check(-1L, "-1");
    check(0L, "0");
    check(12345L, "12345");
    check(-32768L, "-32768");

    if (fails) {
        printf("tldref failed: %d\n", fails);
        return 1;
    }
    printf("tldref completed with great success\n");
    return 0;
}
