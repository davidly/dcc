/* Signed byte compound right shifts use the promoted left operand's
 * signedness, independent of the count; cover direct and indirect lvalues. */
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

static int16_t shifted(int8_t x, uint16_t n)
{
    x >>= n;
    return x;
}

static int edge_counts(void)
{
    static int8_t values[6] = { -128, -34, -1, 0, 1, 127 };
    uint16_t n;
    int i;
    int8_t x = -128;
    for (n = 0; n < 16; ++n)
        for (i = 0; i < 6; ++i)
            if (shifted(values[i], n) != (int16_t)((int16_t)values[i] >> n))
                return 0;
    x >>= 15u;
    if (x != -1)
        return 0;
    return (int16_t)(x >>= 9u) == -1;
}

int main(void)
{
    printf("array element %d\n", array_element());
    printf("global %d\n", global());
    printf("local %d\n", local());
    printf("pointer %d\n", through_pointer());
    printf("int count %d\n", int_count());
    printf("16-bit object %d\n", wider());
    printf("edge counts %d\n", edge_counts());
    return 0;
}
