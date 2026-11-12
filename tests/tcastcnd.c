/* tcastcnd.c - int expressions dcc used to reject
 *
 * Valid C that failed with "unsupported ... condition" or "unsupported
 * return expression": a cast as an if/while condition, int arithmetic on a
 * comparison or ?: of long operands, and constant && / ||, unary minus of
 * constant expressions and word shifts by long constant counts (as macros
 * produce).
 */

#include <stdio.h>

#define FLAG_A 1
#define FLAG_B 0
#define MASK 4
#define BIT 1
#define BIT7 (1U << 7L)
#define HALF (65535U >> 1L)

static int fails;

static void check(const char *name, long got, long want)
{
    if (got != want) {
        printf("FAIL %s: got %lx want %lx\n", name, got, want);
        ++fails;
    }
}

/* A cast as the controlling expression. */
static int if_schar(unsigned char c) { if ((signed char)c) return 1; return 0; }
static int if_int_of_long(long l) { if ((int)l) return 1; return 0; }
static int while_uchar(int n) { int steps = 0; while ((unsigned char)n) { --n; ++steps; } return steps; }

/* Int arithmetic whose operand is an int built from long operands. */
static unsigned add_cmp(int a, long b) { return a + (b == 0); }
static int mul_ne(int a, long b) { return a * (b != 0L); }
static unsigned or_shift_cmp(int a, long l) { return a | ((l < a) << 2); }
static int add_logical(int a, long b) { return a + (b && a); }
static int add_not(int a, long b) { return a + !b; }
static int add_ternary(int x, long lf) { return x + (lf ? 1 : 2); }
static int ne_ternary(int x, unsigned long u) { return x != (u ? x : 0); }

/* Constant logicals and negated constant expressions. */
static int const_or(void) { return FLAG_A || FLAG_B; }
static int const_and_sum(int x) { return x + (FLAG_A && 2); }
static int neg_const_or(void) { return -(MASK | BIT); }
static int neg_const_product(int x) { return x + -(2 * 3); }
static int assign_const_or(void) { int x; x = FLAG_B || FLAG_A; return x; }
static unsigned const_shift_store(void) { unsigned o; o = 65535U << 7L; return o; }

int main(void)
{
    check("if_schar_0", if_schar(0), 0L);
    check("if_schar_80", if_schar(0x80), 1L);
    check("if_int_of_long", if_int_of_long(0x10000L), 0L);
    check("if_int_of_long_1", if_int_of_long(0x10001L), 1L);
    check("while_uchar", while_uchar(0x103), 3L);
    check("add_cmp", add_cmp(5, 0L), 6L);
    check("add_cmp_wide", add_cmp(5, 0x10000L), 5L);
    check("mul_ne", mul_ne(7, 0x10000L), 7L);
    check("or_shift_cmp", or_shift_cmp(1, -5L), 5L);
    check("add_logical", add_logical(3, 0x10000L), 4L);
    check("add_not", add_not(3, 0x10000L), 3L);
    check("add_ternary", add_ternary(10, 0x10000L), 11L);
    check("add_ternary_0", add_ternary(10, 0L), 12L);
    check("ne_ternary", ne_ternary(4, 0UL), 1L);
    check("const_or", const_or(), 1L);
    check("const_and_sum", const_and_sum(4), 5L);
    check("neg_const_or", neg_const_or(), -5L);
    check("neg_const_product", neg_const_product(10), 4L);
    check("assign_const_or", assign_const_or(), 1L);
    check("const_shift_bit7", (long)BIT7, 0x80L);
    check("const_shift_half", (long)HALF, 0x7fffL);
    check("const_shift_store", (long)const_shift_store(), 0xff80L);
    if (fails == 0)
        printf("tcastcnd passed\n");
    return fails;
}
