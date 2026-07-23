#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

/*
 * ATTNC11 - "Paper Tape is All You Need"
 *
 * A 1-layer, 1-head transformer trained to reverse an 8-digit sequence.
 *
 * This dcc C11 implementation for CP/M 2.2 / Z80 is based on the PDP-11
 * 2.11BSD assembly attn.s (davepl/pdpsrc) and the NN11 library by
 * Damien Boureille (dbrll/ATTN-11).
 *
 * Implementation notes:
 *
 *   1. dcc maps `int32_t` to its native 32-bit `long`, so fixed-point
 *      helpers use ordinary arithmetic in the hot vector and matrix loops.
 *
 *   2. Q16 weights use `int32_t[]`.  dcc stores `int32_t` little-endian
 *      (Z80-native), so ATTN.WTS is little-endian.  The companion inference
 *      port ATTNZ80.MAC reads the same format.
 *
 *   3. Lookup tables use aggregate initializers.
 *
 *   4. POSIX byte I/O uses open/read/write from <fcntl.h>/<unistd.h>.
 *
 * Fixed-point formats:
 *   Q8  forward activations   (int16_t, value = real * 256)
 *   Q15 backward gradients    (int16_t)
 *   Q16 weight accumulators   (int32_t, value = real * 65536)
 *
 * Build (from the dcc repo, with DCC/DCCPEEP/DCCRTLSTRIP exported):
 *   dccmake                     -> ATTNC11.COM
 *
 * Usage:
 *   attnc11 -t      train, save weights to ATTN.WTS, then test
 *   attnc11         run inference using the saved weights (default)
 *   attnc11 <file>  run inference on <file> instead of ATTN.IN
 *   attnc11 -h      help
 */

#define D 16            /* d_model            */
#define S 8             /* sequence length    */
#define V 10            /* vocab (digits 0-9) */

typedef int16_t model_value_t;
typedef int32_t weight_value_t;

#define MODEL_VALUE_MAX 32767
#define MODEL_VALUE_MIN (-32768)
#define Q16_MODEL_MAX ((weight_value_t)MODEL_VALUE_MAX * 256L)
#define Q16_MODEL_MIN ((weight_value_t)MODEL_VALUE_MIN * 256L)

#define NPARAM (V*D + S*D + D*D + D*D + D*D + D*V)
#define WBYTES (NPARAM * (int)sizeof(weight_value_t))

#define NSTEP 700       /* max training steps  */
#define RPRT 50         /* report interval     */
#define FSTEP 10        /* mix in IFILE every n steps */

#define ERROR (-1)

#define FM_TRAIN 1
#define FM_VALID 2
#define FM_INFER 3

/* Trained weights are saved here so inference can reload them. */
#define WFILE "ATTN.WTS"

/* Default inference input file: one S-digit sequence per line. */
#define IFILE "ATTN.IN"

/* WORK layout: Q | K | V | A(scores), each S*D except A is S*S */
#define QB 0
#define KB (S*D)
#define VB (2*S*D)
#define AB (3*S*D)

_Static_assert(sizeof(model_value_t) == 2, "model values must be 16-bit");
_Static_assert(sizeof(weight_value_t) == 4, "weights must be 32-bit");
_Static_assert(D == 16, "ATTNC11 assumes d_model is 16");
_Static_assert(S == 8, "ATTNC11 assumes sequence length is 8");
_Static_assert(V == 10, "ATTNC11 assumes 10 digit tokens");
_Static_assert(AB == 3*S*D, "ATTNC11 workspace offsets changed");
_Static_assert(AB + S*S == 3*S*D + S*S, "ATTNC11 workspace size changed");
_Static_assert(WBYTES == 4864, "ATTNC11 weight file payload changed");

/* --- Q16 weight accumulators (native little-endian int32_t) --- */
static weight_value_t token_weights_q16[V*D];
static weight_value_t position_weights_q16[S*D];
static weight_value_t query_weights_q16[D*D];
static weight_value_t key_weights_q16[D*D];
static weight_value_t value_weights_q16[D*D];
static weight_value_t output_weights_q16[D*V];

/* --- Q8 weight copies (rebuilt from the Q16 accumulators) --- */
static model_value_t token_weights_q8[V*D];
static model_value_t position_weights_q8[S*D];
static model_value_t query_weights_q8[D*D];
static model_value_t key_weights_q8[D*D];
static model_value_t value_weights_q8[D*D];
static model_value_t output_weights_q8[D*V];

/* --- Q15 gradient accumulators --- */
static model_value_t token_gradients[V*D];
static model_value_t position_gradients[S*D];
static model_value_t query_weight_gradients[D*D];
static model_value_t key_weight_gradients[D*D];
static model_value_t value_weight_gradients[D*D];
static model_value_t output_weight_gradients[D*V];

/* --- forward state --- */
static model_value_t embeddings[S*D];
static model_value_t attention_output[S*D];
static model_value_t logits[S*V];
static model_value_t attention_workspace[3*S*D + S*S];

