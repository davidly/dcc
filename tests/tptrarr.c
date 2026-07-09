// tptrarr (was c9913): regression - VLA parameters plus `*p` where p is a
// pointer-to-array (`bool (*p)[31]`) used as a function argument: the deref
// must decay to an element pointer whose value is p itself (no load); dcc
// previously loaded through the pointer, corrupting the argument and stack.
// Scenario: elementary cellular automaton (Rule 90) over generation buffers.
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

// Advance one generation of Rule 90: next[i] = left XOR right (wrap-around).
static int32_t step(int n, const bool cur[n], bool next[n])
{
    int32_t live = 0;
    for (int i = 0; i < n; i++) {
        bool left  = cur[(i - 1 + n) % n];
        bool right = cur[(i + 1) % n];
        next[i] = left ^ right;
        if (next[i]) live++;
    }
    return live;
}

int main(void)
{
    int n = 31;
    bool a[31] = { false };
    bool b[31] = { false };

    a[n / 2] = true; // single seed cell in the middle

    bool (*cur)[31] = &a;
    bool (*nxt)[31] = &b;
    int32_t total_live = 1;
    int32_t peak = 1;

    for (int g = 0; g < 15; g++) {
        int32_t live = step(n, *cur, *nxt);
        total_live += live;
        if (live > peak) peak = live;
        bool (*tmp)[31] = cur; cur = nxt; nxt = tmp;
    }

    printf("c9913 total_live=%ld peak=%ld\n", (long)total_live, (long)peak);
    return 0;
}
