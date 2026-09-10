#include <stdio.h>

#define ADDW12_ACTIVE 16
#define ADDW12_GUARD 8
#define ADDW12_ROWS 6
#define ADDW12_MASK 15
#define ADDW12_GUARD_VALUE 211
#define ADDW12_SRCV 3
#define ADDW12_DSTV 8

typedef unsigned char addw12_byte;

#ifdef ADDW12_SIGNED_SOURCE
typedef signed short addw12_source_word;
#else
typedef unsigned short addw12_source_word;
#endif

#ifdef ADDW12_MEMBER_LAYOUT
struct Addw12Pair {
    addw12_source_word offset;
    unsigned char gap;
    addw12_source_word value;
};
static const struct Addw12Pair addw12_source[ADDW12_ROWS] = {
    {0x19, 7, 0x90}, {0x32, 8, 0x90}, {0x3b, 9, 0x90},
    {0x48, 10, 0x90}, {0x5a, 11, 0x0c}, {0x66, 12, 0x90}
};
#define ADDW12_SOURCE_OFFSET(i) addw12_source[i].offset
#define ADDW12_SOURCE_VALUE(i) addw12_source[i].value
#elif defined(ADDW12_ARRAY_LAYOUT)
static const addw12_source_word addw12_source[ADDW12_ROWS][3] = {
    {0x19, 7, 0x90}, {0x32, 8, 0x90}, {0x3b, 9, 0x90},
    {0x48, 10, 0x90}, {0x5a, 11, 0x0c}, {0x66, 12, 0x90}
};
#define ADDW12_SOURCE_OFFSET(i) addw12_source[i][0]
#define ADDW12_SOURCE_VALUE(i) addw12_source[i][2]
#else
static const addw12_source_word addw12_source[ADDW12_ROWS][2] = {
    {0x19, 0x90}, {0x32, 0x90}, {0x3b, 0x90},
    {0x48, 0x90}, {0x5a, 0x0c}, {0x66, 0x90}
};
#define ADDW12_SOURCE_OFFSET(i) addw12_source[i][0]
#define ADDW12_SOURCE_VALUE(i) addw12_source[i][1]
#endif

static addw12_byte addw12_target[ADDW12_ACTIVE + ADDW12_GUARD];
static int addw12_failures;
static unsigned int addw12_checks;

#ifdef ADDW12_CALL_ORDER
static void addw12_check(
    const char *name, long expected, long got)
#else
static void addw12_check(
    const char *name, long got, long expected)
#endif
{
    ++addw12_checks;
    if (got != expected) {
        printf("FAIL %s got %ld expected %ld\n",
               name, got, expected);
        ++addw12_failures;
    }
}

#ifdef ADDW12_WIDE_OFFSET
#define ADDW12_OFFSET_TYPE long
#else
#define ADDW12_OFFSET_TYPE int
#endif

#ifdef ADDW12_CALL_ORDER
#define ADDW12_CHECK(name, got, expected) \
    addw12_check(name, expected, got)
#else
#define ADDW12_CHECK(name, got, expected) \
    addw12_check(name, got, expected)
#endif

