/* tptrcnd.c - C89 regression test for complex pointer expressions in conditionals.
 *
 * Focus: complex pointer/struct/array expressions used in if, while, do/while,
 * for, logical operators, comparison operators, and ?: condition tests.
 *
 * Expected output:
 * tptrcnd start
 * PASS
 */

#include <stdio.h>

#ifndef MIR_CLOBBER_IF_COUNT
#define MIR_CLOBBER_IF_COUNT 41
#endif
#ifndef MIR_CLOBBER_IF_I010
#define MIR_CLOBBER_IF_I010 7005
#endif

struct Leaf {
    int v;
    int a[4];
    char cv;
    char ca[4];
    long lv;
    long la[4];
};

struct Node {
    struct Leaf leaf[3];
    struct Leaf *pl;
    int *pi;
    char *pc;
    long *plong;
    int m[2][3];
    char cm[2][3];
    long lm[2][3];
};

struct Wrapper {
    struct Node n[2];
    struct Node *pn;
    struct Leaf *lp[3];
    int *ip[4];
    char *cp[4];
    long *longp[4];
};

struct Wrapper gw[2];
struct Wrapper *gwp[2];
struct Wrapper **gwpp[2];

int gi[8];
char gc[8];
long gl[8];

static int fails;

static void fail(name)
char *name;
{
    printf("FAIL %s\n", name);
    fails++;
}

static void check_int(name, got, exp)
char *name;
int got;
int exp;
{
    if (got != exp) {
        printf("FAIL %s got %d expected %d\n", name, got, exp);
        fails++;
    }
}

static struct Wrapper *pickw(pp, n)
struct Wrapper **pp;
int n;
{
    return *(pp + n);
}

static struct Node *pickn(wp, n)
struct Wrapper *wp;
int n;
{
    return wp->n + n;
}

static struct Leaf *pickl(np, n)
struct Node *np;
int n;
{
    return np->leaf + n;
}

static int *pickip(wp, n)
struct Wrapper *wp;
int n;
{
    return *(wp->ip + n);
}

static char *pickcp(wp, n)
struct Wrapper *wp;
int n;
{
    return *(wp->cp + n);
}

static long *picklp(wp, n)
struct Wrapper *wp;
int n;
{
    return *(wp->longp + n);
}

static void init_wrapper(w, base)
struct Wrapper *w;
int base;
{
    int wi;
    int ni;
    int li;
    int k;
    int r;
    int c;

    for (ni = 0; ni < 2; ni++) {
        for (li = 0; li < 3; li++) {
            w->n[ni].leaf[li].v = base + ni * 100 + li * 10 + 1;
            w->n[ni].leaf[li].cv = (char)((base + ni * 20 + li * 3 + 2) & 127);
            w->n[ni].leaf[li].lv = (long)base * 1000L + (long)ni * 100L + (long)li * 10L + 3L;
            for (k = 0; k < 4; k++) {
                w->n[ni].leaf[li].a[k] = base + ni * 100 + li * 10 + k + 20;
                w->n[ni].leaf[li].ca[k] = (char)((base + ni * 20 + li * 5 + k + 30) & 127);
                w->n[ni].leaf[li].la[k] = (long)base * 1000L + (long)ni * 100L + (long)li * 10L + (long)k + 40L;
            }
        }
        for (r = 0; r < 2; r++) {
            for (c = 0; c < 3; c++) {
                w->n[ni].m[r][c] = base + ni * 100 + r * 10 + c + 200;
                w->n[ni].cm[r][c] = (char)((base + ni * 20 + r * 5 + c + 60) & 127);
                w->n[ni].lm[r][c] = (long)base * 1000L + (long)ni * 100L + (long)r * 10L + (long)c + 300L;
            }
        }
        w->n[ni].pl = &w->n[ni].leaf[1];
        w->n[ni].pi = &w->n[ni].m[1][0];
        w->n[ni].pc = &w->n[ni].cm[1][0];
        w->n[ni].plong = &w->n[ni].lm[1][0];
    }

    w->pn = &w->n[0];
    for (wi = 0; wi < 3; wi++) {
        w->lp[wi] = &w->n[wi & 1].leaf[wi];
    }
    for (k = 0; k < 4; k++) {
        w->ip[k] = &w->n[k & 1].leaf[k % 3].a[k & 3];
        w->cp[k] = &w->n[k & 1].leaf[k % 3].ca[k & 3];
        w->longp[k] = &w->n[k & 1].leaf[k % 3].la[k & 3];
    }
}

