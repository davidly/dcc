/*
 * tctxflt.c - parenthesized/compound float operands in arithmetic and compares.
 *
 * Regression for the "peek blind spot" class: peek_simple_unary_type only
 * inspects the first token of a binary operator's right-hand side, so a
 * parenthesized or otherwise compound RHS such as  x + (fa * fb)  was
 * mis-predicted as 16-bit and the float operand was dropped.  The fix makes
 * the post-generation type (g_expr_type) authoritative: after the RHS is
 * generated, an actual float operand routes through the float helpers in both
 * value context (gen_add/gen_mul) and comparison context (gen_rel/gen_eq and
 * the if-condition direct-branch path).
 *
 * Each value is forced through a function call so the operands are real
 * runtime values (not constant-folded) and the compound RHS is genuinely
 * parenthesized at the point the operator is generated.
 *
 * Prints exactly one summary line so runall captures a stable result.
 */
#include <stdio.h>

static int fails;

static void chk(long got, long want, const char *name)
{
    if (got != want) {
        printf("FAIL %s got=%ld want=%ld\n", name, got, want);
        fails++;
    }
}

/* ---- value context: int LHS, parenthesized float RHS ---- */

static long add_pf(int x, float a, float b)   { return (long)(x + (a * b)); }
static long sub_pf(int x, float a, float b)   { return (long)(x - (a * b)); }   /* non-commutative */
static long mul_pf(int x, float a, float b)   { return (long)(x * (a + b)); }
static long div_pf(int x, float a, float b)   { return (long)(x / (a + b)); }   /* non-commutative */

/* ---- value context: unsigned LHS, parenthesized float RHS ---- */
static long uadd_pf(unsigned u, float a, float b) { return (long)(u + (a * b)); }

/* ---- compare context (value of relational expression) ---- */
static int lt_pf(int x, float a, float b)     { return x < (a * b); }
static int ge_pf(int x, float a, float b)     { return x >= (a * b); }
static int eq_pf(int x, float a, float b)     { return x == (a + b); }
static int ne_pf(int x, float a, float b)     { return x != (a + b); }

/* ---- compare context inside an if-condition (direct-branch path) ---- */
static int if_lt_pf(int x, float a, float b)
{
    if (x < (a * b))
        return 1;
    return 0;
}

static int if_gt_pf(int x, float a, float b)
{
    if (x > (a * b))
        return 100;
    return 0;
}

/* ---- direct-branch context with a float HIDDEN from the snippet pre-scan ----
 * These are not float literals or float-typed identifiers, so the condition
 * pre-scan cannot classify them as float; the codegen must fall back on the
 * type computed after generating the operand.
 */
struct flt_box { int i; float f; };

/* RHS cast-to-float */
static int if_rhs_cast_lt(int x, int y) { if (x < (float)y) return 1; return 0; }
static int if_rhs_cast_eq(int x, int y) { if (x == (float)y) return 1; return 0; }
/* LHS cast-to-float (variable RHS) */
static int if_lhs_cast_lt(int x, int y) { if ((float)x < y) return 1; return 0; }
/* LHS cast-to-float, integer constant RHS (the constant-RHS fast path) */
static int if_lhs_cast_const(int x) { if ((float)x < 10) return 1; return 0; }
/* RHS struct-member float (hidden, not a cast) */
static int if_rhs_memb_lt(int x, struct flt_box *s) { if (x < s->f) return 1; return 0; }
/* LHS struct-member float */
static int if_lhs_memb_lt(struct flt_box *s, int x) { if (s->f < x) return 1; return 0; }

/* ---- long operand mixed with a hidden float (the long-branch blind spot) ----
 * Here one side is a 32-bit long, so the snippet/peek pre-analysis predicts a
 * long common type; the other side is a hidden float (a cast, a typedef cast,
 * or a struct member).  The codegen must promote the long to float once the
 * operand's real type is known instead of casting the float bits to long.
 * Both the direct-branch (if) path and the value path are covered, including
 * unsigned long (which needs the unsigned long->float conversion).
 */
