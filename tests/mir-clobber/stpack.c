#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef STATUS_RENAMED
#define pack_status renamed_pack_status
#define state renamed_state
#define consume_status renamed_consumer
#define negative renamed_negative
#define overflow renamed_overflow
#endif

struct StatusState {
#ifdef STATUS_BITFIELDS
    bool negative : 1;
    bool overflow : 1;
    bool decimal : 1;
    bool interrupt_disable : 1;
    bool zero : 1;
    bool carry : 1;
#else
    bool negative;
    bool overflow;
    bool decimal;
    bool interrupt_disable;
    bool zero;
    bool carry;
#endif
};

#ifdef STATUS_VOLATILE
static volatile struct StatusState state;
#else
static struct StatusState state;
#endif
unsigned int observed;
static unsigned int calls;

#ifdef STATUS_FASTCALL
extern void __fastcall consume_status(uint8_t value);
#asm
        public  _consume_status
_consume_status:
        ld      (_observed),hl
        ret
#endasm
#else
static void consume_status(uint8_t value)
{
    observed = value;
    ++calls;
}
#endif

static void pack_status(void)
{
#ifdef STATUS_LOCAL_VOLATILE
    volatile uint8_t value = 0x30;
#else
    uint8_t value = 0x30;
#endif

    if (state.negative)
#ifdef STATUS_BOOL_CAST
        value = (bool)value | 0x80;
#elif defined(STATUS_UNSIGNED_MASK)
        value |= 0x80u;
#else
        value |= 0x80;
#endif
    if (state.overflow)
#ifdef STATUS_MASK
        value |= 0x20;
#else
        value |= 0x40;
#endif
    if (state.decimal)
        value |= 0x08;
    if (state.interrupt_disable)
        value |= 0x04;
    if (state.zero)
        value |= 0x02;
    if (state.carry)
        value |= 0x01;
    consume_status(value);
}

int main(void)
{
    unsigned int combination;
    unsigned int failures = 0;
    unsigned int expected;

    for (combination = 0; combination < 64; ++combination) {
        state.negative = (combination & 32) != 0;
        state.overflow = (combination & 16) != 0;
        state.decimal = (combination & 8) != 0;
        state.interrupt_disable = (combination & 4) != 0;
        state.zero = (combination & 2) != 0;
        state.carry = (combination & 1) != 0;
        expected = 0x30;
        if (combination & 32) {
#if defined(STATUS_BOOL_CAST) || defined(STATUS_EXPECT_BOOLEAN)
            expected = 1;
#endif
            expected += 0x80;
        }
#ifndef STATUS_MASK
        if (combination & 16)
            expected += 0x40;
#else
        if (combination & 16)
            expected |= 0x20;
#endif
        expected |= combination & 15;
        calls = 0;
        pack_status();
        if (observed != expected
#ifndef STATUS_FASTCALL
            || calls != 1
#endif
           )
            ++failures;
    }
    printf("status pack checks=64 failures=%u\n", failures);
    return failures != 0;
}
