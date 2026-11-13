/* BUG-015: locals declared in a nested block lose their declared type in
 * further shapes. Through a nested-block pointer to a 32-bit object, `*q *= k`
 * is computed in 16 bits and `c <= *q` is compared as signed; a nested-block
 * unsigned char is compared as unsigned int. Each is correct when the local
 * is declared at function scope. */
#include <stdint.h>
#include <stdio.h>

struct S { uint16_t m5 : 6; };
static struct S s = { 24 };
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

int main(void)
{
    multiply_through_pointer();
    printf("multiply %04x%04x\n", (unsigned)(a4[4] >> 16), (unsigned)(a4[4] & 0xffff));
    printf("compare char %ld\n", (long)compare_through_pointer());
    printf("compare int %ld\n", (long)compare_int_through_pointer());
    printf("byte local %ld\n", (long)byte_local());
    return 0;
}
