#include <stdio.h>
#include <math.h>
#include <errno.h>

typedef union {
    float f;
    unsigned long u;
} FU;

static int checks;
static int failures;

static void oki(const char *name, int got, int want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s got=%d want=%d\n", name, got, want);
    }
}

int main(void)
{
    FU x;
    float y;

    oki("MATH_ERRNO", MATH_ERRNO, 1);
    oki("MATH_ERREXCEPT", MATH_ERREXCEPT, 2);
    oki("math_errhandling", math_errhandling, MATH_ERRNO);

    errno = 0; y = expf(100.0f); oki("exp overflow", errno, ERANGE);
    errno = 0; y = expf(-150.0f); oki("exp underflow", errno, ERANGE);
    errno = 0; y = expf(-88.0f); oki("exp boundary underflow", errno, ERANGE);
    oki("exp boundary zero", y == 0.0f, 1);
    errno = 0; y = expf(INFINITY); oki("exp infinity", errno, 0);
    errno = 0; y = expf(NAN); oki("exp NaN", errno, 0);

    errno = 0; y = logf(0.0f); oki("log pole", errno, ERANGE);
    errno = 0; y = logf(-1.0f); oki("log domain", errno, EDOM);
    errno = 0; y = logf(INFINITY); oki("log infinity", errno, 0);
    errno = 0; y = logf(NAN); oki("log NaN", errno, 0);

    errno = 0; y = powf(0.0f, -1.0f); oki("pow pole", errno, ERANGE);
    errno = 0; y = powf(-2.0f, 0.5f); oki("pow domain", errno, EDOM);
    errno = 0; y = powf(-2.0f, NAN); oki("pow NaN", errno, 0);

    errno = 0; y = sinhf(100.0f); oki("sinh overflow", errno, ERANGE);
    errno = 0; y = coshf(100.0f); oki("cosh overflow", errno, ERANGE);
    errno = 0; y = sinhf(INFINITY); oki("sinh infinity", errno, 0);
    errno = 0; y = coshf(INFINITY); oki("cosh infinity", errno, 0);

    errno = 0; y = sinf(INFINITY); oki("sin infinity", errno, EDOM);
    errno = 0; y = cosf(INFINITY); oki("cos infinity", errno, EDOM);
    errno = 0; y = tanf(INFINITY); oki("tan infinity", errno, EDOM);
    errno = 0; y = sinf(NAN); oki("sin NaN", errno, 0);

    errno = 0; y = asinf(2.0f); oki("asin domain", errno, EDOM);
    errno = 0; y = acosf(-2.0f); oki("acos domain", errno, EDOM);
    errno = 0; y = asinf(NAN); oki("asin NaN", errno, 0);

    errno = 0; y = fmodf(INFINITY, 2.0f); oki("fmod infinite x", errno, EDOM);
    errno = 0; y = fmodf(1.0f, 0.0f); oki("fmod zero y", errno, EDOM);
    errno = 0; y = fmodf(NAN, 1.0f); oki("fmod NaN", errno, 0);

    errno = 0; y = sqrtf(-1.0f); oki("sqrt negative", errno, EDOM);
    errno = 0; y = sqrtf(-INFINITY); oki("sqrt negative infinity", errno, EDOM);
    errno = 0; y = sqrtf(NAN); oki("sqrt NaN", errno, 0);

    x.u = 0x7f7fffffUL;
    errno = 0; y = nextafterf(x.f, INFINITY);
    oki("nextafter overflow", errno, ERANGE);
    oki("nextafter infinity result", isinf(y), 1);
    errno = 0; y = nextafterf(NAN, INFINITY);
    oki("nextafter NaN", errno, 0);

    errno = 0; y = ldexpf(1.0f, 32767);
    oki("ldexp overflow", errno, ERANGE);
    errno = 0; y = ldexpf(1.0f, -32768);
    oki("ldexp underflow", errno, ERANGE);
    errno = 0; y = ldexpf(1.0f, -150);
    oki("ldexp rounded subnormal", errno, ERANGE);
    x.u = 1UL;
    errno = 0; y = ldexpf(x.f, 0);
    oki("ldexp exact minsub", errno, 0);

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
