/* dccpeep folds a materialized boolean's zero test (ld a,h / or l / jp z)
 * into the branches that produced it, but a signed `b <= 0` reads the boolean
 * again after that test (bit 7,h), which then tested whatever HL held: a
 * constant boolean, or a comparison's 0 or 1 in HL. */
#include <stdio.h>

int fails;
struct { char pad; short f; } m;
unsigned char g;

static void check(unsigned long got, unsigned long want, int line)
{
    if (got != want) {
        printf("line %d: got %lx, expected %lx\n", line, got, want);
        ++fails;
    }
}

unsigned long both(unsigned long a)
{
    m.f = 256;
    {
        unsigned long k = a;
        unsigned long *p = &k;
        unsigned long h;
        m.f = (((unsigned char)1L) > ((short)0L)) && ((unsigned short)100L);
        h = 0;
        h = h * 31 + (unsigned long)k;
        h = h * 31 + (unsigned long)m.f;
        return h;
    }
}

long signed_le(void)
{
    if ((g < 65536L) <= !65536L)
        return 1;
    return 0;
}

int main()
{
    check(both(1), 0x20, __LINE__);
    g = 177;
    check(signed_le(), 0, __LINE__);
    if (fails == 0)
        printf("tcbool passed\n");
    return fails;
}
