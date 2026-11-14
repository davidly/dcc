/* Reused function-scoped labels must not hide a later MIR emission error.
 * Initializing a local aggregate from va_arg is valid C but is still declined
 * by the generated emitters, including full debug. The fixed-width last
 * named parameter is compatible with its default argument promotion.
 * DCC-EXPECT-ERROR: MIR emission is required
 */
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

struct S { int32_t a; float b; int16_t c[3]; };
static uint16_t first(void) { L1:; return 1; }
static uint16_t second(void) { { L1:; } return 2; }

static int16_t third(int32_t n, ...)
{
    va_list ap;
    va_start(ap, n);
    struct S value = va_arg(ap, struct S);
    va_end(ap);
    return value.a;
}

int main(void)
{
    struct S value = { 7, 2.0f, { 1, 2, 3 } };
    printf("labels %u %u\n", (unsigned)first(), (unsigned)second());
    printf("third %d\n", (int)third((int32_t)1, value));
    return 0;
}
