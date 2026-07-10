/*
 * WUMPUS for CP/M Z80 via dcc.
 */

#include <stdio.h>
#include <ctype.h>
#include <string.h>

#define RMCNT 20
#define TUNC  3
#define LOCC  6
#define ARRMX 5
#define ARRWS 5

#define NO  0
#define YES 1

#define EPLY 0
#define EWUM 1
#define EP1  2
#define EP2  3
#define EB1  4
#define EB2  5

#define TMOV 0
#define TSHO 1

#define OCON 0
#define OWIN 1
#define OLOS 2

typedef struct Gm {
    int loc[LOCC];
    int ini[LOCC];
    int arr;
    int cpu;
} Gm;

/*
 * Dodecahedron cave map: cave[room][tunnel] = destination room.
 * Flattened from the original nested {{ }} initializer so that
 * dcc's global-initializer parser (which handles one brace level)
 * can process it.  Row N starts at index N*TUNC.
 *   room 0  (unused sentinel): 0  0  0
 *   room 1:  2  5  8
 *   ...
 *   room 20: 13 16 19
 */
static int cave[RMCNT + 1][TUNC] = {
    0,  0,  0,
    2,  5,  8,
    1,  3, 10,
    2,  4, 12,
    3,  5, 14,
    1,  4,  6,
    5,  7, 15,
    6,  8, 17,
    1,  7,  9,
    8, 10, 18,
    2,  9, 11,
   10, 12, 19,
    3, 11, 13,
   12, 14, 20,
    4, 13, 15,
    6, 14, 16,
   15, 17, 20,
    7, 16, 18,
    9, 17, 19,
   11, 18, 20,
   13, 16, 19
};

static int seed = 317;

/* Forward declarations */
static int flsh(void);
static int prmt(char *s);
static int rnd16(void);
static int rndrm(void);
static int rndix(int n);
static int cpint(int *d, int *s, int n);
static int rdlin(char *buf, int siz);
static int rdchr(void);
static int rdnum(void);
static int adjo(int f, int t);
static int dupck(int *v, int n);
static int hpit(Gm *g, int r);
static int hbat(Gm *g, int r);
static int hwum(Gm *g, int r);
static int safeo(Gm *g, int r);
static int inst(void);
static int ginit(Gm *g);
static int gsame(Gm *g);
static int warns(Gm *g);
static int stats(Gm *g);
static int wwak(Gm *g);
static int presv(Gm *g);
static int pact(void);
static int movto(Gm *g, int dst);
static int pmove(Gm *g);
static int plen(void);
static int ppath(int *pth, int len);
static int ashot(Gm *g, int *pth, int len);
static int pshot(Gm *g);
static int pins(void);
static int psame(void);
static int rmove(Gm *g);
static int smove(Gm *g);
static int fwum(Gm *g, int *out);
static int cbfs(int src, int dst, int a1, int a2, int *path, int maxp);
static int cturn(Gm *g);
static int pargs(int ac, char **av, int *cpu);

static int flsh(void)
{
    fflush(stdout);
    return 0;
}

static int prmt(char *s)
{
    printf("%s", s);
    fflush(stdout);
    return 0;
}

static int rnd16(void)
{
    seed = seed * 25173 + 13849;
    return (seed >> 1) & 0x7fff;
}

static int rndrm(void)
{
    return (rnd16() % RMCNT) + 1;
}

static int rndix(int n)
{
    if (n <= 0)
        return 0;
    return rnd16() % n;
}

static int cpint(int *d, int *s, int n)
{
    int i;

    for (i = 0; i < n; ++i)
        d[i] = s[i];

    return 0;
}

static int rdlin(char *buf, int siz)
{
    int i;
    int ch;

    if (fgets(buf, siz, stdin) == 0) {
        buf[0] = '\0';
        return 0;
    }

    for (i = 0; buf[i] != '\0'; ++i) {
        ch = buf[i];
        if (ch == '\n' || ch == '\r') {
            buf[i] = '\0';
            break;
        }
    }

    return 0;
}

static int rdchr(void)
{
    char buf[64];
    int i, ch;

    rdlin(buf, sizeof(buf));

    for (i = 0; buf[i] != '\0'; ++i) {
        ch = buf[i];
        if (!isspace(ch))
            return toupper(ch);
    }

    return '\0';
}

static int rdnum(void)
{
    char buf[64];
    int i, ch, neg, val, got;

    for (;;) {
        rdlin(buf, sizeof(buf));

        i = 0;
        while (buf[i] != '\0' && isspace(buf[i]))
            ++i;

        neg = 0;
        if (buf[i] == '-') {
            neg = 1;
            ++i;
        }

        val = 0;
        got = 0;
        while (buf[i] != '\0') {
            ch = buf[i];
            if (!isdigit(ch))
                break;
            val = val * 10 + (ch - '0');
            got = 1;
            ++i;
        }

        if (got) {
            if (neg)
                val = -val;
            return val;
        }

        prmt("? ");
    }
}

