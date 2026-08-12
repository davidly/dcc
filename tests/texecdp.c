/* texecdp.c - exec()/execv() on a nonexistent file, called from deep enough
 * in the call stack to actually reach the memory a failed exec used to
 * clobber.
 *
 * dcc's start: sets the initial SP to the raw BDOS entry address itself, and
 * the old _exec/_execv built a scratch FCB directly at a fixed high-memory
 * address (BDOS entry - 67) *before* checking whether the target file even
 * exists - so a failed exec (file not found) still cleared/wrote 36 bytes
 * there. Since the caller's live stack always spans from the current SP up
 * to that same original starting point (BDOS entry), any call depth of more
 * than ~32 bytes already reaches into that clobbered range, corrupting
 * live return addresses/locals for a call that's supposed to fail cleanly
 * and just return -1. texec.c's shallow, non-recursive exec calls don't
 * reach deep enough to trigger this; this test recurses first specifically
 * to get there.
 *
 * Each recursion level snapshots a local before recursing and re-checks it
 * after the recursive call returns, so corruption anywhere in the depth
 * range gets caught by whichever level's frame actually landed in the
 * danger window - not just the outermost one.
 */
#include <stdio.h>
#include <stdlib.h>

#define DEPTH 40

static int fails;

static int recurse( int depth, int marker, int use_execv )
{
    int local_check;
    int result;

    if ( 0 == depth )
    {
        int r;
        if ( use_execv )
        {
            char *av[ 2 ];
            av[ 0 ] = "nosuchfi";
            av[ 1 ] = (char *) 0;
            r = execv( "nosuchfi", av );
        }
        else
            r = exec( "nosuchfi", "" );

        if ( -1 != r )
        {
            printf( "FAIL: exec/execv missing file at max depth returned %d\n", r );
            fails++;
            return -999;
        }
        return marker;
    }

    local_check = marker + depth;
    result = recurse( depth - 1, marker, use_execv );

    if ( result != marker )
    {
        printf( "FAIL: stack corruption: depth %d expected marker %d got %d\n", depth, marker, result );
        fails++;
        return -999;
    }
    if ( local_check != marker + depth )
    {
        printf( "FAIL: stack corruption: depth %d local expected %d got %d\n", depth, marker + depth, local_check );
        fails++;
        return -999;
    }

    return result;
}

int main( void )
{
    int r;

    r = recurse( DEPTH, 12345, 0 );
    if ( 12345 == r )
        printf( "PASS deep_exec_missing_file_no_corruption: 1\n" );
    else
        printf( "FAIL deep_exec_missing_file_no_corruption: got %d\n", r );

    r = recurse( DEPTH, 22222, 1 );
    if ( 22222 == r )
        printf( "PASS deep_execv_missing_file_no_corruption: 1\n" );
    else
        printf( "FAIL deep_execv_missing_file_no_corruption: got %d\n", r );

    if ( fails )
        printf( "texecdp FAILED %d\n", fails );
    else
        printf( "texecdp ok\n" );
    return fails ? 1 : 0;
}
