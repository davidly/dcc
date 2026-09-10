#include <stdio.h>

#define DIMENSION 7
#define STORAGE_STRIDE (DIMENSION + 1)
#define STORAGE_CELLS (DIMENSION * STORAGE_STRIDE)
#define GUARD_WORDS 4
#define GUARD_VALUE 23130

#ifdef GRIDW11_RECTANGULAR
#define ACTIVE_STRIDE STORAGE_STRIDE
#else
#define ACTIVE_STRIDE DIMENSION
#endif

static int *grid_wave11_table;

static int grid_wave11(int x, int y)
{
    unsigned char i;
    int r;

    for (r = i = 0; i < DIMENSION; i++) {
#ifdef GRIDW11_ROW_FLOW
        r = r + grid_wave11_table[y + ACTIVE_STRIDE * i];
#else
        r = r + grid_wave11_table[x + ACTIVE_STRIDE * i];
#endif
#ifdef GRIDW11_COLUMN_FLOW
        r = r + grid_wave11_table[i + ACTIVE_STRIDE * x];
#else
        r = r + grid_wave11_table[i + ACTIVE_STRIDE * y];
#endif
#ifdef GRIDW11_BOUND
        if (x + i < DIMENSION - 1 & y + i < DIMENSION)
#else
        if (x + i < DIMENSION & y + i < DIMENSION)
#endif
#ifdef GRIDW11_DIAGONAL_FLOW
            r = r + grid_wave11_table[
                x + i + ACTIVE_STRIDE * (x + i)];
#else
            r = r + grid_wave11_table[
                x + i + ACTIVE_STRIDE * (y + i)];
#endif
        if (x + i < DIMENSION & y - i >= 0)
            r = r + grid_wave11_table[
                x + i + ACTIVE_STRIDE * (y - i)];
        if (x - i >= 0 & y + i < DIMENSION)
            r = r + grid_wave11_table[
                x - i + ACTIVE_STRIDE * (y + i)];
        if (x - i >= 0 & y - i >= 0)
            r = r + grid_wave11_table[
                x - i + ACTIVE_STRIDE * (y - i)];
    }
#ifdef GRIDW11_RETURN
    return r + 17;
#else
    return r;
#endif
}

struct GuardedGrid {
    int before[GUARD_WORDS];
    int cells[STORAGE_CELLS];
    int after[GUARD_WORDS];
};

static int failures;
static unsigned int checks;
static unsigned long oracle_hash = 2166136261UL;

static int reference_cell(const int cells[], int x, int y)
{
    return cells[x + ACTIVE_STRIDE * y];
}

static int reference_grid_wave11(const int cells[], int x, int y)
{
    static const int dx[4] = {1, 1, -1, -1};
    static const int dy[4] = {1, -1, 1, -1};
    int total = 0;
    int i;

    for (i = 0; i < DIMENSION; ++i) {
#ifdef GRIDW11_ROW_FLOW
        total += reference_cell(cells, y, i);
#else
        total += reference_cell(cells, x, i);
#endif
#ifdef GRIDW11_COLUMN_FLOW
        total += reference_cell(cells, i, x);
#else
        total += reference_cell(cells, i, y);
#endif
    }
    for (i = 0; i < DIMENSION; ++i) {
        int direction;

        for (direction = 0; direction < 4; ++direction) {
            int column = x + dx[direction] * i;
            int row = y + dy[direction] * i;
            int access_row = row;

#ifdef GRIDW11_BOUND
            if (direction == 0 && column == DIMENSION - 1)
                continue;
#endif
#ifdef GRIDW11_DIAGONAL_FLOW
            if (direction == 0)
                access_row = column;
#endif
            if (column >= 0 && column < DIMENSION &&
                row >= 0 && row < DIMENSION)
                total += reference_cell(cells, column, access_row);
        }
    }
#ifdef GRIDW11_RETURN
    total += 17;
#endif
    return total;
}

static void mix_result(int value)
{
    oracle_hash =
        (oracle_hash ^ (unsigned int)value) * 16777619UL;
}

static int guards_intact(const struct GuardedGrid *grid)
{
    int index;

    for (index = 0; index < GUARD_WORDS; ++index)
        if (grid->before[index] != GUARD_VALUE ||
            grid->after[index] != GUARD_VALUE)
            return 0;
    return 1;
}

static void fill_grid(struct GuardedGrid *grid, int pattern)
{
    int index;

    for (index = 0; index < GUARD_WORDS; ++index) {
        grid->before[index] = GUARD_VALUE;
        grid->after[index] = GUARD_VALUE;
    }
    for (index = 0; index < STORAGE_CELLS; ++index) {
        if (pattern == 0)
            grid->cells[index] = index % 13 - 6;
        else if (pattern == 1)
            grid->cells[index] =
                ((index / ACTIVE_STRIDE) & 1) ? 9 - index % 7
                                              : index % 7 - 9;
        else
            grid->cells[index] =
                index % 11 == 0 ? 31 : (index % 5 == 0 ? -17 : 0);
    }
}

static void run_pattern(int pattern)
{
    struct GuardedGrid grid;
    int x;
    int y;

    fill_grid(&grid, pattern);
    grid_wave11_table = grid.cells;
    for (y = 0; y < DIMENSION; ++y)
        for (x = 0; x < DIMENSION; ++x) {
            int expected =
                reference_grid_wave11(grid.cells, x, y);
            int actual = grid_wave11(x, y);

            ++checks;
            if (actual != expected)
                ++failures;
            mix_result(actual);
        }
    ++checks;
    if (!guards_intact(&grid) ||
        grid_wave11_table != grid.cells)
        ++failures;
}

int main(void)
{
    run_pattern(0);
    run_pattern(1);
    run_pattern(2);
    printf("GRIDW11 failures=%d checks=%u guards=%d hash=%lu\n",
           failures, checks, failures == 0, oracle_hash);
    return failures != 0;
}
