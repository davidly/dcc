/* Unreachable constant float division by zero in controlling expressions,
 * with nonzero, integer-division and variable-denominator controls. */
#include <stdint.h>
#include <stdio.h>

static uint16_t constant_zero(void)
{
    if (0) {
        if (0.001f / 0.0f)
            return 2;
    }
    return 1;
}

static uint16_t control_nonzero(void)
{
    if (0) {
        if (0.001f / 1.0f)
            return 2;
    }
    return 1;
}

static uint16_t control_integer(void)
{
    if (0) {
        if (5 / 0)
            return 2;
    }
    return 1;
}

static uint16_t control_variable(void)
{
    if (0) {
        float d = 0.0f;
        if (0.001f / d)
            return 2;
    }
    return 1;
}

static uint16_t runtime_guard(uint16_t guard)
{
    if (guard) {
        if (2.71828f / 0.0f)
            return 2;
    }
    return 1;
}

int main(void)
{
    printf("float zero %u\n", constant_zero());
    printf("nonzero control %u\n", control_nonzero());
    printf("integer control %u\n", control_integer());
    printf("variable control %u\n", control_variable());
    printf("runtime guard %u\n", runtime_guard(0));
    return 0;
}
