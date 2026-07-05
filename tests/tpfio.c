/* tpfio.c - printf float-only runtime entry coverage (_pffio). */
#include <stdio.h>

int main(void)
{
    float a;
    float b;

    a = 1.25f;
    b = 2.5f;

    printf("tpfio start\n");
    printf("a=%f\n", a);
    printf("b=%.2f\n", b);
    printf("sum=%f\n", a + b);
    printf("mix=%d %.1f %s\n", 7, b, "ok");
    printf("tpfio ok\n");
    return 0;
}