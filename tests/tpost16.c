/* Preserve the old value of 16-bit local post-increment/decrement after
 * assignment from a local array element, including assignment conversion,
 * pointer access and an unrelated narrowing byte-local initializer. */
#include <stdint.h>
#include <stdio.h>

static uint8_t a0[8] = { 16, 76, 156, 127, 255, 188, 0, 57 };

static uint16_t returned_increment(void)
{
    signed char l1 = (int8_t)(1 - a0[1]);
    int16_t l3 = 1;
    uint16_t v6[2] = { 0xffde, 9029 };
    l3 = v6[1];
    return l3++;
}

static uint16_t returned_decrement(void)
{
    signed char l1 = (int8_t)(1 - a0[1]);
    int16_t l3 = 1;
    uint16_t v6[2] = { 0xffde, 9029 };
    l3 = v6[1];
    return l3--;
}

static uint16_t assigned_increment(void)
{
    signed char l1 = (int8_t)(((int32_t)1) - a0[1]);
    int16_t l3 = 1;
    uint16_t v6[8] = { 0xffde, 0xaa55, 9029, 0xff85, 129, 0xfffe, 1, 127 };
    uint16_t r;
    l3 = v6[2];
    r = l3++;
    return r;
}

static uint16_t through_pointer(void)
{
    signed char l1 = (int8_t)(((int32_t)1) - a0[1]);
    int16_t l3 = 1;
    uint16_t v6[8] = { 0xffde, 0xaa55, 9029, 0xff85, 129, 0xfffe, 1, 127 };
    uint16_t *p7 = &v6[2];
    l3 = (*p7);
    return l3++;
}

int main(void)
{
    printf("returned x++ %u\n", (unsigned)returned_increment());
    printf("returned x-- %u\n", (unsigned)returned_decrement());
    printf("assigned x++ %u\n", (unsigned)assigned_increment());
    printf("through pointer %u\n", (unsigned)through_pointer());
    return 0;
}
