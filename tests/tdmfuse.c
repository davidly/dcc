/* tdivmodfuse.c - compiler-level correctness test for the AST-level %/ /
 * fusion pass (ast_divmod_fuse_compound, dcc_ast_gen_support.c), which
 * rewrites two adjacent `X = ... A%B ...;` / `Y = ... A/B ...;` statements
 * sharing bare-identifier operands A,B into a single __udivmod/__sdivmod
 * call - exercised here through real % and / source syntax (not the #asm
 * wrappers tdivmod.c uses to test the RTL primitives in isolation).
 *
 * All expected values are literal constants independently computed via
 * Python (truncating division, dividend-sign remainder, matching C's own
 * semantics), not derived by calling the operators under test.
 */

#include <stdio.h>

static int checks = 0, failures = 0;

static void ck(int got, int want, const char *label)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s: got %d want %d\n", label, got, want);
    }
}

/* Test 1: plain-int for-loop, both operands unregistered - the simplest
 * shape the detector should fuse. */
static void test_for_plain_int(void)
{
    int x, n, q, r;
    x = 1000;
    for (n = 7; n > 0; --n) {
        q = x % n;
        r = x / n;
        if (n == 7) { ck(q, 6, "t1n7q"); ck(r, 142, "t1n7r"); }
        if (n == 6) { ck(q, 4, "t1n6q"); ck(r, 23, "t1n6r"); }
        if (n == 5) { ck(q, 3, "t1n5q"); ck(r, 4, "t1n5r"); }
        if (n == 4) { ck(q, 0, "t1n4q"); ck(r, 1, "t1n4r"); }
        if (n == 3) { ck(q, 1, "t1n3q"); ck(r, 0, "t1n3r"); }
        if (n == 2) { ck(q, 0, "t1n2q"); ck(r, 0, "t1n2r"); }
        if (n == 1) { ck(q, 0, "t1n1q"); ck(r, 0, "t1n1r"); }
        x = r;
    }
}

/* Test 2: tests/e.c's own motivating shape - a `register int n` narrowed to
 * unsigned char by try_narrow_for_counter, the divmod pair nested two while
 * loops deep (not inside any for-loop at all), a[n]=x%n reading the
 * remainder and x=10*a[n-1]+x/n reading the quotient. Traces a scaled-down
 * digits-of-e spigot (N=10 instead of e.c's 200) and checks x after each
 * outer pass against an independently computed Python trace. */
static void test_while_register_narrowed(void)
{
    int N;
    register int n;
    int x;
    int a[10];
    int expect[7];
    int idx;

    expect[0] = 27; expect[1] = 1; expect[2] = 8; expect[3] = 2;
    expect[4] = 7;  expect[5] = 5; expect[6] = 0;

    N = 10;
    x = 0;
    for (n = N - 1; n > 0; --n)
        a[n] = 1;
    a[1] = 2;
    a[0] = 0;

    idx = 0;
    while (N > 3) {
        n = N--;
        while (--n) {
            a[n] = x % n;
            x = 10 * a[n-1] + x / n;
        }
        ck(x, expect[idx], "t2x");
        idx++;
    }
}

/* Test 3: reversed statement order (the / statement comes first, the %
 * statement second) - the detector must find the pair regardless of which
 * operator appears first. */
static void test_reversed_order(void)
{
    int x, n, q, r;
    x = 21321;
    for (n = 6; n > 0; --n) {
        r = x / n;
        q = x % n;
        if (n == 6) { ck(q, 3, "t3n6q"); ck(r, 3553, "t3n6r"); }
        if (n == 5) { ck(q, 3, "t3n5q"); ck(r, 710, "t3n5r"); }
        if (n == 4) { ck(q, 2, "t3n4q"); ck(r, 177, "t3n4r"); }
        if (n == 3) { ck(q, 0, "t3n3q"); ck(r, 59, "t3n3r"); }
        if (n == 2) { ck(q, 1, "t3n2q"); ck(r, 29, "t3n2r"); }
        if (n == 1) { ck(q, 0, "t3n1q"); ck(r, 29, "t3n1r"); }
        x = r;
    }
}

