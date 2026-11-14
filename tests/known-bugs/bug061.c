/* BUG-061: constant sizeof division treats a 16-bit size as signed. */
#include <stdint.h>
#include <stdio.h>

static const uint8_t large[32768] = { 0 };
static const uint8_t small[32760] = { 0 };

int main(void)
{
    printf("large %lu\n", (unsigned long)(sizeof(large) / 8));
    printf("small %lu\n", (unsigned long)(sizeof(small) / 8));
    printf("large size %lu\n", (unsigned long)sizeof(large));
    return 0;
}
