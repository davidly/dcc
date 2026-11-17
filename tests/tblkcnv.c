/* Nested-block locals whose stored value lost its declared type before it was
 * widened.  A decremented unsigned folded to the bits of -68 and was then
 * sign-extended in a long add; an unsigned short initialized from a negative
 * short kept the int type of the constant, so it was sign-extended as a long
 * operand and as a long argument; and ~k of a byte constant was folded to an
 * unsigned bit pattern, so a long comparison read it as 65532, not -4. */
#include <stdio.h>

int fails;
unsigned long g;
long gl;

static void check(unsigned long got, unsigned long want, int line)
{
    if (got != want) {
        printf("line %d: got %lx, expected %lx\n", line, got, want);
        ++fails;
    }
}

static long pass(long v) { return v; }

void decrement(void) { { unsigned u = 65469U; u--; g = u + 1UL; } }
void shadowed(void)
{
    {
        unsigned t = (signed char)-67L;
        { long t = 5; gl = t; }
        t--;
        g = t + (4294967295UL / (signed char)-1L);
    }
}
void negative_short(void) { { unsigned short t = (short)-32768L; g = t | pass(t); } }
void argument(void) { { unsigned short t = (short)-32768L; g = pass(t); } }

unsigned long complement(unsigned char a)
{
    static unsigned char s;
    {
        unsigned char k = a;
        unsigned char *p = &s;
        long *q = &gl;
        unsigned long h;
        k = 3;
        *p = (~*q) <= (~k);
        h = 0;
        h = h * 31 + (unsigned long)s;
        h = h * 31 + (unsigned long)k;
        return h;
    }
}

int main()
{
    decrement();
    check(g, 0xffbdUL, __LINE__);
    shadowed();
    check(g, 0xffbdUL, __LINE__);
    negative_short();
    check(g, 0x8000UL, __LINE__);
    argument();
    check(g, 0x8000UL, __LINE__);
    gl = -23;
    check(complement(2), 3, __LINE__);
    if (fails == 0)
        printf("tblkcnv passed\n");
    return fails;
}
