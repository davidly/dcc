/*
 * tfmodfsp.c - C99 7.12.10.1 domain/special-value regression coverage
 * for dccrtl.mac's fmodf. The q=x/y -> (long)q -> x - q*y pipeline had
 * no self-correction for Inf/NaN operands: fmodf(Inf, y) truncated
 * (long)(Inf/y) to some finite value (long can't represent infinity)
 * and silently produced Inf back out instead of the required NaN;
 * fmodf(x, Inf) computed prod = 0*Inf = NaN (correctly, from an earlier
 * fix this session) and that NaN propagated through the final
 * subtraction instead of returning x unchanged as C99 requires.
 *
 * This also fixes sinf/cosf/tanf's domain handling for infinite input
 * indirectly, since they all reduce their argument via fmodf first -
 * sinf(Inf) already happened to give NaN by accident (some intermediate
 * inf-inf cancellation in its polynomial), but cosf(Inf) did not before
 * this fix.
 */
#include <stdio.h>
#include <math.h>

static int checks = 0;
static int failures = 0;

static void okb(const char *name, int got, int want)
{
    checks++;
    if ((got != 0) != (want != 0)) {
        failures++;
        printf("FAIL %s: got %d want %d\n", name, got, want);
    }
}

static void okf(const char *name, float got, float want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s: got=%f want=%f\n", name, got, want);
    }
}

int main(void)
{
    float pinf = INFINITY, ninf = -INFINITY, nan1 = NAN;

    okb("fmodf(inf,2pi) isnan", isnan(fmodf(pinf, 6.28318531f)), 1);
    okb("fmodf(-inf,2pi) isnan", isnan(fmodf(ninf, 6.28318531f)), 1);
    okb("fmodf(nan,2pi) isnan", isnan(fmodf(nan1, 6.28318531f)), 1);
    okb("fmodf(5,0) isnan", isnan(fmodf(5.0f, 0.0f)), 1);
    okf("fmodf(5,inf)", fmodf(5.0f, pinf), 5.0f);
    okf("fmodf(-5,inf)", fmodf(-5.0f, pinf), -5.0f);
    okf("fmodf(5,3)", fmodf(5.0f, 3.0f), 2.0f);

    okb("sinf(inf) isnan", isnan(sinf(pinf)), 1);
    okb("cosf(inf) isnan", isnan(cosf(pinf)), 1);
    okb("tanf(inf) isnan", isnan(tanf(pinf)), 1);
    okb("sinf(1) close", fabsf(sinf(1.0f) - 0.841471f) < 0.001f, 1);
    okf("cosf(0)", cosf(0.0f), 1.0f);

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