/* --- backward workspace --- */
static model_value_t logit_gradients[V];
static model_value_t attention_output_gradients[S*D];
static model_value_t attention_score_gradients[S*S];
static model_value_t query_state_gradients[S*D];
static model_value_t key_state_gradients[S*D];
static model_value_t value_state_gradients[S*D];
static model_value_t embedding_gradients[S*D];
static model_value_t gradient_column[D];

/* --- training data / state --- */
static model_value_t tokens[S];
static model_value_t targets[S];
static model_value_t test_predictions[S];
static uint16_t random_seed;
static int training_hits;
static int training_total;
static int training_step;
static int file_hits;
static int validation_hits;

/* --- lookup tables --- */

/* exp(-i/32) in Q8 */
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

/* -ln(x/256)*4096 in Q12.  Indexed by a Q8 probability p only under the
 * `if (probability < 256)` guard in cross_entropy_loss(), so index 256
 * (probability == 1.0) is never read;
 * the table is 256 entries (dcc caps an initializer list at 256 elements). */
static model_value_t logarithm_table[256] = {
    22713,22713,19874,18213,17035,16121,15374,14743,14196,13713,
    13282,12891,12535,12207,11903,11621,11357,11108,10874,10653,
    10443,10243,10052,9870,9696,9529,9368,9213,9064,8921,
    8782,8647,8517,8391,8269,8150,8035,7923,7813,7707,
    7603,7502,7404,7307,7213,7121,7031,6943,6857,6772,
    6689,6608,6529,6451,6374,6299,6225,6153,6081,6011,
    5943,5875,5808,5743,5678,5615,5552,5491,5430,5370,
    5311,5253,5196,5139,5084,5029,4974,4921,4868,4816,
    4764,4713,4663,4613,4564,4516,4468,4421,4374,4328,
    4282,4237,4192,4148,4104,4060,4017,3975,3933,3891,
    3850,3810,3769,3729,3690,3650,3612,3573,3535,3497,
    3460,3423,3386,3350,3314,3278,3242,3207,3172,3138,
    3103,3069,3036,3002,2969,2936,2904,2871,2839,2807,
    2776,2744,2713,2682,2651,2621,2591,2561,2531,2501,
    2472,2443,2414,2385,2357,2328,2300,2272,2244,2217,
    2189,2162,2135,2108,2082,2055,2029,2003,1977,1951,
    1925,1900,1874,1849,1824,1799,1774,1750,1725,1701,
    1677,1653,1629,1605,1582,1558,1535,1512,1488,1466,
    1443,1420,1397,1375,1353,1330,1308,1286,1265,1243,
    1221,1200,1178,1157,1136,1115,1094,1073,1052,1032,
    1011,991,970,950,930,910,890,870,850,831,
    811,792,772,753,734,715,696,677,658,639,
    621,602,584,565,547,529,511,492,474,457,
    439,421,403,386,368,351,333,316,299,281,
    264,247,230,213,197,180,163,147,130,114,
    97,81,65,48,32,16
};

/* --- forward declarations --- */
static inline model_value_t clamp_to_model_value(weight_value_t value);
static inline model_value_t q16_to_q8(weight_value_t value);
static inline model_value_t multiply_q8(model_value_t left,
                                         model_value_t right);
static inline model_value_t divide_q8(model_value_t numerator,
                                       model_value_t denominator);
static model_value_t arithmetic_shift_right(model_value_t value, int bits);
static inline void add_clamped(model_value_t *destination,
                               model_value_t value);
static inline model_value_t subtract_clamped(model_value_t left,
                                              model_value_t right);
static model_value_t vector_maximum(model_value_t *vector,
                                    unsigned char length, int *index);
static model_value_t vector_dot_product(model_value_t *left,
                                         model_value_t *right,
                                         unsigned char length);
static model_value_t attention_score_16(model_value_t *query,
                                         model_value_t *key);
static void vector_scaled_add(model_value_t scalar, model_value_t *source,
                              model_value_t *destination,
                              unsigned char length);
static void softmax(model_value_t *vector, unsigned char length);
static void softmax_8(model_value_t *vector);
static void matrix_vector_multiply(model_value_t *matrix,
                                   model_value_t *input, model_value_t *output,
                                   unsigned char rows, unsigned char columns);
static void matrix_vector_add(model_value_t *matrix, model_value_t *input,
                              model_value_t *output,
                              unsigned char rows, unsigned char columns);
static void transposed_matrix_vector_multiply(
    model_value_t *matrix, model_value_t *input, model_value_t *output,
    unsigned char rows, unsigned char columns);
static void project_all_qkv(void);
static void transposed_multiply_8x16(model_value_t *matrix,
                                     model_value_t *input,
                                     model_value_t *output);
static void transposed_multiply_16x10(model_value_t *matrix,
                                      model_value_t *input,
                                      model_value_t *output);
static void add_outer_product(model_value_t *matrix, model_value_t *left,
                              model_value_t *right,
                              unsigned char rows, unsigned char columns);
static void build_embeddings(void);
static void forward_attention(void);
static void project_logits(void);
static void forward_pass(void);
static void convert_weight_group(weight_value_t *weights,
                                 model_value_t *quantized, int count);
