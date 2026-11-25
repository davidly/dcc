/* PHIs homed in a byte or word local's frame slot (typed-expr-fuzz prog
 * seeds 1009308 and 1013071, clang-oracle seed 1003410). A copy of a loop
 * value must survive a later join that writes the same slot, including a
 * join placed after the copy's last use, and an edge into a slot-homed join
 * must still store its value there. */
#include <stdint.h>
#include <stdio.h>

static int32_t r[3];

static int32_t pass(int32_t v)
{
    return v;
}

/* `a3 = k0` shares k0's slot; the join after the `if` must not clobber it. */
static uint16_t copy_then_join(int32_t a1, int i)
{
    uint8_t k0 = 0, a3;
    int32_t *p0 = &r[i];
    int j;

    r[i] = a1;
    for (j = 0; j < 3; ++j)
        k0 = (uint8_t)*p0;
    *p0 >>= 6;
    a3 = k0;
    if (1)
        k0 = 0;
    else
        r[i] += 1;
    return (uint16_t)(a3 * 256u + k0);
}

/* The join's PHI sits after the last read of the copied value. */
static uint32_t late_join(uint8_t a0, uint8_t a2, uint8_t l3, uint16_t a4)
{
    uint8_t k2 = a2;
    int j;
    uint32_t h;

    for (j = 0; j < 3; ++j)
        k2 += k2;
    a0 = k2;
    if (l3)
        k2 = (uint8_t)pass(l3);
    else
        k2 |= a4;
    h = 0;
    h = h * 31 + (uint32_t)a0;
    h = h * 31 + (uint32_t)k2;
    h = h * 31 + (uint32_t)l3;
    h = h * 31 + (uint32_t)a4;
    return h;
}

static uint16_t g7;

/* The loop-exit edge must store l1 into the slot `++l1` updates in place. */
static int incremented_join(void)
{
    uint16_t l1 = 1;
    int i;

    if (0) {
    } else {
        for (i = 2; i != 4; i++)
            l1 = 1;
    }
    ++l1;
    switch (1) {
    case 3:
        break;
    case 1:
        ;
    }
    g7 = l1;
    return 1;
}

int main(void)
{
    printf("copy %u %u %u\n", copy_then_join(2147483647L, 0),
           copy_then_join(40, 1), copy_then_join(127, 2));
    printf("late %lx %lx\n", (unsigned long)late_join(4, 0, 1, 23),
           (unsigned long)late_join(4, 1, 0, 5));
    incremented_join();
    printf("join %u\n", g7);
    return 0;
}