static int adjo(int f, int t)
{
    int i;

    for (i = 0; i < TUNC; ++i) {
        if (cave[f][i] == t)
            return YES;
    }

    return NO;
}

static int dupck(int *v, int n)
{
    int i, j;

    for (i = 0; i < n; ++i)
        for (j = i + 1; j < n; ++j)
            if (v[i] == v[j])
                return YES;

    return NO;
}

static int hpit(Gm *g, int r)
{
    return (r == g->loc[EP1] || r == g->loc[EP2]);
}

static int hbat(Gm *g, int r)
{
    return (r == g->loc[EB1] || r == g->loc[EB2]);
}

static int hwum(Gm *g, int r)
{
    return (r == g->loc[EWUM]);
}

static int safeo(Gm *g, int r)
{
    return !hpit(g, r) && !hbat(g, r) && !hwum(g, r);
}

static int inst(void)
{
    printf("WELCOME TO 'HUNT THE WUMPUS'\n");
    printf("THE WUMPUS LIVES IN A CAVE OF 20 ROOMS. EACH ROOM\n");
    printf("HAS 3 TUNNELS LEADING TO OTHER ROOMS.\n");
    printf("\n");
    printf("HAZARDS:\n");
    printf("  BOTTOMLESS PITS - TWO ROOMS HAVE PITS.\n");
    printf("      GO THERE AND YOU LOSE.\n");
    printf("  SUPER BATS - TWO ROOMS HAVE BATS.\n");
    printf("      THEY MAY CARRY YOU TO A RANDOM ROOM.\n");
    printf("\n");
    printf("WUMPUS:\n");
    printf("  ENTER HIS ROOM OR SHOOT TO WAKE HIM.\n");
    printf("  WHEN WAKENED, HE MAY MOVE OR STAY PUT.\n");
    printf("  IF HE ENDS UP IN YOUR ROOM, YOU LOSE.\n");
    printf("\n");
    printf("YOU:\n");
    printf("  EACH TURN YOU MAY MOVE OR SHOOT.\n");
    printf("  YOU HAVE 5 ARROWS.\n");
    printf("  EACH ARROW MAY TRAVEL THROUGH 1 TO 5 ROOMS.\n");
    printf("  IF AN ARROW HITS THE WUMPUS, YOU WIN.\n");
    printf("  IF IT HITS YOU, YOU LOSE.\n");
    printf("\n");
    printf("WARNINGS:\n");
    printf("  WUMPUS - I SMELL A WUMPUS\n");
    printf("  BATS   - BATS NEARBY\n");
    printf("  PIT    - I FEEL A DRAFT\n");
    printf("\n");
    flsh();
    return 0;
}

static int ginit(Gm *g)
{
    int i;

    do {
        for (i = 0; i < LOCC; ++i)
            g->loc[i] = rndrm();
    } while (dupck(g->loc, LOCC));

    cpint(g->ini, g->loc, LOCC);
    g->arr = ARRWS;
    return 0;
}

static int gsame(Gm *g)
{
    cpint(g->loc, g->ini, LOCC);
    g->arr = ARRWS;
    return 0;
}

static int warns(Gm *g)
{
    int rm, i, a;

    rm = g->loc[EPLY];

    for (i = 0; i < TUNC; ++i) {
        a = cave[rm][i];
        if (a == g->loc[EWUM])
            printf("I SMELL A WUMPUS!\n");
        if (a == g->loc[EP1] || a == g->loc[EP2])
            printf("I FEEL A DRAFT!\n");
        if (a == g->loc[EB1] || a == g->loc[EB2])
            printf("BATS NEARBY!\n");
    }

    return 0;
}

static int stats(Gm *g)
{
    int rm;

    rm = g->loc[EPLY];
    printf("\n");
    warns(g);
    printf("YOU ARE IN ROOM %d\n", rm);
    printf("TUNNELS LEAD TO %d, %d, AND %d\n",
           cave[rm][0], cave[rm][1], cave[rm][2]);
    printf("ARROWS LEFT: %d\n", g->arr);
    printf("\n");
    flsh();
    return 0;
}

static int wwak(Gm *g)
{
    int mv, wr;

    mv = rndix(4);
    if (mv < 3) {
        wr = g->loc[EWUM];
        g->loc[EWUM] = cave[wr][mv];
    }

    if (g->loc[EWUM] == g->loc[EPLY]) {
        printf("TSK TSK TSK - WUMPUS GOT YOU!\n");
        flsh();
        return OLOS;
    }

    return OCON;
}

