/* Issue #197: prefix byte updates must return the narrowed stored value. */
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>

static unsigned char global_byte;
static volatile unsigned char volatile_byte;
static volatile signed char volatile_signed_byte;
static unsigned char index_values[257];

struct ByteBits {
    unsigned value:8;
};

struct SignedByteBits {
    int value:8;
};

struct ByteValue {
    unsigned char value;
};

static int increment_value(unsigned char i)
{
    return ++i;
}

static int decrement_value(unsigned char i)
{
    return --i;
}

static int postincrement_value(unsigned char i)
{
    return i++;
}

static int postdecrement_value(unsigned char i)
{
    return i--;
}

static int truth_return(unsigned char i)
{
    return ++i ? 1 : 0;
}

static int truth_pair(unsigned char i, unsigned char j)
{
    return ++i && ++j;
}

static int consume_int(int value)
{
    return value;
}

static int consume_variadic(int marker, ...)
{
    va_list arguments;
    int value;

    va_start(arguments, marker);
    value = va_arg(arguments, int);
    va_end(arguments);
    return value;
}

static int conditional_value(unsigned char incremented,
                             unsigned char decremented, int increment)
{
    return increment ? ++incremented : --decremented;
}

static int conditional_postfix_value(unsigned char incremented,
                                     unsigned char decremented, int increment)
{
    return increment ? incremented++ : decremented--;
}

static int switch_increment(unsigned char value)
{
    switch (++value) {
    case 0:
        return 1;
    default:
        return 0;
    }
}

static int switch_decrement(unsigned char value)
{
    switch (--value) {
    case 255:
        return 1;
    default:
        return 0;
    }
}

static int switch_postincrement(unsigned char value)
{
    switch (value++) {
    case 255:
        return value == 0;
    default:
        return 0;
    }
}

static int switch_postdecrement(unsigned char value)
{
    switch (value--) {
    case 0:
        return value == 255;
    default:
        return 0;
    }
}

static int increment_loop(void)
{
    unsigned char i = 0;
    unsigned int count = 0;

    /* do...while evaluates the prefix result at 255 -> 0. */
    do {
        if (++count > 256)
            return 0;
    } while (++i);
    return count == 256 && i == 0;
}

static int decrement_loop(void)
{
    unsigned char i = 0;
    unsigned int count = 0;

    /* The matching decrement path evaluates 0 -> 255 before returning to 0. */
    do {
        if (++count > 256)
            return 0;
    } while (--i);
    return count == 256 && i == 0;
}

/* The update expression itself crosses each byte boundary in the for-loop
 * header. The condition leaves enough room to observe 255 -> 0 and
 * 0 -> 255 without relying on an unbounded loop. */
static int for_rollover_boundaries(void)
{
    unsigned char i;
    unsigned int count;

    count = 0;
    for (i = 254; i != 1; ++i) {
        if (++count > 3)
            return 0;
    }
    if (count != 3 || i != 1)
        return 0;

    count = 0;
    for (i = 1; i != 254; --i) {
        if (++count > 3)
            return 0;
    }
    if (count != 3 || i != 254)
        return 0;

    count = 0;
    for (i = 254; i != 1; i++) {
        if (++count > 3)
            return 0;
    }
    if (count != 3 || i != 1)
        return 0;

    count = 0;
    for (i = 1; i != 254; i--) {
        if (++count > 3)
            return 0;
    }
    return count == 3 && i == 254;
}

/* Prefix and postfix results in a while condition must use the appropriate
 * side of the update while retaining the narrowed stored byte. */
static int while_rollover_boundaries(void)
{
    unsigned char i;
    unsigned int count;

    i = 255;
    count = 0;
    while (++i) {
        if (++count > 1)
            return 0;
    }
    if (count != 0 || i != 0)
        return 0;

    i = 0;
    count = 0;
    while (--i) {
        if (++count > 1)
            return 0;
        break;
    }
    if (count != 1 || i != 255)
        return 0;

    i = UCHAR_MAX;
    count = 0;
    while (i++) {
        if (++count > 1)
            return 0;
    }
    if (count != 1 || i != 1)
        return 0;

    i = 0;
    count = 0;
    while (i--) {
        if (++count > 1)
            return 0;
    }
    return count == 0 && i == UCHAR_MAX;
}

