/* Block-local wide update values preserve deferred operand types through
 * unary negation, mixed-width arithmetic and comparisons. Explicit casts
 * remain type boundaries; a global update is the resolved-type control. */
#include <stdint.h>
#include <stdio.h>
static uint32_t result;
static uint32_t scalar = 65536UL;
int main(void)
{
    result = -(scalar++) + (uint16_t)1;
    printf("global %08lx %08lx\n", (unsigned long)result, (unsigned long)scalar);
    { uint32_t local = 65536UL; result = -(local++) + (uint16_t)1; }
    printf("block post %08lx\n", (unsigned long)result);
    { uint32_t local = 65537UL; result = -(--local) + (uint16_t)1; }
    printf("block pre %08lx\n", (unsigned long)result);
    { uint32_t local = 65536UL; result = (uint16_t)1 + -(local++); }
    printf("reverse %08lx\n", (unsigned long)result);
    { uint32_t local = 65536UL; printf("compare %d\n", local++ > (uint16_t)1); }
    { uint32_t local = 65536UL; printf("neg compare %d\n", -(local++) < (uint16_t)1); }
    { uint32_t local = 65536UL; result = (uint16_t)-(local++) + (uint16_t)1; }
    printf("cast fence %08lx\n", (unsigned long)result);
    return 0;
}
