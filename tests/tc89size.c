/* tc89size.c - sizeof expression tests for dcc */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

struct SzOne {
    char c;
    int i;
    long l;
};

static int fails;
static int ga[5];
static long gl[3];
static struct SzOne gs[2];
static char gstr[] = "abcd";
static _Bool gb[5];
static bool gba[5];
static signed char gsc[5];
static unsigned char guc[5];
static short gsh[5];
static unsigned short gush[5];
static unsigned int gui[5];
static unsigned long gul[5];
static float gf[5];
static int8_t gi8[5];
static uint8_t gu8[5];
static int16_t gi16[5];
static uint16_t gu16[5];
static int32_t gi32[5];
static uint32_t gu32[5];
static int_least8_t gli8[5];
static uint_least8_t glu8[5];
static int_least16_t gli16[5];
static uint_least16_t glu16[5];
static int_least32_t gli32[5];
static uint_least32_t glu32[5];
static int_fast8_t gfi8[5];
static uint_fast8_t gfu8[5];
static int_fast16_t gfi16[5];
static uint_fast16_t gfu16[5];
static int_fast32_t gfi32[5];
static uint_fast32_t gfu32[5];
static intmax_t gimax[5];
static uintmax_t gumax[5];

static void chki(const char *name, int got, int expect)
{
    if (got != expect) {
        printf("FAIL %s got %d expected %d\n", name, got, expect);
        fails = fails + 1;
    }
}

static void chkl(const char *name, long got, long expect)
{
    if (got != expect) {
        printf("FAIL %s\n", name);
        fails = fails + 1;
    }
}

static void chkul(const char *name, unsigned long got, unsigned long expect)
{
    if (got != expect) {
        printf("FAIL %s\n", name);
        fails = fails + 1;
    }
}

/* Arrays declared in a NESTED scope (a bare block, an if/for body, or several
 * levels deep) enter the local symbol table only when their declaration is
 * emitted, not when the enclosing block's AST is built.  sizeof of such an
 * array must therefore be resolved at emit time.  Regression for a bug where
 * nested-scope sizeof was computed at AST-build time - before the symbol was
 * in scope - and silently collapsed to sizeof(int) (so the element-count
 * idiom returned 1 instead of the true length).  Expectations use element
 * counts / sizeof(int) so they are int-width independent. */
static int nb_block_count(void)
{
    {
        int a[10];
        return (int)(sizeof a / sizeof a[0]);   /* 10 */
    }
}

static int nb_block_bytes(void)
{
    {
        int a[10];
        return (int)sizeof a;                   /* 10 * sizeof(int) */
    }
}

static int nb_if_count(int n)
{
    if (n > 0) {
        int a[7];
        return (int)(sizeof a / sizeof a[0]);   /* 7 */
    }
    return -1;
}

static int nb_loop_count(void)
{
    int i;
    int last = 0;
    for (i = 0; i < 3; i++) {
        int a[6];
        last = (int)(sizeof a / sizeof a[0]);   /* 6 */
    }
    return last;
}

static int nb_deep_count(void)
{
    {
        {
            {
                int a[4];
                return (int)(sizeof a / sizeof a[0]);   /* 4 */
            }
        }
    }
}

static int nb_2d_rows(void)
{
    {
        int a[5][3];
        return (int)(sizeof a / sizeof a[0]);   /* 5 rows */
    }
}

static int nb_2d_row_bytes(void)
{
    {
        int a[5][3];
        return (int)sizeof a[0];                /* 3 * sizeof(int) */
    }
}

static int nb_char_bytes(void)
{
    {
        char a[9];
        return (int)sizeof a;                   /* 9 */
    }
}

static int nb_between_locals(void)
{
    {
        int guard = 1;
        int a[8];
        int tail = 2;
        int r = (int)(sizeof a / sizeof a[0]);  /* 8 */
        return r + guard - tail;                /* 8 + 1 - 2 = 7 */
    }
}

