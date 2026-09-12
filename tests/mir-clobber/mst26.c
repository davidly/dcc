#include <stdio.h>

typedef int model_value_t;
typedef long weight_value_t;

#define MODEL_VALUE_MAX 32767
#define MODEL_VALUE_MIN (-32768)
#define Q16_MODEL_MAX ((weight_value_t)MODEL_VALUE_MAX * 256L)
#define Q16_MODEL_MIN ((weight_value_t)MODEL_VALUE_MIN * 256L)

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

static model_value_t q16_to_q8_call(weight_value_t value)
{
    return q16_to_q8(value);
}

static void matrix_product_store_exact(
    model_value_t *matrix, model_value_t *input,
    model_value_t *output, unsigned char rows,
    unsigned char columns)
{
    weight_value_t acc;
    unsigned char i, j;

    for (i = 0; i < rows; i++) {
        acc = 0;
        for (j = 0; j < columns; j++)
            acc += (weight_value_t)*matrix++ * input[j];
        *output++ = q16_to_q8(acc);
    }
}

static void matrix_product_store_signed_counts(
    model_value_t *matrix, model_value_t *input,
    model_value_t *output, signed char rows,
    signed char columns)
{
    weight_value_t acc;
    signed char i, j;

    for (i = 0; i < rows; i++) {
        acc = 0;
        for (j = 0; j < columns; j++)
            acc += (weight_value_t)*matrix++ * input[j];
        *output++ = q16_to_q8(acc);
    }
}

static void matrix_product_store_volatile_output(
    model_value_t *matrix, model_value_t *input,
    volatile model_value_t *output, unsigned char rows,
    unsigned char columns)
{
    weight_value_t acc;
    unsigned char i, j;

    for (i = 0; i < rows; i++) {
        acc = 0;
        for (j = 0; j < columns; j++)
            acc += (weight_value_t)*matrix++ * input[j];
        *output++ = q16_to_q8(acc);
    }
}

static void matrix_product_store_called(
    model_value_t *matrix, model_value_t *input,
    model_value_t *output, unsigned char rows,
    unsigned char columns)
{
    weight_value_t acc;
    unsigned char i, j;

    for (i = 0; i < rows; i++) {
        acc = 0;
        for (j = 0; j < columns; j++)
            acc += (weight_value_t)*matrix++ * input[j];
        *output++ = q16_to_q8_call(acc);
    }
}

static int matrix_product_store_returning(
    model_value_t *matrix, model_value_t *input,
    model_value_t *output, unsigned char rows,
    unsigned char columns)
{
    weight_value_t acc;
    unsigned char i, j;

    for (i = 0; i < rows; i++) {
        acc = 0;
        for (j = 0; j < columns; j++)
            acc += (weight_value_t)*matrix++ * input[j];
        *output++ = q16_to_q8(acc);
    }
    return 1234;
}

static void matrix_product_store_float(
    float *matrix, float *input, float *output,
    unsigned char rows, unsigned char columns)
{
    float acc;
    unsigned char i, j;

    for (i = 0; i < rows; i++) {
        acc = 0.0;
        for (j = 0; j < columns; j++)
            acc += *matrix++ * input[j];
        *output++ = acc;
    }
}

static int check_exact_and_alias(void)
{
    model_value_t matrix[] = {256, 0, 0, 0, 128, 128};
    model_value_t input[] = {100, -40, 60};
    model_value_t output[] = {7, -7, 99};
    model_value_t alias_matrix[] = {128, 0, 256, 0};
    model_value_t alias_input[] = {100, 100};

    matrix_product_store_exact(matrix, input, output, 2, 3);
    if (output[0] != 100 || output[1] != 10 || output[2] != 99)
        return 0;
    matrix_product_store_exact(
        alias_matrix, alias_input, alias_input, 2, 2);
    return alias_input[0] == 50 && alias_input[1] == 50;
}

static int check_fallbacks(void)
{
    model_value_t matrix[] = {256, 0};
    model_value_t input[] = {75, -10};
    model_value_t signed_output[] = {111, 222};
    volatile model_value_t volatile_output[] = {333, 444};
    model_value_t called_output[] = {555, 666};
    model_value_t returning_output[] = {777, 888};
    float float_matrix[] = {1.0, 2.0, 3.0, 4.0};
    float float_input[] = {1.0, 2.0};
    float float_output[] = {-1.0, -1.0};

    matrix_product_store_signed_counts(
        matrix, input, signed_output, (signed char)-1,
        (signed char)2);
    if (signed_output[0] != 111 || signed_output[1] != 222)
        return 0;
    matrix_product_store_signed_counts(
        matrix, input, signed_output, (signed char)1,
        (signed char)2);
    if (signed_output[0] != 75 || signed_output[1] != 222)
        return 0;
    matrix_product_store_volatile_output(
        matrix, input, volatile_output, 1, 2);
    if (volatile_output[0] != 75 || volatile_output[1] != 444)
        return 0;
    matrix_product_store_called(
        matrix, input, called_output, 1, 2);
    if (called_output[0] != 75 || called_output[1] != 666)
        return 0;
    if (matrix_product_store_returning(
            matrix, input, returning_output, 1, 2) != 1234 ||
        returning_output[0] != 75 || returning_output[1] != 888)
        return 0;
    matrix_product_store_float(
        float_matrix, float_input, float_output, 2, 2);
    return float_output[0] == 5.0 && float_output[1] == 11.0;
}

int main(void)
{
    if (!check_exact_and_alias() || !check_fallbacks()) {
        puts("matrix store wave26 failed");
        return 1;
    }
    puts("matrix store wave26 passed");
    return 0;
}
