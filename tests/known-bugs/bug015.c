/* BUG-015: locals declared in a nested block lose their declared type in
 * further shapes. Through a nested-block pointer to a 32-bit object, `*q *= k`
 * is computed in 16 bits and `c <= *q` is compared as signed; a nested-block
 * unsigned char is compared as unsigned int. A nested-block pointer to a
 * uint16_t with the top bit set is shifted and divided as signed. Each is
 * correct when the local is declared at function scope. Beside a nested-block
 * 32-bit local, `(int16_t)` of an unsigned 16-bit value is lost as an operand. */
#include <stdint.h>
#include <stdio.h>

struct S { uint16_t m5 : 6; };
static struct S s = { 24 };
static uint16_t a2[2] = { 21930, 0x8001 };
static uint8_t g1 = 239;
static uint16_t g16 = 0x8000;
static uint32_t a4[8] = { 1, 0xaa55, 7, 100, 21930, 1, 0xf941ce65, 257 };

static void multiply_through_pointer(void)
{
    {
        uint32_t *q = &a4[4];
        (*q) *= 11;
    }
}

static int32_t compare_through_pointer(void)
{
    signed char c = -1;
    int32_t r = 0;
    {
        uint32_t *q = &a4[0];
        if (c <= (*q))
            r = 7;
    }
    return r;
}

static int32_t compare_int_through_pointer(void)
{
    int16_t c = -1;
    int32_t r = 0;
    {
        uint32_t *q = &a4[3];
        if (c <= (*q))
            r = 7;
    }
    return r;
}

static int32_t byte_local(void)
{
    signed char l2 = -66;
    {
        unsigned char b = 1;
        if ((b & s.m5) > l2)
            return 1;
    }
    return 3;
}

static int32_t cast_compare(void)
{
    int32_t r = 0;
    {
        int32_t b = 0;
        if (((int16_t)g16) > b)
            r = 1;
    }
    return r;
}

static int32_t cast_shift_compare(void)
{
    int32_t r = 0;
    {
        int32_t b = 0;
        if (((int16_t)((uint16_t)(((uint16_t)1) << (g1 & 15)))) > b)
            r = 1;
    }
    return r;
}

static int32_t cast_sum(void)
{
    int32_t r;
    {
        int32_t b = 0;
        r = b + ((int16_t)g16);
    }
    return r;
}

/* correct: the local is declared at function scope */
static int32_t cast_compare_outer(void)
{
    int32_t r = 0;
    int32_t b = 0;
    if (((int16_t)g16) > b)
        r = 1;
    return r;
}

static uint8_t shift_through_u16_pointer(void)
{
    uint8_t r;
    {
        uint16_t *q = &a2[1];
        r = (*q) >> (g1 & 15);
    }
    return r;
}

static uint8_t divide_through_u16_pointer(void)
{
    uint8_t r;
    {
        uint16_t *q = &a2[1];
        r = (*q) / (g1 & 15);
    }
    return r;
}

/* correct: the pointer is declared at function scope */
static uint8_t shift_through_outer_pointer(void)
{
    uint16_t *q = &a2[1];
    return (*q) >> (g1 & 15);
}

int main(void)
{
    multiply_through_pointer();
    printf("multiply %04x%04x\n", (unsigned)(a4[4] >> 16), (unsigned)(a4[4] & 0xffff));
    printf("compare char %ld\n", (long)compare_through_pointer());
    printf("compare int %ld\n", (long)compare_int_through_pointer());
    printf("byte local %ld\n", (long)byte_local());
    printf("cast compare %ld\n", (long)cast_compare());
    printf("cast shift compare %ld\n", (long)cast_shift_compare());
    printf("cast sum %ld\n", (long)cast_sum());
    printf("cast compare outer %ld\n", (long)cast_compare_outer());
    printf("shift u16 %u\n", (unsigned)shift_through_u16_pointer());
    printf("divide u16 %u\n", (unsigned)divide_through_u16_pointer());
    printf("shift outer %u\n", (unsigned)shift_through_outer_pointer());
    return 0;
}
