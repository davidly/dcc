#include <stdio.h>

enum State { S0, S1, S2, S3 };

static const enum State transition[4][2] = {
    [S0] = { [0] = S0, [1] = S1 },
    [S1] = { [0] = S2, [1] = S1 },
    [S2] = { [0] = S0, [1] = S3 },
    [S3] = { [0] = S3, [1] = S3 }
};

static enum State scan(const char *bits)
{
    enum State state = S0;

    for (; *bits; ++bits)
        state = transition[state][*bits - '0'];

    return state;
}

int main(void)
{
    enum State state = scan("1101");
    printf("tenumfsm machine=%d\n", (int)state);
    return state != S3;
}