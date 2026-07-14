#include <stdio.h>
#include <errno.h>
#include <fcntl.h>

long int lseek();

void show_error( str ) char * str;
{
    printf( "error: %s, errno: %d\n", str, errno );
    exit( 1 );
}

int elem_counts[] = { 1, 7, 20, 32, 63, 64, 65, 77, 127, 128, 129, 701 };

static short buf[ 1024 ];

#define RW_LOOPS 1024

#define TRW_FILE "trw2.dat"

int main( argc, argv ) int argc; char * argv[];
{
    int e, i, j, result, fd;
    long int seek_offset, file_offset;
    int BUF_BYTES, BUF_ELEMENTS;

    for ( e = 0; e < ( sizeof( elem_counts ) / sizeof( elem_counts[ 0 ] ) ); e++ )
    {
        BUF_ELEMENTS = elem_counts[ e ];
        BUF_BYTES = BUF_ELEMENTS * sizeof( buf[ 0 ] );
        printf( "pass %d with element count %d\n", e, BUF_ELEMENTS );

        fd = open( TRW_FILE, O_CREAT | O_RDWR | O_TRUNC, 0 ); /* S_IRWXU | S_IRWXG | S_IRWXO ); */
        if ( -1 == fd )
            show_error( "unable to create data file" );
    
        for ( i = 0; i < RW_LOOPS; i++ )
        {
            for ( j = 0; j < BUF_ELEMENTS; j++ )
                buf[ j ] = i;
    
            result = write( fd, (char *) buf, BUF_BYTES );
            if ( BUF_BYTES != result )
                show_error( "unable to write to file" );
        }

#if 0
        fdatasync( fd );
        fsync( fd );
#endif
        close( fd );
    
        fd = open( TRW_FILE, O_RDONLY );
        if ( -1 == fd )
            show_error( "unable to open data file read only" );
    
        for ( i = 0; i < RW_LOOPS; i++ )
        {
            memset( buf, 0x69, BUF_BYTES );
            result = read( fd, (char *) buf, BUF_BYTES );
            if ( BUF_BYTES != result )
            {
                printf( "result: %d, i %d\n", result, i );
                show_error( "unable to read from file at point A" );
            }
    
            for ( j = 0; j < BUF_ELEMENTS; j++ )
            {
                if ( buf[ j ] != i )
                {
                    printf( "i %x, j %x, buf[j] %04x\n", i, j, buf[j] );
                    show_error( "data read from file isn't what was expected (should be i) at point A" );
                }
            }
        }
    
        close( fd );
    
        fd = open( TRW_FILE, O_RDWR );
        if ( -1 == fd )
            show_error( "unable to open data file read/write" );
    
        for ( i = 0; i < RW_LOOPS; i++ )
        {
            if ( 0 == ( i % 8 ) )
            {
                seek_offset = (long int) i * BUF_BYTES;
                file_offset = lseek( fd, seek_offset, 0 );
                if ( file_offset != seek_offset )
                {
                    printf( "file_offset %ld, seek_offset %ld\n", file_offset, seek_offset );
                    show_error( "lseek location not as expected" );
                }
    
                for ( j = 0; j < BUF_ELEMENTS; j++ )
                    buf[ j ] = i + 0x4000;
    
                result = write( fd, (char *) buf, BUF_BYTES );
                if ( BUF_BYTES != result )
                    show_error( "unable to write to file after lseek" );
            }
        }
    
        close( fd );
    
        fd = open( TRW_FILE, O_RDONLY );
        if ( -1 == fd )
            show_error( "unable to open data file read only" );
    
        for ( i = RW_LOOPS-1; i >= 0; i-- )
        {
            seek_offset = (long int) i * BUF_BYTES;
            file_offset = lseek( fd, seek_offset, 0 );
            if ( file_offset != seek_offset )
            {
                printf( "file_offset %ld, seek_offset %ld\n", file_offset, seek_offset );
                show_error( "lseek location not as expected" );
            }
    
            result = read( fd, (char *) buf, BUF_BYTES );
            if ( BUF_BYTES != result )
                show_error( "unable to read from file after lseek" );
    
            for ( j = 0; j < BUF_ELEMENTS; j++ )
            {
                if ( 0 == ( i % 8 ) )
                {
                    if ( buf[ j ] != i + 0x4000 )
                        show_error( "data read from file isn't what was expected at point B" );
                }
                else
                {
                    if ( buf[ j ] != i )
                        show_error( "data read from file isn't what was expected at point C" );
                }
            }
        }
    
        close( fd );
    }

    result = unlink( TRW_FILE );
    if ( 0 != result )
        show_error( "can't unlink test file" );

    printf( "trw2 completed with great success\n", argv[ 0 ] );
    return 0;
}

