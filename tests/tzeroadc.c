/* Adding a long whose low word is zero.  dccpeep dropped the low word's
 * "add hl,0" as a no-op, but its cleared carry feeds the high word's adc,
 * which then added whatever carry an earlier instruction had left. */
#include <stdio.h>

int fails;
struct { short f0; short f3; } m;

static void check(unsigned long got, unsigned long want, int line)
{
    if (got != want) {
        printf("line %d: got %lx, expected %lx\n", line, got, want);
        ++fails;
    }
}

unsigned long widen_then_add_zero(short a)
{
    unsigned long l;
    m.f3 = a;
    l = m.f3;
    l += !(41UL);
    return l;
}

long add_high_word(long v)
{
    return v + 0x10000000L;
}

int main()
{
    check(widen_then_add_zero(-14), (unsigned long)-14L, __LINE__);
    check((unsigned long)add_high_word(-1L), 0x0fffffffUL, __LINE__);
    if (fails == 0)
        printf("tzeroadc passed\n");
    return fails;
}
