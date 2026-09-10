#include <stdio.h>

typedef int model_value_t;
typedef long weight_value_t;

#define MODEL_VALUE_MAX 32767
#define MODEL_VALUE_MIN (-32768)

static model_value_t exponential_table[256] = {
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

static void softmax(model_value_t *vector, unsigned char length)
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

static void print_vector(const char *name, model_value_t *values, int count)
{
    int i;

    printf("%s", name);
    for (i = 0; i < count; ++i)
        printf(" %d", values[i]);
    putchar('\n');
}

int main(void)
{
    model_value_t equal[] = {0, 0};
    model_value_t stepped[] = {0, -8, -16, -24};
    model_value_t clamped[] = {100, -1940};
    model_value_t rounded[] = {32767, 32760, 32752};

    softmax(equal, 2);
    softmax(stepped, 4);
    softmax(clamped, 2);
    softmax(rounded, 3);
    print_vector("equal", equal, 2);
    print_vector("stepped", stepped, 4);
    print_vector("clamped", clamped, 2);
    print_vector("rounded", rounded, 3);
    return 0;
}
