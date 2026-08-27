/*
 * tginitu.c - regression coverage for leading unary operators in global
 * scalar constant initializers.
 *
 * A global scalar initializer whose constant expression begins with '~'
 * or '!' used to fail to compile with
 * "error DCC-E0902: constant initializer expected" - '-' and '+' both
 * worked fine as a leading unary operator.
 * parse_global_init_atom's entry gate (dcc_global_init.c) only allows
 * {number, char literal, '-', '+', '(', sizeof} to start the constant-
 * expression path; '~'/'!' are simply missing from that list even though
 * the underlying folder (cf_parse_unary) already handles them elsewhere
 * (array bounds, bitfield widths). Found via SDCC's regression test
 * funsigned-char.c.
 */
#include <stdio.h>

static int fails;

static void chk(const char *name, int got, int exp)
{
    if (got != exp) {
        printf("FAIL %s got %d expected %d\n", name, got, exp);
        fails++;
    }
}

int a = -5;
int b = !5;
int c = ~5;

int main(void)
{
    fails = 0;

    chk("neg", a, -5);
    chk("logical_not", b, 0);
    chk("complement", c, ~5);

    if (fails) {
        printf("tginitu failed: %d\n", fails);
        return 1;
    }
    printf("tginitu completed with great success\n");
    return 0;
}
