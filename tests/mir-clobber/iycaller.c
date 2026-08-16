#include <stdio.h>

extern int clobber(void);

int external(void)
{
    return 0;
}

int main(void)
{
    int keep;
    int got;

    keep = 5000;
    got = clobber() + keep;
    printf("iy-module=%d\n", got);
    return got == 5018 ? 0 : 1;
}
