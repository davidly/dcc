/* A narrow nested-block local passed to a wider parameter:
 * { unsigned short b = (-18603) | 1; k(b); } with k(int32_t). The stored
 * int's same-width cast to unsigned short was dropped as an identity, and
 * the argument's reload was then forwarded that int, so 46933 was passed
 * sign-extended as -18603. A cast stored to a block local now stays when a
 * reload of it is widened (#380). */
#include <stdio.h>
#include <stdint.h>

int fails;
int16_t gi = -18603;
uint16_t gu = 46933;
int32_t got;

static void check(long got, long want, int line)
{
    if (got != want) {
        printf("line %d: got %ld, expected %ld\n", line, got, want);
        ++fails;
    }
}

static void take(int32_t v)
{
    got = v;
}

static void take_unsigned(uint32_t v)
{
    got = (int32_t)v;
}

static void take_after_short(int16_t a, int32_t v)
{
    got = v + a;
}

static void unsigned_from_int(void)
{
    int x = gi;

    { unsigned short b = (-18603) | 1; take(b); }
    check(got, 46933L, __LINE__);
    { unsigned short b = x; take(b); }
    check(got, 46933L, __LINE__);
    { unsigned short b = x | 1; take(b); }
    check(got, 46933L, __LINE__);
    { unsigned short b = x * 1; take_unsigned(b); }
    check(got, 46933L, __LINE__);
    { unsigned short b = x; take_after_short(1, b); }
    check(got, 46934L, __LINE__);
}

static void signed_from_unsigned(void)
{
    unsigned u = gu;

    { short b = u; take(b); }
    check(got, -18603L, __LINE__);
    { short b = u | 1; take(b); }
    check(got, -18603L, __LINE__);
}

int main(void)
{
    unsigned_from_int();
    signed_from_unsigned();
    if (fails)
        printf("tblkarg failed %d\n", fails);
    else
        printf("tblkarg passed\n");
    return 0;
}
