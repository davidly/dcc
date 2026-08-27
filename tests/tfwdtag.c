/*
 * tfwdtag.c - regression coverage for forward references between locally
 * tagged structs in a nested block.
 *
 * Two locally-tagged structs, declared in a block nested one level
 * beyond function scope, with a forward-reference pointer from the
 * first tag to the second and a write through that pointer, used to crash the
 * compiler with "dcc: fatal: MIR emission is required" - an internal
 * invariant failure, not a clean diagnostic. All three ingredients (the
 * extra block nesting, the two locally-tagged structs with a
 * forward-reference between them, and the arrow-write through the
 * pointer) are required to trigger it. Found via SDCC's regression test
 * structscope.c.
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

static int test_nested(void)
{
    int result;

    {
        struct tag1 { struct tag2 *p; } ls4;
        struct tag2 { int c; } ls5;

        ls4.p = &ls5;
        ls4.p->c = 9;
        result = ls4.p->c;
    }
    return result;
}

int main(void)
{
    fails = 0;

    chk("nested_fwd_tag_write", test_nested(), 9);

    if (fails) {
        printf("tfwdtag failed: %d\n", fails);
        return 1;
    }
    printf("tfwdtag completed with great success\n");
    return 0;
}
