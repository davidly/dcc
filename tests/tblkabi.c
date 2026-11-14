/* Indirect-call ABI conversions for nested-block and for-init locals.
 * Covers narrowing, widening, and word parameters receiving arithmetic
 * expressions whose block-local operands acquire their declared wide type. */
#include <stdint.h>
#include <stdio.h>

static uint16_t seen;
static int32_t wide = 3;
static int16_t narrow = 3;

static int16_t take_byte(unsigned char x) { seen += x; return 1; }
static int16_t (*take_byte_p)(unsigned char) = take_byte;
static int16_t take_long(int32_t x) { seen += (uint16_t)x; return 1; }
static int16_t (*take_long_p)(int32_t) = take_long;
static int16_t take_word(int16_t x) { return x; }
static int16_t (*take_word_p)(int16_t) = take_word;
static uint16_t take_uword(uint16_t x) { return x; }
static uint16_t (*take_uword_p)(uint16_t) = take_uword;

static int16_t expression_arguments(void)
{
    {
        int32_t v = (int32_t)0x30201;
        if ((*take_word_p)(v ^ 1) != 0x0200)
            return 0;
    }
    {
        uint32_t v = (uint32_t)0x30201;
        if ((*take_uword_p)(v + (uint16_t)0x8d22) != (uint16_t)0x8f23)
            return 0;
    }
    return 1;
}

static void narrowing(void)
{
    {
        int32_t v = wide;
        (*take_byte_p)(v);
    }
}

static void widening(void)
{
    {
        int16_t v = narrow;
        take_long_p(v);
    }
}

static void for_init(void)
{
    for (int32_t i = 3; i != 0; --i) {
        if ((*take_byte_p)(i))
            seen++;
    }
}

int main(void)
{
    if (!expression_arguments()) {
        printf("expression argument conversion failed\n");
        return 1;
    }
    narrowing();
    printf("narrowing %u\n", (unsigned)seen);
    widening();
    printf("widening %u\n", (unsigned)seen);
    for_init();
    printf("for-init %u\n", (unsigned)seen);
    return 0;
}
