/* tselfadj.c - an in-place adjust of a just-stored word
 *
 * `g = a; g += 7;` adjusts g in memory. The load between the two reads the
 * value the store left in HL, and once pushed it as a stack operand that the
 * in-place adjust never popped, so the function returned through a garbage
 * frame pointer.
 */

#include <stdio.h>

static int fails;
static short sg;
static long lg;

static void check(const char *name, long got, long want)
{
    if (got != want) {
        printf("FAIL %s: got %lx want %lx\n", name, got, want);
        ++fails;
    }
}

static signed char add_static(short a) { static short sl; sl = a; sl += 7; return (signed char)sl; }
static signed char add_block(short a) { static short sl; { sl = a; sl += 7; return (signed char)sl; } }
static char sub_global(short a) { sg = a; sg -= 7; return sg; }
static char inc_global(short a) { sg = a; ++sg; return sg; }
static char dec_global(short a) { sg = a; --sg; return sg; }
static char or_global(short a) { sg = a; sg |= 7; return sg; }
static char add_global_plus(short a) { sg = a; sg += 7; return sg + 1; }
static char inc_long(long a) { lg = a; ++lg; return lg; }

int main(void)
{
    check("add_static", add_static(-472), 47L);
    check("add_static2", add_static(116), 123L);
    check("add_block", add_block(-472), 47L);
    check("sub_global", sub_global(0x107), 0L);
    check("inc_global", inc_global(0x17f), (char)0x80);
    check("dec_global", dec_global(0x200), (char)0xff);
    check("or_global", or_global(0x130), 0x37L);
    check("add_global_plus", add_global_plus(0x110), 0x18L);
    check("inc_long", inc_long(0x1ffffL), 0L);
    if (fails == 0)
        printf("tselfadj passed\n");
    return fails;
}
