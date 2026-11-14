/* Byte postfix conditions retain the old value across real local stores.
 * Covers signed/unsigned locals, direct and compared tests, for/while loops,
 * and later do-while updates that keep the named byte home materialized. */
#include <stdint.h>
#include <stdio.h>

static uint16_t hits;
static int16_t out;

static void signed_byte(void)
{
    int8_t l = 0;
    int8_t i;
    short d = 0;
    for (i = 0; i < 3; i++) {
        if (l++)
            hits++;
    }
    do {
        l--;
    } while ((++d) < 2);
    out = l;
}

static void unsigned_byte(void)
{
    uint8_t l = 0;
    int8_t i;
    short d = 0;
    for (i = 3; i < 4; i += 1) {
        if (l++)
            hits++;
    }
    do {
        l--;
    } while ((++d) < 0);
    out = l;
}

static void compared(void)
{
    int8_t l = 0;
    int8_t i;
    short d = 0;
    for (i = 3; i < 4; i += 1) {
        if (l++ != 0)
            hits++;
    }
    do {
        l++;
    } while ((++d) < 0);
    out = l;
}

static void while_loop(void)
{
    int8_t l = 0;
    int8_t i;
    short d = 0;
    i = 3;
    while (i < 4) {
        if (l++)
            hits++;
        i++;
    }
    do {
        l--;
    } while ((++d) < 0);
    out = l;
}

int main(void)
{
    signed_byte();
    printf("signed char %u %d\n", (unsigned)hits, (int)out);
    hits = 0;
    unsigned_byte();
    printf("unsigned char %u %d\n", (unsigned)hits, (int)out);
    hits = 0;
    compared();
    printf("compared %u %d\n", (unsigned)hits, (int)out);
    hits = 0;
    while_loop();
    printf("while loop %u %d\n", (unsigned)hits, (int)out);
    return 0;
}
