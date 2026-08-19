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
uint16_t vals[2] = { 7, 70 };

int main( void )
{
    uint16_t x, y, z, nn, mm;
    uint16_t w, v;

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

    /* Regression for a __fastcall call-site codegen bug: dcc normally
     * evaluates a call's arguments left to right, so the LAST-declared one
     * is usually the last thing computed before the call, and may still be
     * sitting in HL via the compiler's "forward this value directly,
     * skip a real store" optimization (see mir_take_forwarded_hl_call_
     * argument's comment in dcc_mir_spilled_cfg.c). The hardcoded
     * memset/memcpy/... fastcalls already guard against that by pushing
     * the forwarded value before loading any earlier argument; the
     * user-__fastcall path (mir_call_is_user_fastcall) didn't, so loading
     * an EARLIER, trivially-foldable argument (a bare constant here)
     * clobbered HL before the forwarded LAST argument's real value (a
     * genuine memory read, not a constant - vals[] is deliberately
     * non-const-indexed via mm++ so it can't fold away) was ever used.
     * Both fastcall registers ended up holding the first argument's value
     * and the second/third arguments were silently lost. Found while
     * applying __fastcall to tests/a1.c's op_math(). */
    mm = 0;
    w = add2( 5, vals[ mm++ ] );
    v = combine3( 2, 3, vals[ mm++ ] );
    printf( "add2(5,vals[0]=7)=%u\n", w );
    printf( "combine3(2,3,vals[1]=70)=%u\n", v );
    return 0;
}
