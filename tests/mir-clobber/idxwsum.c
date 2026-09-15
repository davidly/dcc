#include <stdio.h>

#ifdef IDXWSUM_VOLATILE_VALUES
typedef volatile struct IndexedValue indexed_value_t;
#else
typedef struct IndexedValue indexed_value_t;
#endif

struct IndexedValue {
    unsigned char tag;
#ifdef IDXWSUM_BITFIELD_VALUE
    int value : 16;
#else
    int value;
#endif
};

#ifdef IDXWSUM_LONG_RESULT
typedef long indexed_result_t;
#else
typedef int indexed_result_t;
#endif

static indexed_result_t indexed_word_sum(indexed_value_t *values
#ifdef IDXWSUM_EXTRA_PARAMETER
                                         , int ignored
#endif
)
{
#ifdef IDXWSUM_VLA_STATE
    int scratch[values[0].tag + 1];

    scratch[0] = 0;
    return values[1].value + values[6].value + scratch[0];
#elif defined(IDXWSUM_LOCAL_STATE)
    volatile int adjustment;

    adjustment = values[0].tag;
    return values[1].value + values[6].value +
           adjustment - adjustment;
#elif defined(IDXWSUM_BRANCH)
    if (values[0].tag == 255)
        return values[2].value;
#endif
#ifdef IDXWSUM_EXTRA_PARAMETER
    (void)ignored;
#endif
    return values[1].value + values[6].value;
}

#ifdef IDXWSUM_EXTRA_PARAMETER
#define INDEXED_WORD_SUM(values) indexed_word_sum((values), 123)
#else
#define INDEXED_WORD_SUM(values) indexed_word_sum(values)
#endif

int main(void)
{
    indexed_value_t first[8] = {
        {1, 301}, {2, -1200}, {3, 77}, {4, 4096},
        {5, -33}, {6, 18}, {7, 2345}, {8, -9}
    };
    indexed_value_t second[8] = {
        {9, -32768}, {10, 5}, {11, -4}, {12, 3},
        {13, -2}, {14, 1}, {15, 32767}, {16, 0}
    };
    indexed_result_t first_sum;
    indexed_result_t second_sum;
    int failures;

    first_sum = INDEXED_WORD_SUM(first);
    second_sum = INDEXED_WORD_SUM(second);
    failures = 0;
    if (first_sum != 1145)
        ++failures;
    if (second_sum != -32764)
        ++failures;
    printf("indexed word sum failures=%d values=%d,%d\n",
           failures, (int)first_sum, (int)second_sum);
    return failures != 0;
}
