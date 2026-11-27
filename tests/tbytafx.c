/* Demanded byte loads, bounded word loops and inlined byte predicates.
 * Keep wrapping comparisons distinct from comparisons after C promotion. */
#include <stdint.h>
#include <stdio.h>

static volatile uint32_t observed;
static uint8_t cells[256];
static unsigned calls;

static int equal(const uint8_t *p, uint8_t a, uint8_t b)
{
    if (*p != a + b) return 0;
    return 1;
}

static int different(const uint8_t *p, uint8_t a, uint8_t b)
{
    if (*p == a + b) return 0;
    return 1;
}

static int volatile_equal(const volatile uint8_t *p, uint8_t a, uint8_t b)
{
    if (*p != a + b) return 0;
    return 1;
}

static uint8_t once(uint8_t n)
{
    ++calls;
    return n;
}

static void overwrite(uint8_t *p) { *p = 99; }

static int bounded(uint8_t first)
{
    unsigned i;
    for (i = 3; i < 13; ++i) cells[i] = first + i;
    for (i = 3; i < 13; ++i)
        if (cells[i] != (uint8_t)(first + i)) return 1;
    for (i = 3; i < 13; ++i)
        if (cells[i] != first + i) return first > 243 ? 0 : 2;
    return first <= 243 ? 0 : 3;
}

static int live_index(uint8_t first)
{
    int i;
    for (i = 249; i < 255; ++i) cells[i] = first + i;
    return i;
}

static unsigned unguarded(unsigned a, uint8_t b)
{
    uint8_t local[1];
    local[0] = (uint8_t)(a + b);
    return equal(local, (uint8_t)a, b);
}

static int signed_sum(int a, uint8_t b)
{
    uint8_t c = (uint8_t)(a + b);
    return c == a + b;
}

static inline unsigned word_arg(unsigned x) { return x + 1; }

int main(void)
{
    static const uint32_t words[] = {
        0UL, 255UL, 256UL, 65535UL, 65536UL, 0x80000080UL, 0xffffffffUL
    };
    static const unsigned wide[] = { 0, 255, 256, 511, 65535 };
    unsigned i, j;
    for (i = 0; i < 256; ++i) {
        if (bounded((uint8_t)i) || live_index((uint8_t)i) != 255)
            return puts("FAIL word loop");
        for (j = 0; j < 7; ++j) {
            unsigned b = (uint8_t)words[j];
            uint8_t local[1];
            local[0] = (uint8_t)(i + b);
            if (equal(local, (uint8_t)i, (uint8_t)b) != (i + b <= 255) ||
                different(local, (uint8_t)i, (uint8_t)b) != (i + b > 255))
                return puts("FAIL guarded sum");
            local[0] ^= 1;
            if (equal(local, (uint8_t)i, (uint8_t)b))
                return puts("FAIL changed byte");
            local[0] = (uint8_t)i;
            overwrite(local);
            if (equal(local, 99, 0) != 1 || volatile_equal(local, 99, 0) != 1)
                return puts("FAIL alias");
            observed = words[j];
            if ((uint8_t)observed != (uint8_t)words[j] || observed != words[j])
                return puts("FAIL wide read");
        }
    }
    for (i = 0; i < 5; ++i)
        for (j = 0; j < 256; ++j)
            if (unguarded(wide[i], (uint8_t)j) != (((wide[i] & 255) + j) <= 255))
                return puts("FAIL lost guard");
    for (i = 0; i < 256; ++i) {
        if (signed_sum((int)i - 128, 0) != (i >= 128) ||
            signed_sum((int)i, 255) != (i == 0))
            return puts("FAIL signed range");
    }
    cells[0] = 42;
    if (!equal(cells, once(42), 0) || calls != 1)
        return puts("FAIL argument effects");
    if (word_arg(word_arg(0x1234)) != 0x1236 || !equal(cells, 42, 0))
        return puts("FAIL pooled arguments");
    puts("tbytafx passed");
    return 0;
}
