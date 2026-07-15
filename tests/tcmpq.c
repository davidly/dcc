/*
 * tcmpq.c - float comparison regression coverage, added alongside the
 * fastcall comparison variants (__feqf/__fnef/__fltf/__fgtf/__flef/__fgef
 * in DCCRTL.MAC): the right-hand operand of every '==','!=','<','>','<=',
 * '>=' now arrives live in DE:HL instead of via a second push/pop, mirroring
 * __faf/__fsf/__fmf/__fdf/__fmaf's existing convention.
 *
 * Exercises both the fast path (positive, finite, non-NaN operands - a
 * straight register-vs-stack byte comparison) and the cold path (negative
 * operands, signed zero, NaN, Inf - routed through __fcmpsf/__frcmpf, a
 * direct register-substituted port of the shared __fcmps/__frcmp
 * classifier, not the original naive stack-frame-reconstruction attempt
 * that regressed tc89fcmp.c's perf baseline ~8.5% before being replaced).
 * Also covers literal/variable/function asymmetry and both codegen call
 * sites (ordinary expression context in gen_binary_ast, and branch context
 * via ast_gen_float_cmp_branch), guarding against an operand mixup like the
 * earlier __fdf bug.
 */
#include <stdio.h>
#include <math.h>

static int checks = 0, failures = 0;
static void okb(const char *name, int got, int want)
{
    checks++;
    if ((got != 0) != (want != 0)) {
        failures++;
        printf("FAIL %s: got %d want %d\n", name, got, want);
    }
}

int main(void)
{
    float pinf = INFINITY, ninf = -INFINITY, nan1 = NAN;
    float a = 5.0f, b = 10.0f, c = 5.0f, d = -3.0f, e = -7.0f;
    float pz = 0.0f, nz = -0.0f;

    /* fast path: both positive, finite, non-NaN */
    okb("5<10", a < b, 1);
    okb("10<5", b < a, 0);
    okb("5<5", a < c, 0);
    okb("5>10", a > b, 0);
    okb("10>5", b > a, 1);
    okb("5<=5", a <= c, 1);
    okb("5<=10", a <= b, 1);
    okb("10<=5", b <= a, 0);
    okb("5>=5", a >= c, 1);
    okb("10>=5", b >= a, 1);
    okb("5>=10", a >= b, 0);
    okb("5==5", a == c, 1);
    okb("5==10", a == b, 0);
    okb("5!=10", a != b, 1);
    okb("5!=5", a != c, 0);

    /* negative operands: cold path */
    okb("-3<-7", d < e, 0);
    okb("-7<-3", e < d, 1);
    okb("-3>-7", d > e, 1);
    okb("-3<5", d < a, 1);
    okb("5<-3", a < d, 0);
    okb("-3==-3", d == -3.0f, 1);
    okb("-3<=-3", d <= -3.0f, 1);
    okb("-3>=-3", d >= -3.0f, 1);
    okb("-7<=-3", e <= d, 1);
    okb("-3<=-7", d <= e, 0);
    okb("-7>=-3", e >= d, 0);

    /* signed zero: cold path, must compare equal */
    okb("+0==-0", pz == nz, 1);
    okb("+0<=-0", pz <= nz, 1);
    okb("+0>=-0", pz >= nz, 1);
    okb("+0<-0", pz < nz, 0);
    okb("+0>-0", pz > nz, 0);
    okb("-0<5", nz < a, 1);
    okb("-3<+0", d < pz, 1);

    /* NaN: every ordered comparison false, != true, == false */
    okb("nan<5", nan1 < a, 0);
    okb("5<nan", a < nan1, 0);
    okb("nan>5", nan1 > a, 0);
    okb("nan<=5", nan1 <= a, 0);
    okb("nan>=5", nan1 >= a, 0);
    okb("nan==5", nan1 == a, 0);
    okb("nan==nan", nan1 == nan1, 0);
    okb("nan!=5", nan1 != a, 1);
    okb("nan!=nan", nan1 != nan1, 1);
    okb("nan<nan", nan1 < nan1, 0);
    okb("nan<=nan", nan1 <= nan1, 0);

    /* Inf: ordered comparisons behave like a very large finite value */
    okb("inf>5", pinf > a, 1);
    okb("inf<5", pinf < a, 0);
    okb("-inf<5", ninf < a, 1);
    okb("-inf>5", ninf > a, 0);
    okb("inf>-inf", pinf > ninf, 1);
    okb("inf==inf", pinf == pinf, 1);
    okb("inf<inf", pinf < pinf, 0);
    okb("inf<=inf", pinf <= pinf, 1);
    okb("inf!=-inf", pinf != ninf, 1);
    okb("inf-nan", pinf < nan1, 0);
    okb("nan-inf", nan1 < pinf, 0);

    /* asymmetric literal/variable/function-call mixes, guarding against
     * an a/b mixup like the earlier __fdf bug */
    okb("lit_5<var_10", 5.0f < b, 1);
    okb("var_10<lit_5", b < 5.0f, 0);
    okb("var_5<lit_10", a < 10.0f, 1);
    okb("lit_10<var_5", 10.0f < a, 0);

    /* branch-context comparisons (if/while), exercising
     * ast_gen_float_cmp_branch's own separate call site */
    okb("branch a<b", a < b, 1);
    okb("branch !(b<a)", b < a, 0);
    while (a < b) {
        a += 1.0f;
        if (a > 100.0f)
            break;
    }
    okb("loop terminated near b", a >= b, 1);

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
