/* tphiadj.c - an in-place adjust of a loop-carried word
 *
 * After `for (...) a = b;` a's value is a loop phi. `a += 127` or `++a` on
 * one branch was done in place on a's memory, which the phi only shares
 * when the adjust is its sole use; here the other branch reads it too, so
 * the phi had its own home and a's memory still held the entry value.
 */

#include <stdio.h>

static int fails;

static void check(const char *name, long got, long want)
{
    if (got != want) {
        printf("FAIL %s: got %lx want %lx\n", name, got, want);
        ++fails;
    }
}

static unsigned add_param(unsigned b, unsigned a, int c)
{
    int j;
    for (j = 0; j < 1; ++j)
        a = b;
    if (c)
        a += 127;
    else
        a += a;
    if (1)
        a *= c;
    return a;
}

static unsigned inc_param(unsigned b, unsigned a, int c)
{
    int j;
    for (j = 0; j < 1; ++j)
        a = b;
    if (c)
        ++a;
    else
        a += a;
    if (1)
        a *= c;
    return a;
}

static unsigned sub_param(unsigned b, unsigned a, int c)
{
    int j;
    for (j = 0; j < 1; ++j)
        a = b;
    if (c)
        a -= 9;
    else
        a += a;
    if (1)
        a *= c;
    return a;
}

static unsigned add_local(unsigned b, int c)
{
    unsigned a;
    int j;
    a = 0;
    for (j = 0; j < 1; ++j)
        a = b;
    if (c)
        a += 127;
    else
        a += a;
    if (1)
        a *= c;
    return a;
}

static unsigned inc_local(unsigned b, int c)
{
    unsigned a;
    int j;
    a = 0;
    for (j = 0; j < 1; ++j)
        a = b;
    if (c)
        ++a;
    else
        a += a;
    if (1)
        a *= c;
    return a;
}

int main(void)
{
    check("add_param", add_param(5, 0, -3), 0xfe74L);
    check("inc_param", inc_param(5, 0, -3), 0xffeeL);
    check("sub_param", sub_param(5, 0, -3), 0xcL);
    check("add_local", add_local(5, -3), 0xfe74L);
    check("inc_local", inc_local(5, -3), 0xffeeL);
    check("add_param_else", add_param(5, 0, 0), 0L);
    if (fails == 0)
        printf("tphiadj passed\n");
    return fails;
}
