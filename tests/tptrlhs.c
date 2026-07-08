/* tptrlhs.c - C89 regression test for complex pointer-expression lvalues.
 *
 * Intended for dcc / CP/M style targets: no stdlib dependency beyond printf.
 * Focus: assignments where the left hand side is reached through combinations
 * of struct fields, arrays, pointer arithmetic, globals, locals, and multiple
 * levels of indirection.
 *
 * Expected final output:
 * tptrlhs start
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
    long *lop[4];
};

static struct Leaf gleaf[5];
static struct Node gnode[3];
static struct Wrapper gwrap[2];
static int gint[16];
static char gchar[16];
static long glong[16];
static int gmat[3][4];
static char gcmat[3][4];
static long glmat[3][4];
static struct Wrapper *gpwrap;
static struct Node *gpnode;
static struct Leaf *gpleaf;
static int *gpint;
static char *gpchar;
static long *gplong;

static int fails;

static void check_int(name, got, exp)
char *name;
int got;
int exp;
{
    if (got != exp) {
        printf("FAIL %s got %d expected %d\n", name, got, exp);
        fails = fails + 1;
    }
}


static void check_char(name, got, exp)
char *name;
char got;
int exp;
{
    if ((int)got != exp) {
        printf("FAIL %s got %d expected %d\n", name, (int)got, exp);
        fails = fails + 1;
    }
}

static void check_long(name, got, exp)
char *name;
long got;
long exp;
{
    if (got != exp) {
        printf("FAIL %s got %ld expected %ld\n", name, got, exp);
        fails = fails + 1;
    }
}

static struct Leaf make_leaf(seed)
int seed;
{
    struct Leaf leaf;
    int i;

    leaf.v = seed;
    leaf.cv = (char)(seed + 1);
    leaf.lv = (long)seed * 1000L;
    for (i = 0; i < 4; ++i) {
        leaf.a[i] = seed + i;
        leaf.ca[i] = (char)(seed + 10 + i);
        leaf.la[i] = (long)seed * 1000L + (long)i;
    }
    return leaf;
}

static void init_all()
{
    int i, j, k;

    for (i = 0; i < 5; ++i) {
        gleaf[i].v = 0;
        gleaf[i].cv = 0;
        gleaf[i].lv = 0L;
        for (j = 0; j < 4; ++j) {
            gleaf[i].a[j] = 0;
            gleaf[i].ca[j] = 0;
            gleaf[i].la[j] = 0L;
        }
    }

    for (i = 0; i < 3; ++i) {
        gnode[i].pl = &gleaf[i];
        gnode[i].pi = &gint[i * 2];
        gnode[i].pc = &gchar[i * 2];
        gnode[i].plong = &glong[i * 2];
        for (j = 0; j < 3; ++j) {
            gnode[i].leaf[j].v = 0;
            gnode[i].leaf[j].cv = 0;
            gnode[i].leaf[j].lv = 0L;
            for (k = 0; k < 4; ++k) {
                gnode[i].leaf[j].a[k] = 0;
                gnode[i].leaf[j].ca[k] = 0;
                gnode[i].leaf[j].la[k] = 0L;
            }
        }
        for (j = 0; j < 2; ++j)
            for (k = 0; k < 3; ++k) {
                gnode[i].m[j][k] = 0;
                gnode[i].cm[j][k] = 0;
                gnode[i].lm[j][k] = 0L;
            }
    }

    for (i = 0; i < 16; ++i) {
        gint[i] = 0;
        gchar[i] = 0;
        glong[i] = 0L;
    }
    for (i = 0; i < 3; ++i)
        for (j = 0; j < 4; ++j) {
            gmat[i][j] = 0;
            gcmat[i][j] = 0;
            glmat[i][j] = 0L;
        }

    for (i = 0; i < 2; ++i) {
        gwrap[i].pn = &gnode[i];
        for (j = 0; j < 3; ++j) gwrap[i].lp[j] = &gleaf[j + i];
        for (j = 0; j < 4; ++j) {
            gwrap[i].ip[j] = &gint[i * 4 + j];
            gwrap[i].cp[j] = &gchar[i * 4 + j];
            gwrap[i].lop[j] = &glong[i * 4 + j];
        }
        for (j = 0; j < 2; ++j) {
            gwrap[i].n[j].pl = &gleaf[i + j];
            gwrap[i].n[j].pi = &gint[8 + i * 2 + j];
            gwrap[i].n[j].pc = &gchar[8 + i * 2 + j];
            gwrap[i].n[j].plong = &glong[8 + i * 2 + j];
            for (k = 0; k < 3; ++k) {
                gwrap[i].n[j].leaf[k].v = 0;
                gwrap[i].n[j].leaf[k].cv = 0;
                gwrap[i].n[j].leaf[k].lv = 0L;
                gwrap[i].n[j].leaf[k].a[0] = 0;
                gwrap[i].n[j].leaf[k].a[1] = 0;
                gwrap[i].n[j].leaf[k].a[2] = 0;
                gwrap[i].n[j].leaf[k].a[3] = 0;
                gwrap[i].n[j].leaf[k].ca[0] = 0;
                gwrap[i].n[j].leaf[k].ca[1] = 0;
                gwrap[i].n[j].leaf[k].ca[2] = 0;
                gwrap[i].n[j].leaf[k].ca[3] = 0;
                gwrap[i].n[j].leaf[k].la[0] = 0L;
                gwrap[i].n[j].leaf[k].la[1] = 0L;
                gwrap[i].n[j].leaf[k].la[2] = 0L;
                gwrap[i].n[j].leaf[k].la[3] = 0L;
            }
            gwrap[i].n[j].m[0][0] = 0;
            gwrap[i].n[j].m[0][1] = 0;
            gwrap[i].n[j].m[0][2] = 0;
            gwrap[i].n[j].m[1][0] = 0;
            gwrap[i].n[j].m[1][1] = 0;
            gwrap[i].n[j].m[1][2] = 0;
            gwrap[i].n[j].cm[0][0] = 0;
            gwrap[i].n[j].cm[0][1] = 0;
            gwrap[i].n[j].cm[0][2] = 0;
            gwrap[i].n[j].cm[1][0] = 0;
            gwrap[i].n[j].cm[1][1] = 0;
            gwrap[i].n[j].cm[1][2] = 0;
            gwrap[i].n[j].lm[0][0] = 0L;
            gwrap[i].n[j].lm[0][1] = 0L;
            gwrap[i].n[j].lm[0][2] = 0L;
            gwrap[i].n[j].lm[1][0] = 0L;
            gwrap[i].n[j].lm[1][1] = 0L;
            gwrap[i].n[j].lm[1][2] = 0L;
        }
    }

    gpwrap = &gwrap[0];
    gpnode = &gnode[0];
    gpleaf = &gleaf[0];
    gpint = &gint[0];
    gpchar = &gchar[0];
    gplong = &glong[0];
}

static void touch_params(wpp, npp, lpp, ipp, cpp, longpp)
struct Wrapper **wpp;
struct Node **npp;
struct Leaf **lpp;
int **ipp;
char **cpp;
long **longpp;
{
    int ix;

    ix = 1;

    /* Through pointer-to-pointer parameters. */
    (*wpp)->n[ix].leaf[ix + 1].a[ix] = 101;
    (*wpp + 1)->n[0].leaf[2].v = 102;
    (*npp)->leaf[2].a[3] = 103;
    (*npp + 1)->pl->a[2] = 104;
    (*lpp)->a[1] = 105;
    (*lpp + 2)->v = 106;
    *(*ipp + 5) = 107;
    (*ipp)[6] = 108;
    (*wpp)->n[ix].leaf[ix].ca[ix + 1] = 21;
    (*wpp + 1)->n[0].leaf[1].cv = 22;
    (*npp)->leaf[0].la[3] = 100001L;
    (*npp + 1)->pl->lv = -100002L;
    (*lpp)->ca[2] = 23;
    (*lpp + 2)->la[1] = 100003L;
    *(*cpp + 5) = 24;
    (*cpp)[6] = 25;
    *(*longpp + 5) = 100004L;
    (*longpp)[6] = -100005L;

    check_int("param wpp n leaf a", gwrap[0].n[1].leaf[2].a[1], 101);
    check_int("param wpp plus n leaf v", gwrap[1].n[0].leaf[2].v, 102);
    check_int("param npp leaf a", gnode[0].leaf[2].a[3], 103);
    check_int("param npp plus pl a", gleaf[1].a[2], 104);
    check_int("param lpp a", gleaf[0].a[1], 105);
    check_int("param lpp plus v", gleaf[2].v, 106);
    check_int("param ipp star plus", gint[5], 107);
    check_int("param ipp subscript", gint[6], 108);
    check_char("param wpp n leaf ca", gwrap[0].n[1].leaf[1].ca[2], 21);
    check_char("param wpp plus n leaf cv", gwrap[1].n[0].leaf[1].cv, 22);
    check_long("param npp leaf la", gnode[0].leaf[0].la[3], 100001L);
    check_long("param npp plus pl lv", gleaf[1].lv, -100002L);
    check_char("param lpp ca", gleaf[0].ca[2], 23);
    check_long("param lpp plus la", gleaf[2].la[1], 100003L);
    check_char("param cpp star plus", gchar[5], 24);
    check_char("param cpp subscript", gchar[6], 25);
    check_long("param longpp star plus", glong[5], 100004L);
    check_long("param longpp subscript", glong[6], -100005L);
}

