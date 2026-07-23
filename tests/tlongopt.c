#include <stdio.h>
#include <stdint.h>

static int fails;

struct LongBox { int32_t l; int32_t a[2]; };

static int32_t glive;
static int32_t glarr[2];

static void chk(long got, long want, char *name)
{
    if (got != want) {
        printf("FAIL %s got=%ld want=%ld\n", name, got, want);
        fails++;
    }
}

static void chku(unsigned long got, unsigned long want, char *name)
{
    if (got != want) {
        printf("FAIL %s got=%lu want=%lu\n", name, got, want);
        fails++;
    }
}

static int s_gt32767(long a)   { return a > 32767L; }
static int s_ltm32768(long a)  { return a < -32768L; }
static int s_lt0(long a)       { return a < 0L; }
static int s_le100(long a)     { return a <= 100L; }
static int s_gtm5(long a)      { return a > -5L; }
static int u_gtbig(unsigned long a) { return a > 3000000000UL; }
static int u_lebig(unsigned long a) { return a <= 3000000000UL; }
static int br_lt40000(int x) { if (x < 40000) return 1; return 0; }
static int br_ge40000(int x) { if (x >= 40000) return 1; return 0; }
static int br_gt40000(int x) { if (x > 40000) return 1; return 0; }
static int br_le40000(int x) { if (x <= 40000) return 1; return 0; }
static int br_cgt_x(int x) { if (40000 > x) return 1; return 0; }
static int br_cle_x(int x) { if (40000 <= x) return 1; return 0; }
static int br_lt32768(int x) { if (x < 32768) return 1; return 0; }
static int br_lt65535(int x) { if (x < 65535) return 1; return 0; }
static int br_lt_hex_u(int x) { if (x < 0xffff) return 1; return 0; }
static int br_ult_hex_u(unsigned int x) { if (x < 0xffff) return 1; return 0; }
static int clamp_int_min(long a) { if (a < -32768L) return -32768; return (int)a; }
static int32_t id32(int32_t value) { return value; }
static int32_t ret_global_live_add(void) { return glive += 5L; }
static int32_t ret_deref_live_add(int32_t *p) { return *p += 7L; }
static int32_t ret_member_live_shr(struct LongBox *p) { return p->l >>= 2; }

/* Compound long expressions that peek_simple_unary_type cannot predict: the
 * first term is 16-bit but the whole RHS is long.  The operator must still
 * compute in 32 bits and must preserve operand order for - / %. */
static long co_add(int x, int y, long la) { return x + (y + la); }
static long co_sub(int x, int y, long la) { return x - (y + la); }
static long co_mul(int x, long la)        { return x * (la + 0L); }
static long co_div(long num, int y, long la) { return num / (y + la); }
static long co_mod(long num, int y, long la) { return num % (y + la); }
static long co_and(int x, int y, long la) { return x & (y + la); }
static long co_or(int x, int y, long la)  { return x | (y + la); }
static long co_xor(int x, int y, long la) { return x ^ (y + la); }
/* value-context comparison with compound long RHS (peek sees only 'y') */
static int  cc_lt(int x, int y, long la)  { return x < y + la; }
static int  cc_gt(int x, int y, long la)  { return x > y + la; }
static int  cc_eq(int x, int y, long la)  { return x == y + la; }
/* branch-context comparison with compound long RHS */
static int  cb_lt(int x, int y, long la)  { if (x < y + la) return 1; return 0; }
static int  cb_ge(int x, int y, long la)  { if (x >= y + la) return 1; return 0; }


static long ret_sbig0(void)
{
    return 70000L;
}

static long ret_sbig(long ignored)
{
    if (ignored == 1234567L)
        return ignored;
    return 70000L;
}

static long ret_sbig2(long first, int second)
{
    if (first == 1234567L)
        return first + second;
    return 70000L;
}

static unsigned long ret_ubig0(void)
{
    return 131072UL;
}

static unsigned long ret_ubig(unsigned long ignored)
{
    if (ignored == 1234567UL)
        return ignored;
    return 131072UL;
}

