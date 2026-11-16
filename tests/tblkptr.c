/* tblkptr.c - nested-block pointers and locals, long updates
 *
 * A nested block's locals, pointers included, are typed only after their
 * expressions are built. A store or update through such a pointer must use
 * its pointee's type, a returned or updated local its own type, and ++ on a
 * long element must carry into the high word. A long squared in a loop once
 * read its right operand from a slot that was never written. A narrow
 * object updated by a nested-block pointer's long pointee must do the
 * update in long, a store through a nested-block pointer must convert to
 * its pointee, and an updated word local passed to a long parameter keeps
 * its signedness. A char read from a string literal by `0["..."]` once
 * kept the next character in its high byte.
 */

#include <stdio.h>

static int fails;
static long gl;
static unsigned long gu;
static unsigned gw;
static unsigned char gc;
static signed char gs;
static long r[3];

static void check(const char *name, long got, long want)
{
    if (got != want) {
        printf("FAIL %s: got %lx want %lx\n", name, got, want);
        ++fails;
    }
}

/* Stores and updates through a nested-block pointer. */
static unsigned long store_ternary(long c, long x) { gl = x; gu = 5; { unsigned long *p = &gu; *p = c ? gl : gu; return gu; } }
static long add_long(long c, long x) { { long *p = &gl; gl = 5; *p += c ? x : (long)gu; return gl; } }
static long sub_int(int x) { { long *p = &gl; gl = 0x10000L; *p -= x; return gl; } }
static long shift_long(int x) { { long *p = &gl; gl = 0x12345678L; *p >>= x; return gl; } }
static long shift_left(int x) { { long *p = &gl; gl = 0x7fffL; *p <<= x; return gl; } }
static long add_char_value(int x) { { unsigned char *p = &gc; gc = 250; return (long)(*p += x); } }
static long div_unsigned(int x) { gu = 0xfffffffeUL; { unsigned long *p = &gu; *p /= 30035U; return gu; } }
static long mod_char(int x) { { signed char *p = &gs; gs = -100; *p %= x; return gs; } }
static long xor_word(long x) { { unsigned *p = &gw; gw = 65535u; *p ^= x; return gw; } }

/* ++ and -- on long elements, members and pointer targets. */
static long elem_preinc(int i) { r[i] = 0x7ffffffdL; ++r[i]; return r[i]; }
static long elem_postdec(int i) { r[i] = 0x10000L; r[i]--; return r[i]; }
static long block_ptr_inc(int i) { { long *p = &r[i]; *p = 0xffffL; ++*p; (*p)++; return r[i]; } }

/* Returned and updated nested-block locals. */
static unsigned long return_unsigned(unsigned a) { { unsigned k = a; return k; } }
static unsigned long return_int(int a) { { int k = a; return k; } }
static long return_int_dec(int a) { { int k = a; k--; return k; } }
static unsigned long cast_unsigned_dec(unsigned a) { { unsigned k = a; k--; return (unsigned long)k; } }
static unsigned long return_char_inc(unsigned char a) { { unsigned char k = a; ++k; return k; } }
static unsigned long return_char_dec(unsigned char a) { { unsigned char k = a; k--; return k; } }
static long return_schar_inc(signed char a) { { signed char k = a; k++; return k; } }
static unsigned long return_long_inc(unsigned long a) { { unsigned long k = a; ++k; return k; } }

/* A long squared in a loop. */
static long square_loop(long a) { long k = a; int j; for (j = 0; j < 2; ++j) { k++; k *= k; } return k; }
static long square_block(long a) { { long k = a; int j; for (j = 0; j < 2; ++j) { k++; k *= k; } return k; } }

/* Narrow updates by a long read through a nested-block pointer or local. */
static long mod_by_pointee(long a) { gw = 65535u; { long k = a; long *p = &k; gw %= *p; return gw; } }
static long mod_by_global_pointee(long a) { gw = 65535u; gl = a; { long *p = &gl; gw %= *p; return gw; } }
static long div_by_pointee(long a) { gw = 100u; { long k = a; long *p = &k; gw /= *p; return gw; } }
static long mod_by_complement(long a) { gw = 65535u; { long k = a; gw %= ~k; return gw; } }

/* Stores and arguments of nested-block locals. */
static unsigned long store_long_local(long a) { { long k = a; unsigned long *p = &gu; *p = k; return gu; } }
static long pass(long v) { return v; }
static long pass_word_inc(unsigned a) { { unsigned k = a; k++; return pass(k); } }
static long store_pass_word_inc(unsigned a) { { long k1 = 0; unsigned k = a; long *p = &k1; k++; *p = pass(k); return k1; } }

static long char_from_literal(void) { char c; c = 0["hello"]; return c; }

int main(void)
{
    check("store_ternary", (long)store_ternary(20L, 32768L), 0x8000L);
    gu = 1;
    check("add_long", add_long(1L, 0x10000L), 0x10005L);
    check("sub_int", sub_int(-1), 0x10001L);
    check("shift_long", shift_long(4), 0x1234567L);
    check("shift_left", shift_left(4), 0x7fff0L);
    check("add_char_value", add_char_value(10), 4L);
    check("div_unsigned", div_unsigned(0), 0x22e96L);
    check("mod_char", mod_char(7), -2L);
    check("xor_word", xor_word(0x10001L), 0xfffeL);
    check("elem_preinc", elem_preinc(0), 0x7ffffffeL);
    check("elem_postdec", elem_postdec(1), 0xffffL);
    check("block_ptr_inc", block_ptr_inc(2), 0x10001L);
    check("return_unsigned", (long)return_unsigned(0xffffu), 0xffffL);
    check("return_int", (long)return_int(-1), -1L);
    check("return_int_dec", return_int_dec(-1), -2L);
    check("cast_unsigned_dec", (long)cast_unsigned_dec(0xffffu), 0xfffeL);
    check("return_char_inc", (long)return_char_inc(0xff), 0L);
    check("return_char_dec", (long)return_char_dec(0), 0xffL);
    check("return_schar_inc", return_schar_inc(127), -128L);
    check("return_long_inc", (long)return_long_inc(4UL), 5L);
    check("square_loop", square_loop(-37L), 0x19ab21L);
    check("square_block", square_block(-37L), 0x19ab21L);
    check("mod_by_pointee", mod_by_pointee(-28L), 0xfL);
    check("mod_by_global_pointee", mod_by_global_pointee(-28L), 0xfL);
    check("div_by_pointee", div_by_pointee(-3L), 0xffdfL);
    check("mod_by_complement", mod_by_complement(27L), 0xfL);
    check("store_long_local", (long)store_long_local(0x7fffffffL), 0x7fffffffL);
    check("pass_word_inc", pass_word_inc(0xffe9u), 0xffeaL);
    check("store_pass_word_inc", store_pass_word_inc(0xffe9u), 0xffeaL);
    check("char_from_literal", char_from_literal(), 104L);
    if (fails == 0)
        printf("tblkptr passed\n");
    return fails;
}
