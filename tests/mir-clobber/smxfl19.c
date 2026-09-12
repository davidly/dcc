#include <stdio.h>
#include <string.h>

static float exponential_table[4] = {
    2.0f, 1.0f, 1.0f, 1.0f
};

static void softmax_float(float *vector, unsigned char length)
{
    float maximum, difference, sum;
    unsigned char i;
    int index;

    maximum = vector[0];
    for (i = 1; i < length; ++i)
        if (vector[i] > maximum)
            maximum = vector[i];
    sum = 0.0f;
    for (i = 0; i < length; ++i) {
        difference = maximum - vector[i];
        if (difference < 0.0f)
            difference = 0.0f;
        index = (int)difference;
        if (index > 3)
            index = 3;
        vector[i] = exponential_table[index];
        sum += vector[i];
    }
    for (i = 0; i < length; ++i)
        vector[i] /= sum;
}

static unsigned long float_bits(float value)
{
    unsigned long bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static int failures;
static unsigned int checks;
static unsigned long hash = 2166136261UL;

static void check(float value, unsigned long expected)
{
    unsigned long actual = float_bits(value);

    ++checks;
    if (actual != expected)
        ++failures;
    hash = (hash ^ actual) * 16777619UL;
}

int main(void)
{
    float singleton[1] = {9.0f};
    float equal[2] = {-3.0f, -3.0f};
    float stepped[3] = {2.0f, 1.0f, 0.0f};

    softmax_float(singleton, 1);
    softmax_float(equal, 2);
    softmax_float(stepped, 3);
    check(singleton[0], 1065353216UL);
    check(equal[0], 1056964608UL);
    check(equal[1], 1056964608UL);
    check(stepped[0], 1056964608UL);
    check(stepped[1], 1048576000UL);
    check(stepped[2], 1048576000UL);
    printf("SMXFL19 failures=%d checks=%u hash=%lu\n",
           failures, checks, hash);
    return failures != 0;
}