static unsigned long ret_ubig2(unsigned long first, unsigned int second)
{
    if (first == 1234567UL)
        return first + second;
    return 131072UL;
}

static void test_widen_mul_edges(void)
{
    int a, b, cond;
    unsigned int ua, ub;
    long la;

    a = 32767;
    b = 32767;
    chk((long)a * b, 1073676289L, "s16mul maxpos");

    a = -32768;
    b = -32768;
    chk((long)a * b, 1073741824L, "s16mul minneg");

    a = -32768;
    b = 2;
    chk((long)a * b, -65536L, "s16mul minneg2");

    a = -32768;
    b = 32767;
    chk((long)a * b, -1073709056L, "s16mul minmax");

    a = 32767;
    b = -32768;
    chk((long)a * b, -1073709056L, "s16mul maxmin");

    ua = 65535U;
    ub = 65535U;
    chku((unsigned long)ua * ub, 4294836225UL, "u16mul max");

    ua = 40000U;
    ub = 40000U;
    chku((unsigned long)ua * ub, 1600000000UL, "u16mul 40000");

    a = 1;
    b = 2;
    chk(((long)a + 65536L) * b, 131074L, "stale add mul");
    chk(((long)a - 65536L) * b, -131070L, "stale sub mul");
    chk((((long)a) | 0x10000L) * b, 131074L, "stale bor mul");
    chk((((long)a) << 16) * b, 131072L, "stale shl mul");

    cond = 1;
    chk((cond ? 65536L : (long)a) * b, 131072L, "stale cond true mul");
    cond = 0;
    chk((cond ? 65536L : (long)a) * b, 2L, "stale cond false mul");

    ua = 65535U;
    ub = 2U;
    chku(((unsigned long)ua + 65536UL) * ub, 262142UL, "ustale add mul");

    a = 1;
    b = 2;
    chk(ret_sbig((long)a) * b, 140000L, "stale call mul");
    chk((la = ret_sbig((long)a)) * b, 140000L, "stale assign call mul");

    la = 65536L;
    chk((la += (long)a) * b, 131074L, "stale compound add mul");
    la = 65536L;
    chk((la *= (long)b) * b, 262144L, "stale compound mul mul");

    ua = 1U;
    ub = 2U;
    chku(ret_ubig((unsigned long)ua) * ub, 262144UL, "ustale call mul");
}

static void test_stale_marker_boundaries(void)
{
    int a, b;
    unsigned int ua, ub;
    long la;
    unsigned long ula;

    a = 1;
    b = 2;
    chk(((long)a, ret_sbig0()) * b, 140000L, "stale call0 mul");
    chk(ret_sbig2((long)a, b) * b, 140000L, "stale call2 mul");

    chk((la = ret_sbig2((long)a, b)) * b, 140000L, "stale local assign call2 mul");
    chk(((long)a, (la = ret_sbig0())) * b, 140000L, "stale comma local assign mul");

    la = 65536L;
    chk((la |= (long)a) * b, 131074L, "stale compound or mul");
    la = 65536L;
    chk((la ^= (long)a) * b, 131074L, "stale compound xor mul");
    la = 210000L;
    chk((la /= (long)b) * b, 210000L, "stale compound div mul");

    la = 131072L;
    chk((la >>= (long)a) * b, 131072L, "stale shift assign var mul");
    la = 131072L;
    chk(((long)a, (la >>= 1)) * b, 131072L, "stale shift assign loop mul");
    la = 1L;
    chk(((long)a, (la <<= 16)) * b, 131072L, "stale shift assign const mul");

    ua = 1U;
    ub = 2U;
    chku(((unsigned long)ua, ret_ubig0()) * ub, 262144UL, "ustale call0 mul");
    chku(ret_ubig2((unsigned long)ua, ub) * ub, 262144UL, "ustale call2 mul");
    chku((ula = ret_ubig2((unsigned long)ua, ub)) * ub, 262144UL, "ustale local assign call2 mul");
}