static void convert_weights_to_q8(void);
static int random_number(void);
static void initialize_weight_group(weight_value_t *weights, int count);
static void initialize_weights(void);
static void update_weight_group(weight_value_t *weights,
                                model_value_t *gradients, int count, int shift);
static void update_weights(void);
static void zero_gradients(void);
static void backward_pass(void);
static void make_targets(void);
static void generate_sample(void);
static void train_sequence(void);
static bool check_sequence(void);
static int process_sequence(int mode);
static int process_sequence_file(char *filename, int mode);
static int run_training_file(char *filename, bool train);
static void count_predictions(void);
static int cross_entropy_loss(void);
static inline int loss_fraction(int loss);
static void report_training(void);
static void test_random_samples(void);
static bool infer_sequence(void);
static int run_inference_file(char *filename);
static int transfer_weight_group(int file, weight_value_t *weights, int count,
                                 bool write_data);
static int transfer_weight_file(int file, bool write_data);
static int save_weights(void);
static int load_weights(void);
static uint16_t elapsed_seconds(void);

/* ============================================================ */
/* 32-bit fixed-point helpers (int32_t)                         */
/* ============================================================ */

/* clamp a 32-bit value to a signed 16-bit model value */
static inline model_value_t clamp_to_model_value(weight_value_t value)
{
    if (value > MODEL_VALUE_MAX)
        return MODEL_VALUE_MAX;
    if (value < MODEL_VALUE_MIN)
        return MODEL_VALUE_MIN;
    return (model_value_t)value;
}

/* a (Q16 int32_t) >> 8 -> clamped Q8 int16_t.
 * Truncates toward zero to match the ATTNZ80.MAC LQ8 helper, so it divides
 * the magnitude and re-applies the sign rather than using an arithmetic
 * shift (which would floor for negatives). */
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

/* (a * b) >> 8 -> clamped Q8 int */
static inline model_value_t multiply_q8(model_value_t left,
                                         model_value_t right)
{
    return q16_to_q8((weight_value_t)left * right);
}

/* Q8 divide: (a << 8) / b -> clamped Q8 int (a >= 0, b > 0 at all call sites) */
static inline model_value_t divide_q8(model_value_t numerator,
                                       model_value_t denominator)
{
    return clamp_to_model_value(
        ((weight_value_t)numerator * 256L) / denominator);
}

/* arithmetic shift right by n (floor toward -inf), n small */
static model_value_t arithmetic_shift_right(model_value_t value, int bits)
{
    int d, q;

    d = 1;
    while (bits-- > 0)
        d = d << 1;
    q = value / d;
    if (value < 0 && (value % d) != 0)
        q = q - 1;
    return q;
}

/* *dst += v, saturating to signed 16-bit */
static inline void add_clamped(model_value_t *destination,
                               model_value_t value)
{
    *destination = clamp_to_model_value((weight_value_t)*destination + value);
}

/* a - b, saturating to signed 16-bit */
static inline model_value_t subtract_clamped(model_value_t left,
                                              model_value_t right)
{
    return clamp_to_model_value((weight_value_t)left - right);
}

/* ============================================================ */
/* Vector primitives                                            */
/* ============================================================ */

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

static model_value_t vector_dot_product(model_value_t *left,
                                         model_value_t *right,
                                         unsigned char length)
{
    weight_value_t acc;
    unsigned char i;

    acc = 0;
    for (i = 0; i < length; i++)
        acc += (weight_value_t)*left++ * *right++;
    return q16_to_q8(acc);
}

/* Fixed D-element dot product scaled by sqrt(D) = 4. */
static model_value_t attention_score_16(model_value_t *query,
                                         model_value_t *key)
{
    weight_value_t acc;
    model_value_t value;
    unsigned char i;

    acc = 0;
    for (i = 0; i < D; i++)
        acc += (weight_value_t)*query++ * *key++;
    value = q16_to_q8(acc);
    if (value < 0 && (value % 4) != 0)
        return value / 4 - 1;
    return value / 4;
}

/* dst[k] += (scalar * src[k]) >> 8, saturating */
static void vector_scaled_add(model_value_t scalar, model_value_t *source,
                              model_value_t *destination,
                              unsigned char length)
{
    unsigned char k;

    for (k = 0; k < length; k++)
        add_clamped(destination++, multiply_q8(scalar, *source++));
}

/* softmax in place (Q8), LUT-based */
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

static void softmax_8(model_value_t *vector)
{
    int mx, d, idx, sum;
    model_value_t *item;
    unsigned char i;

    mx = vector[0];
    for (i = 1; i < S; i++)
        if (vector[i] > mx)
            mx = vector[i];
    sum = 0;
    for (i = 0, item = vector; i < S; i++, item++) {
        d = mx - *item;
        if (d < 0)
            d = 0;
        idx = d >> 3;
        if (idx > 255)
            idx = 255;
        *item = exponential_table[idx];
        sum += *item;
    }
    for (i = 0, item = vector; i < S; i++, item++)
        *item = divide_q8(*item, sum);
}

