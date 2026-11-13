/* BUG-010: narrowing the value of a 32-bit post-increment or post-decrement
 * is rejected with DCC-E1002, whether the narrowing is a cast, a return, or
 * an argument conversion. A pre-increment, a 16-bit variable, and the
 * unconverted value all compile. */
#include <stdint.h>
#include <stdio.h>

static int16_t take(int8_t a) { return a; }

static int32_t cast_of_post_increment(void)
{
    int32_t l2 = 70000;
    int32_t r;
    r = (int16_t)(l2++);
    return r + l2;
}

static int32_t cast_of_post_decrement(void)
{
    uint32_t u2 = 70000;
    int32_t r;
    r = (int32_t)(u2--);
    return r + (int32_t)u2;
}

static int32_t argument(void)
{
    uint32_t u2 = 300;
    int32_t r;
    r = take((int8_t)(u2++));
    return r + (int32_t)u2;
}

static uint8_t returned(void)
{
    int32_t l3 = 300;
    return l3++;
}

int main(void)
{
    printf("cast of post-increment %ld\n", (long)cast_of_post_increment());
    printf("cast of post-decrement %ld\n", (long)cast_of_post_decrement());
    printf("argument %ld\n", (long)argument());
    printf("returned %u\n", (unsigned)returned());
    return 0;
}
