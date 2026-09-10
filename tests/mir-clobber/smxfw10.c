#include <stdio.h>

typedef int model_value_t;
typedef long weight_value_t;

#define MODEL_VALUE_MAX 32767
#define MODEL_VALUE_MIN (-32768)
#define SOFTMAX_LENGTH 8

#ifdef SMXFW10_SHORT_TABLE
#define EXPONENTIAL_TABLE_LENGTH 255
#elif defined(SMXFW10_LONG_TABLE)
#define EXPONENTIAL_TABLE_LENGTH 257
#else
#define EXPONENTIAL_TABLE_LENGTH 256
#endif

static model_value_t exponential_table[EXPONENTIAL_TABLE_LENGTH] = {
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
        ((weight_value_t)numerator * 256L) / denominator);
}

static void fixed_softmax_wave10(model_value_t *vector)
{
    int mx, d, idx, sum;
    model_value_t *item;
    unsigned char i;

    mx = vector[0];
    for (i = 1; i < SOFTMAX_LENGTH; i++)
        if (vector[i] > mx)
            mx = vector[i];
    sum = 0;
    for (i = 0, item = vector; i < SOFTMAX_LENGTH; i++, item++) {
        d = mx - *item;
        if (d < 0)
            d = 0;
        idx = d >> 3;
        if (idx > 255)
            idx = 255;
        *item = exponential_table[idx];
#ifdef SMXFW10_SUBTRACT
        sum -= *item;
#else
        sum += *item;
#endif
    }
    for (i = 0, item = vector; i < SOFTMAX_LENGTH; i++, item++)
        *item = divide_q8(*item, sum);
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
    return 0;
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
        if (index > 255)
            index = 255;
        vector[i] = reference_exponential(index);
#ifdef SMXFW10_SUBTRACT
        sum -= vector[i];
#else
        sum += vector[i];
#endif
    }
    for (i = 0; i < SOFTMAX_LENGTH; ++i)
        vector[i] = clamp_to_model_value(
            ((weight_value_t)vector[i] * 256L) / sum);
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
    fixed_softmax_wave10(actual.values);
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

#ifdef SMXFW10_LONG_TABLE
    exponential_table[256] = 1234;
#endif
    run_case(equal);
    run_case(stepped);
#ifndef SMXFW10_SHORT_TABLE
    run_case(clamped);
#endif
    run_case(mixed);
    printf("SMXFW10 failures=%d checks=%u hash=%lu\n",
           failures, checks, oracle_hash);
    return failures != 0;
}
