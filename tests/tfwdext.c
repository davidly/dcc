/* A narrow value read back from a nested-block local is extended by the
 * local's own type, not by the type of what was stored there
 * (clang-oracle seeds 1016069 and 2002344). */
#include <stdint.h>
#include <stdio.h>

static int32_t g6;
static int16_t g1;
static int32_t x = 0x12345678L;
static int32_t r[8];

/* An int -1 stored to an unsigned short reads back as 0xffff. */
static void unsigned_local(int16_t x1)
{
    uint16_t l3 = -1;
    {
        uint32_t b7 = 1;
        g6 = (int32_t)(b7 + l3);
    }
    printf("ushort %lx", (unsigned long)(uint32_t)g6);
    l3 = x1 = (uint16_t)(((uint16_t)0) - 1);
    {
        uint32_t b7 = 1;
        g6 = (int32_t)(b7 + l3);
    }
    printf(" %lx %d\n", (unsigned long)(uint32_t)g6, x1);
}

/* The same through an assignment-expression initializer. */
static void unsigned_chain(int16_t x1)
{
    uint16_t l3 = x1 = ((uint16_t)(((uint16_t)0) - 1));
    {
        uint32_t b7 = 1;
        g6 = (int32_t)(b7 + l3);
    }
    printf("chain %lx\n", (unsigned long)(uint32_t)g6);
}

/* An unsigned product stored to a short reads back as a negative short. */
static void signed_local(uint16_t x1, int32_t x4)
{
    {
        int16_t b26 = ((int32_t)-1) * ((uint32_t)16);
        switch (b26 & x4) {
        case 2:
            break;
        case 0:
            g1 = 0;
            break;
        default:
            g1 = (int16_t)x1;
        }
    }
    printf("short %d\n", g1);
}

/* A negative narrow constant operand of a 32-bit operation sign-extends. */
static void wide_operands(void)
{
    int i;

    { int16_t b = (-1L) * 16UL; r[0] = b & x; r[1] = b | x; r[2] = b ^ x; r[3] = b + x; }
    { int16_t b = (-1L) * 16UL; r[4] = x & b; r[5] = x | b; r[6] = x ^ b; r[7] = x - b; }
    printf("wide");
    for (i = 0; i < 8; i++)
        printf(" %lx", (unsigned long)(uint32_t)r[i]);
    printf("\n");
}

int main(void)
{
    unsigned_local(-2);
    unsigned_chain(-2);
    signed_local(7, -2147483647L);
    wide_operands();
    return 0;
}