/* ============================================================ */
/* Matrix primitives                                            */
/* ============================================================ */

/* vout[i] = sum_j mat[i][j] * vin[j]  (Q16 accum, >>8, clamp) */
static void matrix_vector_multiply(model_value_t *matrix,
                                   model_value_t *input, model_value_t *output,
                                   unsigned char rows, unsigned char columns)
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

/* vout[i] += sum_j mat[i][j] * vin[j]  (saturating add) */
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

/* vout[j] = sum_i (mat[i][j] * vin[i]) >> 8   (per-product Q8) */
static void transposed_matrix_vector_multiply(
    model_value_t *matrix, model_value_t *input, model_value_t *output,
    unsigned char rows, unsigned char columns)
{
    unsigned char i, j;
    model_value_t sc;

    memset(output, 0, columns * (int)sizeof(model_value_t));
    for (i = 0; i < rows; i++) {
        sc = *input++;
        for (j = 0; j < columns; j++)
            add_clamped(&output[j], multiply_q8(*matrix++, sc));
    }
}

/* Fused Q/K/V projections for all rows: each input is loaded once. */
static void project_all_qkv(void)
{
    unsigned char row, i, j;
    model_value_t sc;
    model_value_t *vin, *voutq, *voutk, *voutv;
    model_value_t *matq, *matk, *matv;

    memset(&attention_workspace[QB], 0,
           3 * S * D * (int)sizeof(model_value_t));
    vin = embeddings;
    voutq = &attention_workspace[QB];
    voutk = &attention_workspace[KB];
    voutv = &attention_workspace[VB];
    for (row = 0; row < S; row++) {
        matq = query_weights_q8;
        matk = key_weights_q8;
        matv = value_weights_q8;
        for (i = 0; i < D; i++) {
            sc = *vin++;
            for (j = 0; j < D; j++) {
                add_clamped(&voutq[j], multiply_q8(*matq++, sc));
                add_clamped(&voutk[j], multiply_q8(*matk++, sc));
                add_clamped(&voutv[j], multiply_q8(*matv++, sc));
            }
        }
        voutq += D;
        voutk += D;
        voutv += D;
    }
}

static void transposed_multiply_8x16(model_value_t *matrix,
                                     model_value_t *input,
                                     model_value_t *output)
{
    unsigned char i, j;
    model_value_t sc;

    memset(output, 0, D * (int)sizeof(model_value_t));
    for (i = 0; i < S; i++) {
        sc = *input++;
        for (j = 0; j < D; j++)
            add_clamped(&output[j], multiply_q8(*matrix++, sc));
    }
}

static void transposed_multiply_16x10(model_value_t *matrix,
                                      model_value_t *input,
                                      model_value_t *output)
{
    unsigned char i, j;
    model_value_t sc;

    memset(output, 0, V * (int)sizeof(model_value_t));
    for (i = 0; i < D; i++) {
        sc = *input++;
        for (j = 0; j < V; j++)
            add_clamped(&output[j], multiply_q8(*matrix++, sc));
    }
}

/* mat[i][j] += (vx[i] * vy[j]) >> 8   (saturating) */
static void add_outer_product(model_value_t *matrix, model_value_t *left,
                              model_value_t *right,
                              unsigned char rows, unsigned char columns)
{
    model_value_t sc;
    unsigned char i, j;

    for (i = 0; i < rows; i++) {
        sc = *left++;
        for (j = 0; j < columns; j++)
            add_clamped(matrix++, multiply_q8(sc, right[j]));
    }
}

/* ============================================================ */
/* Layer operations                                             */
/* ============================================================ */

/* X[i] = tok_emb[tokens[i]] + pos_emb[i] */
static void build_embeddings(void)
{
    int tok;
    model_value_t *dst, *pos, *src;
    unsigned char i, j;

    dst = embeddings;
    pos = position_weights_q8;
    for (i = 0; i < S; i++) {
        tok = tokens[i];
        src = &token_weights_q8[tok * D];
        for (j = 0; j < D; j++)
            *dst++ = clamp_to_model_value(
                (weight_value_t)*src++ + *pos++);
    }
}

/* self-attention forward pass */
static void forward_attention(void)
{
    model_value_t *key, *query, *score;
    unsigned char i, j;

    /* Step 1-3: Q = X.Wq, K = X.Wk, V = X.Wv */
    project_all_qkv();
    /* Step 4: S[i][j] = (Q[i] . K[j]) / sqrt(d), sqrt(16)=4 -> >>2 */
    query = &attention_workspace[QB];
    score = &attention_workspace[AB];
    for (i = 0; i < S; i++) {
        key = &attention_workspace[KB];
        for (j = 0; j < S; j++) {
            *score++ = attention_score_16(query, key);
            key += D;
        }
        /* Step 5: softmax this completed score row. */
        softmax_8(score - S);
        query += D;
    }
    /* Step 6: Y[i] = V^T . A[i] */
    for (i = 0; i < S; i++)
        transposed_multiply_8x16(&attention_workspace[VB],
                      &attention_workspace[AB + i * S],
                      &attention_output[i * D]);
    /* Step 7: residual Y += X */
    for (i = 0; i < S * D; i++)
        add_clamped(&attention_output[i], embeddings[i]);
}

