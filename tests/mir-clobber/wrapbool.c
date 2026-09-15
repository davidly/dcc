#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef WRAPBOOL_UNSIGNED_COUNT
typedef unsigned int wrapbool_count_t;
#else
typedef int wrapbool_count_t;
#endif

#ifdef WRAPBOOL_INT_ELEMENTS
typedef int wrapbool_element_t;
#else
typedef bool wrapbool_element_t;
#endif

#ifdef WRAPBOOL_VOLATILE_CURRENT
#define WRAPBOOL_CURRENT_QUALIFIER volatile
#else
#define WRAPBOOL_CURRENT_QUALIFIER
#endif

#ifdef WRAPBOOL_VOLATILE_NEXT
#define WRAPBOOL_NEXT_QUALIFIER volatile
#else
#define WRAPBOOL_NEXT_QUALIFIER
#endif

#ifdef WRAPBOOL_UNSIGNED_RETURN
typedef uint32_t wrapbool_result_t;
#elif defined(WRAPBOOL_INT_RETURN)
typedef int wrapbool_result_t;
#else
typedef int32_t wrapbool_result_t;
#endif

#ifdef WRAPBOOL_VOLATILE_LIVE
#define WRAPBOOL_LIVE_QUALIFIER volatile
#else
#define WRAPBOOL_LIVE_QUALIFIER
#endif

#ifdef WRAPBOOL_VOLATILE_INDEX
#define WRAPBOOL_INDEX_QUALIFIER volatile
#else
#define WRAPBOOL_INDEX_QUALIFIER
#endif

#ifdef WRAPBOOL_VOLATILE_LEFT
#define WRAPBOOL_LEFT_QUALIFIER volatile
#else
#define WRAPBOOL_LEFT_QUALIFIER
#endif

#ifdef WRAPBOOL_VOLATILE_RIGHT
#define WRAPBOOL_RIGHT_QUALIFIER volatile
#else
#define WRAPBOOL_RIGHT_QUALIFIER
#endif

static wrapbool_result_t fixture_wrapbool(
    wrapbool_count_t count,
    const WRAPBOOL_CURRENT_QUALIFIER wrapbool_element_t current[count],
    WRAPBOOL_NEXT_QUALIFIER wrapbool_element_t next[count])
{
    WRAPBOOL_LIVE_QUALIFIER int32_t live = 0;

#ifdef WRAPBOOL_EXTRA_CFG
    if (count == 123)
        return 7;
#endif
    for (WRAPBOOL_INDEX_QUALIFIER int index = 0;
         index < count; ++index) {
        WRAPBOOL_LEFT_QUALIFIER bool left =
            current[(index - 1 + count) % count];
        WRAPBOOL_RIGHT_QUALIFIER bool right =
            current[(index + 1) % count];

#ifdef WRAPBOOL_REVERSE_XOR
        next[index] = right ^ left;
#else
        next[index] = left ^ right;
#endif
        if (next[index])
            ++live;
    }
    return live;
}

static int check_case(
    int count, const wrapbool_element_t *current,
    const wrapbool_element_t *expected, long expected_live)
{
    wrapbool_element_t next[7] = {
        true, true, true, true, true, true, true
    };
    long live;
    int index;
    int failures = 0;

    live = fixture_wrapbool(count, current, next);
    if (live != expected_live)
        ++failures;
    if (count > 0) {
        for (index = 0; index < count; ++index)
            if (next[index] != expected[index])
                ++failures;
    } else if (!next[0]) {
        ++failures;
    }
    return failures;
}

int main(void)
{
    static const wrapbool_element_t one_current[1] = { true };
    static const wrapbool_element_t one_expected[1] = { false };
    static const wrapbool_element_t two_current[2] = { true, false };
    static const wrapbool_element_t two_expected[2] = { false, false };
    static const wrapbool_element_t five_current[5] = {
        true, false, true, true, false
    };
    static const wrapbool_element_t five_expected[5] = {
        false, false, true, true, false
    };
    int failures = 0;

    failures += check_case(1, one_current, one_expected, 0);
    failures += check_case(2, two_current, two_expected, 0);
    failures += check_case(5, five_current, five_expected, 2);
    failures += check_case(0, one_current, one_expected, 0);
#ifndef WRAPBOOL_UNSIGNED_COUNT
    failures += check_case(-1, one_current, one_expected, 0);
#endif
    printf("wrapbool failures=%d\n", failures);
    return failures != 0;
}
