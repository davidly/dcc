/**
 * @file dccint.c
 * @brief Mixed DCC C and Z80 assembly interrupt timer example.
 *
 * @par Role
 * Installs an IM 1 wrapper and counts periodic port-52 interrupts in C.
 *
 * @par Boundary
 * Requires an emulator or I/O adapter that provides the Altair interrupt
 * timer on port 52. The C handler must not call non-reentrant services.
 *
 * @par Origin
 * Adapted from esp32-altair-8800/Apps/DCCINT/DCCINT.C.
 */

/* --8<-- [start:example] */
#include <stdio.h>

#define TARGET_TICKS 50

volatile unsigned int irq_ticks;

extern void irq_install(volatile unsigned int *counter, void (*handler)(void));
extern void irq_wait(void);
extern unsigned int irq_count(void);
extern void irq_remove(void);

void irq_tick(void)
{
    irq_ticks++;
}

#asm
        public  _irq_install
        public  _irq_wait
        public  _irq_count
        public  _irq_remove

INTVEC  equ     0038h
INTPORT equ     52

; Save CP/M's vector and bind the C state passed through the normal dcc ABI:
; IX+4/5 = counter pointer, IX+6/7 = handler function pointer.
_irq_install:
        di
        push    ix
        ld      ix,0
        add     ix,sp
        ld      l,(ix+4)
        ld      h,(ix+5)
        ld      (irq_counter),hl
        ld      l,(ix+6)
        ld      h,(ix+7)
        ld      (irq_call+1),hl
        pop     ix

        ld      a,(INTVEC)
        ld      (irq_old),a
        ld      a,(INTVEC+1)
        ld      (irq_old+1),a
        ld      a,(INTVEC+2)
        ld      (irq_old+2),a

        ld      a,0c3h
        ld      (INTVEC),a
        ld      hl,irq_isr
        ld      (INTVEC+1),hl

        im      1
        ld      a,10
        out     (INTPORT),a
        ei
        ret

; Sleep until the next accepted interrupt. The ISR resumes after HALT.
_irq_wait:
        halt
        ret

; Return an atomic 16-bit snapshot in HL using the normal dcc return ABI.
_irq_count:
        di
        ld      hl,(irq_counter)
        ld      e,(hl)
        inc     hl
        ld      d,(hl)
        ex      de,hl
        ei
        ret

; Stop the source before restoring CP/M's original three vector bytes.
_irq_remove:
        di
        xor     a
        out     (INTPORT),a
        ld      a,(irq_old)
        ld      (INTVEC),a
        ld      a,(irq_old+1)
        ld      (INTVEC+1),a
        ld      a,(irq_old+2)
        ld      (INTVEC+2),a
        ret

; Preserve all visible Z80 register sets before entering ordinary C.
irq_isr:
        push    af
        push    bc
        push    de
        push    hl
        push    ix
        push    iy

        ex      af,af'
        push    af
        ex      af,af'
        exx
        push    bc
        push    de
        push    hl
        exx

irq_call:
        call    0000h

        exx
        pop     hl
        pop     de
        pop     bc
        exx
        ex      af,af'
        pop     af
        ex      af,af'

        pop     iy
        pop     ix
        pop     hl
        pop     de
        pop     bc
        pop     af
        ei
        reti

irq_counter:
        dw      0
irq_old:
        ds      3
#endasm

int main(void)
{
    unsigned int count;

    irq_ticks = 0;
    printf("DCC C + Z80 interrupt example\n");
    printf("Counting %u timer interrupts at 10 Hz...\n", TARGET_TICKS);

    irq_install(&irq_ticks, irq_tick);
    do {
        irq_wait();
        count = irq_count();
    } while (count < TARGET_TICKS);
    irq_remove();

    printf("C handler count: %u\n", count);
    return count == TARGET_TICKS ? 0 : 1;
}
/* --8<-- [end:example] */