static void touch_locals()
{
    struct Leaf lleaf[4];
    struct Node lnode[2];
    struct Wrapper lwrap;
    struct Wrapper *wp;
    struct Node *np;
    struct Leaf *lp;
    int lint[12];
    char lchar[12];
    long llong[12];
    int lmat[2][5];
    char lcmat[2][5];
    long llmat[2][5];
    int *ip;
    char *cp;
    long *longp;
    int i, j;

    for (i = 0; i < 4; ++i) {
        lleaf[i].v = 0;
        lleaf[i].cv = 0;
        lleaf[i].lv = 0L;
        for (j = 0; j < 4; ++j) {
            lleaf[i].a[j] = 0;
            lleaf[i].ca[j] = 0;
            lleaf[i].la[j] = 0L;
        }
    }
    for (i = 0; i < 12; ++i) {
        lint[i] = 0;
        lchar[i] = 0;
        llong[i] = 0L;
    }
    for (i = 0; i < 2; ++i)
        for (j = 0; j < 5; ++j) {
            lmat[i][j] = 0;
            lcmat[i][j] = 0;
            llmat[i][j] = 0L;
        }

    lnode[0].pl = &lleaf[0];
    lnode[1].pl = &lleaf[1];
    lnode[0].pi = &lint[0];
    lnode[1].pi = &lint[6];
    lnode[0].pc = &lchar[0];
    lnode[1].pc = &lchar[6];
    lnode[0].plong = &llong[0];
    lnode[1].plong = &llong[6];
    for (i = 0; i < 2; ++i) {
        for (j = 0; j < 3; ++j) {
            lnode[i].leaf[j].v = 0;
            lnode[i].leaf[j].cv = 0;
            lnode[i].leaf[j].lv = 0L;
            lnode[i].leaf[j].a[0] = 0;
            lnode[i].leaf[j].a[1] = 0;
            lnode[i].leaf[j].a[2] = 0;
            lnode[i].leaf[j].a[3] = 0;
            lnode[i].leaf[j].ca[0] = 0;
            lnode[i].leaf[j].ca[1] = 0;
            lnode[i].leaf[j].ca[2] = 0;
            lnode[i].leaf[j].ca[3] = 0;
            lnode[i].leaf[j].la[0] = 0L;
            lnode[i].leaf[j].la[1] = 0L;
            lnode[i].leaf[j].la[2] = 0L;
            lnode[i].leaf[j].la[3] = 0L;
        }
        lnode[i].m[0][0] = 0;
        lnode[i].m[0][1] = 0;
        lnode[i].m[0][2] = 0;
        lnode[i].m[1][0] = 0;
        lnode[i].m[1][1] = 0;
        lnode[i].m[1][2] = 0;
        lnode[i].cm[0][0] = 0;
        lnode[i].cm[0][1] = 0;
        lnode[i].cm[0][2] = 0;
        lnode[i].cm[1][0] = 0;
        lnode[i].cm[1][1] = 0;
        lnode[i].cm[1][2] = 0;
        lnode[i].lm[0][0] = 0L;
        lnode[i].lm[0][1] = 0L;
        lnode[i].lm[0][2] = 0L;
        lnode[i].lm[1][0] = 0L;
        lnode[i].lm[1][1] = 0L;
        lnode[i].lm[1][2] = 0L;
    }

    lwrap.n[0] = lnode[0];
    lwrap.n[1] = lnode[1];
    lwrap.pn = &lwrap.n[0];
    lwrap.lp[0] = &lleaf[0];
    lwrap.lp[1] = &lleaf[1];
    lwrap.lp[2] = &lleaf[2];
    lwrap.ip[0] = &lint[0];
    lwrap.ip[1] = &lint[3];
    lwrap.ip[2] = &lint[6];
    lwrap.ip[3] = &lint[9];
    lwrap.cp[0] = &lchar[0];
    lwrap.cp[1] = &lchar[3];
    lwrap.cp[2] = &lchar[6];
    lwrap.cp[3] = &lchar[9];
    lwrap.lop[0] = &llong[0];
    lwrap.lop[1] = &llong[3];
    lwrap.lop[2] = &llong[6];
    lwrap.lop[3] = &llong[9];

    wp = &lwrap;
    np = &lnode[0];
    lp = &lleaf[0];
    ip = &lint[0];
    cp = &lchar[0];
    longp = &llong[0];

    /* Local arrays and local struct objects. */
    lmat[1][3] = 201;
    *(*(lmat + 0) + 4) = 202;
    (lp + 3)->a[2] = 203;
    (*(lp + 2)).v = 204;
    (np + 1)->leaf[2].a[1] = 205;
    (*(np + 0)).m[1][2] = 206;
    *(((np + 1)->pi) + 4) = 207;
    ((wp->pn) + 1)->pl->a[3] = 208;
    (*((wp->lp) + 2))->a[0] = 209;
    *((*(wp->ip + 1)) + 2) = 210;
    *(ip + (lmat[0][4] - 200)) = 211;       /* index 2 */
    lcmat[1][3] = 31;
    *(*(lcmat + 0) + 4) = 32;
    llmat[1][4] = 200001L;
    *(*(llmat + 0) + 3) = -200002L;
    (lp + 3)->ca[2] = 33;
    (*(lp + 2)).lv = 200003L;
    (np + 1)->leaf[2].la[1] = 200004L;
    (*(np + 0)).cm[1][2] = 34;
    *(((np + 1)->pc) + 4) = 35;
    *(((np + 1)->plong) + 4) = -200005L;
    ((wp->pn) + 1)->pl->ca[3] = 36;
    (*((wp->lp) + 2))->la[0] = 200006L;
    *((*(wp->cp + 1)) + 2) = 37;
    *((*(wp->lop + 1)) + 2) = 200007L;
    *(cp + (lcmat[0][4] - 30)) = 38;       /* index 2 */
    *(longp + (int)(lcmat[0][4] - 29)) = -200008L; /* index 3 */

    check_int("local lmat subscript", lmat[1][3], 201);
    check_int("local lmat ptr", lmat[0][4], 202);
    check_int("local lp plus a", lleaf[3].a[2], 203);
    check_int("local lp plus v", lleaf[2].v, 204);
    check_int("local np leaf a", lnode[1].leaf[2].a[1], 205);
    check_int("local np m", lnode[0].m[1][2], 206);
    check_int("local np pi", lint[10], 207);
    check_int("local wp pn pl", lleaf[1].a[3], 208);
    check_int("local wp lp", lleaf[2].a[0], 209);
    check_int("local wp ip", lint[5], 210);
    check_int("local computed index", lint[2], 211);
    check_char("local lcmat subscript", lcmat[1][3], 31);
    check_char("local lcmat ptr", lcmat[0][4], 32);
    check_long("local llmat subscript", llmat[1][4], 200001L);
    check_long("local llmat ptr", llmat[0][3], -200002L);
    check_char("local lp plus ca", lleaf[3].ca[2], 33);
    check_long("local lp plus lv", lleaf[2].lv, 200003L);
    check_long("local np leaf la", lnode[1].leaf[2].la[1], 200004L);
    check_char("local np cm", lnode[0].cm[1][2], 34);
    check_char("local np pc", lchar[10], 35);
    check_long("local np plong", llong[10], -200005L);
    check_char("local wp pn pl", lleaf[1].ca[3], 36);
    check_long("local wp lp", lleaf[2].la[0], 200006L);
    check_char("local wp cp", lchar[5], 37);
    check_long("local wp lop", llong[5], 200007L);
    check_char("local computed char index", lchar[2], 38);
    check_long("local computed long index", llong[3], -200008L);
}

