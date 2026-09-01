/* Exercise generic MIR load reuse and little-endian load combining. */
#include <stdio.h>

static int fails;

static void chk(const char *name, int got, int expected)
{
    if (got != expected) {
        printf("FAIL %s got %d expected %d\n", name, got, expected);
        fails++;
    }
}

static void spoil(int value)
{
    (void)value;
}

static int repeated_branch_load(
    const unsigned char *value, int offset, int index)
{
    if (value[offset + index] < '0' || value[offset + index] > '9')
        return -1;
    return value[offset + index] - '0';
}

static int store_between_loads(unsigned char *value)
{
    int before;

    before = *value;
    *value = 7;
    return before + *value + *value;
}

static int call_between_loads(unsigned char *value)
{
    int before;

    before = *value;
    spoil(before);
    return before + *value + *value;
}

static int loop_store_after_loads(unsigned char *value)
{
    int i;
    int sum;

    sum = *value;
    for (i = 0; i < 2; i++) {
        sum += *value + *value;
        (*value)++;
    }
    return sum + *value;
}

static int repeated_volatile_load(const volatile unsigned char *value)
{
    return *value + *value + *value;
}

static unsigned int read_little_endian_16(const unsigned char *value)
{
    return (unsigned int)value[0] | ((unsigned int)value[1] << 8);
}

static unsigned int read_nonadjacent_16(const unsigned char *value)
{
    return (unsigned int)value[0] | ((unsigned int)value[2] << 8);
}

static unsigned int read_reversed_16(const unsigned char *value)
{
    return (unsigned int)value[1] | ((unsigned int)value[0] << 8);
}

static unsigned int read_interrupted_16(unsigned char *value)
{
    unsigned int low;

    low = value[0];
    spoil(low);
    return low | ((unsigned int)value[1] << 8);
}

int main(void)
{
    unsigned char bytes[3];
    volatile unsigned char volatile_byte;

    fails = 0;
    bytes[0] = '7';
    chk("repeated_branch_load", repeated_branch_load(bytes, 0, 0), 7);
    bytes[0] = 5;
    chk("store_between_loads", store_between_loads(bytes), 19);
    chk("call_between_loads", call_between_loads(bytes), 21);
    bytes[0] = 2;
    chk("loop_store_after_loads", loop_store_after_loads(bytes), 16);
    volatile_byte = 3;
    chk("repeated_volatile_load", repeated_volatile_load(&volatile_byte), 9);
    bytes[0] = 0x34;
    bytes[1] = 0x12;
    bytes[2] = 0x56;
    chk("read_little_endian_16", read_little_endian_16(bytes), 0x1234);
    chk("read_nonadjacent_16", read_nonadjacent_16(bytes), 0x5634);
    chk("read_reversed_16", read_reversed_16(bytes), 0x3412);
    chk("read_interrupted_16", read_interrupted_16(bytes), 0x1234);

    if (fails) {
        printf("tmirload failed: %d\n", fails);
        return 1;
    }
    printf("tmirload completed with great success\n");
    return 0;
}