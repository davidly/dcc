/* BUG-031: a 32-bit local declared in a nested block and initialised from a
 * function-scope 32-bit local is stored to a 16-bit array element, struct
 * member or pointer target with only its low byte loaded: the high byte
 * written is whatever was in the register. It is correct when the local is
 * declared at function scope, when it is initialised from a global, a
 * constant or an expression, and when the destination is a plain global. It
 * is wrong too when the local is first initialised with a constant and then
 * assigned from the other local. */
#include <stdint.h>
#include <stdio.h>

struct S { uint16_t m; };
static uint16_t a2[5] = { 129, 22830, 1, 63, 9 };
static struct S s = { 7 };
static uint16_t g16 = 5;

static void element(uint32_t x)
{
    uint32_t y = x;
    {
        uint32_t b = y;
        a2[1] = b;
    }
}

static void member(uint32_t x)
{
    uint32_t y = x;
    {
        uint32_t b = y;
        s.m = b;
    }
}

static void through_pointer(uint32_t x)
{
    uint32_t y = x;
    uint16_t *p = &a2[2];
    {
        int32_t b = y;
        *p = b;
    }
}

static void in_if(int16_t n, uint32_t x)
{
    uint32_t y = x;
    if (n) {
        uint32_t b = y;
        a2[3] = b;
    }
}

/* the same through an assignment: the local is initialised with a constant
 * and then assigned the other local */
static void assigned_after_init(int32_t x)
{
    int32_t y = x;
    {
        int32_t b = 0;
        b = y;
        a2[4] = b;
    }
}

/* correct: declared at function scope */
static void function_scope(uint32_t x)
{
    uint32_t y = x;
    uint32_t b = y;
    a2[0] = b;
}

/* correct: stored to a plain global */
static void to_global(uint32_t x)
{
    uint32_t y = x;
    {
        uint32_t b = y;
        g16 = b;
    }
}

int main(void)
{
    element(0x00030201);
    member(0x00030201);
    through_pointer(0x00030201);
    in_if(1, 0x00030201);
    assigned_after_init(0x00030201);
    function_scope(0x00030201);
    to_global(0x00030201);
    printf("element %04x\n", (unsigned)a2[1]);
    printf("member %04x\n", (unsigned)s.m);
    printf("pointer %04x\n", (unsigned)a2[2]);
    printf("in if %04x\n", (unsigned)a2[3]);
    printf("assigned after init %04x\n", (unsigned)a2[4]);
    printf("function scope %04x\n", (unsigned)a2[0]);
    printf("global %04x\n", (unsigned)g16);
    return 0;
}
