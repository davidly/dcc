/* tpostptr_stdint.c - regression test for post/pre inc/dec through pointer lvalues.
 *
 * This version uses <stdint.h> fixed-width types instead of assuming int size.
 *
 * Focus examples:
 *     uint8_t bump(uint8_t *p) { return (*p)++; }
 *     int16_t f(void) { int16_t x = 1; return (x)++; }
 *     uint8_t f(uint8_t *p) { return (p[0])++; }
 *     uint8_t f(struct S *p) { return (p->uc)++; }
 *     uint8_t f(uint8_t *p) { return (*p)--; }
 *
 * Expected output:
 * tpostptr_stdint start
 * PASS
 */

#include <stdio.h>
#include <stdint.h>

struct S {
    uint8_t uc;
    int8_t sc;
    int16_t si;
    uint16_t ui;
    int32_t sl;
    uint32_t ul;
    uint8_t uca[4];
    int16_t ia[4];
    int32_t la[4];
};

static int fails;

static void check_i32(name, got, exp)
char *name;
int32_t got;
int32_t exp;
{
    if (got != exp) {
        printf("FAIL %s got %ld expected %ld\n",
               name, (long)got, (long)exp);
        fails++;
    }
}

static void check_u32(name, got, exp)
char *name;
uint32_t got;
uint32_t exp;
{
    if (got != exp) {
        printf("FAIL %s got %lu expected %lu\n",
               name, (unsigned long)got, (unsigned long)exp);
        fails++;
    }
}

static uint8_t bump_u8(p)
uint8_t *p;
{
    return (*p)++;
}

static uint8_t pre_bump_u8(p)
uint8_t *p;
{
    return ++(*p);
}

static uint8_t drop_u8(p)
uint8_t *p;
{
    return (*p)--;
}

static uint8_t pre_drop_u8(p)
uint8_t *p;
{
    return --(*p);
}

static int8_t bump_i8(p)
int8_t *p;
{
    return (*p)++;
}

static int8_t drop_i8(p)
int8_t *p;
{
    return (*p)--;
}

static int16_t bump_i16(p)
int16_t *p;
{
    return (*p)++;
}

static int16_t pre_bump_i16(p)
int16_t *p;
{
    return ++(*p);
}

static int16_t drop_i16(p)
int16_t *p;
{
    return (*p)--;
}

static int16_t pre_drop_i16(p)
int16_t *p;
{
    return --(*p);
}

static uint16_t bump_u16(p)
uint16_t *p;
{
    return (*p)++;
}

static uint16_t drop_u16(p)
uint16_t *p;
{
    return (*p)--;
}

static int32_t bump_i32(p)
int32_t *p;
{
    return (*p)++;
}

static int32_t pre_bump_i32(p)
int32_t *p;
{
    return ++(*p);
}

static int32_t drop_i32(p)
int32_t *p;
{
    return (*p)--;
}

static int32_t pre_drop_i32(p)
int32_t *p;
{
    return --(*p);
}

static uint32_t bump_u32(p)
uint32_t *p;
{
    return (*p)++;
}

static uint32_t drop_u32(p)
uint32_t *p;
{
    return (*p)--;
}

static uint8_t bump_u8_index0(p)
uint8_t *p;
{
    return (p[0])++;
}

static uint8_t drop_u8_deref_paren(p)
uint8_t *p;
{
    return (*p)--;
}

static int16_t bump_local_paren()
{
    int16_t x;

    x = 1;
    return (x)++;
}

static uint8_t bump_struct_u8_paren(p)
struct S *p;
{
    return (p->uc)++;
}

static uint8_t bump_struct_array_u8_paren(p, i)
struct S *p;
int i;
{
    return (p->uca[i])++;
}

static uint8_t bump_u8_expr(base, i)
uint8_t *base;
int i;
{
    return (*(base + i))++;
}

static int16_t bump_i16_expr(base, i)
int16_t *base;
int i;
{
    return (*(base + i))++;
}

static int32_t bump_i32_expr(base, i)
int32_t *base;
int i;
{
    return (*(base + i))++;
}

static void test_u8()
{
    uint8_t x;
    uint8_t r;

    x = (uint8_t)41;
    r = bump_u8(&x);
    check_u32("u8_post_ret", (uint32_t)r, 41UL);
    check_u32("u8_post_store", (uint32_t)x, 42UL);

    r = pre_bump_u8(&x);
    check_u32("u8_pre_ret", (uint32_t)r, 43UL);
    check_u32("u8_pre_store", (uint32_t)x, 43UL);

    r = drop_u8(&x);
    check_u32("u8_drop_ret", (uint32_t)r, 43UL);
    check_u32("u8_drop_store", (uint32_t)x, 42UL);

    r = pre_drop_u8(&x);
    check_u32("u8_pre_drop_ret", (uint32_t)r, 41UL);
    check_u32("u8_pre_drop_store", (uint32_t)x, 41UL);

    x = 255;
    r = bump_u8(&x);
    check_u32("u8_wrap_ret", (uint32_t)r, 255UL);
    check_u32("u8_wrap_store", (uint32_t)x, 0UL);

    r = drop_u8(&x);
    check_u32("u8_under_ret", (uint32_t)r, 0UL);
    check_u32("u8_under_store", (uint32_t)x, 255UL);
}

