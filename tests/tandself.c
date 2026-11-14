/* Self-AND of a word parameter remains available for a widening cast and
 * narrowing return. Plain return, self-OR, a constant mask and a local copy
 * cover the neighbouring non-forwarded forms. */
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

static int16_t plain(unsigned short x)
{
    return x;
}

static int16_t or_self(unsigned short x)
{
    x |= x;
    return (uint32_t)x;
}

static int16_t and_constant(unsigned short x)
{
    x &= 3;
    return (uint32_t)x;
}

static int32_t local_copy(unsigned short x)
{
    unsigned short y = x;
    y &= y;
    return (int32_t)y;
}

int main(void)
{
    printf("and self %d %d\n", (int)and_self(1), (int)and_self(1234));
    printf("and self long %ld\n", (long)and_self_long(40000));
    printf("controls %d %d %d %ld\n", (int)plain(1234), (int)or_self(1234),
           (int)and_constant(1234), (long)local_copy(40000));
    return 0;
}
