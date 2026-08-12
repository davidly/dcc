// This app shows how to enable ntvcm instruction tracing at runtime.
// This feature is useful for finding code generation issues in dcc when enabling instruction tracing
// for the whole apps would creation enormous ntvcm.log files.
// To use it, start ntvcm with tracing enabled (-t) but instruction tracing disabled by not specifying (-i). 

#include <stdio.h>
#include <stdlib.h>

#define BDOS_ENABLE_INSTRUCTION_TRACING  185

int main() 
{
    printf("Hello, World!\n");
    long sq = 0;

    // busywork that isn't traced
    for ( long i = 0; i < 1000; i++ )
        sq += i * i;
    printf( "sq: %ld\n", sq );

    bdos( BDOS_ENABLE_INSTRUCTION_TRACING, 1 );
    sq = 0;
    for ( long i = 0; i < 10; i++ )
        sq += i * i;
    printf( "sq: %ld\n", sq );
    bdos( BDOS_ENABLE_INSTRUCTION_TRACING, 0 );

    sq = 0;
    // more busywork that isn't traced
    for ( long i = 0; i < 1000; i++ )
        sq += i * i;
    printf( "sq: %ld\n", sq );

    return 0;
}
