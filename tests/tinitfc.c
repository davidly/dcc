/*
 * tinitfc.c - a compile-time-constant float expression in a global
 * initializer. dcc's float-initializer fast path (parse_float_init_literal)
 * only recognized a single literal token (optionally signed); the product
 * of two compile-time constants, `(float)0x100000 * 0x10`, isn't a single
 * token, so it fell through to DCC-E0912 "float initializer must be
 * constant" even though every operand is knowable at compile time. A bare
 * float literal like `5.0` always worked.
 */
#include <stdio.h>

static int fails;

static void chkf(const char *name, float got, float exp)
{
    if (got != exp) {
        printf("FAIL %s got %f expected %f\n", name, got, exp);
        fails++;
    }
}

float vals[2] = { (float)0x100000 * 0x10, 5.0 };
float vdiv = (float)100 / 4;
float vneg = (float)2 * -3;

int main(void)
{
    fails = 0;

    chkf("cast_mul", vals[0], 16777216.0f);
    chkf("bare_literal", vals[1], 5.0f);
    chkf("cast_div", vdiv, 25.0f);
    chkf("cast_mul_neg", vneg, -6.0f);

    if (fails) {
        printf("tinitfc failed: %d\n", fails);
        return 1;
    }
    printf("tinitfc completed with great success\n");
    return 0;
}
