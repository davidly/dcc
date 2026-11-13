/* BUG-023: a 16-bit local that was assigned a value of a different type and
 * is then updated loses the assigned value when a branch or loop follows:
 * the update is applied to 0 or to a stale value. `if (l++ > K) l = v;`
 * leaves 1 (or 65535) when the branch is not taken, and `l++` before a loop
 * that updates l again conditionally does not count from the assigned
 * value. It is correct when the local was assigned a value of its own type. */
#include <stdint.h>
#include <stdio.h>

static int16_t gs = 300;
static uint16_t gu = 300;
static uint16_t big = 0xbeef;
static int16_t zero = 0;
static int32_t wide;
static uint16_t out;

static uint16_t from_parameter(int16_t n)
{
    uint16_t l;
    l = n;
    if (l++ > 400)
        l = 0;
    return l;
}

static uint16_t from_global(void)
{
    uint16_t l;
    l = gs;
    if (l++ >= 400)
        l = 77;
    return l;
}

static uint16_t signed_from_unsigned(void)
{
    int16_t l;
    l = gu;
    if (l++ > 400)
        l = 0;
    return l;
}

static uint16_t decrement(int16_t n)
{
    uint16_t l;
    l = n;
    if (l-- > 400)
        l = 0;
    return l;
}

static uint16_t equality(int16_t n)
{
    uint16_t l;
    l = n;
    if (l++ == 400)
        l = 77;
    return l;
}

static uint16_t stored(int16_t n)
{
    uint16_t l;
    l = n;
    if (l++ < 200)
        l = 77;
    out = l;
    return 1;
}

static int32_t updated_in_loop(void)
{
    short l = 0;
    uint8_t i;
    l = big;                /* a uint16_t: 0xbeef becomes -16657 */
    l++;
    for (i = 5; i > 0; i--) {
        if (zero)
            continue;
        l++;
    }
    return l;
}

static int32_t decremented_in_loop(void)
{
    short l = 0;
    uint8_t i;
    l = big;
    l -= 1;
    for (i = 5; i > 0; i--) {
        if (zero)
            continue;
        wide = (--l);
    }
    return wide;
}

int main(void)
{
    printf("from parameter %u\n", (unsigned)from_parameter(300));
    printf("from global %u\n", (unsigned)from_global());
    printf("signed from unsigned %u\n", (unsigned)signed_from_unsigned());
    printf("decrement %u\n", (unsigned)decrement(300));
    printf("equality %u\n", (unsigned)equality(300));
    printf("stored %u %u\n", (unsigned)stored(300), (unsigned)out);
    printf("updated in loop %ld\n", (long)updated_in_loop());
    printf("decremented in loop %ld\n", (long)decremented_in_loop());
    return 0;
}
