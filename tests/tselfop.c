/* Same-value binary operands after computation or integer-to-wide conversion.
 * Covers 16-/32-bit locals, a copied value, parameter reassignment, nested
 * float/long initializers, and mixed-width float expressions, with separated
 * statements and different operands as controls. */
#include <stdint.h>
#include <stdio.h>

static int16_t g5 = 5;
static int16_t calls;

static int16_t square(int16_t a, int16_t b)
{
    int16_t d = a - b;
    return d * d;
}

static int16_t twice(int16_t a, int16_t b)
{
    int16_t d;
    d = a - b;
    return d + d;
}

static int16_t minus_self(int16_t a, int16_t b)
{
    a = a ^ b;
    return a - a;
}

static int16_t equal_self(int16_t a, int16_t b)
{
    int16_t d = a + b;
    return d == d;
}

static int16_t and_self(int16_t a, int16_t b)
{
    int16_t d = a + b;
    return d & d;
}

static int16_t from_global(int16_t a)
{
    a = g5;
    return a + a;
}

static int16_t in_condition(int16_t a, int16_t b)
{
    int16_t d = a - b;
    if (d + d > 3)
        return 1;
    return 0;
}

static int32_t square_long(int16_t a, int16_t b)
{
    int32_t d = a - b;
    return d * d;
}

/* the second operand is a copy of the first */
static int16_t copied(int16_t a, int16_t b)
{
    int16_t d = a - b;
    int16_t e = d;
    return d * e;
}

/* Control: the other operand is a different value */
static int16_t other_operand(int16_t a, int16_t b)
{
    int16_t d = a - b;
    return d * a;
}

/* Control: a statement comes between */
static int16_t separated(int16_t a, int16_t b)
{
    int16_t d = a - b;
    calls++;
    return d * d;
}

static float fg = 2.0f;
static int8_t c8 = 3;
static float g;
static int32_t lg;

static void block_global(void)
{
    { float b = fg; g = b + b; }
}

static void block_add(void)
{
    { float b = c8; g = b + b; }
}

static void block_sub(void)
{
    { float b = c8; g = b - b; }
}

static void block_mul(void)
{
    { float b = c8; g = b * b; }
}

static void block_long(void)
{
    { int32_t b = c8; lg = b + b; }
}

static void block_assigned(void)
{
    { float b; b = c8; g = b + b; }
}

static float mixed_mul(void)
{
    float l = 3.14159f;
    float l1 = 1.0f; /* The dead local is part of the register-placement case. */
    l = (uint16_t)0xdcc7;
    return (l * l) + (int32_t)1;
}

static float mixed_add(void)
{
    float l = 3.14159f;
    float l1 = 1.0f;
    l = (uint16_t)0xdcc7;
    return (l + l) + (int32_t)1;
}

static float mixed_control(void)
{
    float l = 3.14159f;
    float l1 = 1.0f;
    l = (uint16_t)0xdcc7;
    return (l * l) + 1.0f;
}

static void show_bits(float f)
{
    union { float f; uint32_t u; } v;
    v.f = f;
    printf("%lx\n", (unsigned long)v.u);
}

int main(void)
{
    int16_t i;
    for (i = 1; i <= 3; i++) {
        int16_t a = i * 10;
        printf("%d: square %d twice %d minus %d equal %d and %d\n", (int)i,
               (int)square(a, i), (int)twice(a, i), (int)minus_self(a, i),
               (int)equal_self(a, i), (int)and_self(a, i));
        printf("%d: global %d condition %d long %ld copied %d\n", (int)i,
               (int)from_global(a), (int)in_condition(a, i),
               (long)square_long(a, i), (int)copied(a, i));
        printf("%d: other operand %d separated %d\n", (int)i,
               (int)other_operand(a, i), (int)separated(a, i));
    }
    block_global(); show_bits(g);
    block_add(); show_bits(g);
    block_sub(); show_bits(g);
    block_mul(); show_bits(g);
    block_long(); printf("%ld\n", (long)lg);
    block_assigned(); show_bits(g);
    show_bits(mixed_mul());
    show_bits(mixed_add());
    show_bits(mixed_control());
    return 0;
}
