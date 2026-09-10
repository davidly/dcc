#include <stdio.h>

typedef int model_value_t;
typedef long weight_value_t;

#define MODEL_VALUE_MAX 32767
#define MODEL_VALUE_MIN (-32768)

#ifdef SMXW10_SHORT_TABLE
#define SMXW10_TABLE_LENGTH 255
#elif defined(SMXW10_LONG_TABLE)
#define SMXW10_TABLE_LENGTH 257
#else
#define SMXW10_TABLE_LENGTH 256
#endif

static model_value_t exponential_table[SMXW10_TABLE_LENGTH] = {
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

static model_value_t vector_maximum(model_value_t *vector,
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

static void softmax_wave10(model_value_t *vector, unsigned char length)
{
    int mx, d, idx, sum, dummy;
    model_value_t *item;
    unsigned char i;

    mx = vector_maximum(vector, length, &dummy);
    sum = 0;
    for (i = 0, item = vector; i < length; i++, item++) {
        d = mx - *item;
        if (d < 0)
            d = 0;
        idx = d >> 3;
        if (idx > 255)
            idx = 255;
        *item = exponential_table[idx];
        sum = sum + *item;
    }
    for (i = 0, item = vector; i < length; i++, item++)
        *item = divide_q8(*item, sum);
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
        index = difference >> 3;
        if (index > 255)
            index = 255;
        vector[i] = reference_exponential(index);
        sum += vector[i];
    }
    for (i = 0; i < length; ++i)
        vector[i] = clamp_to_model_value(
            ((weight_value_t)vector[i] * 256L) / sum);
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
    printf("SMXW10 failures=%d checks=%u hash=%lu\n",
           failures, checks, oracle_hash);
    return failures != 0;
}
