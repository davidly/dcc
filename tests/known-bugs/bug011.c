/* BUG-011: compound assignment to an element of a char, signed char or
 * unsigned char array that is a struct member is rejected with DCC-E1002,
 * for every operator and for constant and variable indexes. `s.b[i]++`,
 * `s.b[i] = s.b[i] ^ 1`, a plain byte array, and a 16-bit array member all
 * compile. */
#include <stdint.h>
#include <stdio.h>

struct B { uint8_t b[4]; int8_t c[4]; char name[4]; };
static struct B s = { { 4, 5, 6, 7 }, { 1, 2, 3, 4 }, "abc" };
static struct B sa[2];
static int16_t idx = 2;

static void direct(void)
{
    s.b[0] ^= 1;
    s.b[1] += 1;
    s.b[idx] |= 0x20;
    s.b[3] <<= 1;
    s.c[2] -= 5;
    s.name[1] &= 0x5f;
}

static void through_pointer(struct B *p)
{
    p->b[2] *= 3;
    p->c[idx] += 3;
    p->name[idx] |= 0x80;
}

static void element(void)
{
    sa[1].b[2] += 1;
    sa[1].c[idx - 1] -= 1;
}

int main(void)
{
    uint8_t i;
    sa[0] = s;
    sa[1] = s;
    direct();
    element();
    through_pointer(&sa[0]);
    for (i = 0; i < 4; i++)
        printf("%u %d %u | %u %d | %u %d %u\n", (unsigned)s.b[i], (int)s.c[i],
               (unsigned)(uint8_t)s.name[i], (unsigned)sa[1].b[i], (int)sa[1].c[i],
               (unsigned)sa[0].b[i], (int)sa[0].c[i], (unsigned)(uint8_t)sa[0].name[i]);
    return 0;
}
