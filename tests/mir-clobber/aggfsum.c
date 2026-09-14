#include <stdio.h>

#ifdef AGGFSUM_VOLATILE_FIELDS
#define AGGFSUM_FIELD_QUAL volatile
#else
#define AGGFSUM_FIELD_QUAL
#endif

struct AggregateFieldSumRecord {
    AGGFSUM_FIELD_QUAL signed char first;
#ifdef AGGFSUM_PADDED_RECORD
    unsigned char padding;
#endif
    AGGFSUM_FIELD_QUAL long second;
    AGGFSUM_FIELD_QUAL unsigned int third;
#ifdef AGGFSUM_FOUR_FIELDS
    AGGFSUM_FIELD_QUAL int fourth;
#endif
};

#ifdef AGGFSUM_RENAMED
#define aggregate_field_sum_kernel aggregate_field_sum_renamed
#endif

#ifdef AGGFSUM_POINTER_PARAMETER
static long aggregate_field_sum_kernel(
    const struct AggregateFieldSumRecord *record)
{
    return record->first + record->second + record->third;
}
#else
static long aggregate_field_sum_kernel(
    struct AggregateFieldSumRecord record)
{
#ifdef AGGFSUM_FOUR_FIELDS
    return record.first + record.second + record.third + record.fourth;
#else
    return record.first + record.second + record.third;
#endif
}
#endif

static long evaluate(struct AggregateFieldSumRecord *record)
{
#ifdef AGGFSUM_POINTER_PARAMETER
    return aggregate_field_sum_kernel(record);
#else
    return aggregate_field_sum_kernel(*record);
#endif
}

int main(void)
{
    struct AggregateFieldSumRecord negative;
    struct AggregateFieldSumRecord positive;
    long first;
    long second;
    long oracle;

    negative.first = -5;
#ifdef AGGFSUM_PADDED_RECORD
    negative.padding = 0x5a;
#endif
    negative.second = -70000L;
    negative.third = 60000U;
#ifdef AGGFSUM_FOUR_FIELDS
    negative.fourth = 0;
#endif

    positive.first = 127;
#ifdef AGGFSUM_PADDED_RECORD
    positive.padding = 0xa5;
#endif
    positive.second = 200000L;
    positive.third = 65535U;
#ifdef AGGFSUM_FOUR_FIELDS
    positive.fourth = 0;
#endif

    first = evaluate(&negative);
    second = evaluate(&positive);
    oracle = first * 17L + second;
    printf("aggregate field sum first=%ld second=%ld oracle=%ld\n",
           first, second, oracle);
    return first != -10005L ||
           second != 265662L ||
           oracle != 95577L;
}