static void test_unary_widen_mul(void)
{
    int a, b;
    unsigned int ua, ub;

    /* Unary minus on a freshly widened long produces a computed value that is
     * not a faithful sign/zero-extension of a 16-bit operand, so the 16x16
     * multiply helper must not be used. */
    a = -32768;
    b = 2;
    chk(-(long)a * b, 65536L, "neg minint mul");

    a = 5;
    b = 3;
    chk(-(long)a * b, -15L, "neg small mul");

    a = -7;
    b = 4;
    chk(-(long)a * b, 28L, "neg negsmall mul");

    /* Unary minus on unsigned long: high word becomes 0xffff for x != 0. */
    ua = 1U;
    ub = 2U;
    chku(-(unsigned long)ua * ub, 4294967294UL, "uneg one mul");

    ua = 3U;
    ub = 4U;
    chku(-(unsigned long)ua * ub, 4294967284UL, "uneg three mul");

    /* Unary complement: safe for signed, but ~ of a zero-extended unsigned
     * long sets the high word to 0xffff. */
    a = 5;
    b = 3;
    chk(~(long)a * b, -18L, "compl small mul");

    ua = 5U;
    ub = 1U;
    chku(~(unsigned long)ua * ub, 4294967290UL, "ucompl five mul");
}

static long g_lx_ct = 100000L;

static void test_const_times_long(void)
{
    unsigned char uc;
    signed char sc;
    int x;
    unsigned int ux;
    int si;
    unsigned int ui;
    long lx;
    unsigned long ul;

    /* A small integer constant on the LEFT of '*' must not use the 16-bit
     * emit_mul_hl_const fast path when the other operand is long: the high
     * word would be dropped.  Mirrors the guarded right-operand path. */
    x = 20000;
    chk(5 * (long)x, 100000L, "c5 times long");
    chk((long)x * 5, 100000L, "long times c5");

    x = 30000;
    chk(10 * (long)x, 300000L, "c10 times long");
    chk(3 * (long)x, 90000L, "c3 times long");
    chk(8 * (long)x, 240000L, "c8 times long");

    ux = 50000U;
    chku(5UL * (unsigned long)ux, 250000UL, "uc5 times ulong");

    /* long variable operand (not a cast) */
    lx = 100000L;
    chk(5 * lx, 500000L, "c5 times longvar");
    chk(g_lx_ct * 3, 300000L, "longglob times c3");

    /* long-suffixed constant with a 16-bit operand must stay 32-bit */
    x = 30000;
    chk(5L * x, 150000L, "c5L times int");

    /* wide constant whose low 16 bits alias a fast-path value (65536 -> 0) */
    x = 7;
    chk(65536L * x, 458752L, "wideconst times int");

    /* parenthesized long expression (peek cannot see the type) */
    lx = 1L;
    chk(5 * (lx + 65535L), 327680L, "c5 times paren long");

    /* sanity: plain 16-bit const*int still correct */
    x = 7;
    chk(5 * x, 35L, "c5 times int");

    /* The prefix fast path must still report the promoted result type, not
     * the original byte operand type, so a later cast/store widens all of HL. */
    uc = 255;
    chk((long)(5 * uc), 1275L, "c5 times uchar promoted");

    sc = -2;
    chk((long)(5 * sc), -10L, "c5 times schar promoted");

    si = -1;
    ul = 5U * si;
    chku(ul, 65531UL, "c5U times sint unsigned16");

    ui = 65535U;
    chk((long)(5 * ui), 65531L, "c5 times uint unsigned16");
}

static void test_shift_edges(void)
{
    long a;
    unsigned long ua;

    a = 0x12345678L;
    chk(a >> 8, 1193046L, "s shr8 pos");
    chk(a >> 16, 4660L, "s shr16 pos");
    chk(a >> 24, 18L, "s shr24 pos");

    a = -19088744L;
    chk(a >> 8, -74566L, "s shr8 neg");
    chk(a >> 16, -292L, "s shr16 neg");
    chk(a >> 24, -2L, "s shr24 neg");

    a = -1L;
    chk(a >> 8, -1L, "s shr8 minus1");
    chk(a >> 24, -1L, "s shr24 minus1");

    ua = 0xFEDCBA98UL;
    chku(ua >> 8, 0x00FEDCBAUL, "u shr8 highbit");
    chku(ua >> 16, 0x0000FEDCUL, "u shr16 highbit");
    chku(ua >> 24, 0x000000FEUL, "u shr24 highbit");
}

