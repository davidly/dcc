/* C99 for-init storage after sibling scopes: arrays, address-taken locals,
 * pointers, switch cases, loop bodies, and 8/16/32-bit counters must keep
 * independent homes. Block-declared counters and a scope with no earlier
 * declaration are controls. */
#include <stdint.h>
#include <stdio.h>

static int8_t bytes[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
static uint16_t n;
static int16_t sel;
static int16_t three = 3;

static uint16_t array_in_else(void)
{
    uint16_t r = 0;
    if (sel) {
        uint32_t a = 0;
        n += (uint16_t)a;
    } else {
        uint16_t x[2] = { 0x1234, 0x5678 };
        for (uint8_t i = 5; i != 0; --i)
            n++;
        r = x[0] ^ x[1];
    }
    return r;
}

static uint16_t array_in_else_word(void)
{
    uint16_t r = 0;
    if (sel) {
        uint32_t a = 0;
        n += (uint16_t)a;
    } else {
        uint16_t x[2] = { 0x1234, 0x5678 };
        for (uint16_t i = 300; i != 0; --i)
            n++;
        r = x[0] ^ x[1];
    }
    return r;
}

static uint16_t array_in_else_wide(void)
{
    uint16_t r = 0;
    if (sel) {
        uint32_t a = 0;
        n += (uint16_t)a;
    } else {
        uint16_t x[2] = { 0x1234, 0x5678 };
        for (uint32_t i = 300; i != 0; --i)
            n++;
        r = x[0] ^ x[1];
    }
    return r;
}

static uint16_t address_taken(void)
{
    uint16_t r = 0;
    if (sel) {
        uint32_t a = 0;
        n += (uint16_t)a;
    } else {
        uint16_t x = 0x1234;
        uint16_t *px = &x;
        for (uint8_t i = 5; i != 0; --i)
            n++;
        r = *px;
    }
    return r;
}

/* Bound the walk so a counter/pointer alias is reported rather than hanging. */
static uint16_t pointer_walk(void)
{
    uint16_t r = 0;
    if (sel) {
        uint32_t a = 0;
        n += (uint16_t)a;
    } else {
        int8_t *q = &bytes[0];
        int8_t *end = bytes + 8;
        for (; q != end && n < 200; ++q) {
            for (uint8_t i = 5; i != 0; --i)
                n++;
        }
        r = (uint16_t)(q - bytes);
    }
    return r;
}

static uint16_t switch_case(void)
{
    uint16_t r = 0;
    switch (three) {
    case 0:
        {
            uint32_t a = 0;
            n += (uint16_t)a;
        }
        break;
    case 3:
        {
            uint16_t x[2] = { 0x1234, 0x5678 };
            for (uint8_t i = 5; i != 0; --i)
                n++;
            r = x[0] ^ x[1];
        }
        break;
    }
    return r;
}

/* two sibling blocks inside a loop body, rather than arms of one statement */
static uint16_t sibling_blocks(void)
{
    uint16_t r = 0;
    int16_t k;
    for (k = 0; k < 1; k++) {
        {
            uint32_t a = 0;
            n += (uint16_t)a;
        }
        {
            uint16_t x[2] = { 0x1234, 0x5678 };
            for (uint8_t i = 5; i != 0; --i)
                n++;
            r = x[0] ^ x[1];
        }
    }
    return r;
}

/* correct: the counter is declared at the top of the block */
static uint16_t counter_in_block(void)
{
    uint16_t r = 0;
    if (sel) {
        uint32_t a = 0;
        n += (uint16_t)a;
    } else {
        uint16_t x[2] = { 0x1234, 0x5678 };
        uint8_t i;
        for (i = 5; i != 0; --i)
            n++;
        r = x[0] ^ x[1];
    }
    return r;
}

/* correct: the first arm declares nothing */
static uint16_t no_earlier_declaration(void)
{
    uint16_t r = 0;
    if (sel) {
        n += 1;
    } else {
        uint16_t x[2] = { 0x1234, 0x5678 };
        for (uint8_t i = 5; i != 0; --i)
            n++;
        r = x[0] ^ x[1];
    }
    return r;
}

static void show(const char *name, uint16_t r)
{
    printf("%s %04x %u\n", name, (unsigned)r, (unsigned)n);
    n = 0;
}

int main(void)
{
    show("array in else", array_in_else());
    show("word counter", array_in_else_word());
    show("wide counter", array_in_else_wide());
    show("address taken", address_taken());
    show("pointer walk", pointer_walk());
    show("switch case", switch_case());
    show("sibling blocks", sibling_blocks());
    show("counter in block", counter_in_block());
    show("no earlier declaration", no_earlier_declaration());
    return 0;
}
