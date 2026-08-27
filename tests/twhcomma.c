/*
 * twhcomma.c - regression coverage for large comma expressions in while
 * loop bodies.
 *
 * A while loop whose body is a single expression statement built from
 * many comma-separated sub-expressions used to fail to compile with
 * "error DCC-E1002: unsupported while condition or body" once past a fixed
 * capacity in the while-body fast path. This reproduces SDCC's
 * regression test gcc-torture-execute-pr28982a.c's MULTI()-macro shape
 * directly (each of NVARS unrolled iterations contributes two
 * comma-joined sub-expressions - a float accumulate and a pointer
 * advance): NVARS=16 (32 comma terms) compiled, while NVARS=17 (34 terms),
 * used here, failed.
 */
#include <stdio.h>

static int fails;

static void chk(const char *name, int got, int exp)
{
    if (got != exp) {
        printf("FAIL %s got %d expected %d\n", name, got, exp);
        fails++;
    }
}

#define NVARS 17
#define MULTI(X) \
    X( 0), X( 1), X( 2), X( 3), X( 4), X( 5), X( 6), X( 7), X( 8), X( 9), \
    X(10), X(11), X(12), X(13), X(14), X(15), X(16)
#define DECLAREI(INDEX) inc##INDEX = incs[INDEX]
#define DECLAREF(INDEX) *ptr##INDEX = ptrs[INDEX], result##INDEX = 0
#define LOOP(INDEX) result##INDEX += *ptr##INDEX, ptr##INDEX += inc##INDEX
#define COPYOUT(INDEX) results[INDEX] = result##INDEX

static float *ptrs[NVARS];
static float results[NVARS];
static int incs[NVARS];
static float input[4 * NVARS];

static void foo(int n)
{
    int MULTI(DECLAREI);
    float MULTI(DECLAREF);
    while (n--)
        MULTI(LOOP);
    MULTI(COPYOUT);
}

int main(void)
{
    int i;

    fails = 0;
    for (i = 0; i < NVARS; i++) {
        ptrs[i] = input + i;
        incs[i] = i;
    }
    for (i = 0; i < 4 * NVARS; i++)
        input[i] = (float)i;

    foo(4);

    chk("result0", (int)results[0], 0);
    chk("result1", (int)results[1], 10);

    if (fails) {
        printf("twhcomma failed: %d\n", fails);
        return 1;
    }
    printf("twhcomma completed with great success\n");
    return 0;
}
