/* tcastasg.c - a narrowing cast applied to the value of an assignment
 *
 * The value of `x = e` or `x op= e` is the stored value in x's type; a cast
 * to an 8- or 16-bit type then narrows it again. Covered as an expression
 * value, an if condition and a switch control. dcc used to reject these
 * with DCC-E1002; the baseline is the host compiler's output.
 */
#include <stdint.h>
#include <stdio.h>

static uint16_t g16 = 7;
static int32_t g32 = 70000;

static int32_t as_value(int16_t p)
{
    int8_t c1 = 1;
    int16_t l1 = 1;
    int32_t l2 = 1;
    int32_t r;
    r = (uint16_t)(c1 = 257);
    r += (int16_t)(l1 = p);
    r += (int8_t)(l2 = g16);
    r += (uint8_t)(c1 += 3);
    r += (uint16_t)(l2 = g32);
    r += (int8_t)(l1 = p);
    return r + c1 + l1 + l2;
}

static int32_t as_condition(void)
{
    int32_t l2 = 1;
    int32_t r = 0;
    if ((int16_t)(l2 = g32) > 0)
        r += 1000;
    if ((int8_t)(l2 = 384) < 0)
        r += 100;
    return r + l2;
}

static int32_t as_switch_control(void)
{
    uint8_t l3 = 9;
    switch ((uint8_t)(l3 = 1)) {
    case 1:
        return 11 + l3;
    default:
        return 22;
    }
}

static int32_t in_comparison(void)
{
    int16_t l1 = 5;
    return ((uint16_t)1) >= ((uint16_t)(l1 = ((uint16_t)1)));
}

int main(void)
{
    printf("value %ld\n", (long)as_value(300));
    printf("condition %ld\n", (long)as_condition());
    printf("switch %ld\n", (long)as_switch_control());
    printf("comparison %ld\n", (long)in_comparison());
    return 0;
}
