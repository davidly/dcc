#include <stdio.h>
#include <string.h>

/* Regression: a fully indexed multidimensional array may have pointer-valued
 * elements.  Parser typing and value gates must accept and load
 * table[group][item] in assignments, call arguments, and conditions. */

static char *table[2][3] = {
    {"one", "two", "three"},
    {"four", "five", "six"}
};

static int total;

static void consume(char *text, int weight)
{
    total += (int)strlen(text) * weight;
}

int main(void)
{
    int item;
    int group;
    char *selected;

    group = 1;
    total = 0;
    for (item = 0; item < 3; item++) {
        consume(table[group][item], item + 1);
        if (item == 1)
            consume(table[0][0], 2);
        else
            consume(table[0][item], 1);
    }

    selected = table[group][2];
    consume(selected, 1);

    if (table[0][0])
        consume(table[0][0], 1);

    item = 0;
    while (table[0][item]) {
        consume(table[0][item], 1);
        break;
    }

    item = 0;
    do {
        consume(table[0][item], 1);
        break;
    } while (table[0][item]);

    for (item = 0; table[0][item]; item++) {
        consume(table[0][item], 1);
        break;
    }

    printf("tptr2dv total=%d last=%s\n", total, table[group][2]);
    return 0;
}