#include <stdio.h>

#ifdef POST_INDEX_RENAMED
#define post_index_report_fixture post_index_report_fixture_renamed
#endif

#ifdef POST_INDEX_CHANGED_VALUES
#define GLOBAL_VALUE 2345
#define LOCAL_VALUE 67
#else
#define GLOBAL_VALUE 10
#define LOCAL_VALUE 20
#endif

#ifdef POST_INDEX_CHANGED_INDEX
#define INITIAL_INDEX 2
#else
#define INITIAL_INDEX 1
#endif

#ifdef POST_INDEX_GLOBAL_UNSIGNED
#define INDEX_TYPE unsigned int
#else
#define INDEX_TYPE int
#endif

#ifdef POST_INDEX_VOLATILE_ARRAY
#define ARRAY_QUALIFIER volatile
#else
#define ARRAY_QUALIFIER
#endif

INDEX_TYPE post_index_global;
ARRAY_QUALIFIER int post_index_values[4];
#ifdef POST_INDEX_EXTRA_CFG
volatile int post_index_gate;
#endif

static int post_index_report_fixture(void)
{
    int local_values[4];
    int local_index;

    post_index_global = INITIAL_INDEX;
    post_index_values[post_index_global++] = GLOBAL_VALUE;
    local_index = INITIAL_INDEX;
    local_values[local_index++] = LOCAL_VALUE;
#ifdef POST_INDEX_EXTRA_CFG
    if (post_index_gate)
        local_values[1] = 0;
#endif
    printf("post index global=%d value=%d local=%d value=%d\n",
           post_index_global, post_index_values[INITIAL_INDEX],
           local_index, local_values[INITIAL_INDEX]);
    return 0;
}

int main(void)
{
    int result;
    int failures;
    unsigned long checksum;

    result = post_index_report_fixture();
    failures = 0;
    if (result != 0)
        failures++;
    if (post_index_global != INITIAL_INDEX + 1)
        failures++;
    if (post_index_values[INITIAL_INDEX] != GLOBAL_VALUE)
        failures++;
    checksum = (unsigned long)(unsigned int)post_index_global * 257UL;
    checksum +=
        (unsigned long)(unsigned int)post_index_values[INITIAL_INDEX] * 17UL;
    checksum += (unsigned long)(unsigned int)GLOBAL_VALUE * 5UL;
    checksum += (unsigned long)(unsigned int)LOCAL_VALUE;
    printf("post index failures=%d checksum=%lu\n", failures, checksum);
    return failures;
}
