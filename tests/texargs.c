/* texargs.c - bounded exec/execv command-tail and argv round-trip tests */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SELF "texargs"

static unsigned char raw_box[ 131 ];
static unsigned char arg_box[ 126 ];
static char tail_box[ 129 ];
static char long_arg[ 124 ];
static char *av[ 66 ];

static int all_char( const char *s, int n, int ch )
{
    int i;

    if ( (int) strlen( s ) != n )
        return 0;
    for ( i = 0; i < n; i++ )
        if ( (unsigned char) s[ i ] != (unsigned char) ch )
            return 0;
    return 1;
}

static void fill_chars( char *s, int n, int ch )
{
    int i;

    for ( i = 0; i < n; i++ )
        s[ i ] = (char) ch;
    s[ n ] = 0;
}

static int reject_exec_128( void )
{
    int i;
    int r;

    raw_box[ 0 ] = 0x5a;
    for ( i = 0; i < 128; i++ )
        raw_box[ i + 1 ] = 'R';
    raw_box[ 129 ] = 0;
    raw_box[ 130 ] = 0xa5;
    errno = 0;
    r = exec( SELF, (char *) &raw_box[ 1 ] );
    if ( r != -1 || errno != E2BIG ||
         raw_box[ 0 ] != 0x5a || raw_box[ 130 ] != 0xa5 )
    {
        printf( "FAIL reject_exec_128 r=%d errno=%d guard=%u/%u\n",
                r, errno, raw_box[ 0 ], raw_box[ 130 ] );
        return 0;
    }
    printf( "PASS reject_exec_128\n" );
    return 1;
}

static int reject_execv_128( void )
{
    int r;

    arg_box[ 0 ] = 0x3c;
    fill_chars( (char *) &arg_box[ 1 ], 123, 'V' );
    arg_box[ 125 ] = 0xc3;
    av[ 0 ] = SELF;
    av[ 1 ] = "V";
    av[ 2 ] = "X";
    av[ 3 ] = (char *) &arg_box[ 1 ];
    av[ 4 ] = (char *) 0;
    errno = 0;
    r = execv( SELF, av );
    if ( r != -1 || errno != E2BIG ||
         arg_box[ 0 ] != 0x3c || arg_box[ 125 ] != 0xc3 )
    {
        printf( "FAIL reject_execv_128 r=%d errno=%d guard=%u/%u\n",
                r, errno, arg_box[ 0 ], arg_box[ 125 ] );
        return 0;
    }
    printf( "PASS reject_execv_128\n" );
    return 1;
}

static int reject_many_64( void )
{
    int i;
    int r;

    av[ 0 ] = SELF;
    for ( i = 1; i <= 64; i++ )
        av[ i ] = "X";
    av[ 65 ] = (char *) 0;
    errno = 0;
    r = execv( SELF, av );
    if ( r != -1 || errno != E2BIG )
    {
        printf( "FAIL reject_many_64 r=%d errno=%d\n", r, errno );
        return 0;
    }
    printf( "PASS reject_many_64\n" );
    return 1;
}

static int reject_unrepresentable( const char *name, char *value )
{
    int r;

    av[ 0 ] = SELF;
    av[ 1 ] = value;
    av[ 2 ] = (char *) 0;
    errno = 0;
    r = execv( SELF, av );
    if ( r != -1 || errno != EINVAL )
    {
        printf( "FAIL %s r=%d errno=%d\n", name, r, errno );
        return 0;
    }
    printf( "PASS %s\n", name );
    return 1;
}

static void copy_low( unsigned address, const char *s )
{
    char *d;

    d = (char *) address;
    do
    {
        *d++ = *s;
    } while ( *s++ != 0 );
}

static int fcb_matches( unsigned address, const char *name, const char *type )
{
    unsigned char *f;
    int i;

    f = (unsigned char *) address;
    if ( f[ 0 ] != 0 )
        return 0;
    for ( i = 0; i < 8; i++ )
        if ( f[ i + 1 ] != (unsigned char) name[ i ] )
            return 0;
    for ( i = 0; i < 3; i++ )
        if ( f[ i + 9 ] != (unsigned char) type[ i ] )
            return 0;
    return 1;
}

static int exec_stack_source( void )
{
    char local_tail[ 16 ];

    strcpy( local_tail, " STACK" );
    exec( SELF, local_tail );
    printf( "FAIL exec_stack_source returned errno=%d\n", errno );
    return 1;
}

static int run_parent( void )
{
    if ( !reject_exec_128() )
        return 1;
    if ( !reject_execv_128() )
        return 1;
    if ( !reject_many_64() )
        return 1;
    if ( !reject_unrepresentable( "reject_empty", "" ) )
        return 1;
    if ( !reject_unrepresentable( "reject_space", "two words" ) )
        return 1;
    if ( !reject_unrepresentable( "reject_tab", "two\twords" ) )
        return 1;

    return exec_stack_source();
}

