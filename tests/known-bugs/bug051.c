/* BUG-051: a function-like macro parameter whose name is the suffix of a
 * numeric constant in the replacement list is substituted inside that
 * constant. `#define T(f) g = 9.0f; f(1)` expands to `g = 9.0a1; a1(1)`
 * and `#define U(u) n = 10u; u` to `n = 10n = 2;; …`. The suffix letters
 * are lexed as a separate identifier instead of being part of the
 * pp-number, so `f`, `u`, `l`, `L`, `U`, `F` and their combinations are
 * all affected. A suffix that is not a parameter (`1e5` with parameter
 * `e`, `0x10` with parameter `x`) is fine. */
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
