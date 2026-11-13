/* tblkdiv.c - dividing a 32-bit member by a nested-block 16-bit local
 *
 * `m /= b` where `m` is a 32-bit struct member and `b` an unsigned 16-bit
 * local declared in a nested block. The division is a 32-bit one; dcc used
 * to carry it out in 16 bits and zero the member's high word. The baseline
 * is the host compiler's output.
 */
#include <stdint.h>
#include <stdio.h>

struct S { uint16_t m0; int32_t m3; };
static struct S s1 = { 1, 610804097 };
static struct S s2[4] = { { 1, 2 }, { 3, 4 }, { 5, 610804097 }, { 7, 8 } };
static struct S s3 = { 1, 610804097 };
static struct S s4 = { 1, -610804097 };

static void show(const char *name, int32_t v)
{
    printf("%s %ld\n", name, (long)v);
}

static void member(void)
{
    {
        uint16_t b = (uint16_t)1;
        s1.m3 /= b;
    }
}

static void member_of_element(void)
{
    {
        uint16_t b = (uint16_t)1;
        s2[2].m3 /= b;
    }
}

static void by_three(struct S *p)
{
    {
        uint16_t b = (uint16_t)3;
        p->m3 /= b;
    }
}

static void remainder_of(struct S *p)
{
    {
        uint16_t b = (uint16_t)1000;
        p->m3 %= b;
    }
}

int main(void)
{
    member();
    member_of_element();
    by_three(&s3);
    remainder_of(&s4);
    show("member / 1", s1.m3);
    show("element member / 1", s2[2].m3);
    show("pointer member / 3", s3.m3);
    show("pointer member % 1000", s4.m3);
    return 0;
}
