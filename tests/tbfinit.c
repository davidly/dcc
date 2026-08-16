// tbfinit (was c9917): regression - designated initializers for a struct
// whose members are bit-fields (`{ .red = 31, .green = 0, .blue = 0 }`) must
// pack all same-unit designators into one store; dcc previously consumed the
// second designator as a positional value and errored with DCC-E0904.
// Also covers (review edge cases): revisiting a storage unit after leaving it
// (must merge, not clobber), a designator following the last declared field,
// reversed designators within one unit, and the same field designated twice
// (last one wins), for both locals and globals.
// Scenario: RGB565 packing/unpacking with bit-fields and read-modify-write.
#include <stdio.h>
#include <stdint.h>

#ifndef MIR_CLOBBER_GMIX_A
#define MIR_CLOBBER_GMIX_A 1
#endif

// Bit-field layout for a 16-bit RGB565 pixel. Plain unsigned keeps it portable.
struct Rgb565 {
    unsigned blue  : 5;
    unsigned green : 6;
    unsigned red   : 5;
};

static uint16_t pack(const struct Rgb565 *p)
{
    return (uint16_t)((p->red << 11) | (p->green << 5) | p->blue);
}

// Unit-revisit shapes: a/b share unit 0, x separates, d/e share a later unit.
struct Mix {
    unsigned a : 5;
    unsigned b : 6;
    int x;
    unsigned d : 3;
    unsigned e : 4;
};

// Global designated bit-field initializers exercise the data-image writer.
static struct Mix gmix = { .a = MIR_CLOBBER_GMIX_A, .d = 2, .b = 3 }; // revisit unit 0
static struct Rgb565 grev = { .red = 7, .blue = 6, .green = 5 }; // reversed

static void check(const char *name, int got, int want, int *fails)
{
    if (got != want) {
        printf("FAIL %s got %d expected %d\n", name, got, want);
        *fails = *fails + 1;
    }
}

int main(void)
{
    struct Rgb565 px = { .red = 31, .green = 0, .blue = 0 }; // pure red
    int fails = 0;

    // Local edge cases mirroring the globals above.
    struct Mix lmix = { .a = 1, .d = 2, .b = 3 };  // revisit unit 0 after .d
    struct Rgb565 lrev = { .red = 7, .blue = 6, .green = 5 };
    struct Rgb565 ldup = { .green = 1, .green = 9 }; // same field twice

    check("gmix.a", (int)gmix.a, 1, &fails);
    check("gmix.b", (int)gmix.b, 3, &fails);
    check("gmix.x", gmix.x, 0, &fails);
    check("gmix.d", (int)gmix.d, 2, &fails);
    check("gmix.e", (int)gmix.e, 0, &fails);
    check("grev.r", (int)grev.red, 7, &fails);
    check("grev.g", (int)grev.green, 5, &fails);
    check("grev.b", (int)grev.blue, 6, &fails);
    check("lmix.a", (int)lmix.a, 1, &fails);
    check("lmix.b", (int)lmix.b, 3, &fails);
    check("lmix.x", lmix.x, 0, &fails);
    check("lmix.d", (int)lmix.d, 2, &fails);
    check("lmix.e", (int)lmix.e, 0, &fails);
    check("lrev.r", (int)lrev.red, 7, &fails);
    check("lrev.g", (int)lrev.green, 5, &fails);
    check("lrev.b", (int)lrev.blue, 6, &fails);
    check("ldup.g", (int)ldup.green, 9, &fails);
    check("ldup.r", (int)ldup.red, 0, &fails);
    if (fails == 0)
        printf("bfinit edge cases pass\n");

    // Read-modify-write on bit-fields: fade toward white.
    px.green += 20;
    px.blue += 10;
    px.red -= 1;

    uint16_t packed = pack(&px);

    // Round-trip back out of the packed word.
    unsigned r = (packed >> 11) & 0x1F;
    unsigned g = (packed >> 5) & 0x3F;
    unsigned b = packed & 0x1F;

    printf("c9917 packed=%04X r=%u g=%u b=%u\n",
           (unsigned)packed, r, g, b);
    return 0;
}
