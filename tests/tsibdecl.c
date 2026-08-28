/*
 * Reused names in sibling blocks denote distinct objects, including when the
 * declarations change width or signedness.  Found via GCC arith-rand.c.
 */
#include <stdio.h>

static int failures;

static void check(int condition)
{
    if (!condition)
        ++failures;
}

int main(void)
{
    unsigned long x = 3758097407UL;
    unsigned long y = 4294769664UL;

    {
        unsigned long xx = 1023UL, yy = 49152UL, r1, r2;
        r1 = xx / yy;
        r2 = xx % yy;
        check(r2 < yy);
        check(r1 * yy + r2 == xx);
    }
    {
        signed long xx = (signed long)x, yy = (signed long)y, r1, r2;
        r1 = xx / yy;
        r2 = xx % yy;
        check((unsigned long)(r2 >= 0 ? r2 : -r2) <
              (unsigned long)(yy >= 0 ? yy : -yy));
        check(r1 * yy + r2 == xx);
    }
    {
        unsigned int xx = 1023U, yy = 49152U, r1, r2;
        r1 = xx / yy;
        r2 = xx % yy;
        check(r2 < yy);
        check(r1 * yy + r2 == xx);
    }
    {
        signed int xx = -12345, yy = 97, r1, r2;
        r1 = xx / yy;
        r2 = xx % yy;
        check(r1 * yy + r2 == xx);
    }

    if (failures) {
        printf("tsibdecl failed: %d\n", failures);
        return 1;
    }
    printf("tsibdecl completed with great success\n");
    return 0;
}