/* logits[i] = Wout^T . Y[i] */
static void project_logits(void)
{
    unsigned char i;

    for (i = 0; i < S; i++)
        transposed_multiply_16x10(output_weights_q8,
                       &attention_output[i * D], &logits[i * V]);
}

static void forward_pass(void)
{
    build_embeddings();
    forward_attention();
    project_logits();
}

/* ============================================================ */
/* Weight conversion / init / update                            */
/* ============================================================ */

/* convert one Q16 weight group to its Q8 copy */
static void convert_weight_group(weight_value_t *weights,
                                 model_value_t *quantized, int count)
{
    int i;

    for (i = 0; i < count; i++)
        quantized[i] = q16_to_q8(weights[i]);
}

static void convert_weights_to_q8(void)
{
    convert_weight_group(token_weights_q16, token_weights_q8, V * D);
    convert_weight_group(position_weights_q16, position_weights_q8, S * D);
    convert_weight_group(query_weights_q16, query_weights_q8, D * D);
    convert_weight_group(key_weights_q16, key_weights_q8, D * D);
    convert_weight_group(value_weights_q16, value_weights_q8, D * D);
    convert_weight_group(output_weights_q16, output_weights_q8, D * V);
}

/* 15-bit LCG */
static int random_number(void)
{
    random_seed = (random_seed * 25173 + 13849) & 0x7FFF;
    return random_seed;
}

/* fill n Q16 weights with random Q8 in [-128,127] */
static void initialize_weight_group(weight_value_t *weights, int count)
{
    int i, r;

    for (i = 0; i < count; i++) {
        r = (random_number() & 0x00FF) - 128;
        weights[i] = (weight_value_t)r * 256L;
    }
}

static void initialize_weights(void)
{
    initialize_weight_group(token_weights_q16, V * D);
    initialize_weight_group(position_weights_q16, S * D);
    initialize_weight_group(query_weights_q16, D * D);
    initialize_weight_group(key_weights_q16, D * D);
    initialize_weight_group(value_weights_q16, D * D);
    initialize_weight_group(output_weights_q16, D * V);
}

/* w_q16 -= grad_q15 >> (shift-1); zero grad after read */
static void update_weight_group(weight_value_t *weights,
                                model_value_t *gradients, int count, int shift)
{
    int i, delta;

    for (i = 0; i < count; i++) {
        delta = arithmetic_shift_right(gradients[i], shift - 1);
        gradients[i] = 0;
        weights[i] -= (weight_value_t)delta;
    }
}

static void update_weights(void)
{
    update_weight_group(token_weights_q16, token_gradients, V * D, 4);
    update_weight_group(position_weights_q16, position_gradients, S * D, 4);
    update_weight_group(query_weights_q16, query_weight_gradients, D * D, 1);
    update_weight_group(key_weights_q16, key_weight_gradients, D * D, 1);
    update_weight_group(value_weights_q16, value_weight_gradients, D * D, 1);
    update_weight_group(output_weights_q16, output_weight_gradients, D * V, 6);
}

static void zero_gradients(void)
{
    memset(token_gradients, 0, V * D * (int)sizeof(model_value_t));
    memset(position_gradients, 0, S * D * (int)sizeof(model_value_t));
    memset(query_weight_gradients, 0,
        D * D * (int)sizeof(model_value_t));
    memset(key_weight_gradients, 0, D * D * (int)sizeof(model_value_t));
    memset(value_weight_gradients, 0,
        D * D * (int)sizeof(model_value_t));
    memset(output_weight_gradients, 0,
        D * V * (int)sizeof(model_value_t));
}

/* ============================================================ */
/* Backward pass                                                */
/* ============================================================ */

