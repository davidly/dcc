/* Byte-local logical SSA values widened into array/member stores, a long
 * initializer/postincrement, and signed long comparisons. Includes nested
 * and side-effecting short-circuit paths and a constant-byte control. */
#include <stdint.h>
#include <stdio.h>

struct S0 {
    int16_t m0;
    int32_t m1;
    int32_t m2;
    uint32_t m3;
    int16_t m4 : 15;
    uint32_t m5[4];
};
struct S1 {
    uint16_t m0 : 3;
    int16_t m1 : 2;
    int16_t m2;
    uint16_t m3;
};

static uint32_t a4[8] = { 0xa279b3d8, 0xffff7fff, 0xaa55, 0xffffff80, 0x80000001, 0x8000, 0x95b10ca, 0x8001 };

static void hex4(uint16_t v)
{
    int8_t k;
    for (k = 12; k >= 0; k -= 4)
        putchar("0123456789abcdef"[(v >> k) & 15]);
}

static void show(uint16_t id, uint32_t v)
{
    hex4(id);
    putchar(' ');
    hex4((uint16_t)(v >> 16));
    hex4((uint16_t)v);
    putchar('\n');
}

static uint32_t ck;

static void mix(uint32_t v)
{
    ck = ((ck << 5) | (ck >> 27)) ^ v;
    ck = ck + (uint32_t)0x9e3779b1;
}

static uint32_t state(void)
{
    uint8_t i, j;
    ck = 0;
    for (i = 0; i < 8; i++)
        mix((uint32_t)a4[i]);
    return ck;
}


static int16_t f0(void)
{
    signed char l2 = 1 && ((uint16_t)1);
    a4[3] = l2;
    return 1;
}

static int32_t result;
static struct S0 object;
static uint16_t calls;

static void false_initializer(void)
{
    uint8_t b = -191 && (uint32_t)0;
    int32_t c = b;
    result = c++;
}

static void or_member(void)
{
    uint8_t b = (uint16_t)1 || (uint32_t)1;
    object.m1 = b;
}

static int32_t minus_one(void)
{
    return (int32_t)-1;
}

static void long_compare(void)
{
    int8_t b = (uint16_t)1 && 29464;
    result = minus_one() >= b;
}

static void variable_and(uint16_t x, uint16_t y)
{
    int8_t b = x && y;
    result = b;
}

static void variable_or(uint16_t x, uint16_t y)
{
    uint8_t b = x || y;
    object.m1 = b;
}

static void nested(uint16_t x, uint16_t y)
{
    int8_t b = (x || y) && (y || x);
    result = b;
}

static void assignment(uint16_t x, uint16_t y)
{
    uint8_t b = 0;
    b = x && y;
    result = b;
}

static void short_circuit(void)
{
    int8_t b = (++calls || calls++);
    result = b;
}

static void constant_control(void)
{
    int8_t b = 1;
    result = b;
}

int main(void)
{
    show(1, (uint32_t)f0());
    show(16385, state());
    false_initializer();
    show(2, (uint32_t)result);
    or_member();
    show(3, (uint32_t)object.m1);
    long_compare();
    show(4, (uint32_t)result);
    variable_and(1, 1);
    show(5, (uint32_t)result);
    variable_and(1, 0);
    show(6, (uint32_t)result);
    variable_or(0, 1);
    show(7, (uint32_t)object.m1);
    variable_or(0, 0);
    show(8, (uint32_t)object.m1);
    nested(1, 0);
    show(9, (uint32_t)result);
    nested(0, 0);
    show(10, (uint32_t)result);
    assignment(1, 1);
    show(11, (uint32_t)result);
    assignment(0, 1);
    show(12, (uint32_t)result);
    short_circuit();
    show(13, (uint32_t)result);
    show(14, calls);
    constant_control();
    show(15, (uint32_t)result);
    return 0;
}
