/* tarray6.c - C89 regression test for required 6-dimensional arrays.
 *
 * C89 translation limits require support for at least 6 array dimensions.
 *
 * Expected output:
 * tarray6 start
 * PASS
 */

#include <stdio.h>

#ifdef _DCC_
#define TEST_UINT32_MAX 0xffffffffUL
#else
#define TEST_UINT32_MAX 0xffffffffU
#endif

static int fails;

int ga[TEST_UINT32_MAX + 3][2][2][2][2][2];
char gc[2][2][2][2][2][2];
long gl[2][2][2][2][2][2];

static void check_int(name, got, exp)
char *name;
int got;
int exp;
{
    if (got != exp) {
        printf("FAIL %s got %d expected %d\n", name, got, exp);
        fails++;
    }
}

static void check_long(name, got, exp)
char *name;
long got;
long exp;
{
    if (got != exp) {
        printf("FAIL %s got %ld expected %ld\n", name, got, exp);
        fails++;
    }
}

static int v6(a, b, c, d, e, f)
int a; int b; int c; int d; int e; int f;
{
    return a * 32 + b * 16 + c * 8 + d * 4 + e * 2 + f;
}

static void fill_i(a, base)
int a[2][2][2][2][2][2];
int base;
{
    int a0, a1, a2, a3, a4, a5;
    for (a0 = 0; a0 < 2; a0++)
    for (a1 = 0; a1 < 2; a1++)
    for (a2 = 0; a2 < 2; a2++)
    for (a3 = 0; a3 < 2; a3++)
    for (a4 = 0; a4 < 2; a4++)
    for (a5 = 0; a5 < 2; a5++)
        a[a0][a1][a2][a3][a4][a5] = base + v6(a0,a1,a2,a3,a4,a5);
}

static void fill_c(a, base)
char a[2][2][2][2][2][2];
int base;
{
    int a0, a1, a2, a3, a4, a5;
    for (a0 = 0; a0 < 2; a0++)
    for (a1 = 0; a1 < 2; a1++)
    for (a2 = 0; a2 < 2; a2++)
    for (a3 = 0; a3 < 2; a3++)
    for (a4 = 0; a4 < 2; a4++)
    for (a5 = 0; a5 < 2; a5++)
        a[a0][a1][a2][a3][a4][a5] =
            (char)(base + v6(a0,a1,a2,a3,a4,a5));
}

static void fill_l(a, base)
long a[2][2][2][2][2][2];
long base;
{
    int a0, a1, a2, a3, a4, a5;
    for (a0 = 0; a0 < 2; a0++)
    for (a1 = 0; a1 < 2; a1++)
    for (a2 = 0; a2 < 2; a2++)
    for (a3 = 0; a3 < 2; a3++)
    for (a4 = 0; a4 < 2; a4++)
    for (a5 = 0; a5 < 2; a5++)
        a[a0][a1][a2][a3][a4][a5] =
            base + (long)v6(a0,a1,a2,a3,a4,a5);
}

static int sum_i(a)
int a[2][2][2][2][2][2];
{
    int a0, a1, a2, a3, a4, a5;
    int sum = 0;
    for (a0 = 0; a0 < 2; a0++)
    for (a1 = 0; a1 < 2; a1++)
    for (a2 = 0; a2 < 2; a2++)
    for (a3 = 0; a3 < 2; a3++)
    for (a4 = 0; a4 < 2; a4++)
    for (a5 = 0; a5 < 2; a5++)
        sum += a[a0][a1][a2][a3][a4][a5];
    return sum;
}

static int sum_c(a)
char a[2][2][2][2][2][2];
{
    int a0, a1, a2, a3, a4, a5;
    int sum = 0;
    for (a0 = 0; a0 < 2; a0++)
    for (a1 = 0; a1 < 2; a1++)
    for (a2 = 0; a2 < 2; a2++)
    for (a3 = 0; a3 < 2; a3++)
    for (a4 = 0; a4 < 2; a4++)
    for (a5 = 0; a5 < 2; a5++)
        sum += (int)a[a0][a1][a2][a3][a4][a5];
    return sum;
}

static long sum_l(a)
long a[2][2][2][2][2][2];
{
    int a0, a1, a2, a3, a4, a5;
    long sum = 0L;
    for (a0 = 0; a0 < 2; a0++)
    for (a1 = 0; a1 < 2; a1++)
    for (a2 = 0; a2 < 2; a2++)
    for (a3 = 0; a3 < 2; a3++)
    for (a4 = 0; a4 < 2; a4++)
    for (a5 = 0; a5 < 2; a5++)
        sum += a[a0][a1][a2][a3][a4][a5];
    return sum;
}

