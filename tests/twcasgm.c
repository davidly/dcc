/* twcasgm.c - compound assignment of a 32-bit value to a narrower lvalue
 *
 * The right operand is unsigned long or long and the target is an 8- or
 * 16-bit struct member, array element or pointer target. The operation is
 * carried out in 32 bits and the result narrowed to the target. dcc used to
 * reject every one of these statements with DCC-E1002; the baseline is the
 * host compiler's output.
 */
#include <stdint.h>
#include <stdio.h>

struct S { uint8_t b; int16_t w; };
static struct S s1 = { 200, 300 };
static struct S s2 = { 200, 300 };
static struct S sa[2] = { { 1, 2 }, { 200, 300 } };
static uint8_t a8[4] = { 1, 200, 3, 4 };
static int16_t a16[4] = { 1, 300, 3, 4 };
static uint8_t m8[2][2] = { { 1, 2 }, { 200, 4 } };
static uint32_t r = 5;
static int32_t big = 70003;
static int16_t idx = 1;

static void member_through_pointer(struct S *p)
{
    p->b += r;
    p->w ^= r;
}

static void member(void)
{
    s2.b <<= r;
    s2.w /= r;
}

static void array_element(void)
{
    a8[1] += big;
    a16[idx] -= big;
    m8[idx][0] ^= r;
    sa[idx].w += big;
}

static void through_pointer(uint8_t *p, int16_t *q)
{
    *p |= r;
    q[idx] %= r;
}

int main(void)
{
    member_through_pointer(&s1);
    member();
    array_element();
    through_pointer(&a8[2], &a16[1]);
    printf("member via pointer %u %d\n", (unsigned)s1.b, (int)s1.w);
    printf("member %u %d\n", (unsigned)s2.b, (int)s2.w);
    printf("array %u %d %u %d\n", (unsigned)a8[1], (int)a16[1], (unsigned)m8[1][0], (int)sa[1].w);
    printf("pointer %u %d\n", (unsigned)a8[2], (int)a16[2]);
    return 0;
}
