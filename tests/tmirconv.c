/**
 * @file tmirconv.c
 * @brief Collects MIR regressions for mixed narrow and wide integer operands.
 *
 * @par Coverage
 * Exercises signed 32-bit comparisons against constants in branch conditions,
 * loop conditions that compare a 16-bit index with a 32-bit bound, call
 * arguments whose block-scoped operand needs a narrowing or floating
 * conversion to reach the parameter type, and floating arguments passed to
 * 32-bit integer parameters.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void check(int condition, const char *name)
{
    if (!condition) {
        printf("FAIL %s\n", name);
        failures++;
    }
}

/* ---- signed 32-bit value compared with a constant ----------------------- */
static int16_t cmp_shift_gt(int32_t la)
{
    if ((la >> 1) > 50000) return 2;
    return 0;
}
static int16_t cmp_shift_ge(int32_t la)
{
    if ((la >> 1) >= 50001) return 3;
    return 0;
}
static int16_t cmp_shift_lt(int32_t la)
{
    if ((la >> 1) < -50000) return 4;
    return 0;
}
static int16_t cmp_shift_le(int32_t la)
{
    if ((la >> 1) <= -50001) return 5;
    return 0;
}
static int16_t cmp_sum_gt(int16_t y, int32_t la)
{
    if (((y + la) >> 1) > 50000) return 6;
    return 0;
}
static int16_t cmp_swapped_sum(int16_t y, int32_t la)
{
    if (((la + y) >> 1) > 50000) return 1;
    return 0;
}
static int16_t cmp_named_temp(int16_t y, int32_t la)
{
    int32_t t = (y + la) >> 1;
    if (t > 50000) return 1;
    return 0;
}
static int16_t cmp_selects_argument(int16_t y, int32_t la, int16_t z)
{
    if (((y + la) >> 1) > 50000) return z;
    return 0;
}

static void check_wide_constant_comparisons(void)
{
    check(cmp_shift_gt(200000) == 2, "shift gt above");
    check(cmp_shift_gt(100000) == 0, "shift gt boundary");
    check(cmp_shift_gt(-200000) == 0, "shift gt negative");
    check(cmp_shift_ge(100002) == 3, "shift ge boundary");
    check(cmp_shift_ge(100000) == 0, "shift ge below");
    check(cmp_shift_lt(-100002) == 4, "shift lt below");
    check(cmp_shift_lt(-100000) == 0, "shift lt boundary");
    check(cmp_shift_lt(100002) == 0, "shift lt positive");
    check(cmp_shift_le(-100002) == 5, "shift le boundary");
    check(cmp_shift_le(-100000) == 0, "shift le above");
    check(cmp_sum_gt(0, 200000) == 6, "sum gt above");
    check(cmp_sum_gt(0, 80000) == 0, "sum gt below");
    check(cmp_swapped_sum(0, 200000) == 1, "swapped sum above");
    check(cmp_swapped_sum(0, 80000) == 0, "swapped sum below");
    check(cmp_named_temp(0, 200000) == 1, "named temp above");
    check(cmp_named_temp(0, 80000) == 0, "named temp below");
    check(cmp_selects_argument(0, 200000, 7) == 7, "argument above");
    check(cmp_selects_argument(0, 80000, 7) == 0, "argument below");
}

/* ---- 16-bit loop index compared with a 32-bit bound --------------------- */
static uint8_t touched[64];

static int16_t count_unsigned_below(uint16_t start, uint16_t beyond)
{
    int32_t end = start + 8;
    int32_t cap = (end > beyond) ? beyond : end;
    int16_t n = 0;

    for (uint16_t o = start; o < cap; o++) {
        touched[o & 63] = (uint8_t)o;
        n++;
    }
    return n;
}
static int16_t count_signed_through(int16_t start, int32_t last)
{
    int16_t n = 0;
    int16_t mix = -1;

    for (int16_t i = start; i <= last; i++) {
        n++;
        mix ^= n;
        touched[i & 63] = (uint8_t)mix;
    }
    return n;
}
static int16_t count_bound_on_left(int32_t limit, uint16_t start)
{
    int16_t n = 0;
    int16_t mix = -1;

    for (uint16_t i = start; limit > i; i++) {
        n++;
        mix -= n;
        touched[i & 63] = (uint8_t)mix;
    }
    return n;
}
static int16_t count_down_to(uint16_t start, int32_t floor)
{
    int16_t n = 0;
    int16_t mix = -1;

    for (uint16_t i = start; i >= floor; i--) {
        n++;
        mix += n;
        touched[i & 63] = (uint8_t)mix;
        if (i == 0) break;
    }
    return n;
}

