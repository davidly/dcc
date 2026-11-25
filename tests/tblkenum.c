/* Enums declared in nested blocks (BUG-096). Their constants are in scope
 * from their declaration to the end of the block, may be reused by sibling
 * blocks and other functions, and are shadowed by an inner block's local
 * or enum of the same name. */
#include <stdio.h>

enum { G = 100 };

static int in_case(int x)
{
    switch (x) {
    case 1: {
        enum { A = 4, B } e = B;
        x = A + e;
        break;
    }
    }
    return x;
}

static int scoped(int x)
{
    { enum { A = 1, B } e = B; x += A * 10 + e; }
    { enum { A = 5 } f = A; x += f; }
    {
        enum { K = 3 };
        int arr[K];
        arr[K - 1] = 7;
        x += arr[2] + (int)(sizeof arr / sizeof arr[0]);
    }
    {
        enum { P = 2, Q = 9 };
        switch (x & 3) {
        case P: x += Q; break;
        case Q & 3: x += 1; break;
        default: x += P;
        }
        { int Q = 50; x += Q; }
        { enum { P = 40 }; x += P; }
        x += P;
    }
    for (int i = 0; i < 2; ++i) { enum { S = 4 }; x += S + i; }
    x += G;
    return x;
}

static int reused(int x)
{
    { enum { A = 7, B } e = A; x += e + B; }
    if (x) { enum E2 { M = 3 } m = M; x += m; }
    return x;
}

int main(void)
{
    printf("case %d\n", in_case(1));
    printf("scoped %d\n", scoped(0));
    printf("reused %d\n", reused(1));
    return 0;
}
