/* tdivmod.c - isolated correctness test for DCCRTL.MAC's fused divmod
 * primitives (__udivmod, __sdivmod), added for the compiler's upcoming
 * a%b/a/b fusion optimization (see dcc_ast_gen_support.c). Calls them
 * directly via #asm wrapper functions rather than through C's % and /
 * operators, since no C-level construct reaches them yet - the fusion
 * pass that will do that doesn't exist until this RTL is proven correct
 * on its own first.
 *
 * All expected values are literal constants independently computed via
 * Python (truncating division, dividend-sign remainder - matching C's own
 * semantics for %/ on negative operands), not derived by calling the
 * operators under test - a circular check would only prove self-
 * consistency, not correctness (this exact mistake was caught and fixed
 * twice earlier in this project's history; the same discipline applies
 * here from the start).
 */

#include <stdio.h>

extern unsigned int t_udiv(unsigned int dividend, unsigned int divisor);
extern int t_sdiv(int dividend, int divisor);
extern int g_rem;

#asm
        extrn   __udivmod
        extrn   __sdivmod

        public  _g_rem
_g_rem:
        dw      0

        public  _t_udiv
_t_udiv:
        ; unsigned int t_udiv(unsigned int dividend, unsigned int divisor)
        ; returns quotient in HL; remainder stashed in g_rem for the caller.
        push    ix
        ld      ix,0
        add     ix,sp
        ld      l,(ix+4)
        ld      h,(ix+5)
        ld      e,(ix+6)
        ld      d,(ix+7)
        call    __udivmod
        ld      (_g_rem),de
        ld      sp,ix
        pop     ix
        ret

        public  _t_sdiv
_t_sdiv:
        ; int t_sdiv(int dividend, int divisor)
        ; returns quotient in HL; remainder stashed in g_rem for the caller.
        push    ix
        ld      ix,0
        add     ix,sp
        ld      l,(ix+4)
        ld      h,(ix+5)
        ld      e,(ix+6)
        ld      d,(ix+7)
        call    __sdivmod
        ld      (_g_rem),de
        ld      sp,ix
        pop     ix
        ret
#endasm

static int checks = 0, failures = 0;

static void oku(unsigned int a, unsigned int b, unsigned int wantq, unsigned int wantr)
{
    unsigned int q = t_udiv(a, b);
    unsigned int r = (unsigned int)g_rem;
    checks++;
    if (q != wantq || r != wantr) {
        failures++;
        printf("FAIL u %u/%u: got q=%u r=%u want q=%u r=%u\n", a, b, q, r, wantq, wantr);
    }
}

static void oks(int a, int b, int wantq, int wantr)
{
    int q = t_sdiv(a, b);
    int r = g_rem;
    checks++;
    if (q != wantq || r != wantr) {
        failures++;
        printf("FAIL s %d/%d: got q=%d r=%d want q=%d r=%d\n", a, b, q, r, wantq, wantr);
    }
}

int main(void)
{
    /* unsigned: boundary values, divide-by-values covering the
     * repeated-subtraction cap (40) both under and over, exact multiples,
     * divisor 1, dividend 0. */
    oku(0u, 1u, 0u, 0u);
    oku(1u, 1u, 1u, 0u);
    oku(7u, 3u, 2u, 1u);
    oku(65535u, 1u, 65535u, 0u);
    oku(65535u, 2u, 32767u, 1u);
    oku(65535u, 65535u, 1u, 0u);
    oku(100u, 7u, 14u, 2u);
    oku(0u, 7u, 0u, 0u);
    oku(1u, 65535u, 0u, 1u);
    oku(65535u, 40u, 1638u, 15u);
    oku(65535u, 41u, 1598u, 17u);
    oku(1000u, 3u, 333u, 1u);
    oku(9u, 10u, 0u, 9u);
    oku(10u, 9u, 1u, 1u);
    oku(39u, 1u, 39u, 0u);
    oku(40u, 1u, 40u, 0u);
    oku(41u, 1u, 41u, 0u);

    /* signed: both-positive fast path, all four sign combinations, the
     * INT_MIN edge cases (negating INT_MIN overflows in two's complement -
     * must still round-trip correctly through the double negation), and
     * a case where the truncated quotient is 0 despite a nonzero dividend. */
    oks(0, 1, 0, 0);
    oks(7, 3, 2, 1);
    oks(-7, 3, -2, -1);
    oks(7, -3, -2, 1);
    oks(-7, -3, 2, -1);
    oks(-32768, 1, -32768, 0);
    oks(32767, 1, 32767, 0);
    oks(1, -1, -1, 0);
    oks(-1, 1, -1, 0);
    oks(-1, -1, 1, 0);
    oks(100, -7, -14, 2);
    oks(-100, 7, -14, -2);
    oks(-100, -7, 14, -2);
    oks(0, -7, 0, 0);
    oks(-32768, 32767, -1, -1);
    oks(32767, -32768, 0, 32767);
    oks(9, -10, 0, 9);
    oks(-9, 10, 0, -9);

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
