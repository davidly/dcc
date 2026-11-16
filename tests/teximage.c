/* Load a distinct COM image: self-exec tests cannot detect a zero-record load.
 * The child jumps to code in its last record, so the whole image must load.
 * Run both APIs with 3 and 257 records to check both record-count bytes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char record[128];

int main( int argc, char **argv )
{
    FILE *file;
    int i;
    int records;
    unsigned int entry;
    char *av[3];
    static unsigned char child[] = {
        0x11, 0, 0,          /* ld de,message (patched below) */
        0x0e, 9,             /* ld c,9 */
        0xcd, 5, 0,          /* call 5: print string */
        0xc3, 0, 0           /* jp 0: warm boot */
    };
    static char message[] = "PASS distinct exec image\r\n$";

    if ( argc > 1 && strcmp( argv[1], "CHILD" ) == 0 )
    {
        printf( "FAIL exec re-entered parent image\n" );
        return 1;
    }
    records = argc > 2 ? 257 : 3;
    entry = 0x100u + (records - 1) * 128u;
    file = fopen( "XIMAGE.COM", "wb" );
    if ( file == NULL )
    {
        printf( "FAIL create child image\n" );
        return 1;
    }
    for ( i = 0; i < records; i++ )
    {
        memset( record, 0, sizeof(record) );
        if ( i == 0 )
        {
            record[0] = 0xc3; /* jp entry in last record */
            record[1] = entry & 255;
            record[2] = entry >> 8;
        }
        if ( i == records - 1 )
        {
            child[1] = (entry + sizeof(child)) & 255;
            child[2] = (entry + sizeof(child)) >> 8;
            memcpy( record, child, sizeof(child) );
            memcpy( record + sizeof(child), message, sizeof(message) );
        }
        if ( fwrite( record, 1, sizeof(record), file ) != sizeof(record) )
        {
            printf( "FAIL write child image\n" );
            fclose( file );
            return 1;
        }
    }
    if ( fclose( file ) != 0 )
    {
        printf( "FAIL close child image\n" );
        return 1;
    }

    if ( argc > 1 && strcmp( argv[1], "V" ) == 0 )
    {
        av[0] = "XIMAGE";
        av[1] = "CHILD";
        av[2] = (char *) 0;
        execv( "XIMAGE", av );
    }
    else
        exec( "XIMAGE", " CHILD" );
    printf( "FAIL exec returned\n" );
    return 1;
}
