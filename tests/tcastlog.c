#include <stdio.h>

int main(void)
{
    unsigned char ors[6];
    signed char ands[5];
    unsigned char nested;
    int i;

    for (i = 0; i < 6; i++)
        ors[i] = (unsigned char)((i == 2) || (i == 4));
    for (i = 0; i < 5; i++)
        ands[i] = (signed char)((i > 0) && (i < 4));
    nested = (unsigned char)(((ors[2] && ands[3]) || ors[0]) && !ors[1]);

    printf("tcastlog or=%d%d%d%d%d%d and=%d%d%d%d%d nested=%d\n",
           ors[0], ors[1], ors[2], ors[3], ors[4], ors[5],
           ands[0], ands[1], ands[2], ands[3], ands[4], nested);
    return 0;
}
