#include <stdio.h>

enum State { IDLE, RUNNING, STOPPED };
enum Event { START, STOP };

static const enum State transitions[3][2] = {
    [IDLE] = {[START] = RUNNING, [STOP] = IDLE},
    [RUNNING] = {[START] = RUNNING, [STOP] = STOPPED},
    [STOPPED] = {[START] = RUNNING, [STOP] = STOPPED}
};

int main(void)
{
    enum State state = transitions[IDLE][START];
    state = transitions[state][STOP];
    printf("t2denum fsm=%d\n", state);
    return state != STOPPED;
}