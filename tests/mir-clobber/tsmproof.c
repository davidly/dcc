#include <stdio.h>

/* Keep the retained thoistbc schedule's shape while changing one semantic edge. */
static int sliding_max(const int a[], int n, int k, int out[])
{
    int dq[8], head = 0, tail = 0, count = 0, i;
    for (i = 0; i < n; ++i) {
        int limit = i - k + 1;
        while (head < tail &&
#ifdef FRONT_INCLUSIVE
               dq[head] <= limit)
#else
               dq[head] < limit)
#endif
            ++head;
        while (head < tail &&
#ifdef BACK_MINIMUM
               a[dq[tail - 1]] >= a[i])
#else
               a[dq[tail - 1]] <= a[i])
#endif
            --tail;
        dq[tail++] = i;
        if (i >= k - 1)
            out[count++] = a[dq[head]];
    }
    return count;
}

static void report(const char *name, const int *input, int n, int k)
{
    int output[8], count, i;
    for (i = 0; i < 8; ++i)
        output[i] = 12345;
    count = sliding_max(input, n, k, output);
    printf("%s %d:", name, count);
    for (i = 0; i < count; ++i)
        printf(" %d", output[i]);
    printf(" guard=%d\n", output[count]);
}

int main(void)
{
    static const int descending[8] = {9, 8, 7, 6, 5, 4, 3, 2};
    static const int mixed[8] = {1, 3, -1, -3, 5, 3, 6, 7};
    static const int extremes[8] = {
        32767, -32768, -1, 0, -32768, 32767, 32767, -1
    };

    report("descending", descending, 8, 3);
    report("mixed", mixed, 8, 3);
    report("extremes", extremes, 8, 3);
    report("empty", mixed, 0, 3);
    report("short", mixed, 2, 3);
    report("single", extremes, 1, 1);
    return 0;
}
