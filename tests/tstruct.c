#include <stdio.h>
#include <string.h>

struct Pair {
    char a;
    int b;
    char c;
};

struct Node {
    int value;
    struct Pair *pairp;
};

struct Pair g_pair;
struct Pair g_arr[2];
struct Node g_node;

#include <stdio.h>

struct Pair ret_arr_elem(void)
{
    return g_arr[1];
}

struct Pair ret_pair_value(int b)
{
    struct Pair p;
    p.a = 9;
    p.b = b;
    p.c = 10;
    return p;
}

void test2()
{
    /* 
     * In C89, all declarations must be at the very top of the block.
     * Mixing register variables, auto structs, and pointers here.
     */
    register char *i, *j, *x, *r;
    
    auto struct stk {
        char *l;
        char *r;
    } stack[16];
    
    struct stk *sp;

    /* Initialize register string pointers */
    i = "Karina";
    j = "Winter";
    x = "Giselle";
    r = "Ningning";

    /* Initialize the pointer to point to the first element of the auto array */
    sp = &stack[0];

    /* Assign values to the structure fields via the pointer */
    sp->l = i;
    sp->r = j;

    /* Advance the pointer to the next element in the stack array */
    sp++;
    sp->l = x;
    sp->r = r;

    /* Print values using the original array to verify storage works */
    printf("Stack[0]: %s, %s\n", stack[0].l, stack[0].r);
    printf("Stack[1]: %s, %s\n", stack[1].l, stack[1].r);

    return;
}

int main()
{
    struct Pair local;
    struct Pair copy1;
    struct Pair copy2;
    struct Pair *pp;

    g_pair.a = 3;
    g_pair.b = 1000;
    g_pair.c = 7;

    local.a = 4;
    local.b = 2000;
    local.c = 8;

    g_arr[0].a = 1;
    g_arr[0].b = 11;
    g_arr[0].c = 2;

    g_arr[1].a = 5;
    g_arr[1].b = 22;
    g_arr[1].c = 6;

    pp = &g_arr[1];

    g_node.value = 1234;
    g_node.pairp = pp;

    printf("g=%d %d %d\n", g_pair.a, g_pair.b, g_pair.c);
    printf("l=%d %d %d\n", local.a, local.b, local.c);
    printf("a=%d %d %d\n", g_arr[1].a, g_arr[1].b, g_arr[1].c);
    printf("p=%d %d %d\n", pp->a, pp->b, pp->c);
    printf("n=%d %d\n", g_node.value, g_node.pairp->b);

    copy1 = copy2 = local;
    printf("chain=%d %d %d %d\n", copy1.b, copy2.b, copy1.c, copy2.c);

    copy1 = ret_arr_elem();
    printf("retarr=%d %d %d\n", copy1.a, copy1.b, copy1.c);
    ret_pair_value(77);
    printf("discard struct return ok\n");

    test2();

    printf( "tstruct completed with great success\n" );
    return 0;
}
