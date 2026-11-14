/* Loop-carried 32-bit locals also passed directly to calls: for/while/count
 * loops, variable assignments, post-loop tests and untaken-loop initialisers.
 * Covers float assignment-expression and split-statement forms, used/ignored
 * call results and long/float compound updates. Expression/narrow arguments
 * and no-loop forms are neighbouring controls. */
#include <stdint.h>
#include <stdio.h>

static uint32_t take32(int32_t x) { return (uint32_t)x + 1; }
static uint32_t take16(int16_t x) { return (uint32_t)x + 1; }
static uint32_t sink;

static int32_t for_loop(void)
{
    int32_t l = 0;
    uint16_t i;
    for (i = 5; i != 0; i--) {
        l = 1;
        take32(l);
    }
    return l;
}

static int32_t while_loop(void)
{
    int32_t l = 0;
    uint16_t i = 5;
    while (i != 0) {
        i--;
        l = 1;
        sink = take32(l);
    }
    return l;
}

static int32_t counted_loop(void)
{
    int32_t l = 0;
    uint16_t i;
    for (i = 0; i < 5; i++) {
        l = 7;
        take32(l);
    }
    return l;
}

static int32_t used_in_test(void)
{
    int32_t l = 0;
    uint16_t i;
    for (i = 5; i != 0; i--) {
        l = 1;
        take32(l);
    }
    return l == 1 ? 100 : 200;
}

static int32_t expression_argument(void)
{
    int32_t l = 0;
    uint16_t i;
    for (i = 5; i != 0; i--) {
        l = 1;
        take32(l + 1);
    }
    return l;
}

static int32_t narrow_argument(void)
{
    int32_t l = 0;
    uint16_t i;
    for (i = 5; i != 0; i--) {
        l = 1;
        take16((int16_t)l);
    }
    return l;
}

static float fg = -4087.25f;
static float fout;
static float fid(float x) { return x; }
static int32_t lid(int32_t x) { return x; }

static int32_t variable_argument(void)
{
    int32_t l = 0;
    uint16_t i;
    for (i = 1; i < 4; i++) { l = i; take32(l); }
    return l;
}

static int32_t untaken_loop(void)
{
    int32_t l = 65537;
    uint16_t i = 0;
    while (i) { l = 1; take32(l); i--; }
    return l;
}

static float float_assignment(void)
{
    float l = 3.0f;
    uint16_t i;
    for (i = 0; i < 2; i++) fid((l = fg));
    return l;
}

static float float_split_assignment(void)
{
    float l = 3.0f;
    uint16_t i;
    for (i = 0; i < 2; i++) { l = fg; fid(l); }
    return l;
}

static float float_used_call(void)
{
    float l = 3.0f;
    uint16_t i;
    for (i = 0; i < 2; i++) fout = fid((l = fg));
    return l;
}

static float float_argument_update(void)
{
    float l = 3.0f;
    uint16_t i;
    for (i = 0; i < 2; i++) fid((l += fg));
    return l;
}

static float float_multiply(void)
{
    float l = 0.5f;
    uint16_t n = 1;
    while (n) { l *= 2.0f; fid(l); n--; }
    return l;
}

static float float_add(void)
{
    float l = 0.5f;
    uint16_t n = 2;
    while (n) { l += 1.0f; fid(l); n--; }
    return l;
}

static int32_t long_multiply(void)
{
    int32_t l = 3;
    uint16_t n = 2;
    while (n) { l *= 2; lid(l); n--; }
    return l;
}

static int32_t long_add(void)
{
    int32_t l = 3;
    uint16_t n = 2;
    while (n) { l += 1; lid(l); n--; }
    return l;
}

static float float_expression_argument(void)
{
    float l = 3.0f;
    uint16_t i;
    for (i = 0; i < 2; i++) fid((l = fg) + 0.0f);
    return l;
}

static float float_constant_assignment(void)
{
    float l = 3.0f;
    uint16_t i;
    for (i = 0; i < 2; i++) { l = 7.0f; fid(l); }
    return l;
}

static float float_without_loop(void)
{
    float l = 3.0f;
    fid((l = fg));
    return l;
}

static float float_used_product(void)
{
    float l = 0.5f;
    uint16_t n = 1;
    while (n) { l *= 2.0f; fout = fid(l); n--; }
    return l;
}

int main(void)
{
    printf("for %ld\n", (long)for_loop());
    printf("while %ld\n", (long)while_loop());
    printf("counted %ld\n", (long)counted_loop());
    printf("test %ld\n", (long)used_in_test());
    printf("expression %ld\n", (long)expression_argument());
    printf("narrow %ld\n", (long)narrow_argument());
    printf("variable %ld\n", (long)variable_argument());
    printf("untaken %ld\n", (long)untaken_loop());
    printf("float assign %.2f\n", float_assignment());
    printf("float split %.2f\n", float_split_assignment());
    printf("float used %.2f\n", float_used_call());
    printf("float argument update %.2f\n", float_argument_update());
    printf("float multiply %.2f\n", float_multiply());
    printf("float add %.2f\n", float_add());
    printf("long multiply %ld\n", (long)long_multiply());
    printf("long add %ld\n", (long)long_add());
    printf("float expression %.2f\n", float_expression_argument());
    printf("float constant %.2f\n", float_constant_assignment());
    printf("float no loop %.2f\n", float_without_loop());
    printf("float used product %.2f\n", float_used_product());
    return 0;
}
