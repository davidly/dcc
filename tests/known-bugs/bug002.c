/* BUG-002: an unsigned char local shifted left by an unsigned char variable
 * is not truncated to 8 bits when it is returned from a function returning
 * unsigned char. 200 << 5 is 6400, which is 0 as an unsigned char. */
#include <stdint.h>
#include <stdio.h>

static uint8_t a1[4] = { 200, 2, 3, 4 };
static uint8_t count = 5;

static uint8_t from_array(void)
{
    uint8_t x = a1[0];
    x <<= count;
    return x;
}

static uint8_t from_constant(void)
{
    uint8_t x = 200;
    x <<= count;
    return x;
}

static uint8_t after_add(void)
{
    uint8_t x = a1[0];
    x += count;
    x <<= count;
    return x;
}

static uint8_t local_count(void)
{
    uint8_t x = a1[0];
    uint8_t n = count;
    x <<= n;
    return x;
}

int main(void)
{
    printf("from array %u\n", (unsigned)from_array());
    printf("from constant %u\n", (unsigned)from_constant());
    printf("after add %u\n", (unsigned)after_add());
    printf("local count %u\n", (unsigned)local_count());
    return 0;
}
