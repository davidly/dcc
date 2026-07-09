/* C89 declaration syntax regression test */
#include <stdio.h>

static int g_failed = 0;

static void vri(int actual, int expected, const char *name)
{
    if (actual != expected) {
        printf("FAIL decl %s got %d expected %d\n", name, actual, expected);
        g_failed++;
    }
}

int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }
int mul2(int x) { return x * 2; }

/* Raw and typedefed function-pointer declarators. */
int (*fp)(int, int);
int (*ops[2])(int, int);
typedef int (*binop_t)(int, int);
binop_t top;
binop_t tops[2];

/* Abstract function-pointer declarator in a prototype. */
int call2(int (*)(int, int), int, int);

/* Arrays of pointers and pointer-to-array declarators. */
int a0 = 10;
int a1 = 20;
int *pa[2];
int mat[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };
int (*rowp)[3];
int (*matp)[2][3];

int call2(int (*f)(int, int), int x, int y)
{
    return f(x, y);
}

int sum_row(int (*rp)[3], int row)
{
    return rp[row][0] + rp[row][1] + rp[row][2];
}

struct PickNode {
    int value;
};

static struct PickNode pick_node = { 101 };
typedef const struct PickNode *(*pick_node_fn)(const struct PickNode *node, int seed);

static const struct PickNode *pick_same_node(const struct PickNode *node, int seed)
{
    (void) seed;
    return node;
}

static const struct PickNode *pick_global_node(const struct PickNode *node, int seed)
{
    (void) node;
    (void) seed;
    return &pick_node;
}

static int local_structptr_fnptr_array(void)
{
    pick_node_fn pickers[2] = { pick_same_node, pick_global_node };
    (void) pickers;
    return 202;
}

struct AutoTask {
    const char *name;
    int priority;
    _Bool done;
};

static int count_open_tasks(const struct AutoTask *tasks, int count)
{
    int i;
    int open;

    open = 0;
    for (i = 0; i < count; ++i) {
        if (!tasks[i].done)
            ++open;
    }
    return open;
}

static const struct AutoTask *highest_open_task(const struct AutoTask *tasks, int count)
{
    int i;
    const struct AutoTask *best;

    best = 0;
    for (i = 0; i < count; ++i) {
        if (!tasks[i].done && (best == 0 || tasks[i].priority > best->priority))
            best = &tasks[i];
    }
    return best;
}

static int auto_mixed_struct_array_init(void)
{
    struct AutoTask tasks[] = {
        { "parse", 2, 1 },
        { "build", 3, 1 },
        { "run",   5, 0 },
        { "log",   1, 0 }
    };
    const struct AutoTask *next;

    next = highest_open_task(tasks, 4);
    if (count_open_tasks(tasks, 4) != 2)
        return 10 + count_open_tasks(tasks, 4);
    if (next == 0)
        return 20;
    if (next->priority != 5)
        return 30 + next->priority;
    if (next->name[0] != 'r' || next->name[1] != 'u' || next->name[2] != 'n' || next->name[3] != 0)
        return 40;
    return 1;
}

static int lpa(void)
{
    int local[2][3];
    int (*lp)[3];

    local[0][0] = 7;
    local[0][1] = 8;
    local[0][2] = 9;
    local[1][0] = 11;
    local[1][1] = 12;
    local[1][2] = 13;

    lp = local;
    if (lp[1][2] != 13)
        return 10 + lp[1][2];
    if (sum_row(lp, 0) != 24)
        return 20 + sum_row(lp, 0);
    return 1;
}

static unsigned char (*glyph_matrix(void))[4]
{
    static unsigned char matrix[3][4] = {
        { 'N', 'E', 'S', 'W' },
        { 'R', 'I', 'N', 'G' },
        { 'Z', '8', '0', '!' }
    };

    return matrix;
}

int main(void)
{
    int r;
    unsigned char (*glyph_rows)[4];

    fp = add;
    ops[0] = add;
    ops[1] = sub;
    top = sub;
    tops[0] = add;
    tops[1] = sub;

    pa[0] = &a0;
    pa[1] = &a1;
    rowp = mat;
    matp = &mat;

    vri(fp(2, 3), 5, "fp-call");
    vri(ops[1](7, 2), 5, "funcptr-array-call");
    vri(top(9, 4), 5, "typedef-funcptr-call");
    vri(tops[0](2, 3), 5, "typedef-funcptr-array-call");
    vri(call2(add, 4, 5), 9, "abstract-funcptr-param");

    vri(*pa[1], 20, "array-of-pointers");
    vri(rowp[1][2], 6, "pointer-to-array-index");
    vri((*matp)[0][2], 3, "pointer-to-multidim-array-deref");

    r = lpa();
    vri(r, 1, "local-pointer-to-array");
    vri(local_structptr_fnptr_array(), 202, "local-structptr-fnptr-array");
    vri(auto_mixed_struct_array_init(), 1, "auto-mixed-struct-array-init");

    glyph_rows = glyph_matrix();
    vri(glyph_rows[0][0], 'N', "func-return-ptr-array-first");
    vri(glyph_rows[1][2], 'N', "func-return-ptr-array-stride");
    vri(glyph_rows[2][3], '!', "func-return-ptr-array-last");

    if (g_failed) {
        printf("declaration syntax test failed: %d\n", g_failed);
        return 1;
    }

    printf("declaration syntax test passed with great success\n");
    return 0;
}
