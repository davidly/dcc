#include <stdio.h>

typedef int model_value_t;
#ifdef FIXSMX_UNSIGNED_WEIGHT
typedef unsigned long weight_value_t;
#else
typedef long weight_value_t;
#endif

#define MODEL_VALUE_MAX 32767
#define MODEL_VALUE_MIN (-32768)
#define SOFTMAX_LENGTH 8

#ifdef FIXSMX_SHORT_TABLE
#define EXPONENTIAL_TABLE_LENGTH 255
#elif defined(FIXSMX_LONG_TABLE)
#define EXPONENTIAL_TABLE_LENGTH 257
#else
#define EXPONENTIAL_TABLE_LENGTH 256
#endif

#ifdef FIXSMX_VOLATILE_TABLE
static volatile model_value_t
#else
static model_value_t
#endif
exponential_table[EXPONENTIAL_TABLE_LENGTH] = {
    256, 248, 240, 233, 226, 219, 212, 206,
    199, 193, 187, 182, 176, 171, 165, 160
};

#ifdef FIXSMX_RENAMED_CLAMP
#define CLAMP_FUNCTION renamed_clamp_to_model_value
#else
#define CLAMP_FUNCTION clamp_to_model_value
#endif

#ifdef FIXSMX_VARIADIC_CLAMP
static model_value_t CLAMP_FUNCTION(weight_value_t value, ...)
#else
static model_value_t CLAMP_FUNCTION(weight_value_t value)
#endif
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
#ifdef FIXSMX_SCALE_128
    return CLAMP_FUNCTION(
        ((weight_value_t)numerator * 128L) / denominator);
#elif defined(FIXSMX_INDIRECT_CLAMP)
    model_value_t (*clamp_function)(weight_value_t);

    clamp_function = CLAMP_FUNCTION;
    return clamp_function(
        ((weight_value_t)numerator * 256L) / denominator);
#else
    return CLAMP_FUNCTION(
        ((weight_value_t)numerator * 256L) / denominator);
#endif
}

#ifdef FIXSMX_RENAMED
#define SOFTMAX_FUNCTION fixture_fixed_softmax_renamed
#else
#define SOFTMAX_FUNCTION fixture_fixed_softmax
#endif

