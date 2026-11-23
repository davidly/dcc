/* A cached frame spill must still be stored when a union aliases it.
 * Integer oracle seed 171789; the unused XOR creates the tempting reload. */
#include <stdint.h>
#include <stdio.h>

union U { uint16_t word; int32_t wide; };
static int32_t aliased(int32_t unused)
{
    int16_t values[8] = {-19995, 16, -75, -129, 8, 8, 0, 1};
    int16_t *p = &values[0];
    union U u = {0xffff};
    (uint16_t)(*p ^ u.word);
    return u.wide;
}
static int32_t control(uint16_t x)
{
    union U u = {123};
    u.word = x;
    return u.wide;
}
int main(void)
{
    printf("%ld %ld %ld\n", (long)aliased(1),
           (long)control(257), (long)control(65535U));
    return 0;
}
