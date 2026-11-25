/* Object-like macros that name themselves are expanded once (syntax-crash
 * directive seeds 1001181 and 1009619). A body that starts with a number
 * or a float went through a path that did not disable the macro, and more
 * than 64 live expansions dropped the oldest guard, so both expanded
 * without bound. */
#include <stdio.h>

struct S { int v; int w; } G;
int N = 3;
float F = 2.0f;

#define v G.v
#define w G.w
#define N -1 * N
#define F 1.5f * F

int main(void)
{
    v = v + 1; w = w + v;  v = v + 1; w = w + v;  v = v + 1; w = w + v;
    v = v + 1; w = w + v;  v = v + 1; w = w + v;  v = v + 1; w = w + v;
    v = v + 1; w = w + v;  v = v + 1; w = w + v;  v = v + 1; w = w + v;
    v = v + 1; w = w + v;  v = v + 1; w = w + v;  v = v + 1; w = w + v;
    v = v + 1; w = w + v;  v = v + 1; w = w + v;  v = v + 1; w = w + v;
    v = v + 1; w = w + v;  v = v + 1; w = w + v;  v = v + 1; w = w + v;
    v = v + 1; w = w + v;  v = v + 1; w = w + v;  v = v + 1; w = w + v;
    v = v + 1; w = w + v;  v = v + 1; w = w + v;  v = v + 1; w = w + v;
    v = v + 1; w = w + v;  v = v + 1; w = w + v;  v = v + 1; w = w + v;
    v = v + 1; w = w + v;  v = v + 1; w = w + v;  v = v + 1; w = w + v;
    printf("members %d %d\n", v, w);
    printf("numbers %d %d\n", N, (int)(F * 2));
    return 0;
}
