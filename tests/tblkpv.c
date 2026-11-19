/* The value of a compound assignment through a pointer declared in a
 * nested block, when that value is used: v = (p[1] -= 1). Lowering had no
 * pointee type at all, so it emitted no narrowing to it, and repair only
 * retargets a narrowing that exists; the uint8_t result 255 was used as
 * the int -1. A used value now gets a narrowing that follows the store. */
#include <stdio.h>
#include <stdint.h>

int fails;
uint8_t bytes[2];
int8_t sbytes[2];
uint16_t words[2];
int32_t v;

static void check(long got, long want, int line)
{
    if (got != want) {
        printf("line %d: got %ld, expected %ld\n", line, got, want);
        ++fails;
    }
}

int main(void)
{
    { uint8_t *p = bytes; p[1] = 0; v = (p[1] -= 1); }
    check(v, 255, __LINE__);
    check(bytes[1], 255, __LINE__);
    { uint8_t *p = bytes; p[1] = 0; v = 7; v += (p[1] -= 1); }
    check(v, 262, __LINE__);
    { uint8_t *p = bytes; *p = 250; v = (*p += 10); }
    check(v, 4, __LINE__);
    { int8_t *q = sbytes; q[0] = 127; v = (q[0] += 1); }
    check(v, -128, __LINE__);
    { uint16_t *w = words; w[1] = 0; v = (w[1] -= 1); }
    check(v, 65535, __LINE__);
    { uint8_t *p = bytes; p[1] = 3; p[1] -= 1; }
    check(bytes[1], 2, __LINE__);
    if (fails)
        printf("tblkpv failed %d\n", fails);
    else
        printf("tblkpv passed\n");
    return 0;
}
