/* Block integer operands keep numeric float conversions for binary and
 * compound arithmetic; repaired float conditional values feed integers. */
#include <stdint.h>
#include <stdio.h>

static float result;
static uint16_t flag = 1;
static int32_t integer;

static uint32_t bits(float f)
{
    union { float f; uint32_t u; } value;
    value.f = f;
    return value.u;
}

int main(void)
{
    { int16_t b = -3; result = b + 1.0f; }
    printf("integer binary %08lx\n", (unsigned long)bits(result));
    { uint8_t b = 3; result = 1.0f; result += b; }
    printf("integer compound %08lx\n", (unsigned long)bits(result));
    { float b = 1.0f, c = 2.0f; integer = 7 + (flag ? b : c); }
    printf("integer consumer %ld\n", (long)integer);
    return 0;
}
