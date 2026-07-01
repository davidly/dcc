#include <stdio.h>

typedef unsigned char uint8_t;
typedef signed char int8_t;
typedef unsigned short uint16_t;
typedef signed short int16_t;
typedef unsigned long uint32_t;
typedef signed long int32_t;
typedef int bool;
enum { false, true };

#define ab( x ) ( x < 0 ) ? ( -x ) : ( x )

/* Exercises ternary-expression array subscripts in comparison conditions
 * using the ab() macro.  All five comparison operators tested for each of
 * the six standard integer types.  Function names kept <= 6 significant
 * chars (M80 limit) so the linker does not merge distinct functions. */
#define the_test_content \
  for ( i = 0; i < (long)(sizeof( a ) / sizeof( a[0] )); i++ ) \
      a[ i ] = (int8_t)i;                                       \
                                                                \
  for ( i = min; i < max; i++ )                                 \
  {                                                             \
      if ( a[ ab( i + 12 ) ] > a[ ab( i + 13 ) ] )             \
          a[ ab( i + 14 ) ] += 3;                              \
                                                                \
      if ( a[ ab( i + 12 ) ] < a[ ab( i + 13 ) ] )             \
          a[ ab( i + 14 ) ] += 7;                              \
                                                                \
      if ( a[ ab( i + 12 ) ] >= a[ ab( i + 13 ) ] )            \
          a[ ab( i + 14 ) ] += 3;                              \
                                                                \
      if ( a[ ab( i + 12 ) ] <= a[ ab( i + 13 ) ] )            \
          a[ ab( i + 14 ) ] += 7;                              \
                                                                \
      if ( a[ ab( i + 12 ) ] == a[ ab( i + 13 ) ] )            \
          a[ ab( i + 14 ) ] += 9;                              \
  }

int8_t ti8( int8_t min, int8_t max )
{
    int8_t a[ 50 ];
    long i;

    the_test_content;
    return a[ 10 ];
}

uint8_t tui8( uint8_t min, uint8_t max )
{
    uint8_t a[ 50 ];
    long i;

    the_test_content;
    return a[ 10 ];
}

int16_t ti16( int16_t min, int16_t max )
{
    int16_t a[ 50 ];
    long i;

    the_test_content;
    return a[ 10 ];
}

uint16_t tui16( uint16_t min, uint16_t max )
{
    uint16_t a[ 50 ];
    long i;

    the_test_content;
    return a[ 10 ];
}

int32_t ti32( int32_t min, int32_t max )
{
    int32_t a[ 50 ];
    long i;

    the_test_content;
    return a[ 10 ];
}

uint32_t tui32( uint32_t min, uint32_t max )
{
    uint32_t a[ 50 ];
    long i;

    the_test_content;
    return a[ 10 ];
}

void si8( const char *text, int8_t x )
{
    printf( "%s result: %d\n", text, x );
}

void sui8( const char *text, uint8_t x )
{
    printf( "%s result: %u\n", text, x );
}

void si16( const char *text, int16_t x )
{
    printf( "%s result: %d\n", text, x );
}

void sui16( const char *text, uint16_t x )
{
    printf( "%s result: %u\n", text, x );
}

void si32( const char *text, int32_t x )
{
    printf( "%s result: %ld\n", text, x );
}

void sui32( const char *text, uint32_t x )
{
    printf( "%s result: %lu\n", text, x );
}

int main()
{
    int8_t i8min, i8max, i8;
    uint8_t ui8min, ui8max, u8;
    int16_t i16min, i16max, i16;
    uint16_t ui16min, ui16max, u16;
    int32_t i32min, i32max, i32;
    uint32_t ui32min, ui32max, u32;

    i8min = -12;
    i8max = 12;
    i8 = ti8( i8min, i8max );
    si8( "int8_t", i8 );

    ui8min = 0;
    ui8max = 24;
    u8 = tui8( ui8min, ui8max );
    sui8( "uint8_t", u8 );

    i16min = -12;
    i16max = 12;
    i16 = ti16( i16min, i16max );
    si16( "int16_t", i16 );

    ui16min = 0;
    ui16max = 24;
    u16 = tui16( ui16min, ui16max );
    sui16( "uint16_t", u16 );

    i32min = -12;
    i32max = 12;
    i32 = ti32( i32min, i32max );
    si32( "int32_t", i32 );

    ui32min = 0;
    ui32max = 24;
    u32 = tui32( ui32min, ui32max );
    sui32( "uint32_t", u32 );

    printf( "end of the app\n" );
    return 0;
}
