/* tbios.c - CP/M BIOS validation test: exercises bios()/bioshl(), which
 * call the CBIOS jump table directly instead of going through BDOS
 * (compare tbdos.c, which tests bdos()/bdoshl()). */

#include <stdio.h>

extern int bios(int fn, int dearg);
extern int bioshl(int fn, int dearg);

int main(void)
{
    int r;

    printf("tbios start\n");

    /* BIOS 2: console status. In a non-interactive/batch run there's no
     * pending keystroke, so this should be 0 - same story as BDOS 11's
     * console status test in tbdos.c. */
    r = bios(2, 0);
    printf("BIOS const: %d\n", r);

    /* BIOS 4: console output. Writes the character in C directly to the
     * screen, bypassing BDOS entirely. */
    bios(4, 'H');
    bios(4, 'i');
    bios(4, '!');
    bios(4, '\r');
    bios(4, '\n');

    /* BIOS 7: reader input. No paper tape reader is attached; ntvcm
     * returns ^Z (26) unconditionally for this function. */
    r = bios(7, 0);
    printf("BIOS reader: %d\n", r);

    /* bioshl(): same calls as above, but returns the untouched HL instead
     * of bios()'s zero-extended-A result. None of the BIOS functions this
     * emulator implements (0-7) write anything to HL, only A - so what
     * comes back is whatever HL held on entry to the BIOS call: the
     * jump-table entry address for fn itself (ntvcm's BIOS jump-table base
     * plus 3*fn). That's an ntvcm implementation detail, not part of the
     * CP/M BIOS contract, but this does prove bioshl() is a distinct entry
     * point that returns raw HL rather than bios()'s zero-extended A - the
     * same relationship bdos()/bdoshl() have in tbdos.c. */
    r = bioshl(2, 0);
    printf("BIOS const raw hl: %d\n", r);

    r = bioshl(7, 0);
    printf("BIOS reader raw hl: %d\n", r);

    /* exercise bios()/bioshl() through function pointers too, since dcc
     * emits a different DCCRTL entry point for direct calls (fastcall
     * __biosf/__bhf) vs. indirect calls (stack-marshaling _bios/_bioshl) -
     * see tfpcall.c. */
    {
        int (*fp_bios)(int, int) = bios;
        int (*fp_bioshl)(int, int) = bioshl;
        printf("BIOS const via fnptr: %d\n", fp_bios(2, 0));
        printf("BIOS const raw hl via fnptr: %d\n", fp_bioshl(2, 0));
    }

    printf("tbios completed with great success\n");

    return 0;
}
