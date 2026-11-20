/* A comparison with a for-init long index, typed as if the index were int:
 * for (int32_t i = 7; i >= 3; i--) r = (-32767 - 1) >= (i + (uint16_t)1).
 * Assuming int, i + (uint16_t)1 is unsigned, so lowering converted the
 * constant to unsigned and it widened to the long comparison as 32768.
 * Scoped type repair skipped functions whose only deferred locals were
 * for-init ones; it now runs for those too. */
#include <stdio.h>
#include <stdint.h>

int fails;
uint16_t one = 1;

static void check(long got, long want, int line)
{
    if (got != want) {
        printf("line %d: got %ld, expected %ld\n", line, got, want);
        ++fails;
    }
}

static int32_t min_int(void)
{
    int32_t r = 9;

    for (int32_t i = 7; i >= 3; i--)
        r = ((-32767 - 1) >= (i + ((uint16_t)1)));
    return r;
}

static int32_t count_below(void)
{
    int32_t r = 0;

    for (int32_t i = 7; i >= 3; i--)
        r += ((-5) >= (i + one));
    return r;
}

static int32_t count_above(void)
{
    int32_t r = 0;

    for (int32_t i = 7; i >= 3; i--)
        r += ((i + one) > -5);
    return r;
}

static uint32_t through_pointer(void)
{
    int32_t v[4] = { -32769, 31, 1954829153, 63 };
    int32_t *p = &v[2];

    for (int32_t i = 7; i >= 3; i--)
        p[0] = ((-32767 - 1) >= (i + ((uint16_t)1)));
    return *p;
}

int main(void)
{
    check(min_int(), 0, __LINE__);
    check(count_below(), 0, __LINE__);
    check(count_above(), 5, __LINE__);
    check(through_pointer(), 0, __LINE__);
    if (fails)
        printf("tforlong failed %d\n", fails);
    else
        printf("tforlong passed\n");
    return 0;
}
