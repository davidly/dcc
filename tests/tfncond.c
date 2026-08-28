/*
 * tfncond.c - conditional function-designator call regression.
 *
 * A function designator in a value context decays to a function pointer, so
 * the result of `condition ? first : second` can be called directly.  dcc
 * previously rejected that standard-C form with DCC-E1002.  Found via
 * z88dk's Issue_1167_choosing_which_function.c regression test.
 */
#include <stdio.h>

static int seen;
static int fails;

static void first(int value)
{
    seen = 100 + value;
}

static void second(int value)
{
    seen = 200 + value;
}

static void call_selected(int select_first, int value)
{
    (select_first ? first : second)(value);
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
    fails = 0;

    call_selected(1, 7);
    check("true arm", seen, 107);

    call_selected(0, 9);
    check("false arm", seen, 209);

    if (fails) {
        printf("tfncond failed: %d\n", fails);
        return 1;
    }
    printf("tfncond completed with great success\n");
    return 0;
}
