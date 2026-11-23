/* Regressions from the Clang frontend differential campaign. */
#include <stdio.h>
#include "tfefuzz.h"
#include "tfefuzz.h"

#define LOGICAL_LINE 161
#define LOGICAL_FILE "tfefuzz.c"
#line LOGICAL_LINE LOGICAL_FILE
enum { line_from_macro = __LINE__ };
#if __LINE__ != 162
#error incorrect preprocessing line after macro-expanded #line
#endif

typedef long Callback(long);
typedef Callback CallbackAlias;

static long add_one(long value)
{
    return value + 1;
}

static long call_typedef(CallbackAlias callback, int value)
{
    return callback(value);
}

static long call_declarator(long callback(long), int value)
{
    return callback(value);
}

typedef int Word;

static int typedef_scope(void)
{
    Word outer = 5;
    {
        typedef signed char Word;
        Word inner = -2;
        outer += inner + (int)sizeof(Word);
    }
    return outer + (sizeof(Word) == sizeof(int));
}

struct Cell {
    int left;
    int right;
};

static struct Cell cells[5] = {
    [3] = { .right = 9 },
    [0].left = 5, 6,
    [2].left = 7,
    [3].left = 8
};

static int local_init(void)
{
    struct Cell local[4] = { [3] = { .left = 24 }, [1] = { .right = 6 } };
    return local[3].left + local[1].right + local[0].right + local[2].left;
}

static int loop_vla(int rows, int early_exit)
{
    int total = 0;
    int i;
    for (i = 0; i < 3; ++i) {
        int count = rows;
        int matrix[count][2];
        matrix[rows - 1][1] = i;
        ++count;
        total += (int)(sizeof matrix / sizeof(int)) + matrix[rows - 1][1];
        if (early_exit && i == 1)
            break;
        ++total;
    }
    return total;
}

static int vla_dereference(int rows)
{
    {
        int matrix[rows][2][3];
        int *last_row = matrix[rows - 1][1];
        int row = rows - 1;
        matrix[rows - 1][1][2] = 4;
        *matrix[row++][1] = 8;
        matrix[rows - 1][0][2] = 16;
        int initial = *matrix[rows - 1][1];
        if (row != rows || initial != 8 || *last_row != 8 ||
            matrix[rows - 1][1][0] != 8)
            return -1;
        matrix[rows - 1][1][0] = 10;
        return *matrix[rows - 1][1] + matrix[rows - 1][1][2];
    }
}

static int vla_2d_dereference(int rows)
{
    int matrix[rows][3];
    *matrix[rows - 1] = 5;
    matrix[rows - 1][1] = 2;
    return *matrix[rows - 1] + matrix[rows - 1][0];
}

int main(void)
{
    printf("preprocessing %d %d\n", TPPDIR_HEADER_VALUE, line_from_macro);
    printf("function parameters %ld %ld\n",
           call_typedef(add_one, -4), call_declarator(add_one, 5));
    printf("typedef scope %d\n", typedef_scope());
    printf("designated initializers %d %d %d %d %d %d %d\n", cells[0].left,
           cells[0].right, cells[2].left, cells[2].right, cells[3].left,
           cells[3].right, local_init());
    printf("VLA scope %d %d %d %d\n", loop_vla(2, 0), loop_vla(2, 1),
           vla_dereference(2), vla_2d_dereference(2));
    return 0;
}
