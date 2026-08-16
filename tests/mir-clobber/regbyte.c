#include <stdio.h>

static int scratch;

static int helper(int value)
{
    return value * 7 + 1;
}

static int late(int flag)
{
    register int first;
    register int second;
    int temporary;

    if (flag) {
        first = 11;
        second = 23;
    } else {
        first = 17;
        second = 29;
    }
    temporary = helper(5);
    scratch = helper(6);
    scratch = scratch + temporary;
    return first * 100 + second;
}

int main(void)
{
    int left = late(0);
    int right = late(1);

    printf("regional-byte %d %d %d\n", left, right, scratch);
    return left == 1729 && right == 1123 && scratch == 79 ? 0 : 1;
}
