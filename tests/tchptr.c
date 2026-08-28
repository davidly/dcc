#include <stdio.h>

struct Item {
    int first;
    int second;
};

struct Holder {
    struct Item **items;
    int index;
};

static struct Item item = {17, 25};
static struct Item other = {9, 30};
static struct Item *item_ptr = &item;
static struct Holder holder[1] = {{&item_ptr, 0}};

static int total(struct Item *value)
{
    return value->first + value->second;
}

static struct Item *replacement(void)
{
    return &other;
}

int main(void)
{
    if (total(holder->items[holder->index]) != 42)
        return 1;
    holder->items[holder->index] = replacement();
    holder->items[holder->index]->first++;
    if (total(holder->items[holder->index]) != 40)
        return 2;
    printf("tchptr completed with great success\n");
    return 0;
}
