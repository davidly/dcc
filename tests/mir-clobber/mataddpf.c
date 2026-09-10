#include <stdio.h>

typedef int model_value_t;
typedef long weight_value_t;

#define MODEL_VALUE_MAX 32767
#define MODEL_VALUE_MIN (-32768)
#define Q16_MODEL_MAX ((weight_value_t)MODEL_VALUE_MAX * 256L)
#define Q16_MODEL_MIN ((weight_value_t)MODEL_VALUE_MIN * 256L)

struct guarded_vector {
    int before;
    model_value_t values[4];
    int after;
};

static model_value_t clamp_to_model_value(weight_value_t value)
{
    if (value > MODEL_VALUE_MAX)
        return MODEL_VALUE_MAX;
    if (value < MODEL_VALUE_MIN)
        return MODEL_VALUE_MIN;
    return (model_value_t)value;
}

static inline model_value_t q16_to_q8(weight_value_t value)
{
    if (value > Q16_MODEL_MAX)
        return MODEL_VALUE_MAX;
    if (value < Q16_MODEL_MIN)
        return MODEL_VALUE_MIN;
    if (value < 0)
        return (model_value_t)-((-value) >> 8);
    return (model_value_t)(value >> 8);
}

static inline void add_clamped(model_value_t *destination,
                               model_value_t value)
{
    *destination = clamp_to_model_value(
        (weight_value_t)*destination + value);
}

static void matrix_vector_add(model_value_t *matrix, model_value_t *input,
                              model_value_t *output,
                              unsigned char rows, unsigned char columns)
{
    weight_value_t acc;
    unsigned char i, j;

    for (i = 0; i < rows; i++) {
        acc = 0;
        for (j = 0; j < columns; j++)
            acc += (weight_value_t)*matrix++ * input[j];
        add_clamped(output++, q16_to_q8(acc));
    }
}

static void print_vector(const char *name, struct guarded_vector *vector,
                         int count)
{
    int i;

    printf("%s", name);
    for (i = 0; i < count; ++i)
        printf(" %d", vector->values[i]);
    printf(" guard=%d,%d\n", vector->before, vector->after);
}

int main(void)
{
    model_value_t basic_matrix[] = {
        256, 0, 0, 256, 128, 128
    };
    model_value_t basic_input[] = {100, -40};
    struct guarded_vector basic = {1111, {7, -3, 10, 0}, 2222};
    model_value_t round_matrix[] = {-300, 300, -255, 255};
    model_value_t round_input[] = {1};
    struct guarded_vector round = {3333, {10, 10, 10, 10}, 4444};
    model_value_t saturation_matrix[] = {
        32767, -32768, 256, -256
    };
    model_value_t saturation_input[] = {32767};
    struct guarded_vector saturation = {
        5555, {1, -1, 32760, -32760}, 6666
    };
    struct guarded_vector empty = {
        7777, {1234, -2345, 0, 0}, 8888
    };

    matrix_vector_add(
        basic_matrix, basic_input, basic.values, 3, 2);
    matrix_vector_add(
        round_matrix, round_input, round.values, 4, 1);
    matrix_vector_add(
        saturation_matrix, saturation_input, saturation.values, 4, 1);
    matrix_vector_add(
        basic_matrix, basic_input, empty.values, 0, 2);

    print_vector("basic", &basic, 3);
    print_vector("round", &round, 4);
    print_vector("saturation", &saturation, 4);
    print_vector("empty", &empty, 2);
    return 0;
}
