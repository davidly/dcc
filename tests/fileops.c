/*  tests various buffered I/O functions in C */

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>

char acbuf[ 128 ];
char bigacbuf[ 512 ];

long cpm_filelen( FILE * fp )
{
    long size = 0;
    int read = 0;
    long offset = 0;
    long x;

    fseek( fp, offset, SEEK_SET );
    while ( !feof( fp ) )
    {
        read = fread( acbuf, 1, 128, fp );
        if ( 0 == read )
            break;
        for ( x = 0; x < 128; x++ )
            if ( acbuf[ x ] == 0x1a )
                break;
            else
                size++;
    }

    fseek( fp, offset, SEEK_SET );
    return size;
}

long portable_filelen( FILE * fp )
{
    int result;
    long len;
    long offset;
    long current = ftell( fp );
    printf( "current offset: %ld\n", current );
    offset = 0;
    result = fseek( fp, offset, SEEK_END );
    printf( "result of fseek: %d\n", result );
    len = ftell( fp );
    printf( "file length from ftell: %ld\n", len );
    fseek( fp, current, SEEK_SET );
    return len;
} 

void read_and_validate( long offset, int chunkLen, FILE * fp )
{
    int result;

    result = fread( bigacbuf, 1, chunkLen, fp );
    printf( "result of read at offset %ld: %d\n", offset, result );
    if ( 0 == result )
        printf( "  errno (0 if at eof): %d\n", errno );
    else
    {
        if ( 512 == offset )
        {
            if ( bigacbuf[ 0 ] != 'k' )
                printf( "data at fixed offset 512 isn't a k\n" );
            if ( bigacbuf[ 127 ] != 'k' )
                printf( "data at fixed offset 512 + 127 isn't a k\n" );
            if ( bigacbuf[ 128 ] != 0 )
                printf( "data at fixed offset 512 + 128 isn't a 0\n" );
        }
        else if ( 8192 == offset )
        {
            if ( bigacbuf[ 0 ] != 'j' )
                printf( "data at fixed offset 8192 isn't a j\n" );
            if ( bigacbuf[ 127 ] != 0x1a )
                printf( "didn't find a ^z at the end of the file\n" );
        }
        else
        {
            if ( 0 != bigacbuf[ 0 ] )
                printf( "data at offset %d isn't a 0\n", offset );
            if ( 0 != bigacbuf[ 511 ] )
                printf( "data at offset %d isn't a 0\n", offset + 511 );
        }
    }
} /*read_and_validate*/

int main( int argc, char *argv[] )
{
    char * pcFile = "fileops.dat";
    FILE * fp;
    long len, offset;
    int chunkLen;
    int result;

    unlink( pcFile );

    fp = fopen( pcFile, "w+" );
    printf( "fp: %d\n", fp );
    if ( 0 == fp )
    {
        printf( "unable to open file, errno %d\n", errno );
        exit( 1 );
    }

    len = cpm_filelen( fp );
    printf( "empty file length: %ld\n", len );

    /* explicitly zero-fill bytes 0..8191 rather than relying on a seek
       past end-of-file to leave a zero-filled gap: C89 makes no promise
       about the contents of such a gap, and POSIX's zero-fill-on-hole
       guarantee doesn't apply here since this isn't a POSIX filesystem */
    memset( bigacbuf, 0, sizeof( bigacbuf ) );
    for ( offset = 0; offset < 8192; offset += sizeof( bigacbuf ) )
    {
        result = fwrite( bigacbuf, sizeof( bigacbuf ), 1, fp );
        if ( 1 != result )
        {
            printf( "zero-fill fwrite failed at offset %ld\n", offset );
            exit( 1 );
        }
    }

    offset = 8192;
    result = fseek( fp, offset, SEEK_SET );
    printf( "result of fseek: %d\n", result );
    if ( -1 == result )
        printf( "errno after fseek: %d\n", errno );

    memset( acbuf, 'j', sizeof( acbuf ) );
    acbuf[ sizeof( acbuf ) - 1 ] = 0x1a; /* ^z end of file */

    result = fwrite( acbuf, sizeof( acbuf ), 1, fp );
    printf( "result of fwrite (should be 1): %d\n", result );
    if ( 1 != result )
        printf( "errno after fwrite: %d\n", errno );

    len = cpm_filelen( fp );
    printf( "8192 + 128 = 8320 file length from cpm_filelen minus one for ^z: %ld\n", len );

    len = portable_filelen( fp );
    printf( "8192 + 128 = 8320 file length from portable_filelen: %ld\n", len );

    offset = 512;
    result = fseek( fp, offset, SEEK_SET );
    printf( "result of fseek to middle of file: %d\n", result );
    if ( -1 == result )
        printf( "errno after fseek to middle of file: %d\n", errno );

    memset( acbuf, 'k', sizeof( acbuf ) );
    result = fwrite( acbuf, sizeof( acbuf), 1, fp );
    printf( "result of fwrite to middle of file (should be 1): %d\n", result );
    if ( 1 != result )
        printf( "errno after fwrite middle of file: %d\n", errno );

    fflush( fp );
    fclose( fp );

    fp = fopen( pcFile, "r+" );
    printf( "fp: %d\n", fp );
    if ( 0 == fp )
    {
        printf( "unable to open file a second time, errno %d\n", errno );
        exit( 1 );
    }

    len = cpm_filelen( fp );
    printf( "8192 + 128 = 8320 file length from cpm_filelen minus one for ^z: %ld\n", len );

    memset( bigacbuf, 'd', sizeof( bigacbuf ) );
    chunkLen = 512;
    for ( offset = 0; offset < 8320; offset += chunkLen )
    {
        read_and_validate( offset, chunkLen, fp );
    }

    /* now read in blocks from the end of the file to the start using fseek() */

    printf( "testing backwards read\n" );

    memset( bigacbuf, 'e', sizeof( bigacbuf ) );
    for ( offset = 8192; offset >= 0; offset -= chunkLen )
    {
        result = fseek( fp, offset, SEEK_SET );
        read_and_validate( offset, chunkLen, fp );
    }

    fclose( fp );
    result = unlink( pcFile );
    if ( 0 != result )
    {
        printf( "unlink failed with error %d\n", errno );
        exit( 1 );
    }

    printf( "fileops completed with great success\n" );
    return 0;
} /*main*/
