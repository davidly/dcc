#include <stdio.h>

#ifdef LOCALINIT_RENAMED
#define local_initializer_small local_initializer_small_renamed
#define local_initializer_large local_initializer_large_renamed
#endif

#ifdef LOCALINIT_CHANGED_VALUES
#define SMALL_X 8
#define LARGE_PAIR_A 61
#else
#define SMALL_X 7
#define LARGE_PAIR_A 21
#endif

#ifdef LOCALINIT_VOLATILE
#define LOCAL_QUALIFIER volatile
#else
#define LOCAL_QUALIFIER
#endif

#ifdef _DCC_
#define TEST_UINT32_MAX 0xffffffffUL
#else
#define TEST_UINT32_MAX 0xffffffffU
#endif

struct Pair {
    int a;
    int b;
};

struct Rect {
    int w;
    int h;
};

struct Grid {
    int n;
    struct Rect cells[3];
};

static int failures;
static int checks;
static unsigned long checksum;
#ifdef LOCALINIT_EXTRA_CFG
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

static void check_small(const char *name, int got, int want)
{
    record_check(name, got, want);
}

static void check_large(int got, int want, const char *name)
{
    record_check(name, got, want);
}

static void local_initializer_small(void)
{
    int *p;
    int a;
    int arr[3];
    int x;

    x = SMALL_X;
    p = &x;
    a = 5;
    arr[0] = 10;
    arr[1] = 11;
    arr[2] = 12;

#ifdef LOCALINIT_EXTRA_CFG
    if (extra_gate)
        failures++;
#endif
    check_small("multi_ptr", *p, SMALL_X);
    check_small("multi_scalar", a, 5);
    check_small("multi_array", arr[2], 12);
}

static void local_initializer_large(void)
{
    LOCAL_QUALIFIER struct Pair local_pair = {
        .b = 22, .a = LARGE_PAIR_A
    };
    struct Pair local_pairs[2] = {
        [1] = { .b = 42, .a = 41 },
        [0] = { 39, 40 }
    };
    int local_values[4] = {
        [TEST_UINT32_MAX + 3] = 32,
        [0] = 30, 31, [3] = 33
    };
    struct Grid local_grid = {
        .n = 3,
        .cells[1] = { 2, 9 },
        .cells[0].w = 1
    };
    static const struct Pair static_pairs[] = {
        { .b = 52, .a = 51 },
        { .b = 54, .a = 53 }
    };

#ifdef LOCALINIT_EXTRA_CFG
    if (extra_gate)
        failures++;
#endif
    check_large(local_pair.a, LARGE_PAIR_A, "local_pair.a");
    check_large(local_pair.b, 22, "local_pair.b");
    check_large(local_pairs[0].a, 39, "local_pairs[0].a");
    check_large(local_pairs[0].b, 40, "local_pairs[0].b");
    check_large(local_pairs[1].a, 41, "local_pairs[1].a");
    check_large(local_pairs[1].b, 42, "local_pairs[1].b");
    check_large(local_values[0], 30, "local_values[0]");
    check_large(local_values[1], 31, "local_values[1]");
    check_large(local_values[2], 32, "local_values[2]");
    check_large(local_values[3], 33, "local_values[3]");
    check_large(local_grid.n, 3, "local_grid.n");
    check_large(local_grid.cells[0].w, 1, "local_grid.cells[0].w");
    check_large(local_grid.cells[0].h, 0, "local_grid.cells[0].h");
    check_large(local_grid.cells[1].w, 2, "local_grid.cells[1].w");
    check_large(local_grid.cells[1].h, 9, "local_grid.cells[1].h");
    check_large(local_grid.cells[2].w, 0, "local_grid.cells[2].w");
    check_large(static_pairs[0].a, 51, "static_pairs[0].a");
    check_large(static_pairs[0].b, 52, "static_pairs[0].b");
    check_large(static_pairs[1].a, 53, "static_pairs[1].a");
    check_large(static_pairs[1].b, 54, "static_pairs[1].b");
}

int main(void)
{
    local_initializer_small();
    local_initializer_large();
    printf("local initializer failures=%d checks=%d checksum=%lu\n",
           failures, checks, checksum);
    return failures;
}
