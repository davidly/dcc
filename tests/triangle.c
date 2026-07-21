/* validates recursion works */

#include <stdint.h>
#include <stdio.h>

int32_t triangle( int32_t n )
{
    if ( 0 == n )
        return 0;

    return n + triangle( n - 1 );
} //triangle

int main()
{
    int32_t n = 50;
    printf( "triangle( %ld ) = %ld\n", n, triangle( n ) );
    return 0;
} //main
