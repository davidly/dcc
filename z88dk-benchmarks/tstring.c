#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "benchmark_rand.h"

char ac[ 4096 ];
char other[ 4096 ];
char zeroes[ 4096 ]; // will be put in bss and guaranteed to be 0 by C89 and later

#define _countof( X ) ( sizeof( X ) / sizeof( X[0] ) )

int main( int argc, char * argv[] )
{
    char alpha[27];
    char *pmem;
    int loops, i, slen;
    int loop_count = ( argc > 1 ) ? atoi( argv[ 1 ] ) : 1;

    for ( loops = 0; loops < loop_count; loops++ )
    {
        srand( 317 );
        for ( i = 0; i < sizeof( ac ); i++ )
            ac[ i ] = ( 'a' + ( i % 26 ) );

        printf( "testing strlen\n" );
        for ( i = 0; i < 1000; i++ )
        {
            int start = ( (unsigned int) rand() % 300 );
            int end = 1 + start + ( (unsigned int) rand() % 3000 );
            int len = end - start;
            char orig = ac[ end ];
            ac[ end ] = 0;
            slen = strlen( ac + start );
            if ( len != slen )
            {
                printf( "strlen failed: iteration %d, len %d, strlen %d, start %d, end %d\n", i, len, slen, start, end );
                exit( 1 );
            }
            ac[ end ] = orig;
        }

        printf( "testing strchr and strrchr\n" );
        for ( i = 0; i < 1000; i++ )
        {
            char * pbang;
            int start = ( (unsigned int) rand() % 300 ); 
            int end = 1 + start + ( (unsigned int) rand() % 70 );
            int len = end - start;
            char orig = ac[ end ];
            ac[ end ] = '!';
            pbang = strchr( ac + start, '!' );
            if ( !pbang )
            {
                printf( "strchr failed to find char: iteration %d, len %d, start %d, end %d\n", i, len, start, end );
                exit( 1 );
            }
            if ( pbang != ( ac + end ) )
            {
                printf( "strchr offset incorrect: iteration %d, len %d, start %d, end %d\n", i, len, start, end );
                exit( 1 );
            }
            pbang = strrchr( ac + start, '!' );
            if ( !pbang )
            {
                printf( "strrchr failed to find char: iteration %d, len %d, start %d, end %d\n", i, len, start, end );
                exit( 1 );
            }
            if ( pbang != ( ac + end ) )
            {
                printf( "strrchr offset incorrect: iteration %d, len %d, start %d, end %d\n", i, len, start, end );
                exit( 1 );
            }
            if ( strchr( ac + start, '$' ) )
            {
                printf( "strrchr somehow found $: iteration %d, len %d, start %d, end %d\n", i, len, start, end );
                exit( 1 );
            }
            ac[ end ] = orig;
        }

        printf( "testing strstr\n" );
        strcpy( alpha, "abcdefghijklmnopqrstuvwxyz" );
        for ( i = 0; i < 1000; i++ )
        {
            const char * pattern, *p;
            int start = ( (unsigned int) rand() % 300 );
            int offset = ( (unsigned int) rand() % 26 );
            int len = 1 + ( (unsigned int) rand() % ( 26 - offset ) );
            if ( ( offset + len ) > 26 )
            {
                printf( "test bug offset %d, len %d\n", offset, len );
                exit( 1 );
            }
            pattern = alpha + offset;
            p = strstr( ac + start, pattern );
            if ( !p )
            {
                printf( "strstr pattern not found iteration %d, start %d, offset %d, len %d, pattern %s\n", i, start, offset, len, pattern );
                exit( 1 );
            }
            if ( memcmp( p, pattern, len ) )
            {
                printf( "strstr found the wrong pattern iteration %d, start %d, offset %d, len %d, pattern %s\n", i, start, offset, len, pattern );
                exit( 1 );
            }
            if ( strstr( ac + start, "gfe" ) )
            {
                printf( "strstr somehow found gfe. iteration %d, start %d, offset %d\n", i, start, offset );
                exit( 1 );
            }
        }

        printf( "testing memcpy and memcmp\n" );
        if ( memchr( ac, 'a', 0 ) )
        {
            printf( "memchr found data with zero length\n" );
            exit( 1 );
        }
        for ( i = 0; i < 1000; i++ )
        {
            int start = ( (unsigned int) rand() % 300 );
            int end = 1 + start + ( (unsigned int) rand() % 3000 );
            int len = end - start;

            memcpy( other + start, ac + start, len );
            if ( memcmp( other + start, ac + start, len ) )
            {
                printf( "memcmp of memcpy'ed memory failed to find match, iteration %d, len %d, start %d, end %d\n", i, len, start, end );
                exit( 1 );
            }
            memset( other + start, 0, len );
            if ( memcmp( other + start, zeroes + start, len ) )
            {
                printf( "zeroes not found in zero-filled memory, iteration %d, len %d, start %d, end %d\n", i, len, start, end );
                exit( 1 );
            }

            other[ start + ( len / 2 ) ] = '!';
            pmem = (char *) memchr( other + start, '!', len );
            if ( pmem != other + start + ( len / 2 ) )
            {
                printf( "memchr found wrong offset, iteration %d, len %d, start %d, end %d\n", i, len, start, end );
                exit( 1 );
            }
            other[ start + len - 1 ] = '?';
            if ( memchr( other + start, '?', len - 1 ) )
            {
                printf( "memchr searched past count, iteration %d, len %d, start %d, end %d\n", i, len, start, end );
                exit( 1 );
            }
            if ( memchr( other + start, '@', len ) )
            {
                printf( "memchr found missing byte, iteration %d, len %d, start %d, end %d\n", i, len, start, end );
                exit( 1 );
            }
        }

        printf( "testing printf\n" );
        for ( i = 0; i < 20; i++ )
        {
            /* Deterministic, platform-independent selection so the displayed
             * lines are reproducible: rand() is implementation-defined and
             * diverges between dcc and a host libc.  The string functions
             * themselves are validated by the self-checking loops above. */
            int start = ( i * 37 ) % 300;
            int len = 1 + ( i * 17 ) % 70;
            int end = start + len;
            char orig = ac[ end ];
            ac[ end ] = 0;

            int l = strlen( ac + start );
            printf( "%2d (%2d): %s\n", len, l, ac + start );
            ac[ end ] = orig;
        }
    }

    printf( "tnarrow completed with great success\n" );
    return 0;
} //main
