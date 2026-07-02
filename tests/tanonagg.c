/* tanonagg.c - C11 anonymous struct/union member lookup and initialization. */
#include <stdio.h>

struct NestedAnon {
    int a;
    union {
        int b1;
        int b2;
    };
    struct {
        union {
            struct {
                int c;
            };
        };
    };
    struct {
        int d;
    };
};

struct Pair {
    int x;
    int y;
};

struct InitAnon {
    int a;
    int b;
    union {
        int c;
        int d;
    };
    struct Pair pair;
};

static int failures;
static struct NestedAnon global_nested = { 1, 2, 3, 4 };
static struct InitAnon global_init = { 10, 20, 30, { 40, 50 } };

static void check_int(const char *name, int got, int want)
{
    if (got != want) {
        printf("FAIL %s got=%d want=%d\n", name, got, want);
        failures++;
    }
}

static void check_global_initializers(void)
{
    check_int("global_nested.a", global_nested.a, 1);
    check_int("global_nested.b1", global_nested.b1, 2);
    check_int("global_nested.b2", global_nested.b2, 2);
    check_int("global_nested.c", global_nested.c, 3);
    check_int("global_nested.d", global_nested.d, 4);
    check_int("global_init.a", global_init.a, 10);
    check_int("global_init.b", global_init.b, 20);
    check_int("global_init.c", global_init.c, 30);
    check_int("global_init.d", global_init.d, 30);
    check_int("global_init.pair.x", global_init.pair.x, 40);
    check_int("global_init.pair.y", global_init.pair.y, 50);
}

static void check_local_initializers(void)
{
    struct NestedAnon nested;
    struct InitAnon init = { 11, 12, 13, { 14, 15 } };

    nested.a = 5;
    nested.b2 = 6;
    nested.c = 7;
    nested.d = 8;

    check_int("nested.a", nested.a, 5);
    check_int("nested.b1", nested.b1, 6);
    check_int("nested.b2", nested.b2, 6);
    check_int("nested.c", nested.c, 7);
    check_int("nested.d", nested.d, 8);
    check_int("init.a", init.a, 11);
    check_int("init.b", init.b, 12);
    check_int("init.c", init.c, 13);
    check_int("init.d", init.d, 13);
    check_int("init.pair.x", init.pair.x, 14);
    check_int("init.pair.y", init.pair.y, 15);
}

int main(void)
{
    check_global_initializers();
    check_local_initializers();

    if (failures == 0)
        printf("anonymous aggregate tests passed\n");
    return failures != 0;
}