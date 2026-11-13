/* BUG-007: a call through a function pointer whose argument is a local
 * declared in a nested block (or in a for-init) and needs converting to the
 * parameter type makes the MIR verifier report "incorrect argument ABI type".
 * Depending on the surrounding code the compile then fails ("MIR verification
 * failed before required emission"), or dcc exits 0 and the program crashes.
 * The direct call, an explicit cast, and a function-scope local all work. */
#include <stdint.h>
#include <stdio.h>

static uint16_t seen;
static int32_t wide = 3;
static int16_t narrow = 3;

static int16_t take_byte(unsigned char x) { seen += x; return 1; }
static int16_t (*take_byte_p)(unsigned char) = take_byte;
static int16_t take_long(int32_t x) { seen += (uint16_t)x; return 1; }
static int16_t (*take_long_p)(int32_t) = take_long;

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
    narrowing();
    printf("narrowing %u\n", (unsigned)seen);
    widening();
    printf("widening %u\n", (unsigned)seen);
    for_init();
    printf("for-init %u\n", (unsigned)seen);
    return 0;
}
