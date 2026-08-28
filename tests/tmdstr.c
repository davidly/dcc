#include <stdio.h>

static const char two[2][3] = {"1", "12"};
static const char three[2][2][5] = {
    {"1", "12"},
    {"123", "1234"}
};

int main(void)
{
    if (two[0][0] != '1' || two[0][1] != 0 ||
        two[1][0] != '1' || two[1][1] != '2' || two[1][2] != 0)
        return 1;
    if (three[0][0][0] != '1' || three[0][0][1] != 0)
        return 2;
    if (three[0][1][0] != '1' || three[0][1][1] != '2' ||
        three[0][1][2] != 0)
        return 3;
    if (three[1][0][0] != '1' || three[1][0][1] != '2' ||
        three[1][0][2] != '3' || three[1][0][3] != 0)
        return 4;
    if (three[1][1][0] != '1' || three[1][1][1] != '2' ||
        three[1][1][2] != '3' || three[1][1][3] != '4' ||
        three[1][1][4] != 0)
        return 5;
    printf("tmdstr completed with great success\n");
    return 0;
}
