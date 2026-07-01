#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

/*
 * ATTNC99 - "Paper Tape is All You Need"
 *
 * A 1-layer, 1-head transformer trained to reverse an 8-digit sequence.
 *
 * This is the dcc C89 (CP/M 2.2 / Z80) port of the BDS C 1.6 program
 * Apps/ATTN/ATTN.C, itself a port of the PDP-11 2.11BSD assembly attn.s
 * (davepl/pdpsrc) and the NN11 library by Damien Boureille (dbrll/ATTN-11).
 *
 * What changed vs. the BDS C original, and why it is faster/smaller:
 *
 *   1. Native 32-bit `long`.  BDS C had no 32-bit type, so every Q16
 *      operation went through the LONG package (char[4] buffers plus the
 *      itol/ltoi/lmul/ladd/lsub/ldiv/lcomp builtins - a function call per
 *      op).  dcc has a real `long`, so the fixed-point helpers collapse to
 *      ordinary arithmetic and the hot loops (vdot, mvmul, vtmul, ...) run
 *      far faster with much less code.
 *
 *   2. The Q16 weight arrays are now `long[]` instead of char[N*4].  dcc
 *      stores `long` LITTLE-ENDIAN (Z80-native), so the weights written to
 *      ATTN.WTS are little-endian - unlike the BDS C LONG package, which
 *      stored them big-endian.  The companion inference port ATTNZ80.MAC
 *      has been updated to read little-endian to match this file.
 *
 *   3. Aggregate initializers replace the hand-written initex()/initlg()
 *      table fillers (BDS C had no array initializers; dcc does).
 *
 *   4. POSIX byte I/O (open/read/write from <fcntl.h>/<unistd.h>) replaces
 *      BDS C's 128-byte sector creat/open/read/write.  The six weight
 *      blocks are exact multiples of 128 bytes, so the on-disk layout is
 *      identical apart from the endianness change in (2).
 *
 * Numerics (faithful to ATTN.C):
 *   Q8  forward activations   (int, value = real * 256)
 *   Q15 backward gradients    (int)
 *   Q16 weight accumulators   (long, value = real * 65536)
 *
 * Build (from the dcc repo, with DCC/DCCPEEP/DCCRTLSTRIP exported):
 *   ./ma.sh attnc99 peep        -> ATTNC99.COM
 *
 * Usage:
 *   attnc99 -t      train, save weights to ATTN.WTS, then test
 *   attnc99         run inference using the saved weights (default)
 *   attnc99 <file>  run inference on <file> instead of ATTN.IN
 *   attnc99 -h      help
 */

#define D 16            /* d_model            */
#define S 8             /* sequence length    */
#define V 10            /* vocab (digits 0-9) */

#define NPARAM (V*D + S*D + D*D + D*D + D*D + D*V)
#define WBYTES (NPARAM * (int)sizeof(long))

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201112L
#define SA_CAT2(a, b) a##b
#define SA_CAT(a, b) SA_CAT2(a, b)
#define _Static_assert(e, msg) typedef char SA_CAT(static_assertion_, __LINE__)[(e) ? 1 : -1]
#endif

#define NSTEP 700       /* max training steps  */
#define RPRT 50         /* report interval     */
#define FSTEP 10        /* mix in IFILE every n steps */

#define ERROR (-1)

#define FM_TRAIN 1
#define FM_VALID 2
#define FM_INFER 3

/* Trained weights are saved here so inference can reload them. */
#define WFILE "ATTN.WTS"

/* Default inference input file: one S-digit sequence per line. */
#define IFILE "ATTN.IN"

/* WORK layout: Q | K | V | A(scores), each S*D except A is S*S */
#define QB 0
#define KB (S*D)
#define VB (2*S*D)
#define AB (3*S*D)

_Static_assert(sizeof(int) == 2, "ATTNC89 needs 16-bit int");
_Static_assert(sizeof(long) == 4, "ATTNC89 needs 32-bit long");
_Static_assert(D == 16, "ATTNC89 assumes d_model is 16");
_Static_assert(S == 8, "ATTNC89 assumes sequence length is 8");
_Static_assert(V == 10, "ATTNC89 assumes 10 digit tokens");
_Static_assert(AB == 3*S*D, "ATTNC89 workspace offsets changed");
_Static_assert(AB + S*S == 3*S*D + S*S, "ATTNC89 workspace size changed");
_Static_assert(WBYTES == 4864, "ATTNC89 weight file payload changed");

