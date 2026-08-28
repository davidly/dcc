#include <stdio.h>

struct Item {
    unsigned short length;
    unsigned short kind;
};

static struct Item items[2];

static void advance(struct Item **table, int count)
{
    int index;

    for (index = 0; index < count; ++index) {
        struct Item *item = table[index];
        if (item)
            table[index] = item + 1;
    }
}

int main(void)
{
    struct Item *table[2];

    table[0] = 0;
    table[1] = &items[0];
    advance(table, 2);
    if (table[0] != 0)
        return 1;
    if (table[1] != &items[1])
        return 2;
    printf("tppasgn completed with great success\n");
    return 0;
}
