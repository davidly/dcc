#include <stdio.h>

struct Holder {
    int cells[2][3];
};

static int fixed[2][3];
static int failures;
static int row_calls;
static int column_calls;
static int rhs_calls;

static void check(int condition, const char *name)
{
    if (!condition) {
        printf("FAIL %s\n", name);
        ++failures;
    }
}

static int choose_row(void)
{
    ++row_calls;
    return 1;
}

static int choose_column(void)
{
    ++column_calls;
    return 2;
}

static int choose_rhs(int value)
{
    ++rhs_calls;
    return value;
}

static int vla_result(int rows, int columns, int grid[rows][columns])
{
    return grid[choose_row()][choose_column()] ^=
           rows == 2 ? choose_rhs(0x55) : choose_rhs(0x2a);
}

static int member_result(struct Holder *holder)
{
    return holder->cells[choose_row()][choose_column()] *=
           choose_rhs(3);
}

int main(void)
{
    struct Holder holder;
    int (* const qualified_rows)[3] = fixed;
    int result;

    fixed[1][2] = 10;
    row_calls = column_calls = rhs_calls = 0;
    result = fixed[choose_row()][choose_column()] +=
             choose_rhs(7);
    check(result == 17 && fixed[1][2] == 17,
          "fixed array live result");
    check(row_calls == 1 && column_calls == 1 && rhs_calls == 1,
          "fixed array single evaluation");

    result = (qualified_rows[1][2] -= choose_rhs(5));
    check(result == 12 && qualified_rows[1][2] == 12,
          "qualified pointer live result");

    result = vla_result(2, 3, fixed);
    check(result == (12 ^ 0x55) && fixed[1][2] == (12 ^ 0x55),
          "VLA conditional rhs result");

    holder.cells[1][2] = 9;
    result = member_result(&holder);
    check(result == 27 && holder.cells[1][2] == 27,
          "member array live result");

    holder.cells[0][0] = 80;
    check((holder.cells[0][0] /= 4) == 20, "division result");
    check((holder.cells[0][0] %= 6) == 2, "remainder result");
    check((holder.cells[0][0] |= 0x30) == 0x32, "or result");
    check((holder.cells[0][0] &= 0x1f) == 0x12, "and result");

    check(row_calls == 3 && column_calls == 3 && rhs_calls == 4,
          "combined call counts");

    printf("multidimensional assignment result failures=%d\n", failures);
    return failures != 0;
}
