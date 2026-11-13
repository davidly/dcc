/* BUG-052: in a loop, `a[1] = (0 ? K : x) - 1.0f;` with `x` a 32-bit
 * parameter that is assigned (and not read) after the loop is accumulated
 * into the parameter: each iteration subtracts 1 again, so three
 * iterations store x - 3. The constant condition is folded and the
 * parameter is then treated as the expression's own temporary. In a `for`
 * loop the low word of the float is also overwritten by the counter
 * (`c0560002` for -0.35f - 3). A `float` or `long` parameter is wrong; an
 * `int` one, a local copy of the parameter, `(x + 0.0f) - 1.0f` without
 * the `?:`, an element that varies with the counter (`a[i]`), a plain
 * global target, and reading `x` after the loop are all right. */
#include <stdio.h>
#include <stdint.h>

static float fa[4];
static long la[4];
static int ia[4];
static float g;

static void pf(const char *n, float f)
{
    union { float f; uint32_t u; } u;
    u.f = f;
    printf("%s %08lx\n", n, (unsigned long)u.u);
}

static void f1(float x) { int i; for (i = 0; i < 3; ++i) fa[1] = (0 ? 2.0f : x) - 1.0f; x = -1.0f; }
static void f2(float x) { int i; for (i = 0; i < 3; ++i) fa[1] = (1 ? x : 2.0f) - 1.0f; x = -1.0f; }
static void f3(float x) { int i; i = 0; while (i < 3) { fa[1] = (0 ? 2.0f : x) - 1.0f; i++; } x = g; }
static void f4(long x) { int i; for (i = 0; i < 3; ++i) la[1] = (0 ? 2L : x) - 1L; x = -1L; }
static void f5(int x) { int i; for (i = 0; i < 3; ++i) ia[1] = (0 ? 2 : x) - 1; x = -1; }
static void ok1(float x) { int i; for (i = 0; i < 3; ++i) fa[1] = (x + 0.0f) - 1.0f; x = -1.0f; }
static void ok2(float x) { float y = x; int i; for (i = 0; i < 3; ++i) fa[1] = (0 ? 2.0f : y) - 1.0f; y = -1.0f; }
static void ok3(float x) { int i; for (i = 0; i < 3; ++i) fa[1] = (0 ? 2.0f : x) - 1.0f; fa[2] = x; }

int main(void)
{
    f1(-0.35f); pf("f1", fa[1]);
    f2(-0.35f); pf("f2", fa[1]);
    f3(-0.35f); pf("f3", fa[1]);
    f4(5L); printf("f4 %ld\n", la[1]);
    f5(5); printf("f5 %d\n", ia[1]);
    ok1(-0.35f); pf("ok1", fa[1]);
    ok2(-0.35f); pf("ok2", fa[1]);
    ok3(-0.35f); pf("ok3", fa[1]); pf("ok3", fa[2]);
    return 0;
}
