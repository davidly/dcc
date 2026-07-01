#include <stdio.h>

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

int garr[8];
long glarr[6];

int main(void)
{
    int a[8];
    char ca[8];
    long la[6];
    int *p;
    char *cp;
    long *lp;
    int i;

    for (i = 0; i < 8; ++i) { a[i] = i * 10; ca[i] = (char)(i + 1); }
    for (i = 0; i < 6; ++i) la[i] = (long)i * 1000L;

    /* --- negative constant subscript through an int pointer --- */
    p = &a[5];
    chki("p[-1]", p[-1], 40);
    chki("p[-3]", p[-3], 20);
    chki("p[-5]", p[-5], 0);
    chki("p[0]", p[0], 50);
    chki("p[2]", p[2], 70);

    /* write through negative subscript */
    p[-2] = 999;               /* a[3] = 999 */
    chki("a[3] after p[-2]=999", a[3], 999);

    /* --- negative subscript through a char pointer (elem size 1) --- */
    cp = &ca[6];
    chki("cp[-1]", (int)cp[-1], 6);
    chki("cp[-4]", (int)cp[-4], 3);
    cp[-2] = 88;               /* ca[4] = 88 */
    chki("ca[4] after cp[-2]=88", (int)ca[4], 88);

    /* --- negative subscript through a long pointer (elem size 4) --- */
    lp = &la[4];
    chkl("lp[-1]", lp[-1], 3000L);
    chkl("lp[-4]", lp[-4], 0L);
    lp[-3] = 77777L;           /* la[1] = 77777 */
    chkl("la[1] after lp[-3]", la[1], 77777L);

    /* --- negative subscript on globals --- */
    for (i = 0; i < 8; ++i) garr[i] = i + 100;
    p = &garr[7];
    chki("garr p[-2]", p[-2], 105);
    for (i = 0; i < 6; ++i) glarr[i] = (long)i * 10L;
    lp = &glarr[5];
    chkl("glarr lp[-5]", lp[-5], 0L);

    /* --- unary + and ~ constant subscripts --- */
    p = &a[3];
    chki("p[+1]", p[+1], a[4]);
    chki("p[~0]", p[~0], a[2]);   /* ~0 == -1 */

    if (fails) return 1;

    printf("tnegidx completed with great success\n");
    return 0;
}
