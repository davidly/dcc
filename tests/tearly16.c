/* Word PHI assignments must reach their named home before fused updates
 * followed by an early return. Cover signedness, nested assignments, byte
 * carry, decrement, and the no-update/no-early-return neighboring shapes. */
#include <stdint.h>
#include <stdio.h>

static int16_t arr[2] = { 0, 5 };
volatile int16_t z = 1;
unsigned char sink;

static int else_arm(void)
{
    unsigned short l = 0;
    if (z == 0) {
    } else {
        l = 1;
    }
    l -= 129;
    if (!arr[1]) {
        return 1;
    }
    sink = l;
    return sink;
}

static int add_constant(void)
{
    unsigned short l = 0;
    if (z == 0) {
    } else {
        l = 1;
    }
    l += 2;
    if (!arr[1]) {
        return 1;
    }
    sink = l;
    return sink;
}

static int signed_local(void)
{
    int16_t l = 0;
    if (z == 0) {
    } else {
        l = 1;
    }
    l -= 129;
    if (!arr[1]) {
        return 1;
    }
    sink = l;
    return sink;
}

/* correct: the local is not updated */
static int no_update(void)
{
    unsigned short l = 0;
    if (z == 0) {
    } else {
        l = 1;
    }
    if (!arr[1]) {
        return 1;
    }
    sink = l;
    return sink;
}

static int nested_carry(void)
{
    unsigned short l = 0;
    if (z == 0) {
    } else {
        if (l = 255) {
        }
    }
    ++l;
    if (!arr[1]) {
        return 1;
    }
    sink = l;
    return sink;
}

static int decrement(void)
{
    unsigned short l = 0;
    if (z == 0) {
    } else {
        l = 1;
    }
    --l;
    if (!arr[1]) {
        return 1;
    }
    sink = l;
    return sink;
}

static int no_early_return(void)
{
    unsigned short l = 0;
    if (z == 0) {
    } else {
        l = 1;
    }
    l += 2;
    sink = l;
    return sink;
}

int main(void)
{
    printf("else arm %d\n", else_arm());
    printf("add constant %d\n", add_constant());
    printf("signed local %d\n", signed_local());
    printf("no update %d\n", no_update());
    printf("nested carry %d\n", nested_carry());
    printf("decrement %d\n", decrement());
    printf("no early return %d\n", no_early_return());
    return 0;
}