static void backward_pass(void)
{
    int i, j, k, o, tok, dad, t;

    /* Step 1: dLogits, dWout, dY */
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
                      &attention_output[i * D], logit_gradients, D, V);
          matrix_vector_multiply(output_weights_q8, logit_gradients,
                         &attention_output_gradients[i * D], D, V);
    }

    /* Step 2: dA, dV */
        memset(value_state_gradients, 0,
            S * D * (int)sizeof(model_value_t));
    for (i = 0; i < S; i++)
        for (j = 0; j < S; j++) {
            attention_score_gradients[i * S + j] =
                    vector_dot_product(&attention_workspace[VB + j * D],
                                 &attention_output_gradients[i * D], D);
                vector_scaled_add(attention_workspace[AB + i * S + j],
                            &attention_output_gradients[i * D],
                            &value_state_gradients[j * D], D);
        }

    /* Step 3: backward softmax -> dSc (in da) */
    for (i = 0; i < S; i++) {
        dad = vector_dot_product(&attention_workspace[AB + i * S],
                     &attention_score_gradients[i * S], S);
        for (j = 0; j < S; j++) {
            t = subtract_clamped(attention_score_gradients[i * S + j], dad);
            t = multiply_q8(attention_workspace[AB + i * S + j], t);
            attention_score_gradients[i * S + j] =
                arithmetic_shift_right(t, 2);
        }
    }

    /* Step 4: dQ, dK */
    for (i = 0; i < S; i++)
        transposed_matrix_vector_multiply(
            &attention_workspace[KB], &attention_score_gradients[i * S],
            &query_state_gradients[i * D], S, D);
    for (j = 0; j < S; j++) {
        for (i = 0; i < S; i++)
            gradient_column[i] = attention_score_gradients[i * S + j];
        transposed_matrix_vector_multiply(
            &attention_workspace[QB], gradient_column,
            &key_state_gradients[j * D], S, D);
    }

    /* Step 5: backward projections + dX */
        memcpy(embedding_gradients, attention_output_gradients,
               S * D * (int)sizeof(model_value_t));
    for (i = 0; i < S; i++) {
        o = i * D;
        matrix_vector_add(query_weights_q8, &query_state_gradients[o],
                          &embedding_gradients[o], D, D);
        add_outer_product(query_weight_gradients, &embeddings[o],
                          &query_state_gradients[o], D, D);
        matrix_vector_add(key_weights_q8, &key_state_gradients[o],
                          &embedding_gradients[o], D, D);
        add_outer_product(key_weight_gradients, &embeddings[o],
                          &key_state_gradients[o], D, D);
        matrix_vector_add(value_weights_q8, &value_state_gradients[o],
                          &embedding_gradients[o], D, D);
        add_outer_product(value_weight_gradients, &embeddings[o],
                          &value_state_gradients[o], D, D);
    }

    /* Step 6: backward embedding */
    for (i = 0; i < S; i++) {
        o = i * D;
        tok = tokens[i];
        for (k = 0; k < D; k++) {
            add_clamped(&token_gradients[tok * D + k],
                        embedding_gradients[o + k]);
            add_clamped(&position_gradients[i * D + k],
                        embedding_gradients[o + k]);
        }
    }
}

/* ============================================================ */
/* Training driver                                              */
/* ============================================================ */

/* set reversal target for the current tokens */
static void make_targets(void)
{
    int i;

    for (i = 0; i < S; i++)
        targets[i] = tokens[S - 1 - i];
}

/* generate a random reversal sample */
static void generate_sample(void)
{
    int i;

    for (i = 0; i < S; i++)
        tokens[i] = random_number() % 10;
    make_targets();
}

/* train one current tokens/target sample */
static void train_sequence(void)
{
    convert_weights_to_q8();
    forward_pass();
    backward_pass();
    update_weights();
    count_predictions();
}

/* quiet accuracy check for one current tokens/target sample */
static bool check_sequence(void)
{
    int i, idx;
    bool ok;

    forward_pass();
    ok = true;
    for (i = 0; i < S; i++) {
        vector_maximum(&logits[i * V], V, &idx);
        if (idx != targets[i])
            ok = false;
    }
    return ok;
}

static int process_sequence(int mode)
{
    if (mode == FM_INFER)
        return infer_sequence();
    make_targets();
    if (mode == FM_TRAIN) {
        train_sequence();
        return 1;
    }
    return check_sequence();
}

static int process_sequence_file(char *filename, int mode)
{
    int character, digit_count, sequence_count;
    FILE *input_file;

    input_file = fopen(filename, "r");
    if (input_file == NULL)
        return ERROR;

    sequence_count = 0;
    digit_count = 0;
    while ((character = getc(input_file)) != EOF && character != 26) {
        if (character >= '0' && character <= '9') {
            if (digit_count < S)
                tokens[digit_count] = character - '0';
            digit_count = digit_count + 1;
        } else if (character == '\n') {
            if (digit_count >= S) {
                sequence_count = sequence_count + 1;
                if (mode == FM_VALID)
                    validation_hits = validation_hits + process_sequence(mode);
                else if (mode == FM_INFER)
                    file_hits = file_hits + process_sequence(mode);
                else
                    process_sequence(mode);
            } else if (mode == FM_INFER && digit_count > 0)
                printf(" (skipped line: %d digits, need %d)\n", digit_count, S);
            digit_count = 0;
        }
    }
    if (digit_count >= S) {
        sequence_count = sequence_count + 1;
        if (mode == FM_VALID)
            validation_hits = validation_hits + process_sequence(mode);
        else if (mode == FM_INFER)
            file_hits = file_hits + process_sequence(mode);
        else
            process_sequence(mode);
    }
    fclose(input_file);
    return sequence_count;
}

/* read fixed samples from fname. trn=1 trains; trn=0 validates quietly. */
static int run_training_file(char *filename, bool train)
{
    if (!train) {
        validation_hits = 0;
        convert_weights_to_q8();
    }
    return process_sequence_file(filename, train ? FM_TRAIN : FM_VALID);
}

/* count correct argmax predictions */
static void count_predictions(void)
{
    int i, idx;

    for (i = 0; i < S; i++) {
        vector_maximum(&logits[i * V], V, &idx);
        if (idx == targets[i])
            training_hits = training_hits + 1;
        training_total = training_total + 1;
    }
}

