#include <stdio.h>
static int sizes[] = { 1, 2, 3, 4, 0 };
static int st[16];
static int sp;
static void run_test(int size)
{
    st[sp++] = size + 100;
    st[sp++] = size;
    switch (size) {
    case 1: { int t = st[--sp]; st[sp-1] += t; } break;
    case 2: { int t = st[--sp]; st[sp-1] -= t; } break;
    case 3: { int t = st[--sp]; st[sp-1] *= t; } break;
    default: { int t = st[--sp]; st[sp-1] &= t; } break;
    }
    printf("step %d value %d\n", size, st[--sp]);
}
int main(void)
{
    int i;
    printf("GIY start\n");
    for (i = 0; sizes[i] != 0; i++)
        run_test(sizes[i]);
    printf("GIY done\n");
    return 0;
}
