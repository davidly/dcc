/* Nested-block pointer pointee types must reach arithmetic and comparison
 * consumers before deferred conversions are repaired. Covers long compound
 * multiplication, signed byte/word versus unsigned long comparisons, and
 * unsigned word shift/divide, with function-scope controls. Also preserves
 * the range-based promotion of a block byte beside a narrow bit-field. */
#include <stdint.h>
#include <stdio.h>

struct S { uint16_t m5 : 6; };
static struct S s = { 24 };
static uint16_t a2[2] = { 21930, 0x8001 };
static uint8_t g1 = 239;
static uint32_t a4[8] = { 1, 0xaa55, 7, 100, 21930, 1, 0xf941ce65, 257 };

static void multiply_through_pointer(void)
{
    {
        uint32_t *q = &a4[4];
        (*q) *= 11;
    }
}

static void multiply_through_pointer_outer(void)
{
    uint32_t *q = &a4[4];
    {
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

static int32_t compare_through_pointer_outer(void)
{
    signed char c = -1;
    int32_t r = 0;
    uint32_t *q = &a4[0];
    {
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

static int32_t compare_int_through_pointer_outer(void)
{
    int16_t c = -1;
    int32_t r = 0;
    uint32_t *q = &a4[3];
    {
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

static int32_t byte_local_outer(void)
{
    signed char l2 = -66;
    unsigned char b = 1;
    {
        if ((b & s.m5) > l2)
            return 1;
    }
    return 3;
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

static uint8_t shift_through_u16_pointer_outer(void)
{
    uint8_t r;
    uint16_t *q = &a2[1];
    {
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

static uint8_t divide_through_u16_pointer_outer(void)
{
    uint8_t r;
    uint16_t *q = &a2[1];
    {
        r = (*q) / (g1 & 15);
    }
    return r;
}

int main(void)
{
    multiply_through_pointer();
    printf("multiply block %04x%04x\n", (unsigned)(a4[4] >> 16), (unsigned)(a4[4] & 0xffff));
    a4[4] = 21930;
    multiply_through_pointer_outer();
    printf("multiply outer %04x%04x\n", (unsigned)(a4[4] >> 16), (unsigned)(a4[4] & 0xffff));
    printf("compare char block %ld\n", (long)compare_through_pointer());
    printf("compare char outer %ld\n", (long)compare_through_pointer_outer());
    printf("compare int block %ld\n", (long)compare_int_through_pointer());
    printf("compare int outer %ld\n", (long)compare_int_through_pointer_outer());
    printf("byte block %ld\n", (long)byte_local());
    printf("byte outer %ld\n", (long)byte_local_outer());
    printf("shift block %u\n", (unsigned)shift_through_u16_pointer());
    printf("shift outer %u\n", (unsigned)shift_through_u16_pointer_outer());
    printf("divide block %u\n", (unsigned)divide_through_u16_pointer());
    printf("divide outer %u\n", (unsigned)divide_through_u16_pointer_outer());
    return 0;
}
