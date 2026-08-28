/*
 * tboolbf.c - C99 _Bool bit-field regression.
 *
 * Dcc supported _Bool and int bit-fields independently but rejected the C99
 * form `_Bool flag : 1`.  Found via GCC torture execute test 20030714-1;
 * its much larger original has unrelated public names that collide under
 * M80's six-significant-character rule.
 */
#include <stdio.h>

typedef _Bool bool;

struct Flags {
    bool ready : 1;
    bool active : 1;
    unsigned int count : 3;
    bool visible : 1;
};

static struct Flags flags = { 0, 1, 5, 1 };
static int fails;

static void check(const char *name, int got, int expected)
{
    if (got != expected) {
        printf("FAIL %s got %d expected %d\n", name, got, expected);
        ++fails;
    }
}

int main(void)
{
    struct Flags local = { 1, 0, 3, 0 };

    fails = 0;
    check("global ready", flags.ready, 0);
    check("global active", flags.active, 1);
    check("global count", flags.count, 5);
    check("global visible", flags.visible, 1);

    flags.ready = 7;
    flags.active = 0;
    check("normalize true", flags.ready, 1);
    check("normalize false", flags.active, 0);
    check("neighbor preserved", flags.count, 5);

    check("local ready", local.ready, 1);
    check("local active", local.active, 0);
    check("local count", local.count, 3);

    if (fails) {
        printf("tboolbf failed: %d\n", fails);
        return 1;
    }
    printf("tboolbf completed with great success\n");
    return 0;
}
