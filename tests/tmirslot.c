/**
 * @file tmirslot.c
 * @brief Validates MIR backend-slot liveness and value forwarding.
 *
 * @par Coverage
 * Exercises immediate call-result use, forwarding across labels and stores,
 * dead slot/local stores, PHI values live on only one incoming path, and
 * values that must survive a caller-clobbering call.
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

/* A plain callee used to force a real call boundary around the slot. */
static int scale(a) int a; { return a * 3 + 1; }

/* Immediate use of a call result: the result should forward directly into
 * the following arithmetic without a spurious backend-slot round trip. */
static int immediate_use(a, b) int a; int b; {
    return scale(a) + b;
}

/* Item 15: the call result crosses a single-predecessor label (the `if`
 * has no else, so the label immediately following it has exactly one
 * predecessor) before being consumed. */
static int forward_across_label(a, b) int a; int b; {
    int v;
    v = scale(a);
    if (b > 0) {
        v = v + b;
    }
    return v;
}

/* Forward straight into a store: value is computed, immediately stored to
 * a local, and used once from that local -- no dead round trip. */
static int forward_into_store(a) int a; {
    int t;
    t = scale(a);
    return t - 1;
}

/* Item 16/17: a slot/local is written but never read again anywhere in
 * the function. The write must still occur (for any global side effects
 * of the right-hand side) but must not force a spilled backend slot or a
 * dead reload. `unused` is deliberately never read after assignment. */
static int dead_store_elision(a) int a; {
    int unused;
    int result;
    unused = scale(a);
    result = a + 2;
    unused = result;    /* still dead -- never read below */
    return result;
}

/* Item 18: a phi destination that is dead down one incoming edge (the
 * `else` branch overwrites it before any use) but live down the other
 * (the `if` branch's value survives to the return). The dead-copy skip
 * predicate must not remove the copy needed on the live edge. */
static int phi_partial_dead(a, cond) int a; int cond; {
    int v;
    if (cond) {
        v = a + 5;
    } else {
        v = a - 5;
        v = v * 2;
    }
    return v;
}

/* Cross-call: a value must survive across an intervening call that could
 * clobber the same physical registers/slots used for forwarding. */
static int cross_call(a, b) int a; int b; {
    int saved;
    saved = a + b;
    return scale(saved) - saved;
}

int main()
{
    check("immediate_use", immediate_use(2, 10), 17);
    check("immediate_use zero", immediate_use(0, 0), 1);

    check("forward_across_label taken", forward_across_label(2, 3), 10);
    check("forward_across_label skipped", forward_across_label(2, -1), 7);
    check("forward_across_label zero", forward_across_label(0, 0), 1);

    check("forward_into_store", forward_into_store(4), 12);
    check("forward_into_store zero", forward_into_store(0), 0);

    check("dead_store_elision", dead_store_elision(3), 5);
    check("dead_store_elision neg", dead_store_elision(-2), 0);

    check("phi_partial_dead if", phi_partial_dead(10, 1), 15);
    check("phi_partial_dead else", phi_partial_dead(10, 0), 10);
    check("phi_partial_dead else neg", phi_partial_dead(1, 0), -8);

    check("cross_call", cross_call(2, 3), 11);
    check("cross_call zero", cross_call(0, 0), 1);

    if (failures) {
        printf("tmirslot: %d failure(s)\n", failures);
        return 1;
    }

    printf("tmirslot: all tests passed\n");
    return 0;
}
