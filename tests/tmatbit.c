#include <stdio.h>

/*
 * Regression test for two dcc AST codegen fixes.
 *
 * 1. Compound assignment (+=, -=, *=, |=, &=, ^=) into a struct MEMBER
 *    N-D array element inside nested for loops - e.g. matrix multiply
 *        r.values[i][j] += a.values[i][k] * b.values[k][j];
 *    The AST statement support gate previously rejected member 2-D array
 *    compound stores, so an ordinary nested for loop over one was reported as
 *    "unsupported for statement" (issue c9908).
 *
 * 2. A 16-bit bitwise compound assignment whose right operand is a long-typed
 *    constant, e.g.
 *        value &= 65535;
 *    65535 does not fit in a 16-bit int, so it is a long literal.  The gate
 *    previously rejected it, so a while loop containing one was reported as
 *    "unsupported while condition or body" (issue c9915).
 *
 * All arithmetic is kept width-independent (every result fits in 16 bits and
 * wide values are explicitly masked) so the output is identical on a 16-bit
 * int target (dcc) and a 32-bit int host (the Clang oracle).
 */

struct Matrix { int values[2][2]; };

static struct Matrix multiply(struct Matrix a, struct Matrix b)
{
    struct Matrix r = { { { 0, 0 }, { 0, 0 } } };
    int i, j, k;
    for (i = 0; i < 2; ++i)
        for (j = 0; j < 2; ++j)
            for (k = 0; k < 2; ++k)
                r.values[i][j] += a.values[i][k] * b.values[k][j];
    return r;
}

/* Exercise the remaining compound operators on a member 2-D array element,
 * reached through a pointer (arrow) base. */
static void combine(struct Matrix *m)
{
    int i, j;
    for (i = 0; i < 2; ++i)
        for (j = 0; j < 2; ++j) {
            m->values[i][j] *= 3;
            m->values[i][j] -= 1;
            m->values[i][j] |= 0x100;
            m->values[i][j] &= 0x1ff;
            m->values[i][j] ^= 2;
        }
}

/* 16-bit rotate-left using while(count--) and a long-constant truncation mask. */
static int rotate_left(unsigned value, int count)
{
    unsigned mask = 1 << 15;
    while (count--) {
        value = (value << 1) | ((value & mask) != 0);
        value &= 65535;              /* 65535 is a long literal on a 16-bit int */
    }
    return (int)value;
}

/* 16-bit bitwise compound assignment against long-constant masks. */
static unsigned bitops(unsigned v)
{
    v |= 65280;                      /* 0xFF00 */
    v &= 65535;                      /* 0xFFFF */
    v ^= 43981;                      /* 0xABCD */
    return v & 65535;
}

int main(void)
{
    struct Matrix a = { { { 1, 2 }, { 3, 4 } } };
    struct Matrix b = { { { 2, 0 }, { 1, 2 } } };
    struct Matrix r;

    r = multiply(a, b);
    printf("matrix=%d,%d/%d,%d\n",
           r.values[0][0], r.values[0][1], r.values[1][0], r.values[1][1]);

    combine(&r);
    printf("combine=%d,%d/%d,%d\n",
           r.values[0][0], r.values[0][1], r.values[1][0], r.values[1][1]);

    printf("rotate=%d,%d\n", rotate_left(1, 4), rotate_left(32768, 1));
    printf("bitops=%u,%u\n", bitops(0x0000), bitops(0x1234));
    return 0;
}
