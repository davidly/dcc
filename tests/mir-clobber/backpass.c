#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define D 16
#define S 8
#define V 10

typedef int16_t model_value_t;
typedef int32_t weight_value_t;

#define MODEL_VALUE_MAX 32767
#define MODEL_VALUE_MIN (-32768)
#define Q16_MODEL_MAX ((weight_value_t)MODEL_VALUE_MAX * 256L)
#define Q16_MODEL_MIN ((weight_value_t)MODEL_VALUE_MIN * 256L)

#define QB 0
#define KB (S * D)
#define VB (2 * S * D)
#define AB (3 * S * D)

static model_value_t token_gradients[V * D];
static model_value_t position_gradients[S * D];
static model_value_t query_weight_gradients[D * D];
static model_value_t key_weight_gradients[D * D];
static model_value_t value_weight_gradients[D * D];
static model_value_t output_weight_gradients[D * V];
static model_value_t logit_gradients[V];
static model_value_t attention_output_gradients[S * D];
static model_value_t attention_score_gradients[S * S];
static model_value_t query_state_gradients[S * D];
static model_value_t key_state_gradients[S * D];
static model_value_t value_state_gradients[S * D];
static model_value_t embedding_gradients[S * D];
static model_value_t gradient_column[D];

static model_value_t query_weights_q8[D * D];
static model_value_t key_weights_q8[D * D];
static model_value_t value_weights_q8[D * D];
static model_value_t output_weights_q8[D * V];
static model_value_t embeddings[S * D];
static model_value_t attention_output[S * D];
#ifdef BACKPASS_VOLATILE_LOGITS
static volatile model_value_t logits[S * V];
#else
static model_value_t logits[S * V];
#endif
static model_value_t attention_workspace[3 * S * D + S * S];
static model_value_t tokens[S];
static model_value_t targets[S];

