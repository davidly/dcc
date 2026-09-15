/* Exhaustive exact-schedule fixture for a for-initializer pointer walk. */
#include <stdio.h>

#ifdef FORINITPTR_RENAMED
#define fixture_for_init_pointer_walk fixture_for_init_pointer_walk_renamed
#endif

#ifdef FORINITPTR_UNSIGNED_LENGTH
typedef unsigned int length_type;
#else
typedef int length_type;
#endif

#ifdef FORINITPTR_WIDE_POINTER
typedef const int *walk_pointer;
#else
typedef const char *walk_pointer;
#endif

static int fixture_for_init_pointer_walk(
    walk_pointer s, length_type len)
{
    walk_pointer p = s + len;
#ifdef FORINITPTR_VOLATILE_STEPS
    volatile int steps = 0;
#elif defined(FORINITPTR_UNSIGNED_STEPS)
    unsigned int steps = 0;
#else
    int steps = 0;
#endif

#ifdef FORINITPTR_EXTRA_CFG
    if (len == 12345)
        return -1;
#endif
#ifdef FORINITPTR_GREATER_THAN
    for (--p; p > s; --p)
#elif defined(FORINITPTR_POSTFIX)
    for (p--; p != s; p--)
#else
    for (--p; p != s; --p)
#endif
#ifdef FORINITPTR_DOUBLE_STEP
        steps += 2;
#else
        steps++;
#endif
    return steps;
}

static int expected_steps(int length)
{
#ifdef FORINITPTR_DOUBLE_STEP
    return (length - 1) * 2;
#else
    return length - 1;
#endif
}

static int check_case(walk_pointer text, int length)
{
    return fixture_for_init_pointer_walk(
        text, (length_type)length) != expected_steps(length);
}

int main(void)
{
#ifdef FORINITPTR_WIDE_POINTER
    static const int input[] = {
        11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53
    };
#else
    static const char input[] = "pointer-walk";
#endif
    int failures = 0;
    int checksum;

    failures += check_case(input, 1);
    failures += check_case(input, 2);
    failures += check_case(input, 5);
    failures += check_case(input, 10);
    checksum =
        fixture_for_init_pointer_walk(input, 2) * 3 +
        fixture_for_init_pointer_walk(input, 5) * 7 +
        fixture_for_init_pointer_walk(input, 10) * 11;
    printf(
        "for init pointer walk failures=%d checksum=%d\n",
        failures, checksum);
#ifdef FORINITPTR_DOUBLE_STEP
    return failures != 0 || checksum != 260;
#else
    return failures != 0 || checksum != 130;
#endif
}
