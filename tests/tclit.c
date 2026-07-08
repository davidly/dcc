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

/* Nested, address-taken compound literals used to build a small tree inline,
 * then walked recursively. A field initialized to the address of another
 * compound literal (.left = &(struct Node){...}) re-enters the initializer
 * emitter, so this exercises both the root-address capture and the frame
 * reservation for every nested literal. */
struct Node {
    long v;
    const struct Node *left;
    const struct Node *right;
};

static long sum_tree(const struct Node *n)
{
    if (n == NULL)
        return 0;
    return n->v + sum_tree(n->left) + sum_tree(n->right);
}

static void check_nested_literals(void)
{
    const struct Node *tree = &(struct Node){
        .v = 1L,
        .left = &(struct Node){ .v = 20L },
        .right = &(struct Node){ .v = 300L }
    };
    /* Same shape, but the nested literals are designated out of source order:
     * the root must still resolve to the outer literal, not the last-built
     * nested one. */
    const struct Node *reordered = &(struct Node){
        .v = 7L,
        .right = &(struct Node){ .v = 500L },
        .left = &(struct Node){ .v = 40L }
    };

    check_int((int)tree->v, 1, "tree.v");
    check_int((int)tree->left->v, 20, "tree.left.v");
    check_int((int)tree->right->v, 300, "tree.right.v");
    check_int((int)sum_tree(tree), 321, "sum_tree");

    check_int((int)reordered->v, 7, "reordered.v");
    check_int((int)reordered->left->v, 40, "reordered.left.v");
    check_int((int)reordered->right->v, 500, "reordered.right.v");
    check_int((int)sum_tree(reordered), 547, "sum_tree reordered");
}

int main(void)
{
    check_block_literals();
    check_nested_literals();
    if (failures == 0)
        printf("test tclit completed with great success\n");
    return failures;
}