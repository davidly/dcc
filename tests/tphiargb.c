/* Integer oracle seed 399950 passed a byte from a control-flow join as the
 * last variadic argument. The switch and loops put the function in the CFG
 * size range where the backend considers pushing the joined value early. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

static int16_t words[] = { 31, -2, 32767 };
static int16_t table[2][2] = { { 0 }, { 0, 98 } };
static volatile int scan_progress;

static uint32_t sum_args(int tag, ...)
{
    va_list args;
    uint32_t wide;
    int word;
    int byte;

    va_start(args, tag);
    wide = va_arg(args, uint32_t);
    word = va_arg(args, int);
    byte = va_arg(args, int);
    va_end(args);
    return wide + word + byte;
}

static uint32_t scan_last(int mode)
{
    uint8_t last = 1;
    int8_t pass = 0;
    int32_t step = 0;
    int ticks = 0;
    uint8_t i;

    switch (mode ? mode : 2) {
    case 3:
        for (i = 4; i != 2; i--)
            ticks++;
        break;
    case 127:
        for (i = 2; i < 7; i++)
            ticks--;
        break;
    case 1:
    case 100:
    default:
        break;
    }
    do {
        last = 1;
        while (step++ < ticks)
            scan_progress++;
    } while (++pass < 3);
    if (mode == 1 || mode == 2 || mode == 3) {
        int16_t *p = words;
        while (p != words + 3)
            last = *p++;
        (mode > 1) && (scan_progress = 1);
    } else {
        scan_progress = 0;
    }
    return sum_args(0, (uint32_t)1, table[1][1], last);
}

int main(void)
{
    unsigned long scanned = scan_last(3);
    unsigned long skipped = scan_last(127);

    printf("%lu %lu\n", scanned, skipped);
    return 0;
}
