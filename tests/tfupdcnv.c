/* Block-local float updates that reach typed unary/binary MIR materialize
 * their arithmetic conversion once the deferred type resolves to float.
 * Promoted from BUG-059 (cluster D2e-2). */
#include <stdint.h>
#include <stdio.h>

static float result;
static uint32_t bits(float f)
{
    union { float f; uint32_t u; } value;
    value.f = f;
    return value.u;
}
static float control(float f)
{
    return -f + (uint16_t)1;
}
int main(void)
{
    { float local = 1.5f; result = -(local++) + (uint16_t)1; }
    printf("post %08lx\n", (unsigned long)bits(result));
    { float local = 2.5f; result = -(--local) + (uint16_t)1; }
    printf("pre %08lx\n", (unsigned long)bits(result));
    { float local = 1.5f; result = (uint16_t)1 + -(local++); }
    printf("reverse %08lx\n", (unsigned long)bits(result));
    { float local = 1.5f; result = (-(local++) + (uint16_t)1) * (uint16_t)2; }
    printf("chain %08lx\n", (unsigned long)bits(result));
    printf("control %08lx\n", (unsigned long)bits(control(1.5f)));
    return 0;
}
