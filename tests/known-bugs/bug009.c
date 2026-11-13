/* BUG-009: a do-while loop nested inside another loop never exits when its
 * condition is `d < n` with a 32-bit `d`: the comparison is always true.
 * This program hangs. The same loop on its own, a while loop, `d != n`, or a
 * 16-bit `d` all terminate. */
#include <stdint.h>
#include <stdio.h>

static uint16_t passes;
static int32_t limit = 3;

static int32_t signed_counter(void)
{
    int32_t i, d = 0, n = 0;
    for (i = 0; i < 4; i++) {
        n = limit;
        do {
            passes++;
            ++d;
        } while (d < n);
    }
    return d;
}

static int32_t unsigned_counter(void)
{
    uint32_t d = 0, n = 0;
    int16_t i;
    for (i = 6; i != 2; --i) {
        n = (uint32_t)limit;
        do {
            passes++;
            ++d;
        } while (d < n);
    }
    return (int32_t)d;
}

static int32_t global_bound(void)
{
    int32_t i, d = 0;
    for (i = 6; i != 2; --i) {
        do {
            passes++;
            ++d;
        } while (d < limit);
    }
    return d;
}

int main(void)
{
    printf("signed %ld %u\n", (long)signed_counter(), (unsigned)passes);
    printf("unsigned %ld %u\n", (long)unsigned_counter(), (unsigned)passes);
    printf("global bound %ld %u\n", (long)global_bound(), (unsigned)passes);
    return 0;
}
