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

struct Mix {
    struct Pair p;
    long z;
    float f;
};

static int failures;

static void check_int(int got, int want, const char *name)
{
    if (got != want) {
        printf("FAIL %s got=%d want=%d\n", name, got, want);
        failures++;
    }
}

static void check_long(long got, long want, const char *name)
{
    if (got != want) {
        printf("FAIL %s\n", name);
        failures++;
    }
}

static void check_float(float got, float want, const char *name)
{
    float diff = got - want;
    if (diff < 0.0)
        diff = -diff;
    if (diff > 0.001) {
        printf("FAIL %s\n", name);
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

static int pair_sum(struct Pair p)
{
    return p.a + p.b;
}

static struct Pair echo_pair(struct Pair p)
{
    return p;
}

static long add_two_long(long v)
{
    return v + 2L;
}

static float add_quarter(float v)
{
    return v + 0.25;
}

static void check_value_literals(void)
{
    struct Pair init = (struct Pair){ 12, 34 };
    struct Pair assigned;
    struct Pair echoed;
    int *ip;
    long *lp;

    assigned = (struct Pair){ .b = 78, .a = 56 };
    echoed = echo_pair((struct Pair){ 90, 10 });
    ip = &(int){ 44 };
    lp = &(long){ 123456L };

    check_int(pair_sum((struct Pair){ 6, 7 }), 13, "struct literal argument");
    check_pair(&init, 12, 34, "struct literal initializer");
    check_pair(&assigned, 56, 78, "struct literal assignment");
    check_pair(&echoed, 90, 10, "struct literal echo");
    check_int((struct Pair){ 22, 33 }.a, 22, "struct literal member a");
    check_int((struct Holder){ { 1, 2 }, { 3, 4, 5 }, 0 }.pair.b,
              2, "nested literal member");
    check_int((int){ 5 } + 1, 6, "scalar int literal value");
    check_long((long){ 70000L } + 3L, 70003L, "scalar long literal value");
    check_float((float){ 1.5 } + 0.25, 1.75, "scalar float literal value");
    check_long(add_two_long((long){ 8L }), 10L, "scalar long literal argument");
    check_float(add_quarter((float){ 2.0 }), 2.25, "scalar float literal argument");
    check_int(*ip, 44, "address-taken int literal still works");
    check_long(*lp, 123456L, "address-taken long literal still works");
}

static int two_pairs(struct Pair a, struct Pair b)
{
    return a.a * a.b + b.a * b.b;
}

static int sum_pair_val(struct Pair p)
{
    return p.a + p.b;
}

static struct Pair pick_pair(void)
{
    return (struct Pair){ 8, 9 };
}

static void check_value_literals_extra(void)
{
    struct Pair returned;

    /* Multiple struct compound-literal arguments in a single call. */
    check_int(two_pairs((struct Pair){ 2, 3 }, (struct Pair){ 4, 5 }),
              26, "two struct literal arguments");

    /* long / float fields read directly from a struct literal. */
    check_long((struct Mix){ { 1, 2 }, 90000L, 1.5 }.z,
               90000L, "long field of struct literal");
    check_float((struct Mix){ { 1, 2 }, 90000L, 2.5 }.f,
                2.5, "float field of struct literal");

    /* A struct-typed field of a literal passed by value. */
    check_int(sum_pair_val((struct Mix){ { 3, 4 }, 0L, 0.0 }.p),
              7, "struct field of literal by value");

    /* A function that returns a compound literal directly. */
    returned = pick_pair();
    check_pair(&returned, 8, 9, "return compound literal");

    /* Scalar _Bool compound literal normalizes a nonzero value to 1
     * (initializer context). */
    {
        int flag = (_Bool){ 5 };
        check_int(flag, 1, "scalar _Bool literal normalizes");
    }

    /* Pointer-typed compound literal used as a value (initializer context). */
    {
        int x = 77;
        int *xp = (int *){ &x };
        check_int(*xp, 77, "pointer-typed literal value");
    }
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
    check_value_literals();
    check_value_literals_extra();
    check_nested_literals();
    if (failures == 0)
        printf("test tclit completed with great success\n");
    return failures;
}