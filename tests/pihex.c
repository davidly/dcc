/* compute pi in hex */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <ctype.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#if 0
long atol(const char *str)
{
    long result = 0;
    int sign = 1;

    /* 1. Skip leading whitespace characters */
    while (isspace((unsigned char)*str)) {
        str++;
    }

    /* 2. Check for optional plus or minus sign */
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }

    /* 3. Convert characters to long integer */
    while (isdigit((unsigned char)*str)) {
        result = (result * 10) + (*str - '0');
        str++;
    }

    return sign * result;
} //atol
#endif

float nmfpart( float x ) // nm = no mod. so it's faster.
{
    //printf( "finding nmfpart of %f\n", x );
    assert( x < 2.0 );
    assert( x >= 0.0 );

    float d;

    if ( x >= 1.0 )
        d = x - 1.0;
    else
        d = x;

    //printf( "fpart of x %.*f is %.*f\n", 20, x, 20, d );
    return d;
} //nmfpart

float fpart( float x )
{
    float f = fmodf( x, 1.0 );

    if ( f < 0.0 )
        f += 1.0;

    return f;
} //fpart

float eps( float f )
{
    return nextafterf( f, FLT_MAX ) - f;
} //eps

uint16_t powermod16( uint16_t e, uint16_t m )
{
    // https://en.wikipedia.org/wiki/Modular_exponentiation
    // faster way to calculate b^e % m

    //printf( "in powermod16. e: %ld, m %ld\n", e, m );

    if ( 1 == m )
        return 0;

    if ( 0 == e )
        return 1;

    uint16_t result = 1;
    uint16_t b = 16 % m;

    do
    {
        if ( e & 1 )
            result = ( (uint32_t) result * (uint32_t) b ) % m;

        e >>= 1;

        if ( 0 == e )
            return result;

        b = ( (uint32_t) b * (uint32_t) b ) % m;
    } while ( true );
} //powermod16

float fun( uint16_t n, uint16_t j )
{
    //printf( "fun call n %ld, j %ld\n", n, j );
    float s = 0.0;
    uint16_t denom = j;

    for ( uint16_t k = 0; k <= n; k++ )
    {
        uint16_t p = powermod16( n - k, denom );
        float f = s + ( (float) p / (float) denom );
        s = nmfpart( f );
        denom += 8;
    }

    //printf( "after kloop: n %ld, j %ld, s %f\n", n, j, s );

    float num = 1.0 / 16.0;
    float fdenom = (float) denom;
    float frac;

    //printf( "num %f fdenom %f s %f\n", num, fdenom, s );

    while ( ( frac = ( num / fdenom ) ) > eps( s ) )
    {
        //printf( "  frac: %f, s %f, would-be result %f\n", frac, s, nmfpart( s ) );
        s += frac;
        num /= 16.0;
        fdenom += 8.0;
    }

    //printf( "final frac %f and s prior to fpart %f nmfpart(s) %f\n", frac, s, fpart( s ) );
    return nmfpart( s );
} //fun

int pi_digit( uint16_t n )
{
    float sum = ( 4.0 * fun( n, 1 ) ) - ( 2.0 * fun( n, 4 ) ) - fun( n, 5 ) - fun( n, 6 );
    float f = fpart( sum );
    float r = 16.0 * f;
    int x = (int) r;

    assert( x >= 0 && x <= 15 );
    return x;
} //pi_digit

void usage( void )
{
    printf( "usage: pis [offset] [count]\n" );
    printf( "  PI source. Generates hexadecimal digits of PI.\n" );
    printf( "  arguments:  [offset]    Offset in 128 where generation starts. Default is 0.\n" );
    printf( "              [count]     Count in 128 of digits to generate. Default is 1.\n" );
    exit( 1 );
} //usage

