/* tlogwide.c - && and || results widened to long or float
 *
 * The result of && and || is an int 0 or 1. Converted to long (a return,
 * cast, assignment, store or argument) or to float, it must be widened; the
 * high word used to be whatever the registers held, often a long operand's.
 */

#include <stdio.h>

static long gl;
static long sb;

static long ret_or(int a, int b) { return a || b; }
static long ret_and_long(long a, long b) { return a && b; }
static long cast_and_ptr(unsigned long *pa, long b) { sb = b; return (long)((*pa) && sb); }
static long cast_or(long a, long b) { return (long)(a || b); }
static long assign_and(long a, long b) { long x; x = a && b; return x; }
static long store_or(short a, short b) { gl = a || b; return gl; }
static long take(long v) { return v; }
static long arg_and(int a, long b) { return take(a && b); }
static unsigned long ret_or_unsigned(unsigned a, unsigned b) { return a || b; }
static float float_or(long a, long b) { return (float)(a || b); }

static int fails;

static void check(const char *name, long got, long want)
{
    if (got != want) {
        printf("FAIL %s: got %lx want %lx\n", name, got, want);
        ++fails;
    }
}

int main(void)
{
    unsigned long v = 199;

    check("ret_or", ret_or(0, 7), 1L);
    check("ret_or_0", ret_or(0, 0), 0L);
    check("ret_and_long", ret_and_long(5L, 0x7fff0000L), 1L);
    check("cast_and_ptr", cast_and_ptr(&v, 2147483647L), 1L);
    check("cast_or", cast_or(0L, 0x12340000L), 1L);
    check("assign_and", assign_and(0x10000L, 0x7fff0000L), 1L);
    check("assign_and_0", assign_and(0x10000L, 0L), 0L);
    check("store_or", store_or(-1, 0), 1L);
    check("arg_and", arg_and(3, 0x7fff0000L), 1L);
    check("ret_or_unsigned", (long)ret_or_unsigned(0xffffU, 0U), 1L);
    check("float_or", (long)float_or(0x10000L, 0L), 1L);
    if (fails == 0)
        printf("tlogwide passed\n");
    return fails;
}
