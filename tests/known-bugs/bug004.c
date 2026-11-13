/* BUG-004: an unsigned 16-bit local initialised from && or ||, then stored
 * to a 32-bit lvalue, leaves garbage in the high word. Storing the logical
 * result directly, or a local initialised from anything else, is correct. */
#include <stdint.h>
#include <stdio.h>

struct S { uint16_t m0; uint32_t m1; };
static struct S s = { 255, 0x5dd1d183 };
static uint32_t u[2] = { 0x80000000, 0x12345678 };
static uint32_t g = 0xdeadbeef;
static uint32_t g2 = 0xdeadbeef;
static uint16_t one = 1;
static uint16_t zero = 0;

static void show(const char *name, uint32_t v)
{
    printf("%s %04x%04x\n", name, (unsigned)(v >> 16), (unsigned)(v & 0xffff));
}

static void or_to_member(void)
{
    uint16_t l = one || zero;
    s.m1 = l;
}

static void and_to_element(void)
{
    uint16_t l = one && one;
    u[0] = l;
}

static void or_to_global(void)
{
    uint16_t l = zero || zero;
    g = l;
}

static void constant_or(void)
{
    uint16_t l = ((uint16_t)1) || ((uint16_t)1);
    u[1] = l;
}

/* the same un-widened result, assigned to a 32-bit nested-block local */
static void block_local(uint16_t x)
{
    {
        int32_t b = 0;
        b = ((--x) || 1);
        g2 = (uint32_t)b;
    }
}

int main(void)
{
    or_to_member();
    and_to_element();
    or_to_global();
    constant_or();
    block_local(1);
    show("|| to member", s.m1);
    show("&& to element", u[0]);
    show("|| to global", g);
    show("constant ||", u[1]);
    show("block local", g2);
    return 0;
}
