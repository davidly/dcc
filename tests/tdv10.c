/* Exhaustive /10 runtime check.  The oracle increments quotient and remainder
 * counters rather than using the division or modulo operators being tested. */
#include <stdio.h>

unsigned int input, remainder;
int preserved = 1;
extern unsigned int fused(void), fixed(void), ten(void);

#asm
        extrn   __udivmod
        extrn   __q2u
        extrn   __d10u

        public  _fused
_fused:
        call    d10setup
        push    af
        call    __udivmod
        jp      d10check
        public  _fixed
_fixed:
        call    d10setup
        push    af
        call    __q2u
        jp      d10check
        public  _ten
_ten:
        call    d10setup
        push    af
        call    __d10u
        jp      d10check

d10setup:
        ld      hl,(_input)
        ld      de,10
        ld      bc,55aah
        ld      a,0a5h
        or      a
        ret

d10check:
        ld      (_remainder),de
        push    hl              ; quotient
        push    af              ; actual AF
        ld      a,b
        cp      55h
        jr      nz,d10badregs
        ld      a,c
        cp      0aah
        jr      nz,d10badregs
        pop     bc              ; actual AF as a word
        pop     de              ; quotient
        pop     hl              ; expected AF
        or      a
        sbc     hl,bc
        jr      nz,d10bad
        ex      de,hl
        ret

d10badregs:
        pop     bc
        pop     de
        pop     hl
d10bad:
        ld      hl,0
        ld      (_preserved),hl
        ex      de,hl
        ret
#endasm

static int signed_ten(int n, int q, int r)
{
    return n / 10 == q && n % 10 == r &&
           n / -10 == -q && n % -10 == r;
}

int main(void)
{
    unsigned int q = 0, r = 0;
    input = 0;
    do {
        if (fused() != q || remainder != r ||
            fixed() != q || remainder != r ||
            ten() != q || remainder != r || !preserved) {
            printf("FAIL /10 at %u\n", input);
            return 1;
        }
        if (++r == 10) {
            r = 0;
            ++q;
        }
    } while (++input != 0);
    if (!signed_ten(-32768, -3276, -8) ||
        !signed_ten(-32767, -3276, -7) ||
        !signed_ten(-10000, -1000, 0) ||
        !signed_ten(-11, -1, -1) || !signed_ten(-10, -1, 0) ||
        !signed_ten(-9, 0, -9) || !signed_ten(0, 0, 0) ||
        !signed_ten(9, 0, 9) || !signed_ten(10, 1, 0) ||
        !signed_ten(11, 1, 1) || !signed_ten(32767, 3276, 7)) {
        puts("FAIL signed /10");
        return 1;
    }
    puts("/10: 65536 inputs, 3 helpers OK");
    return 0;
}
