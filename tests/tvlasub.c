/*
 * tvlasub.c - one-dimensional bool VLA regression.
 * Generated archive case: batch9/c9931.
 */
#include <stdbool.h>
#include <stdio.h>

static bool subset(int count, const int values[count], int target)
{
    bool possible[target + 1];

    for (int sum = 0; sum <= target; ++sum)
        possible[sum] = false;
    possible[0] = true;
    for (int index = 0; index < count; ++index)
        for (int sum = target; sum >= values[index]; --sum)
            if (possible[sum - values[index]])
                possible[sum] = true;
    return possible[target];
}

int main(void)
{
    int values[5] = { 3, 7, 9, 12, 4 };
    bool has_sixteen = subset(5, values, 16);
    bool has_two = subset(5, values, 2);
    int ok = has_sixteen && !has_two;

    printf("tvlasub subset=%d,%d\n",
           (int)has_sixteen, (int)has_two);
    if (ok)
        printf("tvlasub passed with great success\n");
    return !ok;
}