static void touch_globals()
{
    int i;

    i = 1;

    /* Global arrays, global pointer vars, arrays of pointers, and matrices. */
    gleaf[i + 1].a[i] = 301;
    (gpleaf + 3)->v = 302;
    (*(gpleaf + 4)).a[2] = 303;
    gnode[2].leaf[1].a[3] = 304;
    (gpnode + 1)->m[1][2] = 305;
    *((gpnode + 2)->pi + 1) = 306;
    gwrap[1].lp[2]->a[3] = 307;
    (*((gwrap[0].lp) + 1))->v = 308;
    *(gwrap[1].ip[3]) = 309;
    *((*(gwrap[0].ip + 2)) + 1) = 310;
    gpwrap->pn->leaf[1].a[0] = 311;
    ((gpwrap + 1)->pn + 1)->m[0][2] = 312;
    (*(gpwrap + 1)).n[1].pl->v = 313;
    gmat[2][1] = 314;
    *(*(gmat + 1) + 3) = 315;
    *(gpint + 15) = 316;
    gleaf[i + 1].ca[i] = 41;
    (gpleaf + 3)->lv = 300001L;
    (*(gpleaf + 4)).la[2] = -300002L;
    gnode[2].leaf[1].ca[3] = 42;
    (gpnode + 1)->lm[1][2] = 300003L;
    *((gpnode + 2)->pc + 1) = 43;
    *((gpnode + 2)->plong + 1) = -300004L;
    gwrap[1].lp[2]->la[3] = 300005L;
    (*((gwrap[0].lp) + 1))->cv = 44;
    *(gwrap[1].cp[3]) = 45;
    *(gwrap[1].lop[3]) = 300006L;
    *((*(gwrap[0].cp + 2)) + 1) = 46;
    *((*(gwrap[0].lop + 2)) + 1) = -300007L;
    gpwrap->pn->leaf[1].ca[0] = 47;
    ((gpwrap + 1)->pn + 1)->cm[0][2] = 48;
    (*(gpwrap + 1)).n[1].pl->la[1] = 300008L;
    gcmat[2][1] = 49;
    *(*(gcmat + 1) + 3) = 50;
    glmat[2][1] = 300009L;
    *(*(glmat + 1) + 3) = -300010L;
    *(gpchar + 15) = 51;
    *(gplong + 15) = 300011L;

    check_int("global gleaf a", gleaf[2].a[1], 301);
    check_int("global gpleaf v", gleaf[3].v, 302);
    check_int("global gpleaf a", gleaf[4].a[2], 303);
    check_int("global gnode leaf", gnode[2].leaf[1].a[3], 304);
    check_int("global gpnode m", gnode[1].m[1][2], 305);
    check_int("global gpnode pi", gint[5], 306);
    check_int("global gwrap lp", gleaf[3].a[3], 307);
    check_int("global gwrap lp star", gleaf[1].v, 308);
    check_int("global gwrap ip", gint[7], 309);
    check_int("global gwrap ip star", gint[3], 310);
    check_int("global gpwrap pn", gnode[0].leaf[1].a[0], 311);
    check_int("global gpwrap plus pn", gnode[2].m[0][2], 312);
    check_int("global gpwrap n pl", gleaf[2].v, 313);
    check_int("global gmat subscript", gmat[2][1], 314);
    check_int("global gmat ptr", gmat[1][3], 315);
    check_int("global gpint", gint[15], 316);
    check_char("global gleaf ca", gleaf[2].ca[1], 41);
    check_long("global gpleaf lv", gleaf[3].lv, 300001L);
    check_long("global gpleaf la", gleaf[4].la[2], -300002L);
    check_char("global gnode leaf ca", gnode[2].leaf[1].ca[3], 42);
    check_long("global gpnode lm", gnode[1].lm[1][2], 300003L);
    check_char("global gpnode pc", gchar[5], 43);
    check_long("global gpnode plong", glong[5], -300004L);
    check_long("global gwrap lp la", gleaf[3].la[3], 300005L);
    check_char("global gwrap lp star cv", gleaf[1].cv, 44);
    check_char("global gwrap cp", gchar[7], 45);
    check_long("global gwrap lop", glong[7], 300006L);
    check_char("global gwrap cp star", gchar[3], 46);
    check_long("global gwrap lop star", glong[3], -300007L);
    check_char("global gpwrap pn", gnode[0].leaf[1].ca[0], 47);
    check_char("global gpwrap plus pn", gnode[2].cm[0][2], 48);
    check_long("global gpwrap n pl", gleaf[2].la[1], 300008L);
    check_char("global gcmat subscript", gcmat[2][1], 49);
    check_char("global gcmat ptr", gcmat[1][3], 50);
    check_long("global glmat subscript", glmat[2][1], 300009L);
    check_long("global glmat ptr", glmat[1][3], -300010L);
    check_char("global gpchar", gchar[15], 51);
    check_long("global gplong", glong[15], 300011L);
}

