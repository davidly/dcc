/* BUG-026: a 16-bit variable that is assigned a computed value and is then,
 * in the next statement, both operands of one binary operator: the second
 * operand is read from a stack slot the function never allocates, so
 * `d = a - b; return d * d;` returns garbage. Plain `int` behaves the same
 * as int16_t. It is correct when another statement comes between, when the
 * other operand is a different value, and for 8-bit variables. */
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

/* correct: the other operand is a different value */
static int16_t other_operand(int16_t a, int16_t b)
{
    int16_t d = a - b;
    return d * a;
}

/* correct: a statement comes between */
static int16_t separated(int16_t a, int16_t b)
{
    int16_t d = a - b;
    calls++;
    return d * d;
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
    return 0;
}
