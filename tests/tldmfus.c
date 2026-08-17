/* Long div/mod fusion, including signed remainder and unsigned high-bit data. */

#include <stdio.h>

static int checks;
static int failures;

static void cks(long got, long want, const char *name)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s got=%ld want=%ld\n", name, got, want);
    }
}

static void cku(unsigned long got, unsigned long want, const char *name)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s got=%lu want=%lu\n", name, got, want);
    }
}

int main(void)
{
    {
        long x = -123456789L;
        long y = 12345L;
        long r = x % y;
        long q = x / y;
        cks(q, -10000L, "sq1");
        cks(r, -6789L, "sr1");
    }
    {
        long x = 123456789L;
        long y = -12345L;
        long q = x / y;
        long r = x % y;
        cks(q, -10000L, "sq2");
        cks(r, 6789L, "sr2");
    }
    {
        unsigned long x = 0xfedcba98UL;
        unsigned long y = 70001UL;
        unsigned long r = x % y;
        unsigned long q = x / y;
        cku(q, 61083UL, "uq1");
        cku(r, 7469UL, "ur1");
    }
    {
        unsigned long x = 0xffffffffUL;
        unsigned long y = 65537UL;
        unsigned long q = x / y;
        unsigned long r = x % y;
        cku(q, 65535UL, "uq2");
        cku(r, 0UL, "ur2");
    }
    printf("checks=%d failures=%d\n", checks, failures);
    puts("RESULT: PASS");
    return failures != 0;
}
