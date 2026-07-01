/* tc99init.c - C99 designated initializers and compound literals. */
#include <stdio.h>

struct Pair {
    int a;
    int b;
};

struct Holder {
    struct Pair pair;
    struct Pair *ptr;
    int values[3];
};

static int failures;
static int target = 27;

static struct Pair reordered = { .b = 20, .a = 10 };
static struct Pair *literal_pair = &(struct Pair){ .b = 4, .a = 3 };
static struct Pair pairs[2] = { [1] = { .b = 8, .a = 7 }, [0] = { 5, 6 } };
static struct Holder holder = {
    .values = { [2] = 12, [0] = 10, 11 },
    .ptr = &reordered,
    .pair = { .b = 2, .a = 1 }
};
static struct Holder *literal_holder = &(struct Holder){
    { .b = 14, .a = 13 },
    &reordered,
    { [0] = 15, 16, [2] = 17 }
};
static int grid[][3][5] = {
    {
        { 0, 0, 3, 5 },
        { 1, [3] = 6, 7 },
    },
    {
        { 1, 2 },
        { [4] = 7 },
    },
};

static void check_int(int got, int want, const char *name)
{
    if (got != want) {
        printf("FAIL %s got=%d want=%d\n", name, got, want);
        failures++;
    }
}

static void check_local_designators(void)
{
    struct Pair local_pair = { .b = 22, .a = 21 };
    struct Pair local_pairs[2] = { [1] = { .b = 42, .a = 41 }, [0] = { 39, 40 } };
    int local_values[4] = { [2] = 32, [0] = 30, 31, [3] = 33 };

    check_int(local_pair.a, 21, "local_pair.a");
    check_int(local_pair.b, 22, "local_pair.b");
    check_int(local_pairs[0].a, 39, "local_pairs[0].a");
    check_int(local_pairs[0].b, 40, "local_pairs[0].b");
    check_int(local_pairs[1].a, 41, "local_pairs[1].a");
    check_int(local_pairs[1].b, 42, "local_pairs[1].b");
    check_int(local_values[0], 30, "local_values[0]");
    check_int(local_values[1], 31, "local_values[1]");
    check_int(local_values[2], 32, "local_values[2]");
    check_int(local_values[3], 33, "local_values[3]");
}

int main(void)
{
    check_int(reordered.a, 10, "reordered.a");
    check_int(reordered.b, 20, "reordered.b");
    check_int(literal_pair->a, 3, "literal_pair.a");
    check_int(literal_pair->b, 4, "literal_pair.b");
    check_int(pairs[0].a, 5, "pairs[0].a");
    check_int(pairs[0].b, 6, "pairs[0].b");
    check_int(pairs[1].a, 7, "pairs[1].a");
    check_int(pairs[1].b, 8, "pairs[1].b");
    check_int(holder.pair.a, 1, "holder.pair.a");
    check_int(holder.pair.b, 2, "holder.pair.b");
    check_int(holder.ptr->a, 10, "holder.ptr.a");
    check_int(holder.values[0], 10, "holder.values[0]");
    check_int(holder.values[1], 11, "holder.values[1]");
    check_int(holder.values[2], 12, "holder.values[2]");
    check_int(literal_holder->pair.a, 13, "literal_holder.pair.a");
    check_int(literal_holder->pair.b, 14, "literal_holder.pair.b");
    check_int(literal_holder->values[0], 15, "literal_holder.values[0]");
    check_int(literal_holder->values[1], 16, "literal_holder.values[1]");
    check_int(literal_holder->values[2], 17, "literal_holder.values[2]");
    check_int(grid[0][1][3], 6, "grid[0][1][3]");
    check_int(grid[0][1][4], 7, "grid[0][1][4]");
    check_int(grid[1][1][4], 7, "grid[1][1][4]");
    check_int(target, 27, "target");
    check_local_designators();

    if (failures == 0)
        printf("test tc99init completed with great success\n");
    return failures;
}