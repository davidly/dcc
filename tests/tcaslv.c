#include <stdio.h>

int fails = 0;

void chk(const char *e, int got, int exp) {
    if (got != exp) { printf("FAIL %s = %d (exp %d)\n", e, got, exp); fails = 1; }
    else printf("PASS %s = %d\n", e, exp);
}

struct S { int x; char c; };

static int global_lhs;
static int global_rhs;
static signed char global_schar;
static unsigned char global_uchar;
static _Bool global_bool;
static unsigned int global_uint;
static long global_long_lhs;
static long global_long_rhs;
static float global_float_lhs;
static float global_float_rhs;
static int global_array[8];
static int *global_ptr;
static unsigned char global_observed;

static void check_global_compound_param(int rhs)
{
    global_lhs = 40;
    global_lhs += rhs;
    chk("global += param", global_lhs, 39);
}

static int apply_global_compound_param(int rhs)
{
    global_lhs += rhs;
    return global_lhs;
}

static int apply_local_compound_repeated_param(int rhs)
{
    int value;

    value = 40;
    value += rhs;
    value += rhs;
    value += rhs;
    value += rhs;
    return value;
}

static void check_global_compound_assign(void)
{
    unsigned char previous;
    int local;

    global_lhs = 40;
    global_rhs = -1;
    previous = (unsigned char)global_lhs;
    global_lhs += global_rhs;
    global_observed = previous;
    chk("global += global", global_lhs, 39);
    chk("global += global local", global_observed, 40);

    global_lhs = 40;
    global_rhs = 1;
    global_lhs -= global_rhs;
    chk("global -= global", global_lhs, 39);

    global_lhs = 40;
    local = -1;
    global_lhs += local;
    chk("global += local", global_lhs, 39);

    local = 40;
    global_rhs = -1;
    local += global_rhs;
    chk("local += global", local, 39);

    global_lhs = 40;
    global_rhs = -1;
    global_lhs = global_lhs + global_rhs;
    chk("global = global + global", global_lhs, 39);

    global_lhs = 40;
    global_schar = -1;
    global_lhs += global_schar;
    chk("global += signed char global", global_lhs, 39);

    global_lhs = 40;
    global_uchar = 2;
    global_lhs += global_uchar;
    chk("global += unsigned char global", global_lhs, 42);

    global_lhs = 40;
    global_bool = 1;
    global_lhs += global_bool;
    chk("global += bool global", global_lhs, 41);

    global_schar = 40;
    global_rhs = -1;
    global_schar += global_rhs;
    chk("signed char global += global", global_schar, 39);
    local = (global_schar += global_rhs);
    chk("(signed char global += global) value", local, 38);

    global_uint = 40;
    global_rhs = -1;
    global_uint += global_rhs;
    chk("unsigned global += signed global", (int)global_uint, 39);

    global_lhs = 20;
    global_rhs = 3;
    global_lhs *= global_rhs;
    chk("global *= global", global_lhs, 60);
    global_lhs /= global_rhs;
    chk("global /= global", global_lhs, 20);
    global_lhs %= global_rhs;
    chk("global %= global", global_lhs, 2);
    global_lhs = 12;
    global_lhs &= global_rhs;
    chk("global &= global", global_lhs, 0);
    global_lhs = 12;
    global_lhs |= global_rhs;
    chk("global |= global", global_lhs, 15);
    global_lhs ^= global_rhs;
    chk("global ^= global", global_lhs, 12);
    global_lhs = 3;
    global_rhs = 2;
    global_lhs <<= global_rhs;
    chk("global <<= global", global_lhs, 12);
    global_lhs >>= global_rhs;
    chk("global >>= global", global_lhs, 3);

    global_lhs = 40;
    global_rhs = -1;
    local = (global_lhs += global_rhs);
    chk("(global += global) value", local, 39);
    local = (global_lhs -= global_rhs);
    chk("(global -= global) value", local, 40);

    global_ptr = &global_array[2];
    global_rhs = 2;
    global_ptr += global_rhs;
    chk("global pointer += global", global_ptr == &global_array[4], 1);
    global_ptr -= global_rhs;
    chk("global pointer -= global", global_ptr == &global_array[2], 1);

    global_long_lhs = 40L;
    global_long_rhs = -1L;
    global_long_lhs += global_long_rhs;
    chk("long global += global", global_long_lhs == 39L, 1);
    local = (global_long_lhs -= global_long_rhs) == 40L;
    chk("(long global -= global) value", local, 1);

    global_float_lhs = 40.0f;
    global_float_rhs = -1.0f;
    global_float_lhs += global_float_rhs;
    chk("float global += global", global_float_lhs == 39.0f, 1);
    local = (global_float_lhs -= global_float_rhs) == 40.0f;
    chk("(float global -= global) value", local, 1);

    check_global_compound_param(-1);
    global_lhs = 40;
    chk("global += no-ix param", apply_global_compound_param(-1), 39);
    chk("local += repeated param", apply_local_compound_repeated_param(-1), 36);
}

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

    check_global_compound_assign();

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
