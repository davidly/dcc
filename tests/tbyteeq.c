#include <stdio.h>

#ifdef BYTE_EQUALITY_W28_LARGE_ARRAYS
#define BYTE_EQUALITY_ARRAY_SIZE 6
#else
#define BYTE_EQUALITY_ARRAY_SIZE 4
#endif

#ifdef BYTE_EQUALITY_W28_VOLATILE_ARRAYS
#define BYTE_EQUALITY_ARRAY_VOLATILE volatile
#else
#define BYTE_EQUALITY_ARRAY_VOLATILE
#endif

#ifdef BYTE_EQUALITY_W28_VOLATILE_FAILURES
#define BYTE_EQUALITY_FAILURES_VOLATILE volatile
#else
#define BYTE_EQUALITY_FAILURES_VOLATILE
#endif

#ifdef BYTE_EQUALITY_W28_ALT_STRINGS
#define BYTE_EQUALITY_NAME(text) "wave28-" text
#else
#define BYTE_EQUALITY_NAME(text) text
#endif

static BYTE_EQUALITY_ARRAY_VOLATILE signed char
    signed_values[BYTE_EQUALITY_ARRAY_SIZE];
static BYTE_EQUALITY_ARRAY_VOLATILE unsigned char
    unsigned_values[BYTE_EQUALITY_ARRAY_SIZE];
static BYTE_EQUALITY_FAILURES_VOLATILE int failures;

static void check(const char *name, int got, int expected)
{
    if (got != expected) {
        printf("FAIL %s got=%d expected=%d\n", name, got, expected);
        failures++;
    }
}

int main(int argc, char **argv)
{
#ifdef BYTE_EQUALITY_W28_VOLATILE_LOCAL
    volatile
#endif
    signed char signed_high = (signed char)(-56 * argc);
    signed char signed_same = (signed char)(-56 * argc);
    signed char signed_low = (signed char)(65 * argc);
    unsigned char unsigned_high = (unsigned char)(200 * argc);
    unsigned char unsigned_same = (unsigned char)(200 * argc);
    int index = argc;
    int branch_result;
    int value_result;

    (void)argv;
    signed_values[index + 1] = signed_high;
    unsigned_values[index + 1] = unsigned_high;

    branch_result = 0;
    if (signed_high == 200)
        branch_result = 1;
    check(BYTE_EQUALITY_NAME("signed-const-hi-eq"), branch_result, 0);

    branch_result = 0;
    if (signed_high != 200)
        branch_result = 1;
    check(BYTE_EQUALITY_NAME("signed-const-hi-ne"), branch_result, 1);

    branch_result = 0;
    if (signed_high == unsigned_high)
        branch_result = 1;
    check(BYTE_EQUALITY_NAME("mixed-ident"), branch_result, 0);

    value_result = (signed_high == 200) || (unsigned_high == 201);
    check(BYTE_EQUALITY_NAME("logical-value"), value_result, 0);

    branch_result = 0;
    if (signed_values[index + 1] == 200)
        branch_result = 1;
    check(BYTE_EQUALITY_NAME("signed-array-const"), branch_result, 0);

    branch_result = 0;
    if (200 == signed_values[index + 1])
        branch_result = 1;
    check(BYTE_EQUALITY_NAME("signed-array-reversed"), branch_result, 0);

    branch_result = 0;
    if (signed_values[index + 1] == unsigned_high)
        branch_result = 1;
    check(BYTE_EQUALITY_NAME("signed-array-mixed"), branch_result, 0);

    check(BYTE_EQUALITY_NAME("signed-ident-safe"),
          signed_high == signed_same, 1);
    check(BYTE_EQUALITY_NAME("unsigned-ident-safe"),
          unsigned_high == unsigned_same, 1);
    check(BYTE_EQUALITY_NAME("signed-const-low-safe"),
          signed_low == 65, 1);
    check(BYTE_EQUALITY_NAME("unsigned-const-hi-safe"),
          unsigned_values[index + 1] == 200, 1);

    /* Array element vs identifier, matching signedness: the safe half of
     * the same guard "signed-array-mixed" above exercises the decline
     * side of - not covered by any existing case, in either operand
     * order. */
    branch_result = 0;
    if (signed_values[index + 1] == signed_high)
        branch_result = 1;
    check(BYTE_EQUALITY_NAME("signed-array-signed-ident-safe"),
          branch_result, 1);

    branch_result = 0;
    if (signed_high == signed_values[index + 1])
        branch_result = 1;
    check(BYTE_EQUALITY_NAME("signed-ident-signed-array-reversed-safe"),
          branch_result, 1);

    branch_result = 0;
    if (unsigned_values[index + 1] == unsigned_high)
        branch_result = 1;
    check(BYTE_EQUALITY_NAME("unsigned-array-unsigned-ident-safe"),
          branch_result, 1);

    branch_result = 0;
    if (unsigned_high == unsigned_values[index + 1])
        branch_result = 1;
    check(BYTE_EQUALITY_NAME("unsigned-ident-unsigned-array-reversed-safe"),
          branch_result, 1);

    printf("tbyteeq %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures != 0;
}