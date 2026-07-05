#include <stdio.h>

/* Guards dcc_array_narrow.c's scalar narrowing (try_narrow_register_scalar)
 * and dccpeep's pass_byte_loop_counter_to_reg_c, which promotes a narrowed
 * register-qualified byte loop counter into Z80 register C for a
 * self-guarding `while(--n)` loop whose only calls are __mods/__divs. */

int helper(int x, int y)
{
    return x + y;
}

/* No calls in the loop at all: n should narrow to unsigned char and the
 * loop counter should register-ify (trivially - no whitelist to check). */
int lres(void)
{
    register int n;
    int total;

    total = 0;
    n = 5;
    while (--n)
        total = total + n;
    return total;
}

/* A '%' call inside the loop: n should narrow and register-ify, since
 * __mods is on the peephole's whitelist of calls known to preserve BC. */
int lmod(void)
{
    register int n;
    int x;
    int total;

    total = 0;
    x = 17;
    n = 5;
    while (--n)
        total = total + (x % n);
    return total;
}

/* A call to an ordinary user function inside the loop: n should still
 * narrow to unsigned char (that part is independent of what the loop
 * calls), but the loop counter must NOT register-ify, since an arbitrary
 * function is not known to preserve C - the peephole must decline safely
 * here rather than risk it, and the result must still be correct either
 * way. */
int lusr(void)
{
    register int n;
    int total;

    total = 0;
    n = 5;
    while (--n)
        total = helper(total, n);
    return total;
}

/* Guards a real, reproduced bug: an unguarded increment (`i++`) on a
 * register-qualified scalar was invisible to the narrowing proof (only
 * `--i`/`i--` was ever checked), so `i` here was incorrectly narrowed to
 * unsigned char even though it climbs to 299 - wrapping at 256 and
 * silently corrupting the sum. Loop termination is deliberately driven by
 * `j` (unrelated to `i`) rather than by comparing `i` itself, so even a
 * regression of this fix gives a wrong SUM rather than an infinite loop
 * in the test suite. */
int lbig(void)
{
    register int i;
    int j;
    int total;

    total = 0;
    i = 0;
    for (j = 0; j < 300; j++) {
        total = total + i;
        i++;
    }
    return total;
}

int main()
{
    printf("a=%d\n", lres());
    printf("b=%d\n", lmod());
    printf("c=%d\n", lusr());
    printf("d=%d\n", lbig());
    return 0;
}
