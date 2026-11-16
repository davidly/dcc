/* A global word read three times around a store through a pointer to it.
 * dccpeep once kept the first read in BC for the later ones, so `g` got
 * m.f5's old value instead of the one stored through p. */
#include <stdio.h>

int fails;
unsigned short g;
struct { char pad; unsigned long f1; signed char f2; unsigned short f5; } m;

static void bump(void) { m.f2 += 3; }

void through_pointer(signed char a2)
{
    m.f2 = a2;
    {
        unsigned short *p = &m.f5;
        bump();
        *p -= m.f5 - m.f2;
        g = m.f5 ? m.f5 : 1;
    }
}

unsigned short w;
unsigned short *wp = &w;

unsigned through_global_pointer(void)
{
    unsigned short r;
    w = 10;
    r = w;
    *wp = w + 5;
    r += w;
    r += w;
    return r;
}

void check(unsigned got, unsigned want, int line)
{
    if (got != want) {
        printf("line %d: got %u, expected %u\n", line, got, want);
        ++fails;
    }
}

int main()
{
    through_pointer(-1);
    check(m.f5, 2, __LINE__);
    check(g, 2, __LINE__);
    m.f5 = 40;
    through_pointer(5);
    check(m.f5, 8, __LINE__);
    check(g, 8, __LINE__);
    check(through_global_pointer(), 10 + 15 + 15, __LINE__);
    if (fails == 0)
        printf("tglbali passed\n");
    return fails;
}