static int nb_shadow_inner(void)
{
    int a[3];
    {
        int a[6];
        return (int)(sizeof a / sizeof a[0]);   /* inner array */
    }
}

static int nb_shadow_outer_after(void)
{
    int a[5];
    {
        int a[2];
        (void)a;
    }
    return (int)(sizeof a / sizeof a[0]);       /* outer array */
}

static int nb_c99_type_bytes(void)
{
    {
        _Bool b[3];
        bool ba[3];
        signed char sc[3];
        unsigned char uc[3];
        short sh[3];
        unsigned short ush[3];
        unsigned int ui[3];
        unsigned long ul[3];
        float f[3];
        int8_t i8[3];
        uint8_t u8[3];
        int16_t i16[3];
        uint16_t u16[3];
        int32_t i32[3];
        uint32_t u32[3];
         int_least8_t li8[3];
         uint_least8_t lu8[3];
         int_least16_t li16[3];
         uint_least16_t lu16[3];
         int_least32_t li32[3];
         uint_least32_t lu32[3];
         int_fast8_t fi8[3];
         uint_fast8_t fu8[3];
         int_fast16_t fi16[3];
         uint_fast16_t fu16[3];
         int_fast32_t fi32[3];
         uint_fast32_t fu32[3];
         intmax_t imax[3];
         uintmax_t umax[3];
        return (int)sizeof b + (int)sizeof ba +
               (int)sizeof sc + (int)sizeof uc +
               (int)sizeof sh + (int)sizeof ush +
               (int)sizeof ui + (int)sizeof ul +
               (int)sizeof f +
               (int)sizeof i8 + (int)sizeof u8 +
               (int)sizeof i16 + (int)sizeof u16 +
             (int)sizeof i32 + (int)sizeof u32 +
             (int)sizeof li8 + (int)sizeof lu8 +
             (int)sizeof li16 + (int)sizeof lu16 +
             (int)sizeof li32 + (int)sizeof lu32 +
             (int)sizeof fi8 + (int)sizeof fu8 +
             (int)sizeof fi16 + (int)sizeof fu16 +
             (int)sizeof fi32 + (int)sizeof fu32 +
             (int)sizeof imax + (int)sizeof umax;
    }
}

static int nb_c99_type_counts(void)
{
    {
        _Bool b[3];
        bool ba[3];
        signed char sc[3];
        unsigned char uc[3];
        short sh[3];
        unsigned short ush[3];
        unsigned int ui[3];
        unsigned long ul[3];
        float f[3];
        int8_t i8[3];
        uint8_t u8[3];
        int16_t i16[3];
        uint16_t u16[3];
        int32_t i32[3];
        uint32_t u32[3];
         int_least8_t li8[3];
         uint_least8_t lu8[3];
         int_least16_t li16[3];
         uint_least16_t lu16[3];
         int_least32_t li32[3];
         uint_least32_t lu32[3];
         int_fast8_t fi8[3];
         uint_fast8_t fu8[3];
         int_fast16_t fi16[3];
         uint_fast16_t fu16[3];
         int_fast32_t fi32[3];
         uint_fast32_t fu32[3];
         intmax_t imax[3];
         uintmax_t umax[3];
        return (int)(sizeof b / sizeof b[0]) +
               (int)(sizeof ba / sizeof ba[0]) +
               (int)(sizeof sc / sizeof sc[0]) +
               (int)(sizeof uc / sizeof uc[0]) +
               (int)(sizeof sh / sizeof sh[0]) +
               (int)(sizeof ush / sizeof ush[0]) +
               (int)(sizeof ui / sizeof ui[0]) +
               (int)(sizeof ul / sizeof ul[0]) +
               (int)(sizeof f / sizeof f[0]) +
               (int)(sizeof i8 / sizeof i8[0]) +
               (int)(sizeof u8 / sizeof u8[0]) +
               (int)(sizeof i16 / sizeof i16[0]) +
               (int)(sizeof u16 / sizeof u16[0]) +
             (int)(sizeof i32 / sizeof i32[0]) +
             (int)(sizeof u32 / sizeof u32[0]) +
             (int)(sizeof li8 / sizeof li8[0]) +
             (int)(sizeof lu8 / sizeof lu8[0]) +
             (int)(sizeof li16 / sizeof li16[0]) +
             (int)(sizeof lu16 / sizeof lu16[0]) +
             (int)(sizeof li32 / sizeof li32[0]) +
             (int)(sizeof lu32 / sizeof lu32[0]) +
             (int)(sizeof fi8 / sizeof fi8[0]) +
             (int)(sizeof fu8 / sizeof fu8[0]) +
             (int)(sizeof fi16 / sizeof fi16[0]) +
             (int)(sizeof fu16 / sizeof fu16[0]) +
             (int)(sizeof fi32 / sizeof fi32[0]) +
             (int)(sizeof fu32 / sizeof fu32[0]) +
             (int)(sizeof imax / sizeof imax[0]) +
             (int)(sizeof umax / sizeof umax[0]);
    }
}

