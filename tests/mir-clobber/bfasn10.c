#include <stdio.h>

struct Bits {
    unsigned int left : 3;
    signed int signed_value : 5;
    unsigned int unsigned_value : 5;
    unsigned int right : 3;
};

static int failures;

static void check(int condition, const char *name)
{
    if (!condition) {
        printf("FAIL %s\n", name);
        ++failures;
    }
}

int main(void)
{
    struct Bits bits;
    struct Bits *pointer = &bits;
    long signed_long = -7L;
    long unsigned_long = 37L;
    float signed_float = -3.75f;
    float unsigned_float = 35.5f;
    int result;

    bits.left = 5;
    bits.right = 6;
    bits.signed_value = signed_long;
    bits.unsigned_value = unsigned_long;
    check(bits.signed_value == -7, "signed long store");
    check(bits.unsigned_value == 5, "unsigned long truncation");
    check(bits.left == 5 && bits.right == 6, "long neighbor preservation");

    result = (pointer->signed_value = signed_float);
    pointer->unsigned_value = unsigned_float;
    check(result == -3 && bits.signed_value == -3, "signed float store");
    check(bits.unsigned_value == 3, "unsigned float truncation");
    check(bits.left == 5 && bits.right == 6, "float neighbor preservation");

    printf("bitfield numeric assignment failures=%d\n", failures);
    return failures != 0;
}
