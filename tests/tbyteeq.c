#include <stdio.h>

static signed char signed_values[4];
static unsigned char unsigned_values[4];
static int failures;

static void check(const char *name, int got, int expected)
{
    if (got != expected) {
        printf("FAIL %s got=%d expected=%d\n", name, got, expected);
        failures++;
    }
}

int main(int argc, char **argv)
{
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
    check("signed-const-hi-eq", branch_result, 0);

    branch_result = 0;
    if (signed_high != 200)
        branch_result = 1;
    check("signed-const-hi-ne", branch_result, 1);

    branch_result = 0;
    if (signed_high == unsigned_high)
        branch_result = 1;
    check("mixed-ident", branch_result, 0);

    value_result = (signed_high == 200) || (unsigned_high == 201);
    check("logical-value", value_result, 0);

    branch_result = 0;
    if (signed_values[index + 1] == 200)
        branch_result = 1;
    check("signed-array-const", branch_result, 0);

    branch_result = 0;
    if (200 == signed_values[index + 1])
        branch_result = 1;
    check("signed-array-reversed", branch_result, 0);

    branch_result = 0;
    if (signed_values[index + 1] == unsigned_high)
        branch_result = 1;
    check("signed-array-mixed", branch_result, 0);

    check("signed-ident-safe", signed_high == signed_same, 1);
    check("unsigned-ident-safe", unsigned_high == unsigned_same, 1);
    check("signed-const-low-safe", signed_low == 65, 1);
    check("unsigned-const-hi-safe", unsigned_values[index + 1] == 200, 1);

    printf("tbyteeq %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures != 0;
}