static int do_while_postfix_boundaries(void)
{
    unsigned char i;
    unsigned int count;

    i = UCHAR_MAX;
    count = 0;
    do {
        if (++count > 2)
            return 0;
    } while (i++);
    if (count != 2 || i != 1)
        return 0;

    i = 0;
    count = 0;
    do {
        if (++count > 1)
            return 0;
    } while (i--);
    return count == 1 && i == UCHAR_MAX;
}

static int postfix_lvalue_boundaries(void)
{
    unsigned char bytes[2];
    unsigned char *pointer;
    struct ByteValue member;

    global_byte = UCHAR_MAX;
    if (global_byte++ != UCHAR_MAX || global_byte != 0 ||
        global_byte-- != 0 || global_byte != UCHAR_MAX)
        return 0;

    bytes[0] = UCHAR_MAX;
    if (bytes[0]++ != UCHAR_MAX || bytes[0] != 0 ||
        bytes[0]-- != 0 || bytes[0] != UCHAR_MAX)
        return 0;

    bytes[1] = UCHAR_MAX;
    pointer = &bytes[1];
    if ((*pointer)++ != UCHAR_MAX || *pointer != 0 ||
        (*pointer)-- != 0 || *pointer != UCHAR_MAX)
        return 0;

    member.value = UCHAR_MAX;
    if (member.value++ != UCHAR_MAX || member.value != 0 ||
        member.value-- != 0 || member.value != UCHAR_MAX)
        return 0;
    return 1;
}

/* Every expression below has defined unsigned modulo arithmetic. This covers
 * the byte typedef and byte-shaped lvalues alongside the native word and long
 * widths, without relying on undefined signed overflow. */
static int unsigned_type_boundaries(void)
{
    uint8_t byte_alias;
    unsigned short short_word;
    unsigned int word;
    unsigned long wide;
    struct ByteBits bits;

    byte_alias = UCHAR_MAX;
    if (++byte_alias != 0 || byte_alias != 0 ||
        --byte_alias != UCHAR_MAX || byte_alias != UCHAR_MAX)
        return 0;
    if (byte_alias++ != UCHAR_MAX || byte_alias != 0 ||
        byte_alias-- != 0 || byte_alias != UCHAR_MAX)
        return 0;

    volatile_byte = UCHAR_MAX;
    if (++volatile_byte != 0 || volatile_byte != 0 ||
        --volatile_byte != UCHAR_MAX || volatile_byte != UCHAR_MAX)
        return 0;
    if (volatile_byte++ != UCHAR_MAX || volatile_byte != 0 ||
        volatile_byte-- != 0 || volatile_byte != UCHAR_MAX)
        return 0;

    bits.value = UCHAR_MAX;
    if (++bits.value != 0 || bits.value != 0 ||
        --bits.value != UCHAR_MAX || bits.value != UCHAR_MAX)
        return 0;
    if (bits.value++ != UCHAR_MAX || bits.value != 0 ||
        bits.value-- != 0 || bits.value != UCHAR_MAX)
        return 0;

    short_word = USHRT_MAX;
    if (++short_word != 0U || short_word != 0U ||
        --short_word != USHRT_MAX || short_word != USHRT_MAX)
        return 0;
    if (short_word++ != USHRT_MAX || short_word != 0U ||
        short_word-- != 0U || short_word != USHRT_MAX)
        return 0;

    word = UINT_MAX;
    if (++word != 0U || word != 0U ||
        --word != UINT_MAX || word != UINT_MAX)
        return 0;
    if (word++ != UINT_MAX || word != 0U ||
        word-- != 0U || word != UINT_MAX)
        return 0;

    wide = ULONG_MAX;
    if (++wide != 0UL || wide != 0UL ||
        --wide != ULONG_MAX || wide != ULONG_MAX)
        return 0;
    if (wide++ != ULONG_MAX || wide != 0UL ||
        wide-- != 0UL || wide != ULONG_MAX)
        return 0;
    return 1;
}

/* Signed overflow is undefined, so exercise the values immediately inside
 * each signed range instead: minimum + 1 and maximum - 1. */