/* --- Q16 weight accumulators (native little-endian long) --- */
long wtke[V*D];         /* token embed  */
long wpse[S*D];         /* pos embed    */
long wwq[D*D];          /* Wq           */
long wwk[D*D];          /* Wk           */
long wwv[D*D];          /* Wv           */
long wwot[D*V];         /* Wout         */

/* --- Q8 weight copies (rebuilt from the Q16 accumulators) --- */
int qtke[V*D];
int qpse[S*D];
int qwq[D*D];
int qwk[D*D];
int qwv[D*D];
int qwot[D*V];

/* --- Q15 gradient accumulators --- */
int gtke[V*D];
int gpse[S*D];
int gwq[D*D];
int gwk[D*D];
int gwv[D*D];
int gwot[D*V];

/* --- forward state --- */
int xx[S*D];            /* embeddings (attn input)   */
int yy[S*D];            /* attn output (Y = O + X)   */
int logits[S*V];        /* output logits             */
int work[3*S*D + S*S];  /* attn workspace Q|K|V|A     */

/* --- backward workspace --- */
int dl[V];              /* dLogits (one position)    */
int dy[S*D];            /* dY                        */
int da[S*S];            /* dA, reused as dSc         */
int dqq[S*D];           /* dQ                        */
int dkk[S*D];           /* dK                        */
int dvv[S*D];           /* dV                        */
int dxx[S*D];           /* dX                        */
int dtmp[D];            /* temp column vector        */

/* --- training data / state --- */
int tokens[S];
int target[S];
int teprd[S];
unsigned rseed;
int thit;
int ttot;
int tstep;
int fhits;
int vhits;

/* --- lookup tables (aggregate initialised; BDS C used init functions) --- */

