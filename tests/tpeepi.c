/* tpeepi.c - peephole regression for automatic aggregate init aliasing */

#include <stdio.h>

struct Task {
    const char *name;
    int priority;
    _Bool done;
};

static int count_open(const struct Task *tasks, int count)
{
    int open = 0;

    for (int i = 0; i < count; ++i) {
        if (!tasks[i].done)
            ++open;
    }
    return open;
}

static const struct Task *highest_open(const struct Task *tasks, int count)
{
    const struct Task *best = 0;

    for (int i = 0; i < count; ++i) {
        if (!tasks[i].done && (best == 0 || tasks[i].priority > best->priority))
            best = &tasks[i];
    }
    return best;
}

int main(void)
{
    struct Task tasks[] = {
        { "parse", 2, 1 },
        { "build", 3, 1 },
        { "run",   5, 0 },
        { "log",   1, 0 }
    };
    const struct Task *next = highest_open(tasks, 4);

    printf("tpeepi open=%d next=%s prio=%d\n",
           count_open(tasks, 4), next->name, next->priority);
    return 0;
}