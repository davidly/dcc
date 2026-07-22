#include <stdio.h>
#include <stdint.h>

union Mini {
    uint8_t bits;
    struct {
        unsigned mant : 4;
        unsigned exp : 3;
        unsigned sign : 1;
    };
};

struct ByteBox { unsigned char value; };

static int32_t decode(union Mini value)
{
    int32_t result = (int32_t)value.mant << value.exp;
    return value.sign ? -result : result;
}

static int byte_value(struct ByteBox box)
{
    return box.value;
}

static int byte_loop_cache(unsigned char first, unsigned char offset)
{
    unsigned char value;
    int sum = 0;

    for (value = first + offset; value < 8; value += offset)
        sum += value;
    return sum;
}

static int byte_loop_alias(unsigned char first, unsigned char offset)
{
    unsigned char value;
    unsigned char *alias = &value;
    int sum = 0;

    for (value = first + offset; value < 8; value += offset) {
        sum += value;
        if (value == 4)
            *alias += offset;
    }
    return sum;
}

static int word_loop_alias(int first, int offset)
{
    int value;
    int *alias = &value;
    int sum = 0;

    for (value = first + offset; value < 8; value += offset) {
        sum += value;
        if (value == 4)
            *alias += offset;
    }
    return sum;
}

int main(void)
{
    union Mini a;
    union Mini b;
    union Mini c;
    struct ByteBox box;

    a.bits = 0;
    a.mant = 5; a.exp = 3; a.sign = 0;
    b.bits = 0;
    b.mant = 12; b.exp = 1; b.sign = 1;
    c.bits = 0xff;
    box.value = 173;

     printf("tpeepal a=%ld b=%ld c=%ld byte=%d bits=%u loop=%d aliases=%d/%d\n",
           (long)decode(a), (long)decode(b), (long)decode(c),
              byte_value(box), (unsigned)a.bits, byte_loop_cache(2, 1),
              byte_loop_alias(2, 1), word_loop_alias(2, 1));
    return 0;
}
