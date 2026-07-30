/*
 * tmirfuse.c -- DCC regression tests
 *
 * Permanent regression fixture for MIR Plan-100 Items 1/4: fusing a scalar
 * comparison directly into its consuming branch (mir_try_emit_spilled_scalar_cfg /
 * mir_emit_fused_comparison_branch), including the logical-not extension.
 * Exercises every comparison operator, signed and unsigned, bare and negated,
 * across boundary values (INT_MIN/INT_MAX, 0/-1, unsigned wraparound) so a
 * future regression in the fused branch polarity or sign handling shows up
 * as a wrong-answer failure here rather than only as a byte-count change.
 *
 * Expected output:
 *   tmirfuse: all tests passed
 *
 * Expected exit status:
 *   0
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

/* Signed comparisons, bare condition. */
static int seq(a, b) int a; int b; { if (a == b) return 1; return 0; }
static int sne(a, b) int a; int b; { if (a != b) return 1; return 0; }
static int slt(a, b) int a; int b; { if (a < b) return 1; return 0; }
static int sgt(a, b) int a; int b; { if (a > b) return 1; return 0; }
static int sle(a, b) int a; int b; { if (a <= b) return 1; return 0; }
static int sge(a, b) int a; int b; { if (a >= b) return 1; return 0; }

/* Signed comparisons, negated condition (Item 4). */
static int nseq(a, b) int a; int b; { if (!(a == b)) return 1; return 0; }
static int nsne(a, b) int a; int b; { if (!(a != b)) return 1; return 0; }
static int nslt(a, b) int a; int b; { if (!(a < b)) return 1; return 0; }
static int nsgt(a, b) int a; int b; { if (!(a > b)) return 1; return 0; }
static int nsle(a, b) int a; int b; { if (!(a <= b)) return 1; return 0; }
static int nsge(a, b) int a; int b; { if (!(a >= b)) return 1; return 0; }

/* Unsigned comparisons, bare and negated condition. */
static int ult(a, b) unsigned int a; unsigned int b; { if (a < b) return 1; return 0; }
static int ugt(a, b) unsigned int a; unsigned int b; { if (a > b) return 1; return 0; }
static int ule(a, b) unsigned int a; unsigned int b; { if (a <= b) return 1; return 0; }
static int uge(a, b) unsigned int a; unsigned int b; { if (a >= b) return 1; return 0; }
static int nult(a, b) unsigned int a; unsigned int b; { if (!(a < b)) return 1; return 0; }
static int nuge(a, b) unsigned int a; unsigned int b; { if (!(a >= b)) return 1; return 0; }

/* Each of these has enough locals in front of the comparison to steer the
 * MIR selector toward mir_try_emit_spilled_scalar_cfg (the frame/spill-heavy
 * path) rather than mir_try_emit_homed_scalar_cfg, so the fused code path
 * under test is actually exercised regardless of the compiler's internal
 * selector heuristics on a given day. */
static int slt_spilled(a, b) int a; int b;
{
    int x1 = a, x2 = b, x3 = a + b, x4 = a - b, x5 = a ^ b, x6 = a | b;
    if (x1 < x2)
        return x3 + x4 + x5 + x6 - (a + b) - (a - b) - (a ^ b) - (a | b) + 1;
    return x3 + x4 + x5 + x6 - (a + b) - (a - b) - (a ^ b) - (a | b);
}

static int nslt_spilled(a, b) int a; int b;
{
    int x1 = a, x2 = b, x3 = a + b, x4 = a - b, x5 = a ^ b, x6 = a | b;
    if (!(x1 < x2))
        return x3 + x4 + x5 + x6 - (a + b) - (a - b) - (a ^ b) - (a | b) + 1;
    return x3 + x4 + x5 + x6 - (a + b) - (a - b) - (a ^ b) - (a | b);
}

static int and_chain_spilled(a, b, c) int a; int b; int c;
{
    int x1 = a, x2 = b, x3 = c, x4 = a + b, x5 = b + c, x6 = a + c;
    if (x1 < x2 && x2 < x3)
        return x4 + x5 + x6 - (a + b) - (b + c) - (a + c) + 1;
    return x4 + x5 + x6 - (a + b) - (b + c) - (a + c);
}

int main()
{
    check("seq eq", seq(5, 5), 1);
    check("seq ne", seq(5, 6), 0);
    check("sne eq", sne(5, 5), 0);
    check("sne ne", sne(5, 6), 1);
    check("slt lt", slt(-1, 1), 1);
    check("slt eq", slt(1, 1), 0);
    check("slt gt", slt(1, -1), 0);
    check("slt bounds", slt(INT_MIN, INT_MAX), 1);
    check("sgt gt", sgt(1, -1), 1);
    check("sgt eq", sgt(1, 1), 0);
    check("sgt lt", sgt(-1, 1), 0);
    check("sle lt", sle(-1, 1), 1);
    check("sle eq", sle(1, 1), 1);
    check("sle gt", sle(1, -1), 0);
    check("sge gt", sge(1, -1), 1);
    check("sge eq", sge(1, 1), 1);
    check("sge lt", sge(-1, 1), 0);

    check("nseq eq", nseq(5, 5), 0);
    check("nseq ne", nseq(5, 6), 1);
    check("nsne eq", nsne(5, 5), 1);
    check("nsne ne", nsne(5, 6), 0);
    check("nslt lt", nslt(-1, 1), 0);
    check("nslt eq", nslt(1, 1), 1);
    check("nslt gt", nslt(1, -1), 1);
    check("nslt bounds", nslt(INT_MIN, INT_MAX), 0);
    check("nsgt gt", nsgt(1, -1), 0);
    check("nsgt eq", nsgt(1, 1), 1);
    check("nsgt lt", nsgt(-1, 1), 1);
    check("nsle lt", nsle(-1, 1), 0);
    check("nsle eq", nsle(1, 1), 0);
    check("nsle gt", nsle(1, -1), 1);
    check("nsge gt", nsge(1, -1), 0);
    check("nsge eq", nsge(1, 1), 0);
    check("nsge lt", nsge(-1, 1), 1);

    check("ult lt", ult(1u, 2u), 1);
    check("ult wrap", ult((unsigned int)-1, 0u), 0);
    check("ult wrap2", ult(0u, (unsigned int)-1), 1);
    check("ugt gt", ugt(2u, 1u), 1);
    check("ugt wrap", ugt((unsigned int)-1, 0u), 1);
    check("ule eq", ule(1u, 1u), 1);
    check("uge eq", uge(1u, 1u), 1);
    check("nult lt", nult(1u, 2u), 0);
    check("nult wrap", nult((unsigned int)-1, 0u), 1);
    check("nuge wrap", nuge(0u, (unsigned int)-1), 1);

    check("slt_spilled lt", slt_spilled(-1, 1), 1);
    check("slt_spilled ge", slt_spilled(1, -1), 0);
    check("nslt_spilled lt", nslt_spilled(-1, 1), 0);
    check("nslt_spilled ge", nslt_spilled(1, -1), 1);

    check("and_chain lt-lt", and_chain_spilled(1, 2, 3), 1);
    check("and_chain lt-ge", and_chain_spilled(1, 2, 1), 0);
    check("and_chain ge-lt", and_chain_spilled(2, 1, 3), 0);

    if (failures) {
        printf("tmirfuse: %d failure(s)\n", failures);
        return 1;
    }

    printf("tmirfuse: all tests passed\n");
    return 0;
}
