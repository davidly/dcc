#include <stdio.h>

#define NDIG 100
#define GUARD 16
#define BASE 10000L
#define WIDTH 4
#define NBLOCKS ((NDIG + GUARD + WIDTH - 1) / WIDTH)

static void add(long a[], const long b[])
{
    int i;
    long carry = 0;

    for (i = NBLOCKS - 1; i >= 0; --i) {
        long v = a[i] + b[i] + carry;
        if (v >= BASE) {
            v -= BASE;
            carry = 1;
        } else {
            carry = 0;
        }
        a[i] = v;
    }
}

static int is_zero(const long a[])
{
    int i;
    for (i = 0; i < NBLOCKS; ++i)
        if (a[i] != 0)
            return 0;
    return 1;
}

static void mul_div(long a[], long mul, long div)
{
    int i;
    long carry = 0;
    long rem = 0;

    for (i = NBLOCKS - 1; i >= 0; --i) {
        long v = a[i] * mul + carry;
        a[i] = v % BASE;
        carry = v / BASE;
    }

    for (i = 0; i < NBLOCKS; ++i) {
        long v = rem * BASE + a[i];
        a[i] = v / div;
        rem = v % div;
    }
}

int main(void)
{
    long sum[NBLOCKS], term[NBLOCKS];
    int i, k, printed;

    for (i = 0; i < NBLOCKS; ++i) {
#ifdef MIR_CLOBBER_LOG_INIT_ORDER
        term[i] = 0;
        sum[i] = 0;
#else
        sum[i] = 0;
        term[i] = 0;
#endif
    }

    {
        long rem = 2;
        for (i = 0; i < NBLOCKS; ++i) {
            rem *= BASE;
            term[i] = rem / 3;
            rem %= 3;
        }
    }

    k = 0;
    while (!is_zero(term)) {
        add(sum, term);
        mul_div(
            term,
#ifdef MIR_CLOBBER_LOG_NUMERATOR_ORDER
            1L + 2L * k,
#else
            2L * k + 1L,
#endif
#ifdef MIR_CLOBBER_LOG_DENOMINATOR_ORDER
            (2L * k + 3L) * 9L);
#else
            9L * (2L * k + 3L));
#endif
        ++k;
    }

    printf("0.");
    printed = 0;
#ifdef MIR_CLOBBER_LOG_COMPARE_ORDER
    for (i = 0; i < NBLOCKS && NDIG > printed; ++i) {
#else
    for (i = 0; i < NBLOCKS && printed < NDIG; ++i) {
#endif
        int d;
        long p = BASE / 10;
#ifdef MIR_CLOBBER_LOG_COMPARE_ORDER
        while (p > 0 && NDIG > printed) {
#else
        while (p > 0 && printed < NDIG) {
#endif
            d = (int)((sum[i] / p) % 10);
#ifdef MIR_CLOBBER_LOG_DIGIT_ORDER
            putchar(d + '0');
#else
            putchar('0' + d);
#endif
            p /= 10;
            ++printed;
        }
    }
    putchar('\n');

    return 0;
}
