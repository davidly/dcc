#include <stdio.h>

#define WORD_BITS 16
struct BitSet { unsigned int words[2]; };

static void bitset_add(struct BitSet *set, int value)
{
    set->words[value / WORD_BITS] |= (unsigned int)(1U << (value % WORD_BITS));
}

static int bitset_has(const struct BitSet *set, int value)
{
    return (set->words[value / WORD_BITS] & (1U << (value % WORD_BITS))) != 0;
}

int main(void)
{
    struct BitSet set = {{0U, 0U}};
    bitset_add(&set, 3);
    bitset_add(&set, 20);
    printf("tidxasgn bitset=%d,%d,%d\n",
           bitset_has(&set, 3), bitset_has(&set, 4), bitset_has(&set, 20));
    return !(bitset_has(&set, 3) && !bitset_has(&set, 4) && bitset_has(&set, 20));
}