#ifdef ADDW12_RETURN_VALUE
static int additive_wave12(void)
#else
static void additive_wave12(void)
#endif
{
    int i;

#ifdef ADDW12_LOOP_PHI
    i = ADDW12_ACTIVE;
    while (i > 0) {
        --i;
        addw12_target[i] = (addw12_byte)(i + 1);
    }
#else
    for (i = 0; i < ADDW12_ACTIVE; i++)
        addw12_target[i] = (addw12_byte)(i + 1);
#endif

    addw12_target[ADDW12_SRCV + 0] = 0xa0;
    addw12_target[ADDW12_SRCV + 1] = 0xa1;
    addw12_target[ADDW12_DSTV + 0] = 0xb0;
    addw12_target[ADDW12_DSTV + 1] = 0xb1;

    ADDW12_CHECK("addw12 fixed 3",
                 addw12_target[ADDW12_SRCV + 0], 0xa0L);
    ADDW12_CHECK("addw12 fixed 4",
                 addw12_target[ADDW12_SRCV + 1], 0xa1L);
    ADDW12_CHECK("addw12 fixed 8",
                 addw12_target[ADDW12_DSTV + 0], 0xb0L);
    ADDW12_CHECK("addw12 fixed 9",
                 addw12_target[ADDW12_DSTV + 1], 0xb1L);

    for (i = 0; i < ADDW12_ROWS; i++) {
        ADDW12_OFFSET_TYPE off;
        int val;

        off = ADDW12_SOURCE_OFFSET(i) & ADDW12_MASK;
        val = ADDW12_SOURCE_VALUE(i) & 0xff;
#ifdef ADDW12_INDEX_ORDER
        addw12_target[1 - (-off)] = (addw12_byte)val;
#else
        addw12_target[off + 1] = (addw12_byte)val;
#endif
    }

    ADDW12_CHECK("addw12 loop 10",
                 addw12_target[(0x19 & 0x0f) + 1], 0x90L);
    ADDW12_CHECK("addw12 loop 11",
                 addw12_target[(0x5a & 0x0f) + 1], 0x0cL);
#ifdef ADDW12_RETURN_VALUE
    return 0;
#endif
}

static unsigned long addw12_mix(
    unsigned long hash, unsigned int value)
{
    return (hash ^ value) * 16777619UL;
}

int main(void)
{
    addw12_byte expected[ADDW12_ACTIVE];
    unsigned long hash;
    unsigned long source_hash;
    int i;
    int guards_ok;

    for (i = 0; i < ADDW12_ACTIVE + ADDW12_GUARD; ++i)
        addw12_target[i] = ADDW12_GUARD_VALUE;
#ifdef ADDW12_RETURN_VALUE
    if (additive_wave12() != 0)
        ++addw12_failures;
#else
    additive_wave12();
#endif

    for (i = 0; i < ADDW12_ACTIVE; ++i)
        expected[i] = (addw12_byte)(i + 1);
    expected[ADDW12_SRCV + 0] = 0xa0;
    expected[ADDW12_SRCV + 1] = 0xa1;
    expected[ADDW12_DSTV + 0] = 0xb0;
    expected[ADDW12_DSTV + 1] = 0xb1;
    for (i = 0; i < ADDW12_ROWS; ++i)
        expected[(ADDW12_SOURCE_OFFSET(i) & ADDW12_MASK) + 1] =
            (addw12_byte)(ADDW12_SOURCE_VALUE(i) & 0xff);

    hash = 2166136261UL;
    for (i = 0; i < ADDW12_ACTIVE; ++i) {
        ++addw12_checks;
        if (addw12_target[i] != expected[i])
            ++addw12_failures;
        hash = addw12_mix(hash, addw12_target[i]);
    }
    guards_ok = 1;
    for (i = ADDW12_ACTIVE;
         i < ADDW12_ACTIVE + ADDW12_GUARD; ++i) {
        ++addw12_checks;
        if (addw12_target[i] != ADDW12_GUARD_VALUE) {
            guards_ok = 0;
            ++addw12_failures;
        }
    }
    source_hash = 2166136261UL;
    for (i = 0; i < ADDW12_ROWS; ++i) {
        ++addw12_checks;
        source_hash = addw12_mix(
            source_hash, (unsigned int)ADDW12_SOURCE_OFFSET(i));
        source_hash = addw12_mix(
            source_hash, (unsigned int)ADDW12_SOURCE_VALUE(i));
    }
    printf("ADDW12 failures=%d checks=%u guards=%d calls=6 "
           "hash=%lu source=%lu\n",
           addw12_failures, addw12_checks, guards_ok,
           hash, source_hash);
    return addw12_failures != 0;
}