static void test_i8()
{
    int8_t x;
    int8_t r;

    x = (int8_t)10;
    r = bump_i8(&x);
    check_i32("i8_post_ret", (int32_t)r, 10L);
    check_i32("i8_post_store", (int32_t)x, 11L);

    r = drop_i8(&x);
    check_i32("i8_drop_ret", (int32_t)r, 11L);
    check_i32("i8_drop_store", (int32_t)x, 10L);

    x = (int8_t)-5;
    r = bump_i8(&x);
    check_i32("i8_neg_post_ret", (int32_t)r, -5L);
    check_i32("i8_neg_post_store", (int32_t)x, -4L);
}

static void test_16()
{
    int16_t si;
    uint16_t ui;
    int16_t ri;
    uint16_t ru;

    si = (int16_t)-10;
    ri = bump_i16(&si);
    check_i32("i16_post_ret", (int32_t)ri, -10L);
    check_i32("i16_post_store", (int32_t)si, -9L);

    ri = pre_bump_i16(&si);
    check_i32("i16_pre_ret", (int32_t)ri, -8L);
    check_i32("i16_pre_store", (int32_t)si, -8L);

    ri = drop_i16(&si);
    check_i32("i16_drop_ret", (int32_t)ri, -8L);
    check_i32("i16_drop_store", (int32_t)si, -9L);

    ri = pre_drop_i16(&si);
    check_i32("i16_pre_drop_ret", (int32_t)ri, -10L);
    check_i32("i16_pre_drop_store", (int32_t)si, -10L);

    ui = (uint16_t)60000UL;
    ru = bump_u16(&ui);
    check_u32("u16_post_ret", (uint32_t)ru, 60000UL);
    check_u32("u16_post_store", (uint32_t)ui, 60001UL);

    ru = drop_u16(&ui);
    check_u32("u16_drop_ret", (uint32_t)ru, 60001UL);
    check_u32("u16_drop_store", (uint32_t)ui, 60000UL);
}

static void test_32()
{
    int32_t sl;
    uint32_t ul;
    int32_t rl;
    uint32_t rul;

    sl = -123456L;
    rl = bump_i32(&sl);
    check_i32("i32_post_ret", rl, -123456L);
    check_i32("i32_post_store", sl, -123455L);

    rl = pre_bump_i32(&sl);
    check_i32("i32_pre_ret", rl, -123454L);
    check_i32("i32_pre_store", sl, -123454L);

    rl = drop_i32(&sl);
    check_i32("i32_drop_ret", rl, -123454L);
    check_i32("i32_drop_store", sl, -123455L);

    rl = pre_drop_i32(&sl);
    check_i32("i32_pre_drop_ret", rl, -123456L);
    check_i32("i32_pre_drop_store", sl, -123456L);

    ul = 123456789UL;
    rul = bump_u32(&ul);
    check_u32("u32_post_ret", rul, 123456789UL);
    check_u32("u32_post_store", ul, 123456790UL);

    rul = drop_u32(&ul);
    check_u32("u32_drop_ret", rul, 123456790UL);
    check_u32("u32_drop_store", ul, 123456789UL);
}

