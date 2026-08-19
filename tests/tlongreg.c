/*
 * tlongreg.c - focused long/unsigned long regression tests for dcc
 *
 * This supplements tlong.c and tlongaud.c with the unaudited long cases:
 * postfix ++/--, variable/compound shifts, long comparisons, mixed-width
 * compound assignments, and long argument/return preservation.
 */

#include <stdio.h>

static int failures;
static unsigned long gul;
static long gsl;

static void ck(const char *name, unsigned long got, unsigned long exp)
{
    if (got != exp) {
        printf("FAIL %s got %lu expected %lu\n", name, got, exp);
        failures++;
    }
}

static void cki(const char *name, int got, int exp)
{
    if (got != exp) {
        printf("FAIL %s got %d expected %d\n", name, got, exp);
        failures++;
    }
}

static long idsl(long a)
{
    return a;
}

static unsigned long idul(unsigned long a)
{
    return a;
}

static unsigned long mixargs(int a, unsigned long b, long c, unsigned int d)
{
    return (unsigned long)a + b + (unsigned long)c + (unsigned long)d;
}

static unsigned long ret_high_only(void)
{
    return 0x00010000UL;
}

static long ret_negative_one(void)
{
    return -1L;
}

static void use_after_long_return(void)
{
    unsigned long x;
    x = ret_high_only();
    ck("return high-only", x, 0x00010000UL);
    ck("call cleanup preserves de", idul(0x12345678UL), 0x12345678UL);
}

static void test_postfix(void)
{
    unsigned long ul;
    unsigned long old;
    long sl;
    long sold;

    ul = 0x0000ffffUL;
    old = ul++;
    ck("postinc old carry", old, 0x0000ffffUL);
    ck("postinc new carry", ul, 0x00010000UL);

    old = ul--;
    ck("postdec old borrow", old, 0x00010000UL);
    ck("postdec new borrow", ul, 0x0000ffffUL);

    ul = 0xffffffffUL;
    old = ul++;
    ck("postinc old wrap", old, 0xffffffffUL);
    ck("postinc new wrap", ul, 0UL);

    old = ul--;
    ck("postdec old wrap", old, 0UL);
    ck("postdec new wrap", ul, 0xffffffffUL);

    sl = -1L;
    sold = sl++;
    ck("postinc signed old", (unsigned long)sold, 0xffffffffUL);
    ck("postinc signed new", (unsigned long)sl, 0UL);

    gsl = 0x0000ffffL;
    sold = gsl++;
    ck("postinc global old", (unsigned long)sold, 0x0000ffffUL);
    ck("postinc global new", (unsigned long)gsl, 0x00010000UL);

    gul = 0x00010000UL;
    old = gul--;
    ck("postdec global old", old, 0x00010000UL);
    ck("postdec global new", gul, 0x0000ffffUL);
}

static void test_shifts(void)
{
    unsigned long ul;
    long sl;
    int n;

    ul = 1UL;
    n = 16;
    ck("var lshift 16", ul << n, 0x00010000UL);
    n = 17;
    ck("var lshift 17", ul << n, 0x00020000UL);
    n = 31;
    ck("var lshift 31", ul << n, 0x80000000UL);

    ul = 0x80000000UL;
    n = 16;
    ck("var urshift 16", ul >> n, 0x00008000UL);
    n = 31;
    ck("var urshift 31", ul >> n, 1UL);

    sl = (long)0x80000000UL;
    n = 16;
    ck("var srshift 16", (unsigned long)(sl >> n), 0xffff8000UL);
    n = 31;
    ck("var srshift 31", (unsigned long)(sl >> n), 0xffffffffUL);

    ul = 1UL;
    n = 16;
    ul <<= n;
    ck("compound lshift var 16", ul, 0x00010000UL);
    n = 1;
    ul <<= n;
    ck("compound lshift var carry", ul, 0x00020000UL);
    n = 17;
    ul >>= n;
    ck("compound urshift var 17", ul, 1UL);

    sl = (long)0x80000000UL;
    n = 17;
    sl >>= n;
    ck("compound srshift var 17", (unsigned long)sl, 0xffffc000UL);
}

static void test_compares(void)
{
    long a;
    long b;
    unsigned long ua;
    unsigned long ub;
    int i;
    unsigned int ui;

    a = (long)0x80000000UL;
    b = 0L;
    cki("signed min < zero", a < b, 1);
    cki("signed min > zero", a > b, 0);
    cki("signed min <= zero", a <= b, 1);
    cki("signed min >= zero", a >= b, 0);

    ua = 0x80000000UL;
    ub = 0x7fffffffUL;
    cki("unsigned high > low", ua > ub, 1);
    cki("unsigned high < low", ua < ub, 0);
    cki("unsigned high >= low", ua >= ub, 1);
    cki("unsigned high <= low", ua <= ub, 0);

    a = -1L;
    i = 1;
    cki("mixed signed long < int", a < i, 1);
    cki("mixed signed long > int", a > i, 0);

    ua = 0xffffffffUL;
    ui = 1U;
    cki("mixed unsigned long > uint", ua > ui, 1);
    cki("mixed unsigned long < uint", ua < ui, 0);

    cki("eq high-only", 0x00010000UL == ret_high_only(), 1);
    cki("ne high-only", 0x00010000UL != ret_high_only(), 0);
    cki("call signed < zero", ret_negative_one() < 0L, 1);
    cki("call signed >= zero", ret_negative_one() >= 0L, 0);
    cki("call signed > -2", ret_negative_one() > -2L, 1);
    cki("call signed <= -2", ret_negative_one() <= -2L, 0);
}

static void test_compound(void)
{
    long sl;
    unsigned long ul;
    int i;
    unsigned int ui;

    sl = 0x0000ffffL;
    i = 1;
    sl += i;
    ck("compound += int carry", (unsigned long)sl, 0x00010000UL);
    sl -= i;
    ck("compound -= int borrow", (unsigned long)sl, 0x0000ffffUL);

    ul = 0x00010000UL;
    ui = 1U;
    ul -= ui;
    ck("compound -= uint borrow", ul, 0x0000ffffUL);
    ul += ui;
    ck("compound += uint carry", ul, 0x00010000UL);

    ul = 0x00010000UL;
    ul *= 3U;
    ck("compound *= uint high", ul, 0x00030000UL);
    ul /= 3U;
    ck("compound /= uint high", ul, 0x00010000UL);
    ul %= 65535U;
    ck("compound %= uint high", ul, 1UL);

    ul = 0x12345678UL;
    ul &= 0x00ff00ffUL;
    ck("compound &= long", ul, 0x00340078UL);
    ul |= 0x12005600UL;
    ck("compound |= long", ul, 0x12345678UL);
    ul ^= 0xffffffffUL;
    ck("compound ^= long", ul, 0xedcba987UL);
}

static void test_args(void)
{
    unsigned long r;

    r = mixargs(1, 0x00010000UL, -1L, 2U);
    ck("mixed args preserve words", r, 0x00010002UL);

    r = idul(idsl(-1L));
    ck("signed return to unsigned arg", r, 0xffffffffUL);

    r = idul(0x87654321UL) + idul(0x00000001UL);
    ck("two long calls in expr", r, 0x87654322UL);
}

int main(void)
{
    test_postfix();
    test_shifts();
    test_compares();
    test_compound();
    test_args();
    use_after_long_return();

    if (failures) {
        printf("tlongreg: %d failure(s)\n", failures);
        return 1;
    }

    printf("tlongreg: all tests passed with great success\n");
    return 0;
}
