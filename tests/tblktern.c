/* tblktern.c - ?: and mixed-width operations on nested-block locals
 *
 * A nested block's locals are typed as int while their expressions are
 * built. A ?: arm or a binary operand that is such a local must still take
 * the declared type: unsigned arms zero-extend, and a long operation widens
 * its narrower operand (including an && result). A shift by such a long
 * count inside a loop once made the type repair loop forever.
 */

#include <stdio.h>

static int fails;
static long gl;
static struct { signed char f0; unsigned short f1; unsigned short f2; } m;

static void check(const char *name, long got, long want)
{
    if (got != want) {
        printf("FAIL %s: got %lx want %lx\n", name, got, want);
        ++fails;
    }
}

static long tern_unsigned(int c, int i, unsigned u) { { unsigned k = u; return c ? i : k; } }
static unsigned long tern_ushort(int c, int i, unsigned short u) { { unsigned short k = u; return c ? i : k; } }
static long tern_long_arm(int c, int i, long l) { { long k = l; return c ? i : k; } }
static long mul_narrow(unsigned long u, short s) { { unsigned long k = u; short n = s; return (long)(k * n); } }
static unsigned long sub_member(unsigned long u, int m) { { unsigned long k = u; return (k * k) - m; } }
static int cmp_logical(long a, long b) { { long k = b; return ((k && a) < k); } }
static long div_logical(long a, unsigned short m) { { long k = a; return (-k) / (k && (m ^ k)); } }
static long shift_long_count(unsigned a, long b) { { unsigned ka = a; long kb = b; return (long)(ka << kb); } }
static unsigned long mod_sub(long a) { { long k = a; return ((m.f0 ^ k) % k) - m.f2; } }
static unsigned long div_mul(unsigned long a, short s) { { short n = s; unsigned long k = a; return (m.f1 << 2) / (k * n); } }
static int ge_unsigned(unsigned long a) { { unsigned long k = a; return gl >= (m.f2 - k); } }
static unsigned char shift_loop(signed char a, long b)
{
    static signed char sa;
    sa = a;
    {
        long kb = b;
        unsigned char acc;
        int k;
        acc = 0;
        for (k = 0; k < 3; ++k)
            acc += (unsigned char)(sa << kb);
        return acc;
    }
}
static unsigned tern_logical(unsigned long a) { { unsigned long k = a; return ((k > m.f0) ? (m.f1 ^ k) : (m.f0 && k)) >> 9; } }

int main(void)
{
    check("tern_unsigned", tern_unsigned(0, -1, 0xfff0), 0xfff0L);
    check("tern_unsigned_int", tern_unsigned(1, -1, 0xfff0), 0xffffL);
    check("tern_ushort", (long)tern_ushort(0, -1, 0xffff), 0xffffL);
    check("tern_long_arm", tern_long_arm(1, -1, 5L), -1L);
    check("mul_narrow", mul_narrow(0xffffffffUL, 100), -100L);
    check("sub_member", (long)sub_member(0xffffffffUL, 127), (long)0xffffff82UL);
    check("cmp_logical", cmp_logical(-108L, -1L), 0L);
    check("div_logical", div_logical(-100L, 100), 100L);
    m.f0 = 0;
    m.f1 = 32767;
    m.f2 = 127;
    check("shift_long_count", shift_long_count(65436U, 3L), 0xfce0L);
    check("mod_sub", (long)mod_sub(128L), -127L);
    check("div_mul", (long)div_mul(0xffffffffUL, 100), 0L);
    gl = 2147483646L;
    m.f2 = 256;
    check("ge_unsigned", ge_unsigned(32768UL), 0L);
    check("shift_loop", shift_loop(3, 2L), 36L);
    m.f0 = -1;
    check("tern_logical", tern_logical(0xffffffffUL), 0L);
    if (fails == 0)
        printf("tblktern passed\n");
    return fails;
}
