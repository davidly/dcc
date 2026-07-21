/* validates the ((uint32_t)a * (uint32_t)b) % m fused-mulmod codegen
 * pattern (__m1mu in DCCRTL.MAC) against the generic 32-bit path across a
 * spread of values, including a >= m (the rare fallback-to-division case),
 * m == 1, both operands == 0, and values near the 16-bit boundary. */

#include <stdio.h>
#include <stdint.h>

uint32_t mulmod( uint16_t a, uint16_t b, uint16_t m )
{
    /* volatile-free but structured so dcc's pattern match applies: both
     * multiplicands cast to uint32_t, modulus a plain uint16_t. */
    return ( (uint32_t) a * (uint32_t) b ) % m;
}

int main( void )
{
    static const uint16_t vals[] = { 0, 1, 2, 3, 5, 7, 8, 9, 15, 16, 17,
        100, 255, 256, 257, 1000, 1023, 1024, 4095, 4096, 30000, 32767,
        32768, 40000, 65534, 65535 };
    int nv = sizeof(vals) / sizeof(vals[0]);
    int i, j, k;
    int mismatches = 0;
    long total = 0;

    for ( i = 0; i < nv; i++ )
    {
        for ( j = 0; j < nv; j++ )
        {
            for ( k = 0; k < nv; k++ )
            {
                uint16_t a = vals[i];
                uint16_t b = vals[j];
                uint16_t m = vals[k];

                if ( 0 == m )
                    continue; /* mod by zero: skip, undefined */

                total++;
                {
                    uint32_t got = mulmod( a, b, m );
                    uint32_t want = ( (uint32_t) a * (uint32_t) b ) % (uint32_t) m;
                    if ( got != want )
                    {
                        mismatches++;
                        if ( mismatches <= 10 )
                            printf( "MISMATCH a=%u b=%u m=%u got=%lu want=%lu\n",
                                    a, b, m, (unsigned long) got, (unsigned long) want );
                    }
                }
            }
        }
    }

    printf( "checked %ld cases, %d mismatches\n", total, mismatches );

    /* a few values spot-checked against hand-computed results */
    printf( "%lu\n", (unsigned long) mulmod( 5, 3, 7 ) );        /* 1 */
    printf( "%lu\n", (unsigned long) mulmod( 40000, 40000, 65521 ) ); /* known-good below */
    printf( "%lu\n", (unsigned long) mulmod( 65535, 65535, 65535 ) ); /* 0 */
    printf( "%lu\n", (unsigned long) mulmod( 0, 12345, 999 ) );  /* 0 */
    printf( "%lu\n", (unsigned long) mulmod( 12345, 0, 999 ) );  /* 0 */
    printf( "%lu\n", (unsigned long) mulmod( 1, 1, 1 ) );        /* 0 */

    printf( "tm1mu ok\n" );
    return 0;
}
