#include <stdio.h>

#define DIMENSION 16
#define SEQUENCE 8
#define VOCABULARY 5

typedef int model_value_t;
typedef long weight_value_t;

static model_value_t token_weights[VOCABULARY * DIMENSION];
static model_value_t position_weights[SEQUENCE * DIMENSION];
static model_value_t embeddings[SEQUENCE * DIMENSION];
#ifdef FXEMBD_VOLATILE_TOKENS
static volatile model_value_t tokens[SEQUENCE];
#else
static model_value_t tokens[SEQUENCE];
#endif

#ifdef FXEMBD_VARIADIC_CLAMP
static model_value_t clamp_embedding(weight_value_t value, ...)
#else
static model_value_t clamp_embedding(weight_value_t value)
#endif
{
    if (value > 32767L)
        return 32767;
    if (value < -32768L)
        return -32768;
    return (model_value_t)value;
}

static void fixed_embedding_build(void)
{
    int token;
    model_value_t *destination, *position, *source;
    unsigned char i, j;

    destination = embeddings;
    position = position_weights;
    for (i = 0; i < SEQUENCE; i++) {
        token = tokens[i];
        source = &token_weights[token * DIMENSION];
        for (j = 0; j < DIMENSION; j++)
            *destination++ = clamp_embedding(
                (weight_value_t)*source++ + *position++);
    }
}

static model_value_t expected_embedding(int row, int column)
{
    long value;

    value = (long)token_weights[tokens[row] * DIMENSION + column];
    value += position_weights[row * DIMENSION + column];
    if (value > 32767L)
        return 32767;
    if (value < -32768L)
        return -32768;
    return (model_value_t)value;
}

int main(void)
{
    long checksum;
    int failures, lower_clamps, upper_clamps;
    int i, j;

    for (i = 0; i < VOCABULARY; ++i)
        for (j = 0; j < DIMENSION; ++j)
            token_weights[i * DIMENSION + j] =
                (model_value_t)(i * 14500 + j * 100 - 29000);
    for (i = 0; i < SEQUENCE; ++i) {
        tokens[i] = (model_value_t)((i * 3 + 1) % VOCABULARY);
        for (j = 0; j < DIMENSION; ++j)
            position_weights[i * DIMENSION + j] =
                (model_value_t)(i * 3500 - j * 150 - 15500);
    }

    fixed_embedding_build();
    failures = 0;
    lower_clamps = 0;
    upper_clamps = 0;
    checksum = 0;
    for (i = 0; i < SEQUENCE; ++i)
        for (j = 0; j < DIMENSION; ++j) {
            model_value_t expected;
            model_value_t actual;

            expected = expected_embedding(i, j);
            actual = embeddings[i * DIMENSION + j];
            if (actual != expected)
                ++failures;
            if (expected == 32767)
                ++upper_clamps;
            if (expected == -32768)
                ++lower_clamps;
            checksum = checksum * 17L + actual;
        }

    if (upper_clamps != 16 || lower_clamps != 16)
        ++failures;
    printf("fixed embedding failures=%d clamps=%d/%d checksum=%ld\n",
           failures, lower_clamps, upper_clamps, checksum);
    return failures != 0;
}
