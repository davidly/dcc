/*
 * tstackov.c - exercises the lightweight stack-overflow guard (dcc
 * -fstack-check).  #pragma stack_check(on) below turns the guard on for the
 * rest of this file, regardless of how the harness/build script that compiles
 * it is otherwise configured (e.g. runall.ps1 -Report, which builds every app
 * without the guard by default) - a source-level, self-contained request
 * instead of a build-script convention every tool has to know about.
 *
 * Each recursion level allocates a local frame, so unbounded recursion walks
 * the C stack down into the heap region.  With the guard enabled the runtime
 * prints "?stack overflow" from the function prologue and exits to CP/M
 * (return code 0FFh) instead of silently corrupting memory.  Without the guard
 * the same recursion would scribble over the heap and crash unpredictably.
 */
#include <stdio.h>

#pragma stack_check(on)

/* volatile-ish sink so the optimizer cannot discard the frame or the call. */
int g_sink;

int descend(int depth)
{
    int local[8];
    int i;

    for (i = 0; i < 8; ++i)
        local[i] = depth + i;

    g_sink = local[depth & 7];

    /* Unbounded recursion: returns only after the guard aborts the program. */
    return descend(depth + 1) + local[0];
}

int main(void)
{
    printf("tstackov start\n");
    g_sink = descend(1);
    /* Unreachable: the stack guard exits before we return from descend(). */
    printf("tstackov should not reach here %d\n", g_sink);
    return 0;
}
