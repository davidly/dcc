/*
 * tphijoin.c -- DCC regression tests
 *
 * Permanent regression fixture for MIR Plan-100 Item 42: CFG shape
 * hygiene from Phase 4 (Items 35-41). Exercises:
 *   - a simple if/else join where both arms define the same object
 *     identically, so the post-join load should alias the shared value
 *     instead of spilling and reloading (Item 38's then-arm labeling);
 *   - a nested if/else whose inner join value itself feeds an outer
 *     join, stacking Item 38's fix two levels deep;
 *   - a while loop with an interior `continue`, whose continue-label and
 *     loop-latch jump chain should collapse to a direct retarget instead
 *     of a jump-to-jump indirection (Items 35/36);
 *   - a loop-carried value phi at the loop header, merging the entry
 *     value and the latch value (the shape Item 37 found already works,
 *     and confirmed Items 35/36 must not disturb).
 *
 * Expected output:
 *   tphijoin: all tests passed
 *
 * Expected exit status:
 *   0
 */

#include <stdio.h>

static int failures = 0;

static void fail(name, got, expected)
const char *name;
int got;
int expected;
{
    printf("FAIL %s got %d expected %d\n", name, got, expected);
    failures++;
}

static void check(name, got, expected)
const char *name;
int got;
int expected;
{
    if (got != expected)
        fail(name, got, expected);
}

/* Item 38: both arms of the if/else compute the identical expression, so
 * the value reaching the post-join use should be recognized as the same
 * value on both incoming edges rather than spilled to memory. */
static int if_else_join(x, cond) int x; int cond; {
    int y;
    if (cond) {
        y = x * 2;
    } else {
        y = x * 2;
    }
    return y + x;
}

/* Nested version: the inner if/else's joined value is itself one arm of
 * an outer if/else join, stacking Item 38's fix two levels deep. */
static int nested_if_else_join(x, outer, inner) int x; int outer; int inner; {
    int y;
    if (outer) {
        if (inner) {
            y = x + 1;
        } else {
            y = x + 1;
        }
    } else {
        y = x + 1;
    }
    return y * 3;
}

/* Items 35/36: a while loop whose body has an interior `continue`. The
 * continue-target and the loop-latch's own back-jump should collapse to
 * a single direct retarget rather than a jump chaining through an
 * intermediate now-redundant label. */
static int sum_even_with_continue(n) int n; {
    int i;
    int sum;

    i = 0;
    sum = 0;
    while (i < n) {
        i = i + 1;
        if (i % 2 != 0) {
            continue;
        }
        sum = sum + i;
    }
    return sum;
}

/* Loop-carried value phi at the loop header: `total` merges its entry
 * value (0) and its own loop-latch value on every iteration. Item 37
 * found this already works via mir_try_make_object_phi() and must keep
 * working after Items 35/36/38's label/jump-chain changes. */
static int loop_header_phi(n) int n; {
    int total;
    int i;

    total = 0;
    for (i = 1; i <= n; i = i + 1) {
        total = total + i;
    }
    return total;
}

int main()
{
    check("if_else_join taken", if_else_join(5, 1), 15);
    check("if_else_join not taken", if_else_join(5, 0), 15);
    check("if_else_join zero", if_else_join(0, 1), 0);

    check("nested_if_else_join outer/inner", nested_if_else_join(4, 1, 1), 15);
    check("nested_if_else_join outer/not-inner", nested_if_else_join(4, 1, 0), 15);
    check("nested_if_else_join not-outer", nested_if_else_join(4, 0, 1), 15);
    check("nested_if_else_join zero", nested_if_else_join(-1, 1, 1), 0);

    check("sum_even_with_continue small", sum_even_with_continue(6), 12);
    check("sum_even_with_continue zero", sum_even_with_continue(0), 0);
    check("sum_even_with_continue one", sum_even_with_continue(1), 0);
    check("sum_even_with_continue odd-top", sum_even_with_continue(7), 12);

    check("loop_header_phi small", loop_header_phi(5), 15);
    check("loop_header_phi zero", loop_header_phi(0), 0);
    check("loop_header_phi one", loop_header_phi(1), 1);

    if (failures) {
        printf("tphijoin: %d failure(s)\n", failures);
        return 1;
    }

    printf("tphijoin: all tests passed\n");
    return 0;
}
