/* tclit.c - block-scope compound literals. */
#include <stdio.h>

struct Pair {
    int a;
    int b;
};

struct Holder {
    struct Pair pair;
    int values[3];
    struct Pair *ptr;
};

static int failures;

static void check_int(int got, int want, const char *name)
{
    if (got != want) {
        printf("FAIL %s got=%d want=%d\n", name, got, want);
        failures++;
    }
}

static void check_pair(struct Pair *p, int a, int b, const char *name)
{
    check_int(p->a, a, name);
    check_int(p->b, b, name);
}

static void check_block_literals(void)
{
    struct Pair *first;
    struct Pair *second;
    struct Holder *holder;
    int *ip;

    first = &(struct Pair){ .b = 20, .a = 10 };
    second = &(struct Pair){ 30, 40 };
    first->a = 11;

    holder = &(struct Holder){
        { .b = 2, .a = 1 },
        { [2] = 7, [0] = 5, 6 },
        first
    };

    ip = &(int){ 1234 };
    *ip = *ip + 1;

    check_pair(first, 11, 20, "first");
    check_pair(second, 30, 40, "second");
    check_int(holder->pair.a, 1, "holder.pair.a");
    check_int(holder->pair.b, 2, "holder.pair.b");
    check_int(holder->values[0], 5, "holder.values[0]");
    check_int(holder->values[1], 6, "holder.values[1]");
    check_int(holder->values[2], 7, "holder.values[2]");
    check_int(holder->ptr->a, 11, "holder.ptr.a");
    check_int(*ip, 1235, "scalar literal");
}

int main(void)
{
    check_block_literals();
    if (failures == 0)
        printf("test tclit completed with great success\n");
    return failures;
}