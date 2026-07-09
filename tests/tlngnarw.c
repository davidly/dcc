/* tlngnarw (was c8916): regression - narrowing an out-of-int-range constant
 * (`prev = -32768;`, a long literal on the 16-bit target) into an int local
 * must work even when a large-array struct local pushes the frame beyond
 * IX-direct range; dcc previously rejected it with DCC-E1002.
 * The live-result edge case uses C99 exact-width types (int16_t/int32_t) so
 * the narrowing semantics are identical on the clang host and the target.
 * Scenario: binary min-heap used as a priority queue. */
#include <stdio.h>
#include <stdint.h>

#define HEAP_MAX 64

struct Heap {
    int data[HEAP_MAX];
    int size;
};

static void heap_push(struct Heap *h, int value)
{
    int i;

    if (h->size >= HEAP_MAX) return;
    i = h->size;
    h->data[i] = value;
    h->size = h->size + 1;
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (h->data[parent] <= h->data[i]) break;
        {
            int t = h->data[parent];
            h->data[parent] = h->data[i];
            h->data[i] = t;
        }
        i = parent;
    }
}

static int heap_pop(struct Heap *h)
{
    int top;
    int i;

    top = h->data[0];
    h->size = h->size - 1;
    h->data[0] = h->data[h->size];
    i = 0;
    for (;;) {
        int left = 2 * i + 1;
        int right = 2 * i + 2;
        int smallest = i;

        if (left < h->size && h->data[left] < h->data[smallest]) smallest = left;
        if (right < h->size && h->data[right] < h->data[smallest]) smallest = right;
        if (smallest == i) break;
        {
            int t = h->data[i];
            h->data[i] = h->data[smallest];
            h->data[smallest] = t;
        }
        i = smallest;
    }
    return top;
}

int main(void)
{
    static const int input[] = { 23, 5, 71, 12, 90, 3, 44, 17, 58, 8, 33, 61 };
    struct Heap h;
    int i;
    int n;
    int sorted_ok;
    int prev;

    n = (int)(sizeof input / sizeof input[0]);
    h.size = 0;
    for (i = 0; i < n; i = i + 1) heap_push(&h, input[i]);

    sorted_ok = 1;
    prev = -32768;
    for (i = 0; i < n; i = i + 1) {
        int v = heap_pop(&h);
        if (v < prev) sorted_ok = 0;
        prev = v;
    }
    printf("c8916 n=%d sorted=%d last=%d\n", n, sorted_ok, prev);

    /* Review edge case: the LIVE result of a narrowing assignment into a
     * non-IX-direct 16-bit local (the big struct Heap local pushes it out of
     * IX range) must be the narrowed value, not a stale 32-bit pair.
     * int16_t is exactly 16 bits on both the clang host and dcc, so the
     * wrap of 40000 to -25536 is the same everywhere. */
    {
        int16_t w;
        int32_t big;
        int32_t y;
        int32_t z;
        int live_ok;

        y = (w = -32768);      /* long-typed constant on the 16-bit target */
        live_ok = (y == -32768L && w == -32768);
        big = 40000L;
        z = (w = big);         /* runtime 32-bit value narrows mod 2^16 */
        if (z != -25536L || w != -25536) live_ok = 0;
        printf("c8916 live=%s\n", live_ok ? "ok" : "FAIL");
    }
    return 0;
}
