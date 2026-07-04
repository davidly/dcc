/* tc99apar.c - C99 array parameter qualifier regressions. */
#include <stdio.h>

static int failures;

static void check(const char *name, int got, int want)
{
    if (got != want) {
        printf("FAIL %s got=%d want=%d\n", name, got, want);
        failures++;
    }
}

void proto_named_const(int x[const 5]);
void proto_named_static(int x[static 5]);
void proto_named_volatile(int x[volatile 5]);
void proto_named_restrict(int x[restrict 5]);
void proto_named_mix1(int x[static restrict const 5]);
void proto_named_mix2(int x[const volatile restrict static 5]);
void proto_unnamed_const(int [const 5]);
void proto_unnamed_static(int [static 5]);
void proto_unnamed_volatile(int [volatile 5]);
void proto_unnamed_restrict(int [restrict 5]);
void proto_unspecified_vla(int [const *]);
void proto_callback(int (*cb)(int [const 3]), int data[static 3]);
void proto_paren_ptr_const(int (* const));
void proto_paren_ptr_volatile(int (* volatile));
void proto_paren_ptr_restrict(int (* restrict));

static int sum_const(int a[const 5])
{
    int i;
    int sum;

    sum = 0;
    for (i = 0; i < 5; i++)
        sum += a[i];
    return sum;
}

static int sum_static(int a[static 5])
{
    return a[0] + a[1] + a[2] + a[3] + a[4];
}

static int sum_volatile(int a[volatile 5])
{
    int i;
    int sum;

    sum = 0;
    for (i = 0; i < 5; i++)
        sum += a[i];
    return sum;
}

static int sum_restrict(int a[restrict 5])
{
    return a[4] - a[0];
}

static int sum_static_restrict_const(int a[static restrict const 5])
{
    return a[0] * 100 + a[4];
}

static int sum_qual_static(int a[const volatile restrict static 5])
{
    return a[1] * 10 + a[3];
}

static int set_static_restrict(int a[static restrict 5])
{
    a[3] = a[0] + a[4];
    return a[3];
}

static int sum_matrix(int a[static restrict 2][3])
{
    return a[0][0] + a[0][2] + a[1][0] + a[1][2];
}

static int mutate_matrix(int a[const 2][3])
{
    a[1][1] = a[0][1] + a[1][2];
    return a[1][1];
}

static int read_paren_const(int (* const p))
{
    return *p;
}

static int read_paren_volatile(int (* volatile p))
{
    return *p;
}

static int read_paren_restrict(int (* restrict p))
{
    return *p;
}

static int cb_sum3(int a[const 3])
{
    return a[0] + a[1] + a[2];
}

static int call_callback(int (*cb)(int [const 3]), int data[static 3])
{
    return cb(data);
}

int main(void)
{
    int a[5];
    int b[5];
    int m[2][3];
    int c[3];
    int x;

    a[0] = 1;
    a[1] = 2;
    a[2] = 3;
    a[3] = 4;
    a[4] = 5;

    b[0] = 10;
    b[1] = 20;
    b[2] = 30;
    b[3] = 40;
    b[4] = 50;

    m[0][0] = 1;
    m[0][1] = 2;
    m[0][2] = 3;
    m[1][0] = 4;
    m[1][1] = 5;
    m[1][2] = 6;

    c[0] = 7;
    c[1] = 8;
    c[2] = 9;
    x = 1234;

    check("const bound", sum_const(a), 15);
    check("static bound", sum_static(a), 15);
    check("volatile bound", sum_volatile(a), 15);
    check("restrict bound", sum_restrict(a), 4);
    check("static restrict const", sum_static_restrict_const(a), 105);
    check("qual static", sum_qual_static(a), 24);
    check("write through static restrict", set_static_restrict(b), 60);
    check("write result", b[3], 60);
    check("matrix stride", sum_matrix(m), 14);
    check("matrix mutate", mutate_matrix(m), 8);
    check("matrix result", m[1][1], 8);
    check("paren ptr const", read_paren_const(&x), 1234);
    check("paren ptr volatile", read_paren_volatile(&x), 1234);
    check("paren ptr restrict", read_paren_restrict(&x), 1234);
    check("callback array qualifier", call_callback(cb_sum3, c), 24);

    if (failures == 0)
        printf("tc99apar completed with great success\n");
    return failures;
}