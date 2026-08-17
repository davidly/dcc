/* texbnd08.c - exec path bounds and synthetic loader-layout checks */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int xacom_test( const char *src, char *dst );
extern int xwcp_test( const char *src, char *dst, int length );
extern unsigned xfit_test( unsigned fbase, unsigned records, unsigned r2 );

#asm
        public  _xacom_test
        public  _xwcp_test
        public  _xfit_test
        extrn   __xacom
        extrn   __xwcp
        extrn   __xfit

_xacom_test:
        push    ix
        ld      ix,0
        add     ix,sp
        ld      l,(ix+4)
        ld      h,(ix+5)
        ld      e,(ix+6)
        ld      d,(ix+7)
        call    __xacom
        ld      hl,0
        jr      nc,xat_done
        dec     hl
xat_done:
        pop     ix
        ret

_xwcp_test:
        push    ix
        ld      ix,0
        add     ix,sp
        ld      l,(ix+4)
        ld      h,(ix+5)
        ld      e,(ix+6)
        ld      d,(ix+7)
        ld      b,(ix+8)
        call    __xwcp
        ld      e,(ix+4)
        ld      d,(ix+5)
        or      a
        sbc     hl,de
        pop     ix
        ret

_xfit_test:
        push    ix
        ld      ix,0
        add     ix,sp
        ld      l,(ix+4)
        ld      h,(ix+5)
        ld      e,(ix+6)
        ld      d,(ix+7)
        ld      a,(ix+8)
        call    __xfit
        jr      nc,xft_ok
        ld      hl,0
xft_ok:
        pop     ix
        ret
#endasm

static int fails;
static unsigned char guard[ 18 ];

static void fail( const char *what )
{
    printf( "FAIL %s\n", what );
    fails++;
}

static void fill_guard( void )
{
    int i;
    for ( i = 0; i < (int) sizeof guard; i++ )
        guard[ i ] = 0xa5;
}

static void path_case( const char *src, const char *expected, int valid )
{
    int result;

    fill_guard();
    result = xacom_test( src, (char *) guard + 1 );
    if ( guard[ 0 ] != 0xa5 || guard[ 17 ] != 0xa5 )
        fail( "xacom guard" );
    if ( valid )
    {
        if ( result != 0 )
            fail( "xacom rejected valid path" );
        else if ( strcmp( (char *) guard + 1, expected ) != 0 )
            fail( "xacom output" );
        if ( guard[ 16 ] != 0xa5 )
            fail( "xacom used byte past maximum path" );
    }
    else if ( result != -1 )
        fail( "xacom accepted invalid path" );
}

static void test_paths( void )
{
    static char high_path[ 3 ] = { 'A', 0x7f, 0 };

    path_case( (const char *) 0, "", 0 );
    path_case( "12345678", "12345678.COM", 1 );
    path_case( "p:12345678.abc", "P:12345678.abc", 1 );
    path_case( "a-b$c", "a-b$c.COM", 1 );
    path_case( "", "", 0 );
    path_case( "123456789", "", 0 );
    path_case( "123456789012", "", 0 );
    path_case( "A.ABCD", "", 0 );
    path_case( "A.", "", 0 );
    path_case( ".COM", "", 0 );
    path_case( "Q:A.COM", "", 0 );
    path_case( "1:A.COM", "", 0 );
    path_case( "A:B:C.COM", "", 0 );
    path_case( "A/B.COM", "", 0 );
    path_case( "A?.COM", "", 0 );
    path_case( "A.B.C", "", 0 );
    path_case( "A%B.COM", "", 0 );
    path_case( high_path, "", 0 );
}

static void test_word_copy( void )
{
    static const char word[] = "ABCDEFGHIJKLMNOPQRST NEXT";
    static const char tab_word[] = "ABC\tNEXT";
    static const char ctl_word[] = { 'A', 'B', 1, 'N', 'E', 'X', 'T', 0 };
    int offset;

    fill_guard();
    offset = xwcp_test( word, (char *) guard + 1, strlen( word ) );
    if ( offset != 20 )
        fail( "xwcp source cursor" );
    if ( strcmp( (char *) guard + 1, "ABCDEFGHIJKLMNO" ) != 0 )
        fail( "xwcp truncation" );
    if ( guard[ 0 ] != 0xa5 || guard[ 17 ] != 0xa5 )
        fail( "xwcp guard" );

    fill_guard();
    offset = xwcp_test( tab_word, (char *) guard + 1, strlen( tab_word ) );
    if ( offset != 3 || strcmp( (char *) guard + 1, "ABC" ) != 0 )
        fail( "xwcp tab delimiter" );

    fill_guard();
    offset = xwcp_test( ctl_word, (char *) guard + 1, 7 );
    if ( offset != 2 || strcmp( (char *) guard + 1, "AB" ) != 0 )
        fail( "xwcp control delimiter" );
}

static void test_load_bounds( void )
{
    if ( xfit_test( 0xf000, 477, 0 ) != 0xefb0 )
        fail( "xfit ordinary exact boundary" );
    if ( xfit_test( 0xf000, 478, 0 ) != 0 )
        fail( "xfit ordinary overlap" );
    if ( xfit_test( 0xffff, 509, 0 ) != 0xffaf )
        fail( "xfit 16-bit maximum boundary" );
    if ( xfit_test( 0xffff, 510, 0 ) != 0 )
        fail( "xfit 16-bit wrap" );
    if ( xfit_test( 0xf000, 0, 1 ) != 0 )
        fail( "xfit R2 record count" );
    if ( xfit_test( 0x015f, 0, 0 ) != 0 )
        fail( "xfit low TPA underflow" );
    if ( xfit_test( 0x0160, 0, 0 ) != 0x0110 )
        fail( "xfit minimum empty layout" );
}

int main( int argc, char **argv )
{
    int result;

    if ( argc == 2 && strcmp( argv[ 1 ], "XBOUND" ) == 0 )
    {
        printf( "texbnd08 ok\n" );
        return 0;
    }

    test_paths();
    test_word_copy();
    test_load_bounds();

    errno = 0;
    result = exec( "123456789012", "" );
    if ( result != -1 || errno != EINVAL )
        fail( "exec invalid path result/errno" );

    if ( fails )
    {
        printf( "texbnd08 FAILED %d\n", fails );
        return 1;
    }

    exec( "A:texbnd08", " XBOUND" );
    printf( "FAIL boundary self exec returned\n" );
    return 1;
}
