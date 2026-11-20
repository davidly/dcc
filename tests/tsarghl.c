/* A two-byte struct passed by value ahead of a constant argument:
 * f(t[1], 0). The struct's address was left in HL for the constant to be
 * pushed through DE, but the struct copy reloads its address from BC,
 * which was never set, so the callee received garbage. A by-value struct
 * argument is no longer cached in HL. */
#include <stdio.h>
#include <stdint.h>

struct W { uint16_t w; };
struct B { uint16_t m0 : 2; uint16_t m1 : 4; int16_t m2 : 6; };

int fails;
struct W tw[4] = { { 1 }, { 3533 }, { 5 }, { 7 } };
struct B tb[4] = { { 2, 14, -29 }, { 1, 3, -9 }, { 2, 8, -27 }, { 2, 5, -25 } };

static void check(long got, long want, int line)
{
    if (got != want) {
        printf("line %d: got %ld, expected %ld\n", line, got, want);
        ++fails;
    }
}

int16_t copy_word(struct W x, signed char c)
{
    tw[2] = x;
    return c + 1;
}

int16_t copy_bits(struct B x, signed char c)
{
    tb[2] = x;
    return c + 1;
}

void copy_word_void(struct W x, char *s)
{
    tw[3] = x;
    if (s[0] != 'z')
        ++fails;
}

void pass_word(void)
{
    copy_word(tw[1], 0);
}

void pass_bits(void)
{
    copy_bits(tb[1], 0);
}

int main(void)
{
    pass_word();
    check(tw[2].w, 3533, __LINE__);
    pass_bits();
    check(tb[2].m0, 1, __LINE__);
    check(tb[2].m1, 3, __LINE__);
    check(tb[2].m2, -9, __LINE__);
    copy_word_void(tw[0], "z");
    check(tw[3].w, 1, __LINE__);
    if (fails)
        printf("tsarghl failed %d\n", fails);
    else
        printf("tsarghl passed\n");
    return 0;
}
