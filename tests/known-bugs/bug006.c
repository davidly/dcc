/* BUG-006: /= and %= on an unsigned bit-field store the wrong value.
 * `bf = bf % x` and += are correct, as is %= on a signed bit-field. */
#include <stdint.h>
#include <stdio.h>

struct S { uint16_t m1; uint16_t m2 : 11; int16_t m4 : 6; uint16_t m5 : 5; };
static struct S a = { 0x827f, 1148, -11, 18 };
static struct S b = { 0x827f, 1148, -11, 18 };
static struct S c = { 0x827f, 1148, -11, 18 };
static int16_t big = 21930;
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

int main(void)
{
    modulo_constant();
    modulo_variable(wide);
    divide();
    printf("%%= constant %u %u\n", (unsigned)a.m5, (unsigned)a.m2);
    printf("%%= variable %u %u\n", (unsigned)b.m5, (unsigned)b.m2);
    printf("/= %u %u\n", (unsigned)c.m5, (unsigned)c.m2);
    printf("neighbours %u %d %u %d\n", (unsigned)a.m1, (int)a.m4, (unsigned)c.m1, (int)c.m4);
    return 0;
}