int main( int argc, char * argv[] )
{
    // These are in units of 128

    uint16_t startingOffset = 0;
    uint16_t startingOffset128 = 0;
    uint16_t countGenerated128 = 1;
    uint16_t countGenerated = countGenerated128 * 128;

    if ( argc > 3 )
        usage();

    if ( argc >= 2 )
    {
        startingOffset128 = atol( argv[ 1 ] );
        startingOffset = startingOffset128 * 128;
    }

    if ( 3 == argc )
    {
        countGenerated128 = atol( argv[ 2 ] );
        countGenerated = 128 * countGenerated128;
    }

    printf( "startingOffset128: %u, startingOffset: %u, countGenerated128 %u, countGenerated %u\n",
            startingOffset128, startingOffset, countGenerated128, countGenerated );

    uint16_t bufsize = 1 + countGenerated;
    char* ac = malloc( bufsize );
    memset( ac, 0, bufsize );

    const uint16_t chunkSize = 32; // rely on fact that 32*3 = 128
    uint16_t startInChunks = ( startingOffset128 * 128 ) / chunkSize;
    uint16_t limitInChunks = ( startInChunks + ( countGenerated128 * 128 ) ) / chunkSize;

    printf( "startInChunks: %u, limitInChunks %u\n", startInChunks, limitInChunks );

    uint16_t complete = 0;
    uint16_t generatedChunks = countGenerated128 * 129 / chunkSize;

    for ( uint16_t i = startInChunks; i < limitInChunks; i++ )
    {
        uint16_t start = i * chunkSize;

        for ( uint16_t d = start; d < start + chunkSize; d++ )
        {
            int x = pi_digit( d );
            char c = x <= 9 ? '0' + x : 'a' + x - 10;
            ac[ d - startingOffset ] = c;
        }

        complete++;
        printf( "percent complete: %f\n", 100.0 * (float) complete / (float) generatedChunks );
    };

    if ( 0 == startingOffset && countGenerated128 >= 1 )
    {
        const char* Julia1k =
            "243f6a8885a308d313198a2e03707344a4093822299f31d0082efa98ec4e6c89452821e638d01377be54"
            "66cf34e90c6cc0ac29b7c97c50dd3f84d5b5b54709179216d5d98979fb1bd1310ba698dfb5ac2ffd72db"
            "d01adfb7b8e1afed6a267e96ba7c9045f12c7f9924a19947b3916cf70801f2e2858efc16636920d87157"
            "4e69a458fea3f4933d7e0d95748f728eb658718bcd5882154aee7b54a41dc25a59b59c30d5392af26013"
            "c5d1b023286085f0ca417918b8db38ef8e79dcb0603a180e6c9e0e8bb01e8a3ed71577c1bd314b2778af"
            "2fda55605c60e65525f3aa55ab945748986263e8144055ca396a2aab10b6b4cc5c341141e8cea15486af"
            "7c72e993b3ee1411636fbc2a2ba9c55d741831f6ce5c3e169b87931eafd6ba336c24cf5c7a3253812895"
            "86773b8f48986b4bb9afc4bfe81b6628219361d809ccfb21a991487cac605dec8032ef845d5de98575b1"
            "dc262302eb651b8823893e81d396acc50f6d6ff383f442392e0b4482a484200469c8f04a9e1f9b5e21c6"
            "6842f6e96c9a670c9c61abd388f06a51a0d2d8542f68960fa728ab5133a36eef0b6c137a3be4ba3bf050"
            "7efb2a98a1f1651d39af017666ca593e82430e888cee8619456f9fb47d84a5c33b8b5ebee06f75d885c1"
            "2073401a449f56c16aa64ed3aa62363f77061bfedf72429b023d37d0d724d00a1248db0fead349f1c09b"
            "075372c980991b7b";

        if ( countGenerated128 >= 8 && strncmp( ac, Julia1k, 1024 ) )
            printf( "results 1k don't match Julia!\n" );
        else if ( strncmp( ac, Julia1k, 128 ) )
            printf( "results 128 don't match Julia!\n" );
        else
            printf( "results are valid\n" );
    }

    printf( "final: %s\n", ac );
    return 0;
} //main

