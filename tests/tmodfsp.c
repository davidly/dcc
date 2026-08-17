/* Bit-exact modff mantissa-mask boundaries and special values. */

#include <math.h>
#include <stdio.h>

typedef union {
    float f;
    unsigned long u;
} FU;

static int checks;
static int failures;

static void ck(unsigned long input, unsigned long want_i,
               unsigned long want_f, const char *name)
{
    FU x;
    FU ip;
    FU fp;

    x.u = input;
    fp.f = modff(x.f, &ip.f);
    checks += 2;
    if (ip.u != want_i) {
        failures++;
        printf("FAIL %s ip=%lx want=%lx\n", name, ip.u, want_i);
    }
    if (fp.u != want_f) {
        failures++;
        printf("FAIL %s fp=%lx want=%lx\n", name, fp.u, want_f);
    }
}

int main(void)
{
    ck(0x00000000UL, 0x00000000UL, 0x00000000UL, "pz");
    ck(0x80000000UL, 0x80000000UL, 0x80000000UL, "nz");
    ck(0x00000001UL, 0x00000000UL, 0x00000001UL, "psub");
    ck(0x80000001UL, 0x80000000UL, 0x80000001UL, "nsub");
    ck(0x3f7fffffUL, 0x00000000UL, 0x3f7fffffUL, "lt1");
    ck(0x3f800000UL, 0x3f800000UL, 0x00000000UL, "one");
    ck(0xbf800000UL, 0xbf800000UL, 0x80000000UL, "none");
    ck(0x3fc00000UL, 0x3f800000UL, 0x3f000000UL, "onehalf");
    ck(0x437fffffUL, 0x437f0000UL, 0x3f7fff00UL, "e134");
    ck(0x43ffffffUL, 0x43ff8000UL, 0x3f7ffe00UL, "e135");
    ck(0x477fffffUL, 0x477fff00UL, 0x3f7f0000UL, "e142");
    ck(0x47ffffffUL, 0x47ffff80UL, 0x3f7e0000UL, "e143");
    ck(0x4affffffUL, 0x4afffffeUL, 0x3f000000UL, "e149");
    ck(0x4b000000UL, 0x4b000000UL, 0x00000000UL, "e150");
    ck(0xc37fffffUL, 0xc37f0000UL, 0xbf7fff00UL, "neg");
    ck(0x7f800000UL, 0x7f800000UL, 0x00000000UL, "pinf");
    ck(0xff800000UL, 0xff800000UL, 0x80000000UL, "ninf");
    ck(0x7fc12345UL, 0x7fc12345UL, 0x7fc12345UL, "qnan");
    ck(0x7f812345UL, 0x7f812345UL, 0x7f812345UL, "snan");

    printf("checks=%d failures=%d\n", checks, failures);
    puts("RESULT: PASS");
    return failures != 0;
}
