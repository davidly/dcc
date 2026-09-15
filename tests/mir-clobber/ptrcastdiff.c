/* Isolated runtime and structural controls for pointer-cast differences. */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

typedef float ftype;

#ifdef PTRCASTDIFF_VOLATILE_ARRAY
#define PTRCASTDIFF_ARRAY_QUALIFIER volatile
#else
#define PTRCASTDIFF_ARRAY_QUALIFIER
#endif

#ifdef PTRCASTDIFF_RENAMED
#define PTRCASTDIFF_FUNCTION fixture_pointer_cast_diff_renamed
#else
#define PTRCASTDIFF_FUNCTION fixture_pointer_cast_diff
#endif

#ifdef PTRCASTDIFF_CHANGED_COUNT
#define PTRCASTDIFF_FLOAT_COUNT (19 * 21)
#define PTRCASTDIFF_FLOAT_EXPECTED 1596
#else
#define PTRCASTDIFF_FLOAT_COUNT (20 * 20)
#define PTRCASTDIFF_FLOAT_EXPECTED 1600
#endif

#ifdef PTRCASTDIFF_ALT_FAILURE
#define PTRCASTDIFF_EXIT_CODE 2
#else
#define PTRCASTDIFF_EXIT_CODE 1
#endif

PTRCASTDIFF_ARRAY_QUALIFIER ftype fC[20][20];
int16_t iC[20][20];
ftype fD[400];

#ifdef PTRCASTDIFF_EXTRA_CFG
volatile int fixture_pointer_cast_guard;
#endif

int PTRCASTDIFF_FUNCTION()
{
    ftype *fp = (ftype *)fC;
    ftype *fpend =
        ((ftype *)fC) + PTRCASTDIFF_FLOAT_COUNT;
#ifdef PTRCASTDIFF_UNSIGNED_DIFF
    unsigned int fdiff = (int)fpend - (int)fp;
#else
    int fdiff = (int)fpend - (int)fp;
#endif

    int16_t *ip = (int16_t *)iC;
    int16_t *ipend = ((int16_t *)iC) + (20 * 20);
    int idiff = (int)ipend - (int)ip;

    ftype *dp = (ftype *)fD;
    ftype *dpend = ((ftype *)fD) + 400;
    int ddiff = (int)dpend - (int)dp;

#ifdef PTRCASTDIFF_EXTRA_CFG
    if (fixture_pointer_cast_guard)
        return 7;
#endif

    if (PTRCASTDIFF_FLOAT_EXPECTED != fdiff)
    {
        printf(
            "FAIL: 2D float array cast diff is %d, expected 1600\n",
            fdiff);
        exit(PTRCASTDIFF_EXIT_CODE);
    }

    if (800 != idiff)
    {
        printf(
            "FAIL: 2D int array cast diff is %d, expected 800\n",
            idiff);
        exit(PTRCASTDIFF_EXIT_CODE);
    }

    if (1600 != ddiff)
    {
        printf(
            "FAIL: 1D float array cast diff is %d, expected 1600\n",
            ddiff);
        exit(PTRCASTDIFF_EXIT_CODE);
    }

    printf("all pointer-cast diffs correct\n");
    return 0;
}

static int runtime_pointer_cast_checksum()
{
    ftype *fp = (ftype *)fC;
    ftype *fpend = ((ftype *)fC) + PTRCASTDIFF_FLOAT_COUNT;
    int16_t *ip = (int16_t *)iC;
    int16_t *ipend = ((int16_t *)iC) + (20 * 20);
    ftype *dp = (ftype *)fD;
    ftype *dpend = ((ftype *)fD) + 400;

    return ((int)fpend - (int)fp) +
        ((int)ipend - (int)ip) +
        ((int)dpend - (int)dp);
}

int main()
{
    int result = PTRCASTDIFF_FUNCTION();
    int checksum = runtime_pointer_cast_checksum();
    int expected =
        PTRCASTDIFF_FLOAT_EXPECTED + 800 + 1600;

    if (result != 0 || checksum != expected)
    {
        printf(
            "pointer cast diff failures=1 result=%d checksum=%d\n",
            result, checksum);
        return 1;
    }
    printf(
        "pointer cast diff failures=0 checksum=%d\n",
        checksum);
    return 0;
}
