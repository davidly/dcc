/* tc89flta.c - float assignment/copy/call/return storage tests */
#include <stdio.h>

static int fails;

static void chkb(const char *name, unsigned char got, unsigned char expect)
{
    if (got != expect) {
        printf("FAIL %s got %u expected %u\n", name, (unsigned int)got, (unsigned int)expect);
        fails++;
    }
}

static void chk4(const char *name, float *pf, unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3)
{
    unsigned char *p;
    p = (unsigned char *)pf;
    chkb(name, p[0], b0);
    chkb(name, p[1], b1);
    chkb(name, p[2], b2);
    chkb(name, p[3], b3);
}

struct Sflt { int tag; float f; };
struct Core { float flux; float reserve; };

static float gf;
static float gg;
static float ga[2];

static float f_id(float x)
{
    return x;
}

static float f_gv(void)
{
    return ga[1];
}

static void f_st(float x)
{
    gg = x;
}

static void core_rebalance(struct Core *core)
{
    core->reserve += core->flux * 0.125f;
}

static float core_rebalance_live(struct Core *core)
{
    return core->reserve += core->flux * 0.125f;
}

static float deref_add_live(float *p)
{
    return *p += 1.5f;
}

int main(void)
{
    float lf;
    float lg;
    float lh;
    float la[2];
    struct Sflt s;
    struct Core core;
    float lc;
    float ld;

    fails = 0;
    printf("tc89flta start\n");

    gf = 1.0f;
    chk4("gf", &gf, 0, 0, 128, 63);

    gg = gf;
    chk4("gg", &gg, 0, 0, 128, 63);

    lf = 2.5f;
    chk4("lf", &lf, 0, 0, 32, 64);

    lg = lf;
    chk4("lg", &lg, 0, 0, 32, 64);

    ga[0] = 1.0f;
    ga[1] = -3.0f;
    chk4("ga0", &ga[0], 0, 0, 128, 63);
    chk4("ga1", &ga[1], 0, 0, 64, 192);

    la[0] = 2.0f;
    la[1] = la[0];
    chk4("la0", &la[0], 0, 0, 0, 64);
    chk4("la1", &la[1], 0, 0, 0, 64);

    s.f = 1.0f;
    chk4("sf", &s.f, 0, 0, 128, 63);

    core.flux = 3.5f;
    core.reserve = 0.25f;
    core_rebalance(&core);
    chk4("core", &core.reserve, 0, 0, 48, 63);

    core.flux = 3.5f;
    core.reserve = 0.25f;
    ld = core_rebalance_live(&core);
    chk4("corelf", &core.reserve, 0, 0, 48, 63);
    chk4("corelr", &ld, 0, 0, 48, 63);

    gf = 2.0f;
    ld = f_id(gf += 1.5f);
    chk4("gfadd_f", &gf, 0, 0, 96, 64);
    chk4("gfadd_r", &ld, 0, 0, 96, 64);

    lc = 2.0f;
    ld = deref_add_live(&lc);
    chk4("dadd_f", &lc, 0, 0, 96, 64);
    chk4("dadd_r", &ld, 0, 0, 96, 64);

    ga[1] = 2.0f;
    ld = f_id(ga[1] += 1.5f);
    chk4("gaadd_f", &ga[1], 0, 0, 96, 64);
    chk4("gaadd_r", &ld, 0, 0, 96, 64);
    ga[1] = -3.0f;

    /* local float assignment used as a live value (ix-direct store path) */
    lc = 2.0f;
    ld = (lc += 1.5f);            /* lc=3.5, ld=3.5 */
    chk4("lcadd_f", &lc, 0, 0, 96, 64);
    chk4("lcadd_r", &ld, 0, 0, 96, 64);

    lc = 2.0f;
    ld = (lc = 5.0f);            /* lc=5.0, ld=5.0 */
    chk4("lcset_f", &lc, 0, 0, 160, 64);
    chk4("lcset_r", &ld, 0, 0, 160, 64);

    lh = f_id(la[1]);
    chk4("fid", &lh, 0, 0, 0, 64);

    lh = f_gv();
    chk4("fgv", &lh, 0, 0, 64, 192);

    f_st(lf);
    chk4("fst", &gg, 0, 0, 32, 64);

    if (fails) {
        printf("tc89flta failed: %d\n", fails);
        return 1;
    }
    printf("tc89flta ok\n");
    return 0;
}
