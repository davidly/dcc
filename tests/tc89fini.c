/* tc89flinit.c -- float initializers using expressions in braced lists */
#include <stdio.h>

#ifndef MIR_CLOBBER_ARR_LIT0_EXPECT
#define MIR_CLOBBER_ARR_LIT0_EXPECT 1.5f
#endif

static int fails;

static void chkf(const char *name, float got, float expect)
{
    if (got != expect) {
        printf("FAIL %s got %f expected %f\n", name, got, expect);
        fails++;
    }
}

struct SF {
    char c;
    float f;
    int i;
};

float gbase = 1.5f;

int main(void)
{
    float a, b;
    float arr[3];
    struct SF s;

    a = 2.0f;
    b = 3.0f;

    /* float array: literal constants */
    {
        float la[3] = { 1.5f, -2.5f, 0.0f };
        chkf("arr_lit0", la[0], MIR_CLOBBER_ARR_LIT0_EXPECT);
        chkf("arr_lit1", la[1], -2.5f);
        chkf("arr_lit2", la[2], 0.0f);
    }

    /* float array: expression initializers */
    {
        float la[3] = { a + b, a * 2.0f, b - a };
        chkf("arr_expr0", la[0], 5.0f);
        chkf("arr_expr1", la[1], 4.0f);
        chkf("arr_expr2", la[2], 1.0f);
    }

    /* float array: mixed literal and expression */
    {
        float la[3] = { 1.0f, a + 1.0f, b };
        chkf("arr_mix0", la[0], 1.0f);
        chkf("arr_mix1", la[1], 3.0f);
        chkf("arr_mix2", la[2], 3.0f);
    }

    /* float array: global variable as initializer */
    {
        float la[2] = { gbase, gbase + 1.0f };
        chkf("arr_glob0", la[0], 1.5f);
        chkf("arr_glob1", la[1], 2.5f);
    }

    /* struct float field: literal constant */
    {
        struct SF ls = { 'x', 7.5f, 42 };
        chkf("sf_lit", ls.f, 7.5f);
    }

    /* struct float field: expression initializer */
    {
        struct SF ls = { 'y', a + b, 99 };
        chkf("sf_expr", ls.f, 5.0f);
    }

    /* struct float field: global variable */
    {
        struct SF ls = { 'z', gbase, 0 };
        chkf("sf_glob", ls.f, 1.5f);
    }

    if (fails) {
        printf("tc89flinit FAILED: %d\n", fails);
        return 1;
    }
    printf("tc89fini completed with great success\n");
    return 0;
}
