/* sizeof applied to a nested-block local (BUG-066).
 *
 * Block-scope declarations are replayed into the symbol table after the
 * enclosing statement is lowered, so the operand of a sizeof inside the block
 * had no symbol when its size was taken and every scalar reported 2 bytes.
 * Arrays were already correct through the declared-object table; this covers
 * the scalar, for-init, shadowed and arithmetic shapes with fixed-width types
 * so the expected output is identical on the host. */
#include <stdio.h>
#include <stdint.h>

static int scalar_in_block(void)
{
    uint8_t v[2] = { 16, 40 };
    uint8_t *p = &v[1];
    {
        uint32_t wide = 1;
        *p = sizeof(wide);
    }
    return p[0];
}

static int shadowed(void)
{
    int outer_size;
    int inner_size;
    {
        int32_t x = 1;
        {
            uint8_t x = 2;
            inner_size = (int)sizeof x;
        }
        outer_size = (int)sizeof x;
    }
    return outer_size * 10 + inner_size;
}

static unsigned total_bytes(void)
{
    unsigned total = 0;
    {
        int32_t a = 1;
        float f = 2.0f;
        uint16_t h = 3;
        int8_t c = 4;
        int32_t arr[3];
        arr[0] = a;
        total += sizeof a + sizeof f + sizeof h + sizeof c + sizeof arr;
        total += (unsigned)(sizeof arr / sizeof arr[0]);
        total += (unsigned)(sizeof a * 100u);
    }
    return total;
}

static int for_init(void)
{
    int sum = 0;
    for (int32_t i = 0; i < 3; ++i)
        sum += (int)sizeof i;
    return sum;
}

static int conditional(int pick)
{
    int r;
    {
        int32_t wide = 0;
        uint8_t narrow = 0;
        r = pick ? (int)sizeof wide : (int)sizeof narrow;
    }
    return r;
}

int main(void)
{
    printf("scalar %d\n", scalar_in_block());
    printf("shadowed %d\n", shadowed());
    printf("total %u\n", total_bytes());
    printf("for %d\n", for_init());
    printf("cond %d %d\n", conditional(1), conditional(0));
    return 0;
}
