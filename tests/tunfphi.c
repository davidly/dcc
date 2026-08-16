/*
 * tunfphi.c - cyclic loop-value PHI regression.
 * Generated archive case: batch9/c8922.
 */
#include <stdio.h>

static int root(int parent[], int node)
{
    while (parent[node] != node) {
        parent[node] = parent[parent[node]];
        node = parent[node];
    }
    return node;
}

static void unite(int parent[], int first, int second)
{
    int ra = root(parent, first);
    int rb = root(parent, second);

    if (ra != rb)
        parent[rb] = ra;
}

int main(void)
{
    int parent[6], i, a, b;

    for (i = 0; i < 6; ++i)
        parent[i] = i;
    unite(parent, 0, 1);
    unite(parent, 1, 2);
    unite(parent, 4, 5);
    a = root(parent, 0) == root(parent, 2);
    b = root(parent, 0) == root(parent, 5);
    printf("tunfphi union=%d,%d\n", a, b);
    return !(a && !b);
}
