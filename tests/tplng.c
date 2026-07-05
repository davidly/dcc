/* tplng.c - printf long-only runtime entry coverage (_pflng). */
#include <stdio.h>

int main(void)
{
    long sl;
    unsigned long ul;

    sl = -123456L;
    ul = 0x89ABCDEFUL;

    printf("tplng start\n");
    printf("sl=%ld\n", sl);
    printf("ul=%lu\n", ul);
    printf("hex=%lx\n", ul);
    printf("wide=[%12ld]\n", sl);
    printf("mix=%d %ld %s\n", 42, 1234567L, "ok");
    printf("tplng ok\n");
    return 0;
}