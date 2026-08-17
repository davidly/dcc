/* texec.c - self-contained exec/execv test; execs itself with a marker arg */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHILD_ARG "XCHILD"

static int run_as_child( int argc, char **argv )
{
    int i;
    int len;
    char *tail;
    char buf[ 128 ];

    /* verify command tail at 0x80 */
    len = * (unsigned char *) 0x80;
    tail = (char *) 0x81;
    if ( len > 126 ) len = 126;
    for ( i = 0; i < len; i++ )
        buf[ i ] = tail[ i ];
    buf[ i ] = 0;

    printf( "child: tail='%s'\n", buf );
    printf( "child: argc=%d\n", argc );
    for ( i = 0; i < argc; i++ )
        printf( "child: argv[%d]='%s'\n", i, argv[ i ] );

    /* argc must be at least 2 and argv[1] must be CHILD_ARG */
    if ( argc < 2 )
    {
        printf( "FAIL: child argc %d < 2\n", argc );
        return 1;
    }
    if ( strcmp( argv[ 1 ], CHILD_ARG ) != 0 )
    {
        printf( "FAIL: argv[1]='%s' expected '%s'\n", argv[ 1 ], CHILD_ARG );
        return 1;
    }

    printf( "child: pass\n" );
    return 0;
}

int main( int argc, char **argv )
{
    int r;
    char *av[ 3 ];

    /* child mode: this process was exec'd by the parent run */
    if ( argc >= 2 && strcmp( argv[ 1 ], CHILD_ARG ) == 0 )
        return run_as_child( argc, argv );

    /* parent mode */
    printf( "parent: exec missing file\n" );
    errno = 0;
    r = exec( "nosuchfi", "" );
    if ( r != -1 || errno != ENOENT )
    {
        printf( "FAIL: exec missing returned %d errno %d\n", r, errno );
        return 1;
    }

    printf( "parent: execv missing file\n" );
    av[ 0 ] = "nosuchfi";
    av[ 1 ] = (char *) 0;
    errno = 0;
    r = execv( "nosuchfi", av );
    if ( r != -1 || errno != ENOENT )
    {
        printf( "FAIL: execv missing returned %d errno %d\n", r, errno );
        return 1;
    }

    /* exec self as child; does not return on success */
    printf( "parent: exec self as child\n" );
    exec( "texec", " " CHILD_ARG );

    printf( "FAIL: exec self returned\n" );
    return 1;
}