static int stage_stack( int argc, char **argv )
{
    int i;

    if ( argc != 2 || argv[ argc ] != (char *) 0 ||
         strcmp( argv[ 1 ], "STACK" ) != 0 )
    {
        printf( "FAIL exec_stack_source argc=%d\n", argc );
        return 1;
    }
    printf( "PASS exec_stack_source\n" );

    strcpy( tail_box, " S127 " );
    for ( i = 6; i < 126; i++ )
        tail_box[ i ] = 'R';
    tail_box[ 126 ] = 'Z';
    tail_box[ 127 ] = 0;
    tail_box[ 128 ] = (char) 0x96;
    exec( SELF, tail_box );
    printf( "FAIL exec_exact_127 returned errno=%d guard=%u\n",
            errno, (unsigned char) tail_box[ 128 ] );
    return 1;
}

static int stage_s127( int argc, char **argv )
{
    if ( *(unsigned char *) 0x80 != 127 ||
         *(unsigned char *) 0xff != 'Z' ||
         argc != 3 || argv[ argc ] != (char *) 0 ||
         strcmp( argv[ 1 ], "S127" ) != 0 ||
         (int) strlen( argv[ 2 ] ) != 121 ||
         argv[ 2 ][ 120 ] != 'Z' )
    {
        printf( "FAIL exec_exact_127 len=%u argc=%d last=%u\n",
                *(unsigned char *) 0x80, argc,
                *(unsigned char *) 0xff );
        return 1;
    }
    printf( "PASS exec_exact_127\n" );

    fill_chars( long_arg, 121, 'V' );
    av[ 0 ] = SELF;
    av[ 1 ] = "V127";
    av[ 2 ] = long_arg;
    av[ 3 ] = (char *) 0;
    execv( SELF, av );
    printf( "FAIL execv_exact_127 returned errno=%d\n", errno );
    return 1;
}

static int stage_v127( int argc, char **argv )
{
    if ( *(unsigned char *) 0x80 != 127 ||
         *(unsigned char *) 0xff != 'V' ||
         argc != 3 || argv[ argc ] != (char *) 0 ||
         strcmp( argv[ 1 ], "V127" ) != 0 ||
         !all_char( argv[ 2 ], 121, 'V' ) )
    {
        printf( "FAIL execv_exact_127 len=%u argc=%d\n",
                *(unsigned char *) 0x80, argc );
        return 1;
    }
    printf( "PASS execv_exact_127\n" );

    copy_low( 0x80, " O80 X" );
    exec( SELF, (char *) 0x80 );
    printf( "FAIL exec_overlap_0080 returned errno=%d\n", errno );
    return 1;
}

static int stage_o80( int argc, char **argv )
{
    if ( *(unsigned char *) 0x80 != 6 ||
         *(unsigned char *) 0x87 != 0x0d ||
         argc != 3 || argv[ argc ] != (char *) 0 ||
         strcmp( argv[ 1 ], "O80" ) != 0 ||
         strcmp( argv[ 2 ], "X" ) != 0 )
    {
        printf( "FAIL exec_overlap_0080 len=%u argc=%d\n",
                *(unsigned char *) 0x80, argc );
        return 1;
    }
    printf( "PASS exec_overlap_0080\n" );

    copy_low( 0x70, " A1.TX\tB2.BIN\1SFCB" );
    exec( SELF, (char *) 0x70 );
    printf( "FAIL exec_low_fcb_dma returned errno=%d\n", errno );
    return 1;
}

static int stage_sfcb( int argc, char **argv )
{
    if ( argc != 4 || argv[ argc ] != (char *) 0 ||
         strcmp( argv[ 1 ], "A1.TX" ) != 0 ||
         strcmp( argv[ 2 ], "B2.BIN" ) != 0 ||
         strcmp( argv[ 3 ], "SFCB" ) != 0 ||
         !fcb_matches( 0x5c, "A1      ", "TX " ) ||
         !fcb_matches( 0x6c, "B2      ", "BIN" ) )
    {
        printf( "FAIL exec_low_fcb_dma argc=%d\n", argc );
        return 1;
    }
    printf( "PASS exec_low_fcb_dma\n" );
    printf( "PASS default_fcb_control_delimiters\n" );

    av[ 0 ] = SELF;
    av[ 1 ] = "EQS";
    av[ 2 ] = "A\"B";
    av[ 3 ] = "C\\D";
    av[ 4 ] = (char *) 0;
    execv( SELF, av );
    printf( "FAIL execv_quote_backslash returned errno=%d\n", errno );
    return 1;
}

