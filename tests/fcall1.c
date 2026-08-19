/* Functional test for __fastcall: hand-asm-bodied functions taking 1, 2, and
 * 3 register-passed arguments, called from ordinary C call sites (including
 * one with an array-indexing pointer argument, the exact shape that once
 * miscompiled the strlen/strchr fastcall special cases before
 * gen_fastcall_arg existed - see its comment in dcc_ast_gen_expr.c). */
#include <stdio.h>
#include <stdint.h>

extern uint16_t __fastcall double_it( uint16_t x );
extern uint16_t __fastcall add2( uint16_t a, uint16_t b );
extern uint16_t __fastcall combine3( uint16_t a, uint16_t b, uint16_t c );
extern uint16_t __fastcall strlen16( char * s );

#asm
        public _double_it
_double_it:
        add     hl,hl
        ret

        public _add2
_add2:
        add     hl,de
        ret

        ; a in HL, b in DE, c in BC -> (a+b)*2 + c
        public _combine3
_combine3:
        add     hl,de
        add     hl,hl
        add     hl,bc
        ret

        ; s in HL -> string length, exercising a real pointer argument
        public _strlen16
_strlen16:
        push    bc
        ld      bc,0
_sl16_loop:
        ld      a,(hl)
        or      a
        jr      z,_sl16_done
        inc     hl
        inc     bc
        jr      _sl16_loop
_sl16_done:
        ld      h,b
        ld      l,c
        pop     bc
        ret
#endasm

char * names[3] = { "zero", "one", "twotwo" };

int main( void )
{
    uint16_t x, y, z, nn;

    x = double_it( 21 );
    y = add2( 100, 23 );
    z = combine3( 10, 5, 3 );
    nn = 0;
    printf( "double_it(21)=%u\n", x );
    printf( "add2(100,23)=%u\n", y );
    printf( "combine3(10,5,3)=%u\n", z );
    printf( "strlen(zero)=%u\n", strlen16( names[ nn++ ] ) );
    printf( "strlen(one)=%u\n", strlen16( names[ nn++ ] ) );
    printf( "strlen(twotwo)=%u\n", strlen16( names[ nn++ ] ) );
    return 0;
}
