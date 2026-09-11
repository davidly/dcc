#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef MIR_W24_BYTE_FLAGS
typedef uint8_t flag_t;
#else
typedef bool flag_t;
#endif

struct RotateState {
#ifdef MIR_W24_STATE_PREFIX
    uint8_t prefix;
#endif
    uint8_t a, x, y, sp;
    uint16_t pc;
    flag_t negative, overflow, decimal, interrupt_disable, zero, carry;
};

#ifdef MIR_W24_VOLATILE_STATE
static volatile struct RotateState state;
#else
static struct RotateState state;
#endif

#ifdef MIR_W24_RENAMED
#define rotate_byte rotate_byte_wave24
#endif

#ifdef MIR_W24_SIGNED_OP
typedef int8_t operation_t;
#else
typedef uint8_t operation_t;
#endif

#ifdef MIR_W24_WIDE_VALUE
typedef unsigned int value_t;
#else
typedef uint8_t value_t;
#endif

#ifdef MIR_W24_MASK_F0
#define ROTATE_MASK 0xf0
#else
#define ROTATE_MASK 0xe0
#endif

#ifdef MIR_W24_CARRY_40
#define HIGH_BIT 0x40
#else
#define HIGH_BIT 0x80
#endif

#ifdef MIR_W24_SHIFT_TWO
#define SHIFT_COUNT 2
#else
#define SHIFT_COUNT 1
#endif

#ifdef MIR_W24_HELPER_FLAGS
static void set_flags(uint8_t value)
{
    state.negative = (value & 0x80) != 0;
    state.zero = value == 0;
}
#define SET_FLAGS(value) set_flags(value)
#else
#define SET_FLAGS(value) \
    (state.negative = ((value) & 0x80), state.zero = !(value))
#endif

#ifdef MIR_W24_RETURN_INT
static unsigned int rotate_byte(operation_t op, value_t value)
#else
static uint8_t rotate_byte(operation_t op, value_t value)
#endif
{
    bool old_carry;

    op &= ROTATE_MASK;
    if (0 == op) {
        state.carry = (HIGH_BIT & value);
        value <<= SHIFT_COUNT;
    } else if (0x20 == op) {
        old_carry = state.carry;
        state.carry = (HIGH_BIT & value);
#ifdef MIR_W24_BRANCHLESS_CARRY
        value = (uint8_t)((value << 1) | old_carry);
#else
        value <<= 1;
        if (old_carry)
            value |= 1;
#endif
    } else if (0x40 == op) {
        state.carry = (value & 1);
        value >>= 1;
    } else {
        old_carry = state.carry;
        state.carry = (value & 1);
#ifdef MIR_W24_BRANCHLESS_CARRY
        value = (uint8_t)((value >> 1) | (old_carry << 7));
#else
        value >>= 1;
        if (old_carry)
            value |= 0x80;
#endif
    }

    SET_FLAGS(value);
    return value;
}

static void check(unsigned int op, unsigned int value, unsigned int carry)
{
    unsigned int result;

    state.carry = (flag_t)carry;
    result = (uint8_t)rotate_byte(
        (operation_t)op, (value_t)value);
    printf("%u %u %u %u %u\n", op, value, result,
           (unsigned int)state.carry,
           (unsigned int)state.negative * 2U + state.zero);
}

int main(void)
{
    check(0x00, 0x80, 0);
    check(0x00, 0x01, 1);
    check(0x20, 0x80, 0);
    check(0x20, 0x40, 1);
    check(0x40, 0x01, 1);
    check(0x40, 0x80, 0);
    check(0x60, 0x01, 0);
    check(0x60, 0x02, 1);
    check(0x10, 0x02, 1);
    check(0x30, 0x40, 1);
    return 0;
}