/* average cross-entropy loss (Q12) for the current sample */
static int cross_entropy_loss(void)
{
    weight_value_t acc;
    int i, probability, target_token;

    acc = 0;
    for (i = 0; i < S; i++) {
         memcpy(logit_gradients, &logits[i * V],
             V * (int)sizeof(model_value_t));
        softmax(logit_gradients, V);
        target_token = targets[i];
        probability = logit_gradients[target_token];
        if (probability < 256)
            acc += (weight_value_t)logarithm_table[probability];
    }
    return (int)(acc / 8L);
}

/* fractional part (0-9999) of a Q12 value */
static inline int loss_fraction(int loss)
{
    return (int)(((weight_value_t)(loss & 0x0FFF) * 10000L) / 4096L);
}

/* print step / loss / accuracy, then reset counters */
static void report_training(void)
{
    int loss, accuracy_thousandths;

    loss = cross_entropy_loss();
        printf("\n step %4d loss=%d.%.4d", training_step, loss >> 12,
               loss_fraction(loss));

    accuracy_thousandths =
        (int)(((weight_value_t)training_hits * 1000L) / training_total);
    if (accuracy_thousandths >= 1000)
        printf(" acc=1.000\n");
    else
        printf(" acc=0.%.3d\n", accuracy_thousandths);

    training_hits = 0;
    training_total = 0;
}

/* final test: 10 samples */
static void test_random_samples(void)
{
    int sample, position, prediction, sample_correct, correct_samples;

    correct_samples = 0;
    convert_weights_to_q8();
    for (sample = 0; sample < 10; sample++) {
        generate_sample();
        forward_pass();
        for (position = 0; position < S; position++) {
            vector_maximum(&logits[position * V], V, &prediction);
            test_predictions[position] = prediction;
        }
        printf(" ");
        for (position = 0; position < S; position++)
            printf("%d ", tokens[position]);
        printf("-> ");
        for (position = 0; position < S; position++)
            printf("%d ", test_predictions[position]);
        sample_correct = 1;
        for (position = 0; position < S; position++)
            if (test_predictions[position] != targets[position])
                sample_correct = 0;
        if (sample_correct) {
            correct_samples = correct_samples + 1;
            printf(" ok\n");
        } else
            printf(" fail\n");
    }
    printf("\naccuracy  %2d/%d\n", correct_samples, 10);
}

/* run inference on the current tokens[] and print "in -> out".
 * The task is to reverse the sequence, so the expected output is the
 * input read backwards; score the prediction against it.
 * Assumes convert_weights_to_q8() has already built the Q8 weight copies.
 * Returns 1 if every position is correct, else 0. */
static bool infer_sequence(void)
{
    int position, prediction;
    bool sequence_correct;

    forward_pass();
    printf(" ");
    for (position = 0; position < S; position++)
        printf("%d ", tokens[position]);
    printf("-> ");
    sequence_correct = true;
    for (position = 0; position < S; position++) {
        vector_maximum(&logits[position * V], V, &prediction);
        printf("%d ", prediction);
        if (prediction != tokens[S - 1 - position])
            sequence_correct = false;
    }
    if (sequence_correct)
        printf(" ok\n");
    else
        printf(" fail\n");
    return sequence_correct;
}

/* read S-digit sequences from a text file and run inference on each.
 * Digits are 0-9; any non-digit (space, newline) separates sequences.
 * A line must supply at least S digits; the first S are used.
 * Returns the count processed, or ERROR if the file cannot be opened. */
static int run_inference_file(char *filename)
{
    file_hits = 0;
    return process_sequence_file(filename, FM_INFER);
}

/* ============================================================ */
/* Weight persistence (POSIX byte I/O)                          */
/* ============================================================ */

/* save the six Q16 weight arrays to WFILE; 0 ok, ERROR on fail.
 * dcc stores int32_t little-endian, so the file is little-endian. */
static int transfer_weight_group(int file, weight_value_t *weights, int count,
                                 bool write_data)
{
    int bytes;

    bytes = count * (int)sizeof(weight_value_t);
    if (write_data)
        return write(file, weights, bytes) == bytes;
    return read(file, weights, bytes) == bytes;
}

static int transfer_weight_file(int file, bool write_data)
{
    return transfer_weight_group(file, token_weights_q16, V * D, write_data)
        && transfer_weight_group(file, position_weights_q16, S * D, write_data)
        && transfer_weight_group(file, query_weights_q16, D * D, write_data)
        && transfer_weight_group(file, key_weights_q16, D * D, write_data)
        && transfer_weight_group(file, value_weights_q16, D * D, write_data)
        && transfer_weight_group(file, output_weights_q16, D * V, write_data);
}

