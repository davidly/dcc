#if defined(SDCC) || defined(ZCC)
/* CP/M: reserve stack space and let the CRT initialize the remaining heap. */
#pragma output CLIB_MALLOC_HEAP_SIZE = 0
#pragma output CRT_STACK_SIZE = 4096
#endif

/* tests malloc, calloc, free, and memset */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define allocs 66 /* dcc runs with 73 and fails at 74 */

int logging = 1;

void chkmem( char *p, int v, size_t c )
{
    register unsigned char * pc = (unsigned char *) p;
    unsigned char val = (unsigned char) ( v & 0xff );

    if ( 0 == p )
    {
        printf( "request to chkmem a null pointer\n" );
        exit( 1 );
    }

    for ( size_t i = 0; i < c; i++ )
    {
        if ( *pc != val )
        {
            printf( "memory isn't as expected! p %u, v %d, c %d, *pc %d\n",p, v, c, *pc );
            exit( 1 );
        }
        pc++;
    }
}

int main( int argc, char * argv[] )
{
    size_t i, cb, c_cb;
    char * pc;
    static char * ap[ allocs ];

    logging = ( argc > 1 );
    pc = argv[ 0 ]; /* evade compiler warning */

    for ( size_t j = 0; j < 10; j++ )
    {
        if ( logging )
            printf( "in alloc mode\n" );
    
        for ( i = 0; i < allocs; i++ )
        {
            cb = 8 + ( i * 10 );
            c_cb = cb + 5;
            if ( logging )
                printf( "  i, cb: %d %d\n", i, cb );

            pc = (char *) calloc( c_cb, 1 );
            chkmem( pc, 0, c_cb );
            memset( pc, 0xcc, c_cb );
    
            ap[ i ] = (char *) malloc( cb );
            memset( ap[ i ], 0xaa, cb );
    
            chkmem( pc, 0xcc, c_cb );
            free( pc );
        }
    
        if ( logging )
            printf( "in free mode, even first\n" );
    
        for ( i = 0; i < allocs; i += 2 )
        {
            cb = 8 + ( i * 10 );
            c_cb = cb + 3;
            if ( logging )
                printf( "  i, cb: %d %d\n", i, cb );
    
            pc = (char *) calloc( c_cb, 1 );
            chkmem( pc, 0, c_cb );
            memset( pc, 0xcc, c_cb );
    
            chkmem( ap[ i ], 0xaa, cb );
            memset( ap[ i ], 0xff, cb );
            free( ap[ i ] );
    
            chkmem( pc, 0xcc, c_cb );
            free( pc );
        }
    
        if ( logging )
            printf( "in free mode, now odd\n" );
    
        for ( i = 1; i < allocs; i += 2 )
        {
            cb = 8 + ( i * 10 );
            c_cb = cb + 7;
            if ( logging )
                printf( "  i, cb: %d %d\n", i, cb );
    
            pc = (char *) calloc( c_cb, 1 );
            chkmem( pc, 0, c_cb );
            memset( pc, 0xcc, c_cb );
    
            chkmem( ap[ i ], 0xaa, cb );
            memset( ap[ i ], 0xff, cb );
            free( ap[ i ] );
    
            chkmem( pc, 0xcc, c_cb );
            free( pc );
        }
    }

    printf( "success\n" );
    return 0;
}