static void check_narrow_index_wide_bound(void)
{
    check(count_unsigned_below(0, 100) == 8, "unsigned index full row");
    check(count_unsigned_below(96, 100) == 4, "unsigned index short row");
    check(touched[3] == 3 && touched[35] == 99, "unsigned index stores");
    check(count_signed_through(5, 10) == 6, "signed index through");
    check(count_signed_through(-3, -5) == 0, "signed index empty");
    check(count_signed_through(-2, 3) == 6, "signed index crosses zero");
    check(count_bound_on_left(10, 3) == 7, "bound on left");
    check(count_bound_on_left(-1, 3) == 0, "negative bound on left");
    check(count_down_to(9, 4) == 6, "count down");
    check(count_down_to(3, 70000) == 0, "count down above range");
}

/* ---- block-scoped operands converted to the parameter type -------------- */
static int16_t take_word(int16_t v) { return (int16_t)(v + 1); }
static int16_t take_byte(uint8_t v) { return v; }
static int32_t take_long(int32_t v) { return v + 100000; }
static float take_float(float v) { return v * 2.0f; }
static uint8_t copied[16];
static char spliced[16];

static int16_t narrow_long_argument(int16_t n)
{
    if (n) {
        int32_t y = 0x12340 + n;
        return take_word(y);
    }
    return 0;
}
static int16_t narrow_long_to_byte(int16_t n)
{
    if (n) {
        int32_t y = 0x12345 + n;
        return take_byte(y);
    }
    return 0;
}
static int16_t float_to_word_argument(int16_t n)
{
    if (n) {
        float x = 2.75f + n;
        return take_word(x);
    }
    return 0;
}
static int16_t float_to_byte_argument(int16_t n)
{
    if (n) {
        float x = 200.5f;
        return take_byte(x);
    }
    return 0;
}
static int16_t word_to_float_argument(int16_t n)
{
    if (n) {
        int16_t z = n * 3;
        return (int16_t)take_float(z);
    }
    return 0;
}
static int32_t word_to_long_argument(int16_t n)
{
    if (n) {
        int16_t z = -n;
        return take_long(z);
    }
    return 0;
}
static int16_t narrow_long_in_loop(int16_t n)
{
    int16_t s = 0;

    while (n--) {
        int32_t y = 0x12340 + n;
        s += take_word(y) & 15;
    }
    return s;
}
static int16_t float_in_loop(int16_t n)
{
    int16_t s = 0;

    while (n--) {
        float x = 2.75f + n;
        s += take_word(x);
    }
    return s;
}
static int16_t word_to_float_in_loop(int16_t n)
{
    int16_t s = 0;

    while (n--) {
        int16_t z = n;
        s += (int16_t)take_float(z);
    }
    return s;
}
static int16_t copy_with_long_count(const uint8_t *source, uint16_t length)
{
    uint16_t offset = 0;
    int16_t sum = 0;

    while (offset < length) {
        int32_t count = ((offset + 4) > length) ? (length % 4) : 4;
        memcpy(copied + offset, source + offset, count);
        offset += 4;
    }
    for (offset = 0; offset < length; offset++)
        sum += copied[offset];
    return sum;
}
static void splice_at_long_offset(char *target, const char *text, int16_t n)
{
    if (n) {
        int32_t at = n;
        strcpy(target + at, text);
    }
}

