/* Exhaust all 65,536 inputs to the byte multiply used by float FMA.
 * Also verify the register-preservation contract used by its callers. */
#include <stdio.h>
#include <stdint.h>

unsigned int bad;
unsigned int mulbyte(unsigned int a, unsigned int b);

#asm
        extrn __m88u
_mulbyte:
        push ix
        ld ix,0
        add ix,sp
        ld a,(ix+4)
        ld c,(ix+6)
        ld b,0
        ld d,90
        call __m88u
        cp (ix+4)
        jr nz,MBFAIL
        ld a,d
        cp 90
        jr nz,MBFAIL
        ld a,b
        or a
        jr nz,MBFAIL
        ld a,c
        cp (ix+6)
        jr nz,MBFAIL
        ld a,h
        or a
        jr z,MBDONE
MBFAIL:
        ld a,1
        ld (_bad),a
MBDONE:
        ld h,l
        ld l,e
        pop ix
        ret
#endasm

int main(void)
{
    unsigned int a, b;
    for (a = 0; a < 256; ++a)
        for (b = 0; b < 256; ++b)
            if (mulbyte(a, b) != (uint16_t)(a * b)) {
                printf("FAIL %u %u\n", a, b);
                return 1;
            }
    printf("products=65536 preserved=%d\n", bad == 0);
    return bad != 0;
}
