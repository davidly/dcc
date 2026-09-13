#include <stdio.h>

static int data[16];
static int *slots[2];
static const int *qualified_slots[2];
static int failures;
static int index_calls;

struct Holder {
    int *slots[2];
    const int *qualified_slots[2];
};

static void check(int condition, const char *name)
{
    if (!condition) {
        printf("FAIL %s\n", name);
        ++failures;
    }
}

static int choose_slot(void)
{
    ++index_calls;
    return 1;
}

static void retreat(int **cursor)
{
    *cursor -= 4;
}

int main(void)
{
    struct Holder holder;
    int *cursor;
    int *assigned;
    int i;

    for (i = 0; i < 16; ++i)
        data[i] = 100 + i;

    slots[0] = data + 1;
    assigned = (slots[0] += 2);
    check(assigned == data + 3 && *assigned == 103,
          "array assignment value");
    slots[0] -= 1;
    check(slots[0] == data + 2 && *slots[0] == 102,
          "array subtract");

    (slots + 0)[0] += 1;
    check(slots[0] == data + 3, "pointer expression index");
    slots[0] -= 1;
    check(slots[0] == data + 2, "second array subtract");

    qualified_slots[0] = data + 4;
    qualified_slots[0] += 2;
    check(*qualified_slots[0] == 106, "qualified array element");

    holder.slots[0] = data + 2;
    holder.slots[0] += 3;
    check(*holder.slots[0] == 105, "member array element");

    holder.qualified_slots[0] = data + 5;
    holder.qualified_slots[0] -= 2;
    check(*holder.qualified_slots[0] == 103,
          "qualified member array element");

    cursor = data + 6;
    retreat(&cursor);
    check(cursor == data + 2 && *cursor == 102, "deref pointer");

    slots[1] = data;
    index_calls = 0;
    slots[choose_slot()] += 7;
    check(slots[1] == data + 7 && index_calls == 1,
          "single index evaluation");

    printf("pointer compound assignment failures=%d\n", failures);
    return failures != 0;
}
