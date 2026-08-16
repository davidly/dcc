/*
 * tindopt.c - indirect-load home preservation regression.
 * Generated archive case: batch8/c1115.
 */
#include <stdio.h>

struct Optional {
    int present;
    union {
        int value;
    };
};

static int value_or(const struct Optional *optional, int fallback)
{
    return optional->present ? optional->value : fallback;
}

int main(void)
{
    struct Optional present = { 1, { 9 } };
    struct Optional missing = { 0, { 0 } };
    int value = value_or(&present, 5);
    int fallback = value_or(&missing, 5);
    int ok = value == 9 && fallback == 5;

    printf("tindopt optional=%d,%d\n", value, fallback);
    if (ok)
        printf("tindopt passed with great success\n");
    return !ok;
}