static int presv(Gm *g)
{
    int rm;

    for (;;) {
        rm = g->loc[EPLY];

        if (rm == g->loc[EWUM]) {
            printf("... OOPS! BUMPED A WUMPUS!\n");
            flsh();
            return wwak(g);
        }

        if (rm == g->loc[EP1] || rm == g->loc[EP2]) {
            printf("YYYIIIIEEEE . . . FELL IN PIT\n");
            flsh();
            return OLOS;
        }

        if (rm == g->loc[EB1] || rm == g->loc[EB2]) {
            printf("ZAP--SUPER BAT SNATCH! ELSEWHEREVILLE FOR YOU!\n");
            flsh();
            g->loc[EPLY] = rndrm();
            continue;
        }

        return OCON;
    }
}

static int pact(void)
{
    int ch;

    for (;;) {
        prmt("SHOOT OR MOVE (S-M) ");
        ch = rdchr();
        if (ch == 'S')
            return TSHO;
        if (ch == 'M')
            return TMOV;
    }
}

static int movto(Gm *g, int dst)
{
    g->loc[EPLY] = dst;
    return presv(g);
}

static int pmove(Gm *g)
{
    int dst;

    for (;;) {
        prmt("WHERE TO ");
        dst = rdnum();

        if (dst < 1 || dst > RMCNT) {
            printf("NOT POSSIBLE -\n");
            flsh();
            continue;
        }

        if (!adjo(g->loc[EPLY], dst)) {
            printf("NOT POSSIBLE -\n");
            flsh();
            continue;
        }

        break;
    }

    return movto(g, dst);
}

static int plen(void)
{
    int n;

    for (;;) {
        prmt("NO. OF ROOMS (1-5) ");
        n = rdnum();
        if (n >= 1 && n <= ARRMX)
            return n;
    }
}

static int ppath(int *pth, int len)
{
    int i;

    for (i = 0; i < len; ++i) {
        for (;;) {
            printf("ROOM #%d ", i + 1);
            flsh();
            pth[i] = rdnum();

            if (pth[i] < 1 || pth[i] > RMCNT)
                continue;

            if (i >= 2 && pth[i] == pth[i - 2]) {
                printf("ARROWS AREN'T THAT CROOKED - TRY ANOTHER ROOM\n");
                flsh();
                continue;
            }

            break;
        }
    }

    return 0;
}

static int ashot(Gm *g, int *pth, int len)
{
    int ar, i;

    ar = g->loc[EPLY];

    for (i = 0; i < len; ++i) {
        if (adjo(ar, pth[i]))
            ar = pth[i];
        else
            ar = cave[ar][rndix(TUNC)];

        if (ar == g->loc[EWUM]) {
            printf("AHA! YOU GOT THE WUMPUS!\n");
            flsh();
            return OWIN;
        }

        if (ar == g->loc[EPLY]) {
            printf("OUCH! ARROW GOT YOU!\n");
            g->arr = g->arr - 1;
            flsh();
            return OLOS;
        }
    }

    g->arr = g->arr - 1;
    printf("MISSED\n");
    flsh();

    if (g->arr <= 0) {
        printf("YOU RAN OUT OF ARROWS!\n");
        flsh();
        return OLOS;
    }

    return wwak(g);
}

static int pshot(Gm *g)
{
    int pth[ARRMX];
    int len;

    len = plen();
    ppath(pth, len);
    return ashot(g, pth, len);
}

static int pins(void)
{
    prmt("INSTRUCTIONS (Y-N) ");
    return (rdchr() != 'N');
}

static int psame(void)
{
    prmt("SAME SET-UP (Y-N) ");
    return (rdchr() == 'Y');
}

static int rmove(Gm *g)
{
    int rm;

    rm = g->loc[EPLY];
    return cave[rm][rndix(TUNC)];
}

static int smove(Gm *g)
{
    int rm, lst[TUNC], cnt, i, r;

    rm = g->loc[EPLY];
    cnt = 0;

    for (i = 0; i < TUNC; ++i) {
        r = cave[rm][i];
        if (safeo(g, r))
            lst[cnt++] = r;
    }

    if (cnt == 0)
        return 0;

    return lst[rndix(cnt)];
}

static int fwum(Gm *g, int *out)
{
    int rm, i, r;

    rm = g->loc[EPLY];

    for (i = 0; i < TUNC; ++i) {
        r = cave[rm][i];
        if (r == g->loc[EWUM]) {
            *out = r;
            return YES;
        }
    }

    return NO;
}

