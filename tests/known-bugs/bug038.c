/* BUG-038: a 16-bit local assigned in one arm of an `if … else` and then
 * updated loses the assignment when an `if` that may return early follows
 * and the local is used after it: the update is applied to the old value.
 * The same loss as BUG-027 without the outer `if` and without a loop. It is
 * correct when the local is not updated. */
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

int main(void)
{
    printf("else arm %d\n", else_arm());
    printf("add constant %d\n", add_constant());
    printf("signed local %d\n", signed_local());
    printf("no update %d\n", no_update());
    return 0;
}