static int stage_eqs( int argc, char **argv )
{
    if ( argc != 4 || argv[ argc ] != (char *) 0 ||
         strcmp( argv[ 2 ], "A\"B" ) != 0 ||
         strcmp( argv[ 3 ], "C\\D" ) != 0 )
    {
        printf( "FAIL execv_quote_backslash argc=%d\n", argc );
        return 1;
    }
    printf( "PASS execv_quote_backslash\n" );

    exec( SELF, " DPAR A\tB Q\"R S\\T" );
    printf( "FAIL direct_space_tab_literals returned errno=%d\n", errno );
    return 1;
}

static int stage_dpar( int argc, char **argv )
{
    if ( argc != 6 || argv[ argc ] != (char *) 0 ||
         strcmp( argv[ 2 ], "A" ) != 0 ||
         strcmp( argv[ 3 ], "B" ) != 0 ||
         strcmp( argv[ 4 ], "Q\"R" ) != 0 ||
         strcmp( argv[ 5 ], "S\\T" ) != 0 )
    {
        printf( "FAIL direct_space_tab_literals argc=%d\n", argc );
        return 1;
    }
    printf( "PASS direct_space_tab_literals\n" );

    fill_chars( long_arg, 121, 'V' );
    av[ 0 ] = SELF;
    av[ 1 ] = "V";
    av[ 2 ] = "X";
    av[ 3 ] = long_arg;
    av[ 4 ] = (char *) 0;
    execv( SELF, av );
    printf( "FAIL execv_exact_126 returned errno=%d\n", errno );
    return 1;
}

static int stage_v126( int argc, char **argv )
{
    int i;

    if ( *(unsigned char *) 0x80 != 126 ||
         *(unsigned char *) 0xff != 0x0d ||
         argc != 4 || argv[ argc ] != (char *) 0 ||
         strcmp( argv[ 2 ], "X" ) != 0 ||
         !all_char( argv[ 3 ], 121, 'V' ) )
    {
        printf( "FAIL execv_exact_126 len=%u argc=%d\n",
                *(unsigned char *) 0x80, argc );
        return 1;
    }
    printf( "PASS execv_exact_126\n" );

    av[ 0 ] = SELF;
    av[ 1 ] = "M";
    for ( i = 2; i <= 63; i++ )
        av[ i ] = "X";
    av[ 64 ] = (char *) 0;
    execv( SELF, av );
    printf( "FAIL execv_many_63 returned errno=%d\n", errno );
    return 1;
}

static int stage_many( int argc, char **argv )
{
    int i;

    if ( *(unsigned char *) 0x80 != 126 ||
         *(unsigned char *) 0xff != 0x0d ||
         argc != 64 || argv[ argc ] != (char *) 0 )
    {
        printf( "FAIL execv_many_63 len=%u argc=%d\n",
                *(unsigned char *) 0x80, argc );
        return 1;
    }
    for ( i = 2; i <= 63; i++ )
        if ( strcmp( argv[ i ], "X" ) != 0 )
        {
            printf( "FAIL execv_many_63 argv[%d]\n", i );
            return 1;
        }
    printf( "PASS execv_many_63\n" );
    printf( "texargs ok\n" );
    return 0;
}

static int direct_cpm( int argc, char **argv )
{
    if ( argc != 4 || argv[ argc ] != (char *) 0 ||
         strcmp( argv[ 0 ], "" ) != 0 ||
         strcmp( argv[ 1 ], "DIRECT" ) != 0 ||
         strcmp( argv[ 2 ], "A\"B" ) != 0 ||
         strcmp( argv[ 3 ], "C\\D" ) != 0 )
    {
        printf( "FAIL direct_cpm_parser argc=%d\n", argc );
        return 1;
    }
    printf( "PASS direct_cpm_parser\n" );
    return 0;
}

int main( int argc, char **argv )
{
    if ( argc < 2 )
        return run_parent();
    if ( argc >= 4 && strcmp( argv[ 3 ], "SFCB" ) == 0 )
        return stage_sfcb( argc, argv );
    if ( strcmp( argv[ 1 ], "STACK" ) == 0 )
        return stage_stack( argc, argv );
    if ( strcmp( argv[ 1 ], "S127" ) == 0 )
        return stage_s127( argc, argv );
    if ( strcmp( argv[ 1 ], "V127" ) == 0 )
        return stage_v127( argc, argv );
    if ( strcmp( argv[ 1 ], "O80" ) == 0 )
        return stage_o80( argc, argv );
    if ( strcmp( argv[ 1 ], "EQS" ) == 0 )
        return stage_eqs( argc, argv );
    if ( strcmp( argv[ 1 ], "DPAR" ) == 0 )
        return stage_dpar( argc, argv );
    if ( strcmp( argv[ 1 ], "V" ) == 0 )
        return stage_v126( argc, argv );
    if ( strcmp( argv[ 1 ], "M" ) == 0 )
        return stage_many( argc, argv );
    if ( strcmp( argv[ 1 ], "DIRECT" ) == 0 )
        return direct_cpm( argc, argv );
    printf( "FAIL unknown stage '%s'\n", argv[ 1 ] );
    return 1;
}
