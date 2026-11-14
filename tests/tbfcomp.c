/* Unsigned bit-field compound division, remainder and right shift: high
 * and low top bits, negative divisors, local/pointer fields, live results,
 * and full-width unsigned fields. Neighbouring members remain intact. */
#include <stdint.h>
#include <stdio.h>

struct S { uint16_t m1; uint16_t m2 : 11; int16_t m4 : 6; uint16_t m5 : 5; };
static struct S a = { 0x827f, 1148, -11, 18 };
static struct S b = { 0x827f, 1148, -11, 18 };
static struct S c = { 0x827f, 1148, -11, 18 };
static struct S d = { 0x827f, 1148, -11, 18 };
static struct S e = { 0x827f, 1000, -11, 9 };
static int16_t big = 21930;
static uint8_t three = 3;
static uint32_t wide[2] = { 255, 21930 };

static void modulo_constant(void)
{
    a.m5 %= 7;
    a.m2 %= 100;
}

static void modulo_variable(uint32_t *p)
{
    b.m5 %= big;
    b.m2 %= ((int16_t)p[1]);
}

static void divide(void)
{
    c.m5 /= big;
    c.m2 /= 7;
}

static void shift_right(void)
{
    d.m5 >>= 1;
    d.m2 >>= three;
}

/* the same operators, with the top bit of each field clear: correct */
static void top_bit_clear(void)
{
    e.m5 %= 7;
    e.m2 >>= 3;
}

struct W { uint16_t u : 16; };
static struct W full = { 0x8421 };
static uint16_t edge[5];

static void promoted_and_live(void)
{
    struct S local = { 0x827f, 1149, -11, 18 };
    struct S *p = &local;
    {
        int16_t negative = -7;
        p->m5 /= negative;
        p->m2 %= negative;
    }
    edge[0] = local.m5;
    edge[1] = local.m2;
    local.m5 = 18;
    edge[2] = (p->m5 >>= three);
    edge[3] = (full.u >>= three);
    edge[4] = local.m4 == -11 && local.m1 == 0x827f;
}

int main(void)
{
    modulo_constant();
    modulo_variable(wide);
    divide();
    shift_right();
    top_bit_clear();
    promoted_and_live();
    printf("%%= constant %u %u\n", (unsigned)a.m5, (unsigned)a.m2);
    printf("%%= variable %u %u\n", (unsigned)b.m5, (unsigned)b.m2);
    printf("/= %u %u\n", (unsigned)c.m5, (unsigned)c.m2);
    printf(">>= %u %u\n", (unsigned)d.m5, (unsigned)d.m2);
    printf("top bit clear %u %u\n", (unsigned)e.m5, (unsigned)e.m2);
    printf("neighbours %u %d %u %d\n", (unsigned)a.m1, (int)a.m4, (unsigned)c.m1, (int)c.m4);
    printf("promotion %u %u live %u %u neighbours %u\n", (unsigned)edge[0],
           (unsigned)edge[1], (unsigned)edge[2], (unsigned)edge[3],
           (unsigned)edge[4]);
    return 0;
}
