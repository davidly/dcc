/* Numeric suffixes remain part of their preprocessing token when a
 * function-like macro has a parameter named f, u, L or ul. Each macro
 * preserves its constant and substitutes the separate parameter token. */
#include <stdio.h>

static float g;
static unsigned n;
static long m;

static void a1(int x) { g += (float)x; }

#define T(f) g = 9.0f; f(1)
#define U(u) n = 10u; u
#define L(L) m = 10L; L
#define UL(ul) m = (long)40ul; ul

int main(void)
{
    T(a1);
    U(n += 2;)
    L(m += 3;)
    UL(m += 4;)
    printf("%d %u %ld\n", (int)g, n, m);
    return 0;
}