static void test_long_const_compare_edges(void)
{
    chk(s_gt32767(32767L), 0L, "cmp 32767 gt");
    chk(s_gt32767(32768L), 1L, "cmp 32768 gt");
    chk(s_gt32767(-1L), 0L, "cmp neg gt");

    chk(s_ltm32768(-32768L), 0L, "cmp -32768 lt");
    chk(s_ltm32768(-32769L), 1L, "cmp -32769 lt");
    chk(s_ltm32768(0L), 0L, "cmp zero ltneg");

    chk(s_lt0(-1L), 1L, "cmp -1 lt0");
    chk(s_lt0(0L), 0L, "cmp 0 lt0");
    chk(s_lt0(1L), 0L, "cmp 1 lt0");

    chk(s_le100(100L), 1L, "cmp le eq");
    chk(s_le100(101L), 0L, "cmp le hi");
    chk(s_gtm5(-5L), 0L, "cmp gt neg eq");
    chk(s_gtm5(-4L), 1L, "cmp gt neg hi");

    chk(u_gtbig(3000000000UL), 0L, "ucmp gt eq");
    chk(u_gtbig(3000000001UL), 1L, "ucmp gt hi");
    chk(u_gtbig(1UL), 0L, "ucmp gt low");
    chk(u_lebig(3000000000UL), 1L, "ucmp le eq");
    chk(u_lebig(4000000000UL), 0L, "ucmp le hi");

    chk(br_lt40000(-32768), 1L, "br lt40000 min");
    chk(br_lt40000(0), 1L, "br lt40000 zero");
    chk(br_lt40000(32767), 1L, "br lt40000 max");
    chk(br_ge40000(-32768), 0L, "br ge40000 min");
    chk(br_ge40000(32767), 0L, "br ge40000 max");
    chk(br_gt40000(32767), 0L, "br gt40000 max");
    chk(br_le40000(-32768), 1L, "br le40000 min");
    chk(br_le40000(32767), 1L, "br le40000 max");
    chk(br_cgt_x(-32768), 1L, "br cgt min");
    chk(br_cgt_x(32767), 1L, "br cgt max");
    chk(br_cle_x(-32768), 0L, "br cle min");
    chk(br_cle_x(32767), 0L, "br cle max");
    chk(br_lt32768(32767), 1L, "br lt32768 max");
    chk(br_lt65535(32767), 1L, "br lt65535 max");
    chk(br_lt_hex_u(-1), 0L, "br lt hex -1");
    chk(br_lt_hex_u(32767), 1L, "br lt hex maxpos");
    chk(br_ult_hex_u(65534U), 1L, "br ult hex 65534");
    chk(br_ult_hex_u(65535U), 0L, "br ult hex 65535");
    chk(clamp_int_min(-32769L), -32768L, "return int min clamp");
    chk(clamp_int_min(-32768L), -32768L, "return int min passthrough");
}

