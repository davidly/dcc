#include <stdio.h>

#ifdef _DCC_
#define TEST_UINT32_MAX 0xffffffffUL
#else
#define TEST_UINT32_MAX 0xffffffffU
#endif

struct B1 {
    unsigned a:TEST_UINT32_MAX + 2;
    unsigned b:3;
    unsigned c:4;
    unsigned d:8;
};

struct B2 {
    unsigned a:5;
    unsigned b:6;
    unsigned c:5;
    unsigned d:4;
};

static int fails;

static void chki(const char *name, int got, int expect)
{
    if (got != expect) {
        printf("FAIL %s got %d expected %d\n", name, got, expect);
        fails++;
    }
}

static void tbone(void)
{
    struct B1 x;

    x.a = 1;
    x.b = 5;
    x.c = 12;
    x.d = 170;

    chki("b1_a", x.a, 1);
    chki("b1_b", x.b, 5);
    chki("b1_c", x.c, 12);
    chki("b1_d", x.d, 170);

    x.b = 2;
    chki("b1_keep_a", x.a, 1);
    chki("b1_set_b", x.b, 2);
    chki("b1_keep_c", x.c, 12);
    chki("b1_keep_d", x.d, 170);

    x.c = 31;
    chki("b1_mask_c", x.c, 15);
}

static void tbtwo(void)
{
    struct B2 y;

    y.a = 17;
    y.b = 45;
    y.c = 23;
    y.d = 9;

    chki("b2_a", y.a, 17);
    chki("b2_b", y.b, 45);
    chki("b2_c", y.c, 23);
    chki("b2_d", y.d, 9);
}

int main(void)
{
    printf("tc89bit start\n");
    fails = 0;
    tbone();
    tbtwo();
    if (fails) {
        printf("tc89bit failed: %d\n", fails);
        return 1;
    }
    printf("tc89bit completed with great success\n");
    return 0;
}
