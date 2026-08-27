/*
 * tinitbf.c - non-constant bitfield initializer for an automatic (local)
 * struct. C89 6.5.7's constant-only initializer rule applies to objects
 * with static storage duration; a local aggregate's initializer elements
 * may be any expression, including one that reads a function parameter.
 * dcc used to apply the constant-only check unconditionally, rejecting
 * `struct x a = {x, y, z};` inside a function with DCC-E0915 even though
 * a, being local, never needed a compile-time constant.
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

struct x { unsigned x1:1; unsigned x2:2; unsigned x3:3; };

static void foobar(int x, int y, int z)
{
    struct x a = {x, y, z};
    chk("param_x1", a.x1, x);
    chk("param_x2", a.x2, y);
    chk("param_x3", a.x3, z);
}

static void partial(int x, int y)
{
    /* x3 is omitted: an automatic aggregate's un-listed trailing members
     * still default-initialize to 0, same as a fully constant initializer
     * would - the shared 16-bit storage unit must not carry over garbage. */
    struct x a = {x, y};
    chk("partial_x1", a.x1, x);
    chk("partial_x2", a.x2, y);
    chk("partial_x3", a.x3, 0);
}

int main(void)
{
    fails = 0;

    foobar(1, 3, 5);
    partial(1, 2);

    if (fails) {
        printf("tinitbf failed: %d\n", fails);
        return 1;
    }
    printf("tinitbf completed with great success\n");
    return 0;
}
