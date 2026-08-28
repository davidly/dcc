/*
 * tinitbr.c - optional scalar braces in global aggregate initializers.
 *
 * Braces may wrap scalar subobjects, including string literals used for
 * pointer elements.  A string initializing a row of a multidimensional char
 * array must instead be copied inline.  Found via z88dk's
 * Issue_1054_initialisation.c regression test.
 */
#include <stdio.h>

static char rows[][6] = {
    { "alpha" },
    { "b" }
};

static const char *names[] = {
    { "first" },
    {{ "second" }}
};

static int fails;

static void check(const char *name, int condition)
{
    if (!condition) {
        printf("FAIL %s\n", name);
        ++fails;
    }
}

int main(void)
{
    fails = 0;
    check("inline row alpha", rows[0][0] == 'a' && rows[0][5] == 0);
    check("inline row b", rows[1][0] == 'b' && rows[1][1] == 0);
    check("pointer first", names[0][0] == 'f');
    check("pointer nested", names[1][0] == 's');
    if (fails) {
        printf("tinitbr failed: %d\n", fails);
        return 1;
    }
    printf("tinitbr completed with great success\n");
    return 0;
}
