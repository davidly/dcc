#include <stdio.h>
#include <stdlib.h>

struct CallWave17Record {
    int first;
    int second;
    int third;
};

#if defined(CALLW17_LOCAL_CALLEE)
static int callw17_apply(int value)
{
    return value < 0 ? -value : value;
}
#elif defined(CALLW17_UNSIGNED_ARG)
static int callw17_apply(unsigned int value)
{
    return value > 32767U ? -(int)value : (int)value;
}
#elif defined(CALLW17_ALIAS_CALLEE)
static struct CallWave17Record *callw17_alias_record;

static int callw17_apply(int value)
{
    int result = value < 0 ? -value : value;

    ++callw17_alias_record->first;
    return result;
}
#else
#define callw17_apply abs
#endif

#if defined(CALLW17_VOLATILE_RECORD)
#define CALLW17_RECORD_QUAL const volatile
#else
#define CALLW17_RECORD_QUAL const
#endif

int callw17_member_sum(
    CALLW17_RECORD_QUAL struct CallWave17Record *record, int count)
{
    int index;
    int total = 0;

    for (index = 0; index < count; ++index) {
#if defined(CALLW17_BRANCH_BYPASS)
        if (record->first == 32767)
            continue;
#endif
        total += callw17_apply(record->first);
        total += callw17_apply(record->second);
        total += callw17_apply(record->third);
        total += record->first + record->second + record->third;
    }
    return total;
}

int main(void)
{
    struct CallWave17Record record;
    int positive;
    int zero;
    int negative;

    record.first = -7;
    record.second = 11;
    record.third = -13;
#if defined(CALLW17_ALIAS_CALLEE)
    callw17_alias_record = &record;
#endif
    positive = callw17_member_sum(&record, 4);
    zero = callw17_member_sum(&record, 0);
    negative = callw17_member_sum(&record, -2);
    printf("CALLW17 positive=%d zero=%d negative=%d oracle=%d\n",
           positive, zero, negative,
           positive * 17 + zero * 5 + negative);
#if defined(CALLW17_ALIAS_CALLEE)
    return positive != 104 || zero != 0 || negative != 0;
#else
    return positive != 88 || zero != 0 || negative != 0;
#endif
}
