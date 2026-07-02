#include <stdio.h>
#include <stdint.h>

int fails = 0;

void chki(const char *expr, int result, int expected) {
    if (result != expected) {
        printf("FAIL: %s = %d (expected %d)\n", expr, result, expected);
        fails = 1;
    } else {
        printf("PASS: %s = %d\n", expr, expected);
    }
}

void chkl(const char *expr, long result, long expected) {
    if (result != expected) {
        printf("FAIL: %s = %ld (expected %ld)\n", expr, result, expected);
        fails = 1;
    } else {
        printf("PASS: %s = %ld\n", expr, expected);
    }
}

struct S { int a; char c; };

int main(void)
{
    int v;
    int r;
    int arr[4];
    char carr[4];
    long larr[3];
    int *p;
    char *cp;
    long *lp;
    struct S s;

    /* --- prefix ++/-- through a pointer deref, value used --- */
    v = 10;
    p = &v;
    r = ++*p;               /* v becomes 11, expression yields 11 */
    chki("r = ++*p", r, 11);
    chki("v after ++*p", v, 11);

    r = --*p;               /* v becomes 10, expression yields 10 */
    chki("r = --*p", r, 10);
    chki("v after --*p", v, 10);

    r = ++(*p) + 1;         /* v -> 11, expr -> 12 */
    chki("r = ++(*p)+1", r, 12);
    chki("v after ++(*p)+1", v, 11);

    /* value stored via assignment from a prefix-deref */
    {
        int x;
        v = 20; p = &v;
        x = ++*p;           /* v -> 21, x -> 21 */
        chki("x = ++*p", x, 21);
        chki("v after x=++*p", v, 21);
    }

    /* char deref */
    {
        char cv;
        cv = 5; cp = &cv;
        r = ++*cp;          /* cv -> 6, expr -> 6 */
        chki("r = ++*cp", r, 6);
        chki("cv after ++*cp", (int)cv, 6);
    }

    /* --- prefix ++/-- through an array index, value used --- */
    arr[0] = 100; arr[1] = 200; arr[2] = 300; arr[3] = 400;
    r = ++arr[1];           /* arr[1] -> 201, expr -> 201 */
    chki("r = ++arr[1]", r, 201);
    chki("arr[1] after ++arr[1]", arr[1], 201);

    r = --arr[2];           /* arr[2] -> 299, expr -> 299 */
    chki("r = --arr[2]", r, 299);
    chki("arr[2] after --arr[2]", arr[2], 299);

    r = ++arr[0] + ++arr[3];/* arr[0]->101, arr[3]->401, expr -> 502 */
    chki("r = ++arr[0]+++arr[3]", r, 502);
    chki("arr[0] after", arr[0], 101);
    chki("arr[3] after", arr[3], 401);

    /* char array index */
    carr[0] = 40; carr[1] = 41;
    r = ++carr[0];          /* carr[0] -> 41, expr -> 41 */
    chki("r = ++carr[0]", r, 41);
    chki("carr[0] after", (int)carr[0], 41);

    /* --- prefix ++/-- through a struct member, value used --- */
    s.a = 7; s.c = 3;
    r = ++s.a;              /* s.a -> 8, expr -> 8 */
    chki("r = ++s.a", r, 8);
    chki("s.a after ++s.a", s.a, 8);

    /* --- prefix ++/-- on a plain identifier, value used --- */
    v = 0;
    r = ++v;                /* v -> 1, expr -> 1 */
    chki("r = ++v", r, 1);
    r = --v;                /* v -> 0, expr -> 0 */
    chki("r = --v", r, 0);

    /* --- statement-context (result discarded) still works --- */
    v = 50; p = &v;
    ++*p;                   /* v -> 51 */
    chki("v after stmt ++*p", v, 51);
    ++arr[1];               /* arr[1] -> 202 */
    chki("arr[1] after stmt ++arr[1]", arr[1], 202);

    /* --- long array index (element size 4) statement + value --- */
    larr[0] = 1000L; larr[1] = 2000L; larr[2] = 3000L;
    ++larr[0];              /* long array index, discarded */
    chkl("larr[0] after ++larr[0]", larr[0], 1001L);

    if (fails) return 1;

    printf("tpreinc completed with great success\n");
    return 0;
}
