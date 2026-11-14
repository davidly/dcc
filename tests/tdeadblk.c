/* Unreachable declaring blocks and for initializers after return, break,
 * continue, goto and constant exits. Scalar/VLA-bound side effects must stay
 * dead; nested labels, for-body labels and declarations before later labels
 * remain reachable through goto. */
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

static int16_t after_continue(void)
{
    int16_t i, r = 0;
    for (i = 0; i < 3; ++i) {
        ++r;
        if ((0 + 7) >= -128)
            continue;
        { int16_t b = 1; low += (uint16_t)b; }
    }
    return r;
}

static int16_t scalar_effect(void)
{
    if (sel) {
        if ((int16_t)((int32_t)1000))
            return 7;
        { int16_t b = ++low; low += (uint16_t)b; }
    }
    return 0;
}

static int16_t vla_effect(void)
{
    if (sel) {
        return 8;
        { int16_t a[sel + low++]; a[0] = 1; low += (uint16_t)a[0]; }
    }
    return 0;
}

static int16_t label_reentry(void)
{
    if (sel) {
        goto entry;
        return 0;
        { int16_t b; entry: b = 9; return b; }
    }
    return 0;
}

static int16_t for_reentry(void)
{
    if (sel) {
        goto entry;
        return 0;
        for (int16_t k = 0; k < 2; ++k) {
        entry:
            k = 10;
            return k;
        }
    }
    return 0;
}

static int16_t decl_before_label(void)
{
    if (sel) {
        goto entry;
        int16_t b;
        { int16_t unused = 1; low += (uint16_t)unused; }
    entry:
        b = 11;
        return b;
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
    printf("after continue %d\n", (int)after_continue());
    printf("scalar effect %d\n", (int)scalar_effect());
    printf("VLA effect %d\n", (int)vla_effect());
    printf("label reentry %d\n", (int)label_reentry());
    printf("for reentry %d\n", (int)for_reentry());
    printf("declaration before label %d\n", (int)decl_before_label());
    printf("dead code ran %u times\n", (unsigned)low);
    return 0;
}
