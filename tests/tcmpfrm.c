/* Signed word comparisons between two frame-resident operands branch on
 * the overflow-corrected sign of an in-place subtract. Cover every
 * relational operator across the values where the subtract overflows. */
#include <stdio.h>

static const int values[] = { -32768, -32767, -256, -1, 0, 1, 255, 256,
                              32766, 32767 };
#define COUNT (int)(sizeof values / sizeof values[0])

static int count_less(int a, int b)
{
    int n = 0, i;
    for (i = 0; i < 3; ++i)
        if (a < b) ++n;
    return n;
}

static int count_greater(int a, int b)
{
    int n = 0, i;
    for (i = 0; i < 3; ++i)
        if (a > b) ++n;
    return n;
}

static int count_less_equal(int a, int b)
{
    int n = 0, i;
    for (i = 0; i < 3; ++i)
        if (a <= b) ++n;
    return n;
}

static int count_greater_equal(int a, int b)
{
    int n = 0, i;
    for (i = 0; i < 3; ++i)
        if (a >= b) ++n;
    return n;
}

/* A loop bounded by two parameters, like an interpreter's pc range. */
static int walk(int start, int end)
{
    int pc = start, steps = 0;
    while (pc <= end) {
        ++steps;
        if (pc < start || pc > end) break;
        if (pc > end - 4096) break;
        pc += 4096;
    }
    return steps;
}

int main(void)
{
    int i, j;
    unsigned sum = 0;
    for (i = 0; i < COUNT; ++i)
        for (j = 0; j < COUNT; ++j) {
            int a = values[i], b = values[j];
            int r = count_less(a, b) * 1000 + count_greater(a, b) * 100 +
                    count_less_equal(a, b) * 10 + count_greater_equal(a, b);
            if (r != (a < b) * 3000 + (a > b) * 300 + (a <= b) * 30 +
                         (a >= b) * 3)
                printf("mismatch %d %d: %d\n", a, b, r);
            sum = (sum * 31 + (unsigned)r) & 0xffffu;
        }
    printf("checksum %u\n", sum);
    printf("walk %d %d %d\n", walk(-30000, 30000), walk(0, 10000),
           walk(30000, -30000));
    return 0;
}
