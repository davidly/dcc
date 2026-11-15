/* sizeof is unsigned size_t even when the result is small or runtime-sized (BUG-062). */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static const uint8_t small[8] = { 0 };
static const uint8_t large[32768] = { 0 };
static const uint8_t init_compare = sizeof(uint8_t[8]) > -1;

static void vla_compare(uint16_t n)
{
    uint8_t x[n];
    int i = -1;
    printf("vla %u %u\n", (unsigned int)(i < sizeof(x)),
           (unsigned int)(sizeof(x) > -1));
}

int main(void)
{
    int i = -1;
    uint16_t count = 0;
    printf("object %u %u\n", (unsigned int)(i < sizeof(small)),
           (unsigned int)(sizeof(small) > -1));
    printf("type %u %u\n", (unsigned int)(i < sizeof(uint8_t[8])),
           (unsigned int)(sizeof(uint8_t[8]) > -1));
    for (i = -1; i < sizeof(small); ++i)
        ++count;
    printf("loop %u\n", (unsigned int)count);
    printf("unsigned %u %u\n", (unsigned int)(i < (unsigned int)sizeof(small)),
           (unsigned int)((unsigned int)sizeof(small) > -1));
    printf("large %u %u\n", (unsigned int)(-1 < sizeof(large)),
           (unsigned int)(sizeof(large) > -1));
    printf("initializer %u\n", (unsigned int)init_compare);
    vla_compare(8);
    return 0;
}
