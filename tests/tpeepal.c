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

/* Regression: the address of a scalar local escapes into a PERSISTENT global
 * pointer, then the local is written by name (a direct `ld (ix+K)` store) and
 * read back only through the escaped pointer across a call. The dead-store
 * peephole must not delete that write-back. */
int *g_escaped_ptr;
static char *g_escaped_byte_ptr;
static int g_escape_marker;

static int read_escaped(void)
{
    return *g_escaped_ptr;
}

static void retain_escaped(int *ptr)
{
    g_escaped_ptr = ptr;
}

static int global_escape_store(int seed)
{
    int local = seed;
    g_escaped_ptr = &local;
    if (seed)
        g_escape_marker = seed;
    local = seed + 100;
    return read_escaped();
}

static int call_escape_store(int seed)
{
    int local = seed;
    retain_escaped(&local);
    local = seed + 200;
    return read_escaped();
}

static int interior_escape_store(void)
{
    int local = 0;
    g_escaped_byte_ptr = ((char *)&local) + 1;
    local = 0x1234;
    return (unsigned char)g_escaped_byte_ptr[-1];
}

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

         printf("tpeepal a=%ld b=%ld c=%ld byte=%d bits=%u loop=%d aliases=%d/%d escape=%d/%d/%d\n",
           (long)decode(a), (long)decode(b), (long)decode(c),
              byte_value(box), (unsigned)a.bits, byte_loop_cache(2, 1),
              byte_loop_alias(2, 1), word_loop_alias(2, 1),
              global_escape_store(1), interior_escape_store(), call_escape_store(1));
    return 0;
}
