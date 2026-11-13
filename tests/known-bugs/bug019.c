/* BUG-019: a declaration in a nested scope (a block-local, or a for-init
 * declaration) that is unreachable because a return, break or goto precedes
 * it in the same block stops the compile with "MIR emission is required".
 * The same dead code compiles at function scope, without the declaration,
 * or when the jump is conditional. */
#include <stdint.h>
#include <stdio.h>

static uint16_t low;
static int16_t sel = 1;
static int16_t zero = 0;

static int32_t after_return(void)
{
    if (sel) {
        return 1;
        {
            int16_t b = 1;
            low += (uint16_t)b;
        }
    }
    return 0;
}

static int32_t for_init_after_return(void)
{
    if (sel) {
        return 2;
        for (uint8_t k = 3; k < 5; ++k) {
            low++;
        }
    }
    return 0;
}

static int32_t after_break(void)
{
    int32_t r = 0;
    int16_t i;
    for (i = 0; i < 3; i++) {
        r++;
        break;
        {
            uint8_t b = 1;
            low += b;
        }
    }
    return r;
}

static int32_t after_goto(void)
{
    int32_t r = 0;
    if (sel) {
        goto out;
        {
            int16_t b = 1;
            low += (uint16_t)b;
        }
    }
out:
    r = 4;
    return r;
}

static int32_t in_switch(void)
{
    switch (sel) {
    case 1:
        return 5;
        {
            int16_t b = 1;
            low += (uint16_t)b;
        }
    }
    return 0;
}

static int32_t in_while(void)
{
    while (zero == 0) {
        return 6;
        {
            int16_t b = 1;
            low += (uint16_t)b;
        }
    }
    return 0;
}

int main(void)
{
    printf("after return %ld\n", (long)after_return());
    printf("for-init after return %ld\n", (long)for_init_after_return());
    printf("after break %ld\n", (long)after_break());
    printf("after goto %ld\n", (long)after_goto());
    printf("in switch %ld\n", (long)in_switch());
    printf("in while %ld\n", (long)in_while());
    printf("dead code ran %u times\n", (unsigned)low);
    return 0;
}