static void touch_alias_mix()
{
    struct Wrapper *wa[2];
    struct Node *na[3];
    struct Leaf *la[5];
    int *ia[4];
    char *ca[4];
    long *loa[4];
    int sel;

    wa[0] = &gwrap[0];
    wa[1] = &gwrap[1];
    na[0] = &gnode[0];
    na[1] = &gwrap[0].n[1];
    na[2] = &gwrap[1].n[0];
    la[0] = &gleaf[0];
    la[1] = &gnode[1].leaf[2];
    la[2] = &gwrap[0].n[0].leaf[1];
    la[3] = &gwrap[1].n[1].leaf[2];
    la[4] = &gleaf[4];
    ia[0] = &gint[0];
    ia[1] = &gint[4];
    ia[2] = &gmat[0][0];
    ia[3] = &gmat[2][0];
    ca[0] = &gchar[0];
    ca[1] = &gchar[4];
    ca[2] = &gcmat[0][0];
    ca[3] = &gcmat[2][0];
    loa[0] = &glong[0];
    loa[1] = &glong[4];
    loa[2] = &glmat[0][0];
    loa[3] = &glmat[2][0];

    sel = 1;

    (*(wa + sel))->n[sel].leaf[sel].a[sel + 1] = 401;
    ((*(wa + 0))->pn + sel)->leaf[0].v = 402;
    (*(na + 2))->pl->a[1] = 403;
    ((*(na + 1))->pl)->a[2] = 404;
    (*(la + 3))->a[3] = 405;
    (*(la + 1))->v = 406;
    *(*(ia + 0) + 11) = 407;
    *(*(ia + 1) + 3) = 408;
    *(*(ia + 2) + 6) = 409;  /* gmat[1][2] because gmat is contiguous */
    *(*(ia + 3) + 3) = 410;  /* gmat[2][3] */
    (*(wa + sel))->n[sel].leaf[sel].ca[sel + 1] = 61;
    ((*(wa + 0))->pn + sel)->leaf[0].lv = 400001L;
    (*(na + 2))->pl->ca[1] = 62;
    ((*(na + 1))->pl)->la[2] = -400002L;
    (*(la + 3))->la[3] = 400003L;
    (*(la + 1))->cv = 63;
    *(*(ca + 0) + 11) = 64;
    *(*(ca + 1) + 3) = 65;
    *(*(ca + 2) + 6) = 66;  /* gcmat[1][2] */
    *(*(ca + 3) + 3) = 67;  /* gcmat[2][3] */
    *(*(loa + 0) + 11) = 400004L;
    *(*(loa + 1) + 3) = -400005L;
    *(*(loa + 2) + 6) = 400006L;  /* glmat[1][2] */
    *(*(loa + 3) + 3) = -400007L; /* glmat[2][3] */

    check_int("alias wa n leaf", gwrap[1].n[1].leaf[1].a[2], 401);
    check_int("alias wa pn plus", gnode[1].leaf[0].v, 402);
    check_int("alias na pl", gleaf[1].a[1], 403);
    check_int("alias na dot pl", gleaf[1].a[2], 404);
    check_int("alias la a", gwrap[1].n[1].leaf[2].a[3], 405);
    check_int("alias la v", gnode[1].leaf[2].v, 406);
    check_int("alias ia gint", gint[11], 407);
    check_int("alias ia gint offset", gint[7], 408);
    check_int("alias ia gmat mid", gmat[1][2], 409);
    check_int("alias ia gmat end", gmat[2][3], 410);
    check_char("alias wa n leaf ca", gwrap[1].n[1].leaf[1].ca[2], 61);
    check_long("alias wa pn plus lv", gnode[1].leaf[0].lv, 400001L);
    check_char("alias na pl ca", gleaf[1].ca[1], 62);
    check_long("alias na dot pl la", gleaf[1].la[2], -400002L);
    check_long("alias la la", gwrap[1].n[1].leaf[2].la[3], 400003L);
    check_char("alias la cv", gnode[1].leaf[2].cv, 63);
    check_char("alias ca gchar", gchar[11], 64);
    check_char("alias ca gchar offset", gchar[7], 65);
    check_char("alias ca gcmat mid", gcmat[1][2], 66);
    check_char("alias ca gcmat end", gcmat[2][3], 67);
    check_long("alias loa glong", glong[11], 400004L);
    check_long("alias loa glong offset", glong[7], -400005L);
    check_long("alias loa glmat mid", glmat[1][2], 400006L);
    check_long("alias loa glmat end", glmat[2][3], -400007L);
}

