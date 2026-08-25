#include <stdio.h>

/* Dense switch whose case values straddle zero - regression coverage for
 * the constant-result switch table matcher accepting negative case values
 * (mir_match_constant_result_switch in dcc_mir_machine_constant_folding.c). */
int has_negative(int x)
{
    switch (x) {
    case -2: return 10;
    case -1: return 20;
    case 0: return 30;
    case 1: return 40;
    default: return -1;
    }
}

/* Dense switch entirely below zero. */
int all_negative(int x)
{
    switch (x) {
    case -4: return 100;
    case -3: return 200;
    case -2: return 300;
    default: return -1;
    }
}

int main(void)
{
    int i;

    for (i = -4; i <= 3; i++)
        printf("%d %d\n", i, has_negative(i));
    for (i = -6; i <= -1; i++)
        printf("%d %d\n", i, all_negative(i));
    return 0;
}
