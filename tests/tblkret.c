/* Return conversions for block, if-body and for-init locals: byte
 * truncation, signedness changes, wide extension, array elements and
 * updated values, with function-scope and word-return controls. */
#include <stdint.h>
#include <stdio.h>

static int16_t g300 = 300;

static uint8_t block(void)
{
    {
        int16_t b = 300;
        return b;
    }
}

static uint8_t in_if(int16_t n)
{
    if (n) {
        int16_t b = n;
        return b;
    }
    return 1;
}

static uint8_t for_counter(void)
{
    for (int16_t b = 300; b < 400; b++) {
        return b;
    }
    return 1;
}

static int8_t to_signed(void)
{
    {
        uint8_t b = 200;
        return b;
    }
}

static uint8_t to_unsigned(void)
{
    {
        int8_t b = -56;
        return b;
    }
}

static uint8_t from_long(void)
{
    {
        int32_t b = 70000;
        return b;
    }
}

static uint8_t element(void)
{
    {
        int16_t b[2] = { 300, 1 };
        return b[0];
    }
}

static uint8_t updated(int16_t n)
{
    {
        int16_t b = g300;
        b += n;
        return b;
    }
}

static uint8_t function_scope(void)
{
    int16_t b = 300;
    {
        return b;
    }
}

/* Widening to a 32-bit return type. */
static uint32_t widened(void)
{
    {
        int16_t b = 7;
        return b;
    }
}

static int32_t widened_signed(int16_t n)
{
    if (n) {
        int16_t b = n;
        return b;
    }
    return 1;
}

/* Word-return control. */
static int16_t to_16_bits(void)
{
    {
        int8_t b = -2;
        return b;
    }
}

int main(void)
{
    int16_t r;
    int32_t w;
    r = block();
    printf("block %d\n", r);
    r = in_if(300);
    printf("if %d\n", r);
    r = for_counter();
    printf("for %d\n", r);
    r = to_signed();
    printf("to signed %d\n", r);
    r = to_unsigned();
    printf("to unsigned %d\n", r);
    r = from_long();
    printf("from long %d\n", r);
    r = element();
    printf("element %d\n", r);
    r = updated(300);
    printf("updated %d\n", r);
    r = function_scope();
    printf("function scope %d\n", r);
    w = (int32_t)widened();
    printf("widened %ld\n", (long)w);
    w = widened_signed(-2);
    printf("widened signed %ld\n", (long)w);
    r = to_16_bits();
    printf("to 16 bits %d\n", r);
    return 0;
}