static int signed_type_boundaries(void)
{
    char plain_byte;
    signed char byte;
    short short_word;
    int word;
    long wide;
    struct SignedByteBits bits;

    plain_byte = CHAR_MIN;
    if (++plain_byte != CHAR_MIN + 1 || plain_byte != CHAR_MIN + 1)
        return 0;
    plain_byte = CHAR_MAX;
    if (--plain_byte != CHAR_MAX - 1 || plain_byte != CHAR_MAX - 1)
        return 0;
    plain_byte = CHAR_MIN;
    if (plain_byte++ != CHAR_MIN || plain_byte != CHAR_MIN + 1)
        return 0;
    plain_byte = CHAR_MAX;
    if (plain_byte-- != CHAR_MAX || plain_byte != CHAR_MAX - 1)
        return 0;

    byte = SCHAR_MIN;
    if (++byte != SCHAR_MIN + 1 || byte != SCHAR_MIN + 1)
        return 0;
    byte = SCHAR_MAX;
    if (--byte != SCHAR_MAX - 1 || byte != SCHAR_MAX - 1)
        return 0;
    byte = SCHAR_MIN;
    if (byte++ != SCHAR_MIN || byte != SCHAR_MIN + 1)
        return 0;
    byte = SCHAR_MAX;
    if (byte-- != SCHAR_MAX || byte != SCHAR_MAX - 1)
        return 0;

    volatile_signed_byte = SCHAR_MIN;
    if (++volatile_signed_byte != SCHAR_MIN + 1 ||
        volatile_signed_byte != SCHAR_MIN + 1)
        return 0;
    volatile_signed_byte = SCHAR_MAX;
    if (--volatile_signed_byte != SCHAR_MAX - 1 ||
        volatile_signed_byte != SCHAR_MAX - 1)
        return 0;
    volatile_signed_byte = SCHAR_MIN;
    if (volatile_signed_byte++ != SCHAR_MIN ||
        volatile_signed_byte != SCHAR_MIN + 1)
        return 0;
    volatile_signed_byte = SCHAR_MAX;
    if (volatile_signed_byte-- != SCHAR_MAX ||
        volatile_signed_byte != SCHAR_MAX - 1)
        return 0;

    bits.value = SCHAR_MIN;
    if (++bits.value != SCHAR_MIN + 1 || bits.value != SCHAR_MIN + 1)
        return 0;
    bits.value = SCHAR_MAX;
    if (--bits.value != SCHAR_MAX - 1 || bits.value != SCHAR_MAX - 1)
        return 0;
    bits.value = SCHAR_MIN;
    if (bits.value++ != SCHAR_MIN || bits.value != SCHAR_MIN + 1)
        return 0;
    bits.value = SCHAR_MAX;
    if (bits.value-- != SCHAR_MAX || bits.value != SCHAR_MAX - 1)
        return 0;

    short_word = SHRT_MIN;
    if (++short_word != SHRT_MIN + 1 || short_word != SHRT_MIN + 1)
        return 0;
    short_word = SHRT_MAX;
    if (--short_word != SHRT_MAX - 1 || short_word != SHRT_MAX - 1)
        return 0;
    short_word = SHRT_MIN;
    if (short_word++ != SHRT_MIN || short_word != SHRT_MIN + 1)
        return 0;
    short_word = SHRT_MAX;
    if (short_word-- != SHRT_MAX || short_word != SHRT_MAX - 1)
        return 0;

    word = INT_MIN;
    if (++word != INT_MIN + 1 || word != INT_MIN + 1)
        return 0;
    word = INT_MAX;
    if (--word != INT_MAX - 1 || word != INT_MAX - 1)
        return 0;
    word = INT_MIN;
    if (word++ != INT_MIN || word != INT_MIN + 1)
        return 0;
    word = INT_MAX;
    if (word-- != INT_MAX || word != INT_MAX - 1)
        return 0;

    wide = LONG_MIN;
    if (++wide != LONG_MIN + 1L || wide != LONG_MIN + 1L)
        return 0;
    wide = LONG_MAX;
    if (--wide != LONG_MAX - 1L || wide != LONG_MAX - 1L)
        return 0;
    wide = LONG_MIN;
    if (wide++ != LONG_MIN || wide != LONG_MIN + 1L)
        return 0;
    wide = LONG_MAX;
    if (wide-- != LONG_MAX || wide != LONG_MAX - 1L)
        return 0;
    return 1;
}