/*
 * cbfs: BFS shortest path from src to dst avoiding rooms a1 and a2
 * (intermediate rooms only; dst itself is always allowed).
 * Fills path[] with the room sequence src+1 .. dst.
 * Returns path length, or 0 if unreachable or length > maxp.
 */
static int cbfs(int src, int dst, int a1, int a2, int *path, int maxp)
{
    static int prev[RMCNT + 1];
    static int q[RMCNT + 1];
    int qh, qt, r, j, k, nb, len, c;

    for (r = 0; r <= RMCNT; ++r)
        prev[r] = 0;

    qh = qt = 0;
    prev[src] = src;
    q[qt++] = src;

    while (qh < qt) {
        r = q[qh++];
        for (j = 0; j < TUNC; ++j) {
            nb = cave[r][j];
            if (prev[nb])
                continue;
            if (nb != dst && (nb == a1 || nb == a2))
                continue;
            prev[nb] = r;
            if (nb == dst) {
                len = 0;
                c = nb;
                while (c != src) {
                    ++len;
                    c = prev[c];
                }
                if (len > maxp)
                    return 0;
                c = nb;
                for (k = len - 1; k >= 0; --k) {
                    path[k] = c;
                    c = prev[c];
                }
                return len;
            }
            q[qt++] = nb;
        }
    }
    return 0;
}

static int cturn(Gm *g)
{
    int pth[ARRMX];
    int len, dst, i;

    /* Wumpus is adjacent: shoot it directly (1 hop). */
    if (g->arr > 0 && fwum(g, pth)) {
        printf("COMPUTER CHOOSES: SHOOT\n");
        printf("COMPUTER ARROW: %d\n", pth[0]);
        flsh();
        return ashot(g, pth, 1);
    }

    /* Plan the shortest shot path to the wumpus (up to ARRMX hops).
     * The dodecahedron has diameter 5 = ARRMX, so this always
     * succeeds while arrows remain. */
    if (g->arr > 0) {
        len = cbfs(g->loc[EPLY], g->loc[EWUM], 0, 0, pth, ARRMX);
        if (len > 0) {
            printf("COMPUTER CHOOSES: SHOOT\n");
            printf("COMPUTER ARROW:");
            for (i = 0; i < len; ++i)
                printf(" %d", pth[i]);
            printf("\n");
            flsh();
            return ashot(g, pth, len);
        }
    }

    /* No arrows left: navigate toward the wumpus, avoiding pits. */
    len = cbfs(g->loc[EPLY], g->loc[EWUM],
               g->loc[EP1], g->loc[EP2], pth, RMCNT);
    if (len > 1) {
        dst = pth[0];
        printf("COMPUTER CHOOSES: MOVE\n");
        printf("COMPUTER MOVES TO ROOM %d\n", dst);
        flsh();
        return movto(g, dst);
    }

    /* Fallback: safe random move, or any random move. */
    dst = smove(g);
    if (dst == 0)
        dst = rmove(g);
    printf("COMPUTER CHOOSES: MOVE\n");
    printf("COMPUTER MOVES TO ROOM %d\n", dst);
    flsh();
    return movto(g, dst);
}

static int pargs(int ac, char **av, int *cpu)
{
    int i;

    *cpu = NO;

    for (i = 1; i < ac; ++i) {
        if (stricmp(av[i], "-c") == 0 || stricmp(av[i], "C") == 0)
            *cpu = YES;
        else {
            printf("USAGE: %s [-c]\n", av[0]);
            flsh();
            return NO;
        }
    }

    return YES;
}

int main(int ac, char **av)
{
    Gm g;
    int out, act;

    if (!pargs(ac, av, &g.cpu))
        return 1;

    printf("            WUMPUS\n");
    printf(" CREATIVE COMPUTING MORRISTOWN, NJ\n");
    printf("\n");
    flsh();

    ginit(&g);

    if (!g.cpu) {
        if (pins())
            inst();
    }

    for (;;) {
        out = OCON;

        printf("HUNT THE WUMPUS\n");
        flsh();

        while (out == OCON) {
            stats(&g);

            if (g.cpu)
                out = cturn(&g);
            else {
                act = pact();
                if (act == TMOV)
                    out = pmove(&g);
                else
                    out = pshot(&g);
            }
        }

        if (out == OWIN)
            printf("HEE HEE HEE - THE WUMPUS'LL GETCHA NEXT TIME!!\n");
        else
            printf("HA HA HA - YOU LOSE!\n");
        flsh();

        if (g.cpu)
            break;

        if (psame())
            gsame(&g);
        else
            ginit(&g);
    }

    return 0;
}
