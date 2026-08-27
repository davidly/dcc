/*
 * tcsecall.c - a value read through a pointer (or a pointer-to-struct-field
 * alias) right after a call that could have mutated it through that alias
 * must reflect the mutation, not a copy an optimizer cached before the
 * call. dcc has hit this exact bug class more than once (LICM/CSE and
 * BC-register-residency miscompiles caching a stale value across a write
 * done through an aliased pointer). Modeled on sdcc's regression test
 * bug-1029883.c (global CSE incorrectly caching a value across intervening
 * function calls).
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

static void spoil(int val)
{
    (void)val;
}

static void inc_via_ptr(int *p)
{
    (*p)++;
}

static int gcse_local(int target)
{
    spoil(target);
    inc_via_ptr(&target);
    return target;
}

struct holder {
    int field1;
    int *field2;
    int field3;
};

static void inc_via_field(struct holder *h)
{
    (*h->field2)++;
}

static int gcse_struct(int target)
{
    struct holder h;

    h.field2 = &h.field3;
    *h.field2 = target;
    spoil(h.field3);
    inc_via_field(&h);
    return h.field3;
}

int main(void)
{
    fails = 0;

    chk("gcse_local", gcse_local(1), 2);
    chk("gcse_struct", gcse_struct(1), 2);

    if (fails) {
        printf("tcsecall failed: %d\n", fails);
        return 1;
    }
    printf("tcsecall completed with great success\n");
    return 0;
}