static model_value_t exponential_table[256] = {
    256,248,240,233,226,219,212,206,199,193,187,182,176,171,165,160,
    155,150,146,141,137,133,129,125,121,117,114,110,107,103,100,97,
    94,91,88,86,83,81,78,76,73,71,69,67,65,63,61,59,
    57,55,54,52,50,49,47,46,44,43,42,41,39,38,37,36,
    35,34,33,32,31,30,29,28,27,26,25,25,24,23,22,22,
    21,20,20,19,19,18,17,17,16,16,15,15,14,14,14,13,
    13,12,12,12,11,11,11,10,10,10,9,9,9,8,8,8,
    8,7,7,7,7,7,6,6,6,6,6,5,5,5,5,5,
    5,5,4,4,4,4,4,4,4,4,3,3,3,3,3,3,
    3,3,3,3,3,2,2,2,2,2,2,2,2,2,2,2,
    2,2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
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

static inline model_value_t multiply_q8(model_value_t left,
                                        model_value_t right)
{
    return q16_to_q8((weight_value_t)left * right);
}

static model_value_t divide_q8(model_value_t numerator,
                               model_value_t denominator)
{
    return clamp_to_model_value(
        ((weight_value_t)numerator * 256L) / denominator);
}

static model_value_t arithmetic_shift_right(model_value_t value, int bits)
{
    int divisor;
    int quotient;

    divisor = 1;
    while (bits-- > 0)
        divisor = divisor << 1;
    quotient = value / divisor;
    if (value < 0 && (value % divisor) != 0)
        quotient = quotient - 1;
    return quotient;
}

static inline void add_clamped(model_value_t *destination,
                               model_value_t value)
{
    *destination = clamp_to_model_value(
        (weight_value_t)*destination + value);
}

static inline model_value_t subtract_clamped(model_value_t left,
                                             model_value_t right)
{
    return clamp_to_model_value((weight_value_t)left - right);
}

static model_value_t vector_maximum(model_value_t *vector,
                                    unsigned char length, int *index)
{
    model_value_t maximum;
    unsigned char maximum_index;
    unsigned char i;

    maximum = *vector;
    maximum_index = 0;
    for (i = 1, ++vector; i < length; ++i, ++vector) {
        if (*vector > maximum) {
            maximum = *vector;
            maximum_index = i;
        }
    }
    *index = maximum_index;
    return maximum;
}

static model_value_t vector_dot_product(model_value_t *left,
                                        model_value_t *right,
                                        unsigned char length)
{
    weight_value_t accumulator;
    unsigned char i;

    accumulator = 0;
    for (i = 0; i < length; ++i)
        accumulator += (weight_value_t)*left++ * *right++;
    return q16_to_q8(accumulator);
}

static void vector_scaled_add(model_value_t scalar, model_value_t *source,
                              model_value_t *destination,
                              unsigned char length)
{
    unsigned char i;

    for (i = 0; i < length; ++i)
        add_clamped(destination++, multiply_q8(scalar, *source++));
}

static void softmax(model_value_t *vector, unsigned char length)
{
    int maximum;
    int difference;
    int table_index;
    int sum;
    int dummy;
    model_value_t *item;
    unsigned char i;

    maximum = vector_maximum(vector, length, &dummy);
    sum = 0;
    for (i = 0, item = vector; i < length; ++i, ++item) {
        difference = maximum - *item;
        if (difference < 0)
            difference = 0;
        table_index = difference >> 3;
        if (table_index > 255)
            table_index = 255;
        *item = exponential_table[table_index];
        sum += *item;
    }
    for (i = 0, item = vector; i < length; ++i, ++item)
        *item = divide_q8(*item, sum);
}

static void matrix_vector_multiply(model_value_t *matrix,
                                   model_value_t *input,
                                   model_value_t *output,
                                   unsigned char rows,
                                   unsigned char columns)
{
    weight_value_t accumulator;
    unsigned char row;
    unsigned char column;

    for (row = 0; row < rows; ++row) {
        accumulator = 0;
        for (column = 0; column < columns; ++column)
            accumulator += (weight_value_t)*matrix++ * input[column];
        *output++ = q16_to_q8(accumulator);
    }
}

static void matrix_vector_add(model_value_t *matrix, model_value_t *input,
                              model_value_t *output,
                              unsigned char rows,
                              unsigned char columns)
{
    weight_value_t accumulator;
    unsigned char row;
    unsigned char column;

    for (row = 0; row < rows; ++row) {
        accumulator = 0;
        for (column = 0; column < columns; ++column)
            accumulator += (weight_value_t)*matrix++ * input[column];
        add_clamped(output++, q16_to_q8(accumulator));
    }
}

static void transposed_matrix_vector_multiply(
    model_value_t *matrix, model_value_t *input, model_value_t *output,
    unsigned char rows, unsigned char columns)
{
    unsigned char row;
    unsigned char column;
    model_value_t scalar;

    memset(output, 0, columns * (int)sizeof(model_value_t));
    for (row = 0; row < rows; ++row) {
        scalar = *input++;
        for (column = 0; column < columns; ++column)
            add_clamped(
                &output[column], multiply_q8(*matrix++, scalar));
    }
}

static void add_outer_product(model_value_t *matrix, model_value_t *left,
                              model_value_t *right,
                              unsigned char rows,
                              unsigned char columns)
{
    model_value_t scalar;
    unsigned char row;
    unsigned char column;

    for (row = 0; row < rows; ++row) {
        scalar = *left++;
        for (column = 0; column < columns; ++column)
            add_clamped(matrix++, multiply_q8(scalar, right[column]));
    }
}

#ifdef BACKPASS_RENAMED
#define BACKPASS_FUNCTION backward_pass_renamed
#else
#define BACKPASS_FUNCTION backward_pass
#endif

static void BACKPASS_FUNCTION(void)
{
    int i, j, k, o, tok, dad, t;

#ifdef BACKPASS_EXTRA_CFG
    if (targets[0] == MODEL_VALUE_MIN)
        return;
#endif
    memset(attention_output_gradients, 0,
           S * D * (int)sizeof(model_value_t));
    for (i = 0; i < S; i++) {
        memcpy(logit_gradients, &logits[i * V],
               V * (int)sizeof(model_value_t));
        softmax(logit_gradients, V);
        logit_gradients[targets[i]] = logit_gradients[targets[i]] - 256;
        for (k = 0; k < V; k++)
            logit_gradients[k] = clamp_to_model_value(
                (weight_value_t)logit_gradients[k] * 128L);
        add_outer_product(output_weight_gradients,
                          &attention_output[i * D],
                          logit_gradients, D, V);
        matrix_vector_multiply(output_weights_q8, logit_gradients,
                               &attention_output_gradients[i * D], D, V);
    }

    memset(value_state_gradients, 0,
           S * D * (int)sizeof(model_value_t));
    for (i = 0; i < S; i++)
        for (j = 0; j < S; j++) {
            attention_score_gradients[i * S + j] =
                vector_dot_product(
                    &attention_workspace[VB + j * D],
                    &attention_output_gradients[i * D], D);
            vector_scaled_add(
                attention_workspace[AB + i * S + j],
                &attention_output_gradients[i * D],
                &value_state_gradients[j * D], D);
        }

    for (i = 0; i < S; i++) {
        dad = vector_dot_product(
            &attention_workspace[AB + i * S],
            &attention_score_gradients[i * S], S);
        for (j = 0; j < S; j++) {
            t = subtract_clamped(
                attention_score_gradients[i * S + j], dad);
            t = multiply_q8(
                attention_workspace[AB + i * S + j], t);
            attention_score_gradients[i * S + j] =
                arithmetic_shift_right(t, 2);
        }
    }

    for (i = 0; i < S; i++)
        transposed_matrix_vector_multiply(
            &attention_workspace[KB],
            &attention_score_gradients[i * S],
            &query_state_gradients[i * D], S, D);
    for (j = 0; j < S; j++) {
        for (i = 0; i < S; i++)
            gradient_column[i] =
                attention_score_gradients[i * S + j];
        transposed_matrix_vector_multiply(
            &attention_workspace[QB], gradient_column,
            &key_state_gradients[j * D], S, D);
    }

    memcpy(embedding_gradients, attention_output_gradients,
           S * D * (int)sizeof(model_value_t));
    for (i = 0; i < S; i++) {
        o = i * D;
        matrix_vector_add(
            query_weights_q8, &query_state_gradients[o],
            &embedding_gradients[o], D, D);
        add_outer_product(
            query_weight_gradients, &embeddings[o],
            &query_state_gradients[o], D, D);
        matrix_vector_add(
            key_weights_q8, &key_state_gradients[o],
            &embedding_gradients[o], D, D);
        add_outer_product(
            key_weight_gradients, &embeddings[o],
            &key_state_gradients[o], D, D);
        matrix_vector_add(
            value_weights_q8, &value_state_gradients[o],
            &embedding_gradients[o], D, D);
        add_outer_product(
            value_weight_gradients, &embeddings[o],
            &value_state_gradients[o], D, D);
    }

    for (i = 0; i < S; i++) {
        o = i * D;
        tok = tokens[i];
        for (k = 0; k < D; k++) {
            add_clamped(
                &token_gradients[tok * D + k],
                embedding_gradients[o + k]);
            add_clamped(
                &position_gradients[i * D + k],
                embedding_gradients[o + k]);
        }
    }
}

static void fill_values(model_value_t *values, int count,
                        int multiplier, int bias, int modulus)
{
    int i;

    for (i = 0; i < count; ++i)
        values[i] = (model_value_t)(
            ((i * multiplier + bias) % modulus) - modulus / 2);
}

static unsigned long checksum_values(
    unsigned long checksum, model_value_t *values, int count)
{
    int i;

    for (i = 0; i < count; ++i)
        checksum = checksum * 33UL +
            (unsigned long)(unsigned int)values[i];
    return checksum;
}

static void initialize_fixture(void)
{
    int i;

    memset(token_gradients, 0, sizeof(token_gradients));
    memset(position_gradients, 0, sizeof(position_gradients));
    memset(query_weight_gradients, 0, sizeof(query_weight_gradients));
    memset(key_weight_gradients, 0, sizeof(key_weight_gradients));
    memset(value_weight_gradients, 0, sizeof(value_weight_gradients));
    memset(output_weight_gradients, 0, sizeof(output_weight_gradients));
    fill_values(query_weights_q8, D * D, 17, 5, 31);
    fill_values(key_weights_q8, D * D, 13, 7, 29);
    fill_values(value_weights_q8, D * D, 11, 3, 27);
    fill_values(output_weights_q8, D * V, 19, 9, 33);
    fill_values(embeddings, S * D, 7, 4, 25);
    fill_values(attention_output, S * D, 5, 2, 23);
    fill_values(logits, S * V, 29, 11, 97);
    fill_values(attention_workspace, 3 * S * D, 23, 6, 35);
    for (i = 0; i < S * S; ++i)
        attention_workspace[AB + i] =
            (model_value_t)(16 + (i * 17 + 3) % 49);
    for (i = 0; i < S; ++i) {
        tokens[i] = (model_value_t)((i * 3 + 1) % V);
        targets[i] = (model_value_t)((i * 7 + 2) % V);
    }
}

int main(void)
{
    unsigned long checksum;
    int failures;

    initialize_fixture();
    BACKPASS_FUNCTION();
    checksum = 5381UL;
    checksum = checksum_values(
        checksum, attention_output_gradients, S * D);
    checksum = checksum_values(
        checksum, attention_score_gradients, S * S);
    checksum = checksum_values(
        checksum, query_state_gradients, S * D);
    checksum = checksum_values(
        checksum, key_state_gradients, S * D);
    checksum = checksum_values(
        checksum, value_state_gradients, S * D);
    checksum = checksum_values(
        checksum, embedding_gradients, S * D);
    checksum = checksum_values(
        checksum, output_weight_gradients, D * V);
    checksum = checksum_values(
        checksum, query_weight_gradients, D * D);
    checksum = checksum_values(
        checksum, key_weight_gradients, D * D);
    checksum = checksum_values(
        checksum, value_weight_gradients, D * D);
    checksum = checksum_values(
        checksum, token_gradients, V * D);
    checksum = checksum_values(
        checksum, position_gradients, S * D);
    failures = checksum != 276938403UL;
    printf(
        "backward pass checks=1 failures=%d checksum=%lu\n",
        failures, checksum);
    return failures != 0;
}
