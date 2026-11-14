/* Negative 32-bit constants assigned to initialized nested-block locals,
 * with signed/unsigned storage and low-word truncation. */
#include <stdint.h>
#include <stdio.h>

static uint16_t low;

static int32_t unsigned_local(void)
{
    {
        uint32_t b = 201;
        b = -3;
        low = (uint16_t)b;
    }
    return 1;
}

static int32_t signed_local(void)
{
    int32_t r = 0;
    {
        int32_t b = 201;
        b = -70000;
        r = b;
    }
    return r;
}

int main(void)
{
    printf("unsigned %ld %u\n", (long)unsigned_local(), (unsigned)low);
    printf("signed %ld\n", (long)signed_local());
    return 0;
}