#ifdef FIXSMX_VOLATILE_VECTOR
#ifdef FIXSMX_NONVOID_RETURN
static int SOFTMAX_FUNCTION(volatile model_value_t *vector)
#else
static void SOFTMAX_FUNCTION(volatile model_value_t *vector)
#endif
#else
#ifdef FIXSMX_NONVOID_RETURN
static int SOFTMAX_FUNCTION(model_value_t *vector)
#else
static void SOFTMAX_FUNCTION(model_value_t *vector)
#endif
#endif
{
    int mx, d, idx;
#ifdef FIXSMX_VOLATILE_SUM
    volatile int sum;
#else
    int sum;
#endif
#ifdef FIXSMX_VOLATILE_VECTOR
    volatile model_value_t *item;
#else
    model_value_t *item;
#endif
#ifdef FIXSMX_UNSIGNED_COUNT
    unsigned int i;
#else
    unsigned char i;
#endif

#ifdef FIXSMX_EXTRA_CFG
    if (vector == 0)
        return;
#endif
    mx = vector[0];
#ifdef FIXSMX_PREFIX_INCREMENT
    for (i = 1; i < SOFTMAX_LENGTH; ++i)
#else
    for (i = 1; i < SOFTMAX_LENGTH; i++)
#endif
        if (vector[i] > mx)
            mx = vector[i];
    sum = 0;
#ifdef FIXSMX_PREFIX_INCREMENT
    for (i = 0, item = vector; i < SOFTMAX_LENGTH; ++i, ++item) {
#else
    for (i = 0, item = vector; i < SOFTMAX_LENGTH; i++, item++) {
#endif
        d = mx - *item;
        if (d < 0)
            d = 0;
        idx = d >> 3;
#ifdef FIXSMX_CLAMP_127
        if (idx > 127)
            idx = 127;
#else
        if (idx > 255)
            idx = 255;
#endif
        *item = exponential_table[idx];
#ifdef FIXSMX_SUBTRACT
        sum -= *item;
#else
        sum += *item;
#endif
    }
#ifdef FIXSMX_PREFIX_INCREMENT
    for (i = 0, item = vector; i < SOFTMAX_LENGTH; ++i, ++item)
#else
    for (i = 0, item = vector; i < SOFTMAX_LENGTH; i++, item++)
#endif
        *item = divide_q8(*item, sum);
#ifdef FIXSMX_NONVOID_RETURN
    return 7;
#endif
}

struct GuardedVector {
    model_value_t before;
    model_value_t values[SOFTMAX_LENGTH];
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
#ifdef FIXSMX_CLAMP_127
    if (index == 127)
        return 123;
    if (index == 255)
        return 456;
#endif
    return 0;
}

static model_value_t reference_clamp(weight_value_t value)
{
    if (value > MODEL_VALUE_MAX)
        return MODEL_VALUE_MAX;
    if (value < MODEL_VALUE_MIN)
        return MODEL_VALUE_MIN;
    return (model_value_t)value;
}

static void mix(model_value_t value)
{
    oracle_hash =
        (oracle_hash ^ (unsigned int)value) * 16777619UL;
}

static void reference_fixed_softmax(model_value_t *vector)
{
    model_value_t maximum;
    int difference, index, sum;
    unsigned char i;

    maximum = vector[0];
    for (i = 1; i < SOFTMAX_LENGTH; ++i)
        if (vector[i] > maximum)
            maximum = vector[i];
    sum = 0;
    for (i = 0; i < SOFTMAX_LENGTH; ++i) {
        difference = maximum - vector[i];
        if (difference < 0)
            difference = 0;
        index = difference >> 3;
#ifdef FIXSMX_CLAMP_127
        if (index > 127)
            index = 127;
#else
        if (index > 255)
            index = 255;
#endif
        vector[i] = reference_exponential(index);
#ifdef FIXSMX_SUBTRACT
        sum -= vector[i];
#else
        sum += vector[i];
#endif
    }
    for (i = 0; i < SOFTMAX_LENGTH; ++i)
        vector[i] = reference_clamp(
#ifdef FIXSMX_SCALE_128
            ((weight_value_t)vector[i] * 128L) / sum);
#else
            ((weight_value_t)vector[i] * 256L) / sum);
#endif
}

static void run_case(const model_value_t *input)
{
    struct GuardedVector actual;
    struct GuardedVector expected;
    int i;

    actual.before = expected.before = 0x1357;
    actual.after = expected.after = 0x2468;
    for (i = 0; i < SOFTMAX_LENGTH; ++i)
        actual.values[i] = expected.values[i] = input[i];
    reference_fixed_softmax(expected.values);
#ifdef FIXSMX_NONVOID_RETURN
    if (SOFTMAX_FUNCTION(actual.values) != 7)
        ++failures;
#else
    SOFTMAX_FUNCTION(actual.values);
#endif
    ++checks;
    if (actual.before != expected.before ||
        actual.after != expected.after)
        ++failures;
    for (i = 0; i < SOFTMAX_LENGTH; ++i) {
        ++checks;
        if (actual.values[i] != expected.values[i])
            ++failures;
        mix(actual.values[i]);
    }
}

int main(void)
{
    static const model_value_t equal[SOFTMAX_LENGTH] = {
        -123, -123, -123, -123, -123, -123, -123, -123
    };
    static const model_value_t stepped[SOFTMAX_LENGTH] = {
        0, -8, -16, -24, -32, -40, -48, -56
    };
    static const model_value_t clamped[SOFTMAX_LENGTH] = {
        2048, 2048, 2048, 2048, 2048, 2048, 2048, 0
    };
    static const model_value_t mixed[SOFTMAX_LENGTH] = {
        91, 67, 83, 75, 43, 35, 27, 19
    };

#ifdef FIXSMX_LONG_TABLE
    exponential_table[256] = 1234;
#endif
#ifdef FIXSMX_CLAMP_127
    exponential_table[127] = 123;
    exponential_table[255] = 456;
#endif
    run_case(equal);
    run_case(stepped);
#ifndef FIXSMX_SHORT_TABLE
    run_case(clamped);
#endif
    run_case(mixed);
    printf("fixed softmax failures=%d checks=%u hash=%lu\n",
           failures, checks, oracle_hash);
    return failures != 0;
}
