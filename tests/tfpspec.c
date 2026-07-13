/*
 * tfpspec.c - IEEE-754 invalid-operation and special-value regression
 * coverage for dccrtl.mac's __fadd/__fsub/__fmul/__fdiv (and their
 * fastcall siblings __faf/__fsf/__fmf).
 *
 * 0/0, Inf-Inf (and Inf+(-Inf)), and Inf*0 are all "invalid operation"
 * per IEEE-754 and must produce NaN, not garbage or a silently wrong
 * finite/zero value. Also covers the other exponent-255 combinations
 * (Inf+/-finite, Inf*finite, finite/Inf, Inf/Inf, Inf/finite) that share
 * the same detection code path in dccrtl.mac and NaN propagation through
 * each operator.
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
    int ok;
    checks++;
    if (isnan(want)) {
        ok = isnan(got);
    } else if (isinf(want)) {
        ok = isinf(got) && (signbit(got) == signbit(want));
    } else {
        ok = (got == want) && (signbit(got) == signbit(want) || got != 0.0f);
    }
    if (!ok) {
        failures++;
        printf("FAIL %s: got=%.6f isnan=%d isinf=%d sign=%d\n",
               name, got, isnan(got), isinf(got), signbit(got));
    }
}

int main(void)
{
    float zero = 0.0f, negzero = -0.0f;
    float five = 5.0f, negfive = -5.0f, three = 3.0f;
    float pinf = INFINITY, ninf = -INFINITY, nan1 = NAN;

    /* division: the three IEEE invalid-operation cases plus propagation */
    okb("0/0 isnan", isnan(zero / zero), 1);
    okb("inf/inf isnan", isnan(pinf / pinf), 1);
    okb("inf/-inf isnan", isnan(pinf / ninf), 1);
    okf("5/0", five / zero, pinf);
    okf("-5/0", negfive / zero, ninf);
    okf("inf/5", pinf / five, pinf);
    okf("5/inf", five / pinf, zero);
    okf("0/inf", zero / pinf, zero);
    okf("nan/5", nan1 / five, nan1);
    okf("5/1", five / 1.0f, five);
    okf("10/4", 10.0f / 4.0f, 2.5f);

    /* addition/subtraction: Inf+(-Inf) and Inf-Inf, plus propagation */
    okb("inf+-inf isnan", isnan(pinf + ninf), 1);
    okb("inf-inf isnan", isnan(pinf - pinf), 1);
    okf("inf+inf", pinf + pinf, pinf);
    okf("-inf+-inf", ninf + ninf, ninf);
    okf("inf-(-inf)", pinf - ninf, pinf);
    okf("inf+5", pinf + five, pinf);
    okf("5-inf", five - pinf, ninf);
    okf("nan+5", nan1 + five, nan1);
    okf("5+3", five + three, 8.0f);
    okf("5-3", five - three, 2.0f);
    okf("5+(-5)", five + negfive, zero);

    /* multiplication: 0*Inf (both operand orders), plus propagation */
    okb("inf*0 isnan", isnan(pinf * zero), 1);
    okb("0*inf isnan", isnan(zero * pinf), 1);
    okb("-inf*0 isnan", isnan(ninf * zero), 1);
    okf("inf*inf", pinf * pinf, pinf);
    okf("inf*-inf", pinf * ninf, ninf);
    okf("inf*5", pinf * five, pinf);
    okf("inf*-5", pinf * negfive, ninf);
    okf("nan*5", nan1 * five, nan1);
    okf("5*3", five * three, 15.0f);
    okf("0*5", zero * five, zero);

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
