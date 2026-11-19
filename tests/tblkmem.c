/* A compound division or remainder of a struct member whose right operand
 * assigns a nested-block local: s.m %= (b -= 1). Scoped type repair runs
 * before member loads are typed, assumed int for the member, and computed
 * a long member's remainder as int: 43605 % 255 stored -1, not 0. The
 * member's type now comes from its typed member address. */
#include <stdio.h>
#include <stdint.h>

int fails;

struct S { int16_t pad; int32_t m; uint32_t u; int16_t w; } s;

static void check(long got, long want, int line)
{
    if (got != want) {
        printf("line %d: got %ld, expected %ld\n", line, got, want);
        ++fails;
    }
}

static void rem_byte(void)
{
    s.m = 43605;
    { uint8_t b = 0; s.m %= (b -= 1); }
    check(s.m, 0, __LINE__);
}

static void div_byte(void)
{
    s.m = 700000;
    { uint8_t b = 0; s.m /= (b = 255); }
    check(s.m, 2745, __LINE__);
}

static void rem_unsigned_member(void)
{
    s.u = 700000;
    { uint8_t b = 0; s.u %= (b += 255); }
    check((long)s.u, 25, __LINE__);
}

static void rem_short_member(void)
{
    s.w = 30000;
    { uint16_t n = 0; s.w %= (n -= 1); }
    check(s.w, 30000, __LINE__);
}

static void div_short_member(void)
{
    s.w = 30000;
    { uint16_t n = 0; s.w /= (n -= 1); }
    check(s.w, 0, __LINE__);
}

int main(void)
{
    rem_byte();
    div_byte();
    rem_unsigned_member();
    rem_short_member();
    div_short_member();
    if (fails)
        printf("tblkmem failed %d\n", fails);
    else
        printf("tblkmem passed\n");
    return 0;
}
