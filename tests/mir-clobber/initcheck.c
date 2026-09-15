#include <stdio.h>

#ifdef INITCHECK_RENAMED
#define initializer_check_fixture initializer_check_fixture_renamed
#endif

#ifdef INITCHECK_CHANGED_VALUE
#define FIRST_VALUE 17
#else
#define FIRST_VALUE 7
#endif

#ifdef INITCHECK_VOLATILE
#define LOCAL_QUALIFIER volatile
#else
#define LOCAL_QUALIFIER
#endif

static int failures;
static int checks;
static unsigned long checksum;
static int expected_third = 29;
static int expected_fourth = 41;
#ifdef INITCHECK_EXTRA_CFG
static volatile int extra_gate;
#endif

static void record_check(const char *name, int got, int want)
{
    checksum = checksum * 33UL +
        (unsigned int)got * 3UL +
        (unsigned int)want * 5UL +
        (unsigned char)name[0];
    checks++;
    if (got != want)
        failures++;
}

static void check_first(const char *name, int got, int want)
{
    record_check(name, got, want);
}

static void check_second(const char *name, int got, int want)
{
    record_check(name, got, want);
}

static void initializer_check_fixture(void)
{
    LOCAL_QUALIFIER int first;
    int second;
    int third;
    int fourth;

    first = FIRST_VALUE;
    second = 13;
    third = 29;
    fourth = 41;
#ifdef INITCHECK_EXTRA_CFG
    if (extra_gate)
        failures++;
#endif
    check_first("first", first, FIRST_VALUE);
    check_second("second", second, 13);
    check_first("third", third, expected_third);
    check_second("fourth", fourth, expected_fourth);
    check_first("sum-a", first + second, FIRST_VALUE + 13);
    check_second("sum-b", third + fourth, 70);
}

int main(void)
{
    initializer_check_fixture();
    printf("initializer check failures=%d checks=%d checksum=%lu\n",
           failures, checks, checksum);
    return failures;
}
