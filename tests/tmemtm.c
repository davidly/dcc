/*
 * tmemtm.c - KNOWN BUG, not yet fixed (tracked via "ignore" in
 * tests/_test_overrides.json - remove that flag and give this a real
 * baseline once fixed).
 *
 * Assigning a wider integer parameter into a narrower struct member
 * through a pointer dereference fails to compile with "error DCC-E1002:
 * unsupported expression statement" - not a size/complexity threshold of
 * the enclosing struct (a 2-field struct reproduces it identically);
 * matching types (both int, or both long) compile fine. Found via SDCC's
 * regression test gcc-torture-execute-20020402-2.c.
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

typedef struct { int foo; } StatsS;

static void set_foo(StatsS *statsPtr, long sid)
{
    statsPtr->foo = sid;
}

int main(void)
{
    StatsS s;

    fails = 0;
    s.foo = 0;
    set_foo(&s, 12345L);

    chk("member_wide_to_narrow", s.foo, 12345);

    if (fails) {
        printf("tmemtm failed: %d\n", fails);
        return 1;
    }
    printf("tmemtm completed with great success\n");
    return 0;
}
