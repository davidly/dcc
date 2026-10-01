#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef BYTE_ROTATE_FLAGS_RENAMED
#define state renamed_state
#define byte_rotate_flags_fixture byte_rotate_flags_fixture_renamed
#endif

#ifdef BYTE_ROTATE_FLAGS_BYTE_FLAGS
typedef uint8_t flag_t;
#elif defined(BYTE_ROTATE_FLAGS_WIDE_FLAGS)
typedef unsigned int flag_t;
#else
typedef bool flag_t;
#endif

struct RotateFlagsState {
    uint8_t a;
    uint8_t x;
    uint8_t y;
    uint8_t sp;
    uint16_t pc;
    flag_t negative;
    flag_t overflow;
    flag_t decimal;
    flag_t interrupt_disable;
    flag_t zero;
    flag_t carry;
};

#ifdef BYTE_ROTATE_FLAGS_VOLATILE_STATE
static volatile struct RotateFlagsState state;
#else
static struct RotateFlagsState state;
#endif

#ifdef BYTE_ROTATE_FLAGS_CHANGED_MASK
#define OPERATION_MASK 0xf0
#else
#define OPERATION_MASK 0xe0
#endif

#ifdef BYTE_ROTATE_FLAGS_CHANGED_SHIFT
#define SHIFT_COUNT 2
#else
#define SHIFT_COUNT 1
#endif

static uint8_t byte_rotate_flags_fixture(
    uint8_t operation, uint8_t value)
{
    bool old_carry;

#ifdef BYTE_ROTATE_FLAGS_EXTRA_CFG
    if (operation == 0xff)
        operation = 0;
#endif
    operation &= OPERATION_MASK;
    if (0 == operation) {
        state.carry = (0x80 & value);
        value <<= SHIFT_COUNT;
    } else if (0x20 == operation) {
        old_carry = state.carry;
#ifdef BYTE_ROTATE_FLAGS_OVERWRITE_VALUE
        value = (uint8_t)old_carry;
#endif
        state.carry = (0x80 & value);
        value <<= SHIFT_COUNT;
        if (old_carry)
            value |= 1;
    } else if (0x40 == operation) {
        state.carry = (value & 1);
        value >>= SHIFT_COUNT;
    } else {
        old_carry = state.carry;
#ifdef BYTE_ROTATE_FLAGS_OVERWRITE_VALUE
        value = (uint8_t)old_carry;
#endif
        state.carry = (value & 1);
        value >>= SHIFT_COUNT;
#if defined(BYTE_ROTATE_FLAGS_OVERWRITE_VALUE) && \
    defined(BYTE_ROTATE_FLAGS_CAST_ONLY_CARRY)
        if (state.carry)
#else
        if (old_carry)
#endif
            value |= 0x80;
    }
    state.negative = (value & 0x80), state.zero = !(value);
    return value;
}

static uint8_t reference_rotate(
    uint8_t operation, uint8_t value, unsigned int carry,
    unsigned int *new_carry)
{
#ifdef BYTE_ROTATE_FLAGS_OVERWRITE_VALUE
    unsigned int masked = (unsigned int)operation & OPERATION_MASK;
    unsigned int input;

#ifdef BYTE_ROTATE_FLAGS_EXTRA_CFG
    if (operation == 0xff)
        masked = 0;
#endif
    input = (masked == 0 || masked == 0x40) ? value : carry;
    if (masked == 0 || masked == 0x20) {
        *new_carry = (unsigned int)(flag_t)(input & 0x80);
        return (uint8_t)((input << SHIFT_COUNT) |
                        (masked == 0x20 ? carry : 0));
    }
    *new_carry = input & 1;
    return (uint8_t)((input >> SHIFT_COUNT) |
                    (masked == 0x40 || !carry ? 0 : 0x80));
#else
#ifdef BYTE_ROTATE_FLAGS_EXTRA_CFG
    if (operation == 0xff)
        operation = 0;
#endif
    operation &= OPERATION_MASK;
    if (operation == 0) {
        *new_carry = (unsigned int)(flag_t)(value & 0x80);
        return (uint8_t)(value << SHIFT_COUNT);
    }
    if (operation == 0x20) {
        *new_carry = (unsigned int)(flag_t)(value & 0x80);
        return (uint8_t)((value << SHIFT_COUNT) | carry);
    }
    if (operation == 0x40) {
        *new_carry = value & 1;
        return (uint8_t)(value >> SHIFT_COUNT);
    }
    *new_carry = value & 1;
    return (uint8_t)((value >> SHIFT_COUNT) | (carry ? 0x80 : 0));
#endif
}

int main(void)
{
    static const uint8_t operations[] = {
        0x00, 0x20, 0x40, 0x60, 0x10, 0x30, 0x50, 0x70, 0xff
    };
    static const uint8_t values[] = {
        0x00, 0x01, 0x02, 0x40, 0x7f, 0x80, 0x81, 0xff
    };
    unsigned int failures = 0;
    unsigned int checks = 0;
    unsigned long checksum = 0;
    unsigned int operation_index;
    unsigned int value_index;
    unsigned int carry;

    for (operation_index = 0;
         operation_index < sizeof(operations); ++operation_index)
        for (value_index = 0;
             value_index < sizeof(values); ++value_index)
            for (carry = 0; carry < 2; ++carry) {
                unsigned int expected_carry;
                uint8_t expected;
                uint8_t actual;
                unsigned int negative;
                unsigned int zero;

                state.carry = (flag_t)carry;
                state.a = 0x5a;
                state.pc = 0xabcd;
                state.overflow = (flag_t)1;
                state.decimal = (flag_t)1;
                state.interrupt_disable = (flag_t)1;
                expected = reference_rotate(
                    operations[operation_index], values[value_index],
                    carry, &expected_carry);
                actual = byte_rotate_flags_fixture(
                    operations[operation_index], values[value_index]);
                negative = (unsigned int)(flag_t)(expected & 0x80);
                zero = expected == 0;
                if (actual != expected ||
                    (unsigned int)state.carry != expected_carry ||
                    (unsigned int)state.negative != negative ||
                    (unsigned int)state.zero != zero ||
                    state.a != 0x5a || state.pc != 0xabcd ||
                    state.overflow != (flag_t)1 ||
                    state.decimal != (flag_t)1 ||
                    state.interrupt_disable != (flag_t)1)
                    ++failures;
                checksum = checksum * 131UL + actual;
                checksum = checksum * 5UL +
                    (unsigned int)state.carry * 4U +
                    (unsigned int)state.negative * 2U +
                    (unsigned int)state.zero;
                ++checks;
            }
    printf(
        "byte rotate flags failures=%u checks=%u checksum=%lu\n",
        failures, checks, checksum);
    return failures != 0;
}
