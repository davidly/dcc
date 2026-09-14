/* Focused byte-record-copy exact-schedule fixture. */
#include <stdio.h>

#ifdef BRC_RENAMED
#define COPY_FUNCTION byte_record_copy_renamed
#else
#define COPY_FUNCTION byte_record_copy
#endif

#ifdef BRC_VOLATILE_FIELD_0
#define BRC_FIELD_0_QUAL volatile
#else
#define BRC_FIELD_0_QUAL
#endif
#ifdef BRC_VOLATILE_FIELD_1
#define BRC_FIELD_1_QUAL volatile
#else
#define BRC_FIELD_1_QUAL
#endif
#ifdef BRC_VOLATILE_FIELD_2
#define BRC_FIELD_2_QUAL volatile
#else
#define BRC_FIELD_2_QUAL
#endif
#ifdef BRC_VOLATILE_FIELD_3
#define BRC_FIELD_3_QUAL volatile
#else
#define BRC_FIELD_3_QUAL
#endif
#ifdef BRC_VOLATILE_FIELD_4
#define BRC_FIELD_4_QUAL volatile
#else
#define BRC_FIELD_4_QUAL
#endif
#ifdef BRC_VOLATILE_FIELD_5
#define BRC_FIELD_5_QUAL volatile
#else
#define BRC_FIELD_5_QUAL
#endif
#ifdef BRC_VOLATILE_FIELD_6
#define BRC_FIELD_6_QUAL volatile
#else
#define BRC_FIELD_6_QUAL
#endif
#ifdef BRC_VOLATILE_FIELD_7
#define BRC_FIELD_7_QUAL volatile
#else
#define BRC_FIELD_7_QUAL
#endif

typedef struct ByteRecord {
#ifdef BRC_BITFIELD_0
    unsigned int first : 8;
#else
    BRC_FIELD_0_QUAL unsigned char first;
#endif
#ifdef BRC_BITFIELD_1
    unsigned int second : 8;
#else
    BRC_FIELD_1_QUAL unsigned char second;
#endif
#ifdef BRC_BITFIELD_2
    unsigned int third : 8;
#else
    BRC_FIELD_2_QUAL unsigned char third;
#endif
#ifdef BRC_BITFIELD_3
    unsigned int fourth : 8;
#else
    BRC_FIELD_3_QUAL unsigned char fourth;
#endif
#ifdef BRC_BITFIELD_4
    unsigned int fifth : 8;
#else
    BRC_FIELD_4_QUAL unsigned char fifth;
#endif
#ifdef BRC_BITFIELD_5
    unsigned int sixth : 8;
#else
    BRC_FIELD_5_QUAL unsigned char sixth;
#endif
#ifdef BRC_BITFIELD_6
    unsigned int seventh : 8;
#else
    BRC_FIELD_6_QUAL unsigned char seventh;
#endif
#ifdef BRC_BITFIELD_7
    unsigned int eighth : 8;
#else
    BRC_FIELD_7_QUAL unsigned char eighth;
#endif
#ifdef BRC_EXTRA_FIELD
    unsigned char ninth;
#endif
} ByteRecord;

#ifdef BRC_DIFFERENT_TYPES
typedef struct ByteRecordSource {
    unsigned char first;
    unsigned char second;
    unsigned char third;
    unsigned char fourth;
    unsigned char fifth;
    unsigned char sixth;
    unsigned char seventh;
    unsigned char eighth;
} ByteRecordSource;
#else
typedef ByteRecord ByteRecordSource;
#endif

#ifdef BRC_NONVOID
static int COPY_FUNCTION(
#else
static void COPY_FUNCTION(
#endif
#ifdef BRC_REVERSED_PARAMETERS
    ByteRecordSource *source,
#ifdef BRC_VOLATILE_DESTINATION
    volatile
#endif
    ByteRecord *destination
#else
#ifdef BRC_VOLATILE_DESTINATION
    volatile
#endif
    ByteRecord *destination,
#ifdef BRC_VOLATILE_SOURCE
    volatile
#endif
    ByteRecordSource *source
#endif
#ifdef BRC_EXTRA_PARAMETER
    , int ignored
#endif
)
{
#ifdef BRC_LOCAL_STATE
    volatile unsigned char observed;

    observed = source->first;
#endif
#ifdef BRC_VLA_STATE
    unsigned char scratch[source->first + 1];

    scratch[0] = source->eighth;
#endif
#ifdef BRC_EXTRA_PARAMETER
    (void)ignored;
#endif
    destination->first = source->first;
    destination->second = source->second;
    destination->third = source->third;
    destination->fourth = source->fourth;
    destination->fifth = source->fifth;
    destination->sixth = source->sixth;
    destination->seventh = source->seventh;
    destination->eighth = source->eighth;
#ifdef BRC_EXTRA_FIELD
    destination->ninth = source->ninth;
#endif
#ifdef BRC_LOCAL_STATE
    if (observed != destination->first)
        destination->eighth = 0;
#endif
#ifdef BRC_VLA_STATE
    if (scratch[0] != destination->eighth)
        destination->first = 0;
#endif
#ifdef BRC_NONVOID
    return 7;
#endif
}

#ifdef BRC_REVERSED_PARAMETERS
#ifdef BRC_EXTRA_PARAMETER
#define COPY_RECORD(destination, source) \
    COPY_FUNCTION((source), (destination), 123)
#else
#define COPY_RECORD(destination, source) \
    COPY_FUNCTION((source), (destination))
#endif
#else
#ifdef BRC_EXTRA_PARAMETER
#define COPY_RECORD(destination, source) \
    COPY_FUNCTION((destination), (source), 123)
#else
#define COPY_RECORD(destination, source) \
    COPY_FUNCTION((destination), (source))
#endif
#endif

static int record_matches(
    ByteRecord *record,
    unsigned char first,
    unsigned char second,
    unsigned char third,
    unsigned char fourth,
    unsigned char fifth,
    unsigned char sixth,
    unsigned char seventh,
    unsigned char eighth)
{
    return record->first == first &&
           record->second == second &&
           record->third == third &&
           record->fourth == fourth &&
           record->fifth == fifth &&
           record->sixth == sixth &&
           record->seventh == seventh &&
           record->eighth == eighth;
}

int main(void)
{
    ByteRecordSource first = {
        1, 17, 33, 49, 65, 81, 97, 113
#ifdef BRC_EXTRA_FIELD
        , 129
#endif
    };
    ByteRecordSource second = {
        254, 238, 222, 110, 190, 174, 158, 142
#ifdef BRC_EXTRA_FIELD
        , 126
#endif
    };
    ByteRecord destination;
    int failures;

    destination.first = 0;
    destination.second = 0;
    destination.third = 0;
    destination.fourth = 0;
    destination.fifth = 0;
    destination.sixth = 0;
    destination.seventh = 0;
    destination.eighth = 0;
    COPY_RECORD(&destination, &first);
    failures = !record_matches(
        &destination, 1, 17, 33, 49, 65, 81, 97, 113);
    COPY_RECORD(&destination, &second);
    if (!record_matches(
            &destination, 254, 238, 222, 110, 190, 174, 158, 142))
        ++failures;
    printf(
        "byte record copy failures=%d values=%u,%u,%u,%u,%u,%u,%u,%u\n",
        failures,
        (unsigned int)destination.first,
        (unsigned int)destination.second,
        (unsigned int)destination.third,
        (unsigned int)destination.fourth,
        (unsigned int)destination.fifth,
        (unsigned int)destination.sixth,
        (unsigned int)destination.seventh,
        (unsigned int)destination.eighth);
    return failures != 0;
}