typedef float flt_t;

static int l_lt_cast(long l, int y)  { if (l < (float)y) return 1; return 0; }
static int l_ge_cast(long l, int y)  { if (l >= (float)y) return 1; return 0; }
static int l_eq_cast(long l, int y)  { if (l == (float)y) return 1; return 0; }
static int cast_lt_l(int x, long l)  { if ((float)x < l) return 1; return 0; }
static int l_lt_memb(long l, struct flt_box *s) { if (l < s->f) return 1; return 0; }
static int memb_gt_l(struct flt_box *s, long l) { if (s->f > l) return 1; return 0; }
static int l_lt_tdef(long l, int y) { if (l < (flt_t)y) return 1; return 0; }
static int ul_lt_cast(unsigned long u, int y) { if (u < (float)y) return 1; return 0; }
static int l_lt_pf(long l, float a, float b)   { return l < (a * b); }
static long l_sub_pf(long l, float a, float b) { return (long)(l - (a * b)); }

/* ---- hidden float CONSTANT in an ordered direct branch ----
 * The long-vs-constant inline compare (try_emit_long_cmp_const_branch) and the
 * all-constant relational fold must NOT treat a cast-to-float constant as an
 * integer.  (float)16777217L rounds to 16777216.0f in single precision, so
 * 2^24 is NOT less than it.  The integer-only constant folder would compare
 * 16777216 < 16777217 and wrongly answer "true"; the codegen must convert the
 * long to float and compare with single-precision rounding instead.
 */
static int l_lt_fk(long l)     { if (l < (float)16777217L) return 1; return 0; }
static int l_ge_fk(long l)     { if (l >= (float)16777217L) return 1; return 0; }
static int l_lt_fkexpr(long l) { if (l < (float)(16777216L + 1L)) return 1; return 0; }
static int allconst_lt_fk(void){ if (16777216L < (float)16777217L) return 1; return 0; }

/* ---- float truth-value testing in if / ! / ?: / && / || ----
 * A float is true iff its magnitude is nonzero; +0.0 and -0.0 are both false.
 * The condition test must inspect the whole 32-bit payload (sign bit masked),
 * not just the low word HL, or e.g. 1.0f (0x3F800000, low word 0) reads false.
 */
static int truth_if(float f)         { if (f) return 1; return 0; }
static int truth_not(float f)        { return !f; }
static int truth_tern(float f)       { return f ? 1 : 0; }
static int truth_and(float f, int x) { return (f && x) ? 1 : 0; }
static int truth_or(float f, int x)  { return (f || x) ? 1 : 0; }
static int truth_memb(struct flt_box *s) { if (s->f) return 1; return 0; }

/* (float) cast inside an integer constant expression must round in single
 * precision: (long)(float)16777217L is 16777216, not 16777217. */
static long cfk_static = (long)(float)16777217L;
static int  cfk_arr[(int)5.0f];
enum { CFK_ENUM = (int)5.0f };

/* ---- conditional operator with mixed float / non-float arms ----
 * The usual arithmetic conversions make the whole ?: float if either result
 * arm is float.  The true arm is generated before the false arm is parsed, so
 * the codegen must look ahead and convert the selected arm to the common float
 * type; otherwise an integer arm leaves raw integer bits where a float is
 * expected.  Covers int/float, float/int, long/float, a float hidden in a
 * compound arm, and a float hidden in a nested conditional arm.
 */
static long cond_if(int c)            { return (long)(c ? 2 : 3.5f); }
static long cond_fi(int c)            { return (long)(c ? 2.5f : 3); }
static long cond_lf(int c)            { return (long)(c ? 16777216L : 3.5f); }
static long cond_compound(int c, float f) { return (long)(c ? 2 : (f + 1.0f)); }
static long cond_nested(int a, int b) { return (long)(a ? 1 : b ? 2 : 3.5f); }

