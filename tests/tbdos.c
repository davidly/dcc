/* tbdos.c - simple CP/M BDOS validation test.
 *
 * Exercises bdos()/bdoshl() (compare tbios.c, which tests bios()/bioshl()).
 *
 * A few of the values these BDOS calls return are inherently emulator or
 * host-environment details rather than something a portable test can pin
 * down: the exact CP/M version reported (2.2 vs. 3.1, ...) is a per-
 * emulator/per-mode choice, and whether a console character happens to be
 * "pending" depends on the host/terminal, not on dcc or the BDOS
 * implementation. Asserting a specific number for either turns this test
 * into a check of one particular emulator's configuration rather than of
 * dcc - see the "Other CP/M emulators" section of the README for a survey
 * of how much these vary. Where a raw value can't be predicted portably,
 * this test checks internal consistency instead - do all of dcc's call
 * paths to the same BDOS function agree with each other - which is the
 * thing actually worth verifying here. */

#include <stdio.h>

extern int bdos(int fn, int dearg);
extern int bdoshl(int fn, int dearg);

static void putstr(const char *s)
{
    while (*s)
        bdos(2, *s++);
}

int main(void)
{
    int ver, verhl, verhlfp;

    printf("tbdos start\n");

    /* first try to read the command tail if there is one */
    /* if there is one, the first character may or may not be a space */
    int len = * (char *) 0x80;
    char *pcmdtail = (char *) 0x81;
    char ct[ 0x80 ];
    memcpy( ct, pcmdtail, len );
    ct[ len ] = 0;
    printf( "command tail len: %d\n", len );
    printf( "command tail: '%s'\n", ct );

    /* BDOS 2: console output */
    bdos(2, 'H');
    bdos(2, 'i');
    bdos(2, '!');
    bdos(2, '\r');
    bdos(2, '\n');

    /* BDOS 9: print $-terminated string */
    {
        static char msg[] = "BDOS 9 string output works$";
        bdos(9, (int)msg);
        bdos(2, '\r');
        bdos(2, '\n');
    }

    /* BDOS 12: get CP/M version. dcc emits three different call paths that
     * can reach this: bdos() itself, bdoshl() (which returns the full HL
     * result instead of bdos()'s zero-extended A - on a 2-digit BCD-style
     * version byte in A, H is conventionally 0, so the two should agree),
     * and bdoshl() reached through a function pointer, since dcc emits a
     * distinct fastcall entry point (__bhlf) for direct calls vs. a
     * stack-marshaling one (_bdoshl) for indirect calls - see tfpcall.c.
     * All three should report the same version and agree with each other
     * regardless of which CP/M version/mode the host emulator implements. */
    ver = bdos(12, 0);
    verhl = bdoshl(12, 0);
    {
        int (*fp_bdoshl)(int, int) = bdoshl;
        verhlfp = fp_bdoshl(12, 0);
    }
    {
        int major = (ver >> 4) & 0x0f;
        printf("BDOS major version recognized (2 or 3): %s\n",
               (major == 2 || major == 3) ? "yes" : "no");
    }
    printf("bdos()/bdoshl() agree: %s\n", (ver == verhl) ? "yes" : "no");
    printf("bdoshl() direct/fnptr agree: %s\n", (verhl == verhlfp) ? "yes" : "no");

    /* BDOS 11: console status. Whether a character happens to be pending
     * depends on the host/terminal (and, for a non-interactive/redirected
     * run, on how a given emulator chooses to answer that question) - not
     * on dcc or the BDOS implementation - so there's nothing portable to
     * assert about the result; just confirm the call completes. */
    (void) bdos(11, 0);
    printf("Console status: call completed\n");

    /* BDOS 6: direct console I/O status poll. This one is allowed to
     * return an arbitrary pending character code rather than a simple
     * ready/not-ready flag, so there's nothing portable to assert about
     * its result at all - just confirm the call completes. */
    (void) bdos(6, 0xff);
    printf("Direct console poll: call completed\n");

    putstr("tbdos completed with great success\r\n");

    return 0;
}
