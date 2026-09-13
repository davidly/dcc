#include <stdio.h>

#define CAPACITY 8
#define GUARD 23130

#ifdef SLIDING_LARGER_DEQUE
#define DEQUE_CAPACITY (CAPACITY + 1)
#else
#define DEQUE_CAPACITY CAPACITY
#endif

#ifdef SLIDING_UNSIGNED_ELEMENTS
typedef unsigned int element_t;
#else
typedef int element_t;
#endif

#ifdef SLIDING_VOLATILE_IO
#define IO_QUALIFIER volatile
#else
#define IO_QUALIFIER
#endif

#ifdef SLIDING_UNSIGNED_LENGTH
typedef unsigned int length_t;
#else
typedef int length_t;
#endif

#ifdef SLIDING_UNSIGNED_RETURN
static unsigned int sliding_wave7(
#else
static int sliding_wave7(
#endif
    const IO_QUALIFIER element_t *a, length_t n, int k,
    IO_QUALIFIER element_t *out)
{
    int dq[DEQUE_CAPACITY];
#ifdef SLIDING_VOLATILE_STATE
    volatile int head = 0, tail = 0, count = 0, i;
#else
    int head = 0, tail = 0, count = 0, i;
#endif
    for (i = 0; i < n; ++i) {
        int limit = i - k + 1;
        while (head < tail && dq[head] < limit)
            ++head;
        while (head < tail && a[dq[tail - 1]] <= a[i])
            --tail;
        dq[tail++] = i;
        if (i >= k - 1)
            out[count++] = a[dq[head]];
    }
    return count;
}

static int reference_maximum(
    const element_t input[], int length, int window,
    element_t output[])
{
    int count = 0;
    int first;

    if (window <= 0 || window > length)
        return 0;
    for (first = 0; first + window <= length; ++first) {
        int maximum = input[first];
        int index;

        for (index = first + 1; index < first + window; ++index)
            if (input[index] > maximum)
                maximum = input[index];
        output[count++] = maximum;
    }
    return count;
}

static int run_case(
    const element_t input[], int length, int window)
{
    element_t actual[CAPACITY + 4];
    element_t expected[CAPACITY];
    int actual_count;
    int expected_count;
    int index;

    for (index = 0; index < CAPACITY + 4; ++index)
        actual[index] = GUARD;
    actual_count = sliding_wave7(input, length, window, actual + 2);
    expected_count = reference_maximum(input, length, window, expected);
    if (actual_count != expected_count)
        return 0;
    for (index = 0; index < expected_count; ++index)
        if (actual[index + 2] != expected[index])
            return 0;
    if (actual[0] != GUARD || actual[1] != GUARD)
        return 0;
    for (index = expected_count + 2; index < CAPACITY + 4; ++index)
        if (actual[index] != GUARD)
            return 0;
    return 1;
}

static int run_alias_case(void)
{
    element_t values[CAPACITY] =
        {1, 3, -1, -3, 5, 3, 6, 7};
    element_t expected[CAPACITY];
    int expected_count;
    int actual_count;
    int index;

    expected_count =
        reference_maximum(values, CAPACITY, 3, expected);
    actual_count =
        sliding_wave7(values, CAPACITY, 3, values);
    if (actual_count != expected_count)
        return 0;
    for (index = 0; index < expected_count; ++index)
        if (values[index] != expected[index])
            return 0;
    return 1;
}

int main(void)
{
    static const element_t descending[CAPACITY] =
        {9, 8, 7, 6, 5, 4, 3, 2};
    static const element_t duplicates[CAPACITY] =
        {-7, -7, -9, -7, -8, -8, -6, -6};
    static const element_t alternating[CAPACITY] =
        {-32768, 32767, -1, 0, 32767, -32768, 4, 4};
    int cases = 0;
    int failures = 0;

    failures += !run_case(descending, 0, 3); ++cases;
    failures += !run_case(descending, 2, 3); ++cases;
    failures += !run_case(descending, 1, 1); ++cases;
    failures += !run_case(descending, CAPACITY, 1); ++cases;
    failures += !run_case(descending, CAPACITY, CAPACITY); ++cases;
    failures += !run_case(descending, CAPACITY, 3); ++cases;
    failures += !run_case(duplicates, CAPACITY, 3); ++cases;
    failures += !run_case(alternating, CAPACITY, 4); ++cases;
    failures += !run_alias_case(); ++cases;

    printf("SLIDINGW7 cases=%d failures=%d guards=%d\n",
           cases, failures, failures == 0);
    return failures != 0;
}
