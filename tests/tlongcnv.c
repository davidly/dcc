/* tlongcnv.c - long operands dcc used to reject
 *
 * Each of these is valid C that failed with "unsupported expression":
 * a shift whose count is a long, a char assigned a long value without a
 * cast, and an int or char compound-assigned a long read from memory.
 */

#include <stdio.h>

static int fails;
static long gl = 70000L;
static long rl[3] = { 1L, 65537L, -2L };
static struct { char pad; long v; } sl = { 0, 3L };
static unsigned char gc;

static void check(const char *name, long got, long want)
{
    if (got != want) {
        printf("FAIL %s: got %lx want %lx\n", name, got, want);
        ++fails;
    }
}

/* Shift counts of type long. */
static int shl_int(int a, long n) { return a << n; }
static unsigned shr_unsigned(unsigned a, long n) { return a >> n; }
static long shl_long_const(long a) { return a << 3L; }
static unsigned long shr_ulong(unsigned long a, unsigned long n) { return a >> n; }
static int shr_assign(int a, long n) { a >>= n; return a; }
static unsigned char shl_assign_char(unsigned char a, long n) { a <<= n; return a; }
static int shl_global_count(int a) { return a >> rl[0]; }
static int shift_test(int a, long n) { if (!(a << n)) return 7; return 3; }

/* A long value assigned to a char. */
static unsigned char char_from_long(long a) { unsigned char o; o = a; return o; }
static signed char char_from_sum(long a, long b) { signed char o; o = a + b; return o; }
static unsigned char char_static(long a) { static unsigned char o; o = a; return o; }
static unsigned char char_global(long a, short b) { gc = a * b; return gc; }

/* An int or char compound-assigned a long from memory. */
static int add_global(int a) { a += gl; return a; }
static int sub_member(int a) { a -= sl.v; return a; }
static int mul_ptr(int a, long *p) { a *= *p; return a; }
static int add_array(int a, int i) { int x; x = a; x += rl[i]; return x; }
static unsigned char add_char_global(unsigned char a) { a += gl; return a; }
static int add_static(int a) { static int x; x = a; x += gl; return x; }

int main(void)
{
    long three = 3L;

    check("shl_int", shl_int(5, 2L), 20L);
    check("shr_unsigned", shr_unsigned(0x8000U, 4L), 0x0800L);
    check("shl_long_const", shl_long_const(0x10000L), 0x80000L);
    check("shr_ulong", (long)shr_ulong(0x80000000UL, 31UL), 1L);
    check("shr_assign", shr_assign(-64, 3L), -8L);
    check("shl_assign_char", shl_assign_char(0x81, 1L), 0x02L);
    check("shl_global_count", shl_global_count(10), 5L);
    check("shift_test", shift_test(0, 4L), 7L);
    check("char_from_long", char_from_long(0x12345678L), 0x78L);
    check("char_from_sum", char_from_sum(0x100L, -1L), -1L);
    check("char_static", char_static(0x1ffL), 0xffL);
    check("char_global", char_global(0x10001L, 3), 3L);
    check("add_global", add_global(5), (int)(5L + 70000L));
    check("sub_member", sub_member(5), 2L);
    check("mul_ptr", mul_ptr(7, &three), 21L);
    check("add_array", add_array(1, 1), 2L);
    check("add_char_global", add_char_global(1), (unsigned char)(1L + 70000L));
    check("add_static", add_static(1), (int)(1L + 70000L));
    if (fails == 0)
        printf("tlongcnv passed\n");
    return fails;
}
