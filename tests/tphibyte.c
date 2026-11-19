/* Regression for a loop-carried byte value widened through a conditional. */
#include <stdint.h>
#include <stdio.h>

static uint16_t result;

static void show(uint16_t id, uint32_t value)
{
    int shift;
    for (shift = 12; shift >= 0; shift -= 4)
        putchar("0123456789abcdef"[(id >> shift) & 15]);
    putchar(' ');
    for (shift = 28; shift >= 0; shift -= 4)
        putchar("0123456789abcdef"[(value >> shift) & 15]);
    putchar('\n');
}

static void update(void)
{
    signed char value = 1;
    uint16_t i = 0;
    uint32_t limit = 3;

    while (i++ < limit) {
        result = (0 ? (uint32_t)0 : (value -= 0));
        value -= 1;
    }
}

static void update_wrapped_add(void)
{
    signed char value = 126;
    uint16_t i = 0;
    uint32_t limit = 2;

    while (i++ < limit)
        result = (1 ? (value += 1) : (uint32_t)0);
}

static void update_variable_subtract(int8_t step)
{
    signed char value = 5;
    uint16_t i = 0;
    uint32_t limit = 3;

    while (i++ < limit)
        result = (0 ? (uint32_t)0 : (value -= step));
}

static void update_variable_add(int8_t step)
{
    signed char value = 125;
    uint16_t i = 0;
    uint32_t limit = 2;

    while (i++ < limit)
        result = (1 ? (value += step) : (uint32_t)0);
}

int main(void)
{
    update();
    show(1, result);
    update_wrapped_add();
    show(2, result);
    update_variable_subtract(2);
    show(3, result);
    update_variable_add(2);
    show(4, result);
    return 0;
}
