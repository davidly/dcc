/**
 * @file timer.c
 * @brief CP/M client for the example debugger adapter's timer ports.
 *
 * @par Role
 * Demonstrates starting and polling a 16-bit millisecond timer with inp/outp.
 *
 * @par Boundary
 * Requires dcc-debug-host to load the example I/O adapter.
 */

/* --8<-- [start:example] */
#include <stdio.h>
#include <stdlib.h>

#define TIMER_0_HIGH 24
#define TIMER_0_LOW  25

static void start_timer(unsigned delay_ms)
{
    outp(TIMER_0_HIGH, delay_ms >> 8);
    outp(TIMER_0_LOW, delay_ms & 0xff);
}

int main(void)
{
    puts("Waiting for one second...");
    start_timer(1000U);
    while (inp(TIMER_0_LOW) != 0)
        ;
    puts("Timer expired.");
    return 0;
}
/* --8<-- [end:example] */