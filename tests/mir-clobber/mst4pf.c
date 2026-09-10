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

static void matrix_product_store(model_value_t *matrix,
                                 model_value_t *input,
                                 model_value_t *output,
                                 unsigned char rows,
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

static void print_vector(const char *name, int rows, int columns,
                         struct guarded_vector *vector, int count)
{
    int i;

    printf("%s %dx%d", name, rows, columns);
    for (i = 0; i < count; ++i)
        printf(" %d", vector->values[i]);
    printf(" bounds=%d,%d\n", vector->before, vector->after);
}

int main(void)
{
    model_value_t dimensions_matrix[] = {
        256, 0, 0, 0, 128, 128
    };
    model_value_t dimensions_input[] = {100, -40, 60};
    struct guarded_vector dimensions = {
        1111, {7, -3, 99, -99}, 2222
    };
    model_value_t rounding_matrix[] = {-300, 300, -255, 255};
    model_value_t rounding_input[] = {1};
    struct guarded_vector rounding = {
        3333, {10, 10, 10, 10}, 4444
    };
    model_value_t saturation_matrix[] = {
        32767, -32768, 256, -256
    };
    model_value_t saturation_input[] = {32767};
    struct guarded_vector saturation = {
        5555, {1, -1, 1, -1}, 6666
    };
    struct guarded_vector zero_columns = {
        7777, {1234, -2345, 77, -77}, 8888
    };
    struct guarded_vector zero_rows = {
        9999, {1234, -2345, 88, -88}, -9999
    };

    matrix_product_store(dimensions_matrix, dimensions_input,
                         dimensions.values, 2, 3);
    matrix_product_store(rounding_matrix, rounding_input,
                         rounding.values, 4, 1);
    matrix_product_store(saturation_matrix, saturation_input,
                         saturation.values, 4, 1);
    matrix_product_store(dimensions_matrix, dimensions_input,
                         zero_columns.values, 2, 0);
    matrix_product_store(dimensions_matrix, dimensions_input,
                         zero_rows.values, 0, 3);

    print_vector("dimensions", 2, 3, &dimensions, 4);
    print_vector("rounding", 4, 1, &rounding, 4);
    print_vector("saturation", 4, 1, &saturation, 4);
    print_vector("zero-columns", 2, 0, &zero_columns, 4);
    print_vector("zero-rows", 0, 3, &zero_rows, 4);
    return 0;
}
