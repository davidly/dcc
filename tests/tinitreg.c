/* focused initializer regression tests for dcc */

#include <stdio.h>

static int fails;

static int garr1[5] = { 1, 2 };
static int gmat1[2][3] = { { 1, 2 }, { 3 } };
static char gstr1[8] = "abc";
static char gstr2[] = "ab" "cd";
static long glng1[3] = { 0x00010000L };
static int gscl1 = { 1234 };

struct Dist {
    long d[4][4];
};

struct Deep {
    int e[2][2][2];
};

static void cki(const char *n, int g, int e)
{
    if (g != e) {
        printf("FAIL %s got %d expected %d\n", n, g, e);
        fails++;
    }
}

static void ckul(const char *n, unsigned long g, unsigned long e)
{
    if (g != e) {
        printf("FAIL %s got %lu expected %lu\n", n, g, e);
        fails++;
    }
}

static void tglob(void)
{
    cki("garr1 0", garr1[0], 1);
    cki("garr1 1", garr1[1], 2);
    cki("garr1 2", garr1[2], 0);
    cki("garr1 4", garr1[4], 0);

    cki("gmat1 00", gmat1[0][0], 1);
    cki("gmat1 01", gmat1[0][1], 2);
    cki("gmat1 02", gmat1[0][2], 0);
    cki("gmat1 10", gmat1[1][0], 3);
    cki("gmat1 11", gmat1[1][1], 0);
    cki("gmat1 12", gmat1[1][2], 0);

    cki("gstr1 nul", gstr1[3], 0);
    cki("gstr1 tail", gstr1[7], 0);
    cki("gstr2 size", sizeof(gstr2), 5);
    cki("gstr2 d", gstr2[3], 'd');
    cki("gstr2 nul", gstr2[4], 0);

    ckul("glng1 0", (unsigned long)glng1[0], 0x00010000UL);
    ckul("glng1 1", (unsigned long)glng1[1], 0UL);
    cki("gscl1", gscl1, 1234);
}

static void tauto(void)
{
    int a[5] = { 1, 2 };
    int m[2][3] = { { 1, 2 }, { 3 } };
    char s[8] = "abc";
    char t[] = "xy" "z";
    long l[3] = { 0x00010000L };
    int x = { 4321 };

    cki("a 0", a[0], 1);
    cki("a 1", a[1], 2);
    cki("a 2", a[2], 0);
    cki("a 4", a[4], 0);

    cki("m 00", m[0][0], 1);
    cki("m 01", m[0][1], 2);
    cki("m 02", m[0][2], 0);
    cki("m 10", m[1][0], 3);
    cki("m 11", m[1][1], 0);
    cki("m 12", m[1][2], 0);

    cki("s nul", s[3], 0);
    cki("s tail", s[7], 0);
    cki("t size", sizeof(t), 4);
    cki("t z", t[2], 'z');
    cki("t nul", t[3], 0);

    ckul("l 0", (unsigned long)l[0], 0x00010000UL);
    ckul("l 1", (unsigned long)l[1], 0UL);
    cki("x", x, 4321);
}

/* Nested aggregate initializer for a struct whose member is a 2D/3D array.
 * Covers every brace spelling clang accepts (values verified at run time):
 *   A  full braces           {{ {..},{..} }}
 *   B  member brace, flat     {{ .. }}
 *   C  full brace elision     { .. }
 *   partial rows -> zero fill {{ {1}, {3} }}
 *   3D  fully nested          {{ {{..},{..}}, ... }}
 */
static void tnest(void)
{
    struct Dist a = {{
        { 0, 3, 99, 7 },
        { 8, 0, 2, 99 },
        { 5, 99, 0, 1 },
        { 2, 99, 99, 0 }
    }};
    struct Dist b = {{ 10, 20, 30, 40, 50, 60, 70, 80,
                       90, 100, 110, 120, 130, 140, 150, 160 }};
    struct Dist c = { 1, 2, 3, 4 };
    struct Dist p = {{ { 1 }, { 3 } }};
    struct Deep q = {{ { { 1, 2 }, { 3, 4 } }, { { 5, 6 }, { 7, 8 } } }};

    ckul("a d03", (unsigned long)a.d[0][3], 7UL);
    ckul("a d10", (unsigned long)a.d[1][0], 8UL);
    ckul("a d23", (unsigned long)a.d[2][3], 1UL);
    ckul("a d33", (unsigned long)a.d[3][3], 0UL);

    ckul("b d00", (unsigned long)b.d[0][0], 10UL);
    ckul("b d33", (unsigned long)b.d[3][3], 160UL);

    ckul("c d00", (unsigned long)c.d[0][0], 1UL);
    ckul("c d03", (unsigned long)c.d[0][3], 4UL);
    ckul("c d10", (unsigned long)c.d[1][0], 0UL);
    ckul("c d33", (unsigned long)c.d[3][3], 0UL);

    ckul("p d00", (unsigned long)p.d[0][0], 1UL);
    ckul("p d01", (unsigned long)p.d[0][1], 0UL);
    ckul("p d10", (unsigned long)p.d[1][0], 3UL);
    ckul("p d33", (unsigned long)p.d[3][3], 0UL);

    cki("q 000", q.e[0][0][0], 1);
    cki("q 011", q.e[0][1][1], 4);
    cki("q 100", q.e[1][0][0], 5);
    cki("q 111", q.e[1][1][1], 8);
}

int main(void)
{
    tglob();
    tauto();
    tnest();

    if (fails) {
        printf("tinitreg: %d failure(s)\n", fails);
        return 1;
    }

    printf("tinitreg: all tests passed\n");
    return 0;
}
