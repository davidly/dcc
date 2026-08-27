/*
 * tcalsuf.c - KNOWN BUG, not yet fixed (tracked via "ignore" in
 * tests/_test_overrides.json - remove that flag and give this a real
 * baseline once fixed).
 *
 * A compound-assignment whose right-hand literal carries an 'L' suffix
 * fails to compile with "error DCC-E1002: unsupported expression
 * statement" when the left-hand side is a narrow (unsigned char)
 * variable - the identical assignment without the L suffix compiles
 * fine, so the literal's suffix (not the value or the narrow LHS alone)
 * is the trigger. Found via SDCC's regression test constmodifiers.c.
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

int main(void)
{
    unsigned char a;

    fails = 0;

    a = 0x0F;
    a |= 0xFFL;
    chk("or_assign_lsuffix", a, 0xFF);

    if (fails) {
        printf("tcalsuf failed: %d\n", fails);
        return 1;
    }
    printf("tcalsuf completed with great success\n");
    return 0;
}
