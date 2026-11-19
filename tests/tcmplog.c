/* A compound assignment to a wide struct member whose right operand is an
 * && or || value, in a function that also declares a nested-block local.
 * The 0/1 value is an untyped phi, which marked the operation for deferred
 * retyping; repair then saw the member load before it was typed and
 * rederived long - int as int, so only the low word was widened and stored
 * (0x47FEEF93 + 1 became 0xFFFFEF94). */
#include <stdio.h>

int fails;
int one = 1, zero;

struct In { long pad; unsigned long m2; };
struct S { long m0[2]; struct In m1; long m4; } s, t[3];

static void check(unsigned long got, unsigned long want, int line)
{
    if (got != want) {
        printf("line %d: got %lx, expected %lx\n", line, got, want);
        ++fails;
    }
}

static void add_or(void)
{
    s.m4 += (1L || 1);
    { short b = 1; (void)b; }
}

static void sub_and(void)
{
    t[1].m4 -= (one && 1000UL);
    { short b = 2; (void)b; }
}

static void nested_or(void)
{
    t[2].m1.m2 -= (zero || one);
    { short b = 3; (void)b; }
}

static void xor_and(void)
{
    s.m0[1] ^= (one && zero);
    t[0].m4 += (zero || zero);
    { short b = 4; (void)b; }
}

int main()
{
    s.m4 = 0x47FEEF93L;
    add_or();
    check((unsigned long)s.m4, 0x47FEEF94UL, __LINE__);
    t[1].m4 = 0x12348000L;
    sub_and();
    check((unsigned long)t[1].m4, 0x12347FFFUL, __LINE__);
    t[2].m1.m2 = 0xABCD0000UL;
    nested_or();
    check(t[2].m1.m2, 0xABCCFFFFUL, __LINE__);
    s.m0[1] = 0x7FFF8001L;
    t[0].m4 = 0x10008000L;
    xor_and();
    check((unsigned long)s.m0[1], 0x7FFF8001UL, __LINE__);
    check((unsigned long)t[0].m4, 0x10008000UL, __LINE__);
    if (fails == 0)
        printf("tcmplog passed\n");
    return fails;
}
