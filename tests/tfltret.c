/* Implicit long/float return conversions for signed and unsigned values,
 * parameters, globals, arithmetic, and call results. Explicit casts,
 * assignment conversions, and 16-bit return conversions are controls. */
#include <stdio.h>
#include <stdint.h>

static int32_t gl = 100000;

static float a(int32_t x) { return x; }
static float b(uint32_t x) { return x + 1; }
static float c(void) { return gl; }
static int32_t d(float x) { return x; }
static uint32_t e(float x) { return x * 2.0f; }
static int32_t ff(void) { return gl; }
static float g(void) { return ff(); }
/* Explicit-conversion and narrow-integer controls. */
static float h(int32_t x) { float r = x; return r; }
static float i(int32_t x) { return (float)x; }
static float j(int16_t x) { return x; }
static int16_t k(float x) { return x; }

int main(void)
{
    printf("%ld %ld %ld\n", (long)a(gl), (long)b(7), (long)c());
    printf("%ld %lu %ld\n", (long)d(2.5f), (unsigned long)e(3.0f), (long)g());
    printf("%ld %ld %d %d\n", (long)h(gl), (long)i(gl), (int)j(9), k(4.75f));
    return 0;
}
