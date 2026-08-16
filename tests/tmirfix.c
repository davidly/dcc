/**
 * @file tmirfix.c
 * @brief Collects MIR correctness regressions found by archive testing.
 *
 * @par Coverage
 * Exercises union/designated initialization, bitfield stores, boolean
 * normalization, indexed byte formatting, scoped declarations, VLAs with
 * narrow and wide elements, and local/global name collisions.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static int failures;

static void check(int condition, const char *name)
{
    if (!condition) {
        printf("FAIL %s\n", name);
        failures++;
    }
}

union ByteFloat {
    unsigned char bytes[4];
    float number;
};

enum MessageKind {
    MESSAGE_TEXT,
    MESSAGE_CODE
};

struct Message {
    enum MessageKind kind;
    union {
        char text[8];
        int code;
    };
};

static void check_union_initializers(void)
{
    union ByteFloat bytes = { { 1, 2, 3, 4 } };
    struct Message messages[2] = {
        { MESSAGE_TEXT, { .text = { 'O', 'K', '\0' } } },
        { MESSAGE_CODE, { .code = 404 } }
    };

    check(bytes.bytes[0] == 1 && bytes.bytes[1] == 2 &&
          bytes.bytes[2] == 3 && bytes.bytes[3] == 4,
          "union array member type");
    check(messages[0].text[0] == 'O' && messages[0].text[1] == 'K' &&
          messages[1].code == 404, "union selected member type");
}

struct Counter {
    union {
        unsigned short raw;
        struct {
            unsigned value : 12;
            unsigned overflow : 1;
            unsigned reserved : 3;
        };
    };
};

static void increment(struct Counter *counter)
{
    if (counter->value == 4095) {
        counter->value = 0;
        counter->overflow = 1;
    } else {
        ++counter->value;
    }
}

static void check_bitfield_store(void)
{
    struct Counter counter;

    counter.raw = 0;
    counter.value = 4095;
    increment(&counter);
    check(counter.value == 0 && counter.overflow == 1,
          "bitfield address preservation");
}

static unsigned hash1(const char *text)
{
    unsigned hash = 0;

    while (*text)
        hash = hash * 31U + (unsigned char)*text++;
    return hash;
}

static unsigned hash2(const char *text)
{
    unsigned hash = 7;

    while (*text)
        hash = hash * 17U ^ (unsigned char)*text++;
    return hash;
}

static void add_word(unsigned char *bits, const char *text)
{
    unsigned first = hash1(text) % 32U;
    unsigned second = hash2(text) % 32U;

    bits[first / 8] |= (unsigned char)(1U << (first % 8));
    bits[second / 8] |= (unsigned char)(1U << (second % 8));
}

static int maybe_has_word(const unsigned char *bits, const char *text)
{
    unsigned first = hash1(text) % 32U;
    unsigned second = hash2(text) % 32U;

    return !!(bits[first / 8] & (1U << (first % 8))) &&
           !!(bits[second / 8] & (1U << (second % 8)));
}

static void check_double_not(void)
{
    unsigned char bits[4] = { 0, 0, 0, 0 };

    add_word(bits, "ALPHA");
    add_word(bits, "GAMMA");
    add_word(bits, "OMEGA");
    check(maybe_has_word(bits, "GAMMA") &&
          !maybe_has_word(bits, "DELTA"), "double logical not lifetime");
}

static void hex_dump(const uint8_t *data, int length, char *out, int capacity)
{
    static const char hex[] = "0123456789ABCDEF";
    int position = 0;

    for (int i = 0; i < length && position + 2 < capacity; i++) {
        out[position++] = hex[data[i] >> 4];
        out[position++] = hex[data[i] & 0x0fU];
    }
    out[position] = '\0';
}

static bool can_jump(int count, const int jumps[count])
{
    int reach = 0;

    for (int i = 0; i <= reach && i < count; i++)
        if (i + jumps[i] > reach)
            reach = i + jumps[i];
    return reach >= count - 1;
}

static int diagonal_sum(int size, const int values[size][3])
{
    int total = 0;

    for (int i = 0; i < size && i < 3; ++i)
        total += values[i][i];
    return total;
}

static int local_bool_vla_count(int count)
{
    bool values[count];
    int i;
    int total = 0;

    for (i = 0; i < count; ++i)
        values[i] = (i & 1) != 0;
    for (i = 0; i < count; ++i)
        total += values[i];
    return total;
}

static long local_long_vla_sum(int count)
{
    long values[count];
    int i;
    long total = 0;

    for (i = 0; i < count; ++i)
        values[i] = 100000L + i;
    for (i = 0; i < count; ++i)
        total += values[i];
    return total;
}

static void check_for_scope_and_vlas(void)
{
    uint8_t data[4] = { 0xde, 0xad, 0xbe, 0xef };
    char out[16];
    int reachable[6] = { 2, 3, 1, 1, 4, 0 };
    int blocked[5] = { 3, 2, 1, 0, 4 };
    int matrix[3][3] = {
        { 2, 0, 0 },
        { 0, 4, 0 },
        { 0, 0, 8 }
    };
    int outer = 73;
    int sum = 0;

    hex_dump(data, 4, out, sizeof(out));
    check(out[0] == 'D' && out[1] == 'E' &&
          out[6] == 'E' && out[7] == 'F', "compound for condition");
    check(can_jump(6, reachable) && !can_jump(5, blocked),
          "VLA parameter and compound for condition");
    check(diagonal_sum(3, matrix) == 14, "multidimensional VLA parameter");
    check(local_bool_vla_count(7) == 3, "local bool VLA pointer type");
    check(local_long_vla_sum(3) == 300003L, "local long VLA pointer type");

    for (int outer = 0, end = 4;
         outer < end && sum < 10;
         ++outer)
        sum += outer;
    check(outer == 73 && sum == 6, "for declaration scope boundary");
}

enum JobKind {
    JOB_COPY,
    JOB_DELETE
};

struct Job {
    enum JobKind kind;
    union {
        struct {
            int source;
            int target;
        };
        int file;
    };
};

static void check_local_global_name_collision(void)
{
    struct Job copy = { JOB_COPY, { .source = 2, .target = 9 } };
    struct Job remove = { JOB_DELETE, { .file = 7 } };

    check(copy.source == 2 && copy.target == 9 &&
          remove.kind == JOB_DELETE && remove.file == 7,
          "local and global name collision");
}

int main(void)
{
    check_union_initializers();
    check_bitfield_store();
    check_double_not();
    check_for_scope_and_vlas();
    check_local_global_name_collision();
    if (failures == 0)
        printf("tmirfix passed with great success\n");
    return failures != 0;
}