static void test_arrays_and_structs()
{
    uint8_t uc[4];
    int16_t ia[4];
    int32_t la[4];
    struct S s;
    struct S *sp;
    uint8_t ruc;
    int16_t ri;
    int32_t rl;

    uc[0] = (uint8_t)1;
    uc[1] = (uint8_t)20;
    uc[2] = (uint8_t)30;
    uc[3] = (uint8_t)40;

    ia[0] = (int16_t)100;
    ia[1] = (int16_t)200;
    ia[2] = (int16_t)300;
    ia[3] = (int16_t)400;

    la[0] = 1000L;
    la[1] = 2000L;
    la[2] = 3000L;
    la[3] = 4000L;

    ruc = bump_u8_expr(uc, 2);
    check_u32("u8_arr_ret", (uint32_t)ruc, 30UL);
    check_u32("u8_arr_store", (uint32_t)uc[2], 31UL);

    ri = bump_i16_expr(ia, 1);
    check_i32("i16_arr_ret", (int32_t)ri, 200L);
    check_i32("i16_arr_store", (int32_t)ia[1], 201L);

    rl = bump_i32_expr(la, 3);
    check_i32("i32_arr_ret", rl, 4000L);
    check_i32("i32_arr_store", la[3], 4001L);

    sp = &s;
    sp->uc = (uint8_t)77;
    sp->sc = (int8_t)-3;
    sp->si = (int16_t)1234;
    sp->ui = (uint16_t)65000UL;
    sp->sl = -99999L;
    sp->ul = 888888UL;

    ruc = bump_u8(&sp->uc);
    check_u32("s_u8_ret", (uint32_t)ruc, 77UL);
    check_u32("s_u8_store", (uint32_t)sp->uc, 78UL);

    check_i32("s_i8_ret", (int32_t)bump_i8(&sp->sc), -3L);
    check_i32("s_i8_store", (int32_t)sp->sc, -2L);

    ri = bump_i16(&sp->si);
    check_i32("s_i16_ret", (int32_t)ri, 1234L);
    check_i32("s_i16_store", (int32_t)sp->si, 1235L);

    check_u32("s_u16_ret", (uint32_t)bump_u16(&sp->ui), 65000UL);
    check_u32("s_u16_store", (uint32_t)sp->ui, 65001UL);

    rl = bump_i32(&sp->sl);
    check_i32("s_i32_ret", rl, -99999L);
    check_i32("s_i32_store", sp->sl, -99998L);

    check_u32("s_u32_ret", bump_u32(&sp->ul), 888888UL);
    check_u32("s_u32_store", sp->ul, 888889UL);

    sp->uca[0] = (uint8_t)9;
    sp->uca[1] = (uint8_t)19;
    sp->uca[2] = (uint8_t)29;
    sp->uca[3] = (uint8_t)39;
    sp->ia[0] = (int16_t)11;
    sp->ia[1] = (int16_t)22;
    sp->ia[2] = (int16_t)33;
    sp->ia[3] = (int16_t)44;
    sp->la[0] = 111L;
    sp->la[1] = 222L;
    sp->la[2] = 333L;
    sp->la[3] = 444L;

    check_u32("s_uca_ret", (uint32_t)bump_u8_expr(sp->uca, 3), 39UL);
    check_u32("s_uca_store", (uint32_t)sp->uca[3], 40UL);
    check_i32("s_ia_ret", (int32_t)bump_i16_expr(sp->ia, 2), 33L);
    check_i32("s_ia_store", (int32_t)sp->ia[2], 34L);
    check_i32("s_la_ret", bump_i32_expr(sp->la, 1), 222L);
    check_i32("s_la_store", sp->la[1], 223L);
}

static void test_requested_forms()
{
    uint8_t uc[2];
    struct S s;
    int16_t ri;
    uint8_t ruc;

    ri = bump_local_paren();
    check_i32("paren_local_ret", (int32_t)ri, 1L);

    uc[0] = (uint8_t)44;
    uc[1] = (uint8_t)55;
    ruc = bump_u8_index0(uc);
    check_u32("paren_index_ret", (uint32_t)ruc, 44UL);
    check_u32("paren_index_store", (uint32_t)uc[0], 45UL);
    check_u32("paren_index_other", (uint32_t)uc[1], 55UL);

    s.uc = (uint8_t)66;
    ruc = bump_struct_u8_paren(&s);
    check_u32("paren_struct_ret", (uint32_t)ruc, 66UL);
    check_u32("paren_struct_store", (uint32_t)s.uc, 67UL);

    s.uca[0] = (uint8_t)10;
    s.uca[1] = (uint8_t)20;
    ruc = bump_struct_array_u8_paren(&s, 1);
    check_u32("paren_struct_array_ret", (uint32_t)ruc, 20UL);
    check_u32("paren_struct_array_store", (uint32_t)s.uca[1], 21UL);

    uc[0] = (uint8_t)7;
    ruc = drop_u8_deref_paren(uc);
    check_u32("paren_deref_drop_ret", (uint32_t)ruc, 7UL);
    check_u32("paren_deref_drop_store", (uint32_t)uc[0], 6UL);
}

static void test_expression_contexts()
{
    uint8_t uc;
    int16_t si;
    int32_t sl;
    int32_t sum;
    int32_t lsum;

    uc = (uint8_t)5;
    sum = 100L + (int32_t)bump_u8(&uc);
    check_i32("expr_u8_sum", sum, 105L);
    check_u32("expr_u8_store", (uint32_t)uc, 6UL);

    si = (int16_t)10;
    sum = (int32_t)bump_i16(&si) + (int32_t)bump_i16(&si);
    check_i32("expr_i16_sum", sum, 21L);
    check_i32("expr_i16_store", (int32_t)si, 12L);

    sl = 1000L;
    lsum = bump_i32(&sl) + bump_i32(&sl);
    check_i32("expr_i32_sum", lsum, 2001L);
    check_i32("expr_i32_store", sl, 1002L);

    si = (int16_t)1;
    if (bump_i16(&si) == 1 && si == 2) {
        check_i32("expr_if_store", (int32_t)si, 2L);
    } else {
        printf("FAIL expr_if\n");
        fails++;
    }

    uc = (uint8_t)3;
    while (bump_u8(&uc) < 6) {
        ;
    }
    check_u32("expr_while_store", (uint32_t)uc, 7UL);
}

int main()
{
    printf("tpostptr_stdint start\n");

    test_u8();
    test_i8();
    test_16();
    test_32();
    test_arrays_and_structs();
    test_expression_contexts();
    test_requested_forms();

    if (fails) {
        printf("FAILED %d\n", fails);
        return 1;
    }

    printf("PASS\n");
    return 0;
}