/* Test 4: unsigned operands, exercising __udivmod (not the signed wrapper). */
static void test_unsigned(void)
{
    unsigned int x, n, q, r;
    x = 60000u;
    for (n = 9u; n > 0u; --n) {
        q = x % n;
        r = x / n;
        if (n == 9u) { ck((int)q, 6, "t4n9q"); ck((int)r, 6666, "t4n9r"); }
        if (n == 8u) { ck((int)q, 2, "t4n8q"); ck((int)r, 833, "t4n8r"); }
        if (n == 7u) { ck((int)q, 0, "t4n7q"); ck((int)r, 119, "t4n7r"); }
        if (n == 6u) { ck((int)q, 5, "t4n6q"); ck((int)r, 19, "t4n6r"); }
        if (n == 5u) { ck((int)q, 4, "t4n5q"); ck((int)r, 3, "t4n5r"); }
        if (n == 4u) { ck((int)q, 3, "t4n4q"); ck((int)r, 0, "t4n4r"); }
        x = r;
    }
}

/* Test 5: signed operands with negative values on both sides, forced
 * through separate calls (not a shared running loop) so each pair is
 * independent - exercises __sdivmod's full sign-handling path (the fast
 * path only covers both-non-negative operands, unlike every other test
 * above). */
static void sdm_pair(int x, int n, int wantq, int wantr, const char *label)
{
    int q, r;
    q = x % n;
    r = x / n;
    ck(q, wantq, label);
}

static void sdm_pair_r(int x, int n, int wantr, const char *label)
{
    int q, r;
    q = x % n;
    r = x / n;
    ck(r, wantr, label);
}

static void test_signed_negative(void)
{
    sdm_pair(-1000, 7, -6, -142, "t5a-q");
    sdm_pair_r(-1000, 7, -142, "t5a-r");
    sdm_pair(1000, -7, 6, -142, "t5b-q");
    sdm_pair_r(1000, -7, -142, "t5b-r");
    sdm_pair(-1000, -7, -6, 142, "t5c-q");
    sdm_pair_r(-1000, -7, 142, "t5c-r");
    sdm_pair(-7, 3, -1, -2, "t5d-q");
    sdm_pair_r(-7, 3, -2, "t5d-r");
    sdm_pair(7, -3, 1, -2, "t5e-q");
    sdm_pair_r(7, -3, -2, "t5e-r");
    sdm_pair(-7, -3, -1, 2, "t5f-q");
    sdm_pair_r(-7, -3, 2, "t5f-r");
    sdm_pair(-32768, 3, -2, -10922, "t5g-q");
    sdm_pair_r(-32768, 3, -10922, "t5g-r");
}

/* Test 6a: the two %/ statements are not adjacent (a plain assignment sits
 * between them) - the detector's adjacent-pair scan must not match this at
 * all, so q reads the original n and r reads the reassigned n. */
static void test_nonadjacent_pair(void)
{
    int x, n, q, r;
    x = 500;
    n = 5;
    q = x % n;
    n = 99;
    r = x / n;
    ck(q, 0, "t6a-q");
    ck(r, 5, "t6a-r");
}

/* Test 6b: the two %/ statements ARE adjacent, but the FIRST statement
 * reassigns `n` (one of the shared operands) as its own target - the
 * detector must DECLINE here, since a fused call would capture n's value
 * before this reassignment, corrupting the second statement's result. If
 * the decline rule regresses, x below comes out 13 (using the stale n=5)
 * instead of the correct 18 (using the reassigned n=2). */
static void test_first_stmt_reassigns_operand(void)
{
    int x, n;
    x = 17;
    n = 5;
    n = x % n;
    x = 10 + x / n;
    ck(n, 2, "t6b-n");
    ck(x, 18, "t6b-x");
}

/* Test 7: a standalone nested compound is AST-emitted as a unit during real
 * codegen, so the frame-sizing scan must replay it too and reserve the two
 * hidden fused-result locals. */
static void test_bare_compound_frame(void)
{
    int x, n, q, r;
    x = 23;
    n = 5;
    q = 0;
    r = 0;
    {
        q = x % n;
        r = x / n;
    }
    ck(q, 3, "t7-q");
    ck(r, 4, "t7-r");
}

/* Test 8: the first assignment may modify a shared operand through an alias.
 * Fusion must decline because the following division observes the stored
 * remainder, not the operand value captured before that store. */
static void test_first_stmt_aliases_operand(void)
{
    int x, n, once;
    int *p;
    x = 23;
    n = 5;
    once = 1;
    p = &x;
    while (once--) {
        *p = x % n;
        x = 100 + x / n;
    }
    ck(x, 100, "t8-x");
}

int main(void)
{
    test_for_plain_int();
    test_while_register_narrowed();
    test_reversed_order();
    test_unsigned();
    test_signed_negative();
    test_nonadjacent_pair();
    test_first_stmt_reassigns_operand();
    test_bare_compound_frame();
    test_first_stmt_aliases_operand();

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
