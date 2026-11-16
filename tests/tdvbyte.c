/* Exhaustive division check.  The oracle increments quotient and remainder
 * counters rather than using the division or modulo operators being tested. */
#include <stdio.h>

unsigned int input, remainder, divisor;
int preserved = 1;
extern unsigned int fused(void), fixed(void);

#asm
        extrn   __udivmod
        extrn   __q2u

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
d10setup:
        ld      hl,(_input)
        ld      de,(_divisor)
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

int main(void)
{
    static const unsigned int words[] = {
        256, 257, 511, 512, 1000, 32767, 32768, 32769, 40000, 65534, 65535
    };
    unsigned int q, r, i;
    for (i = 1; i <= 266; ++i) {
        divisor = i <= 255 ? i : words[i - 256];
        q = r = input = 0;
        do {
            if (fused() != q || remainder != r ||
                fixed() != q || remainder != r || !preserved) {
                printf("FAIL %u/%u\n", input, divisor);
                return 1;
            }
            if (++r == divisor) {
                r = 0;
                ++q;
            }
        } while (++input != 0);
    }
    puts("division: 17432576 inputs, 2 helpers OK");
    return 0;
}
