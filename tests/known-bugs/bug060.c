/* BUG-060: %f decimal rounding differs from the host for exact float bits.
 * No division or libm call: this isolates formatting from BUG-039. */
#include <stdint.h>
#include <stdio.h>

typedef union { float f; uint32_t u; } bits;

int main(void)
{
    bits b;
    b.u = UINT32_C(0x41649249);
    printf("100/7 bits %08lx %.6f\n", (unsigned long)b.u, b.f);
    b.u = UINT32_C(0xc0b55555);
    printf("-17/3 bits %08lx %.6f\n", (unsigned long)b.u, b.f);
    b.u = UINT32_C(0x40b55555);
    printf("17/3 bits %08lx %.6f\n", (unsigned long)b.u, b.f);
    b.u = UINT32_C(0x3fc00000);
    printf("exact bits %08lx %.6f\n", (unsigned long)b.u, b.f);
    return 0;
}
