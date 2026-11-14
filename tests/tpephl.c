/* Global-source byte self-operands retain resident HL through peephole
 * optimization. Covers unsigned/signed subtraction, addition and AND,
 * with parameter and literal sources as controls. */
#include <stdint.h>
#include <stdio.h>
struct S { uint16_t a[2]; int16_t b; uint16_t c; };
static struct S s = { { 11, 32767 }, 97, 1 };
int8_t a6[8] = { 0, 127, 31, 11, 0, 47, 10, -92 };
int16_t a1[8] = { 129, 32767, -151, 15, 32597, 255, 24, 27921 };
uint32_t sub(int8_t p)
{
    uint8_t a = 0, b = 0;
    uint32_t c = 0;
    a = s.c;
    b = a - a;
    c = a6[a1[b & 7] & 7];
    return c;
}

uint32_t add(int8_t p)
{
    uint8_t a = 0, b = 0;
    uint32_t c = 0;
    a = s.c;
    b = a + a;
    c = a6[a1[b & 7] & 7];
    return c;
}

uint32_t and(int8_t p)
{
    uint8_t a = 0, b = 0;
    uint32_t c = 0;
    a = s.c;
    b = a & a;
    c = a6[a1[b & 7] & 7];
    return c;
}

uint32_t sbyte(int8_t p)
{
    int8_t a = 0, b = 0;
    uint32_t c = 0;
    a = s.c;
    b = a - a;
    c = a6[a1[b & 7] & 7];
    return c;
}

uint32_t param(int8_t p)
{
    uint8_t a = 0, b = 0;
    uint32_t c = 0;
    a = (uint16_t)p;
    b = a - a;
    c = a6[a1[b & 7] & 7];
    return c;
}

uint32_t literal(int8_t p)
{
    uint8_t a = 0, b = 0;
    uint32_t c = 0;
    a = 1;
    b = a - a;
    c = a6[a1[b & 7] & 7];
    return c;
}

int main(void)
{
    printf("sub %lu\n", (unsigned long)sub(1));
    printf("add %lu\n", (unsigned long)add(1));
    printf("and %lu\n", (unsigned long)and(1));
    printf("signed %lu\n", (unsigned long)sbyte(1));
    printf("param %lu\n", (unsigned long)param(1));
    printf("literal %lu\n", (unsigned long)literal(1));
    return 0;
}
