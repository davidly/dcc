/* Verify that setjmp/longjmp preserves dcc's callee-saved IY register. */

#include <stdio.h>
#include <setjmp.h>

jmp_buf iyenv;

extern int iytest(void);

#asm
	public _iytest
	extrn _setjmp
	extrn _longjmp
_iytest:
	push iy
	ld iy,1234h

	ld hl,_iyenv
	push hl
	call _setjmp
	pop bc

	ld a,h
	or l
	jr nz,sjiy_resumed

	ld iy,5678h
	ld hl,7
	push hl
	ld hl,_iyenv
	push hl
	call _longjmp

sjiy_resumed:
	push iy
	pop de
	ld a,d
	cp 12h
	jr nz,sjiy_failed
	ld a,e
	cp 34h
	jr nz,sjiy_failed
	ld hl,1
	jr sjiy_done

sjiy_failed:
	ld hl,0

sjiy_done:
	pop iy
	ret
#endasm

int main(void)
{
    if (!iytest()) {
        printf("setjmp IY preservation failed\n");
        return 1;
    }
    printf("setjmp IY preservation passed\n");
    return 0;
}
