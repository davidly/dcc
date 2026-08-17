/* Explicit signed/unsigned long casts must control div/mod helper selection. */

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
        long x = -1L;
        long y = 2L;
        unsigned long q = (unsigned long)x / y;
        unsigned long r = (unsigned long)x % y;
        cku(q, 2147483647UL, "ulhs-q");
        cku(r, 1UL, "ulhs-r");
    }
    {
        long x = -2L;
        long y = -1L;
        unsigned long q = x / (unsigned long)y;
        unsigned long r = x % (unsigned long)y;
        cku(q, 0UL, "urhs-q");
        cku(r, 4294967294UL, "urhs-r");
    }
    {
        unsigned long x = 4294967294UL;
        long y = 2L;
        long q = (long)x / y;
        long r = (long)x % y;
        cks(q, -1L, "slhs-q");
        cks(r, 0L, "slhs-r");
    }
    {
        long x = -7L;
        unsigned long y = 4294967294UL;
        long q = x / (long)y;
        long r = x % (long)y;
        cks(q, 3L, "srhs-q");
        cks(r, -1L, "srhs-r");
    }
    printf("checks=%d failures=%d\n", checks, failures);
    puts("RESULT: PASS");
    return failures != 0;
}
