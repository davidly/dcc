#include <stdio.h>

static int failures;

static void check_long(long got, long want, char *name)
{
    if (got != want) {
        printf("FAIL %s got=%ld want=%ld\n", name, got, want);
        failures++;
    }
}

static void check_ulong(
    unsigned long got, unsigned long want, char *name)
{
    if (got != want) {
        printf("FAIL %s got=%lu want=%lu\n", name, got, want);
        failures++;
    }
}

static long signed_big(long ignored)
{
    if (ignored == 1234567L)
        return ignored;
    return 70000L;
}

static unsigned long unsigned_big(unsigned long ignored)
{
    if (ignored == 1234567UL)
        return ignored;
    return 131072UL;
}

static void test_widen_mul_edges(void)
{
    int a, b, condition;
    unsigned int ua, ub;
    long value;

    a = 32767;
    b = 32767;
    check_long((long)a * b, 1073676289L, "s16mul maxpos");
    a = -32768;
    b = -32768;
    check_long((long)a * b, 1073741824L, "s16mul minneg");
    a = -32768;
    b = 2;
    check_long((long)a * b, -65536L, "s16mul minneg2");
    a = -32768;
    b = 32767;
    check_long((long)a * b, -1073709056L, "s16mul minmax");
    a = 32767;
    b = -32768;
    check_long((long)a * b, -1073709056L, "s16mul maxmin");

    ua = 65535U;
    ub = 65535U;
    check_ulong((unsigned long)ua * ub, 4294836225UL, "u16mul max");
    ua = 40000U;
    ub = 40000U;
    check_ulong((unsigned long)ua * ub, 1600000000UL, "u16mul 40000");

    a = 1;
    b = 2;
    check_long(((long)a + 65536L) * b, 131074L, "stale add mul");
    check_long(((long)a - 65536L) * b, -131070L, "stale sub mul");
    check_long((((long)a) | 0x10000L) * b, 131074L, "stale bor mul");
    check_long((((long)a) << 16) * b, 131072L, "stale shl mul");
    condition = 1;
    check_long(
        (condition ? 65536L : (long)a) * b,
        131072L, "stale cond true mul");
    condition = 0;
    check_long(
        (condition ? 65536L : (long)a) * b,
        2L, "stale cond false mul");

    ua = 65535U;
    ub = 2U;
    check_ulong(
        ((unsigned long)ua + 65536UL) * ub,
        262142UL, "ustale add mul");
    a = 1;
    b = 2;
    check_long(signed_big((long)a) * b, 140000L, "stale call mul");
    check_long(
        (value = signed_big((long)a)) * b,
        140000L, "stale assign call mul");
    value = 65536L;
    check_long(
        (value += (long)a) * b,
        131074L, "stale compound add mul");
    value = 65536L;
    check_long(
        (value *= (long)b) * b,
        262144L, "stale compound mul mul");
    ua = 1U;
    ub = 2U;
    check_ulong(
        unsigned_big((unsigned long)ua) * ub,
        262144UL, "ustale call mul");
#ifdef MIR_CLOBBER_WIDEN_EXTRA
    check_long(1L, 1L, "extra");
#endif
}

int main(void)
{
    test_widen_mul_edges();
    printf("widen failures=%d\n", failures);
    return failures != 0;
}
