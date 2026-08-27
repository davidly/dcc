/*
 * tcanest.c - KNOWN BUG, not yet fixed (tracked via "ignore" in
 * tests/_test_overrides.json - remove that flag and give this a real
 * baseline once fixed).
 *
 * A chained assignment through a struct-pointer member fails to compile
 * with "error DCC-E0920: incompatible integer to pointer assignment"
 * when the assignment target's intermediate variable is declared inside
 * a block nested one level deeper than the function body. The identical
 * code with that variable declared at function-top scope compiles fine -
 * found via SDCC's regression test gcc-torture-execute-20020129-1.c.
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

struct A { struct A *a1; int tag; };
struct B { struct A *b2; };

static struct A target;

static struct A *foo(struct B *y)
{
    struct A *z;
    {
        struct A *a;
        z = a = y->b2;
    }
    return z;
}

int main(void)
{
    struct B y;

    fails = 0;
    target.tag = 42;
    y.b2 = &target;

    chk("chained_nested_scope", foo(&y)->tag, 42);

    if (fails) {
        printf("tcanest failed: %d\n", fails);
        return 1;
    }
    printf("tcanest completed with great success\n");
    return 0;
}
