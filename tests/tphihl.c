/* A register binary whose left operand sits in HL and is still needed by a
 * loop phi: that use is earlier in the instruction stream, so the operand was
 * taken as dead and HL was clobbered while computing k + a into DE.  The next
 * iteration (and the code after the loop) then saw the old k as a. */
#include <stdio.h>

int fails;
struct { char pad; signed char f; } m;

static void check(unsigned long got, unsigned long want, int line)
{
    if (got != want) {
        printf("line %d: got %lu, expected %lu\n", line, got, want);
        ++fails;
    }
}

unsigned long loop(unsigned a, unsigned short b)
{
    {
        unsigned short k = b;
        int j;
        unsigned long h;
        for (j = 0; j < 1; ++j) {
            a = (k == k);
            k = (k + a);
        }
        h = 0;
        h = h * 31 + (unsigned long)m.f;
        h = h * 31 + (unsigned long)a;
        h = h * 31 + (unsigned long)k;
        return h;
    }
}

int main()
{
    check(loop(256U, 0), 32, __LINE__);
    m.f = 1;
    check(loop(256U, 5), 31UL * 31 + 31 + 6, __LINE__);
    if (fails == 0)
        printf("tphihl passed\n");
    return fails;
}