int main(void)
{
    struct SzOne s;
    int *ip;
    long *lp;
    struct SzOne *sp;
    char *cp;

    ip = ga;
    lp = gl;
    sp = gs;
    cp = gstr;
    fails = 0;

    chki("sizeof_char", sizeof(char), 1);
    chki("sizeof_int", sizeof(int), 2);
    chki("sizeof_long", sizeof(long), 4);
    chki("sizeof_ptr", sizeof(ip), 2);

    chki("sizeof_array", sizeof ga, 10);
    chki("sizeof_array_elem", sizeof ga[0], 2);
    chki("sizeof_star_ip", sizeof *ip, 2);
    chki("sizeof_ip_index", sizeof ip[0], 2);

    chki("sizeof_long_array", sizeof gl, 12);
    chki("sizeof_long_elem", sizeof gl[0], 4);
    chki("sizeof_star_lp", sizeof *lp, 4);
    chki("sizeof_lp_index", sizeof lp[0], 4);

    chki("sizeof_struct", sizeof s, 7);
    chki("sizeof_struct_array", sizeof gs, 14);
    chki("sizeof_struct_elem", sizeof gs[0], 7);
    chki("sizeof_sp_index", sizeof sp[0], 7);
    chki("sizeof_field_i", sizeof s.i, 2);
    chki("sizeof_field_l", sizeof s.l, 4);
    chki("sizeof_arrow_l", sizeof sp->l, 4);

    chki("sizeof_string_lit", sizeof "abc", 4);
    chki("sizeof_char_array", sizeof gstr, 5);
    chki("sizeof_char_elem", sizeof cp[0], 1);

    chki("sizeof_bool_array", sizeof gb, 5);
    chki("sizeof_bool_alias_array", sizeof gba, 5);
    chki("sizeof_schar_array", sizeof gsc, 5);
    chki("sizeof_uchar_array", sizeof guc, 5);
    chki("sizeof_short_array", sizeof gsh, 10);
    chki("sizeof_ushort_array", sizeof gush, 10);
    chki("sizeof_uint_array", sizeof gui, 10);
    chki("sizeof_ulong_array", sizeof gul, 20);
    chki("sizeof_float_array", sizeof gf, 20);
    chki("sizeof_int8_array", sizeof gi8, 5);
    chki("sizeof_uint8_array", sizeof gu8, 5);
    chki("sizeof_int16_array", sizeof gi16, 10);
    chki("sizeof_uint16_array", sizeof gu16, 10);
    chki("sizeof_int32_array", sizeof gi32, 20);
    chki("sizeof_uint32_array", sizeof gu32, 20);
    chki("sizeof_int_least8_array", sizeof gli8, 5);
    chki("sizeof_uint_least8_array", sizeof glu8, 5);
    chki("sizeof_int_least16_array", sizeof gli16, 10);
    chki("sizeof_uint_least16_array", sizeof glu16, 10);
    chki("sizeof_int_least32_array", sizeof gli32, 20);
    chki("sizeof_uint_least32_array", sizeof glu32, 20);
    chki("sizeof_int_fast8_array", sizeof gfi8, 5);
    chki("sizeof_uint_fast8_array", sizeof gfu8, 5);
    chki("sizeof_int_fast16_array", sizeof gfi16, 10);
    chki("sizeof_uint_fast16_array", sizeof gfu16, 10);
    chki("sizeof_int_fast32_array", sizeof gfi32, 20);
    chki("sizeof_uint_fast32_array", sizeof gfu32, 20);
    chki("sizeof_intmax_array", sizeof gimax, 20);
    chki("sizeof_uintmax_array", sizeof gumax, 20);
    chki("sizeof_bool_elem", sizeof gb[0], 1);
    chki("sizeof_bool_alias_elem", sizeof gba[0], 1);
    chki("sizeof_float_elem", sizeof gf[0], 4);

    chki("sizeof_expr_long", sizeof(ga[0] + 1L), 4);
    chki("sizeof_expr_uint", sizeof(ga[0] + 1U), 2);
    chki("sizeof_compare", sizeof(ga[0] < 1L), 2);

    /* sizeof of arrays declared in nested scopes (resolved at emit time). */
    chki("nb_block_count", nb_block_count(), 10);
    chki("nb_block_bytes", nb_block_bytes(), 10 * (int)sizeof(int));
    chki("nb_if_count", nb_if_count(1), 7);
    chki("nb_loop_count", nb_loop_count(), 6);
    chki("nb_deep_count", nb_deep_count(), 4);
    chki("nb_2d_rows", nb_2d_rows(), 5);
    chki("nb_2d_row_bytes", nb_2d_row_bytes(), 3 * (int)sizeof(int));
    chki("nb_char_bytes", nb_char_bytes(), 9);
    chki("nb_between_locals", nb_between_locals(), 7);
    chki("nb_shadow_inner", nb_shadow_inner(), 6);
    chki("nb_shadow_outer_after", nb_shadow_outer_after(), 5);
    chki("nb_c99_type_bytes", nb_c99_type_bytes(), 204);
    chki("nb_c99_type_counts", nb_c99_type_counts(), 87);

    chkl("INT8_MIN", INT8_MIN, -128L);
    chkl("INT8_MAX", INT8_MAX, 127L);
    chkul("UINT8_MAX", UINT8_MAX, 255UL);
    chkl("INT16_MIN", INT16_MIN, -32767L - 1L);
    chkl("INT16_MAX", INT16_MAX, 32767L);
    chkul("UINT16_MAX", UINT16_MAX, 65535UL);
    chkl("INT32_MIN", INT32_MIN, -2147483647L - 1L);
    chkl("INT32_MAX", INT32_MAX, 2147483647L);
    chkul("UINT32_MAX", UINT32_MAX, 4294967295UL);
    chkl("INT_LEAST8_MIN", INT_LEAST8_MIN, -128L);
    chkl("INT_LEAST8_MAX", INT_LEAST8_MAX, 127L);
    chkul("UINT_LEAST8_MAX", UINT_LEAST8_MAX, 255UL);
    chkl("INT_LEAST16_MIN", INT_LEAST16_MIN, -32767L - 1L);
    chkl("INT_LEAST16_MAX", INT_LEAST16_MAX, 32767L);
    chkul("UINT_LEAST16_MAX", UINT_LEAST16_MAX, 65535UL);
    chkl("INT_LEAST32_MIN", INT_LEAST32_MIN, -2147483647L - 1L);
    chkl("INT_LEAST32_MAX", INT_LEAST32_MAX, 2147483647L);
    chkul("UINT_LEAST32_MAX", UINT_LEAST32_MAX, 4294967295UL);
    chkl("INT_FAST8_MIN", INT_FAST8_MIN, -128L);
    chkl("INT_FAST8_MAX", INT_FAST8_MAX, 127L);
    chkul("UINT_FAST8_MAX", UINT_FAST8_MAX, 255UL);
    chkl("INT_FAST16_MIN", INT_FAST16_MIN, -32767L - 1L);
    chkl("INT_FAST16_MAX", INT_FAST16_MAX, 32767L);
    chkul("UINT_FAST16_MAX", UINT_FAST16_MAX, 65535UL);
    chkl("INT_FAST32_MIN", INT_FAST32_MIN, -2147483647L - 1L);
    chkl("INT_FAST32_MAX", INT_FAST32_MAX, 2147483647L);
    chkul("UINT_FAST32_MAX", UINT_FAST32_MAX, 4294967295UL);
    chkl("INTPTR_MIN", INTPTR_MIN, -32767L - 1L);
    chkl("INTPTR_MAX", INTPTR_MAX, 32767L);
    chkul("UINTPTR_MAX", UINTPTR_MAX, 65535UL);
    chkl("INTMAX_MIN", INTMAX_MIN, -2147483647L - 1L);
    chkl("INTMAX_MAX", INTMAX_MAX, 2147483647L);
    chkul("UINTMAX_MAX", UINTMAX_MAX, 4294967295UL);
    chkl("PTRDIFF_MIN", PTRDIFF_MIN, -32767L - 1L);
    chkl("PTRDIFF_MAX", PTRDIFF_MAX, 32767L);
    chkul("SIZE_MAX", SIZE_MAX, 65535UL);
    chkul("WCHAR_MIN", WCHAR_MIN, 0UL);
    chkul("WCHAR_MAX", WCHAR_MAX, 65535UL);

    /* UINT8_MAX promotes to int per C99 7.18.2, so it is signed: a signed
     * comparison against a negative value must stay signed (would flip to 0 if
     * it were wrongly unsigned, because -1 would convert to a large value). */
    chki("UINT8_MAX_signedcmp", -1 < UINT8_MAX ? 1 : 0, 1);
    /* UINT16_MAX is genuinely unsigned int, so -1 converts up and the same
     * comparison is false. */
    chki("UINT16_MAX_unsignedcmp", -1 < UINT16_MAX ? 1 : 0, 0);

    /* 7.18.4 integer-constant macros: correct value and underlying width. */
    chkl("INT8_C_value", INT8_C(120), 120L);
    chkl("INT16_C_value", INT16_C(30000), 30000L);
    chkl("INT32_C_value", INT32_C(2000000000), 2000000000L);
    chkul("UINT8_C_value", UINT8_C(250), 250UL);
    chkul("UINT16_C_value", UINT16_C(60000), 60000UL);
    chkul("UINT32_C_value", UINT32_C(4000000000), 4000000000UL);
    chkl("INTMAX_C_value", INTMAX_C(2000000000), 2000000000L);
    chkul("UINTMAX_C_value", UINTMAX_C(4000000000), 4000000000UL);
    chki("INT8_C_width", (int)sizeof(INT8_C(1)), (int)sizeof(int));
    chki("INT16_C_width", (int)sizeof(INT16_C(1)), (int)sizeof(int));
    chki("INT32_C_width", (int)sizeof(INT32_C(1)), 4);
    chki("UINT16_C_width", (int)sizeof(UINT16_C(1)), (int)sizeof(unsigned int));
    chki("UINT32_C_width", (int)sizeof(UINT32_C(1)), 4);
    chki("INTMAX_C_width", (int)sizeof(INTMAX_C(1)), 4);
    chki("UINTMAX_C_width", (int)sizeof(UINTMAX_C(1)), 4);

    if (fails) {
        printf("tc89size failed: %d\n", fails);
        return 1;
    }

    printf("tc89size completed with great success\n");
    return 0;
}
