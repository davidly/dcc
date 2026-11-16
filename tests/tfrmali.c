/* A local written through a pointer between reads of it.  dccpeep kept the
 * local in BC across the store (the loop hoist and the frame-word cache), so
 * later reads saw its old value. */
#include <stdio.h>

int fails;
int g1, g2, g3;

static void check(int got, int want, int line)
{
    if (got != want) {
        printf("line %d: got %d, expected %d\n", line, got, want);
        ++fails;
    }
}

int through_pointer(int a0)
{
    int k = a0;
    unsigned char *p = (unsigned char *)&k;
    g1 = k + 1;
    p[0] = 7;
    g2 = k + 2;
    g3 = k + 3;
    return k;
}

/* Keeps the function above clear of BC users that would hide the bug. */
int identity(int v)
{
    return v;
}

int shift_low_byte(int a0)
{
    int k = a0;
    int j = 3;
    do {
        ((unsigned char *)&k)[0] = (unsigned char)(k >> 3);
    } while (--j > 0);
    return k;
}

int main()
{
    check(shift_low_byte(25), 0, __LINE__);
    check(identity(through_pointer(0x100)), 0x107, __LINE__);
    check(g1, 0x101, __LINE__);
    check(g2, 0x109, __LINE__);
    check(g3, 0x10a, __LINE__);
    if (fails == 0)
        printf("tfrmali passed\n");
    return fails;
}
