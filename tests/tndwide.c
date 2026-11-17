/* Nested do-while loops with signed and unsigned 32-bit counters, 16- and
 * 32-bit outer counters, and local/global bounds. Each inner loop tests the
 * counter after its in-place increment and terminates at the bound. */
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
    int32_t result;
    result = signed_counter();
    printf("signed %ld %u\n", (long)result, (unsigned)passes);
    result = unsigned_counter();
    printf("unsigned %ld %u\n", (long)result, (unsigned)passes);
    result = global_bound();
    printf("global bound %ld %u\n", (long)result, (unsigned)passes);
    return 0;
}
