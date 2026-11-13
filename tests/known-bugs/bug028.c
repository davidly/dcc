/* BUG-028: `>>=` on a signed 8-bit object with a count of type unsigned int
 * shifts logically instead of arithmetically. The 8-bit value is promoted to
 * a 16-bit int, so the difference shows for counts 9 to 15, where a negative
 * value must become -1. It is correct with an int, unsigned char or unsigned
 * long count, for `x = x >> n`, and for a 16-bit object. */
#include <stdint.h>
#include <stdio.h>

static int8_t a6[8] = { 100, 16, -1, 1, 17, 21, 40, -34 };
static int8_t b6[8] = { 100, 16, -1, 1, 17, 21, 40, -34 };
static int8_t g8 = -34;
static int8_t k8 = -34;
static int16_t g16 = -34;
static uint16_t cu11 = 11;
static int16_t ci11 = 11;

static int16_t array_element(void)
{
    a6[7] >>= 11u;
    return a6[7];
}

static int16_t global(void)
{
    g8 >>= cu11;
    return g8;
}

static int16_t local(void)
{
    int8_t l = -34;
    l >>= (uint16_t)11;
    return l;
}

static int16_t through_pointer(void)
{
    int8_t *p = &b6[7];
    *p >>= (uint16_t)11;
    return b6[7];
}

/* correct: an int count */
static int16_t int_count(void)
{
    k8 >>= ci11;
    return k8;
}

/* correct: a 16-bit object */
static int16_t wider(void)
{
    g16 >>= cu11;
    return g16;
}

int main(void)
{
    printf("array element %d\n", array_element());
    printf("global %d\n", global());
    printf("local %d\n", local());
    printf("pointer %d\n", through_pointer());
    printf("int count %d\n", int_count());
    printf("16-bit object %d\n", wider());
    return 0;
}
