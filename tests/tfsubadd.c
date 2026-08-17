/*
 * tfsubadd.c - raw IEEE-754 subnormal add/subtract regression coverage.
 */
#include <stdio.h>

#ifdef _DCC_
typedef unsigned long raw32_t;
#else
typedef unsigned int raw32_t;
#endif

union raw_float {
    float f;
    raw32_t u;
};

static volatile union raw_float left_value;
static volatile union raw_float right_value;
static int failures;

static raw32_t add_bits(raw32_t left, raw32_t right)
{
    union raw_float result;

    left_value.u = left;
    right_value.u = right;
    result.f = left_value.f + right_value.f;
    return result.u;
}

static raw32_t sub_bits(raw32_t left, raw32_t right)
{
    union raw_float result;

    left_value.u = left;
    right_value.u = right;
    result.f = left_value.f - right_value.f;
    return result.u;
}

static void check(const char *name, raw32_t got, raw32_t want)
{
    if (got != want) {
        failures++;
        printf("FAIL %s: got=%08lX want=%08lX\n", name,
               (unsigned long)got, (unsigned long)want);
    }
}

int main(void)
{
    check("minsub + minsub",
          add_bits(0x00000001UL, 0x00000001UL), 0x00000002UL);
    check("minsub + zero",
          add_bits(0x00000001UL, 0x00000000UL), 0x00000001UL);
    check("zero + minsub",
          add_bits(0x00000000UL, 0x00000001UL), 0x00000001UL);
    check("maxsub + minsub",
          add_bits(0x007fffffUL, 0x00000001UL), 0x00800000UL);
    check("minnormal + minsub",
          add_bits(0x00800000UL, 0x00000001UL), 0x00800001UL);
    check("normal + aligned subnormal",
          add_bits(0x01000000UL, 0x00400000UL), 0x01200000UL);

    check("minnormal - minsub",
          sub_bits(0x00800000UL, 0x00000001UL), 0x007fffffUL);
    check("minnormal - maxsub",
          sub_bits(0x00800000UL, 0x007fffffUL), 0x00000001UL);
    check("minnormal - half",
          sub_bits(0x00800000UL, 0x00400000UL), 0x00400000UL);
    check("normal - aligned subnormal",
          sub_bits(0x01000000UL, 0x00400000UL), 0x00c00000UL);
    check("adjacent normals cancel",
          sub_bits(0x00800001UL, 0x00800000UL), 0x00000001UL);
    check("subnormal cancellation",
          sub_bits(0x00000001UL, 0x00000001UL), 0x00000000UL);
    check("reverse boundary subtraction",
          sub_bits(0x007fffffUL, 0x00800000UL), 0x80000001UL);

    check("negative minsub pair",
          add_bits(0x80000001UL, 0x80000001UL), 0x80000002UL);
    check("negative minsub + zero",
          add_bits(0x80000001UL, 0x00000000UL), 0x80000001UL);
    check("opposite subnormals cancel",
          add_bits(0x00000001UL, 0x80000001UL), 0x00000000UL);
    check("negative boundary subtraction",
          sub_bits(0x80800000UL, 0x807fffffUL), 0x80000001UL);

    check("positive zero plus negative zero",
          add_bits(0x00000000UL, 0x80000000UL), 0x00000000UL);
    check("negative zero plus negative zero",
          add_bits(0x80000000UL, 0x80000000UL), 0x80000000UL);
    check("negative zero minus positive zero",
          sub_bits(0x80000000UL, 0x00000000UL), 0x80000000UL);
    check("positive zero minus negative zero",
          sub_bits(0x00000000UL, 0x80000000UL), 0x00000000UL);

    if (failures == 0)
        printf("tfsubadd: PASS\n");
    return failures ? 1 : 0;
}
