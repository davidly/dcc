/* tc99varm.c - C99 variadic macro compatibility tests. */
#include <stdio.h>

#define CALL(FUN, ...) FUN(__VA_ARGS__)
#define ARGS(...) __VA_ARGS__
#define ZERO_VAR(...) 0
#define ZERO_1_VAR(A, ...) 0
#define STR_ARGS(...) #__VA_ARGS__

static int failures;

static void check_int(int got, int want, const char *name)
{
    if (got != want) {
        printf("FAIL %s got=%d want=%d\n", name, got, want);
        failures++;
    }
}

static void check_str(const char *got, const char *want, const char *name)
{
    int i;

    for (i = 0; got[i] || want[i]; ++i) {
        if (got[i] != want[i]) {
            printf("FAIL %s\n", name);
            failures++;
            return;
        }
    }
}

static int none(void)
{
    return 0;
}

static int one(int a)
{
    return a;
}

static int two(int a, int b)
{
    return a * 10 + b;
}

static int three(int a, int b, int c)
{
    return a * 100 + b * 10 + c;
}

int main(void)
{
    check_int(CALL(one, 7), 7, "CALL one");
    check_int(CALL(two, 3, 4), 34, "CALL two");
    check_int(CALL(three, 1, 2, 3), 123, "CALL three");
    check_int(none(ARGS()), 0, "ARGS empty");
    check_int(one(ARGS(5)), 5, "ARGS one");
    check_int(two(ARGS(6, 7)), 67, "ARGS two");
    check_int(ZERO_VAR(), 0, "ZERO_VAR empty");
    check_int(ZERO_VAR(1, 2), 0, "ZERO_VAR args");
    check_int(ZERO_1_VAR(1), 0, "ZERO_1_VAR empty tail");
    check_int(ZERO_1_VAR(1, 2, 3), 0, "ZERO_1_VAR tail");
    check_str(STR_ARGS(1, 2, 3), "1, 2, 3", "STR_ARGS");

    if (failures == 0)
        printf("test tc99varm completed with great success\n");
    return failures;
}