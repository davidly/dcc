/* validates argc and argv work */

#include <stdio.h>
#include <string.h>

static int eval_order[ 8 ];
static int eval_count;

static int mark_arg( int value )
{
    eval_order[ eval_count++ ] = value;
    return value;
}

static void check_conditional_args(
    int a, int b, int c, int d, int e, int f, int g, int h )
{
    printf( "conditional args: %d %d %d %d %d %d %d %d\n",
            a, b, c, d, e, f, g, h );
}

int main( int argc, char * argv[] )
{
    char first[ 4 ];
    char second[ 4 ];

    printf( "argc: %d\n", argc );

    for ( int i = 0; i < argc; i++ )
        printf( "argv[ %d ]: '%s'\n", i, argv[ i ] );

    check_conditional_args(
        argc == 6 ? mark_arg( 1 ) : mark_arg( -1 ),
        argc != 6 ? mark_arg( 2 ) : mark_arg( -2 ),
        argc == 6 ? mark_arg( 3 ) : mark_arg( -3 ),
        argc != 6 ? mark_arg( 4 ) : mark_arg( -4 ),
        argc == 6 ? mark_arg( 5 ) : mark_arg( -5 ),
        argc != 6 ? mark_arg( 6 ) : mark_arg( -6 ),
        argc == 6 ? mark_arg( 7 ) : mark_arg( -7 ),
        argc != 6 ? mark_arg( 8 ) : mark_arg( -8 ) );
    printf( "conditional evaluation order:" );
    for ( int i = 0; i < eval_count; i++ )
        printf( " %d", eval_order[ i ] );
    printf( "\n" );

    first[ 3 ] = 0;
    second[ 2 ] = 0;
    memset(
        argc == 6 ? first : second,
        argc == 6 ? 'x' : 'y',
        argc == 6 ? 3 : 2 );
    printf( "conditional fastcall: %s\n", argc == 6 ? first : second );

    printf( "targs completed with great success\n" );
    return 0;
} // main
