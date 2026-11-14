/* Full-width prefix/postfix increment and decrement of signed and unsigned
 * 32-bit array elements, with constant/variable indexes and pointer bases. */
#include <stdint.h>
#include <stdio.h>

static int32_t a[4] = { (-2147483647 - 1), 65535, 32767, 70000 };
static uint32_t u[4] = { 0x80000000, 65535, 32767, 70000 };
static int32_t d[4] = { -2147483647, 65535, 0, 65536 };
static int16_t idx = 1;

static void show(const char *name, uint32_t v)
{
    printf("%s %04x%04x\n", name, (unsigned)(v >> 16), (unsigned)(v & 0xffff));
}

static void increment(void)
{
    ++a[0];
    a[1]++;
    ++a[2];
    a[idx + 2]++;
}

static void increment_unsigned(void)
{
    ++u[0];
    u[idx]++;
    u[2]++;
    ++u[3];
}

static void decrement(int32_t *p)
{
    p[0]--;
    --p[1];
    p[2]--;
    --p[3];
}

int main(void)
{
    uint8_t i;
    increment();
    increment_unsigned();
    decrement(d);
    for (i = 0; i < 4; i++)
        show("signed ++", (uint32_t)a[i]);
    for (i = 0; i < 4; i++)
        show("unsigned ++", u[i]);
    for (i = 0; i < 4; i++)
        show("pointer --", (uint32_t)d[i]);
    return 0;
}