static void test_compound_long_ops(void)
{
    /* arithmetic: 16-bit LHS, compound long RHS the prediction misses */
    chk(co_add(5, 0, 100000L), 100005L, "co_add 100005");
    chk(co_add(5, 30000, 60000L), 90005L, "co_add 90005");
    chk(co_sub(5, 0, 100000L), -99995L, "co_sub -99995");
    chk(co_sub(5, 30000, 60000L), -89995L, "co_sub -89995");
    chk(co_mul(3, 100000L), 300000L, "co_mul 300000");
    chk(co_mul(-3, 100000L), -300000L, "co_mul -300000");
    chk(co_div(1000000L, 0, 1000L), 1000L, "co_div 1000");
    chk(co_div(-1000000L, 0, 1000L), -1000L, "co_div -1000");
    chk(co_mod(1000003L, 0, 1000L), 3L, "co_mod 3");
    chk(co_and(0x7, 0, 0x12345L), (long)(0x7 & 0x12345L), "co_and");
    chk(co_or(0x7, 0, 0x12340L), (long)(0x7 | 0x12340L), "co_or");
    chk(co_xor(0x7, 0, 0x12345L), (long)(0x7 ^ 0x12345L), "co_xor");

    /* value-context comparisons */
    chk(cc_lt(5, 30000, 60000L), 1L, "cc_lt 5<90000");
    chk(cc_lt(5, 0, -100000L), 0L, "cc_lt 5<-100000");
    chk(cc_gt(5, 0, -100000L), 1L, "cc_gt 5>-100000");
    chk(cc_eq(7, 0, 7L), 1L, "cc_eq 7==7");
    chk(cc_eq(7, 0, 70000L), 0L, "cc_eq 7==70000");

    /* branch-context comparisons */
    chk(cb_lt(5, 30000, 60000L), 1L, "cb_lt 5<90000");
    chk(cb_lt(5, 0, -100000L), 0L, "cb_lt 5<-100000");
    chk(cb_ge(5, 0, -100000L), 1L, "cb_ge 5>=-100000");
    chk(cb_ge(5, 30000, 60000L), 0L, "cb_ge 5>=90000");
}

/* A long assignment used as a live value must produce BOTH the stored lvalue and
 * the propagated result correctly.  int32_t keeps the width identical on the
 * clang host (32-bit int) and dcc (32-bit long). */
static void test_local_long_assign_value(void)
{
    struct LongBox box;
    int32_t a;
    int32_t arr[2];
    int32_t b;
    int32_t *p;

    a = 0L;
    b = (a = 100000L);              /* chained plain = */
    chk(a, 100000L, "chain= field");
    chk(b, 100000L, "chain= result");

    a = 100000L;
    b = (a += 50000L);             /* compound += live */
    chk(a, 150000L, "chain+= field");
    chk(b, 150000L, "chain+= result");

    a = 4L;
    b = (a *= 100000L);            /* compound *= live */
    chk(a, 400000L, "chain*= field");
    chk(b, 400000L, "chain*= result");

    a = 900000L;
    b = (a -= 100000L);            /* compound -= live */
    chk(a, 800000L, "chain-= field");
    chk(b, 800000L, "chain-= result");

    glive = 10L;
    b = ret_global_live_add();
    chk(glive, 15L, "global+= field");
    chk(b, 15L, "global+= result");

    a = 20L;
    p = &a;
    b = ret_deref_live_add(p);
    chk(a, 27L, "deref+= field");
    chk(b, 27L, "deref+= result");

    box.l = 0x80L;
    b = ret_member_live_shr(&box);
    chk(box.l, 0x20L, "member>>= field");
    chk(b, 0x20L, "member>>= result");

    arr[1] = 9L;
    b = id32(arr[1] *= 5L);
    chk(arr[1], 45L, "index*= field");
    chk(b, 45L, "index*= result");

    glarr[1] = 100L;
    b = id32(glarr[1] -= 58L);
    chk(glarr[1], 42L, "gindex-= field");
    chk(b, 42L, "gindex-= result");

    box.a[1] = 0x10L;
    b = id32(box.a[1] <<= 2);
    chk(box.a[1], 0x40L, "mindex<<= field");
    chk(b, 0x40L, "mindex<<= result");
}

int main(void)
{
    test_widen_mul_edges();
    test_stale_marker_boundaries();
    test_unary_widen_mul();
    test_const_times_long();
    test_shift_edges();
    test_long_const_compare_edges();
    test_compound_long_ops();
    test_local_long_assign_value();

    if (fails == 0)
        printf("tlongopt passed with great success\n");
    else
        printf("tlongopt FAILED: %d mismatch(es)\n", fails);
    return fails != 0;
}
