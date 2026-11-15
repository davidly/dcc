/* Deferred block float unary, assignment-value, mixed arithmetic, compound
 * arithmetic and conditional consumers preserve their numeric conversions. */
#include <stdint.h>
#include <stdio.h>
static float result;
static uint16_t flag = 1, word = 3;
static uint32_t bits(float f)
{
    union    { float f; uint32_t u; } value;
    value.f = f;
    return value.u;
}
int main(void)
{
    { float b =  1.5f; result = -b; }
    printf("unary %08lx\n",(unsigned long)bits(result));
    { float b =  1.5f; result = -(b = 2.5f); }
    printf("assignment %08lx\n",(unsigned long)bits(result));
    { float b =  1.5f; result = b + word; }
    printf("mixed %08lx\n",(unsigned long)bits(result));
    { float b =  1.5f; result = word + -b; }
    printf("reverse %08lx\n",(unsigned long)bits(result));
    { float b =  1.5f; result = b += word; }
    printf("compound %08lx\n",(unsigned long)bits(result));
    { float b =  1.0f, c =  2.0f; result = 7 + (flag ? b : c); }
    printf("phi %08lx\n",(unsigned long)bits(result));
    { float b =  1.0f; result = (flag ? b : word) + 7; }
    printf("mixed phi %08lx\n",(unsigned long)bits(result));
    { float b =  1.5f; result = (-b + (uint16_t)1) * (uint16_t)2; }
    printf("chain %08lx\n",(unsigned long)bits(result));
    return 0;
}
