/*
 * tptarith.c - pointer-arithmetic local-initializer regression.
 *
 * On dcc's 16-bit-int target, decimal 56469 has type long.  Casting that
 * value to a pointer and dereferencing pointer arithmetic in a local
 * initializer used to be rejected with DCC-E1002.  Found via z88dk's
 * Issue_2478_dropped_type_arith.c regression test.
 */
#include <stdio.h>

static int fails;
static int absolute_index;

/* Compilation of this unused function preserves the exact upstream trigger
 * without making the runnable test depend on a particular CP/M address. */
static void compile_absolute_address_forms(void)
{
    unsigned char a = *((unsigned char *)56469 + 3);
    unsigned char b = *(3 + (unsigned char *)56469);
    int c = ((int *)64000)[absolute_index];
    int d = ((int *)64000)[10];
    int e = ((char *)64000)[absolute_index];
    int f = ((char *)64000)[10];

    if (a == b && c == d && e == f)
        ++a;
}

static void check(const char *name, int got, int expected)
{
    if (got != expected) {
        printf("FAIL %s got %d expected %d\n", name, got, expected);
        ++fails;
    }
}

int main(void)
{
    unsigned char bytes[5];
    unsigned char *p;
    unsigned char a;
    unsigned char b;

    fails = 0;
    bytes[0] = 11;
    bytes[1] = 22;
    bytes[2] = 33;
    bytes[3] = 44;
    bytes[4] = 55;
    p = bytes;

    a = *(p + 3);
    b = *(3 + p);
    check("pointer plus integer", a, 44);
    check("integer plus pointer", b, 44);

    if (fails) {
        printf("tptarith failed: %d\n", fails);
        return 1;
    }
    printf("tptarith completed with great success\n");
    return 0;
}