static void check_block_scoped_arguments(void)
{
    static const uint8_t source[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

    check(narrow_long_argument(1) == 0x2342, "long to word");
    check(narrow_long_to_byte(1) == 0x46, "long to byte");
    check(float_to_word_argument(1) == 4, "float to word");
    check(float_to_byte_argument(1) == 200, "float to byte");
    check(word_to_float_argument(2) == 12, "word to float");
    check(word_to_long_argument(5) == 99995, "word to long");
    check(narrow_long_in_loop(3) == 6, "long to word in loop");
    check(float_in_loop(3) == 12, "float to word in loop");
    check(word_to_float_in_loop(3) == 6, "word to float in loop");
    check(copy_with_long_count(source, 10) == 55, "long copy count");
    strcpy(spliced, "abcdefgh");
    splice_at_long_offset(spliced, "XY", 3);
    check(strcmp(spliced, "abcXY") == 0, "long pointer offset");
}

/* ---- floating arguments converted to a 32-bit integer parameter --------- */
static uint32_t take_ulong(uint32_t v) { return v + 7; }
static int32_t take_pair(int16_t a, int32_t v) { return v - a; }
static int32_t take_two(int32_t a, int32_t b) { return a - b; }
static int32_t seen_long;
static void sink_long(int32_t v) { seen_long = v; }
static int32_t (*indirect_long)(int32_t) = take_long;
static float scale(float v) { return v * 3.0f; }
static float global_value = 123456.75f;

static int32_t float_local_to_long(int16_t n)
{
    float x = 70000.5f + n;
    return take_long(x);
}
static int32_t float_parameter_to_long(float x) { return take_long(x); }
static int32_t negated_float_to_long(float x) { return take_long(-x); }
static uint32_t float_to_unsigned_long(float x) { return take_ulong(x); }
static int32_t float_second_argument(float x) { return take_pair(1, x); }
static int32_t float_both_arguments(float x, float y)
{
    return take_two(x, y);
}
static int32_t float_global_to_long(void) { return take_long(global_value); }
static int32_t float_literal_to_long(void) { return take_long(2.9f); }
static int32_t float_call_to_long(float x) { return take_long(scale(x)); }
static int32_t float_pointer_to_long(const float *p) { return take_long(*p); }
static void float_statement_call(float x) { sink_long(x); }
static int32_t float_indirect_call(float x) { return indirect_long(x); }
static int32_t float_block_to_long(int16_t n)
{
    if (n) {
        float x = 70000.5f + n;
        return take_long(x);
    }
    return 0;
}
static int32_t float_loop_to_long(int16_t n)
{
    int32_t total = 0;

    while (n--) {
        float x = 40000.25f * (n + 1);
        total += take_two(x, 1.5f);
    }
    return total;
}

static void check_float_to_long_arguments(void)
{
    float value = 99999.9f;

    check(float_local_to_long(1) == 170001, "float local to long");
    check(float_parameter_to_long(65536.75f) == 165536, "float to long");
    check(float_parameter_to_long(-70000.9f) == 30000,
          "negative float to long");
    check(negated_float_to_long(70000.9f) == 30000, "negated float to long");
    check(float_to_unsigned_long(3000000000.0f) == 3000000007u,
          "float to unsigned long");
    check(float_second_argument(70000.5f) == 69999, "float second argument");
    check(float_both_arguments(90000.9f, 20000.2f) == 70000,
          "float both arguments");
    check(float_global_to_long() == 223456, "float global to long");
    check(float_literal_to_long() == 100002, "float literal to long");
    check(float_call_to_long(30000.5f) == 190001, "float call to long");
    check(float_pointer_to_long(&value) == 199999, "float pointer to long");
    float_statement_call(-123456.5f);
    check(seen_long == -123456, "float statement call");
    check(float_indirect_call(1.99f) == 100001, "float indirect call");
    check(float_block_to_long(2) == 170002, "float block to long");
    check(float_loop_to_long(3) == 239997, "float loop to long");
}

int main(void)
{
    check_wide_constant_comparisons();
    check_narrow_index_wide_bound();
    check_block_scoped_arguments();
    check_float_to_long_arguments();
    if (failures == 0)
        printf("tmirconv passed with great success\n");
    return failures != 0;
}
