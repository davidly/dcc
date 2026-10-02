/* tptrrhs.c - C89 regression test for complex pointer-expression rvalues.
 *
 * Intended for dcc / CP/M style targets: no stdlib dependency beyond printf.
 * Focus: assignments where the right hand side is reached through combinations
 * of struct fields, arrays, pointer arithmetic, globals, locals, and multiple
 * levels of indirection.
 *
 * Expected final output:
 * tptrrhs start
 * PASS
 */

#include <stdio.h>

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

static void check_char(name, got, exp)
char *name;
char got;
int exp;
{
    if ((int)got != exp) {
        printf("FAIL %s got %d expected %d\n", name, (int)got, exp);
        fails++;
    }
}

static void check_long(name, got, exp)
char *name;
long got;
long exp;
{
    if (got != exp) {
        printf("FAIL %s got %ld expected %ld\n", name, got, exp);
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
    int ri;
    char rc;
    long rl;
    int li[8];
    char lc[8];
    long ll[8];

    printf("tptrrhs start\n");

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

    for (ri = 0; ri < 8; ri++) {
        gi[ri] = 5000 + ri;
        gc[ri] = (char)(70 + ri);
        gl[ri] = 600000L + (long)ri;
        li[ri] = 7000 + ri;
        lc[ri] = (char)(80 + ri);
        ll[ri] = 800000L + (long)ri;
    }

    wp = &lw[0];
    np = &lw[0].n[0];
    lp = &lw[0].n[0].leaf[0];

    ri = (*lwpp[0])->n[1].leaf[2].a[3];
    check_int("i001", ri, 3143);

    ri = (*(lwpp[1]))->pn[1].leaf[0].v;
    check_int("i002", ri, 4101);

    ri = (*(*gwpp[0])).n[0].pl->a[2];
    check_int("i003", ri, 1032);

    ri = *((*(*gwpp[1])).n[1].pi + 2);
    check_int("i004", ri, 2312);

    ri = (*(pickw(lwp, 0)->ip + 3))[0];
    check_int("i005", ri, 3123);

    ri = pickn(pickw(gwp, 1), 0)->leaf[2].a[1];
    check_int("i006", ri, 2041);

    ri = pickl(&lw[1].n[1], 2)->v;
    check_int("i007", ri, 4121);

    ri = *(&((&lw[0])->n[0].m[1][2]));
    check_int("i008", ri, 3212);

    ri = *(gi + 5);
    check_int("i009", ri, 5005);

    ri = *((li + 2) + 3);
    check_int("i010", ri, 7005);

    ri = ((*(gwpp[0]))[0]).pn[0].leaf[2].a[0];
    check_int("i011", ri, 1040);

    ri = (*((*lwpp[1])->ip[2]));
    check_int("i012", ri, 4042);

    ri = (*(pickip(&lw[0], 1) + 0));
    check_int("i013", ri, 3131);

    ri = (*(&wp))->n[1].leaf[1].a[3];
    check_int("i014", ri, 3133);

    ri = (*(&np))->leaf[2].a[2];
    check_int("i015", ri, 3042);

    ri = (*(&lp))->a[1];
    check_int("i016", ri, 3021);

    rc = (*lwpp[0])->n[1].leaf[2].ca[3];
    check_char("c001", rc, 119);

    rc = (*(lwpp[1]))->pn[1].leaf[0].cv;
    check_char("c002", rc, 54);

    rc = (*(*gwpp[0])).n[0].pl->ca[2];
    check_char("c003", rc, 13);

    rc = *((*(*gwpp[1])).n[1].pc + 2);
    check_char("c004", rc, 39);

    rc = (*(pickw(lwp, 0)->cp + 3))[0];
    check_char("c005", rc, 109);

    rc = pickn(pickw(gwp, 1), 0)->leaf[2].ca[1];
    check_char("c006", rc, 121);

    rc = pickl(&lw[1].n[1], 2)->cv;
    check_char("c007", rc, 60);

    rc = *(&((&lw[0])->n[0].cm[1][2]));
    check_char("c008", rc, 123);

    rc = *(gc + 5);
    check_char("c009", rc, 75);

    rc = *((lc + 2) + 3);
    check_char("c010", rc, 85);

    rc = ((*(gwpp[0]))[0]).pn[0].leaf[2].ca[0];
    check_char("c011", rc, 16);

    rc = (*((*lwpp[1])->cp[2]));
    check_char("c012", rc, 74);

    rc = (*(pickcp(&lw[0], 1) + 0));
    check_char("c013", rc, 112);

    rc = (*(&wp))->n[1].leaf[1].ca[3];
    check_char("c014", rc, 114);

    rc = (*(&np))->leaf[2].ca[2];
    check_char("c015", rc, 98);

    rc = (*(&lp))->ca[1];
    check_char("c016", rc, 87);

    rl = (*lwpp[0])->n[1].leaf[2].la[3];
    check_long("l001", rl, 3000163L);

    rl = (*(lwpp[1]))->pn[1].leaf[0].lv;
    check_long("l002", rl, 4000103L);

    rl = (*(*gwpp[0])).n[0].pl->la[2];
    check_long("l003", rl, 1000052L);

    rl = *((*(*gwpp[1])).n[1].plong + 2);
    check_long("l004", rl, 2000412L);

    rl = (*(pickw(lwp, 0)->longp + 3))[0];
    check_long("l005", rl, 3000143L);

    rl = pickn(pickw(gwp, 1), 0)->leaf[2].la[1];
    check_long("l006", rl, 2000061L);

    rl = pickl(&lw[1].n[1], 2)->lv;
    check_long("l007", rl, 4000123L);

    rl = *(&((&lw[0])->n[0].lm[1][2]));
    check_long("l008", rl, 3000312L);

    rl = *(gl + 5);
    check_long("l009", rl, 600005L);

    rl = *((ll + 2) + 3);
    check_long("l010", rl, 800005L);

    rl = ((*(gwpp[0]))[0]).pn[0].leaf[2].la[0];
    check_long("l011", rl, 1000060L);

    rl = (*((*lwpp[1])->longp[2]));
    check_long("l012", rl, 4000062L);

    rl = (*(picklp(&lw[0], 1) + 0));
    check_long("l013", rl, 3000151L);

    rl = (*(&wp))->n[1].leaf[1].la[3];
    check_long("l014", rl, 3000153L);

    rl = (*(&np))->leaf[2].la[2];
    check_long("l015", rl, 3000062L);

    rl = (*(&lp))->la[1];
    check_long("l016", rl, 3000041L);

    ri = ((*gwpp[1])->n + 1)->leaf[2].a[1];
    check_int("i017", ri, 2141);

    rc = ((*gwpp[1])->n + 1)->leaf[2].ca[1];
    check_char("c017", rc, 13);

    rl = ((*gwpp[1])->n + 1)->leaf[2].la[1];
    check_long("l017", rl, 2000161L);

    ri = *((*gwpp[1])->n[1].pi + 0);
    check_int("im017", ri, 2310);

    rc = *((*gwpp[1])->n[1].pc + 0);
    check_char("cm017", rc, 37);

    rl = *((*gwpp[1])->n[1].plong + 0);
    check_long("lm017", rl, 2000410L);

    ri = ((*lwpp[0])->n + 0)->leaf[0].a[2];
    check_int("i018", ri, 3022);

    rc = ((*lwpp[0])->n + 0)->leaf[0].ca[2];
    check_char("c018", rc, 88);

    rl = ((*lwpp[0])->n + 0)->leaf[0].la[2];
    check_long("l018", rl, 3000042L);

    ri = *((*lwpp[0])->n[0].pi + 0);
    check_int("im018", ri, 3210);

    rc = *((*lwpp[0])->n[0].pc + 0);
    check_char("cm018", rc, 121);

    rl = *((*lwpp[0])->n[0].plong + 0);
    check_long("lm018", rl, 3000310L);

    ri = ((*lwpp[1])->n + 1)->leaf[0].a[2];
    check_int("i019", ri, 4122);

    rc = ((*lwpp[1])->n + 1)->leaf[0].ca[2];
    check_char("c019", rc, 84);

    rl = ((*lwpp[1])->n + 1)->leaf[0].la[2];
    check_long("l019", rl, 4000142L);

    ri = *((*lwpp[1])->n[1].pi + 0);
    check_int("im019", ri, 4310);

    rc = *((*lwpp[1])->n[1].pc + 0);
    check_char("cm019", rc, 117);

    rl = *((*lwpp[1])->n[1].plong + 0);
    check_long("lm019", rl, 4000410L);

    ri = ((*gwpp[0])->n + 0)->leaf[1].a[2];
    check_int("i020", ri, 1032);

    rc = ((*gwpp[0])->n + 0)->leaf[1].ca[2];
    check_char("c020", rc, 13);

    rl = ((*gwpp[0])->n + 0)->leaf[1].la[2];
    check_long("l020", rl, 1000052L);

    ri = *((*gwpp[0])->n[0].pi + 1);
    check_int("im020", ri, 1211);

    rc = *((*gwpp[0])->n[0].pc + 1);
    check_char("cm020", rc, 42);

    rl = *((*gwpp[0])->n[0].plong + 1);
    check_long("lm020", rl, 1000311L);

    ri = ((*gwpp[1])->n + 1)->leaf[1].a[3];
    check_int("i021", ri, 2133);

    rc = ((*gwpp[1])->n + 1)->leaf[1].ca[3];
    check_char("c021", rc, 10);

    rl = ((*gwpp[1])->n + 1)->leaf[1].la[3];
    check_long("l021", rl, 2000153L);

    ri = *((*gwpp[1])->n[1].pi + 1);
    check_int("im021", ri, 2311);

    rc = *((*gwpp[1])->n[1].pc + 1);
    check_char("cm021", rc, 38);

    rl = *((*gwpp[1])->n[1].plong + 1);
    check_long("lm021", rl, 2000411L);

    ri = ((*lwpp[0])->n + 0)->leaf[2].a[3];
    check_int("i022", ri, 3043);

    rc = ((*lwpp[0])->n + 0)->leaf[2].ca[3];
    check_char("c022", rc, 99);

    rl = ((*lwpp[0])->n + 0)->leaf[2].la[3];
    check_long("l022", rl, 3000063L);

    ri = *((*lwpp[0])->n[0].pi + 1);
    check_int("im022", ri, 3211);

    rc = *((*lwpp[0])->n[0].pc + 1);
    check_char("cm022", rc, 122);

    rl = *((*lwpp[0])->n[0].plong + 1);
    check_long("lm022", rl, 3000311L);

    ri = ((*lwpp[1])->n + 1)->leaf[2].a[3];
    check_int("i023", ri, 4143);

    rc = ((*lwpp[1])->n + 1)->leaf[2].ca[3];
    check_char("c023", rc, 95);

    rl = ((*lwpp[1])->n + 1)->leaf[2].la[3];
    check_long("l023", rl, 4000163L);

    ri = *((*lwpp[1])->n[1].pi + 1);
    check_int("im023", ri, 4311);

    rc = *((*lwpp[1])->n[1].pc + 1);
    check_char("cm023", rc, 118);

    rl = *((*lwpp[1])->n[1].plong + 1);
    check_long("lm023", rl, 4000411L);

    ri = ((*gwpp[0])->n + 0)->leaf[0].a[0];
    check_int("i024", ri, 1020);

    rc = ((*gwpp[0])->n + 0)->leaf[0].ca[0];
    check_char("c024", rc, 6);

    rl = ((*gwpp[0])->n + 0)->leaf[0].la[0];
    check_long("l024", rl, 1000040L);

    ri = *((*gwpp[0])->n[0].pi + 1);
    check_int("im024", ri, 1211);

    rc = *((*gwpp[0])->n[0].pc + 1);
    check_char("cm024", rc, 42);

    rl = *((*gwpp[0])->n[0].plong + 1);
    check_long("lm024", rl, 1000311L);

    ri = ((*gwpp[1])->n + 1)->leaf[0].a[0];
    check_int("i025", ri, 2120);

    rc = ((*gwpp[1])->n + 1)->leaf[0].ca[0];
    check_char("c025", rc, 2);

    rl = ((*gwpp[1])->n + 1)->leaf[0].la[0];
    check_long("l025", rl, 2000140L);

    ri = *((*gwpp[1])->n[1].pi + 2);
    check_int("im025", ri, 2312);

    rc = *((*gwpp[1])->n[1].pc + 2);
    check_char("cm025", rc, 39);

    rl = *((*gwpp[1])->n[1].plong + 2);
    check_long("lm025", rl, 2000412L);

    ri = ((*lwpp[0])->n + 0)->leaf[1].a[0];
    check_int("i026", ri, 3030);

    rc = ((*lwpp[0])->n + 0)->leaf[1].ca[0];
    check_char("c026", rc, 91);

    rl = ((*lwpp[0])->n + 0)->leaf[1].la[0];
    check_long("l026", rl, 3000050L);

    ri = *((*lwpp[0])->n[0].pi + 2);
    check_int("im026", ri, 3212);

    rc = *((*lwpp[0])->n[0].pc + 2);
    check_char("cm026", rc, 123);

    rl = *((*lwpp[0])->n[0].plong + 2);
    check_long("lm026", rl, 3000312L);

    ri = ((*lwpp[1])->n + 1)->leaf[1].a[1];
    check_int("i027", ri, 4131);

    rc = ((*lwpp[1])->n + 1)->leaf[1].ca[1];
    check_char("c027", rc, 88);

    rl = ((*lwpp[1])->n + 1)->leaf[1].la[1];
    check_long("l027", rl, 4000151L);

    ri = *((*lwpp[1])->n[1].pi + 2);
    check_int("im027", ri, 4312);

    rc = *((*lwpp[1])->n[1].pc + 2);
    check_char("cm027", rc, 119);

    rl = *((*lwpp[1])->n[1].plong + 2);
    check_long("lm027", rl, 4000412L);

    ri = ((*gwpp[0])->n + 0)->leaf[2].a[1];
    check_int("i028", ri, 1041);

    rc = ((*gwpp[0])->n + 0)->leaf[2].ca[1];
    check_char("c028", rc, 17);

    rl = ((*gwpp[0])->n + 0)->leaf[2].la[1];
    check_long("l028", rl, 1000061L);

    ri = *((*gwpp[0])->n[0].pi + 2);
    check_int("im028", ri, 1212);

    rc = *((*gwpp[0])->n[0].pc + 2);
    check_char("cm028", rc, 43);

    rl = *((*gwpp[0])->n[0].plong + 2);
    check_long("lm028", rl, 1000312L);

    ri = ((*gwpp[1])->n + 1)->leaf[2].a[1];
    check_int("i029", ri, 2141);

    rc = ((*gwpp[1])->n + 1)->leaf[2].ca[1];
    check_char("c029", rc, 13);

    rl = ((*gwpp[1])->n + 1)->leaf[2].la[1];
    check_long("l029", rl, 2000161L);

    ri = *((*gwpp[1])->n[1].pi + 2);
    check_int("im029", ri, 2312);

    rc = *((*gwpp[1])->n[1].pc + 2);
    check_char("cm029", rc, 39);

    rl = *((*gwpp[1])->n[1].plong + 2);
    check_long("lm029", rl, 2000412L);

    ri = ((*lwpp[0])->n + 0)->leaf[0].a[2];
    check_int("i030", ri, 3022);

    rc = ((*lwpp[0])->n + 0)->leaf[0].ca[2];
    check_char("c030", rc, 88);

    rl = ((*lwpp[0])->n + 0)->leaf[0].la[2];
    check_long("l030", rl, 3000042L);

    ri = *((*lwpp[0])->n[0].pi + 0);
    check_int("im030", ri, 3210);

    rc = *((*lwpp[0])->n[0].pc + 0);
    check_char("cm030", rc, 121);

    rl = *((*lwpp[0])->n[0].plong + 0);
    check_long("lm030", rl, 3000310L);

    ri = ((*lwpp[1])->n + 1)->leaf[0].a[2];
    check_int("i031", ri, 4122);

    rc = ((*lwpp[1])->n + 1)->leaf[0].ca[2];
    check_char("c031", rc, 84);

    rl = ((*lwpp[1])->n + 1)->leaf[0].la[2];
    check_long("l031", rl, 4000142L);

    ri = *((*lwpp[1])->n[1].pi + 0);
    check_int("im031", ri, 4310);

    rc = *((*lwpp[1])->n[1].pc + 0);
    check_char("cm031", rc, 117);

    rl = *((*lwpp[1])->n[1].plong + 0);
    check_long("lm031", rl, 4000410L);

    ri = ((*gwpp[0])->n + 0)->leaf[1].a[2];
    check_int("i032", ri, 1032);

    rc = ((*gwpp[0])->n + 0)->leaf[1].ca[2];
    check_char("c032", rc, 13);

    rl = ((*gwpp[0])->n + 0)->leaf[1].la[2];
    check_long("l032", rl, 1000052L);

    ri = *((*gwpp[0])->n[0].pi + 0);
    check_int("im032", ri, 1210);

    rc = *((*gwpp[0])->n[0].pc + 0);
    check_char("cm032", rc, 41);

    rl = *((*gwpp[0])->n[0].plong + 0);
    check_long("lm032", rl, 1000310L);

    ri = ((*gwpp[1])->n + 1)->leaf[1].a[3];
    check_int("i033", ri, 2133);

    rc = ((*gwpp[1])->n + 1)->leaf[1].ca[3];
    check_char("c033", rc, 10);

    rl = ((*gwpp[1])->n + 1)->leaf[1].la[3];
    check_long("l033", rl, 2000153L);

    ri = *((*gwpp[1])->n[1].pi + 0);
    check_int("im033", ri, 2310);

    rc = *((*gwpp[1])->n[1].pc + 0);
    check_char("cm033", rc, 37);

    rl = *((*gwpp[1])->n[1].plong + 0);
    check_long("lm033", rl, 2000410L);

    ri = ((*lwpp[0])->n + 0)->leaf[2].a[3];
    check_int("i034", ri, 3043);

    rc = ((*lwpp[0])->n + 0)->leaf[2].ca[3];
    check_char("c034", rc, 99);

    rl = ((*lwpp[0])->n + 0)->leaf[2].la[3];
    check_long("l034", rl, 3000063L);

    ri = *((*lwpp[0])->n[0].pi + 0);
    check_int("im034", ri, 3210);

    rc = *((*lwpp[0])->n[0].pc + 0);
    check_char("cm034", rc, 121);

    rl = *((*lwpp[0])->n[0].plong + 0);
    check_long("lm034", rl, 3000310L);

    ri = ((*lwpp[1])->n + 1)->leaf[2].a[3];
    check_int("i035", ri, 4143);

    rc = ((*lwpp[1])->n + 1)->leaf[2].ca[3];
    check_char("c035", rc, 95);

    rl = ((*lwpp[1])->n + 1)->leaf[2].la[3];
    check_long("l035", rl, 4000163L);

    ri = *((*lwpp[1])->n[1].pi + 1);
    check_int("im035", ri, 4311);

    rc = *((*lwpp[1])->n[1].pc + 1);
    check_char("cm035", rc, 118);

    rl = *((*lwpp[1])->n[1].plong + 1);
    check_long("lm035", rl, 4000411L);

    ri = ((*gwpp[0])->n + 0)->leaf[0].a[0];
    check_int("i036", ri, 1020);

    rc = ((*gwpp[0])->n + 0)->leaf[0].ca[0];
    check_char("c036", rc, 6);

    rl = ((*gwpp[0])->n + 0)->leaf[0].la[0];
    check_long("l036", rl, 1000040L);

    ri = *((*gwpp[0])->n[0].pi + 1);
    check_int("im036", ri, 1211);

    rc = *((*gwpp[0])->n[0].pc + 1);
    check_char("cm036", rc, 42);

    rl = *((*gwpp[0])->n[0].plong + 1);
    check_long("lm036", rl, 1000311L);

    ri = ((*gwpp[1])->n + 1)->leaf[0].a[0];
    check_int("i037", ri, 2120);

    rc = ((*gwpp[1])->n + 1)->leaf[0].ca[0];
    check_char("c037", rc, 2);

    rl = ((*gwpp[1])->n + 1)->leaf[0].la[0];
    check_long("l037", rl, 2000140L);

    ri = *((*gwpp[1])->n[1].pi + 1);
    check_int("im037", ri, 2311);

    rc = *((*gwpp[1])->n[1].pc + 1);
    check_char("cm037", rc, 38);

    rl = *((*gwpp[1])->n[1].plong + 1);
    check_long("lm037", rl, 2000411L);

    ri = ((*lwpp[0])->n + 0)->leaf[1].a[0];
    check_int("i038", ri, 3030);

    rc = ((*lwpp[0])->n + 0)->leaf[1].ca[0];
    check_char("c038", rc, 91);

    rl = ((*lwpp[0])->n + 0)->leaf[1].la[0];
    check_long("l038", rl, 3000050L);

    ri = *((*lwpp[0])->n[0].pi + 1);
    check_int("im038", ri, 3211);

    rc = *((*lwpp[0])->n[0].pc + 1);
    check_char("cm038", rc, 122);

    rl = *((*lwpp[0])->n[0].plong + 1);
    check_long("lm038", rl, 3000311L);

    ri = ((*lwpp[1])->n + 1)->leaf[1].a[1];
    check_int("i039", ri, 4131);

    rc = ((*lwpp[1])->n + 1)->leaf[1].ca[1];
    check_char("c039", rc, 88);

    rl = ((*lwpp[1])->n + 1)->leaf[1].la[1];
    check_long("l039", rl, 4000151L);

    ri = *((*lwpp[1])->n[1].pi + 1);
    check_int("im039", ri, 4311);

    rc = *((*lwpp[1])->n[1].pc + 1);
    check_char("cm039", rc, 118);

    rl = *((*lwpp[1])->n[1].plong + 1);
    check_long("lm039", rl, 4000411L);

    ri = ((*gwpp[0])->n + 0)->leaf[2].a[1];
    check_int("i040", ri, 1041);

    rc = ((*gwpp[0])->n + 0)->leaf[2].ca[1];
    check_char("c040", rc, 17);

    rl = ((*gwpp[0])->n + 0)->leaf[2].la[1];
    check_long("l040", rl, 1000061L);

    ri = *((*gwpp[0])->n[0].pi + 2);
    check_int("im040", ri, 1212);

    rc = *((*gwpp[0])->n[0].pc + 2);
    check_char("cm040", rc, 43);

    rl = *((*gwpp[0])->n[0].plong + 2);
    check_long("lm040", rl, 1000312L);

    ri = ((*gwpp[1])->n + 1)->leaf[2].a[1];
    check_int("i041", ri, 2141);

    rc = ((*gwpp[1])->n + 1)->leaf[2].ca[1];
    check_char("c041", rc, 13);

    rl = ((*gwpp[1])->n + 1)->leaf[2].la[1];
    check_long("l041", rl, 2000161L);

    ri = *((*gwpp[1])->n[1].pi + 2);
    check_int("im041", ri, 2312);

    rc = *((*gwpp[1])->n[1].pc + 2);
    check_char("cm041", rc, 39);

    rl = *((*gwpp[1])->n[1].plong + 2);
    check_long("lm041", rl, 2000412L);

    ri = ((*lwpp[0])->n + 0)->leaf[0].a[2];
    check_int("i042", ri, 3022);

    rc = ((*lwpp[0])->n + 0)->leaf[0].ca[2];
    check_char("c042", rc, 88);

    rl = ((*lwpp[0])->n + 0)->leaf[0].la[2];
    check_long("l042", rl, 3000042L);

    ri = *((*lwpp[0])->n[0].pi + 2);
    check_int("im042", ri, 3212);

    rc = *((*lwpp[0])->n[0].pc + 2);
    check_char("cm042", rc, 123);

    rl = *((*lwpp[0])->n[0].plong + 2);
    check_long("lm042", rl, 3000312L);

    ri = ((*lwpp[1])->n + 1)->leaf[0].a[2];
    check_int("i043", ri, 4122);

    rc = ((*lwpp[1])->n + 1)->leaf[0].ca[2];
    check_char("c043", rc, 84);

    rl = ((*lwpp[1])->n + 1)->leaf[0].la[2];
    check_long("l043", rl, 4000142L);

    ri = *((*lwpp[1])->n[1].pi + 2);
    check_int("im043", ri, 4312);

    rc = *((*lwpp[1])->n[1].pc + 2);
    check_char("cm043", rc, 119);

    rl = *((*lwpp[1])->n[1].plong + 2);
    check_long("lm043", rl, 4000412L);

    ri = ((*gwpp[0])->n + 0)->leaf[1].a[2];
    check_int("i044", ri, 1032);

    rc = ((*gwpp[0])->n + 0)->leaf[1].ca[2];
    check_char("c044", rc, 13);

    rl = ((*gwpp[0])->n + 0)->leaf[1].la[2];
    check_long("l044", rl, 1000052L);

    ri = *((*gwpp[0])->n[0].pi + 2);
    check_int("im044", ri, 1212);

    rc = *((*gwpp[0])->n[0].pc + 2);
    check_char("cm044", rc, 43);

    rl = *((*gwpp[0])->n[0].plong + 2);
    check_long("lm044", rl, 1000312L);

    ri = ((*gwpp[1])->n + 1)->leaf[1].a[3];
    check_int("i045", ri, 2133);

    rc = ((*gwpp[1])->n + 1)->leaf[1].ca[3];
    check_char("c045", rc, 10);

    rl = ((*gwpp[1])->n + 1)->leaf[1].la[3];
    check_long("l045", rl, 2000153L);

    ri = *((*gwpp[1])->n[1].pi + 0);
    check_int("im045", ri, 2310);

    rc = *((*gwpp[1])->n[1].pc + 0);
    check_char("cm045", rc, 37);

    rl = *((*gwpp[1])->n[1].plong + 0);
    check_long("lm045", rl, 2000410L);

    ri = ((*lwpp[0])->n + 0)->leaf[2].a[3];
    check_int("i046", ri, 3043);

    rc = ((*lwpp[0])->n + 0)->leaf[2].ca[3];
    check_char("c046", rc, 99);

    rl = ((*lwpp[0])->n + 0)->leaf[2].la[3];
    check_long("l046", rl, 3000063L);

    ri = *((*lwpp[0])->n[0].pi + 0);
    check_int("im046", ri, 3210);

    rc = *((*lwpp[0])->n[0].pc + 0);
    check_char("cm046", rc, 121);

    rl = *((*lwpp[0])->n[0].plong + 0);
    check_long("lm046", rl, 3000310L);

    ri = ((*lwpp[1])->n + 1)->leaf[2].a[3];
    check_int("i047", ri, 4143);

    rc = ((*lwpp[1])->n + 1)->leaf[2].ca[3];
    check_char("c047", rc, 95);

    rl = ((*lwpp[1])->n + 1)->leaf[2].la[3];
    check_long("l047", rl, 4000163L);

    ri = *((*lwpp[1])->n[1].pi + 0);
    check_int("im047", ri, 4310);

    rc = *((*lwpp[1])->n[1].pc + 0);
    check_char("cm047", rc, 117);

    rl = *((*lwpp[1])->n[1].plong + 0);
    check_long("lm047", rl, 4000410L);

    ri = ((*gwpp[0])->n + 0)->leaf[0].a[0];
    check_int("i048", ri, 1020);

    rc = ((*gwpp[0])->n + 0)->leaf[0].ca[0];
    check_char("c048", rc, 6);

    rl = ((*gwpp[0])->n + 0)->leaf[0].la[0];
    check_long("l048", rl, 1000040L);

    ri = *((*gwpp[0])->n[0].pi + 0);
    check_int("im048", ri, 1210);

    rc = *((*gwpp[0])->n[0].pc + 0);
    check_char("cm048", rc, 41);

    rl = *((*gwpp[0])->n[0].plong + 0);
    check_long("lm048", rl, 1000310L);

    ri = ((*gwpp[1])->n + 1)->leaf[0].a[0];
    check_int("i049", ri, 2120);

    rc = ((*gwpp[1])->n + 1)->leaf[0].ca[0];
    check_char("c049", rc, 2);

    rl = ((*gwpp[1])->n + 1)->leaf[0].la[0];
    check_long("l049", rl, 2000140L);

    ri = *((*gwpp[1])->n[1].pi + 0);
    check_int("im049", ri, 2310);

    rc = *((*gwpp[1])->n[1].pc + 0);
    check_char("cm049", rc, 37);

    rl = *((*gwpp[1])->n[1].plong + 0);
    check_long("lm049", rl, 2000410L);

    ri = ((*lwpp[0])->n + 0)->leaf[1].a[0];
    check_int("i050", ri, 3030);

    rc = ((*lwpp[0])->n + 0)->leaf[1].ca[0];
    check_char("c050", rc, 91);

    rl = ((*lwpp[0])->n + 0)->leaf[1].la[0];
    check_long("l050", rl, 3000050L);

    ri = *((*lwpp[0])->n[0].pi + 1);
    check_int("im050", ri, 3211);

    rc = *((*lwpp[0])->n[0].pc + 1);
    check_char("cm050", rc, 122);

    rl = *((*lwpp[0])->n[0].plong + 1);
    check_long("lm050", rl, 3000311L);

    ri = ((*lwpp[1])->n + 1)->leaf[1].a[1];
    check_int("i051", ri, 4131);

    rc = ((*lwpp[1])->n + 1)->leaf[1].ca[1];
    check_char("c051", rc, 88);

    rl = ((*lwpp[1])->n + 1)->leaf[1].la[1];
    check_long("l051", rl, 4000151L);

    ri = *((*lwpp[1])->n[1].pi + 1);
    check_int("im051", ri, 4311);

    rc = *((*lwpp[1])->n[1].pc + 1);
    check_char("cm051", rc, 118);

    rl = *((*lwpp[1])->n[1].plong + 1);
    check_long("lm051", rl, 4000411L);

    ri = ((*gwpp[0])->n + 0)->leaf[2].a[1];
    check_int("i052", ri, 1041);

    rc = ((*gwpp[0])->n + 0)->leaf[2].ca[1];
    check_char("c052", rc, 17);

    rl = ((*gwpp[0])->n + 0)->leaf[2].la[1];
    check_long("l052", rl, 1000061L);

    ri = *((*gwpp[0])->n[0].pi + 1);
    check_int("im052", ri, 1211);

    rc = *((*gwpp[0])->n[0].pc + 1);
    check_char("cm052", rc, 42);

    rl = *((*gwpp[0])->n[0].plong + 1);
    check_long("lm052", rl, 1000311L);

    ri = ((*gwpp[1])->n + 1)->leaf[2].a[1];
    check_int("i053", ri, 2141);

    rc = ((*gwpp[1])->n + 1)->leaf[2].ca[1];
    check_char("c053", rc, 13);

    rl = ((*gwpp[1])->n + 1)->leaf[2].la[1];
    check_long("l053", rl, 2000161L);

    ri = *((*gwpp[1])->n[1].pi + 1);
    check_int("im053", ri, 2311);

    rc = *((*gwpp[1])->n[1].pc + 1);
    check_char("cm053", rc, 38);

    rl = *((*gwpp[1])->n[1].plong + 1);
    check_long("lm053", rl, 2000411L);

    ri = ((*lwpp[0])->n + 0)->leaf[0].a[2];
    check_int("i054", ri, 3022);

    rc = ((*lwpp[0])->n + 0)->leaf[0].ca[2];
    check_char("c054", rc, 88);

    rl = ((*lwpp[0])->n + 0)->leaf[0].la[2];
    check_long("l054", rl, 3000042L);

    ri = *((*lwpp[0])->n[0].pi + 1);
    check_int("im054", ri, 3211);

    rc = *((*lwpp[0])->n[0].pc + 1);
    check_char("cm054", rc, 122);

    rl = *((*lwpp[0])->n[0].plong + 1);
    check_long("lm054", rl, 3000311L);

    ri = ((*lwpp[1])->n + 1)->leaf[0].a[2];
    check_int("i055", ri, 4122);

    rc = ((*lwpp[1])->n + 1)->leaf[0].ca[2];
    check_char("c055", rc, 84);

    rl = ((*lwpp[1])->n + 1)->leaf[0].la[2];
    check_long("l055", rl, 4000142L);

    ri = *((*lwpp[1])->n[1].pi + 2);
    check_int("im055", ri, 4312);

    rc = *((*lwpp[1])->n[1].pc + 2);
    check_char("cm055", rc, 119);

    rl = *((*lwpp[1])->n[1].plong + 2);
    check_long("lm055", rl, 4000412L);

    ri = ((*gwpp[0])->n + 0)->leaf[1].a[2];
    check_int("i056", ri, 1032);

    rc = ((*gwpp[0])->n + 0)->leaf[1].ca[2];
    check_char("c056", rc, 13);

    rl = ((*gwpp[0])->n + 0)->leaf[1].la[2];
    check_long("l056", rl, 1000052L);

    ri = *((*gwpp[0])->n[0].pi + 2);
    check_int("im056", ri, 1212);

    rc = *((*gwpp[0])->n[0].pc + 2);
    check_char("cm056", rc, 43);

    rl = *((*gwpp[0])->n[0].plong + 2);
    check_long("lm056", rl, 1000312L);

    ri = ((*gwpp[1])->n + 1)->leaf[1].a[3];
    check_int("i057", ri, 2133);

    rc = ((*gwpp[1])->n + 1)->leaf[1].ca[3];
    check_char("c057", rc, 10);

    rl = ((*gwpp[1])->n + 1)->leaf[1].la[3];
    check_long("l057", rl, 2000153L);

    ri = *((*gwpp[1])->n[1].pi + 2);
    check_int("im057", ri, 2312);

    rc = *((*gwpp[1])->n[1].pc + 2);
    check_char("cm057", rc, 39);

    rl = *((*gwpp[1])->n[1].plong + 2);
    check_long("lm057", rl, 2000412L);

    ri = ((*lwpp[0])->n + 0)->leaf[2].a[3];
    check_int("i058", ri, 3043);

    rc = ((*lwpp[0])->n + 0)->leaf[2].ca[3];
    check_char("c058", rc, 99);

    rl = ((*lwpp[0])->n + 0)->leaf[2].la[3];
    check_long("l058", rl, 3000063L);

    ri = *((*lwpp[0])->n[0].pi + 2);
    check_int("im058", ri, 3212);

    rc = *((*lwpp[0])->n[0].pc + 2);
    check_char("cm058", rc, 123);

    rl = *((*lwpp[0])->n[0].plong + 2);
    check_long("lm058", rl, 3000312L);

    ri = ((*lwpp[1])->n + 1)->leaf[2].a[3];
    check_int("i059", ri, 4143);

    rc = ((*lwpp[1])->n + 1)->leaf[2].ca[3];
    check_char("c059", rc, 95);

    rl = ((*lwpp[1])->n + 1)->leaf[2].la[3];
    check_long("l059", rl, 4000163L);

    ri = *((*lwpp[1])->n[1].pi + 2);
    check_int("im059", ri, 4312);

    rc = *((*lwpp[1])->n[1].pc + 2);
    check_char("cm059", rc, 119);

    rl = *((*lwpp[1])->n[1].plong + 2);
    check_long("lm059", rl, 4000412L);

    ri = ((*gwpp[0])->n + 0)->leaf[0].a[0];
    check_int("i060", ri, 1020);

    rc = ((*gwpp[0])->n + 0)->leaf[0].ca[0];
    check_char("c060", rc, 6);

    rl = ((*gwpp[0])->n + 0)->leaf[0].la[0];
    check_long("l060", rl, 1000040L);

    ri = *((*gwpp[0])->n[0].pi + 0);
    check_int("im060", ri, 1210);

    rc = *((*gwpp[0])->n[0].pc + 0);
    check_char("cm060", rc, 41);

    rl = *((*gwpp[0])->n[0].plong + 0);
    check_long("lm060", rl, 1000310L);

    ri = ((*gwpp[1])->n + 1)->leaf[0].a[0];
    check_int("i061", ri, 2120);

    rc = ((*gwpp[1])->n + 1)->leaf[0].ca[0];
    check_char("c061", rc, 2);

    rl = ((*gwpp[1])->n + 1)->leaf[0].la[0];
    check_long("l061", rl, 2000140L);

    ri = *((*gwpp[1])->n[1].pi + 0);
    check_int("im061", ri, 2310);

    rc = *((*gwpp[1])->n[1].pc + 0);
    check_char("cm061", rc, 37);

    rl = *((*gwpp[1])->n[1].plong + 0);
    check_long("lm061", rl, 2000410L);

    ri = ((*lwpp[0])->n + 0)->leaf[1].a[0];
    check_int("i062", ri, 3030);

    rc = ((*lwpp[0])->n + 0)->leaf[1].ca[0];
    check_char("c062", rc, 91);

    rl = ((*lwpp[0])->n + 0)->leaf[1].la[0];
    check_long("l062", rl, 3000050L);

    ri = *((*lwpp[0])->n[0].pi + 0);
    check_int("im062", ri, 3210);

    rc = *((*lwpp[0])->n[0].pc + 0);
    check_char("cm062", rc, 121);

    rl = *((*lwpp[0])->n[0].plong + 0);
    check_long("lm062", rl, 3000310L);

    ri = ((*lwpp[1])->n + 1)->leaf[1].a[1];
    check_int("i063", ri, 4131);

    rc = ((*lwpp[1])->n + 1)->leaf[1].ca[1];
    check_char("c063", rc, 88);

    rl = ((*lwpp[1])->n + 1)->leaf[1].la[1];
    check_long("l063", rl, 4000151L);

    ri = *((*lwpp[1])->n[1].pi + 0);
    check_int("im063", ri, 4310);

    rc = *((*lwpp[1])->n[1].pc + 0);
    check_char("cm063", rc, 117);

    rl = *((*lwpp[1])->n[1].plong + 0);
    check_long("lm063", rl, 4000410L);

    ri = ((*gwpp[0])->n + 0)->leaf[2].a[1];
    check_int("i064", ri, 1041);

    rc = ((*gwpp[0])->n + 0)->leaf[2].ca[1];
    check_char("c064", rc, 17);

    rl = ((*gwpp[0])->n + 0)->leaf[2].la[1];
    check_long("l064", rl, 1000061L);

    ri = *((*gwpp[0])->n[0].pi + 0);
    check_int("im064", ri, 1210);

    rc = *((*gwpp[0])->n[0].pc + 0);
    check_char("cm064", rc, 41);

    rl = *((*gwpp[0])->n[0].plong + 0);
    check_long("lm064", rl, 1000310L);

    /* Direct identifier-path access to 2D field arrays, e.g. gw[i].n[j].cm[r][c].
     * Tests that val_type is correctly decayed after consuming all dimensions of a
     * 1D field array (n[]) so subsequent 2D field array access uses the right type.
     * Without the fix, the load uses char* or long* instead of char/long,
     * producing wrong-width loads. */
    ri = gw[0].n[0].m[1][2];
    check_int("id2d_i1", ri, 1212);
    rc = gw[0].n[0].cm[1][2];
    check_char("id2d_c1", rc, 43);
    rl = gw[0].n[0].lm[1][2];
    check_long("id2d_l1", rl, 1000312L);

    ri = gw[1].n[1].m[0][1];
    check_int("id2d_i2", ri, 2301);
    rc = gw[1].n[1].cm[0][1];
    check_char("id2d_c2", rc, 33);
    rl = gw[1].n[1].lm[0][1];
    check_long("id2d_l2", rl, 2000401L);

    ri = lw[0].n[0].m[1][2];
    check_int("id2d_i3", ri, 3212);
    rc = lw[0].n[0].cm[1][2];
    check_char("id2d_c3", rc, 123);
    rl = lw[0].n[0].lm[1][2];
    check_long("id2d_l3", rl, 3000312L);

    ri = lw[1].n[1].m[0][1];
    check_int("id2d_i4", ri, 4301);
    rc = lw[1].n[1].cm[0][1];
    check_char("id2d_c4", rc, 113);
    rl = lw[1].n[1].lm[0][1];
    check_long("id2d_l4", rl, 4000401L);

    if (fails) {
        printf("FAILED %d\n", fails);
        return 1;
    }

    printf("PASS\n");
    return 0;
}
