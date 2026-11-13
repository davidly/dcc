/* BUG-018: compound assignment of a 32-bit value to a bit-field, or to an
 * element of a 16-bit array that is a struct member, is rejected with
 * DCC-E1002. The same statements compile with a 16-bit right operand, with a
 * plain member or a 32-bit array member as the target, or written as
 * `x = x op y`. */
#include <stdint.h>
#include <stdio.h>

struct S { uint16_t u : 9; int16_t s : 7; int16_t w[2]; uint16_t x[2]; };
static struct S g = { 444, -15, { 300, -300 }, { 300, 40000 } };
static struct S h = { 444, -15, { 300, -300 }, { 300, 40000 } };
static struct S ga[2];
static uint32_t r = 5;
static int32_t big = 70003;
static int16_t idx = 1;

static void bit_field(void)
{
    g.u |= r;
    g.u += big;
    g.s ^= ((int32_t)1);
    g.u <<= (big & 3);
    g.s >>= (r & 15);
}

static void bit_field_through_pointer(struct S *p)
{
    p->u -= r;
    p->u *= r;
    p->u /= r;
    p->u %= big;
}

static void array_member(struct S *p)
{
    g.w[1] += big;
    g.w[idx] ^= r;
    p->x[1] -= big;
    g.x[idx] <<= (r & 7);
    ga[idx].w[0] += big;
    ga[idx].u &= big;
}

int main(void)
{
    ga[1] = g;
    bit_field();
    bit_field_through_pointer(&h);
    array_member(&h);
    printf("bit-field %u %d\n", (unsigned)g.u, (int)g.s);
    printf("bit-field via pointer %u\n", (unsigned)h.u);
    printf("array member %d %d %u %u\n", (int)g.w[0], (int)g.w[1], (unsigned)h.x[1],
           (unsigned)g.x[1]);
    printf("element of struct array %d %u\n", (int)ga[1].w[0], (unsigned)ga[1].u);
    return 0;
}
