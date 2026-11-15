/* High-bit constant sizeof results: types, arithmetic, bounds and iteration. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static const uint8_t large[32768] = { 0 };
static const uint8_t small[32760] = { 0 };
static const uint8_t top[65535] = { 0 };
struct pair { uint32_t a; uint32_t b; };
static const struct pair pairs[4404] = { { 0, 0 } };
typedef uint8_t bound[sizeof(large) / 8];
static const uint32_t global_size = sizeof(large);
static const uint16_t global_div = sizeof(large) / 8;
static const uint16_t global_shift = sizeof(large) >> 3;
static const uint8_t global_compare = sizeof(large) > 30000;

static void runtime(size_t n)
{
    printf("runtime %lu %lu %u\n", (unsigned long)(n / 8),
           (unsigned long)(n >> 3), (unsigned int)(n > 30000));
}

int main(void)
{
    uint16_t i;
    uint16_t count = 0;
    int negative = -1;
    uint8_t local_bound[sizeof(large) / 4096];
    printf("large %lu\n", (unsigned long)(sizeof(large) / 8));
    printf("small %lu\n", (unsigned long)(sizeof(small) / 8));
    printf("large size %lu\n", (unsigned long)sizeof(large));
    printf("type %lu %lu\n", (unsigned long)sizeof(uint8_t[32768]),
           (unsigned long)(sizeof(uint8_t[32768]) / 8));
    printf("shift %lu %lu\n", (unsigned long)(sizeof(large) >> 3),
           (unsigned long)(sizeof(uint8_t[32768]) >> 3));
    printf("compare %u %u\n", (unsigned int)(sizeof(large) > 30000),
           (unsigned int)(sizeof(uint8_t[32768]) > 30000));
    printf("negative %u %u\n", (unsigned int)(negative < sizeof(large)),
           (unsigned int)(sizeof(uint8_t[32768]) > -1));
    printf("type top %lu %lu %lu\n", (unsigned long)sizeof(uint8_t[65535]),
           (unsigned long)(sizeof(uint8_t[65535]) / 8),
           (unsigned long)(sizeof(uint8_t[65535]) >> 3));
    printf("top %lu %lu %lu\n", (unsigned long)sizeof(top),
           (unsigned long)(sizeof(top) / 8), (unsigned long)(sizeof(top) >> 3));
    printf("global %lu %u %u %u\n", (unsigned long)global_size,
           (unsigned int)global_div, (unsigned int)global_shift,
           (unsigned int)global_compare);
    printf("bounds %lu %lu\n", (unsigned long)sizeof(bound),
           (unsigned long)sizeof(local_bound));
    for (i = 0; i < sizeof(pairs) / sizeof(pairs[0]); ++i)
        ++count;
    printf("pairs %lu %u\n", (unsigned long)(sizeof(pairs) / sizeof(pairs[0])),
           (unsigned int)count);
    runtime(sizeof(large));
    return 0;
}
