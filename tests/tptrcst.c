/* Regression test: casting a multi-dimensional array to a flat pointer of
 * its base element type, then doing pointer arithmetic, must scale by the
 * cast's target type - not by the original array's row size. A 2D array
 * decaying to a pointer signals a "row stride" for arithmetic on ITS OWN
 * (row-pointer) type; a cast to a flat element pointer must not inherit
 * that stale stride. */

#include <stdio.h>
#include <stdlib.h>

typedef float ftype;

ftype fC[20][20];
int iC[20][20];
ftype fD[400];

int main()
{
    ftype * fp = (ftype *) fC;
    ftype * fpend = ( (ftype *) fC ) + ( 20 * 20 );
    int fdiff = (int) fpend - (int) fp;

    int * ip = (int *) iC;
    int * ipend = ( (int *) iC ) + ( 20 * 20 );
    int idiff = (int) ipend - (int) ip;

    ftype * dp = (ftype *) fD;
    ftype * dpend = ( (ftype *) fD ) + 400;
    int ddiff = (int) dpend - (int) dp;

    if ( 1600 != fdiff )
    {
        printf( "FAIL: 2D float array cast diff is %d, expected 1600\n", fdiff );
        exit( 1 );
    }

    if ( 800 != idiff )
    {
        printf( "FAIL: 2D int array cast diff is %d, expected 800\n", idiff );
        exit( 1 );
    }

    if ( 1600 != ddiff )
    {
        printf( "FAIL: 1D float array cast diff is %d, expected 1600\n", ddiff );
        exit( 1 );
    }

    printf( "all pointer-cast diffs correct\n" );
    return 0;
}
