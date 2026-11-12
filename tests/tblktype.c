/* tblktype.c - types of locals declared in nested blocks
 *
 * A nested block's locals enter the symbol table only when the block is
 * emitted, after its expressions were built assuming int. Operations on them
 * must still use their declared types: an unsigned right shift is logical,
 * an unsigned value widens with zeros, and a long compares as a long.
 */

#include <stdio.h>

static int fails;
static int cnt = 4;
static int one = 1;
static int m1 = -1;

static void check(const char *name, long got, long want)
{
    if (got != want) {
        printf("FAIL %s: got %lx want %lx\n", name, got, want);
        ++fails;
    }
}

static unsigned shr(unsigned a) { { unsigned x = a; return x >> cnt; } }
static unsigned shr_assign(unsigned a) { { unsigned x = a; x >>= cnt; return x; } }
static long shr_assign_long(unsigned a) { { unsigned x = a; x >>= cnt; return (long)x; } }
static long sub_long(unsigned a) { { unsigned x = a; return (long)(x - one); } }
static long or_long(unsigned a) { { unsigned x = a; return (long)(x | one); } }
static unsigned long store_sub(unsigned a) { unsigned long o; { unsigned x = a; o = x - one; } return o; }
static long add_ushort(unsigned short a) { { unsigned short x = a; return (long)(x + one); } }
static long nested(unsigned a) { { unsigned x = a; return (long)((x >> 1) + (x & 1)); } }
static long negate(unsigned a) { { unsigned x = a; return (long)(-x); } }
static int cmp_unsigned(unsigned a) { { unsigned x = a; return x < m1; } }
static int long_gt_m1(long a) { { long k = a; return k > -1; } }
static int long_lt_m1(long a) { { long k = a; return k < -1; } }
static int long_ge_m100(long a) { { long k = a; return k >= -100; } }
static int long_gt_var(long a) { { long k = a; return k > m1; } }
static long long_add(long a) { { long k = a; return k + m1; } }
static long long_mul(long a) { { long k = a; return k * -3; } }
static int in_loop(unsigned a)
{
    int i;
    int n = 0;

    for (i = 0; i < 2; ++i) {
        unsigned x = a;
        if ((x >> cnt) > 0x0f00)
            ++n;
    }
    return n;
}

int main(void)
{
    check("shr", shr(0xffdb), 0x0ffdL);
    check("shr_assign", shr_assign(0xffdb), 0x0ffdL);
    check("shr_assign_long", shr_assign_long(0xffdb), 0x0ffdL);
    check("sub_long", sub_long(0xfff0), 0xffefL);
    check("or_long", or_long(0xfff0), 0xfff1L);
    check("store_sub", (long)store_sub(0xfff0), 0xffefL);
    check("add_ushort", add_ushort(0xfff0), 0xfff1L);
    check("nested", nested(0xfff1), 0x7ff9L);
    check("negate", negate(1), 0xffffL);
    check("cmp_unsigned", cmp_unsigned(0xfff0), 1L);
    check("long_gt_m1", long_gt_m1(5L), 1L);
    check("long_gt_m1_neg", long_gt_m1(-2L), 0L);
    check("long_lt_m1", long_lt_m1(5L), 0L);
    check("long_ge_m100", long_ge_m100(5L), 1L);
    check("long_gt_var", long_gt_var(5L), 1L);
    check("long_add", long_add(0x10000L), 0xffffL);
    check("long_mul", long_mul(0x10000L), -0x30000L);
    check("in_loop", in_loop(0xffdb), 2L);
    if (fails == 0)
        printf("tblktype passed\n");
    return fails;
}