/* exp(-i/32) in Q8 */
int exptbl[256] = {
    256,248,240,233,226,219,212,206,199,193,187,182,176,171,165,160,
    155,150,146,141,137,133,129,125,121,117,114,110,107,103,100,97,
    94,91,88,86,83,81,78,76,73,71,69,67,65,63,61,59,
    57,55,54,52,50,49,47,46,44,43,42,41,39,38,37,36,
    35,34,33,32,31,30,29,28,27,26,25,25,24,23,22,22,
    21,20,20,19,19,18,17,17,16,16,15,15,14,14,14,13,
    13,12,12,12,11,11,11,10,10,10,9,9,9,8,8,8,
    8,7,7,7,7,7,6,6,6,6,6,5,5,5,5,5,
    5,5,4,4,4,4,4,4,4,4,3,3,3,3,3,3,
    3,3,3,3,3,2,2,2,2,2,2,2,2,2,2,2,
    2,2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

/* -ln(x/256)*4096 in Q12.  Indexed by a Q8 probability p only under the
 * `if (p < 256)` guard in closs(), so index 256 (p == 1.0) is never read;
 * the table is 256 entries (dcc caps an initializer list at 256 elements). */
int logtbl[256] = {
    22713,22713,19874,18213,17035,16121,15374,14743,14196,13713,
    13282,12891,12535,12207,11903,11621,11357,11108,10874,10653,
    10443,10243,10052,9870,9696,9529,9368,9213,9064,8921,
    8782,8647,8517,8391,8269,8150,8035,7923,7813,7707,
    7603,7502,7404,7307,7213,7121,7031,6943,6857,6772,
    6689,6608,6529,6451,6374,6299,6225,6153,6081,6011,
    5943,5875,5808,5743,5678,5615,5552,5491,5430,5370,
    5311,5253,5196,5139,5084,5029,4974,4921,4868,4816,
    4764,4713,4663,4613,4564,4516,4468,4421,4374,4328,
    4282,4237,4192,4148,4104,4060,4017,3975,3933,3891,
    3850,3810,3769,3729,3690,3650,3612,3573,3535,3497,
    3460,3423,3386,3350,3314,3278,3242,3207,3172,3138,
    3103,3069,3036,3002,2969,2936,2904,2871,2839,2807,
    2776,2744,2713,2682,2651,2621,2591,2561,2531,2501,
    2472,2443,2414,2385,2357,2328,2300,2272,2244,2217,
    2189,2162,2135,2108,2082,2055,2029,2003,1977,1951,
    1925,1900,1874,1849,1824,1799,1774,1750,1725,1701,
    1677,1653,1629,1605,1582,1558,1535,1512,1488,1466,
    1443,1420,1397,1375,1353,1330,1308,1286,1265,1243,
    1221,1200,1178,1157,1136,1115,1094,1073,1052,1032,
    1011,991,970,950,930,910,890,870,850,831,
    811,792,772,753,734,715,696,677,658,639,
    621,602,584,565,547,529,511,492,474,457,
    439,421,403,386,368,351,333,316,299,281,
    264,247,230,213,197,180,163,147,130,114,
    97,81,65,48,32,16
};

/* --- forward declarations --- */
static int  lci(long a);
static int  lq8(long a);
static inline int mq8(int a, int b);
static inline int fxdiv(int a, int b);
static int  asr(int v, int n);
static inline void addcl(int *dst, int v);
static inline int subcl(int a, int b);
static int  vmax(int *vec, unsigned char n, int *pidx);
static int  vdot(int *x, int *y, unsigned char n);
static void vsadd(int sc, int *src, int *dst, unsigned char n);
static void sftmx(int *vec, unsigned char n);
static void mvmul(int *mat, int *vin, int *vout, unsigned char rows, unsigned char cols);
static void mvadd(int *mat, int *vin, int *vout, unsigned char rows, unsigned char cols);
static void vtmul(int *mat, int *vin, int *vout, unsigned char rows, unsigned char cols);
static void outer(int *mat, int *vx, int *vy, unsigned char rows, unsigned char cols);
static void embed(void);
static void attn(void);
static void proj(void);
static void forwrd(void);
static void cv1(long *w, int *q, int n);
static void cvt16(void);
static int  rndnum(void);
static void in_fil(long *w, int n);
static void initw(void);
static void up_do(long *w, int *g, int n, int shift);
static void updat(void);
static void zerog(void);
static void bkwrd(void);
static void mktarg(void);
static void gensm(void);
static void trseq(void);
static int  ckseq(void);
static int  doseq(int mode);
static int  seqfile(char *fname, int mode);
static int  filrun(char *fname, int trn);
static void count(void);
static int  closs(void);
static inline int lossfr(int loss);
static void report(void);
static void test(void);
static int  infseq(void);
static int  runfil(char *fname);
static int  wio(int fd, long *w, int n, int wr);
static int  wfile(int fd, int wr);
static int  savew(void);
static int  loadw(void);
static unsigned elapsed(void);

/* ============================================================ */
/* 32-bit fixed-point helpers (native long)                     */
/* ============================================================ */

/* clamp a 32-bit long to a signed 16-bit int */
static int lci(long a)
{
    if (a > 32767L)
        return 32767;
    if (a < -32768L)
        return -32768;
    return (int)a;
}

/* a (Q16 long) >> 8 -> clamped Q8 int.
 * Truncates toward zero (matching the BDS C ldiv and the ATTNZ80.MAC LQ8
 * helper), so it divides the magnitude and re-applies the sign rather than
 * using an arithmetic shift (which would floor for negatives). */
static int lq8(long a)
{
    if (a < 0)
        return lci(-((-a) >> 8));
    return lci(a >> 8);
}

/* (a * b) >> 8 -> clamped Q8 int */
static inline int mq8(int a, int b)
{
    return lq8((long)a * b);
}

/* Q8 divide: (a << 8) / b -> clamped Q8 int (a >= 0, b > 0 at all call sites) */
static inline int fxdiv(int a, int b)
{
    return lci(((long)a * 256L) / (long)b);
}

/* arithmetic shift right by n (floor toward -inf), n small */
static int asr(int v, int n)
{
    int d, q;

    d = 1;
    while (n-- > 0)
        d = d << 1;
    q = v / d;
    if (v < 0 && (v % d) != 0)
        q = q - 1;
    return q;
}

/* *dst += v, saturating to signed 16-bit */
static inline void addcl(int *dst, int v)
{
    *dst = lci((long)*dst + v);
}

/* a - b, saturating to signed 16-bit */
static inline int subcl(int a, int b)
{
    return lci((long)a - b);
}

/* ============================================================ */
/* Vector primitives                                            */
/* ============================================================ */

static int vmax(int *vec, unsigned char n, int *pidx)
{
    int mx;
    unsigned char mi, i;

    mx = vec[0];
    mi = 0;
    for (i = 1; i < n; i++)
        if (vec[i] > mx) {
            mx = vec[i];
            mi = i;
        }
    *pidx = mi;
    return mx;
}

static int vdot(int *x, int *y, unsigned char n)
{
    long acc;
    unsigned char i;

    acc = 0;
    for (i = 0; i < n; i++)
        acc += (long)x[i] * y[i];
    return lq8(acc);
}

/* dst[k] += (scalar * src[k]) >> 8, saturating */
static void vsadd(int sc, int *src, int *dst, unsigned char n)
{
    unsigned char k;

    for (k = 0; k < n; k++)
        addcl(&dst[k], mq8(sc, src[k]));
}

/* softmax in place (Q8), LUT-based */
static void sftmx(int *vec, unsigned char n)
{
    int mx, d, idx, sum, dummy;
    unsigned char i;

    mx = vmax(vec, n, &dummy);
    sum = 0;
    for (i = 0; i < n; i++) {
        d = mx - vec[i];
        if (d < 0)
            d = 0;
        idx = d >> 3;
        if (idx > 255)
            idx = 255;
        vec[i] = exptbl[idx];
        sum = sum + vec[i];
    }
    for (i = 0; i < n; i++)
        vec[i] = fxdiv(vec[i], sum);
}

/* ============================================================ */
/* Matrix primitives                                            */
/* ============================================================ */

/* vout[i] = sum_j mat[i][j] * vin[j]  (Q16 accum, >>8, clamp) */
static void mvmul(int *mat, int *vin, int *vout, unsigned char rows, unsigned char cols)
{
    long acc;
    int mi;
    unsigned char i, j;

    mi = 0;
    for (i = 0; i < rows; i++) {
        acc = 0;
        for (j = 0; j < cols; j++)
            acc += (long)mat[mi + j] * vin[j];
        vout[i] = lq8(acc);
        mi = mi + cols;
    }
}

/* vout[i] += sum_j mat[i][j] * vin[j]  (saturating add) */
static void mvadd(int *mat, int *vin, int *vout, unsigned char rows, unsigned char cols)
{
    long acc;
    int mi;
    unsigned char i, j;

    mi = 0;
    for (i = 0; i < rows; i++) {
        acc = 0;
        for (j = 0; j < cols; j++)
            acc += (long)mat[mi + j] * vin[j];
        addcl(&vout[i], lq8(acc));
        mi = mi + cols;
    }
}

/* vout[j] = sum_i (mat[i][j] * vin[i]) >> 8   (per-product Q8) */
static void vtmul(int *mat, int *vin, int *vout, unsigned char rows, unsigned char cols)
{
    unsigned char i, j;
    int sc, mi;

    memset(vout, 0, cols * (int)sizeof(int));
    mi = 0;
    for (i = 0; i < rows; i++) {
        sc = vin[i];
        for (j = 0; j < cols; j++)
            addcl(&vout[j], mq8(mat[mi + j], sc));
        mi = mi + cols;
    }
}

/* mat[i][j] += (vx[i] * vy[j]) >> 8   (saturating) */
static void outer(int *mat, int *vx, int *vy, unsigned char rows, unsigned char cols)
{
    int sc, mi;
    unsigned char i, j;

    mi = 0;
    for (i = 0; i < rows; i++) {
        sc = vx[i];
        for (j = 0; j < cols; j++)
            addcl(&mat[mi + j], mq8(sc, vy[j]));
        mi = mi + cols;
    }
}

/* ============================================================ */
/* Layer operations                                             */
/* ============================================================ */

/* X[i] = tok_emb[tokens[i]] + pos_emb[i] */
static void embed(void)
{
    int tok, oi;
    unsigned char i, j;

    oi = 0;
    for (i = 0; i < S; i++) {
        tok = tokens[i];
        for (j = 0; j < D; j++)
            xx[oi + j] = qtke[tok * D + j] + qpse[oi + j];
        oi = oi + D;
    }
}

/* self-attention forward pass */
static void attn(void)
{
    unsigned char i, j;

    /* Step 1-3: Q = X.Wq, K = X.Wk, V = X.Wv */
    for (i = 0; i < S; i++) {
        vtmul(qwq, &xx[i * D], &work[QB + i * D], D, D);
        vtmul(qwk, &xx[i * D], &work[KB + i * D], D, D);
        vtmul(qwv, &xx[i * D], &work[VB + i * D], D, D);
    }
    /* Step 4: S[i][j] = (Q[i] . K[j]) / sqrt(d), sqrt(16)=4 -> >>2 */
    for (i = 0; i < S; i++)
        for (j = 0; j < S; j++)
            work[AB + i * S + j] =
                asr(vdot(&work[QB + i * D], &work[KB + j * D], D), 2);
    /* Step 5: softmax per row */
    for (i = 0; i < S; i++)
        sftmx(&work[AB + i * S], S);
    /* Step 6: Y[i] = V^T . A[i] */
    for (i = 0; i < S; i++)
        vtmul(&work[VB], &work[AB + i * S], &yy[i * D], S, D);
    /* Step 7: residual Y += X */
    for (i = 0; i < S * D; i++)
        yy[i] = yy[i] + xx[i];
}

/* logits[i] = Wout^T . Y[i] */
static void proj(void)
{
    unsigned char i;

    for (i = 0; i < S; i++)
        vtmul(qwot, &yy[i * D], &logits[i * V], D, V);
}

static void forwrd(void)
{
    embed();
    attn();
    proj();
}

/* ============================================================ */
/* Weight conversion / init / update                            */
/* ============================================================ */

/* convert one Q16 weight group to its Q8 copy */
static void cv1(long *w, int *q, int n)
{
    int i;

    for (i = 0; i < n; i++)
        q[i] = lq8(w[i]);
}

static void cvt16(void)
{
    cv1(wtke, qtke, V * D);
    cv1(wpse, qpse, S * D);
    cv1(wwq, qwq, D * D);
    cv1(wwk, qwk, D * D);
    cv1(wwv, qwv, D * D);
    cv1(wwot, qwot, D * V);
}

/* 15-bit LCG */
static int rndnum(void)
{
    rseed = (rseed * 25173 + 13849) & 0x7FFF;
    return rseed;
}

/* fill n Q16 weights with random Q8 in [-128,127] */
static void in_fil(long *w, int n)
{
    int i, r;

    for (i = 0; i < n; i++) {
        r = (rndnum() & 0x00FF) - 128;
        w[i] = (long)r * 256L;
    }
}

static void initw(void)
{
    in_fil(wtke, V * D);
    in_fil(wpse, S * D);
    in_fil(wwq, D * D);
    in_fil(wwk, D * D);
    in_fil(wwv, D * D);
    in_fil(wwot, D * V);
}

/* w_q16 -= grad_q15 >> (shift-1); zero grad after read */
static void up_do(long *w, int *g, int n, int shift)
{
    int i, delta;

    for (i = 0; i < n; i++) {
        delta = asr(g[i], shift - 1);
        g[i] = 0;
        w[i] -= (long)delta;
    }
}

static void updat(void)
{
    up_do(wtke, gtke, V * D, 4);
    up_do(wpse, gpse, S * D, 4);
    up_do(wwq, gwq, D * D, 1);
    up_do(wwk, gwk, D * D, 1);
    up_do(wwv, gwv, D * D, 1);
    up_do(wwot, gwot, D * V, 6);
}

static void zerog(void)
{
    memset(gtke, 0, V * D * (int)sizeof(int));
    memset(gpse, 0, S * D * (int)sizeof(int));
    memset(gwq, 0, D * D * (int)sizeof(int));
    memset(gwk, 0, D * D * (int)sizeof(int));
    memset(gwv, 0, D * D * (int)sizeof(int));
    memset(gwot, 0, D * V * (int)sizeof(int));
}

/* ============================================================ */
/* Backward pass                                                */
/* ============================================================ */

static void bkwrd(void)
{
    int i, j, k, o, tok, dad, t;

    /* Step 1: dLogits, dWout, dY */
    memset(dy, 0, S * D * (int)sizeof(int));
    for (i = 0; i < S; i++) {
        for (k = 0; k < V; k++)
            dl[k] = logits[i * V + k];
        sftmx(dl, V);
        dl[target[i]] = dl[target[i]] - 256;
        for (k = 0; k < V; k++)
            dl[k] = dl[k] << 7;
        outer(gwot, &yy[i * D], dl, D, V);
        mvmul(qwot, dl, &dy[i * D], D, V);
    }

    /* Step 2: dA, dV */
    memset(dvv, 0, S * D * (int)sizeof(int));
    for (i = 0; i < S; i++)
        for (j = 0; j < S; j++) {
            da[i * S + j] = vdot(&work[VB + j * D], &dy[i * D], D);
            vsadd(work[AB + i * S + j], &dy[i * D], &dvv[j * D], D);
        }

    /* Step 3: backward softmax -> dSc (in da) */
    for (i = 0; i < S; i++) {
        dad = vdot(&work[AB + i * S], &da[i * S], S);
        for (j = 0; j < S; j++) {
            t = subcl(da[i * S + j], dad);
            t = mq8(work[AB + i * S + j], t);
            da[i * S + j] = asr(t, 2);
        }
    }

    /* Step 4: dQ, dK */
    for (i = 0; i < S; i++)
        vtmul(&work[KB], &da[i * S], &dqq[i * D], S, D);
    for (j = 0; j < S; j++) {
        for (i = 0; i < S; i++)
            dtmp[i] = da[i * S + j];
        vtmul(&work[QB], dtmp, &dkk[j * D], S, D);
    }

    /* Step 5: backward projections + dX */
    memcpy(dxx, dy, S * D * (int)sizeof(int));
    for (i = 0; i < S; i++) {
        o = i * D;
        mvadd(qwq, &dqq[o], &dxx[o], D, D);
        outer(gwq, &xx[o], &dqq[o], D, D);
        mvadd(qwk, &dkk[o], &dxx[o], D, D);
        outer(gwk, &xx[o], &dkk[o], D, D);
        mvadd(qwv, &dvv[o], &dxx[o], D, D);
        outer(gwv, &xx[o], &dvv[o], D, D);
    }

    /* Step 6: backward embedding */
    for (i = 0; i < S; i++) {
        o = i * D;
        tok = tokens[i];
        for (k = 0; k < D; k++) {
            addcl(&gtke[tok * D + k], dxx[o + k]);
            addcl(&gpse[i * D + k], dxx[o + k]);
        }
    }
}

/* ============================================================ */
/* Training driver                                              */
/* ============================================================ */

/* set reversal target for the current tokens */
static void mktarg(void)
{
    int i;

    for (i = 0; i < S; i++)
        target[i] = tokens[S - 1 - i];
}

/* generate a random reversal sample */
static void gensm(void)
{
    int i;

    for (i = 0; i < S; i++)
        tokens[i] = rndnum() % 10;
    mktarg();
}

/* train one current tokens/target sample */
static void trseq(void)
{
    cvt16();
    forwrd();
    bkwrd();
    updat();
    count();
}

/* quiet accuracy check for one current tokens/target sample */
static int ckseq(void)
{
    int i, idx, ok;

    forwrd();
    ok = 1;
    for (i = 0; i < S; i++) {
        vmax(&logits[i * V], V, &idx);
        if (idx != target[i])
            ok = 0;
    }
    return ok;
}

static int doseq(int mode)
{
    if (mode == FM_INFER)
        return infseq();
    mktarg();
    if (mode == FM_TRAIN) {
        trseq();
        return 1;
    }
    return ckseq();
}

static int seqfile(char *fname, int mode)
{
    int ch, i, n;
    FILE *fp;

    fp = fopen(fname, "r");
    if (fp == NULL)
        return ERROR;

    n = 0;
    i = 0;
    while ((ch = getc(fp)) != EOF && ch != 26) {
        if (ch >= '0' && ch <= '9') {
            if (i < S)
                tokens[i] = ch - '0';
            i = i + 1;
        } else if (ch == '\n') {
            if (i >= S) {
                n = n + 1;
                if (mode == FM_VALID)
                    vhits = vhits + doseq(mode);
                else if (mode == FM_INFER)
                    fhits = fhits + doseq(mode);
                else
                    doseq(mode);
            } else if (mode == FM_INFER && i > 0)
                printf(" (skipped line: %d digits, need %d)\n", i, S);
            i = 0;
        }
    }
    if (i >= S) {
        n = n + 1;
        if (mode == FM_VALID)
            vhits = vhits + doseq(mode);
        else if (mode == FM_INFER)
            fhits = fhits + doseq(mode);
        else
            doseq(mode);
    }
    fclose(fp);
    return n;
}

/* read fixed samples from fname. trn=1 trains; trn=0 validates quietly. */
static int filrun(char *fname, int trn)
{
    if (!trn) {
        vhits = 0;
        cvt16();
    }
    return seqfile(fname, trn ? FM_TRAIN : FM_VALID);
}

/* count correct argmax predictions */
static void count(void)
{
    int i, idx;

    for (i = 0; i < S; i++) {
        vmax(&logits[i * V], V, &idx);
        if (idx == target[i])
            thit = thit + 1;
        ttot = ttot + 1;
    }
}

/* average cross-entropy loss (Q12) for the current sample */
static int closs(void)
{
    long acc;
    int i, k, p, t;

    acc = 0;
    for (i = 0; i < S; i++) {
        for (k = 0; k < V; k++)
            dl[k] = logits[i * V + k];
        sftmx(dl, V);
        t = target[i];
        p = dl[t];
        if (p < 256)
            acc += (long)logtbl[p];
    }
    return (int)(acc / 8L);
}

/* fractional part (0-9999) of a Q12 value */
static inline int lossfr(int loss)
{
    return (int)(((long)(loss & 0x0FFF) * 10000L) / 4096L);
}

/* print step / loss / accuracy, then reset counters */
static void report(void)
{
    int loss, pm;

    loss = closs();
    printf("\n step %4d loss=%d.%.4d", tstep, loss >> 12, lossfr(loss));

    pm = (int)(((long)thit * 1000L) / (long)ttot);
    if (pm >= 1000)
        printf(" acc=1.000\n");
    else
        printf(" acc=0.%.3d\n", pm);

    thit = 0;
    ttot = 0;
}

/* final test: 10 samples */
static void test(void)
{
    int n, i, idx, allok, ok;

    ok = 0;
    cvt16();
    for (n = 0; n < 10; n++) {
        gensm();
        forwrd();
        for (i = 0; i < S; i++) {
            vmax(&logits[i * V], V, &idx);
            teprd[i] = idx;
        }
        printf(" ");
        for (i = 0; i < S; i++)
            printf("%d ", tokens[i]);
        printf("-> ");
        for (i = 0; i < S; i++)
            printf("%d ", teprd[i]);
        allok = 1;
        for (i = 0; i < S; i++)
            if (teprd[i] != target[i])
                allok = 0;
        if (allok) {
            ok = ok + 1;
            printf(" ok\n");
        } else
            printf(" fail\n");
    }
    printf("\naccuracy  %2d/%d\n", ok, 10);
}

/* run inference on the current tokens[] and print "in -> out".
 * The task is to reverse the sequence, so the expected output is the
 * input read backwards; score the prediction against it.
 * Assumes cvt16() has already built the Q8 weight copies.
 * Returns 1 if every position is correct, else 0. */
static int infseq(void)
{
    int i, idx, ok;

    forwrd();
    printf(" ");
    for (i = 0; i < S; i++)
        printf("%d ", tokens[i]);
    printf("-> ");
    ok = 1;
    for (i = 0; i < S; i++) {
        vmax(&logits[i * V], V, &idx);
        printf("%d ", idx);
        if (idx != tokens[S - 1 - i])
            ok = 0;
    }
    if (ok)
        printf(" ok\n");
    else
        printf(" fail\n");
    return ok;
}

/* read S-digit sequences from a text file and run inference on each.
 * Digits are 0-9; any non-digit (space, newline) separates sequences.
 * A line must supply at least S digits; the first S are used.
 * Returns the count processed, or ERROR if the file cannot be opened. */
static int runfil(char *fname)
{
    fhits = 0;
    return seqfile(fname, FM_INFER);
}

/* ============================================================ */
/* Weight persistence (POSIX byte I/O)                          */
/* ============================================================ */

/* save the six Q16 weight arrays to WFILE; 0 ok, ERROR on fail.
 * dcc stores `long` little-endian, so the file is little-endian. */
static int wio(int fd, long *w, int n, int wr)
{
    int bytes;

    bytes = n * (int)sizeof(long);
    if (wr)
        return write(fd, w, bytes) == bytes;
    return read(fd, w, bytes) == bytes;
}

static int wfile(int fd, int wr)
{
    return wio(fd, wtke, V * D, wr)
        && wio(fd, wpse, S * D, wr)
        && wio(fd, wwq,  D * D, wr)
        && wio(fd, wwk,  D * D, wr)
        && wio(fd, wwv,  D * D, wr)
        && wio(fd, wwot, D * V, wr);
}

static int savew(void)
{
    int fd, ok;

    fd = open(WFILE, O_WRONLY | O_CREAT | O_TRUNC, 0);
    if (fd < 0)
        return ERROR;
    ok = wfile(fd, 1);
    close(fd);
    return ok ? 0 : ERROR;
}

/* load the six Q16 weight arrays from WFILE; 0 ok, ERROR on fail */
static int loadw(void)
{
    int fd, ok;

    fd = open(WFILE, O_RDONLY, 0);
    if (fd < 0)
        return ERROR;
    ok = wfile(fd, 0);
    close(fd);
    return ok ? 0 : ERROR;
}

/* ============================================================ */
/* Host stopwatch via Z80 port I/O (see port_drivers/time_io.c)  */
/*                                                               */
/* inp()/outp() are a dcc extension (not C89) for direct 8-bit   */
/* port I/O: inp() runs IN A,(port) and outp() runs OUT (port),A */
/* (only the low 8 bits of port are significant).  We drive the  */
/* same ports ATTNZ80.MAC uses:                                  */
/*                                                               */
/*   OUT 37,0 -> start/reset host stopwatch 0                    */
/*   OUT 37,1 -> latch elapsed seconds (4-byte big-endian long)  */
/*   IN  200  -> read those 4 bytes back, MSB first              */
/*                                                               */
/* The ports are no-ops under a bare CP/M emulator that lacks    */
/* them; the elapsed time is only meaningful on this project's   */
/* Altair emulator / ESP32 firmware.                             */
/* ============================================================ */

extern int  inp(unsigned port);
extern void outp(unsigned port, unsigned val);

#define SWPORT 37               /* host stopwatch 0         */
#define RDPORT 200              /* request-buffer read-back */

/* read the latched 4-byte big-endian elapsed seconds; low 16 bits */
static unsigned elapsed(void)
{
    int hi, lo;

    inp(RDPORT);                /* byte 0 (MSB) - ignore */
    inp(RDPORT);                /* byte 1       - ignore */
    hi = inp(RDPORT);           /* byte 2                */
    lo = inp(RDPORT);           /* byte 3 (LSB)          */
    return (unsigned)((hi << 8) | lo);
}

/* ============================================================ */
/* Entry point                                                  */
/* ============================================================ */

int main(int argc, char *argv[])
{
    int step, train, ns, vs;
    char *fname;

    train = 0;
    fname = IFILE;
    if (argc > 1) {
        if (strcmp(argv[1], "-t") == 0 || strcmp(argv[1], "-T") == 0)
            train = 1;
        else if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "-H") == 0 ||
                 strcmp(argv[1], "/?") == 0) {
            printf("attnc89 - tiny transformer that reverses 8 digits\n\n");
            printf("usage:\n");
            printf("  attnc89          infer from %s (one 8-digit line each)\n", IFILE);
            printf("  attnc89 <file>   infer from <file> instead\n");
            printf("  attnc89 -t       train, save weights to %s, then test\n", WFILE);
            printf("  attnc89 -h       this help\n");
            return 0;
        } else
            fname = argv[1];        /* explicit input file */
    }

    printf("attn/11 - paper tape is all you need\n");
    printf("d=16 seq=8 v=10 params=1216 q8/q15/q16\n\n");

    if (train) {
        printf("training...\n");
        rseed = 887;
        initw();
        zerog();
        thit = 0;
        ttot = 0;
        for (step = 1; step <= NSTEP; step++) {
            tstep = step;
            putchar('.');       /* heartbeat: one dot per completed step */
            gensm();
            trseq();
            if ((step % FSTEP) == 0)
                filrun(IFILE, 1);
            if ((step % RPRT) == 0) {
                report();
                vs = filrun(IFILE, 0);
                if (vs != ERROR) {
                    printf(" validation %2d/%d on %s\n", vhits, vs, IFILE);
                    if (vhits == vs && vs > 0) {
                        printf("validation passed; stopping early\n");
                        break;
                    }
                }
            }
        }
        printf("\nsaving weights to %s ...\n", WFILE);
        if (savew() != 0)
            printf("WARNING: could not save weights\n");
        printf("\n");
        test();
        ns = runfil(IFILE);
        if (ns != ERROR)
            printf("\naccuracy  %2d/%d on %s\n", fhits, ns, IFILE);
        return 0;
    }

    /* inference */
    printf("loading weights from %s ...\n", WFILE);
    if (loadw() != 0) {
        printf("no weights file found - run 'attnc89 -t' first\n");
        return 1;
    }
    cvt16();                        /* build Q8 weight copies once */

    outp(SWPORT, 0);                /* start host stopwatch 0 */
    ns = runfil(fname);
    if (ns == ERROR) {
        if (argc > 1 && fname != IFILE) {
            printf("cannot open input file %s\n", fname);
            return 1;
        }
        /* no input file present: fall back to a random demo */
        printf("no %s found - running random demo\n\n", IFILE);
        rseed = 1;
        test();
        return 0;
    }
    outp(SWPORT, 1);                /* latch elapsed seconds */

    printf("\naccuracy  %2d/%d\n", fhits, ns);
    printf("run time  %u s\n", elapsed());
    return 0;
}
