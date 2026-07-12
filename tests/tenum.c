/* C89 enum regression test */
#include <stdio.h>

#ifdef _DCC_
#define TEST_UINT32_MAX 0xffffffffUL
#else
#define TEST_UINT32_MAX 0xffffffffU
#endif

enum Color {
    RED,
    GREEN = 5,
    BLUE,
    CYAN = -1
};

enum State {
    OFF = 0,
    ON = 1
};

enum Expr {
    EA = 1,
    EB = EA + 2,
    EC = EB * 3,
    ED = (EC << 1) | 1,
    EE = ED & 31,
    EF = EE ^ 7,
    EG = ~0,
    EH = !0,
    EI = (sizeof(char) == 1),
    EJ = (EB < EC) && (EC != 0),
    EK = ((TEST_UINT32_MAX + 1) == 0) ? 31 : 0,
    EL = 1 / (EK == 31),
};

typedef enum Color ColorT;
typedef enum TagEnum {
    TAG_A = 10,
    TAG_B,
    TAG_C = TAG_B + RED + ON
} TagT;

enum Color gc = BLUE;
ColorT gct = CYAN;
TagT gt = TAG_C;
int ga[3] = { RED, GREEN, BLUE };

int main(void)
{
    enum Color c;
    enum State s;
    enum TagEnum t;
    int ok;

    c = BLUE;
    s = ON;
    t = TAG_B;

    ok = 1;

    if (RED != 0) ok = 0;
    if (GREEN != 5) ok = 0;
    if (BLUE != 6) ok = 0;
    if (CYAN != -1) ok = 0;

    if (c != 6) ok = 0;
    if (s != 1) ok = 0;

    c = CYAN;
    if ((int)c != -1) ok = 0;

    if (EA != 1) ok = 0;
    if (EB != 3) ok = 0;
    if (EC != 9) ok = 0;
    if (ED != 19) ok = 0;
    if (EE != 19) ok = 0;
    if (EF != 20) ok = 0;
    if (EG != -1) ok = 0;
    if (EH != 1) ok = 0;
    if (EI != 1) ok = 0;
    if (EJ != 1) ok = 0;

    if (TAG_A != 10) ok = 0;
    if (TAG_B != 11) ok = 0;
    if (TAG_C != 12) ok = 0;
    if (t != 11) ok = 0;

    if (gc != 6) ok = 0;
    if ((int)gct != -1) ok = 0;
    if (gt != 12) ok = 0;
    if (ga[0] != 0 || ga[1] != 5 || ga[2] != 6) ok = 0;

    switch (c) {
    case CYAN:
        break;
    default:
        ok = 0;
        break;
    }

    if (!ok) {
        printf("enum test failed\n");
        return 1;
    }

    printf("enum test passed with great success\n");
    return 0;
}
