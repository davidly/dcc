/* Narrow signed constants promoted by postfix comparison with a long.
 * Integer oracle seed 161489; thresholds retain their declared signedness. */
#include <stdint.h>
#include <stdio.h>

static int32_t value = -1;
static uint16_t scoped(void)
{
    {
        int16_t x = (int32_t)-3;
        if (value > x++)
            return 1;
    }
    return 0;
}
static void controls(void)
{
    int16_t x = -3;
    uint16_t u = 65533U;
    printf("%d %d %d %d\n", value > x, value >= x,
           value < x, value <= x);
    printf("%d %d\n", value > u, value < u);
    {
        int16_t y = (int32_t)-3;
        printf("%d ", value <= y++);
        printf("%d %d\n", value >= y++, value < (int16_t)-2);
    }
}
int main(void)
{
    printf("%u\n", (unsigned)scoped());
    controls();
    return 0;
}
