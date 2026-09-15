#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXMEM 9000

#ifdef FORTGROW_FILL_ONE
#define FILL_VALUE 1
#else
#define FILL_VALUE 0
#endif

#ifdef FORTGROW_ALT_MESSAGE
#define FULL_MESSAGE "capacity exhausted"
#else
#define FULL_MESSAGE "data memory full"
#endif

#ifdef FORTGROW_RENAMED
#define GROW_FUNCTION renamed_fortran_grow_fixture
#else
#define GROW_FUNCTION fortran_grow_fixture
#endif

#ifdef FORTGROW_VOLATILE_CAPACITY
static volatile int grow_capacity;
#else
static int grow_capacity;
#endif
static unsigned char *grow_memory;
static int failures;

static _Noreturn void grow_failure(const char *message)
{
    printf("unexpected grow failure: %s\n", message);
    exit(1);
}

#ifdef FORTGROW_SPLIT_FAILURE
static _Noreturn void grow_memory_failure(const char *message)
{
    grow_failure(message);
}
#endif

static void GROW_FUNCTION(int need)
{
    unsigned char *p;
    int ncap;

#ifdef FORTGROW_EXTRA_CFG
    if (need < 0)
        grow_failure("negative need");
#endif
    if (need <= grow_capacity)
        return;
    if (need > MAXMEM)
        grow_failure(FULL_MESSAGE);

    ncap = grow_capacity;
    while (ncap < need) {
        if (ncap < 1024)
            ncap += 256;
        else
            ncap += 1024;
    }
    if (ncap > MAXMEM)
        ncap = MAXMEM;

    p = (unsigned char *)realloc(grow_memory, (unsigned int)ncap);
    if (!p)
#ifdef FORTGROW_SPLIT_FAILURE
        grow_memory_failure("oom");
#else
        grow_failure("oom");
#endif
    memset(
        p + grow_capacity, FILL_VALUE,
        (unsigned int)(ncap - grow_capacity));
    grow_memory = p;
    grow_capacity = ncap;
}

static void expect_int(const char *label, int actual, int expected)
{
    if (actual != expected) {
        printf(
            "FAIL %s actual=%d expected=%d\n",
            label, actual, expected);
        ++failures;
    }
}

static void expect_fill_range(int first, int limit)
{
    int index;

    for (index = first; index < limit; ++index) {
        if (grow_memory[index] != FILL_VALUE) {
            printf(
                "FAIL fill index=%d actual=%u expected=%d\n",
                index, (unsigned int)grow_memory[index], FILL_VALUE);
            ++failures;
            return;
        }
    }
}

int main(void)
{
    GROW_FUNCTION(1);
    expect_int("capacity-1", grow_capacity, 256);
    expect_fill_range(0, 256);
    grow_memory[0] = 17;
    grow_memory[255] = 29;

    GROW_FUNCTION(200);
    expect_int("capacity-200", grow_capacity, 256);
    expect_int("preserve-0", grow_memory[0], 17);
    expect_int("preserve-255", grow_memory[255], 29);

    GROW_FUNCTION(300);
    expect_int("capacity-300", grow_capacity, 512);
    expect_int("preserve-0-after-300", grow_memory[0], 17);
    expect_int("preserve-255-after-300", grow_memory[255], 29);
    expect_fill_range(256, 512);
    grow_memory[300] = 41;

    GROW_FUNCTION(1500);
    expect_int("capacity-1500", grow_capacity, 2048);
    expect_int("preserve-0-after-1500", grow_memory[0], 17);
    expect_int("preserve-255-after-1500", grow_memory[255], 29);
    expect_int("preserve-300-after-1500", grow_memory[300], 41);
    expect_fill_range(512, 2048);

    GROW_FUNCTION(8500);
    expect_int("capacity-8500", grow_capacity, MAXMEM);
    expect_int("preserve-0-after-clamp", grow_memory[0], 17);
    expect_int("preserve-255-after-clamp", grow_memory[255], 29);
    expect_int("preserve-300-after-clamp", grow_memory[300], 41);
    expect_fill_range(2048, MAXMEM);

    free(grow_memory);
    printf(
        "fortran grow failures=%d capacity=%d sentinels=%d,%d,%d\n",
        failures, grow_capacity, 17, 29, 41);
    return failures != 0;
}