static void mutate_i(a)
int a[2][2][2][2][2][2];
{
    a[1][0][1][0][1][0] += 1000;
    *(&a[0][1][0][1][0][1]) += 2000;
}

static void mutate_c(a)
char a[2][2][2][2][2][2];
{
    a[1][1][0][0][1][1] = (char)99;
    *(&a[0][0][1][1][0][0]) = (char)77;
}

static void mutate_l(a)
long a[2][2][2][2][2][2];
{
    a[1][1][1][0][0][1] += 100000L;
    *(&a[0][0][0][1][1][0]) += 200000L;
}

static void row_i(a)
int a[2][2][2][2][2][2];
{
    int *p;
    p = &a[0][0][0][0][0][0];
    check_int("ri0", p[0], a[0][0][0][0][0][0]);
    check_int("ri1", p[1], a[0][0][0][0][0][1]);
    check_int("ri2", p[2], a[0][0][0][0][1][0]);
    check_int("ri3", p[3], a[0][0][0][0][1][1]);
    check_int("ri63", p[63], a[1][1][1][1][1][1]);
}

static void row_c(a)
char a[2][2][2][2][2][2];
{
    char *p;
    p = &a[0][0][0][0][0][0];
    check_int("rc0", (int)p[0], (int)a[0][0][0][0][0][0]);
    check_int("rc1", (int)p[1], (int)a[0][0][0][0][0][1]);
    check_int("rc2", (int)p[2], (int)a[0][0][0][0][1][0]);
    check_int("rc3", (int)p[3], (int)a[0][0][0][0][1][1]);
    check_int("rc63", (int)p[63], (int)a[1][1][1][1][1][1]);
}

static void row_l(a)
long a[2][2][2][2][2][2];
{
    long *p;
    p = &a[0][0][0][0][0][0];
    check_long("rl0", p[0], a[0][0][0][0][0][0]);
    check_long("rl1", p[1], a[0][0][0][0][0][1]);
    check_long("rl2", p[2], a[0][0][0][0][1][0]);
    check_long("rl3", p[3], a[0][0][0][0][1][1]);
    check_long("rl63", p[63], a[1][1][1][1][1][1]);
}

int main()
{
    int la[2][2][2][2][2][2];
    char lc[2][2][2][2][2][2];
    long ll[2][2][2][2][2][2];

    printf("tarray6 start\n");

    fill_i(ga, 100);
    fill_i(la, 200);
    fill_c(gc, 10);
    fill_c(lc, 20);
    fill_l(gl, 100000L);
    fill_l(ll, 200000L);

    check_int("g_i000000", ga[0][0][0][0][0][0], 100);
    check_int("g_i111111", ga[1][1][1][1][1][1], 163);
    check_int("l_i000000", la[0][0][0][0][0][0], 200);
    check_int("l_i111111", la[1][1][1][1][1][1], 263);

    check_int("g_c000000", (int)gc[0][0][0][0][0][0], 10);
    check_int("g_c111111", (int)gc[1][1][1][1][1][1], 73);
    check_int("l_c000000", (int)lc[0][0][0][0][0][0], 20);
    check_int("l_c111111", (int)lc[1][1][1][1][1][1], 83);

    check_long("g_l000000", gl[0][0][0][0][0][0], 100000L);
    check_long("g_l111111", gl[1][1][1][1][1][1], 100063L);
    check_long("l_l000000", ll[0][0][0][0][0][0], 200000L);
    check_long("l_l111111", ll[1][1][1][1][1][1], 200063L);

    check_int("sum_gi", sum_i(ga), 8416);
    check_int("sum_li", sum_i(la), 14816);
    check_int("sum_gc", sum_c(gc), 2656);
    check_int("sum_lc", sum_c(lc), 3296);
    check_long("sum_gl", sum_l(gl), 6402016L);
    check_long("sum_ll", sum_l(ll), 12802016L);

    mutate_i(ga);
    check_int("mut_i1", ga[1][0][1][0][1][0], 1142);
    check_int("mut_i2", ga[0][1][0][1][0][1], 2121);

    mutate_c(gc);
    check_int("mut_c1", (int)gc[1][1][0][0][1][1], 99);
    check_int("mut_c2", (int)gc[0][0][1][1][0][0], 77);

    mutate_l(gl);
    check_long("mut_l1", gl[1][1][1][0][0][1], 200057L);
    check_long("mut_l2", gl[0][0][0][1][1][0], 300006L);

    row_i(ga);
    row_i(la);
    row_c(gc);
    row_c(lc);
    row_l(gl);
    row_l(ll);

    if (fails) {
        printf("FAILED %d\n", fails);
        return 1;
    }

    printf("PASS\n");
    return 0;
}
