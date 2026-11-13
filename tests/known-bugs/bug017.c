/* BUG-017: an unsigned short parameter and-assigned with itself and then
 * returned through a widening cast returns 0. `return x;` without the cast,
 * `x |= x`, `x &= 3`, and a local copy of the parameter are all correct. */
#include <stdint.h>
#include <stdio.h>

static int16_t and_self(unsigned short x)
{
    x &= x;
    return (uint32_t)x;
}

static int32_t and_self_long(unsigned short x)
{
    x &= x;
    return (int32_t)x;
}

int main(void)
{
    printf("and self %d %d\n", (int)and_self(1), (int)and_self(1234));
    printf("and self long %ld\n", (long)and_self_long(40000));
    return 0;
}
