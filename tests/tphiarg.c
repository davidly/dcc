/* A phi source with a named parameter home must be copied on its CFG edge. */
#include <stdio.h>

static int choose(const void *condition, int selected, int fallback)
{
    return condition ? selected : fallback;
}

int main(void)
{
    int object;

    if (choose(&object, 1234, 5678) != 1234 ||
        choose(0, 1234, 5678) != 5678) {
        printf("tphiarg failed\n");
        return 1;
    }

    printf("tphiarg completed with great success\n");
    return 0;
}
