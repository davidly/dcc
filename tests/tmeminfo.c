#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#ifndef EXPECT_STACK
#define EXPECT_STACK 512
#endif

struct guarded {
    unsigned int before;
    meminfo_t info;
    unsigned int after;
};

static int valid(const meminfo_t *info)
{
    return info->heap_start <= info->heap_break &&
           info->heap_break <= info->heap_limit &&
           info->heap_capacity == info->heap_limit - info->heap_start &&
           info->heap_committed == info->heap_break - info->heap_start &&
           info->heap_growth == info->heap_limit - info->heap_break &&
           info->heap_committed + info->heap_growth == info->heap_capacity;
}

int main(void)
{
    struct guarded snapshot;
    meminfo_t initial, grown, hole, reused, final;
    void *first, *second, *replacement;
    snapshot.before = 0x1234;
    snapshot.after = 0x5678;
    errno = EDOM;
    meminfo(&snapshot.info);
    if (sizeof(meminfo_t) != 14 || snapshot.before != 0x1234 ||
        snapshot.after != 0x5678 || errno != EDOM ||
        !valid(&snapshot.info) || snapshot.info.stack_size != EXPECT_STACK)
        return 1;
    meminfo(&initial);
    if (initial.heap_start != snapshot.info.heap_start ||
        initial.heap_limit != snapshot.info.heap_limit ||
        initial.heap_break != snapshot.info.heap_break ||
        initial.heap_limit != *(unsigned int *)6 - EXPECT_STACK ||
        initial.heap_committed != 0 || initial.heap_capacity < 32768U)
        return 2;
    first = malloc(100);
    second = malloc(200);
    if (!first || !second)
        return 3;
    meminfo(&grown);
    if (!valid(&grown) || grown.heap_start != initial.heap_start ||
        grown.heap_limit != initial.heap_limit ||
        grown.heap_committed != 308 || grown.heap_capacity != initial.heap_capacity)
        return 4;
    free(first);
    meminfo(&hole);
    if (!valid(&hole) || hole.heap_break != grown.heap_break ||
        hole.heap_committed != grown.heap_committed ||
        hole.heap_growth != grown.heap_growth)
        return 5;
    replacement = malloc(50);
    if (replacement != first)
        return 6;
    meminfo(&reused);
    if (!valid(&reused) || reused.heap_break != grown.heap_break)
        return 7;
    free(replacement);
    free(second);
    meminfo(&final);
    if (!valid(&final) || final.heap_committed != 0 ||
        final.heap_break != initial.heap_break ||
        final.heap_growth != initial.heap_growth)
        return 8;
#ifdef MEMINFO_VERBOSE
    printf("MEMINFO start=%u break=%u limit=%u\n",
           initial.heap_start, initial.heap_break, initial.heap_limit);
#endif
    puts("PASS: meminfo bounds, stack, committed space, free-hole reuse and trimming");
    return 0;
}
