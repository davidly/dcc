/* The value of an assignment to a signed narrow nested-block local, folded
 * to a negative constant and compared with a wider operand:
 * (b = (uint32_t)933859954) < x[3]. Lowering omitted the widening as an
 * assumed identity, so the short's bits 0x9272 widened as 37490, not
 * -28046, and the comparison failed. Repair now widens such a negative
 * constant by its own type. */
#include <stdio.h>
#include <stdint.h>

int fails;
int32_t a[4] = { 7, 0, 0, 5 };
int32_t five = 5;
uint8_t flags;

static void check(long got, long want, int line)
{
    if (got != want) {
        printf("line %d: got %ld, expected %ld\n", line, got, want);
        ++fails;
    }
}

static void through_pointer(int32_t *x)
{
    { short b = 1; if ((b = ((uint32_t)933859954)) < x[3]) *x = 1; }
}

static void negative_literal(void)
{
    { short b = 1; if ((b = -3) < five) flags |= 1; }
    { short b = 1; if ((b = -3) > five) flags |= 2; }
    { int8_t c = 1; if ((c = (uint32_t)300) < five) flags |= 4; }
    { int8_t c = 1; if ((c = 200) < five) flags |= 8; }
    { short b = 1; if ((b = ~2) < five) flags |= 16; }
}

int main(void)
{
    through_pointer(a);
    check(a[0], 1, __LINE__);
    negative_literal();
    check(flags, 1 | 8 | 16, __LINE__);
    if (fails)
        printf("tblkncmp failed %d\n", fails);
    else
        printf("tblkncmp passed\n");
    return 0;
}
