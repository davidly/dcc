/* tbig.c -- validates random and sequential file i/o at cp/m 2.2's documented
   8mb (65535 record) file size limit. each 128-byte record is stamped with
   its own record number so misdirected records are caught exactly. */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#define RECSIZE    128
#define LASTVALID  65535L    /* highest legal cp/m 2.2 random record number (16-bit r0/r1) */
#define TBIG_FILE  "TBIG.DAT"

long str_to_long( char * s )
{
    long v;
    int neg;

    v = 0L;
    neg = 0;
    if ( '-' == *s )
    {
        neg = 1;
        s++;
    }
    while ( *s >= '0' && *s <= '9' )
    {
        v = v * 10L + (long) ( *s - '0' );
        s++;
    }
    return neg ? -v : v;
}

long get_stamp( char * b )
{
    long v;

    v  = ( (long) b[0] & 0xffL ) << 24;
    v |= ( (long) b[1] & 0xffL ) << 16;
    v |= ( (long) b[2] & 0xffL ) <<  8;
    v |=   (long) b[3] & 0xffL;
    return v;
}

void fill_record( long rec, char * b )
{
    int i;

    b[0] = (char) ( ( rec >> 24 ) & 0xff );
    b[1] = (char) ( ( rec >> 16 ) & 0xff );
    b[2] = (char) ( ( rec >>  8 ) & 0xff );
    b[3] = (char) (   rec         & 0xff );
    for ( i = 4; i < RECSIZE; i++ )
        b[ i ] = (char) ( ( rec + i ) & 0xff );
}

int check_record( long rec, char * b )
{
    int i;

    if ( get_stamp( b ) != rec )
        return 0;
    for ( i = 4; i < RECSIZE; i++ )
        if ( b[ i ] != (char) ( ( rec + i ) & 0xff ) )
            return 0;
    return 1;
}

void show_error( char * str )
{
    printf( "error: %s\n", str );
    exit( 1 );
}

void probe_one( int fd, long rec, char * buf )
{
    long offset, result;

    offset = rec * (long) RECSIZE;
    result = lseek( fd, offset, 0 );
    if ( result != offset )
    {
        printf( "  record %ld (offset %ld): SEEK returned %ld, expected %ld\n", rec, offset, result, offset );
        return;
    }

    result = read( fd, buf, RECSIZE );
    if ( RECSIZE != result )
    {
        printf( "  record %ld (offset %ld): READ FAILED, result %ld\n", rec, offset, result );
        return;
    }

    if ( check_record( rec, buf ) )
        printf( "  record %ld (offset %ld): OK\n", rec, offset );
    else
        printf( "  record %ld (offset %ld): MISMATCH (stamp %ld)\n", rec, offset, get_stamp( buf ) );
}

int main( int argc, char * argv[] )
{
    long lastrec, rec, ok, bad, offset, seek_result;
    int fd, result;
    char buf[ RECSIZE ];

    lastrec = LASTVALID;
    if ( argc > 1 )
        lastrec = str_to_long( argv[ 1 ] );

    printf( "tbig: validating %ld records (%ld bytes)\n", lastrec + 1L, ( lastrec + 1L ) * (long) RECSIZE );

    unlink( TBIG_FILE );
    fd = open( TBIG_FILE, O_CREAT | O_RDWR | O_TRUNC, 0 );
    if ( -1 == fd )
        show_error( "unable to create data file" );

    printf( "writing sequentially" );
    for ( rec = 0; rec <= lastrec; rec++ )
    {
        fill_record( rec, buf );
        result = write( fd, buf, RECSIZE );
        if ( RECSIZE != result )
        {
            printf( "\nwrite failed at record %ld, result %d\n", rec, result );
            show_error( "sequential write failed" );
        }
        if ( 0L == ( rec & 0x1fffL ) )
            printf( "." );
    }
    printf( "\n" );

    close( fd );

    fd = open( TBIG_FILE, O_RDONLY, 0 );
    if ( -1 == fd )
        show_error( "unable to reopen data file read only" );

    printf( "verifying sequentially" );
    ok = 0L;
    bad = 0L;
    for ( rec = 0; rec <= lastrec; rec++ )
    {
        result = read( fd, buf, RECSIZE );
        if ( RECSIZE != result )
        {
            bad++;
            if ( bad <= 10L )
                printf( "\nshort read at record %ld, result %d\n", rec, result );
            else if ( 10L == bad )
                printf( "\n(further failures suppressed)\n" );
            continue;
        }
        if ( check_record( rec, buf ) )
            ok++;
        else
        {
            bad++;
            if ( bad <= 10L )
                printf( "\nMISMATCH at record %ld (stamp read back: %ld)\n", rec, get_stamp( buf ) );
            else if ( 10L == bad )
                printf( "\n(further failures suppressed)\n" );
        }
        if ( 0L == ( rec & 0x1fffL ) )
            printf( "." );
    }
    printf( "\nsequential verify: %ld ok, %ld bad\n", ok, bad );

    close( fd );

    fd = open( TBIG_FILE, O_RDONLY, 0 );
    if ( -1 == fd )
        show_error( "unable to reopen data file for random probes" );

    printf( "\nrandom access boundary probes:\n" );
    probe_one( fd, 0L, buf );
    probe_one( fd, lastrec / 4L, buf );
    probe_one( fd, lastrec / 2L, buf );
    probe_one( fd, lastrec - 2L, buf );
    probe_one( fd, lastrec - 1L, buf );
    probe_one( fd, lastrec, buf );

    close( fd );

    if ( lastrec == LASTVALID )
    {
        printf( "\nprobing one record past the documented 8mb / 65535 cp/m 2.2 limit...\n" );
        fd = open( TBIG_FILE, O_RDWR, 0 );
        if ( -1 == fd )
            printf( "  can't reopen for the past-limit probe\n" );
        else
        {
            offset = ( LASTVALID + 1L ) * (long) RECSIZE;
            seek_result = lseek( fd, offset, 0 );
            if ( seek_result != offset )
                printf( "  lseek to record 65536 returned %ld, not %ld\n", seek_result, offset );
            else
            {
                fill_record( LASTVALID + 1L, buf );
                result = write( fd, buf, RECSIZE );
                if ( RECSIZE == result )
                    printf( "  write past 65535 SUCCEEDED (allowed here; real cp/m 2.2 documents this as out of range)\n" );
                else
                    printf( "  write past 65535 failed (result %d)\n", result );
            }
            close( fd );
        }
    }

    unlink( TBIG_FILE );

    if ( 0L == bad )
        printf( "\ntbig completed with great success\n" );
    else
        printf( "\ntbig FAILED: %ld bad records\n", bad );

    return ( 0L == bad ) ? 0 : 1;
}
