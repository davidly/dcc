/* A byte store must use the current value of a loop-carried wide counter. */
#include <stdint.h>
#include <stdio.h>

static uint8_t bytes[2];

static void store_bytes(void)
{
    uint8_t *p = &bytes[1];
    uint32_t i;

    for (i = 1; i < 5; ++i) {
        p[0] = (uint8_t)(255UL & i);
    }
}

int main(void)
{
    store_bytes();
    printf("%u\n", (unsigned)bytes[1]);
    return 0;
}