/* Exercise consumers which need a canonical promoted value rather than only
 * the low byte stored back to the lvalue. */
static int consumer_boundaries(void)
{
    unsigned char value;
    unsigned char stored;
    unsigned char *stored_pointer;
    signed char signed_value;
    _Bool boolean;
    float real;
    int assigned;

    value = UCHAR_MAX;
    assigned = value++;
    if (assigned != UCHAR_MAX || value != 0)
        return 0;
    value = 0;
    assigned = value--;
    if (assigned != 0 || value != UCHAR_MAX)
        return 0;

    value = UCHAR_MAX;
    stored = ++value;
    if (stored != 0 || value != 0)
        return 0;
    value = 0;
    stored = --value;
    if (stored != UCHAR_MAX || value != UCHAR_MAX)
        return 0;
    value = UCHAR_MAX;
    stored = value++;
    if (stored != UCHAR_MAX || value != 0)
        return 0;
    value = 0;
    stored = value--;
    if (stored != 0 || value != UCHAR_MAX)
        return 0;

    stored_pointer = &stored;
    value = UCHAR_MAX;
    *stored_pointer = ++value;
    if (*stored_pointer != 0 || value != 0)
        return 0;
    value = 0;
    *stored_pointer = --value;
    if (*stored_pointer != UCHAR_MAX || value != UCHAR_MAX)
        return 0;
    value = UCHAR_MAX;
    *stored_pointer = value++;
    if (*stored_pointer != UCHAR_MAX || value != 0)
        return 0;
    value = 0;
    *stored_pointer = value--;
    if (*stored_pointer != 0 || value != UCHAR_MAX)
        return 0;

    value = UCHAR_MAX;
    if (consume_int(++value) != 0 || value != 0)
        return 0;
    value = 0;
    if (consume_int(--value) != UCHAR_MAX || value != UCHAR_MAX)
        return 0;
    value = UCHAR_MAX;
    if (consume_int(value++) != UCHAR_MAX || value != 0)
        return 0;
    value = 0;
    if (consume_int(value--) != 0 || value != UCHAR_MAX)
        return 0;

    value = UCHAR_MAX;
    if (consume_variadic(0, ++value) != 0 || value != 0)
        return 0;
    value = 0;
    if (consume_variadic(0, --value) != UCHAR_MAX || value != UCHAR_MAX)
        return 0;
    value = UCHAR_MAX;
    if (consume_variadic(0, value++) != UCHAR_MAX || value != 0)
        return 0;
    value = 0;
    if (consume_variadic(0, value--) != 0 || value != UCHAR_MAX)
        return 0;

    if (conditional_value(UCHAR_MAX, 0, 1) != 0 ||
        conditional_value(UCHAR_MAX, 0, 0) != UCHAR_MAX)
        return 0;
    if (conditional_postfix_value(UCHAR_MAX, 0, 1) != UCHAR_MAX ||
        conditional_postfix_value(UCHAR_MAX, 0, 0) != 0)
        return 0;
    if (!switch_increment(UCHAR_MAX) || !switch_decrement(0))
        return 0;
    if (!switch_postincrement(UCHAR_MAX) || !switch_postdecrement(0))
        return 0;

    index_values[0] = 17;
    index_values[UCHAR_MAX] = 29;
    index_values[256] = 41;
    value = UCHAR_MAX;
    if (index_values[++value] != 17 || value != 0)
        return 0;
    value = 0;
    if (index_values[--value] != 29 || value != UCHAR_MAX)
        return 0;
    value = UCHAR_MAX;
    if (index_values[value++] != 29 || value != 0)
        return 0;
    value = 0;
    if (index_values[value--] != 17 || value != UCHAR_MAX)
        return 0;

    value = UCHAR_MAX;
    if ((unsigned long)++value != 0UL || value != 0)
        return 0;
    value = 0;
    if ((unsigned long)--value != (unsigned long)UCHAR_MAX ||
        value != UCHAR_MAX)
        return 0;
    value = UCHAR_MAX;
    if ((unsigned long)value++ != (unsigned long)UCHAR_MAX || value != 0)
        return 0;
    value = 0;
    if ((unsigned long)value-- != 0UL || value != UCHAR_MAX)
        return 0;

    value = UCHAR_MAX;
    boolean = (_Bool)++value;
    if (boolean != 0 || value != 0)
        return 0;
    value = 0;
    boolean = (_Bool)--value;
    if (boolean != 1 || value != UCHAR_MAX)
        return 0;
    value = UCHAR_MAX;
    boolean = (_Bool)value++;
    if (boolean != 1 || value != 0)
        return 0;
    value = 0;
    boolean = (_Bool)value--;
    if (boolean != 0 || value != UCHAR_MAX)
        return 0;

    value = UCHAR_MAX;
    real = (float)++value;
    if (real != 0.0f || value != 0)
        return 0;
    value = 0;
    real = (float)--value;
    if (real != 255.0f || value != UCHAR_MAX)
        return 0;
    value = UCHAR_MAX;
    real = (float)value++;
    if (real != 255.0f || value != 0)
        return 0;
    value = 0;
    real = (float)value--;
    if (real != 0.0f || value != UCHAR_MAX)
        return 0;

    signed_value = SCHAR_MIN;
    if ((long)++signed_value != (long)SCHAR_MIN + 1L ||
        signed_value != SCHAR_MIN + 1)
        return 0;
    signed_value = SCHAR_MAX;
    if ((long)--signed_value != (long)SCHAR_MAX - 1L ||
        signed_value != SCHAR_MAX - 1)
        return 0;
    signed_value = SCHAR_MIN;
    if ((long)signed_value++ != (long)SCHAR_MIN ||
        signed_value != SCHAR_MIN + 1)
        return 0;
    signed_value = SCHAR_MAX;
    if ((long)signed_value-- != (long)SCHAR_MAX ||
        signed_value != SCHAR_MAX - 1)
        return 0;

    signed_value = SCHAR_MIN;
    real = (float)++signed_value;
    if (real != (float)(SCHAR_MIN + 1) ||
        signed_value != SCHAR_MIN + 1)
        return 0;
    signed_value = SCHAR_MAX;
    real = (float)--signed_value;
    if (real != (float)(SCHAR_MAX - 1) ||
        signed_value != SCHAR_MAX - 1)
        return 0;

    if (postincrement_value(UCHAR_MAX) != UCHAR_MAX ||
        postdecrement_value(0) != 0)
        return 0;
    return 1;
}

