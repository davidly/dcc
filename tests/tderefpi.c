/*
 * tderefpi.c - KNOWN BUG, not yet fixed (tracked via "ignore" in
 * tests/_test_overrides.json - remove that flag and give this a real
 * baseline once fixed).
 *
 * Dereferencing a post-incremented double-indirect pointer, when the
 * dereferenced VALUE is used, fails to compile with "error DCC-E1002:
 * unsupported return expression" (the specific wording depends on the
 * surrounding context - an assignment RHS reports "unsupported
 * expression statement" instead): `*(*x)++`. `(*x)++;` alone (side
 * effect only, value discarded) works, and a single-level `c = *p++;`
 * works; only the combination of double indirection plus using the
 * dereferenced result trips it. Found via SDCC's regression test
 * gcc-torture-execute-20050502-1.c.
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

static int deref_postinc(const char **x)
{
    return *(*x)++;
}

int main(void)
{
    static const char s[] = "hi";
    const char *p;

    fails = 0;
    p = s;

    chk("deref_postinc_value", deref_postinc(&p), 'h');
    chk("deref_postinc_side_effect", *p, 'i');

    if (fails) {
        printf("tderefpi failed: %d\n", fails);
        return 1;
    }
    printf("tderefpi completed with great success\n");
    return 0;
}
