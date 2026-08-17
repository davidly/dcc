#include <stdio.h>
#include <math.h>

static int checks;
static int failures;

static void ok(const char *name, int pass)
{
    checks++;
    if (!pass) {
        failures++;
        printf("FAIL %s\n", name);
    }
}

int main(void)
{
    float s = sinhf(89.0f);
    float sn = sinhf(-89.0f);
    float c = coshf(89.0f);
    float cn = coshf(-89.0f);

    ok("sinhf(89) finite", isfinite(s));
    ok("sinhf(89) range", s > 2.2e38f && s < 2.3e38f);
    ok("sinhf(-89) finite", isfinite(sn));
    ok("sinhf(-89) range", sn < -2.2e38f && sn > -2.3e38f);
    ok("coshf(89) finite", isfinite(c));
    ok("coshf(89) range", c > 2.2e38f && c < 2.3e38f);
    ok("coshf(-89) finite", isfinite(cn));
    ok("coshf(-89) range", cn > 2.2e38f && cn < 2.3e38f);
    ok("sinhf(89.4) finite", isfinite(sinhf(89.4f)));
    ok("sinhf(89.4) range", sinhf(89.4f) > 3.2e38f);
    ok("coshf(89.4) finite", isfinite(coshf(89.4f)));
    ok("coshf(89.4) range", coshf(89.4f) > 3.2e38f);
    ok("sinhf(90) overflow", isinf(sinhf(90.0f)));
    ok("coshf(90) overflow", isinf(coshf(90.0f)));

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
