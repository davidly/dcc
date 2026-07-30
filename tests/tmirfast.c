/*
 * tmirfast.c -- DCC regression tests
 *
 * Permanent regression fixture for MIR Plan-100 Phase 3 constant/comparison
 * fast paths added in Items 25/27/30/31:
 *   - Item 25: compare-with-zero fast path (ld a,h / or l) for equality and
 *     inequality against constant 0, avoiding a full 16-bit subtract.
 *   - Item 27: sign-bit test fast path (bit 7,h) for signed < and >= against
 *     constant 0.
 *   - Item 30: shift-and-subtract fast path ((x << k) - x) for multipliers
 *     that are a bottom-aligned run of ones (7, 15, 31, 63, 127, 255),
 *     including multipliers large enough to have previously exceeded the
 *     naive per-bit op-count cap and fallen back to a runtime __mulu call.
 *   - Item 31: fused inc (ix+n) / dec (ix+n) in-place update for a bare
 *     x++;/x--; on a 16-bit local or parameter whose incremented value is
 *     never read again (dead-after-increment), replacing a full
 *     load/add-or-sub/store round trip.
 *
 * Each helper below has enough surrounding locals/arithmetic to give the
 * MIR selector a chance to accept the function through the spilled-scalar-
 * cfg path rather than falling back on text-size or being handled entirely
 * by the fully-registerized homed-scalar-cfg path (which would not exercise
 * these fast paths at all); whether a given helper is actually accepted
 * into MIR on any given day is a selector heuristic, not a correctness
 * requirement of this fixture; either way, the assertions below must hold
 * under both the MIR and legacy backends, and under both nopeep and peep,
 * so a regression in the fast paths' semantics shows up as a wrong-answer
 * failure here rather than only as a byte-count change.
 */

#include <stdio.h>
#include <limits.h>

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

/* Item 25: compare-with-zero fast path, signed and unsigned, both polarities. */
static int eqz(a, b) int a; int b;
{
    int x1 = a, x2 = b, x3 = a + b, x4 = a - b;
    if (x1 == 0)
        return x2 + x3 + x4 + 1;
    return x2 + x3 + x4;
}

static int nez(a, b) int a; int b;
{
    int x1 = a, x2 = b, x3 = a + b, x4 = a - b;
    if (x1 != 0)
        return x2 + x3 + x4 + 1;
    return x2 + x3 + x4;
}

/* Item 27: signed sign-bit test fast path for < 0 and >= 0. */
static int ltz(a, b) int a; int b;
{
    int x1 = a, x2 = b, x3 = a + b, x4 = a - b;
    if (x1 < 0)
        return x2 + x3 + x4 + 1;
    return x2 + x3 + x4;
}

static int gez(a, b) int a; int b;
{
    int x1 = a, x2 = b, x3 = a + b, x4 = a - b;
    if (x1 >= 0)
        return x2 + x3 + x4 + 1;
    return x2 + x3 + x4;
}

/* Item 30: ones-run multipliers, spread across the whole k range this
 * item's shift-and-subtract form covers (k = 3..8). */
static int mul7(x) int x; { int y = x + 1, z = x - 1; return x * 7 + (y - z) - 2; }
static int mul15(x) int x; { int y = x + 1, z = x - 1; return x * 15 + (y - z) - 2; }
static int mul31(x) int x; { int y = x + 1, z = x - 1; return x * 31 + (y - z) - 2; }
static int mul63(x) int x; { int y = x + 1, z = x - 1; return x * 63 + (y - z) - 2; }
static int mul127(x) int x; { int y = x + 1, z = x - 1; return x * 127 + (y - z) - 2; }
static int mul255(x) int x; { int y = x + 1, z = x - 1; return x * 255 + (y - z) - 2; }

/* Item 31: a dead-after-increment/decrement local. The result depends on
 * the value observed *before* the update (via side_effect's return), so a
 * regression that corrupts the fused instruction's target byte(s) - rather
 * than merely leaving the (unread) post-update value wrong - would still
 * have to disturb r's stack slot or another local to be caught; the
 * "*p" reads below additionally force the post-update memory contents to
 * be observed directly through an address-of-local alias, independent of
 * the compiler's normal (register-numbered) value tracking for the
 * variable. */
static int side_effect(y) int y; { return y; }

static int inc_dead(x) int x;
{
    int r = side_effect(x);
    x++;
    return r;
}

static int dec_dead(x) int x;
{
    int r = side_effect(x);
    x--;
    return r;
}

static int inc_observe(x) int x;
{
    int *p = &x;
    side_effect(x);
    x++;
    return *p;
}

static int dec_observe(x) int x;
{
    int *p = &x;
    side_effect(x);
    x--;
    return *p;
}

int main()
{
    check("eqz zero", eqz(0, 5), 6);
    check("eqz nonzero", eqz(3, 5), 11);
    check("eqz neg", eqz(-1, 5), 3);
    check("nez zero", nez(0, 5), 5);
    check("nez nonzero", nez(3, 5), 12);
    check("nez neg", nez(-1, 5), 4);

    check("ltz neg", ltz(-1, 5), 4);
    check("ltz zero", ltz(0, 5), 5);
    check("ltz pos", ltz(1, 5), 7);
    check("ltz min", ltz(INT_MIN, 5), 6);
    check("gez neg", gez(-1, 5), 3);
    check("gez zero", gez(0, 5), 6);
    check("gez pos", gez(1, 5), 8);
    check("gez min", gez(INT_MIN, 5), 5);

    check("mul7 0", mul7(0), 0);
    check("mul7 1", mul7(1), 7);
    check("mul7 100", mul7(100), 700);
    check("mul7 -100", mul7(-100), -700);
    check("mul7 -1", mul7(-1), -7);
    check("mul15 0", mul15(0), 0);
    check("mul15 1", mul15(1), 15);
    check("mul15 100", mul15(100), 1500);
    check("mul15 -100", mul15(-100), -1500);
    check("mul31 1", mul31(1), 31);
    check("mul31 1000", mul31(1000), 31000);
    check("mul31 -1000", mul31(-1000), -31000);
    check("mul63 1", mul63(1), 63);
    check("mul63 500", mul63(500), 31500);
    check("mul63 -500", mul63(-500), -31500);
    check("mul127 1", mul127(1), 127);
    check("mul127 200", mul127(200), 25400);
    check("mul127 -200", mul127(-200), -25400);
    check("mul255 1", mul255(1), 255);
    check("mul255 100", mul255(100), 25500);
    check("mul255 -100", mul255(-100), -25500);
    /* 16-bit-wrapping case: 1000 * 255 = 255000, which wraps mod 65536 to
     * 255000 - 3*65536 = 58392, represented as a negative int (58392 -
     * 65536 = -7144). */
    check("mul255 wrap", mul255(1000), -7144);

    check("inc_dead pre-value 5", inc_dead(5), 5);
    check("inc_dead pre-value -1", inc_dead(-1), -1);
    check("dec_dead pre-value 5", dec_dead(5), 5);
    check("dec_dead pre-value 0", dec_dead(0), 0);

    check("inc_observe 255->256", inc_observe(255), 256);
    check("inc_observe -1->0", inc_observe(-1), 0);
    check("inc_observe 0xFFFF->0", inc_observe(0xFFFF), 0);
    check("dec_observe 256->255", dec_observe(256), 255);
    check("dec_observe 0->-1", dec_observe(0), -1);
    check("dec_observe 1->0", dec_observe(1), 0);

    if (failures) {
        printf("tmirfast: %d failure(s)\n", failures);
        return 1;
    }

    printf("tmirfast: all tests passed\n");
    return 0;
}
