/* A 32-bit va_arg as an operand: h * 31u + va_arg(ap, uint32_t), and in
 * compound assignments. Its dereference counted as a long word read only for
 * float, so these were rejected as an unsupported while body. */
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>

static uint32_t sum(int n, ...)
{
    va_list ap;
    uint32_t h = 7;
    int32_t s = 0;
    float f = 1.0f;

    va_start(ap, n);
    while (n-- > 0) {
        h = h * 31u + va_arg(ap, uint32_t);
        h += va_arg(ap, uint32_t);
        h ^= va_arg(ap, uint32_t);
        s -= va_arg(ap, int32_t);
        f = f * va_arg(ap, float);
    }
    va_end(ap);
    return h + (uint32_t)s + (uint32_t)(int32_t)(f * 100.0f);
}

int main(void)
{
    uint32_t got = sum(2, 100000UL, 3UL, 0xdeadbeefUL, (int32_t)-70000, 2.5f,
                       7UL, 0x80000000UL, 1UL, (int32_t)12345, 3.0f);

    if (got != 1994120442UL)
        printf("tvalong failed: %lu\n", (unsigned long)got);
    else
        printf("tvalong passed\n");
    return 0;
}
