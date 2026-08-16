/*
 * tindres.c - discarded indirect-load/live-home regression.
 * Generated archive case: batch8/c1102.
 */
#include <stdio.h>

struct Result {
    int ok;
    union {
        int value;
        const char *message;
    };
};

static int unwrap(const struct Result *result, int fallback)
{
    return result->ok ? result->value : fallback;
}

int main(void)
{
    struct Result result = { 1, { .value = 42 } };
    struct Result error = { 0, { .message = "bad" } };
    int value = unwrap(&result, -1);
    int fallback = unwrap(&error, -1);
    int ok = value == 42 && fallback == -1;

    printf("tindres result=%d,%d\n", value, fallback);
    if (ok)
        printf("tindres passed with great success\n");
    return !ok;
}
