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

int main(void)
{
    int a = sum_prefix_int(4);      /* 1+2+3+4 = 10 */
    int b = sum_postfix_int(4);     /* 10 */
    int c = walk_prefix_ptr("hello", 5);  /* 4 */
    int d = walk_postfix_ptr("hello", 5); /* 4 */

    printf("tforinc %d,%d,%d,%d\n", a, b, c, d);
    return !(a == 10 && b == 10 && c == 4 && d == 4);
}