/* AST typing resolves a float reachable only through a struct member or a cast
 * buried inside a nested ternary arm - shapes a shallow token scan misses but a
 * full typed expression tree catches.  s->f / (float)y make the whole
 * expression float, so the selected integer arm is converted. */
static long cond_nmemb(int a, int b, struct flt_box *s) { return (long)(a ? 1 : b ? 2 : s->f); }
static long cond_ncast(int a, int b, int y) { return (long)(a ? 1 : b ? 2 : (float)y); }

/* A bare float ARRAY expression decays to float* in value context; AST typing
 * must report the conditional arm as pointer, not float, or a ternary of float
 * arrays passed to a float* parameter pushes float bits instead of the pointer. */
static float cflt_a[4];
static float *cflt_p;
static long use_fptr(float *p) { return (long)p[1]; }
static long cond_arr_ptr(int c) { return use_fptr(c ? cflt_p : cflt_a); }

/* ---- value context: long vs hidden float, ALL six relops ----
 * Only "<" was covered above; each relop has its own gen_rel/gen_eq long-branch
 * float fallback, so exercise gt/le/ge/eq/ne too. a*b = 10.0. */
static int l_gt_pf(long l, float a, float b) { return l > (a * b); }
static int l_le_pf(long l, float a, float b) { return l <= (a * b); }
static int l_ge_pf(long l, float a, float b) { return l >= (a * b); }
static int l_eq_pf(long l, float a, float b) { return l == (a * b); }
static int l_ne_pf(long l, float a, float b) { return l != (a * b); }

/* ---- non-commutative long+float arithmetic (only subtract was covered) ---- */
static long l_mul_pf(long l, float a, float b) { return (long)(l * (a + b)); }
static long l_div_pf(long l, float a, float b) { return (long)(l / (a + b)); }
/* result float threads through a chain of mixed ops */
static long chain_pf(long l, float a, float b) { return (long)(l + (a * b) + 2.0f); }
/* negative long mixed with hidden float: signed long->float conversion */
static int neg_l_lt(long l, int y) { if (l < (float)y) return 1; return 0; }
static long neg_l_add(long l, float a, float b) { return (long)(l + (a * b)); }

/* ---- float truthiness in loop conditions + via array elem / func return ----
 * if/!/?:/&&/|| were covered above; while/for/do-while are distinct statement
 * paths, and the operand can hide a float behind an array index or a call. */
static float tf_arr[2];
static float tf_ret(int y) { return (float)y; }
static int truth_while(float f) { int n = 0; while (f) { n++; f = 0.0f; } return n; }
static int truth_for(float f) { int n = 0; int i; for (i = 0; f && i < 3; i++) n++; return n; }
static int truth_do(float f, int lim) { int n = 0; do { n++; if (n >= lim) f = 0.0f; } while (f); return n; }
static int truth_elem(float *a, int i) { if (a[i]) return 1; return 0; }
static int truth_call(int y) { if (tf_ret(y)) return 1; return 0; }
static int truth_not_memb(struct flt_box *s) { return !s->f; }

/* ---- (float) cast in more integer-constant-expression contexts ----
 * Covers negative and unsigned operands plus a case label. */
static long cfk_neg = (long)(float)(-16777217L);
static long cfk_un = (long)(unsigned)(float)5;
static int cfk_case(long v) { switch (v) { case (long)16777216.0f: return 1; default: return 0; } }

/* ---- conditional operator: oracle precision cases ----
 * float-returning call arm (leaf float), comparison-result arm (stays int),
 * float only in a nested CONDITION (result arms all int -> stays int-correct). */
