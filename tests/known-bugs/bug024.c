/* BUG-024: `if (x++)` inside a loop tests the incremented value instead of
 * the old one, for an 8-bit local that a later do-while loop also modifies.
 * The first pass, where x is 0, takes the branch. It is correct for a 16-bit
 * local, without the later do-while, or when the `if` is not in a loop. */
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
