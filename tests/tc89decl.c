/* tc89decl.c - C89 declarator grammar regression */
#include <stdio.h>

static int fails;

static void chk(const char *n, int got, int exp)
{
    if (got != exp) {
        printf("FAIL %s got %d expected %d\n", n, got, exp);
        fails++;
    }
}

static int add1(int x) { return x + 1; }
static int add2(int x) { return x + 2; }

typedef int (*PFN)(int);

static void tdcl(void)
{
    int *p, a, arr[3];
    int x;

    x = 7;
    p = &x;
    a = 5;
    arr[0] = 10;
    arr[1] = 11;
    arr[2] = 12;

    chk("multi_ptr", *p, 7);
    chk("multi_scalar", a, 5);
    chk("multi_array", arr[2], 12);
}

static void tfpn(void)
{
    int (*fp)(int), y;
    fp = add1;
    y = fp(20);
    chk("fp_decl", y, 21);
}

static void tfpa(void)
{
    int (*tab[2])(int), y;
    tab[0] = add1;
    tab[1] = add2;
    y = tab[0](30) + tab[1](40);
    chk("fp_array", y, 73);
}

static void ttyp(void)
{
    PFN f, table[2];
    int y;
    f = add2;
    table[0] = add1;
    table[1] = f;
    y = table[0](2) + table[1](3);
    chk("typedef_fp", y, 8);
}

static timpreg(a, b)
    register a;
    register int b;
{
    register c;
    c = a + b;
    return c;
}

int main(void)
{
    printf("tc89decl start\n");
    fails = 0;
    tdcl();
    tfpn();
    tfpa();
    ttyp();
    chk("implicit_register_int", timpreg(11, 31), 42);
    if (fails) {
        printf("tc89decl failed: %d\n", fails);
        return 1;
    }
    printf("tc89decl completed with great success\n");
    return 0;
}
