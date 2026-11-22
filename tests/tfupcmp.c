/* Compare independent float member-array updates, preserving their values
 * and exact stored bits. Single-update and separate-value controls follow. */
#include <stdint.h>
#include <stdio.h>

struct S { float v[2]; };
static struct S s = {{4.0f, 1.5f}};
#define a s.v[0]
#define b s.v[1]

static void reset(void)
{
    a = 4.0f;
    b = 1.5f;
}

static uint32_t bits(float f)
{
    union { float f; uint32_t u; } v;
    v.f = f;
    return v.u;
}

static void show(uint16_t r)
{
    printf("%u %08lx %08lx\n", r,
           (unsigned long)bits(a), (unsigned long)bits(b));
}

static void scoped_updates(void)
{
    uint16_t r;
    reset(); r = (++a) > (++b); show(r);
    reset(); r = (a++) > (b++); show(r);
    /* Scoped repair sees the entire function, including earlier updates. */
    for (int16_t i = 0; i < 5; ++i) {
    }
}

int main(void)
{
    uint16_t r;
    float x, y;
    reset(); r = (++a) > (++b); show(r);
    reset(); r = (a++) > (b++); show(r);
    reset(); r = (--a) > (--b); show(r);
    reset(); r = (a--) > (b--); show(r);
    reset(); r = (++a) < (++b); show(r);
    reset(); r = (++a) == (++b); show(r);
    reset(); r = (++a) > b; show(r);
    reset(); r = a > (++b); show(r);
    reset(); x = ++a; y = ++b; r = x > y; show(r);
    reset(); r = (a = 5.0f) > (b = 2.5f); show(r);
    scoped_updates();
    return 0;
}
