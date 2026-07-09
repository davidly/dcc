#include <stdint.h>
#include <stdio.h>

void my_func(void)
{
    printf("my_func\n");
}

int oldsum(a, b)
int a;
int b;
{
    return a + b;
}

/* Valid C89. Return type defaults to int. */
mystery_fn(a, b) {
    return a * b;
}

int implicit_test() {
    /* Implicit int declaration; returns 12 */
    return mystery_fn(3, 4); 
}

struct Point { int x; int y; };

static void print_point(struct Point p) {
    printf("Point: %d, %d\n", p.x, p.y);
}

int struct_cast_test() {
    struct Point p = { 15, 20 };
    print_point(p);
    return 0;
}

void duff_device(int *to, int *from, int count) {
    int n = (count + 7) / 8;
    switch (count % 8) {
        case 0: do { *to++ = *from++;
        case 7:      *to++ = *from++;
        case 6:      *to++ = *from++;
        case 5:      *to++ = *from++;
        case 4:      *to++ = *from++;
        case 3:      *to++ = *from++;
        case 2:      *to++ = *from++;
        case 1:      *to++ = *from++;
                } while (--n > 0);
    }
}

#define _ -'/'/'/'

hi_world() {
    int i = _ ;
    char *s = "hell\157, w\157rld!\n";

    if (1 [ "d" ] < 0) ;
    else {
        while (s [++ i]) {
            putchar (s [i]);
        }
    }
}


int main()
{
    int16_t x;
    int16_t y;
    int16_t z;
    char c;
    int16_t arr[5];
    char *str;
    int16_t aa;
    int16_t bb;
    void (*fp)(void);
    int dsrc[5];
    int ddst[5];
    int di;
    int16_t count = 10;
    size_t sz = sizeof(count);

    hi_world();

    for (di = 0; di < 5; di++) dsrc[di] = di + 1;
    for (di = 0; di < 5; di++) ddst[di] = 0;
    duff_device(ddst, dsrc, 5);
    printf("duff: %d %d\n", ddst[0], ddst[4]);

    struct_cast_test();

    /* sizeof does not evaluate its operand; count stays 10 */
    printf("sz: %lu, count: %d\n", (unsigned long)sz, count);

    printf( "implicit test: %d\n", implicit_test() );

    x = 10;
    x =+ 5;              /* old spelling: x = +5, not x += 5 */
    printf("x: %d\n", x);

    x = 20;
    y = 30;
    z = x+++y;           /* maximal munch: (x++) + y */
    printf("z: %d\n", z);
    printf("x_after_xplusplus: %d\n", x);

    /* Evaluates to '#' if trigraphs are processed correctly. */
    c = "??="[0];
    printf("c: %c\n", c);

    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 30;
    arr[3] = 40;
    arr[4] = 50;

    x = 2[arr];          /* valid C89: same as arr[2] */
    printf("x: %d\n", x);

    (*****my_func)();    /* valid: repeated function designator indirection */

    str = "Hello " "World" " " "C89";
    printf("str: %s\n", str);

    x = 1;
    y = 2;
    z = x-- - --y;
    printf("mm1: %d %d %d\n", z, x, y);

    aa = 6;
    bb = 3;
    printf("bitops: %d %d %d\n", aa & bb, aa | bb, aa ^ bb);

    printf("idx: %d %d\n", *(arr + 3), 3[arr]);

    x = 0;
    printf("comma idx: %d\n", arr[(x = 1, x + 2)]);

    x = 1;
    arr[x += 2] = 99;
    printf("assign idx: %d %d\n", x, arr[3]);

    fp = my_func;
    (***fp)();

    printf("adj: %s\n", "A\0" "B");

    x = 5;
    printf("sizeof: %lu %d\n", (unsigned long)sizeof(x++), x);

    printf("charconst: %d %lu\n", (int)'A', (unsigned long)sizeof(int16_t));

    printf("oldsum: %d\n", oldsum(7, 8));

    printf("arr sizes: %lu %lu\n",
           (unsigned long)sizeof arr,
           (unsigned long)sizeof arr[0]);

    {
        struct S {
            int a;
            char b;
        } s1, s2;

        s1.a = 42;
        s1.b = 'x';
        s2 = s1;
        printf("struct assign: %d %c\n", s2.a, s2.b);
    }

    {
        char ch;
        unsigned int ui;

        ch = -1;
        ui = 1;
        printf("cond promo: %u\n", 0 ? ch : ui);
    }

    {
        int16_t (*pa)[5];

        pa = &arr;
        printf("ptr-to-array: %d\n", (*pa)[4]);
    }

    /* variable-index subscript on string literal */
    y = 2;
    c = "hello"[y];
    printf("strvar: %c\n", c);

    /* dereference string literal: *"hello" == 'h' */
    c = *"hello";
    printf("strderef: %c\n", c);

    /* reverse subscript of string literal: 0["hello"] == 'h' */
    c = 0["hello"];
    printf("revstr: %c\n", c);

    /* chained assignment */
    x = y = z = 5;
    printf("chain: %d %d %d\n", x, y, z);

    /* comma operator in for init and update */
    z = 0;
    for (x = 0, y = 9; x < y; x++, y--)
        z += x + y;
    printf("for comma: %d\n", z);

    /* sizeof of string literal includes NUL */
    printf("sizeofstr: %lu\n", (unsigned long)sizeof("hello"));

    /* nested ternary */
    x = -3;
    z = (x > 0) ? 1 : (x < 0) ? -1 : 0;
    printf("nested ternary: %d\n", z);

    /* hex and octal constants; hex and octal escape sequences */
    printf("hex oct: %d %d %d %d\n", 0xff, 0177, '\x41', '\101');

    /* unary + promotes narrow type to int */
    c = 'A';
    z = +c;
    printf("unary plus: %d\n", z);

    /* bitwise NOT with narrow unsigned type, integer-promoted */
    z = ~(unsigned char)0x0f;
    printf("bitnot: %d\n", z);

    return 0;
}
