/*
 * tcaidxb.c - KNOWN BUG, not yet fixed (tracked via "ignore" in
 * tests/_test_overrides.json - remove that flag and give this a real
 * baseline once fixed).
 *
 * A compound-assignment operator on an unsigned char array element fails
 * to compile with "error DCC-E1002: unsupported expression statement" -
 * plain scalar variables and int arrays are both fine; it's specifically
 * a compound-assign target that is a narrow (char-sized) array element.
 * Reproduces with both |= and +=. Found via SDCC's regression test
 * gcc-torture-execute-20051110-1.c.
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

static unsigned char bytes[5];

int main(void)
{
    fails = 0;

    bytes[0] = 0x01;
    bytes[0] |= 0x80;
    chk("or_assign_elem", bytes[0], 0x81);

    bytes[1] = 10;
    bytes[1] += 5;
    chk("add_assign_elem", bytes[1], 15);

    if (fails) {
        printf("tcaidxb failed: %d\n", fails);
        return 1;
    }
    printf("tcaidxb completed with great success\n");
    return 0;
}
