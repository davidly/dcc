#include <stdio.h>

#ifdef _DCC_
#define TEST_UINT32_MAX 0xffffffffUL
#else
#define TEST_UINT32_MAX 0xffffffffU
#endif

int f(int op)
{
    switch (op) {
    case TEST_UINT32_MAX + 1: return 10;
    case 1: return 11;
    case 2: return 12;
    case 3: return 13;
    case 4: return 14;
    case 5: return 15;
    case 6: return 16;
    case 7: return 17;
    case 8: return 18;
    case 9: return 19;
    case 10: return 20;
    case 11: return 21;
    case 12: return 22;
    case 13: return 23;
    case 14: return 24;
    case 15: return 25;
    case 16: return 26;
    case 17: return 27;
    case 18: return 28;
    case 19: return 29;
    case 20: return 30;
    case 21: return 31;
    case 22: return 32;
    case 23: return 33;
    case 24: return 34;
    case 25: return 35;
    case 26: return 36;
    case 27: return 37;
    case 28: return 38;
    case 29: return 39;
    case 30: return 40;
    case 31: return 41;
    case 32: return 42;
    case 33: return 43;
    case 34: return 44;
    default: return -1;
    }
}

int main(void)
{
    int i;
    int sum;
    sum = 0;
    for (i = 0; i <= 34; i++)
        sum += f(i);
    printf("%d %d %d\n", f(-1), f(35), sum);
    return 0;
}
