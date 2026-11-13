/* BUG-012: unary - or ~ applied to ! of a constant is rejected with
 * DCC-E1002. The same operators applied to ! of a variable compile. */
#include <stdint.h>
#include <stdio.h>

static int32_t negated(void)
{
    int32_t r;
    r = -(!(-128));
    r += -(!0);
    return r;
}

static int32_t complemented(void)
{
    int32_t r;
    r = ~(!15);
    r += ~(!0);
    return r;
}

int main(void)
{
    printf("negated %ld\n", (long)negated());
    printf("complemented %ld\n", (long)complemented());
    return 0;
}
