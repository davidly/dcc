/* tponce.c - #pragma once include-splicing regression test */
#include <stdio.h>
#include "ponce_a.h"
#include "ponce_b.h"
#include "ponce_shared.h"
#include "./ponce_shared.h"
#include "ponce_c.h"
#include "ponce_c.h"
#include "ponce_g.h"
#include "ponce_g.h"

static int fails;

static void ckpi(const char *name, int got, int exp)
{
    if (got != exp) {
        printf("FAIL %s got %d expected %d\n", name, got, exp);
        fails++;
    }
}

int main(void)
{
    printf("tponce start\n");
    fails = 0;
    ckpi("shared", shval(), 41);
    ckpi("a", aval(), 46);
    ckpi("b", bval(), 48);
    ckpi("macro", PONCE_SHARED_MACRO, 7);
    ckpi("cmt", cval(), 12);
    ckpi("guard", gval(), 15);

    if (fails) {
        printf("tponce failed: %d\n", fails);
        return 1;
    }
    printf("tponce completed with great success\n");
    return 0;
}
