/* BUG-037: in a `for` or `while` loop whose 32-bit counter is incremented,
 * a copy of the counter taken in the body (`l = i;`) is stored after the
 * increment, so it holds the next value. Correct for 16-bit counters, for
 * `do ... while`, and outside a loop. */
#include <stdint.h>
#include <stdio.h>

static int32_t for_loop(void)
{
    int32_t l = 0, i;
    for (i = 2; i < 3; i += 1)
        l = i;
    return l;
}

static int32_t for_loop_postinc(void)
{
    int32_t l = 0, i;
    for (i = 10; i < 13; i++)
        l = i;
    return l;
}

static int32_t while_loop(void)
{
    int32_t l = 0, i = 2;
    while (i < 3) {
        l = i;
        i += 1;
    }
    return l;
}

static int32_t narrow_counter(void)
{
    int16_t l = 0, i;
    for (i = 2; i < 3; i += 1)
        l = i;
    return l;
}

static int32_t do_loop(void)
{
    int32_t l = 0, i = 2;
    do {
        l = i;
        i += 1;
    } while (i < 3);
    return l;
}

static int32_t straight_line(void)
{
    int32_t l = 0, i = 2;
    l = i;
    i += 1;
    return l;
}

int main(void)
{
    printf("for %ld\n", (long)for_loop());
    printf("for postinc %ld\n", (long)for_loop_postinc());
    printf("while %ld\n", (long)while_loop());
    printf("narrow %ld\n", (long)narrow_counter());
    printf("do %ld\n", (long)do_loop());
    printf("straight %ld\n", (long)straight_line());
    return 0;
}
