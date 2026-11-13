/* tcmpval.c - values of assignments, compound assignments and ++/--
 *
 * An assignment's value is the stored one: narrowed to the lvalue's type,
 * whatever width the operation was done in. That holds for named, array,
 * member and pointer lvalues, for a long or nested-block rhs, under a cast,
 * and for ++/-- on nested-block locals and array elements.
 */

#include <stdio.h>

static int fails;

static void check(const char *name, long got, long want)
{
    if (got != want) {
        printf("FAIL %s: got %lx want %lx\n", name, got, want);
        ++fails;
    }
}

static int qi[3];
static short qs[3];
static long ql[3];
static unsigned char qc[3];
static signed char qsc[3];
static unsigned char gc;
static signed char gsc;
static struct { char pad; unsigned char c; } mc;
static struct { char pad; unsigned u; } mu;
static struct { char pad; long l; } ml;

/* A long rhs is computed in long and narrowed back to the element. */
static int elem_add_long(int i, long lv) { qi[i] = 5; return qi[i] += lv; }
static int deref_and_long(int *p, long lv) { *p = -1; return *p &= lv; }
static int member_add_long(long lv) { mc.c = 250; return mc.c += lv; }

/* A cast of an assignment converts the stored value. */
static long cast_add(int x) { int k = x; return (long)(k += 1); }
static int cast_char_assign(int x) { signed char c; return (int)(c = x); }

/* A byte's shift is done in int; its value is the narrowed byte. */
static int char_shift(int b) { gc = 200; return gc >>= b; }
static int member_shift(int b) { mc.c = 200; return (unsigned char)(mc.c <<= b); }

/* ++/-- on nested-block locals and int elements. */
static long block_preinc(long a) { { long kl = a; return (long)(++kl); } }
static short block_predec(long a) { { long kl = a; return (short)(--kl); } }
static long elem_preinc(int i) { qi[i] = -3; return (long)(++qi[i]); }
static long elem_long_postdec(int i) { long v; ql[i] = 0x10000L; v = ql[i]--; return v ^ ql[i]; }

/* A nested-block rhs: the operation and its narrowing follow the repaired
 * types. */
static int member_div_block(unsigned a, signed char b)
{
    { signed char kb = b; mu.u = a; return (signed char)(mu.u /= kb); }
}
static int member_mod_block(long a, unsigned short b)
{
    { unsigned short kb = b; ml.l = a; return (int)(ml.l %= kb); }
}
static long elem_sub_block(short a, unsigned long b, int i)
{
    { unsigned long kb = b; qs[i] = a; return (long)(qs[i] -= kb); }
}

/* Byte globals and statics, and byte array elements. */
static int char_div(unsigned b) { gc = 200; return (int)(gc /= b); }
static int char_mul(int b) { static unsigned char s; s = 200; return (int)(s *= b); }
static int char_mod(int b) { static signed char s; s = -100; return (int)(s %= b); }
static long char_div_long(long b) { gsc = -100; return gsc /= b; }
static int elem_char_add(int i, int b) { qc[i] = 250; return qc[i] += b; }
static int elem_char_assign(int i, int b) { return (int)(qc[i] = b); }
static int elem_char_shift(int i, int b) { qsc[i] = 100; return qsc[i] <<= b; }
static int elem_char_mod(int i, int b) { static signed char st[4]; st[i] = -3; return (st[i] %= b) * 2; }

int main(void)
{
    check("elem_add_long", elem_add_long(1, 0x10003L), 8L);
    check("deref_and_long", deref_and_long(&qi[2], 0x1fff0L), -16L);
    check("member_add_long", member_add_long(0x10010L), 10L);
    check("cast_add", cast_add(-2), -1L);
    check("cast_char_assign", cast_char_assign(0x1ff), -1L);
    check("char_shift", char_shift(1), 100L);
    check("member_shift", member_shift(1), 144L);
    check("block_preinc", block_preinc(0xffffL), 0x10000L);
    check("block_predec", (long)block_predec(0x10000L), -1L);
    check("elem_preinc", elem_preinc(0), -2L);
    check("elem_long_postdec", elem_long_postdec(2), 0x1ffffL);
    check("member_div_block", member_div_block(200u, -3), 0L);
    check("member_mod_block", member_mod_block(-100000L, 65535u), 31071L);
    check("elem_sub_block", elem_sub_block(-32768, 2147483647UL, 2), -32767L);
    check("char_div", char_div(3), 66L);
    check("char_mul", char_mul(7), 120L);
    check("char_mod", char_mod(7), -2L);
    check("char_div_long", char_div_long(3L), -33L);
    check("elem_char_add", elem_char_add(1, 10), 4L);
    check("elem_char_assign", elem_char_assign(2, 300), 44L);
    check("elem_char_shift", elem_char_shift(0, 1), -56L);
    check("elem_char_mod", elem_char_mod(2, 2), -2L);
    if (fails == 0)
        printf("tcmpval passed\n");
    return fails;
}
