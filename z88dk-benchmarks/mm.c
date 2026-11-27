/* BYTE magazine October 1982. Jerry Pournelle. */
/* ported to C by David Lee */
/* various bugs not found because dimensions are square fixed by David Lee */
/* expected result: 4.65880E+05 */
/* normal version runs in 13 seconds on the original PC and 8.9 seconds with the "f/fast" versions */

#define LINT_ARGS

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define l 20 /* rows in A and resulting matrix C */
#define m 20 /* columns in A and rows in B (must be identical) */
#define n 20 /* columns in B and resulting matrix C */

typedef float ftype;

ftype Summ;
ftype A[ l ] [ m ];
ftype B[ m ] [ n ];
ftype C[ l ] [ n ];

void filla()
{
    register int i, j;
    for ( i = 0; i < l; i++ )
        for ( j = 0; j < m; j++ )
            A[ i ][ j ] = (ftype) ( i + j + 2 );
}

void fillb()
{
    register int i, j;
    for ( i = 0; i < m; i++ )
        for ( j = 0; j < n; j++ )
            B[ i ][ j ] = (ftype) ( ( i + j + 2 ) / ( j + 1 ) );
}

static void fillar()
{
    register int i, j;
    for ( i = 0; i < l; i++ )
        for ( j = 0; j < m; j++ )
            A[ i ][ j ] = ( (ftype) ( i + j + 2 ) ) / 3.0;
}

static void fillbr()
{
    register int i, j;
    for ( i = 0; i < m; i++ )
        for ( j = 0; j < n; j++ )
            B[ i ][ j ] = ( (ftype) ( i + j + 2 ) ) / 7.0;
}

void fillc()
{
    register int i, j;
    for ( i = 0; i < l; i++ )
        for ( j = 0; j < n; j++ )
            C[ i ][ j ] = 0;
}

void ffillc()
{
    ftype * p = (ftype *) C;
    ftype * pend = ( (ftype *) C ) + ( l * n );
    while ( p < pend )
        *p++ = 0;
}

void matmult()
{
    register int i, j, k;
    for ( i = 0; i < l; i++ )
        for ( j = 0; j < n; j++ )
            for ( k = 0; k < m; k++ )
                C[ i ][ j ] += A[ i ][ k ] * B[ k ][ j ];
}

void fmatmult()
{
    uint8_t i, j, k;
    static ftype * pC, * pA, * pAI, * pBJ, *pCI;

    for ( i = 0; i < l; i++ )
    {
        pAI = (ftype *) & ( A[ i ][ 0 ] );
        pCI = (ftype *) & ( C[ i ][ 0 ] );

        for ( j = 0; j < n; j++ )
        {
            pC = (ftype *) & pCI[ j ];
            pA = pAI;
            pBJ = & ( B[ 0 ][ j ] );

            for ( k = 0; k < m; k++ )
            {
                *pC += pA[ k ] * ( *pBJ );
                pBJ += m;
            }
        }
    }
}

void summit()
{
    register int i, j;
    for ( i = 0; i < l; i++ )
        for ( j = 0; j < n; j++ )
            Summ += C[ i ][ j ];
}

void fsummit()
{
    ftype * p = (ftype *) C;
    ftype * pend = ( (ftype *) C ) + ( l * n );

    while ( p < pend )
        Summ += *p++;
}

int main( int argc, char * argv[] )
{
    ftype fastsumm;

    Summ = 0.0;

    filla();
    fillb();
    fillc();

    /* f* versions are faster */
    fmatmult();
    fsummit();

    if ( 465880.0 != Summ )
    {
        printf( "incorrect Summ for fast version\n" );
        exit( 1 );
    }

    printf( "summ is : %f\n", Summ );

    Summ = 0.0;

    filla();
    fillb();
    fillc();

    matmult();
    summit();

    if ( 465880.0 != Summ )
    {
        printf( "incorrect Summ for normal version\n" );
        exit( 1 );
    }

    printf( "summ is : %f\n", Summ );

    /* Second pass: A and B hold realistic, non-whole-number values (plain
     * ratios, not the small-integer values used above) so fp add/multiply
     * exercise real mantissa rounding instead of dcc's small-integer fast
     * paths. There's no independently precomputed expected sum here - dcc's
     * float routines don't round the same way a hardware/IEEE-754 reference
     * would, so the fast (fmatmult/fsummit) and slow (matmult/summit)
     * results are checked against EACH OTHER instead: both call the same
     * RTL, just via different address computations, so they must agree. */
    Summ = 0.0;

    fillar();
    fillbr();
    fillc();

    fmatmult();
    fsummit();

    fastsumm = Summ;

    printf( "summ is : %f\n", Summ );

    Summ = 0.0;

    fillar();
    fillbr();
    fillc();

    matmult();
    summit();

    if ( fastsumm != Summ )
    {
        printf( "incorrect Summ for realistic-value version\n" );
        exit( 1 );
    }

    printf( "summ is : %f\n", Summ );

    return 0;
}



