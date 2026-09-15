/* Exact and near-match controls for indexed member stores. */
#include <stdio.h>

#ifdef IDXMW_POINTER_POINTEE_VOLATILE
#define IDXMW_ENTRY_QUAL volatile
#else
#define IDXMW_ENTRY_QUAL
#endif

#ifdef IDXMW_VALUE_VOLATILE
#define IDXMW_VALUE_QUAL volatile
#else
#define IDXMW_VALUE_QUAL
#endif

struct IndexedMemberEntry {
    int guard_before;
#ifdef IDXMW_VALUE_BITFIELD
    signed int value : 15;
#elif defined(IDXMW_VALUE_CHAR)
    signed char value;
#elif defined(IDXMW_VALUE_LONG)
    long value;
#else
    IDXMW_VALUE_QUAL int value;
#endif
    int guard_after;
};

struct IndexedMemberState {
    int prefix;
#ifdef IDXMW_POINTER_FIELD_VOLATILE
    IDXMW_ENTRY_QUAL struct IndexedMemberEntry * volatile entries;
#else
    IDXMW_ENTRY_QUAL struct IndexedMemberEntry *entries;
#endif
#ifdef IDXMW_INDEX_VOLATILE
    volatile int index;
#elif defined(IDXMW_INDEX_CHAR)
    signed char index;
#elif defined(IDXMW_INDEX_LONG)
    long index;
#else
    int index;
#endif
    int suffix;
};

static struct IndexedMemberState state_storage;
#ifdef IDXMW_ROOT_VOLATILE
static struct IndexedMemberState * volatile active_state;
#else
static struct IndexedMemberState *active_state;
#endif
static struct IndexedMemberState *x;

#ifdef IDXMW_RENAMED
#define indexed_member_write indexed_member_write_renamed
#endif

#ifdef IDXMW_PARAMETER_CHAR
static void indexed_member_write(signed char value)
#elif defined(IDXMW_NONVOID)
static int indexed_member_write(int value)
#elif defined(IDXMW_SECOND_PARAMETER)
static void indexed_member_write(int value, int ignored)
#else
static void indexed_member_write(int value)
#endif
{
    IDXMW_ENTRY_QUAL struct IndexedMemberEntry *selected;

#ifdef IDXMW_CFG_BRANCH
    if (value == -32768)
        return;
#endif
#ifdef IDXMW_REVERSED_ADD
    selected = active_state->index + active_state->entries;
#elif defined(IDXMW_ADJUSTED_POINTER)
    selected = active_state->entries + active_state->index - 1;
#else
    selected = active_state->entries + active_state->index;
#endif
    selected->value = value;
#ifdef IDXMW_NONVOID
    return value;
#endif
}

int main(void)
{
    struct IndexedMemberEntry entries[5];
    int failures;
    int index;
    long expected;

    for (index = 0; index < 5; ++index) {
        entries[index].guard_before = 1000 + index;
        entries[index].value = -20 - index;
        entries[index].guard_after = 2000 + index;
    }
    state_storage.prefix = 0x1234;
    state_storage.entries = entries;
#ifdef IDXMW_ADJUSTED_POINTER
    state_storage.index = 3;
#else
    state_storage.index = 2;
#endif
    state_storage.suffix = 0x5678;
    active_state = &state_storage;
    x = &state_storage;

#ifdef IDXMW_SECOND_PARAMETER
    indexed_member_write(0x3456, 0);
#elif defined(IDXMW_NONVOID)
    if (indexed_member_write(0x3456) != 0x3456)
        return 1;
#else
    indexed_member_write(0x3456);
#endif

#if defined(IDXMW_VALUE_CHAR) || defined(IDXMW_PARAMETER_CHAR)
    expected = 0x56;
#else
    expected = 0x3456;
#endif
    failures = 0;
    if ((long)entries[2].value != expected)
        ++failures;
    if (entries[2].guard_before != 1002 ||
        entries[2].guard_after != 2002)
        ++failures;
    if ((long)entries[1].value != -21 ||
        (long)entries[3].value != -23)
        ++failures;
    if (state_storage.prefix != 0x1234 ||
        state_storage.suffix != 0x5678)
        ++failures;

    printf("indexed member write target=%ld guards=%d,%d "
           "neighbors=%ld,%ld state=%d failures=%d\n",
           (long)entries[2].value,
           entries[2].guard_before, entries[2].guard_after,
           (long)entries[1].value, (long)entries[3].value,
           state_storage.prefix + state_storage.suffix,
           failures);
    return failures != 0;
}
