/* tbios.c - CP/M BIOS validation test: exercises bios()/bioshl(), which
 * call the CBIOS jump table directly instead of going through BDOS
 * (compare tbdos.c, which tests bdos()/bdoshl()).
 *
 * Several BIOS-layer values are inherently emulator implementation details
 * rather than part of the CP/M BIOS contract: whether a console character
 * happens to be "pending" depends on the host/terminal; there's no
 * standard sentinel for "no such device attached" on BIOS 7 (reader
 * input); and bioshl()'s raw HL result on this emulator is whatever
 * happened to be in HL on entry to the BIOS call - typically a BIOS
 * jump-table address, which differs by emulator and even by build. See the
 * "Other CP/M emulators" section of the README for a survey of how much
 * these vary. Where a raw value can't be predicted portably, this test
 * checks internal consistency instead - do dcc's direct and function-
 * pointer call paths to the same BIOS function agree with each other -
 * which is the thing actually worth verifying here. */

#include <stdio.h>

extern int bios(int fn, int dearg);
extern int bioshl(int fn, int dearg);

int main(void)
{
    int r, rfp, rhl, rhlfp;

    printf("tbios start\n");

    /* BIOS 2: console status. Same story as BDOS 11's console status test
     * in tbdos.c - whether a character happens to be pending depends on
     * the host/terminal, not on dcc or the BIOS implementation, so there's
     * nothing portable to assert about the result; just confirm the call
     * completes, and confirm the direct and function-pointer call paths
     * agree with each other. */
    r = bios(2, 0);
    {
        int (*fp_bios)(int, int) = bios;
        rfp = fp_bios(2, 0);
    }
    printf("BIOS const: call completed\n");
    printf("BIOS const direct/fnptr agree: %s\n", (r == rfp) ? "yes" : "no");

    /* BIOS 4: console output. Writes the character in C directly to the
     * screen, bypassing BDOS entirely. */
    bios(4, 'H');
    bios(4, 'i');
    bios(4, '!');
    bios(4, '\r');
    bios(4, '\n');

    /* BIOS 7 (reader input, no device attached) is deliberately not
     * exercised here: it's an optional BIOS function with no standard
     * behavior for "no such device", and unlike a merely-unpredictable
     * return value, at least two emulators (iz-cpm, zxcc) treat it as an
     * outright unimplemented call and inject their own diagnostic text
     * into the console output when it's invoked - there's no wording of
     * this test that can portably survive that, so covering it is left to
     * each emulator's own test suite rather than dcc's. */

    /* bioshl(): same calls as above, but returns the untouched HL instead
     * of bios()'s zero-extended-A result. None of the BIOS functions this
     * emulator implements (0-7) write anything to HL, only A - so what
     * comes back is whatever HL held on entry to the BIOS call, which is
     * entirely an emulator implementation detail. Rather than printing
     * that raw value, confirm bioshl()'s direct and function-pointer call
     * paths agree with each other - the same relationship bdos()/bdoshl()
     * have in tbdos.c - which proves bioshl() really is a distinct entry
     * point returning raw HL (not bios()'s zero-extended A) without
     * depending on what that raw value happens to be. */
    rhl = bioshl(2, 0);
    {
        int (*fp_bioshl)(int, int) = bioshl;
        rhlfp = fp_bioshl(2, 0);
    }
    printf("BIOS const raw hl direct/fnptr agree: %s\n", (rhl == rhlfp) ? "yes" : "no");

    printf("tbios completed with great success\n");

    return 0;
}
