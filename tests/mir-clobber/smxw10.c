#include <stdio.h>

typedef int model_value_t;
typedef long weight_value_t;

#define MODEL_VALUE_MAX 32767
#define MODEL_VALUE_MIN (-32768)

#ifdef SMXW19_VOLATILE_VECTOR
#define SMXW19_VECTOR_QUAL volatile
#else
#define SMXW19_VECTOR_QUAL
#endif

#ifdef SMXW19_HALF_SCALE
#define SMXW19_SCALE 128L
#else
#define SMXW19_SCALE 256L
#endif

#ifdef SMXW19_SHIFT_TWO
#define SMXW19_SHIFT 2
#else
#define SMXW19_SHIFT 3
#endif

#ifdef SMXW19_NE_LOOPS
#define SMXW19_LOOP_TEST(i, length) ((i) != (length))
#else
#define SMXW19_LOOP_TEST(i, length) ((i) < (length))
#endif

#ifdef SMXW10_SHORT_TABLE
#define SMXW10_TABLE_LENGTH 255
#elif defined(SMXW10_LONG_TABLE)
#define SMXW10_TABLE_LENGTH 257
#else
#define SMXW10_TABLE_LENGTH 256
#endif

#ifdef SMXW19_VOLATILE_TABLE
static volatile model_value_t exponential_table[SMXW10_TABLE_LENGTH] = {
#else
static model_value_t exponential_table[SMXW10_TABLE_LENGTH] = {
#endif
    256, 248, 240, 233, 226, 219, 212, 206,
    199, 193, 187, 182, 176, 171, 165, 160
};

static model_value_t clamp_to_model_value(weight_value_t value)
{
    if (value > MODEL_VALUE_MAX)
        return MODEL_VALUE_MAX;
    if (value < MODEL_VALUE_MIN)
        return MODEL_VALUE_MIN;
    return (model_value_t)value;
}

static inline model_value_t divide_q8(model_value_t numerator,
                                      model_value_t denominator)
{
    return clamp_to_model_value(
        ((weight_value_t)numerator * SMXW19_SCALE) / denominator);
}

#ifdef SMXW19_FASTCALL_MAXIMUM
extern model_value_t __fastcall vector_maximum(
    model_value_t *vector, unsigned char length, int *index);
#else
static model_value_t vector_maximum(
    SMXW19_VECTOR_QUAL model_value_t *vector,
    unsigned char length, int *index)
{
    model_value_t maximum;
    unsigned char maximum_index, i;

    maximum = *vector;
    maximum_index = 0;
    for (i = 1, vector++; i < length; i++, vector++) {
        if (*vector > maximum) {
            maximum = *vector;
            maximum_index = i;
        }
    }
    *index = maximum_index;
    return maximum;
}
#endif

#ifdef SMXW19_INDIRECT_MAXIMUM
static model_value_t (*maximum_dispatch)(
    model_value_t *, unsigned char, int *) = vector_maximum;
#endif

#ifdef SMXW19_RETURN_VALUE
static model_value_t softmax_wave10(
#else
static void softmax_wave10(
#endif
    SMXW19_VECTOR_QUAL model_value_t *vector, unsigned char length)
{
    int mx, d, idx, sum, dummy;
    SMXW19_VECTOR_QUAL model_value_t *item;
    unsigned char i;

#ifdef SMXW19_INDIRECT_MAXIMUM
    mx = maximum_dispatch(vector, length, &dummy);
#else
    mx = vector_maximum(vector, length, &dummy);
#endif
    sum = 0;
    for (i = 0, item = vector; SMXW19_LOOP_TEST(i, length);
         i++, item++) {
        d = mx - *item;
        if (d < 0)
            d = 0;
        idx = d >> SMXW19_SHIFT;
        if (idx > 255)
            idx = 255;
        *item = exponential_table[idx];
#ifdef SMXW19_SUBTRACT_SUM
        sum = sum - *item;
#else
        sum = sum + *item;
#endif
    }
    for (i = 0, item = vector; SMXW19_LOOP_TEST(i, length);
         i++, item++)
        *item = divide_q8(*item, sum);
#ifdef SMXW19_RETURN_VALUE
    return sum;
#endif
}

struct GuardedVector {
    model_value_t before;
    model_value_t values[16];
    model_value_t after;
};

static int failures;
static unsigned int checks;
static unsigned long oracle_hash = 2166136261UL;

static model_value_t reference_exponential(int index)
{
    static const model_value_t first_values[16] = {
        256, 248, 240, 233, 226, 219, 212, 206,
        199, 193, 187, 182, 176, 171, 165, 160
    };

    if (index < 16)
        return first_values[index];
    return 0;
}

static void mix(model_value_t value)
{
    oracle_hash =
        (oracle_hash ^ (unsigned int)value) * 16777619UL;
}

static void reference_softmax(model_value_t *vector, unsigned char length)
{
    model_value_t maximum;
    int difference, index, sum;
    unsigned char i;

    maximum = vector[0];
    for (i = 1; i < length; ++i)
        if (vector[i] > maximum)
            maximum = vector[i];
    sum = 0;
    for (i = 0; i < length; ++i) {
        difference = maximum - vector[i];
        if (difference < 0)
            difference = 0;
        index = difference >> SMXW19_SHIFT;
        if (index > 255)
            index = 255;
        vector[i] = reference_exponential(index);
#ifdef SMXW19_SUBTRACT_SUM
        sum -= vector[i];
#else
        sum += vector[i];
#endif
    }
    for (i = 0; i < length; ++i)
        vector[i] = clamp_to_model_value(
            ((weight_value_t)vector[i] * SMXW19_SCALE) / sum);
}

static void run_case(const model_value_t *input, unsigned char length)
{
    struct GuardedVector actual;
    struct GuardedVector expected;
    int i;

    actual.before = expected.before = 0x1357;
    actual.after = expected.after = 0x2468;
    for (i = 0; i < 16; ++i) {
        actual.values[i] = expected.values[i] = (model_value_t)0x5a5a;
        if (i < length)
            actual.values[i] = expected.values[i] = input[i];
    }
    reference_softmax(expected.values, length);
    softmax_wave10(actual.values, length);
    ++checks;
    if (actual.before != expected.before ||
        actual.after != expected.after) {
        ++failures;
    }
    for (i = 0; i < 16; ++i) {
        ++checks;
        if (actual.values[i] != expected.values[i])
            ++failures;
        mix(actual.values[i]);
    }
}

#ifdef SMXW19_ALIAS_CONTROL
static void run_alias_case(void)
{
    static model_value_t expected[SMXW10_TABLE_LENGTH];
    model_value_t maximum;
    int difference, index, sum;
    unsigned char i;

    for (i = 0; i < 16; ++i)
        expected[i] = exponential_table[i];
    maximum = expected[0];
    for (i = 1; i < 16; ++i)
        if (expected[i] > maximum)
            maximum = expected[i];
    sum = 0;
    for (i = 0; i < 16; ++i) {
        difference = maximum - expected[i];
        if (difference < 0)
            difference = 0;
        index = difference >> SMXW19_SHIFT;
        if (index > 255)
            index = 255;
        expected[i] = expected[index];
#ifdef SMXW19_SUBTRACT_SUM
        sum -= expected[i];
#else
        sum += expected[i];
#endif
    }
    for (i = 0; i < 16; ++i)
        expected[i] = clamp_to_model_value(
            ((weight_value_t)expected[i] * SMXW19_SCALE) / sum);
    softmax_wave10(exponential_table, 16);
    ++checks;
    if (exponential_table[16] != 0)
        ++failures;
    for (i = 0; i < 16; ++i) {
        ++checks;
        if (exponential_table[i] != expected[i])
            ++failures;
        mix(exponential_table[i]);
    }
}
#endif

int main(void)
{
    static const model_value_t singleton[] = {32767};
    static const model_value_t equal[] = {
        -123, -123, -123, -123, -123, -123, -123, -123
    };
    static const model_value_t stepped[] = {
        0, -8, -16, -24, -32, -40, -48, -56
    };
    static const model_value_t rounded[] = {32767, 32760, 32752};
    static const model_value_t mixed[] = {
        91, 67, 83, 75, 43, 35, 27, 19,
        11, 3, -5, -13, -21, -29, -37, -45
    };
#ifndef SMXW10_SHORT_TABLE
    static const model_value_t clamped[] = {2048, 0};
#endif

#ifdef SMXW10_LONG_TABLE
    exponential_table[256] = 1234;
#endif
    run_case(singleton, 1);
    run_case(equal, 8);
    run_case(stepped, 8);
    run_case(rounded, 3);
    run_case(mixed, 16);
#ifndef SMXW10_SHORT_TABLE
    run_case(clamped, 2);
#endif
#if defined(SMXW19_ALIAS_CONTROL) && !defined(SMXW19_VOLATILE_TABLE)
    run_alias_case();
#endif
    printf("SMXW10 failures=%d checks=%u hash=%lu\n",
           failures, checks, oracle_hash);
    return failures != 0;
}
