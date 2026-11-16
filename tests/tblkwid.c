/* A nested-block int updated with an unsigned operand, then used in a long
 * operation, must be sign-extended.  Its update converts the unsigned result
 * to int at the same width, and the forwarded value was widened as the
 * unsigned result it converts: t | 255UL gave 0xffff, not 0xffffffff.  A
 * decremented short was built untyped and widened as unsigned the same way. */
#include <stdio.h>

int fails;
unsigned long g;
long gl;

static void check(unsigned long got, unsigned long want, int line)
{
    if (got != want) {
        printf("line %d: got %lx, expected %lx\n", line, got, want);
        ++fails;
    }
}

void multiply(void) { { int t = 255; t *= 256u; g = t | 255UL; } }
void multiply_short(void) { { int t = 255; t *= (unsigned short)256; g = t | 255UL; } }
void add_long(void) { { int t = 255; t *= (unsigned short)256; gl = t + 0L; } }
void assign(void) { { int t = 255; t = t * (unsigned short)256; g = t | 255UL; } }
void add(void) { { int t = 255; t += (unsigned short)65280; g = t | 255UL; } }
void as_short(void) { { short t = 255; t *= (unsigned short)256; g = t | 255UL; } }
void constant(void) { { int t = -256; g = t | 255UL; } }
void decrement(void) { { short t = 0; --t; g = t + (4294967249UL < t); } }

int main()
{
    multiply();
    check(g, 0xffffffffUL, __LINE__);
    multiply_short();
    check(g, 0xffffffffUL, __LINE__);
    add_long();
    check((unsigned long)gl, (unsigned long)-256L, __LINE__);
    assign();
    check(g, 0xffffffffUL, __LINE__);
    add();
    check(g, 0xffffffffUL, __LINE__);
    as_short();
    check(g, 0xffffffffUL, __LINE__);
    constant();
    check(g, 0xffffffffUL, __LINE__);
    decrement();
    check(g, 0, __LINE__);
    if (fails == 0)
        printf("tblkwid passed\n");
    return fails;
}
