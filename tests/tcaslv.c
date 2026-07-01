#include <stdio.h>

int fails = 0;

void chk(const char *e, int got, int exp) {
    if (got != exp) { printf("FAIL %s = %d (exp %d)\n", e, got, exp); fails = 1; }
    else printf("PASS %s = %d\n", e, exp);
}

struct S { int x; char c; };

int main(void)
{
    int a; int b; int r;
    int arr[4];
    int *p;
    struct S s;
    char carr[4];

    /* ---- compound assign, VALUE USED, through a plain ident ---- */
    a = 20; r = (a += 5);  chk("(a+=5) val", r, 25);  chk("(a+=5) a", a, 25);
    a = 20; r = (a -= 5);  chk("(a-=5) val", r, 15);  chk("(a-=5) a", a, 15);
    a = 6;  r = (a *= 3);  chk("(a*=3) val", r, 18);  chk("(a*=3) a", a, 18);
    a = 20; r = (a /= 4);  chk("(a/=4) val", r, 5);   chk("(a/=4) a", a, 5);
    a = 23; r = (a %= 5);  chk("(a%=5) val", r, 3);   chk("(a%=5) a", a, 3);
    a = 12; r = (a &= 10); chk("(a&=10) val", r, 8);  chk("(a&=10) a", a, 8);
    a = 12; r = (a |= 1);  chk("(a|=1) val", r, 13);  chk("(a|=1) a", a, 13);
    a = 12; r = (a ^= 5);  chk("(a^=5) val", r, 9);   chk("(a^=5) a", a, 9);
    a = 3;  r = (a <<= 2); chk("(a<<=2) val", r, 12); chk("(a<<=2) a", a, 12);
    a = 40; r = (a >>= 2); chk("(a>>=2) val", r, 10); chk("(a>>=2) a", a, 10);

    /* ---- compound assign, VALUE USED, through a pointer deref ---- */
    b = 20; p = &b; r = (*p += 5);  chk("(*p+=5) val", r, 25);  chk("(*p+=5) b", b, 25);
    b = 20; p = &b; r = (*p -= 5);  chk("(*p-=5) val", r, 15);  chk("(*p-=5) b", b, 15);
    b = 6;  p = &b; r = (*p *= 3);  chk("(*p*=3) val", r, 18);  chk("(*p*=3) b", b, 18);
    b = 20; p = &b; r = (*p /= 4);  chk("(*p/=4) val", r, 5);   chk("(*p/=4) b", b, 5);
    b = 23; p = &b; r = (*p %= 5);  chk("(*p%=5) val", r, 3);   chk("(*p%=5) b", b, 3);
    b = 12; p = &b; r = (*p &= 10); chk("(*p&=10) val", r, 8);  chk("(*p&=10) b", b, 8);
    b = 12; p = &b; r = (*p |= 1);  chk("(*p|=1) val", r, 13);  chk("(*p|=1) b", b, 13);
    b = 12; p = &b; r = (*p ^= 5);  chk("(*p^=5) val", r, 9);   chk("(*p^=5) b", b, 9);
    b = 3;  p = &b; r = (*p <<= 2); chk("(*p<<=2) val", r, 12); chk("(*p<<=2) b", b, 12);
    b = 40; p = &b; r = (*p >>= 2); chk("(*p>>=2) val", r, 10); chk("(*p>>=2) b", b, 10);

    /* ---- compound assign, VALUE USED, through an array index ---- */
    arr[1] = 20; r = (arr[1] += 5); chk("(arr+=5) val", r, 25); chk("(arr+=5)", arr[1], 25);
    arr[1] = 6;  r = (arr[1] *= 3); chk("(arr*=3) val", r, 18); chk("(arr*=3)", arr[1], 18);
    arr[1] = 20; r = (arr[1] /= 4); chk("(arr/=4) val", r, 5);  chk("(arr/=4)", arr[1], 5);
    arr[1] = 12; r = (arr[1] &= 10);chk("(arr&=10) val", r, 8); chk("(arr&=10)", arr[1], 8);
    arr[1] = 3;  r = (arr[1] <<= 2);chk("(arr<<=2) val", r, 12);chk("(arr<<=2)", arr[1], 12);

    /* ---- compound assign, VALUE USED, through a struct member ---- */
    s.x = 20; r = (s.x += 5); chk("(s.x+=5) val", r, 25); chk("(s.x+=5)", s.x, 25);
    s.x = 6;  r = (s.x *= 3); chk("(s.x*=3) val", r, 18); chk("(s.x*=3)", s.x, 18);
    s.x = 20; r = (s.x /= 4); chk("(s.x/=4) val", r, 5);  chk("(s.x/=4)", s.x, 5);
    s.x = 12; r = (s.x |= 1); chk("(s.x|=1) val", r, 13); chk("(s.x|=1)", s.x, 13);

    /* keep carr referenced so its stack slot participates in layout */
    carr[0] = 7; chk("carr0", (int)carr[0], 7);

    /* ---- chained plain assignment value-used ---- */
    a = b = 9; chk("a=b=9 a", a, 9); chk("a=b=9 b", b, 9);
    a = (b = 4) + 1; chk("(b=4)+1", a, 5); chk("b after", b, 4);

    /* ---- plain assignment to non-ident lvalue, VALUE USED ---- */
    b = 0; p = &b; r = (*p = 7);       chk("(*p=7) val", r, 7);   chk("(*p=7) b", b, 7);
    arr[2] = 0; r = (arr[2] = 8);      chk("(arr=8) val", r, 8);  chk("(arr=8)", arr[2], 8);
    s.x = 0; r = (s.x = 9);            chk("(s.x=9) val", r, 9);  chk("(s.x=9)", s.x, 9);
    b = 0; p = &b; a = (*p = 3) + 4;   chk("(*p=3)+4", a, 7);     chk("(*p=3) b", b, 3);

    if (fails) return 1;
    printf("tcaslv completed with great success\n");
    return 0;
}
