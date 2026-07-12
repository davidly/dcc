#include <stdio.h>
#include <stddef.h>

#ifdef _DCC_
#define TEST_UINT32_MAX 0xffffffffUL
#else
#define TEST_UINT32_MAX 0xffffffffU
#endif

static int fails;
static void chk(const char *name, unsigned got, unsigned expect)
{
    if (got != expect) {
        printf("FAIL %s got %u expected %u\n", name, got, expect);
        fails++;
    }
}
struct Inner { char c; int i; };
struct Outer { char a; int b; char arr[10]; struct Inner in; long l; };
typedef struct Outer OuterTyp;
typedef struct { char x; long y; } Typ;

enum { OFF_A = offsetof(struct Outer, a), OFF_B = offsetof(struct Outer, b), OFF_ARR = offsetof(struct Outer, arr) };

int main(void)
{
    chk("first", (unsigned)offsetof(struct Outer, a), 0);
    chk("b", (unsigned)offsetof(struct Outer, b), 1);
    chk("arr", (unsigned)offsetof(struct Outer, arr), 3);
    chk("arr3", (unsigned)offsetof(struct Outer, arr[TEST_UINT32_MAX + 4]), 6);
    chk("in", (unsigned)offsetof(struct Outer, in), 13);
    chk("nested_i", (unsigned)offsetof(struct Outer, in.i), 14);
    chk("long", (unsigned)offsetof(OuterTyp, l), 16);
    chk("typedef_member", (unsigned)offsetof(Typ, y), 1);
    chk("enum_a", OFF_A, 0);
    chk("enum_b", OFF_B, 1);
    chk("enum_arr", OFF_ARR, 3);
    if (fails) { printf("offsetof failed: %d\n", fails); return 1; }
    printf("toffset completed with great success\n");
    return 0;
}
