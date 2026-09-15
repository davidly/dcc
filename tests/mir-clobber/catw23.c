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

#ifdef CATALAN_ZERO_WRAPPER_4096
static void catalan_zero_4096(CATALAN_VOLATILE long a[])
{
    zero(a);
}

#define CATALAN_ZERO_4096 catalan_zero_4096
#else
#define CATALAN_ZERO_4096 zero
#endif

static int is_zero(const CATALAN_VOLATILE long a[])
{
    int i;
    for (i = 0; i < NBLOCKS; ++i)
        if (a[i] != 0) return 0;
    return 1;
}

#ifdef CATALAN_IS_ZERO_WRAPPER_4096
static int catalan_is_zero_4096(const CATALAN_VOLATILE long a[])
{
    return is_zero(a);
}

#define CATALAN_IS_ZERO_4096 catalan_is_zero_4096
#else
#define CATALAN_IS_ZERO_4096 is_zero
#endif

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

#ifdef CATALAN_ADD_TERM_WRAPPER_4096
static void catalan_add_term_4096(CATALAN_VOLATILE long sum[],
                                  const CATALAN_VOLATILE long scale[],
                                  int sign, long numer, long pow2, long m)
{
    add_term(sum, scale, sign, numer, pow2, m);
}

#define CATALAN_ADD_TERM_4096 catalan_add_term_4096
#else
#define CATALAN_ADD_TERM_4096 add_term
#endif

#ifdef CATALAN_DIV_SMALL_WRAPPER_4096
static void catalan_div_small_4096(CATALAN_VOLATILE long a[], long d)
{
    div_small(a, d);
}

#define CATALAN_DIV_SMALL_4096 catalan_div_small_4096
#else
#define CATALAN_DIV_SMALL_4096 div_small
#endif

#ifdef CATALAN_FIXED_PRINT_WRAPPER
static int catalan_print_prefix(const char *format, long value)
{
    (void)format;
    return printf("%ld.", value);
}

#define CATALAN_PRINT_PREFIX(value) catalan_print_prefix("%ld.", value)
#elif defined(CATALAN_ALT_FORMAT)
#define CATALAN_PRINT_PREFIX(value) printf("Catalan=%ld.", value)
#else
#define CATALAN_PRINT_PREFIX(value) printf("%ld.", value)
#endif

#ifdef CATALAN_PUTCHAR_WRAPPER
static int catalan_putchar(int value)
{
    return putchar(value);
}

#define CATALAN_PUTCHAR(value) catalan_putchar(value)
#else
#define CATALAN_PUTCHAR(value) putchar(value)
#endif

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
    CATALAN_ZERO_4096(s4096);

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

    for (n = 0; !CATALAN_IS_ZERO_4096(s4096); ++n) {
        long a = 8L * n;

        CATALAN_ADD_TERM_4096(sum, s4096, -1, 1,    4, a + 1);
        CATALAN_ADD_TERM_4096(sum, s4096, -1, 1,    8, a + 2);
        CATALAN_ADD_TERM_4096(sum, s4096, -1, 1,   32, a + 3);
        CATALAN_ADD_TERM_4096(sum, s4096,  1, 1,  256, a + 5);
        CATALAN_ADD_TERM_4096(sum, s4096,  1, 1,  512, a + 6);
        CATALAN_ADD_TERM_4096(sum, s4096,  1, 1, 2048, a + 7);

        CATALAN_DIV_SMALL_4096(s4096, 4096);
    }

    CATALAN_PRINT_PREFIX(sum[0]);

    {
        int printed = 0;
        int i;
        for (i = 1; i < NBLOCKS && printed < NDIG; ++i) {
            long p = BASE / 10;
            while (p > 0 && printed < NDIG) {
                CATALAN_PUTCHAR('0' + (int)((sum[i] / p) % 10));
                p /= 10;
                ++printed;
            }
        }
    }

    CATALAN_PUTCHAR('\n');
    return 0;
}