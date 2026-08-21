#include <stdio.h>

#define LIMB_COUNT 4

struct Limbs {
    unsigned long word[LIMB_COUNT];
};

void limb_shift_left(struct Limbs *value)
{
    unsigned long carry;
    unsigned long next_carry;
    int index;

    carry = 0UL;
    for (index = 0; index < LIMB_COUNT; ++index) {
        next_carry = value->word[index] >> 31;
        value->word[index] = (value->word[index] << 1) | carry;
        carry = next_carry;
    }
}

void limb_shift_right(struct Limbs *value)
{
    unsigned long carry;
    unsigned long next_carry;
    int index;

    carry = 0UL;
    for (index = LIMB_COUNT - 1; index >= 0; --index) {
        next_carry = value->word[index] & 1UL;
        value->word[index] = (value->word[index] >> 1) | (carry << 31);
        carry = next_carry;
    }
}

void limb_shift_left_with_carry(struct Limbs *value)
{
    unsigned long carry;
    unsigned long next_carry;
    int index;

    carry = 1UL;
    for (index = 0; index < LIMB_COUNT; ++index) {
        next_carry = value->word[index] >> 31;
        value->word[index] = (value->word[index] << 1) | carry;
        carry = next_carry;
    }
}

void volatile_limb_shift_left(volatile struct Limbs *value)
{
    unsigned long carry;
    unsigned long next_carry;
    int index;

    carry = 0UL;
    for (index = 0; index < LIMB_COUNT; ++index) {
        next_carry = value->word[index] >> 31;
        value->word[index] = (value->word[index] << 1) | carry;
        carry = next_carry;
    }
}

int limbs_are_zero(const struct Limbs *value)
{
    int index;

    for (index = 0; index < LIMB_COUNT; ++index)
        if (value->word[index] != 0UL)
            return 0;
    return 1;
}

int volatile_limbs_are_zero(const volatile struct Limbs *value)
{
    int index;

    for (index = 0; index < LIMB_COUNT; ++index)
        if (value->word[index] != 0UL)
            return 0;
    return 1;
}

void limbs_add(const struct Limbs *left, const struct Limbs *right,
               struct Limbs *result)
{
    unsigned long left_word;
    unsigned long sum;
    unsigned long with_carry;
    int carry;
    int next_carry;
    int index;

    carry = 0;
    for (index = 0; index < LIMB_COUNT; ++index) {
        left_word = left->word[index];
        sum = left_word + right->word[index];
        next_carry = sum < left_word;
        with_carry = sum + (unsigned long)carry;
        if (carry && with_carry == 0UL)
            next_carry = 1;
        result->word[index] = with_carry;
        carry = next_carry;
    }
}

void limbs_subtract(const struct Limbs *left, const struct Limbs *right,
                    struct Limbs *result)
{
    unsigned long left_word;
    unsigned long difference;
    unsigned long with_borrow;
    int borrow;
    int next_borrow;
    int index;

    borrow = 0;
    for (index = 0; index < LIMB_COUNT; ++index) {
        left_word = left->word[index];
        difference = left_word - right->word[index];
        next_borrow = left_word < right->word[index];
        with_borrow = difference - (unsigned long)borrow;
        if (borrow && difference == 0UL)
            next_borrow = 1;
        result->word[index] = with_borrow;
        borrow = next_borrow;
    }
}

void volatile_limbs_add(const volatile struct Limbs *left,
                        const volatile struct Limbs *right,
                        volatile struct Limbs *result)
{
    unsigned long left_word;
    unsigned long sum;
    unsigned long with_carry;
    int carry;
    int next_carry;
    int index;

    carry = 0;
    for (index = 0; index < LIMB_COUNT; ++index) {
        left_word = left->word[index];
        sum = left_word + right->word[index];
        next_carry = sum < left_word;
        with_carry = sum + (unsigned long)carry;
        if (carry && with_carry == 0UL)
            next_carry = 1;
        result->word[index] = with_carry;
        carry = next_carry;
    }
}

void limbs_zero(struct Limbs *value)
{
    int index;

    for (index = 0; index < LIMB_COUNT; ++index)
        value->word[index] = 0UL;
}

void volatile_limbs_zero(volatile struct Limbs *value)
{
    int index;

    for (index = 0; index < LIMB_COUNT; ++index)
        value->word[index] = 0UL;
}

static int check(const struct Limbs *value,
                 unsigned long word0, unsigned long word1,
                 unsigned long word2, unsigned long word3)
{
    return value->word[0] == word0 && value->word[1] == word1 &&
           value->word[2] == word2 && value->word[3] == word3;
}

int main(void)
{
    struct Limbs value;
    struct Limbs other;

    value.word[0] = 0x80000001UL;
    value.word[1] = 0UL;
    value.word[2] = 0xffffffffUL;
    value.word[3] = 0x40000000UL;
    limb_shift_left(&value);
    if (!check(&value, 2UL, 1UL, 0xfffffffeUL, 0x80000001UL))
        return 1;

    value.word[0] = 1UL;
    value.word[1] = 0UL;
    value.word[2] = 0UL;
    value.word[3] = 1UL;
    limb_shift_right(&value);
    if (!check(&value, 0UL, 0UL, 0x80000000UL, 0UL))
        return 1;

    value.word[0] = 0UL;
    value.word[1] = 0UL;
    value.word[2] = 0UL;
    value.word[3] = 0UL;
    limb_shift_left_with_carry(&value);
    if (!check(&value, 1UL, 0UL, 0UL, 0UL))
        return 1;

    value.word[0] = 0x80000000UL;
    value.word[1] = 0UL;
    value.word[2] = 0UL;
    value.word[3] = 0UL;
    volatile_limb_shift_left(&value);
    if (!check(&value, 0UL, 1UL, 0UL, 0UL))
        return 1;

    value.word[0] = 0UL;
    value.word[1] = 0UL;
    value.word[2] = 0UL;
    value.word[3] = 0UL;
    if (!limbs_are_zero(&value) || !volatile_limbs_are_zero(&value))
        return 1;
    value.word[3] = 1UL;
    if (limbs_are_zero(&value) || volatile_limbs_are_zero(&value))
        return 1;

    value.word[0] = 0xffffffffUL;
    value.word[1] = 0UL;
    value.word[2] = 0xffffffffUL;
    value.word[3] = 0xffffffffUL;
    other.word[0] = 1UL;
    other.word[1] = 0xffffffffUL;
    other.word[2] = 0UL;
    other.word[3] = 0UL;
    limbs_add(&value, &other, &value);
    if (!check(&value, 0UL, 0UL, 0UL, 0UL))
        return 1;
    limbs_subtract(&value, &other, &value);
    if (!check(&value, 0xffffffffUL, 0UL,
               0xffffffffUL, 0xffffffffUL))
        return 1;
    volatile_limbs_add(&value, &other, &value);
    if (!check(&value, 0UL, 0UL, 0UL, 0UL))
        return 1;
    value.word[0] = 1UL;
    value.word[1] = 2UL;
    value.word[2] = 3UL;
    value.word[3] = 4UL;
    limbs_zero(&value);
    if (!limbs_are_zero(&value))
        return 1;
    value.word[0] = 1UL;
    value.word[1] = 2UL;
    value.word[2] = 3UL;
    value.word[3] = 4UL;
    volatile_limbs_zero(&value);
    if (!limbs_are_zero(&value))
        return 1;

    puts("ok");
    return 0;
}