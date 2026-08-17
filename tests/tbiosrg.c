/* tbiosrg.c - exercise every practical CP/M BIOS register/return form.
 *
 * The test temporarily redirects the warm-boot vector to an in-program fake
 * 17-entry BIOS table. This keeps the assertions independent of which BIOS
 * disk calls a host emulator implements while still exercising the public
 * wrappers and their real jump-table dispatch.
 */

#include <stdio.h>
#include <stdlib.h>

unsigned capbc;
unsigned capde;
unsigned oldwb;
unsigned char xltab[4];

extern void bf_on(void);
extern void bf_off(void);

#asm
        public  _bf_on
_bf_on:
        ld      hl,(0001h)
        ld      (_oldwb),hl
        ld      hl,BFTAB+3
        ld      (0001h),hl
        ret

        public  _bf_off
_bf_off:
        ld      hl,(_oldwb)
        ld      (0001h),hl
        ret

BFTAB:
        jp      BFDEF
        jp      BFDEF
        jp      BFN2
        jp      BFDEF
        jp      BFN4
        jp      BFDEF
        jp      BFDEF
        jp      BFDEF
        jp      BFDEF
        jp      BFN9
        jp      BFN10
        jp      BFDEF
        jp      BFDEF
        jp      BFDEF
        jp      BFDEF
        jp      BFDEF
        jp      BFN16

BFN2:
        ld      (_capbc),bc
        ld      (_capde),de
        ld      hl,1234h
        ld      a,5ah
        ret

BFN4:
        ld      (_capbc),bc
        ld      (_capde),de
        ld      hl,4444h
        ld      a,44h
        ret

BFN9:
        ld      (_capbc),bc
        ld      (_capde),de
        ld      hl,9009h
        ld      a,9
        ret

BFN10:
        ld      (_capbc),bc
        ld      (_capde),de
        ld      hl,1010h
        ld      a,10h
        ret

BFN16:
        ld      (_capbc),bc
        ld      (_capde),de
        ld      hl,0a016h
        ld      a,16h
        ret

BFDEF:
        ld      (_capbc),bc
        ld      (_capde),de
        ld      hl,0d00dh
        xor     a
        ret
#endasm

static const char *yn(int value)
{
    return value ? "yes" : "no";
}

int main(void)
{
    int (*fp)(int, int, int);
    int status_ok;
    int carg_ok;
    int bcarg_ok;
    int seldsk_ok;
    int seldskr_ok;
    int sectran_ok;
    int fnptr_ok;
    int result;
    unsigned xaddr;
    int passed;

    bf_on();

    result = bios(2, 0);
    status_ok = result == 0x5a && capbc == 0 && capde == 0;

    result = bios(4, 'Q');
    carg_ok = result == 0x44 && capbc == (unsigned)'Q'
              && capde == (unsigned)'Q';

    result = bios(10, 0x2345);
    bcarg_ok = result == 0x10 && capbc == 0x2345U && capde == 0x2345U;

    result = bioshl(9, 3);
    seldsk_ok = (unsigned)result == 0x9009U && capbc == 3 && capde == 3;

    result = biosreg(9, 0x1203, 0x3401);
    seldskr_ok = (unsigned)result == 0x9009U
                 && capbc == 0x1203U && capde == 0x3401U;

    xaddr = (unsigned)xltab;
    result = biosreg(16, 0x0123, (int)xaddr);
    sectran_ok = (unsigned)result == 0xa016U
                 && capbc == 0x0123U && capde == xaddr;

    fp = biosreg;
    result = fp(16, 0x4567, (int)0x89abU);
    fnptr_ok = (unsigned)result == 0xa016U
               && capbc == 0x4567U && capde == 0x89abU;

    bf_off();

    printf("tbiosrg start\n");
    printf("status A/no-arg: %s\n", yn(status_ok));
    printf("C byte arg: %s\n", yn(carg_ok));
    printf("BC word arg: %s\n", yn(bcarg_ok));
    printf("SELDSK convenience: %s\n", yn(seldsk_ok));
    printf("SELDSK separate BC/DE: %s\n", yn(seldskr_ok));
    printf("SECTRAN direct: %s\n", yn(sectran_ok));
    printf("SECTRAN fnptr: %s\n", yn(fnptr_ok));

    passed = status_ok + carg_ok + bcarg_ok + seldsk_ok
             + seldskr_ok + sectran_ok + fnptr_ok;
    printf("tbiosrg %d/7\n", passed);

    return passed != 7;
}