static void touch_ptr_to_array_deref()
{
    int irow[4];
    char crow[4];
    int *prow[3];
    struct Leaf leafrow[3];
    int (*ip)[4];
    char (*cp)[4];
    int *(*pp)[3];
    struct Leaf (*lp)[3];
    int i;
    int ix;

    for (i = 0; i < 4; ++i) {
        irow[i] = 0;
        crow[i] = 0;
    }
    for (i = 0; i < 3; ++i) {
        prow[i] = 0;
        leafrow[i] = make_leaf(0);
    }

    ip = &irow;
    cp = &crow;
    pp = &prow;
    lp = &leafrow;
    ix = 2;

    (*ip)[ix] = 501;
    (*cp)[ix + 1] = 71;
    (*pp)[1] = &gint[14];
    (*lp)[ix] = make_leaf(70);

    check_int("ptr-array int store", irow[2], 501);
    check_char("ptr-array char store", crow[3], 71);
    check_int("ptr-array pointer store", *prow[1], gint[14]);
    check_int("ptr-array struct v", leafrow[2].v, 70);
    check_char("ptr-array struct cv", leafrow[2].cv, 71);
    check_long("ptr-array struct la", leafrow[2].la[3], 70003L);
}

int main()
{
    struct Wrapper *wp;
    struct Node *np;
    struct Leaf *lp;
    int *ip;
    char *cp;
    long *longp;

    printf("tptrlhs start\n");
    init_all();

    touch_globals();
    touch_locals();

    wp = &gwrap[0];
    np = &gnode[0];
    lp = &gleaf[0];
    ip = &gint[0];
    cp = &gchar[0];
    longp = &glong[0];
    touch_params(&wp, &np, &lp, &ip, &cp, &longp);

    touch_alias_mix();
    touch_ptr_to_array_deref();

    if (fails == 0) {
        printf("PASS\n");
        return 0;
    }
    printf("tptrlhs failed: %d\n", fails);
    return 1;
}
