#include <stdio.h>

/* for-init increment/decrement forms: prefix and postfix, int and pointer
 * inductions. Postfix pointer for-init (`for (p--; ...)`) previously compiled
 * only in prefix form; this pins all four shapes. */

static int sum_prefix_int(int n)
{
    int t = 0;
    int i = 0;
    for (++i; i <= n; ++i)          /* prefix int: i = 1..n */
        t += i;
    return t;
}

static int sum_postfix_int(int n)
{
    int t = 0;
    int i = 0;
    for (i++; i <= n; i++)          /* postfix int: i = 1..n */
        t += i;
    return t;
}

static int walk_prefix_ptr(const char *s, int len)
{
    const char *p = s + len;
    int steps = 0;
    for (--p; p != s; --p)          /* prefix pointer */
        steps++;
    return steps;
}

static int walk_postfix_ptr(const char *s, int len)
{
    const char *p = s + len;
    int steps = 0;
    for (p--; p != s; p--)          /* postfix pointer */
        steps++;
    return steps;
}

/* Compound-assignment for-init forms: the init clause is a full expression
 * (C89 6.6.5 / C99-C11 6.8.5 `for ( expression_opt ; ...`), so a compound
 * assignment is as valid there as in the increment clause or a statement. */
static int mul_init_int(int n)
{
    int t = 0;
    int i = 1;
    for (i *= 2; i <= n; i++)       /* i starts at 2 */
        t += i;
    return t;
}

static int deref_compound_init(void)
{
    int v = 10;
    int *p = &v;
    int t = 0;
    for (*p -= 6; *p < 8; (*p)++)   /* v: 4,5,6,7 */
        t += *p;
    return t;
}

static int index_compound_init(void)
{
    int a[3];
    int t = 0;
    a[1] = 0;
    for (a[1] += 2; a[1] < 6; a[1]++) /* 2,3,4,5 */
        t += a[1];
    return t;
}

int main(void)
{
    int a = sum_prefix_int(4);      /* 1+2+3+4 = 10 */
    int b = sum_postfix_int(4);     /* 10 */
    int c = walk_prefix_ptr("hello", 5);  /* 4 */
    int d = walk_postfix_ptr("hello", 5); /* 4 */
    int e = mul_init_int(5);        /* 2+3+4+5 = 14 */
    int f = deref_compound_init();  /* 4+5+6+7 = 22 */
    int g = index_compound_init();  /* 2+3+4+5 = 14 */

    printf("tforinc %d,%d,%d,%d,%d,%d,%d\n", a, b, c, d, e, f, g);
    return !(a == 10 && b == 10 && c == 4 && d == 4 &&
             e == 14 && f == 22 && g == 14);
}