int main()
{
    struct Wrapper lw[2];
    struct Wrapper *lwp[2];
    struct Wrapper **lwpp[2];
    struct Wrapper *wp;
    struct Node *np;
    struct Leaf *lp;
    int li[8];
    char lc[8];
    long ll[8];
    int i;
    int count;
    int sum;
    int guard;

    printf("tptrcnd start\n");

    init_wrapper(&gw[0], 1000);
    init_wrapper(&gw[1], 2000);
    init_wrapper(&lw[0], 3000);
    init_wrapper(&lw[1], 4000);

    gwp[0] = &gw[0];
    gwp[1] = &gw[1];
    gwpp[0] = &gwp[0];
    gwpp[1] = &gwp[1];

    lwp[0] = &lw[0];
    lwp[1] = &lw[1];
    lwpp[0] = &lwp[0];
    lwpp[1] = &lwp[1];

    for (i = 0; i < 8; i++) {
        gi[i] = 5000 + i;
        gc[i] = (char)(70 + i);
        gl[i] = 600000L + (long)i;
        li[i] = 7000 + i;
        lc[i] = (char)(80 + i);
        ll[i] = 800000L + (long)i;
    }

    wp = &lw[0];
    np = &lw[0].n[0];
    lp = &lw[0].n[0].leaf[0];

    count = 0;
    if ((*lwpp[0])->n[1].leaf[2].a[3] == 3143) count++; else fail("if_i001");
    if ((*(lwpp[1]))->pn[1].leaf[0].v == 4101) count++; else fail("if_i002");
    if ((*(*gwpp[0])).n[0].pl->a[2] == 1032) count++; else fail("if_i003");
    if (*((*(*gwpp[1])).n[1].pi + 2) == 2312) count++; else fail("if_i004");
    if ((*(pickw(lwp, 0)->ip + 3))[0] == 3123) count++; else fail("if_i005");
    if (pickn(pickw(gwp, 1), 0)->leaf[2].a[1] == 2041) count++; else fail("if_i006");
    if (pickl(&lw[1].n[1], 2)->v == 4121) count++; else fail("if_i007");
    if (*(&((&lw[0])->n[0].m[1][2])) == 3212) count++; else fail("if_i008");
    if (*(gi + 5) == 5005) count++; else fail("if_i009");
    if (*((li + 2) + 3) == MIR_CLOBBER_IF_I010)
        count++;
    else
        fail("if_i010");
    if ((*lwpp[0])->n[1].leaf[2].ca[3] == 119) count++; else fail("if_c001");
    if ((*(lwpp[1]))->pn[1].leaf[0].cv == 54) count++; else fail("if_c002");
    if ((*(*gwpp[0])).n[0].pl->ca[2] == 13) count++; else fail("if_c003");
    if (*((*(*gwpp[1])).n[1].pc + 2) == 39) count++; else fail("if_c004");
    if ((*(pickw(lwp, 0)->cp + 3))[0] == 109) count++; else fail("if_c005");
    if (pickn(pickw(gwp, 1), 0)->leaf[2].ca[1] == 121) count++; else fail("if_c006");
    if (pickl(&lw[1].n[1], 2)->cv == 60) count++; else fail("if_c007");
    if (*(&((&lw[0])->n[0].cm[1][2])) == 123) count++; else fail("if_c008");
    if (*(gc + 5) == 75) count++; else fail("if_c009");
    if (*((lc + 2) + 3) == 85) count++; else fail("if_c010");
    if ((*lwpp[0])->n[1].leaf[2].la[3] == 3000163L) count++; else fail("if_l001");
    if ((*(lwpp[1]))->pn[1].leaf[0].lv == 4000103L) count++; else fail("if_l002");
    if ((*(*gwpp[0])).n[0].pl->la[2] == 1000052L) count++; else fail("if_l003");
    if (*((*(*gwpp[1])).n[1].plong + 2) == 2000412L) count++; else fail("if_l004");
    if ((*(pickw(lwp, 0)->longp + 3))[0] == 3000143L) count++; else fail("if_l005");
    if (pickn(pickw(gwp, 1), 0)->leaf[2].la[1] == 2000061L) count++; else fail("if_l006");
    if (pickl(&lw[1].n[1], 2)->lv == 4000123L) count++; else fail("if_l007");
    if (*(&((&lw[0])->n[0].lm[1][2])) == 3000312L) count++; else fail("if_l008");
    if (*(gl + 5) == 600005L) count++; else fail("if_l009");
    if (*((ll + 2) + 3) == 800005L) count++; else fail("if_l010");
    if ((*(&wp))->n[1].leaf[1].a[3] == 3133 && (*(&wp))->n[1].leaf[1].la[3] == 3000153L) count++; else fail("log001");
    if ((*(&np))->leaf[2].ca[2] == 98 || (*(&np))->leaf[2].ca[2] == 0) count++; else fail("log002");
    if (!((*(&lp))->la[1] != 3000041L)) count++; else fail("log003");
    if (*(gi + 5) == 5005 && *(gc + 5) == 75 && *(gl + 5) == 600005L) count++; else fail("log004");
    if (*((li + 2) + 3) == 7005 && *((lc + 2) + 3) == 85 && *((ll + 2) + 3) == 800005L) count++; else fail("log005");
    if (((*(gwpp[0]))[0]).pn[0].leaf[2].a[0] == 1040) count++; else fail("log006");
    if (((*(gwpp[0]))[0]).pn[0].leaf[2].ca[0] == 16) count++; else fail("log007");
    if (((*(gwpp[0]))[0]).pn[0].leaf[2].la[0] == 1000060L) count++; else fail("log008");
    if ((*lwpp[0])->n[1].leaf[2].a[3] != 3143) fail("iff_i001"); else count++;
    if (pickn(pickw(gwp, 1), 0)->leaf[2].ca[1] < 0) fail("iff_c001"); else count++;
    if (pickl(&lw[1].n[1], 2)->lv != 4000123L) fail("iff_l001"); else count++;

    check_int("if_count", count, MIR_CLOBBER_IF_COUNT);

    i = 0;
    sum = 0;
    guard = 0;
    while ((i < 4) && (*(pickip(&lw[i & 1], i & 3)) >= 3000) &&
           (*(picklp(&lw[i & 1], i & 3)) >= 3000000L)) {
        sum += i;
        i++;
        guard++;
        if (guard > 10) {
            fail("while_guard1");
            break;
        }
    }
    check_int("while_i1", i, 4);
    check_int("while_sum1", sum, 6);

    i = 0;
    sum = 0;
    guard = 0;
    while ((i < 3) && ((*(&lwp[i & 1]))->n[i & 1].leaf[i].ca[i] != 0) &&
           ((*(&lwp[i & 1]))->n[i & 1].leaf[i].la[i] > 0L)) {
        sum += (*(&lwp[i & 1]))->n[i & 1].leaf[i].ca[i];
        i++;
        guard++;
        if (guard > 10) {
            fail("while_guard2");
            break;
        }
    }
    check_int("while_i2", i, 3);
    check_int("while_sum2", sum, 272);

    i = 0;
    guard = 0;
    while ((i < 2) || ((*lwpp[0])->n[1].leaf[2].a[3] != 3143)) {
        i++;
        guard++;
        if (guard > 10) {
            fail("while_guard3");
            break;
        }
    }
    check_int("while_i3", i, 2);

    i = 0;
    sum = 0;
    guard = 0;
    do {
        sum += i;
        i++;
        guard++;
        if (guard > 10) {
            fail("do_guard1");
            break;
        }
    } while ((i < 4) && (((*gwpp[i & 1])->n[i & 1].pl->a[i & 3]) > 0));
    check_int("do_i1", i, 4);
    check_int("do_sum1", sum, 6);

    i = 0;
    sum = 0;
    guard = 0;
    do {
        sum += (int)(*(((*lwpp[i & 1])->n[i & 1].pc) + (i % 3)));
        i++;
        guard++;
        if (guard > 10) {
            fail("do_guard2");
            break;
        }
    } while ((i < 3) && (*(((*lwpp[i & 1])->n[i & 1].plong) + (i % 3)) > 0L));
    check_int("do_i2", i, 3);
    check_int("do_sum2", sum, 362);

    sum = 0;
    for (i = 0; (i < 4) && (*(gi + i) >= 5000) && (*(gl + i) < 600010L); i++) {
        sum += i;
    }
    check_int("for_i1", i, 4);
    check_int("for_sum1", sum, 6);

    sum = 0;
    for (i = 0; (i < 3) && (((*lwpp[0])->n[0].leaf[i].v) < 3100); i++) {
        sum += (*lwpp[0])->n[0].leaf[i].a[i];
    }
    check_int("for_i2", i, 3);
    check_int("for_sum2", sum, 9093);

    sum = 0;
    for (i = 0; (i < 3) && (((*lwpp[1])->n[1].leaf[i].la[i]) > 0L); i++) {
        sum += (int)((*lwpp[1])->n[1].leaf[i].ca[i]);
    }
    check_int("for_i3", i, 3);
    check_int("for_sum3", sum, 264);

    if (((*lwpp[0])->n[1].leaf[2].a[3] == 3143) ?
            ((*lwpp[0])->n[1].leaf[2].la[3] == 3000163L) :
            ((*lwpp[0])->n[1].leaf[2].ca[3] == 0)) {
        count++;
    } else {
        fail("ternary_cond1");
    }

    if (((*lwpp[1])->n[0].leaf[1].ca[2] == 69) ?
            ((*lwpp[1])->n[0].leaf[1].a[2] == 4032) :
            ((*lwpp[1])->n[0].leaf[1].la[2] == 4000052L)) {
        count++;
    } else {
        fail("ternary_cond2");
    }

    if (((pickw(gwp, 1)->n[1].plong[2] == 2000412L) &&
         (pickw(gwp, 1)->n[1].pc[2] == 39)) ||
        ((pickw(gwp, 1)->n[1].pi[2] == 2312) &&
         (pickw(gwp, 1)->n[1].pl->lv == 2000113L))) {
        count++;
    } else {
        fail("nested_log1");
    }

    if (fails) {
        printf("FAILED %d\n", fails);
        return 1;
    }

    printf("PASS\n");
    return 0;
}
