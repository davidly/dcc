/*
 * tmatha.c - verification of the C89 <math.h> double-name aliases that dcc
 * maps to its single-precision runtime (sinh cosh tanh frexp ldexp modf).
 *
 * These standard spellings are function-like macros in <math.h> that forward
 * to the f-suffixed runtime entries; this test exercises them by their
 * standard C89 names.  Results are checked against host-libm reference values
 * with a combined absolute/relative tolerance, so the output is a
 * deterministic PASS/FAIL summary.
 */
#include <stdio.h>
#include <math.h>

/*
 * The C89 double-name math macros operate on the widest floating type: that is
 * double on a host compiler used to generate the baseline, but float under dcc
 * (which has no double). Using the right storage type keeps modf's pointer
 * argument type-correct on both, while the checks below still compare in float.
 */
#ifdef _DCC_
typedef float mathreal;
#else
typedef double mathreal;
#endif

static int checks = 0;
static int failures = 0;

/* core check: pass when |got-want| <= tol */
static void chkt(const char *name, float got, float want, float tol)
{
    float diff;

    checks++;
    diff = got - want;
    if (diff < 0.0f) diff = -diff;

    if (diff > tol) {
        failures++;
        printf("  [FAIL] %-8s got=%f want=%f (diff=%f)\n", name, got, want, diff);
    }
}

/* transcendental approximations: ~1e-3 absolute + relative band */
static void chk(const char *name, float got, float want)
{
    float a = (want < 0.0f) ? -want : want;
    chkt(name, got, want, 0.001f + 0.001f * a);
}

/* exact decomposition (frexp/ldexp/modf): demand a tight match */
static void chkx(const char *name, float got, float want)
{
    chkt(name, got, want, 0.0001f);
}

static void chki(const char *name, int got, int want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("  [FAIL] %-8s got=%d want=%d\n", name, got, want);
    }
}

int main(void)
{
    int e;
    mathreal m;
    mathreal ip;
    mathreal fr;

    printf("=== dcc math.h alias verification ===\n");

    /* sinh/cosh/tanh macros -> sinhf/coshf/tanhf */
    chk("sinh0",  sinh(0.0f),   0.000000f);
    chk("sinh1",  sinh(1.0f),   1.175201f);
    chk("sinhn1", sinh(-1.0f), -1.175201f);
    chk("sinh05", sinh(0.5f),   0.521095f);
    chk("cosh0",  cosh(0.0f),   1.000000f);
    chk("cosh1",  cosh(1.0f),   1.543081f);
    chk("cosh05", cosh(0.5f),   1.127626f);
    chk("tanh0",  tanh(0.0f),   0.000000f);
    chk("tanh1",  tanh(1.0f),   0.761594f);
    chk("tanhn1", tanh(-1.0f), -0.761594f);

    /* frexp macro -> frexpf */
    e = 0; m = frexp(8.0f, &e);
    chkx("frexpm8", m, 0.5f);
    chki("frexpe8", e, 4);
    e = 0; m = frexp(12.0f, &e);
    chkx("frexpm12", m, 0.75f);
    chki("frexpe12", e, 4);
    e = 0; m = frexp(0.25f, &e);
    chkx("frexpm025", m, 0.5f);
    chki("frexpe025", e, -1);

    /* ldexp macro -> ldexpf */
    chkx("ldexp8",   ldexp(0.5f, 4),   8.0f);
    chkx("ldexp12",  ldexp(0.75f, 4),  12.0f);
    chkx("ldexp025", ldexp(1.0f, -2),  0.25f);

    /* modf macro -> modff */
    ip = 0.0f; fr = modf(3.25f, &ip);
    chkx("modffr", fr, 0.25f);
    chkx("modfip", ip, 3.0f);
    ip = 0.0f; fr = modf(-2.5f, &ip);
    chkx("modffrn", fr, -0.5f);
    chkx("modfipn", ip, -2.0f);

    printf("-------------------------------\n");
    printf("checks=%d failures=%d\n", checks, failures);
    if (failures) {
        printf("RESULT: FAIL\n");
        return 1;
    }
    printf("RESULT: PASS\n");
    return 0;
}
