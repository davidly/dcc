#include <stdio.h>
#include <string.h>

typedef int model_value_t;
typedef long weight_value_t;

#define MODEL_VALUE_MAX 32767
#define MODEL_VALUE_MIN (-32768)
#define Q16_MODEL_MAX ((weight_value_t)MODEL_VALUE_MAX * 256L)
#define Q16_MODEL_MIN ((weight_value_t)MODEL_VALUE_MIN * 256L)

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

static inline model_value_t multiply_q8(model_value_t left,
                                         model_value_t right)
{
    return q16_to_q8((weight_value_t)left * right);
}

static inline void add_clamped(model_value_t *destination,
                               model_value_t value)
{
    *destination = clamp_to_model_value(
        (weight_value_t)*destination + value);
}

static void matrix_product_transposed(
    model_value_t *matrix, model_value_t *input,
    model_value_t *output,
    unsigned char rows, unsigned char columns)
{
    unsigned char i, j;
    model_value_t scalar;

    memset(output, 0, columns * (int)sizeof(model_value_t));
    for (i = 0; i < rows; i++) {
        scalar = *input++;
#ifdef MATKIND_TRANSPOSED_EXTRA_ARITHMETIC
        scalar += 0;
#endif
        for (j = 0; j < columns; j++)
            add_clamped(&output[j], multiply_q8(*matrix++, scalar));
    }
}

static void matrix_product_outer(
    model_value_t *matrix, model_value_t *left, model_value_t *right,
    unsigned char rows, unsigned char columns)
{
    model_value_t scalar;
    unsigned char i, j;

    for (i = 0; i < rows; i++) {
        scalar = *left++;
#ifdef MATKIND_OUTER_EXTRA_ARITHMETIC
        scalar += 0;
#endif
        for (j = 0; j < columns; j++)
            add_clamped(matrix++, multiply_q8(scalar, right[j]));
    }
}

static int check_transposed(void)
{
    model_value_t matrix[12] = {
        256, -512, 1024, 30000,
        -384, 640, -896, -30000,
        32767, 32767, -32768, 16384
    };
    model_value_t input[3] = {128, -192, 32767};
    model_value_t actual[4] = {111, 222, 333, 444};
    static const model_value_t expected[4] = {
        32767, 32031, -31584, 32767
    };
    int j;
    int failures;

    matrix_product_transposed(matrix, input, actual, 3, 4);
    failures = 0;
    for (j = 0; j < 4; ++j)
        if (actual[j] != expected[j])
            ++failures;
    printf("matrix kind transposed failures=%d values=%d,%d,%d,%d\n",
           failures, actual[0], actual[1], actual[2], actual[3]);
    return failures;
}

static int check_outer(void)
{
    model_value_t actual[12] = {
        30000, -30000, 100, -100,
        -20000, 20000, 1234, -1234,
        32760, -32760, 16000, -16000
    };
    static const model_value_t expected[12] = {
        30512, -30640, 868, -32768,
        -20768, 20960, 82, 31533,
        32767, -32768, 32767, -32768
    };
    model_value_t left[3] = {256, -384, 32767};
    model_value_t right[4] = {512, -640, 768, -32768};
    int i;
    int failures;

    matrix_product_outer(actual, left, right, 3, 4);
    failures = 0;
    for (i = 0; i < 12; ++i)
        if (actual[i] != expected[i])
            ++failures;
    printf("matrix kind outer failures=%d edges=%d,%d,%d,%d\n",
           failures, actual[0], actual[3], actual[8], actual[11]);
    return failures;
}

int main(void)
{
    int failures;

    failures = check_transposed();
    failures += check_outer();
    printf("matrix product kind failures=%d\n", failures);
    return failures != 0;
}
