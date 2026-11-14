/* Compound updates of signed/unsigned byte array members through struct
 * variables, pointers and struct arrays; used values narrow and evaluate
 * their index once, including wide and float right operands. */
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

static uint32_t wide = 70003;
static float factor;

static void used_values(void)
{
    uint8_t value;
    int16_t next = 0;
    s.b[0] = 250;
    value = (s.b[next++] += wide);
    printf("used byte %u %u %d\n", (unsigned)value, (unsigned)s.b[0], (int)next);
    factor = 2.5f;
    s.b[1] = 4;
    value = (s.b[1] += factor);
    printf("mixed byte %u %u\n", (unsigned)value, (unsigned)s.b[1]);
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
    used_values();
    return 0;
}
