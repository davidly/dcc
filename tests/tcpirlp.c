#include <stdio.h>
#include <stdlib.h>

/* Regression test for a real dccpeep bug: pass_cpir used to replace this
 * exact loop shape (mirroring tests/tm.c's chkmem) with a raw CPIR
 * instruction, which stops at the FIRST byte that matches the target value
 * instead of verifying every byte - silently missing any corruption after
 * byte 0 (the common case, since byte 0 almost always already matches the
 * fill value). This deliberately corrupts a non-first byte; detecting it
 * (and reporting the correct position) is this test's expected, passing
 * output - unlike most tests, printing "success" here would mean the bug
 * has come back. */
static void chk(char *p, int v, size_t c)
{
    register unsigned char *pc = (unsigned char *)p;
    unsigned char val = (unsigned char)(v & 0xff);

    for (size_t i = 0; i < c; i++)
    {
        if (*pc != val)
        {
            /* Report via *pc and pc's offset (both driven by the pointer
             * the fast path rewrites on mismatch), not the loop counter i -
             * the CPI-loop replacement has no reason to keep i itself
             * updated once it takes over the counting in hardware (BC),
             * same as the real chkmem's own diagnostic never reads i. */
            printf("detected mismatch at offset %u, byte %d\n",
                   (unsigned)(pc - (unsigned char *)p), (int)*pc);
            exit(1);
        }
        pc++;
    }
    printf("FAIL did not detect corruption\n");
}

int main(void)
{
    unsigned char buf[20];
    int i;

    for (i = 0; i < 20; i++)
        buf[i] = 0x42;
    buf[7] = 0x99; /* deliberate, non-first-byte corruption */

    chk((char *)buf, 0x42, 20);
    return 0;
}
