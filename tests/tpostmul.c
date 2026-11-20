/* A widened postincrement of a parameter as a multiply operand:
 * g = ((int32_t)(x++)) * m with short x and m. The 16x16->32 multiply
 * loaded x from its parameter home, which already held x + 1, and the
 * saved old value's slot was reused. A parameter's home is read directly
 * only while nothing stores to it. */
#include <stdio.h>
#include <stdint.h>

int fails;
uint32_t g;
int32_t r;
int16_t m = -100;

struct S { uint8_t c; int16_t w; };
struct S s[2] = { { 1, 5 }, { 2, -100 } };

static void check(long got, long want, int line)
{
    if (got != want) {
        printf("line %d: got %ld, expected %ld\n", line, got, want);
        ++fails;
    }
}

void post_inc_global(short x)
{
    g = ((int32_t)(x++)) * m;
}

void post_inc_member(short x)
{
    g = ((int32_t)(x++)) * s[1].w;
}

void post_dec(short x)
{
    r = ((int32_t)(x--)) * m;
}

void plain(short x)
{
    g = ((int32_t)x) * m;
}

int main(void)
{
    post_inc_global(0);
    check((long)g, 0, __LINE__);
    post_inc_global(3);
    check((long)(int32_t)g, -300, __LINE__);
    post_inc_member(0);
    check((long)g, 0, __LINE__);
    post_inc_member(3);
    check((long)(int32_t)g, -300, __LINE__);
    post_dec(3);
    check(r, -300, __LINE__);
    plain(3);
    check((long)(int32_t)g, -300, __LINE__);
    if (fails)
        printf("tpostmul failed %d\n", fails);
    else
        printf("tpostmul passed\n");
    return 0;
}
