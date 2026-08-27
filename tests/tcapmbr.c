/*
 * tcapmbr.c - KNOWN BUG, not yet fixed (tracked via "ignore" in
 * tests/_test_overrides.json - remove that flag and give this a real
 * baseline once fixed).
 *
 * A chained assignment through two dot-accessed pointer struct members
 * fails to compile with "error DCC-E1002: unsupported expression
 * statement" - plain int members or a single (non-chained) pointer
 * assignment both compile fine; it's specifically two pointer members of
 * the same struct instance assigned in one chained expression. Found via
 * SDCC's regression test gcc-torture-execute-20050125-1.c.
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

struct parse { char *next; char *end; };

int main(void)
{
    struct parse p;
    static char buf[4] = { 'h', 'i', 0, 0 };
    char *y;

    fails = 0;
    y = buf;
    p.next = p.end = y;

    chk("next_eq_y", p.next == y, 1);
    chk("end_eq_y", p.end == y, 1);
    chk("next_eq_end", p.next == p.end, 1);

    if (fails) {
        printf("tcapmbr failed: %d\n", fails);
        return 1;
    }
    printf("tcapmbr completed with great success\n");
    return 0;
}
