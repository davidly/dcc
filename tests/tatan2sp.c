/*
 * tatan2sp.c - C99 7.12.4.4 special-value table regression coverage for
 * dccrtl.mac's atan2f. The general "atanf(y/x) with a +/-pi correction"
 * algorithm has no self-correction for infinite operands: y/x with x
 * infinite gives 0 (an ordinary finite/Inf division), but atan2(Inf,Inf)
 * computes y/x = Inf/Inf = NaN (correctly invalid) and poisons the whole
 * result instead of the well-defined +/-pi/4 or +/-3pi/4 the standard
 * requires. The x==0/y==0 case also didn't distinguish the sign of
 * either zero, always returning a bare +0.0f instead of the four
 * distinct results (0, -0, +pi, -pi) the sign combinations require.
 */
#include <stdio.h>
#include <math.h>

static int checks = 0;
static int failures = 0;

static void okf(const char *name, float got, float want)
{
    checks++;
    if (fabsf(got - want) > 0.0001f) {
        failures++;
        printf("FAIL %s: got=%f want=%f\n", name, got, want);
    }
}
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
    const float PI = 3.14159265f;
    const float HALF_PI = 1.57079632f;
    const float QPI = 0.78539816f;
    const float TQPI = 2.35619449f;
    float pinf = INFINITY, ninf = -INFINITY, nan1 = NAN;
    float z = 0.0f, nz = -0.0f;

    okb("atan2f(nan,1) isnan", isnan(atan2f(nan1, 1.0f)), 1);
    okb("atan2f(1,nan) isnan", isnan(atan2f(1.0f, nan1)), 1);

    /* both infinite */
    okf("atan2f(inf,inf)", atan2f(pinf, pinf), QPI);
    okf("atan2f(-inf,inf)", atan2f(ninf, pinf), -QPI);
    okf("atan2f(inf,-inf)", atan2f(pinf, ninf), TQPI);
    okf("atan2f(-inf,-inf)", atan2f(ninf, ninf), -TQPI);

    /* x infinite, y finite */
    okf("atan2f(1,inf)", atan2f(1.0f, pinf), 0.0f);
    okb("atan2f(1,inf) sign", signbit(atan2f(1.0f, pinf)), 0);
    okf("atan2f(-1,inf)", atan2f(-1.0f, pinf), 0.0f);
    okb("atan2f(-1,inf) sign", signbit(atan2f(-1.0f, pinf)), 1);
    okf("atan2f(1,-inf)", atan2f(1.0f, ninf), PI);
    okf("atan2f(-1,-inf)", atan2f(-1.0f, ninf), -PI);

    /* y infinite, x finite */
    okf("atan2f(inf,1)", atan2f(pinf, 1.0f), HALF_PI);
    okf("atan2f(-inf,1)", atan2f(ninf, 1.0f), -HALF_PI);
    okf("atan2f(inf,-1)", atan2f(pinf, -1.0f), HALF_PI);
    okf("atan2f(-inf,-1)", atan2f(ninf, -1.0f), -HALF_PI);

    /* both zero, all sign combos */
    okf("atan2f(+0,+0)", atan2f(z, z), 0.0f);
    okb("atan2f(+0,+0) sign", signbit(atan2f(z, z)), 0);
    okf("atan2f(-0,+0)", atan2f(nz, z), 0.0f);
    okb("atan2f(-0,+0) sign", signbit(atan2f(nz, z)), 1);
    okf("atan2f(+0,-0)", atan2f(z, nz), PI);
    okf("atan2f(-0,-0)", atan2f(nz, nz), -PI);

    /* ordinary cases, must be unaffected */
    okf("atan2f(1,0)", atan2f(1.0f, 0.0f), HALF_PI);
    okf("atan2f(-1,0)", atan2f(-1.0f, 0.0f), -HALF_PI);
    okf("atan2f(1,1)", atan2f(1.0f, 1.0f), 0.78539816f);
    okf("atan2f(-1,1)", atan2f(-1.0f, 1.0f), -0.78539816f);
    okf("atan2f(1,-1)", atan2f(1.0f, -1.0f), 2.35619449f);
    okf("atan2f(-1,-1)", atan2f(-1.0f, -1.0f), -2.35619449f);

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