int main(void)
{
    unsigned char local = 255;
    unsigned char bytes[2];
    unsigned char *p = bytes;
    struct { unsigned char value; } s;
    signed char signed_byte = -1;
    unsigned int failures = 0;

    if (!increment_loop() || !decrement_loop())
        failures |= 1;
    if (!for_rollover_boundaries())
        failures |= 8192;
    if (!while_rollover_boundaries() || !do_while_postfix_boundaries())
        failures |= 16384;
    if (!unsigned_type_boundaries() || !signed_type_boundaries() ||
        !postfix_lvalue_boundaries() || !consumer_boundaries())
        failures |= 32768U;
    if (++local != 0 || local != 0)
        failures |= 2;
    if (--local != 255 || local != 255)
        failures |= 4;
    global_byte = 255;
    if (++global_byte != 0 || --global_byte != 255)
        failures |= 8;
    bytes[0] = 255;
    bytes[1] = 0;
    if (++*p++ != 0 || p != bytes + 1 || bytes[0] != 0)
        failures |= 16;
    if (--bytes[1] != 255 || bytes[1] != 255)
        failures |= 32;
    s.value = 255;
    if (++s.value != 0 || --s.value != 255)
        failures |= 64;
    if (++signed_byte != 0 || --signed_byte != -1)
        failures |= 128;
    local = 255;
    if (local++ != 255 || local != 0 || local-- != 0 || local != 255)
        failures |= 256;
    if (truth_return(255) != 0 || truth_return(254) != 1 ||
        truth_pair(255, 0) != 0 || truth_pair(0, 255) != 0 ||
        truth_pair(0, 0) != 1)
        failures |= 512;
    local = 255;
    if (256 + ++local != 256)
        failures |= 1024;
    local = 255;
    if ((!++local) != 1)
        failures |= 2048;
    if (increment_value(255) != 0 || increment_value(0) != 1 ||
        decrement_value(0) != 255 || decrement_value(1) != 0)
        failures |= 4096;
    printf("tbytepre failures: %u\n", failures);
    return failures != 0;
}
