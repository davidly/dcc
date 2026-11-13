/* BUG-035: with dccpeep, `x < low || x > high` style tests of a 16-bit local
 * can compare the local with itself. dccpeep's preserve_ix_word_signed_range
 * pass inserts a `push hl` after the first `ld l,(ix-N)` / `ld h,(ix-M)` of
 * the local, on the assumption that the word is the left operand of the
 * `sbc hl,de`. Here the local is the right operand and the other operand
 * (a struct parameter's member) is already on the stack, so the inserted push
 * makes `pop de` and `pop hl` both yield the local. The program is correct
 * without dccpeep (dcc-peep=false). */
#include <stdint.h>
#include <stdio.h>

struct S1 {
    int16_t m0;
    uint16_t m1;
    uint8_t m2;
};

static struct S1 s1 = { 0, 292, 255 };
static int32_t r_loop, r_counter8, r_switch_var, r_no_switch, r_no_tail, r_plain;

/* a 16-bit local assigned a narrowed constant, tested twice in a loop that
 * holds a switch, and reassigned after the loop */
static uint8_t with_loop(struct S1 x1)
{
    uint16_t l2 = 0;
    uint32_t i8;
    l2 = (uint32_t)1550294082;
    for (i8 = 3; i8 > 1; i8 -= 1) {
        if (((uint16_t)x1.m0 > l2) || (l2 < (uint16_t)1))
            r_loop = 1;
        switch ((uint32_t)1) {
        case 100:
            break;
        default:
            break;
        case 10:
            break;
        }
    }
    l2 = (uint16_t)1;
    return 1;
}

static uint8_t with_byte_counter(struct S1 x1)
{
    uint16_t l2 = 0;
    uint8_t k;
    l2 = (uint32_t)1550294082;
    for (k = 3; k > 1; k -= 1) {
        if (((uint16_t)x1.m0 > l2) || (l2 < (uint16_t)1))
            r_counter8 = 1;
        switch ((uint32_t)1) {
        case 100:
            break;
        default:
            break;
        case 10:
            break;
        }
    }
    l2 = (uint16_t)1;
    return 1;
}

static uint8_t switch_on_counter(struct S1 x1)
{
    uint16_t l2 = 0;
    uint32_t i8;
    l2 = (uint32_t)1550294082;
    for (i8 = 3; i8 > 1; i8 -= 1) {
        if (((uint16_t)x1.m0 > l2) || (l2 < (uint16_t)1))
            r_switch_var = 1;
        switch ((uint32_t)i8) {
        case 100:
            break;
        default:
            break;
        case 10:
            break;
        }
    }
    l2 = (uint16_t)1;
    return 1;
}

/* correct: no switch in the loop */
static uint8_t without_switch(struct S1 x1)
{
    uint16_t l2 = 0;
    uint32_t i8;
    l2 = (uint32_t)1550294082;
    for (i8 = 3; i8 > 1; i8 -= 1) {
        if (((uint16_t)x1.m0 > l2) || (l2 < (uint16_t)1))
            r_no_switch = 1;
    }
    l2 = (uint16_t)1;
    return 1;
}

/* correct: no assignment after the loop */
static uint8_t without_tail(struct S1 x1)
{
    uint16_t l2 = 0;
    uint32_t i8;
    l2 = (uint32_t)1550294082;
    for (i8 = 3; i8 > 1; i8 -= 1) {
        if (((uint16_t)x1.m0 > l2) || (l2 < (uint16_t)1))
            r_no_tail = 1;
        switch ((uint32_t)1) {
        case 100:
            break;
        default:
            break;
        case 10:
            break;
        }
    }
    return 1;
}

/* correct: one test only */
static uint8_t single_test(struct S1 x1)
{
    uint16_t l2 = 0;
    uint32_t i8;
    l2 = (uint32_t)1550294082;
    for (i8 = 3; i8 > 1; i8 -= 1) {
        if ((uint16_t)x1.m0 > l2)
            r_plain = 1;
        switch ((uint32_t)1) {
        case 100:
            break;
        default:
            break;
        case 10:
            break;
        }
    }
    l2 = (uint16_t)1;
    return 1;
}

int main(void)
{
    r_loop = r_counter8 = r_switch_var = r_no_switch = r_no_tail = r_plain = 32768;
    with_loop(s1);
    with_byte_counter(s1);
    switch_on_counter(s1);
    without_switch(s1);
    without_tail(s1);
    single_test(s1);
    printf("loop %ld\n", (long)r_loop);
    printf("byte counter %ld\n", (long)r_counter8);
    printf("switch on counter %ld\n", (long)r_switch_var);
    printf("no switch %ld\n", (long)r_no_switch);
    printf("no tail %ld\n", (long)r_no_tail);
    printf("single test %ld\n", (long)r_plain);
    return 0;
}
