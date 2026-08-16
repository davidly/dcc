#include <float.h>
#include <stdbool.h>
#include <stdio.h>

static int calls;

static float odd_absolute(float value)
{
    (void)value;
    ++calls;
    return 0.0f;
}

void compare_float(float left, float right)
{
    float difference;
    difference = left - right;
    float absolute_difference;
    absolute_difference = odd_absolute(difference);
    bool greater = (difference > 0.0f &&
                    absolute_difference > FLT_EPSILON);
    bool less = (difference < 0.0f &&
                 absolute_difference > FLT_EPSILON);
    bool equal = (absolute_difference < FLT_EPSILON);
    bool less_equal = (difference <= 0.0f ||
                       absolute_difference < FLT_EPSILON);
    bool greater_equal = (difference >= 0.0f ||
                          absolute_difference < FLT_EPSILON);
    printf("cmp=%d,%d,%d,%d,%d\n",
           less, less_equal, equal, greater_equal, greater);
}

int main(void)
{
    compare_float(3.0f, 1.0f);
    printf("calls=%d\n", calls);
    return 0;
}
