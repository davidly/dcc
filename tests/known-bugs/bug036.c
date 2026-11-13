/* BUG-036: a 32-bit local assigned a value inside a loop and passed directly
 * to a call loses the assignment: after the loop it still holds its old value.
 * Correct when the argument is an expression (`l + 1`), is another value, or
 * when the local is not passed to a call. */
#include <stdint.h>
#include <stdio.h>

static uint32_t take32(int32_t x) { return (uint32_t)x + 1; }
static uint32_t take16(int16_t x) { return (uint32_t)x + 1; }
static uint32_t sink;

static int32_t for_loop(void)
{
    int32_t l = 0;
    uint16_t i;
    for (i = 5; i != 0; i--) {
        l = 1;
        take32(l);
    }
    return l;
}

static int32_t while_loop(void)
{
    int32_t l = 0;
    uint16_t i = 5;
    while (i != 0) {
        i--;
        l = 1;
        sink = take32(l);
    }
    return l;
}

static int32_t counted_loop(void)
{
    int32_t l = 0;
    uint16_t i;
    for (i = 0; i < 5; i++) {
        l = 7;
        take32(l);
    }
    return l;
}

static int32_t used_in_test(void)
{
    int32_t l = 0;
    uint16_t i;
    for (i = 5; i != 0; i--) {
        l = 1;
        take32(l);
    }
    return l == 1 ? 100 : 200;
}

static int32_t expression_argument(void)
{
    int32_t l = 0;
    uint16_t i;
    for (i = 5; i != 0; i--) {
        l = 1;
        take32(l + 1);
    }
    return l;
}

static int32_t narrow_argument(void)
{
    int32_t l = 0;
    uint16_t i;
    for (i = 5; i != 0; i--) {
        l = 1;
        take16((int16_t)l);
    }
    return l;
}

int main(void)
{
    printf("for %ld\n", (long)for_loop());
    printf("while %ld\n", (long)while_loop());
    printf("counted %ld\n", (long)counted_loop());
    printf("test %ld\n", (long)used_in_test());
    printf("expression %ld\n", (long)expression_argument());
    printf("narrow %ld\n", (long)narrow_argument());
    return 0;
}