static long cond_callarm(int c, int y) { return (long)(c ? 2 : tf_ret(y)); }
static int cond_cmparm(int c, int x) { return c ? 1 : (x < 5); }
static long cond_condfloat(int c, float f) { return (long)(c ? 7 : (f > 0.0f ? 8 : 9)); }

/* ---- conditional array-decay: more shapes the oracle must report as pointer ----
 * true-arm array, struct array field, multidim row, cast-to-pointer arm, and a
 * string-literal arm (char*, never float). */
struct ad_row { float v[3]; int n; };
static float ad_a[4];
static float *ad_p;
static struct ad_row ad_rows[2];
static float ad_m[2][3];
static long ad_true_arm(int c) { float *p; p = c ? ad_a : ad_p; return (long)p[1]; }
static long ad_field(int c) { float *p; p = c ? ad_rows[0].v : ad_rows[1].v; return (long)p[1]; }
static long ad_multidim(int c) { float *p; p = c ? ad_m[0] : ad_m[1]; return (long)p[1]; }
static int ad_castptr(int c, float *q) { float *p; p = c ? (float *)0 : q; return p == q; }
static int ad_strarm(int c, char *s) { char *p; p = c ? "hi" : s; return p[0]; }

int main(void)
{
    struct flt_box box;

    fails = 0;
    box.i = 0;
    box.f = 10.0f;

    /* value arithmetic: 3 + 2.5*4.0 = 13 ; 3 - 10 = -7 ; 3*(2.5+1.5)=12 ; 30/(2.5+0.5)=10 */
    chk(add_pf(3, 2.5f, 4.0f), 13L, "add_pf");
    chk(sub_pf(3, 2.5f, 4.0f), -7L, "sub_pf");
    chk(mul_pf(3, 2.5f, 1.5f), 12L, "mul_pf");
    chk(div_pf(30, 2.5f, 0.5f), 10L, "div_pf");
    chk(uadd_pf(5u, 2.0f, 3.0f), 11L, "uadd_pf");

    /* relational value context */
    chk(lt_pf(3, 2.5f, 4.0f), 1L, "lt_pf");     /* 3 < 10 */
    chk(lt_pf(20, 2.5f, 4.0f), 0L, "lt_pf2");   /* 20 < 10 -> 0 */
    chk(ge_pf(20, 2.5f, 4.0f), 1L, "ge_pf");    /* 20 >= 10 */
    chk(eq_pf(7, 3.0f, 4.0f), 1L, "eq_pf");     /* 7 == 7.0 */
    chk(ne_pf(7, 3.0f, 4.0f), 0L, "ne_pf");     /* 7 != 7.0 -> 0 */

    /* if-condition direct-branch context */
    chk(if_lt_pf(3, 2.5f, 4.0f), 1L, "if_lt_pf");    /* 3 < 10 */
    chk(if_lt_pf(20, 2.5f, 4.0f), 0L, "if_lt_pf2");  /* 20 < 10 -> 0 */
    chk(if_gt_pf(20, 2.5f, 4.0f), 100L, "if_gt_pf"); /* 20 > 10 */
    chk(if_gt_pf(3, 2.5f, 4.0f), 0L, "if_gt_pf2");   /* 3 > 10 -> 0 */

    /* direct-branch context, float hidden from the snippet pre-scan */
    chk(if_rhs_cast_lt(3, 10), 1L, "if_rhs_cast_lt");     /* 3 < 10.0 */
    chk(if_rhs_cast_lt(20, 10), 0L, "if_rhs_cast_lt2");   /* 20 < 10.0 -> 0 */
    chk(if_rhs_cast_eq(7, 7), 1L, "if_rhs_cast_eq");      /* 7 == 7.0 */
    chk(if_rhs_cast_eq(8, 7), 0L, "if_rhs_cast_eq2");     /* 8 == 7.0 -> 0 */
    chk(if_lhs_cast_lt(3, 10), 1L, "if_lhs_cast_lt");     /* 3.0 < 10 */
    chk(if_lhs_cast_lt(20, 10), 0L, "if_lhs_cast_lt2");   /* 20.0 < 10 -> 0 */
    chk(if_lhs_cast_const(3), 1L, "if_lhs_cast_const");   /* 3.0 < 10 */
    chk(if_lhs_cast_const(20), 0L, "if_lhs_cast_const2"); /* 20.0 < 10 -> 0 */
    chk(if_rhs_memb_lt(3, &box), 1L, "if_rhs_memb_lt");   /* 3 < 10.0 */
    chk(if_rhs_memb_lt(20, &box), 0L, "if_rhs_memb_lt2"); /* 20 < 10.0 -> 0 */
    chk(if_lhs_memb_lt(&box, 20), 1L, "if_lhs_memb_lt");  /* 10.0 < 20 */
    chk(if_lhs_memb_lt(&box, 3), 0L, "if_lhs_memb_lt2");  /* 10.0 < 3 -> 0 */

    /* long mixed with a hidden float: direct-branch path (long-branch blind spot) */
    chk(l_lt_cast(3L, 10), 1L, "l_lt_cast");     chk(l_lt_cast(20L, 10), 0L, "l_lt_cast2");
    chk(l_ge_cast(10L, 10), 1L, "l_ge_cast");    chk(l_ge_cast(9L, 10), 0L, "l_ge_cast2");
    chk(l_eq_cast(7L, 7), 1L, "l_eq_cast");      chk(l_eq_cast(8L, 7), 0L, "l_eq_cast2");
    chk(cast_lt_l(3, 10L), 1L, "cast_lt_l");     chk(cast_lt_l(20, 10L), 0L, "cast_lt_l2");
    chk(l_lt_memb(3L, &box), 1L, "l_lt_memb");   chk(l_lt_memb(20L, &box), 0L, "l_lt_memb2");
    chk(memb_gt_l(&box, 3L), 1L, "memb_gt_l");   chk(memb_gt_l(&box, 20L), 0L, "memb_gt_l2");
    chk(l_lt_tdef(3L, 10), 1L, "l_lt_tdef");     chk(l_lt_tdef(20L, 10), 0L, "l_lt_tdef2");
    chk(ul_lt_cast(5UL, 10), 1L, "ul_lt_cast");  chk(ul_lt_cast(0x80000000UL, 1), 0L, "ul_lt_cast2");
    /* long mixed with a hidden float: value path */
    chk(l_lt_pf(3L, 2.5f, 4.0f), 1L, "l_lt_pf"); chk(l_lt_pf(20L, 2.5f, 4.0f), 0L, "l_lt_pf2");
    chk(l_sub_pf(100L, 2.5f, 4.0f), 90L, "l_sub_pf");

    /* hidden float CONSTANT in an ordered direct branch (no integer mis-fold).
     * (float)16777217L rounds to 16777216.0f, so 2^24 is not < it. */
    chk(l_lt_fk(16777216L), 0L, "l_lt_fk");        /* 2^24 < 16777216.0f -> 0 */
    chk(l_lt_fk(16777215L), 1L, "l_lt_fk2");       /* 2^24-1 < 16777216.0f -> 1 */
    chk(l_ge_fk(16777216L), 1L, "l_ge_fk");        /* 2^24 >= 16777216.0f -> 1 */
    chk(l_lt_fkexpr(16777216L), 0L, "l_lt_fkexpr");/* cast of const expr, same -> 0 */
    chk(allconst_lt_fk(), 0L, "allconst_lt_fk");   /* 16777216.0f < 16777216.0f -> 0 */

    /* float truth-value testing: nonzero is true, +/-0.0 is false */
    chk(truth_if(1.0f), 1L, "truth_if_one");       /* low word 0, must still be true */
    chk(truth_if(0.0f), 0L, "truth_if_zero");
    chk(truth_if(-0.0f), 0L, "truth_if_negzero");  /* -0.0 is false */
    chk(truth_not(0.0f), 1L, "truth_not_zero");
    chk(truth_not(2.0f), 0L, "truth_not_two");
    chk(truth_tern(1.0f), 1L, "truth_tern_one");
    chk(truth_tern(0.0f), 0L, "truth_tern_zero");
    chk(truth_and(1.0f, 1), 1L, "truth_and_TT");
    chk(truth_and(0.0f, 1), 0L, "truth_and_FT");
    chk(truth_or(0.0f, 0), 0L, "truth_or_FF");
    chk(truth_or(0.0f, 1), 1L, "truth_or_FT");
    chk(truth_memb(&box), 1L, "truth_memb");       /* box.f == 10.0 */

    /* (float) cast in an integer constant expression rounds in single prec */
    chk(cfk_static, 16777216L, "cfk_static");
    chk((long)(sizeof(cfk_arr) / sizeof(cfk_arr[0])), 5L, "cfk_arr_dim");
    chk((long)CFK_ENUM, 5L, "cfk_enum");

    /* conditional operator: mixed float / non-float arms convert to float */
    chk(cond_if(1), 2L, "cond_if_T");   chk(cond_if(0), 3L, "cond_if_F");   /* 3.5f -> 3 */
    chk(cond_fi(1), 2L, "cond_fi_T");   chk(cond_fi(0), 3L, "cond_fi_F");   /* 2.5f -> 2 */
    chk(cond_lf(1), 16777216L, "cond_lf_T"); chk(cond_lf(0), 3L, "cond_lf_F");
    chk(cond_compound(0, 2.0f), 3L, "cond_compound"); /* 2.0+1.0 -> 3 */
    chk(cond_nested(1, 0), 1L, "cond_nested_a");
    chk(cond_nested(0, 1), 2L, "cond_nested_b");
    chk(cond_nested(0, 0), 3L, "cond_nested_c"); /* 3.5f -> 3 */
    chk(cond_nmemb(0, 0, &box), 10L, "cond_nmemb"); /* box.f 10.0 -> 10 */
    chk(cond_ncast(0, 0, 5), 5L, "cond_ncast");     /* (float)5 -> 5 */
    cflt_a[0] = 4.0f; cflt_a[1] = 7.0f; cflt_p = cflt_a;
    chk(cond_arr_ptr(1), 7L, "cond_arr_ptr_T");     /* bare float array decays to float* */
    chk(cond_arr_ptr(0), 7L, "cond_arr_ptr_F");

    /* value-context long vs hidden float: all six relops (a*b = 10.0) */
    chk(l_gt_pf(20L, 2.5f, 4.0f), 1L, "l_gt_pf");  chk(l_gt_pf(5L, 2.5f, 4.0f), 0L, "l_gt_pf2");
    chk(l_le_pf(10L, 2.5f, 4.0f), 1L, "l_le_pf");  chk(l_le_pf(11L, 2.5f, 4.0f), 0L, "l_le_pf2");
    chk(l_ge_pf(10L, 2.5f, 4.0f), 1L, "l_ge_pf");  chk(l_ge_pf(9L, 2.5f, 4.0f), 0L, "l_ge_pf2");
    chk(l_eq_pf(10L, 2.5f, 4.0f), 1L, "l_eq_pf");  chk(l_eq_pf(11L, 2.5f, 4.0f), 0L, "l_eq_pf2");
    chk(l_ne_pf(11L, 2.5f, 4.0f), 1L, "l_ne_pf");  chk(l_ne_pf(10L, 2.5f, 4.0f), 0L, "l_ne_pf2");

    /* non-commutative long+float arithmetic + chained + negative */
    chk(l_mul_pf(3L, 2.5f, 1.5f), 12L, "l_mul_pf");   /* 3 * 4.0 */
    chk(l_div_pf(30L, 2.0f, 1.0f), 10L, "l_div_pf");  /* 30 / 3.0 */
    chk(chain_pf(3L, 2.5f, 4.0f), 15L, "chain_pf");   /* 3 + 10.0 + 2.0 */
    chk(neg_l_lt(-20L, -10), 1L, "neg_l_lt");   chk(neg_l_lt(-5L, -10), 0L, "neg_l_lt2");
    chk(neg_l_add(-20L, 2.5f, 4.0f), -10L, "neg_l_add"); /* -20 + 10.0 */

    /* float truthiness in loop conditions + hidden via array elem / call / !member */
    tf_arr[0] = 0.0f; tf_arr[1] = 2.0f;
    chk(truth_while(1.0f), 1L, "truth_while_one");  chk(truth_while(0.0f), 0L, "truth_while_zero");
    chk(truth_for(1.0f), 3L, "truth_for_one");      chk(truth_for(0.0f), 0L, "truth_for_zero");
    chk(truth_do(1.0f, 3), 3L, "truth_do_three");   chk(truth_do(0.0f, 3), 1L, "truth_do_zero");
    chk(truth_elem(tf_arr, 1), 1L, "truth_elem_nz"); chk(truth_elem(tf_arr, 0), 0L, "truth_elem_z");
    chk(truth_call(3), 1L, "truth_call_nz");        chk(truth_call(0), 0L, "truth_call_z");
    chk(truth_not_memb(&box), 0L, "truth_not_memb"); /* !10.0 -> 0 */

    /* (float) cast in more integer-constant contexts */
    chk(cfk_neg, -16777216L, "cfk_neg");            /* (float)-16777217 rounds */
    chk(cfk_un, 5L, "cfk_un");
    chk((long)cfk_case(16777216L), 1L, "cfk_case_hit");
    chk((long)cfk_case(16777217L), 0L, "cfk_case_miss");

    /* conditional operator: oracle precision cases */
    chk(cond_callarm(1, 9), 2L, "cond_callarm_T"); chk(cond_callarm(0, 9), 9L, "cond_callarm_F");
    chk((long)cond_cmparm(0, 3), 1L, "cond_cmparm_lt"); chk((long)cond_cmparm(0, 9), 0L, "cond_cmparm_ge");
    chk((long)cond_cmparm(1, 9), 1L, "cond_cmparm_sel");
    chk(cond_condfloat(1, 1.0f), 7L, "cond_condfloat_T");
    chk(cond_condfloat(0, 1.0f), 8L, "cond_condfloat_F8");
    chk(cond_condfloat(0, -1.0f), 9L, "cond_condfloat_F9");

    /* conditional array-decay shapes: each arm denotes float* (or char*), not float */
    ad_a[0] = 1.0f; ad_a[1] = 7.0f; ad_p = ad_a;
    ad_rows[0].v[1] = 2.0f; ad_rows[1].v[1] = 5.0f;
    ad_m[0][1] = 2.0f; ad_m[1][1] = 5.0f;
    chk(ad_true_arm(1), 7L, "ad_true_arm_T"); chk(ad_true_arm(0), 7L, "ad_true_arm_F");
    chk(ad_field(1), 2L, "ad_field_T");       chk(ad_field(0), 5L, "ad_field_F");
    chk(ad_multidim(1), 2L, "ad_multidim_T"); chk(ad_multidim(0), 5L, "ad_multidim_F");
    chk((long)ad_castptr(1, ad_a), 0L, "ad_castptr_T"); chk((long)ad_castptr(0, ad_a), 1L, "ad_castptr_F");
    chk((long)ad_strarm(1, "Z"), (long)'h', "ad_strarm_T"); chk((long)ad_strarm(0, "Z"), (long)'Z', "ad_strarm_F");

    if (fails == 0)
        printf("tctxflt passed with great success\n");
    else
        printf("tctxflt FAILED with %d error(s)\n", fails);
    return 0;
}