static int save_weights(void)
{
    int file, success;

    file = open(WFILE, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (file < 0)
        return ERROR;
    success = transfer_weight_file(file, true);
    close(file);
    return success ? 0 : ERROR;
}

/* load the six Q16 weight arrays from WFILE; 0 ok, ERROR on fail */
static int load_weights(void)
{
    int file, success;

    file = open(WFILE, O_RDONLY, 0);
    if (file < 0)
        return ERROR;
    success = transfer_weight_file(file, false);
    close(file);
    return success ? 0 : ERROR;
}

/* ============================================================ */
/* Host stopwatch via Z80 port I/O (see port_drivers/time_io.c)  */
/*                                                               */
/* inp()/outp() are a dcc extension (not C89) for direct 8-bit   */
/* port I/O: inp() runs IN A,(port) and outp() runs OUT (port),A */
/* (only the low 8 bits of port are significant).  We drive the  */
/* same ports ATTNZ80.MAC uses:                                  */
/*                                                               */
/*   OUT 37,0 -> start/reset host stopwatch 0                    */
/*   OUT 37,1 -> latch elapsed seconds (4-byte big-endian long)  */
/*   IN  200  -> read those 4 bytes back, MSB first              */
/*                                                               */
/* The ports are no-ops under a bare CP/M emulator that lacks    */
/* them; the elapsed time is only meaningful on this project's   */
/* Altair emulator / ESP32 firmware.                             */
/* ============================================================ */

extern int  inp(unsigned port);
extern void outp(unsigned port, unsigned val);

#define SWPORT 37               /* host stopwatch 0         */
#define RDPORT 200              /* request-buffer read-back */

/* read the latched 4-byte big-endian elapsed seconds; low 16 bits */
static uint16_t elapsed_seconds(void)
{
    int hi, lo;

    inp(RDPORT);                /* byte 0 (MSB) - ignore */
    inp(RDPORT);                /* byte 1       - ignore */
    hi = inp(RDPORT);           /* byte 2                */
    lo = inp(RDPORT);           /* byte 3 (LSB)          */
    return (unsigned)((hi << 8) | lo);
}

/* ============================================================ */
/* Entry point                                                  */
/* ============================================================ */

int main(int argc, char *argv[])
{
    int step, sequence_count, validation_count;
    bool train;
    char *filename;

    train = false;
    filename = IFILE;
    if (argc > 1) {
        if (strcmp(argv[1], "-t") == 0 || strcmp(argv[1], "-T") == 0)
            train = true;
        else if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "-H") == 0 ||
                 strcmp(argv[1], "/?") == 0) {
            printf("attnc11 - tiny transformer that reverses 8 digits\n\n");
            printf("usage:\n");
            printf("  attnc11          infer from %s (one 8-digit line each)\n", IFILE);
            printf("  attnc11 <file>   infer from <file> instead\n");
            printf("  attnc11 -t       train, save weights to %s, then test\n", WFILE);
            printf("  attnc11 -h       this help\n");
            return 0;
        } else
            filename = argv[1];     /* explicit input file */
    }

    printf("attn/11 - paper tape is all you need\n");
    printf("d=16 seq=8 v=10 params=1216 q8/q15/q16\n\n");

    if (train) {
        printf("training...\n");
        random_seed = 887;
        initialize_weights();
        zero_gradients();
        training_hits = 0;
        training_total = 0;
        for (step = 1; step <= NSTEP; step++) {
            training_step = step;
            putchar('.');       /* heartbeat: one dot per completed step */
            generate_sample();
            train_sequence();
            if ((step % FSTEP) == 0)
                run_training_file(IFILE, true);
            if ((step % RPRT) == 0) {
                report_training();
                  validation_count = run_training_file(IFILE, false);
                  if (validation_count != ERROR) {
                      printf(" validation %2d/%d on %s\n", validation_hits,
                          validation_count, IFILE);
                      if (validation_hits == validation_count &&
                       validation_count > 0) {
                        printf("validation passed; stopping early\n");
                        break;
                    }
                }
            }
        }
        printf("\nsaving weights to %s ...\n", WFILE);
        if (save_weights() != 0)
            printf("WARNING: could not save weights\n");
        printf("\n");
        test_random_samples();
        sequence_count = run_inference_file(IFILE);
        if (sequence_count != ERROR)
            printf("\naccuracy  %2d/%d on %s\n", file_hits, sequence_count,
                   IFILE);
        return 0;
    }

    /* inference */
    printf("loading weights from %s ...\n", WFILE);
    if (load_weights() != 0) {
        printf("no weights file found - run 'attnc11 -t' first\n");
        return 1;
    }
    convert_weights_to_q8();        /* build Q8 weight copies once */

    outp(SWPORT, 0);                /* start host stopwatch 0 */
    sequence_count = run_inference_file(filename);
    if (sequence_count == ERROR) {
        if (argc > 1) {
            printf("cannot open input file %s\n", filename);
            return 1;
        }
        /* no input file present: fall back to a random demo */
        printf("no %s found - running random demo\n\n", IFILE);
        random_seed = 1;
        test_random_samples();
        return 0;
    }
    outp(SWPORT, 1);                /* latch elapsed seconds */

    printf("\naccuracy  %2d/%d\n", file_hits, sequence_count);
    printf("run time  %u s\n", elapsed_seconds());
    return 0;
}
