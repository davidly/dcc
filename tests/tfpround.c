/* Exact IEEE single-bit decimal formatting, nearest-even ties, carries,
 * signed zero and subnormal fractions. No division or libm is involved. */
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
    b.u = UINT32_C(0x3f000000);
    printf("half %.0f\n", b.f);
    b.u = UINT32_C(0x40200000);
    printf("even %.0f\n", b.f);
    b.u = UINT32_C(0xbf000000);
    printf("negative half %.0f\n", b.f);
    b.u = UINT32_C(0x3fa00000);
    printf("decimal tie %.1f %.2f\n", b.f, b.f);
    b.u = UINT32_C(0x40080000);
    printf("hundredth tie %.2f\n", b.f);
    b.u = UINT32_C(0x3f7fffff);
    printf("carry %.6f\n", b.f);
    b.u = UINT32_C(1);
    printf("subnormal %.45f\n", b.f);
    return 0;
}
