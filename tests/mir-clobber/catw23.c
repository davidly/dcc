/* tcatalan.c: C89 fixed-point Catalan's constant, 100 digits */

#include <stdio.h>

#define NDIG   100
#define GUARD  20
#define BASE   10000L
#define WIDTH  4
#define FBLOCKS ((NDIG + GUARD + WIDTH - 1) / WIDTH)
#define NBLOCKS (FBLOCKS + 1)   /* block 0 is integer part */

#ifdef CATALAN_RENAMED_HELPERS
#define zero catalan_zero
#define is_zero catalan_is_zero
#define div_small catalan_div_small
#define add_term catalan_add_term
#endif

#ifdef CATALAN_VOLATILE_ARRAYS
#define CATALAN_VOLATILE volatile
#else
#define CATALAN_VOLATILE
#endif

static void zero(CATALAN_VOLATILE long a[])
{
    int i;
    for (i = 0; i < NBLOCKS; ++i) a[i] = 0;
}

static int is_zero(const CATALAN_VOLATILE long a[])
{
    int i;
    for (i = 0; i < NBLOCKS; ++i)
        if (a[i] != 0) return 0;
    return 1;
}

static void copy(CATALAN_VOLATILE long d[],
                 const CATALAN_VOLATILE long s[])
{
    int i;
    for (i = 0; i < NBLOCKS; ++i) d[i] = s[i];
}

static void div_small(CATALAN_VOLATILE long a[], long d)
{
    int i;
    long r = 0;

    for (i = 0; i < NBLOCKS; ++i) {
        long v = r * BASE + a[i];
        a[i] = v / d;
        r = v % d;
    }
}

static void add_signed(CATALAN_VOLATILE long a[],
                       const CATALAN_VOLATILE long b[], int sign)
{
    int i;
    long carry, v;

    if (sign > 0) {
        carry = 0;
        for (i = NBLOCKS - 1; i >= 0; --i) {
            v = a[i] + b[i] + carry;
            if (v >= BASE) {
                v -= BASE;
                carry = 1;
            } else {
                carry = 0;
            }
            a[i] = v;
        }
    } else {
        carry = 0;
        for (i = NBLOCKS - 1; i >= 0; --i) {
            v = a[i] - b[i] - carry;
            if (v < 0) {
                v += BASE;
                carry = 1;
            } else {
                carry = 0;
            }
            a[i] = v;
        }
    }
}

static void add_term(CATALAN_VOLATILE long sum[],
                     const CATALAN_VOLATILE long scale[],
                     int sign, long numer, long pow2, long m)
{
    long t[NBLOCKS];
    int i;

    copy(t, scale);

    for (i = 0; i < numer; ++i)
        add_signed(sum, t, sign); /* temporary; replaced below */

    /* undo above simplification by recomputing cleanly */
    copy(t, scale);
    div_small(t, pow2);
    div_small(t, m);
    div_small(t, m);

    for (i = 0; i < numer; ++i)
        add_signed(sum, t, sign);
}

int main(void)
{
    CATALAN_VOLATILE long sum[NBLOCKS], s16[NBLOCKS], s4096[NBLOCKS];
#ifdef CATALAN_UNSIGNED_COUNTER
    unsigned int n;
#else
    int n;
#endif

    zero(sum);
    zero(s16);
    zero(s4096);

    s16[0] = 1;
    s4096[0] = 1;

    for (n = 0; !is_zero(s16); ++n) {
        long a = 8L * n;

        add_term(sum, s16,  1, 3,  2, a + 1);
        add_term(sum, s16, -1, 3,  2, a + 2);
        add_term(sum, s16,  1, 3,  4, a + 3);
        add_term(sum, s16, -1, 3,  8, a + 5);
        add_term(sum, s16,  1, 3,  8, a + 6);
        add_term(sum, s16, -1, 3, 16, a + 7);

        div_small(s16, 16);
    }

    for (n = 0; !is_zero(s4096); ++n) {
        long a = 8L * n;

        add_term(sum, s4096, -1, 1,    4, a + 1);
        add_term(sum, s4096, -1, 1,    8, a + 2);
        add_term(sum, s4096, -1, 1,   32, a + 3);
        add_term(sum, s4096,  1, 1,  256, a + 5);
        add_term(sum, s4096,  1, 1,  512, a + 6);
        add_term(sum, s4096,  1, 1, 2048, a + 7);

        div_small(s4096, 4096);
    }

#ifdef CATALAN_ALT_FORMAT
    printf("Catalan=%ld.", sum[0]);
#else
    printf("%ld.", sum[0]);
#endif

    {
        int printed = 0;
        int i;
        for (i = 1; i < NBLOCKS && printed < NDIG; ++i) {
            long p = BASE / 10;
            while (p > 0 && printed < NDIG) {
                putchar('0' + (int)((sum[i] / p) % 10));
                p /= 10;
                ++printed;
            }
        }
    }

    putchar('\n');
    return 0;
}