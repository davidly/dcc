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

struct S { int x; int y; };

int gb;
int garr[4];
int *gp;

int main(void)
{
    int b;
    int r;
    int arr[4];
    int *p;
    int *q;
    struct S s;

    /* --- &*p collapses to p --- */
    b = 42;
    p = &b;
    q = &*p;                    /* q == p == &b */
    chki("*(&*p)", *(&*p), 42);
    chki("q=&*p then *q", *q, 42);
    chki("q==p", (q == p), 1);

    /* &*p used to write through */
    *(&*p) = 7;                 /* b = 7 */
    chki("b after *(&*p)=7", b, 7);

    /* &*p as function-argument pointer (identity) */
    q = &*(&*p);                /* still &b */
    chki("nested &*&* deref", *q, 7);

    /* --- &*(p + i) : address arithmetic through deref --- */
    arr[0] = 10; arr[1] = 20; arr[2] = 30; arr[3] = 40;
    p = arr;
    q = &*(p + 2);              /* &arr[2] */
    chki("&*(p+2) deref", *q, 30);
    *(&*(p + 1)) = 99;          /* arr[1] = 99 */
    chki("arr[1] after &*(p+1)=99", arr[1], 99);

    /* --- &*p on globals --- */
    gb = 123;
    gp = &gb;
    q = &*gp;
    chki("global &*gp", *q, 123);
    *(&*gp) = 456;
    chki("gb after *(&*gp)=456", gb, 456);

    /* --- &*(struct-member-pointer) --- */
    s.x = 5; s.y = 6;
    p = &s.x;
    q = &*p;                    /* &s.x */
    chki("&*(&s.x)", *q, 5);
    *(&*p) = 77;
    chki("s.x after *(&*p)=77", s.x, 77);

    /* --- difference of two &* addresses (element distance) --- */
    p = &arr[0];
    chki("&*(p+3) - &*p", (int)((&*(p + 3)) - (&*p)), 3);

    if (fails) return 1;

    printf("taddr completed with great success\n");
    return 0;
}
