#include <stdio.h>

#ifdef AGG_WORD_SUM_RENAMED
#define AGG_WORD_SUM_FUNCTION aggregate_word_sum_fixture_renamed
#else
#define AGG_WORD_SUM_FUNCTION aggregate_word_sum_fixture
#endif

#ifdef AGG_WORD_SUM_UNSIGNED
typedef unsigned int word_type;
#else
typedef int word_type;
#endif

struct AggregateWords {
    unsigned char first;
    word_type second;
    unsigned char third;
    word_type fourth;
};

#ifdef AGG_WORD_SUM_VOLATILE
#define AGG_WORD_SUM_QUALIFIER volatile
#else
#define AGG_WORD_SUM_QUALIFIER
#endif

static int AGG_WORD_SUM_FUNCTION(
    AGG_WORD_SUM_QUALIFIER struct AggregateWords words,
    int extra)
{
#ifdef AGG_WORD_SUM_EXTRA_CFG
    if (extra == -32768)
        return extra;
#endif
#ifdef AGG_WORD_SUM_SUBTRACT
    return words.first + words.second - words.third +
           words.fourth + extra;
#else
    return words.first + words.second + words.third +
           words.fourth + extra;
#endif
}

int main(void)
{
    struct AggregateWords first = { 7, 300, 11, -25 };
    struct AggregateWords second = { 200, -400, 55, 1234 };
    int first_sum;
    int second_sum;
    int checksum;

    first_sum = AGG_WORD_SUM_FUNCTION(first, 9);
    second_sum = AGG_WORD_SUM_FUNCTION(second, -10);
    checksum = first_sum * 17 + second_sum;
    printf(
        "aggregate word sum first=%d second=%d checksum=%d\n",
        first_sum, second_sum, checksum);
#ifdef AGG_WORD_SUM_SUBTRACT
    return first_sum != 280 || second_sum != 969 || checksum != 5729;
#else
    return first_sum != 302 || second_sum != 1079 || checksum != 6213;
#endif
}
