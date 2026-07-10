// tvlaparm (was c9904): regression - C99 VLA function parameters `T p[n]`
// (n a preceding parameter) must be accepted and decay to `T *p`; dcc
// previously rejected the `[n]` declarator with DCC-E0401/E1106.
// Scenario: variable-length arrays for a runtime-sized sliding-window filter.
//
// Also covers 2-D VLA parameters `T p[rows][cols]` with a run-time INNER
// (column) dimension.  The inner bound sets the row stride, so p[r][c] must
// address base + (r*cols + c)*sizeof(T).  dcc previously rejected the run-time
// inner dimension with DCC-E0601 ("variable inner dimensions ... not
// supported").  Only single-identifier inner bounds on a 2-D parameter are
// representable; expression bounds and 3-D+ arrays stay rejected by design.
#include <stdio.h>
#include <stdint.h>

// Compute a centered moving average (integer) over n samples, window w.
static int32_t smooth(int n, int w, const int16_t src[n], int16_t dst[n])
{
    int32_t changed = 0;
    int half = w / 2;

    for (int i = 0; i < n; i++) {
        int32_t sum = 0;
        int count = 0;
        for (int j = i - half; j <= i + half; j++) {
            if (j >= 0 && j < n) {
                sum += src[j];
                count++;
            }
        }
        dst[i] = (int16_t)(sum / count);
        if (dst[i] != src[i]) changed++;
    }
    return changed;
}

// 2-D VLA parameter (run-time row stride): sum every element of an
// rows-by-cols integer matrix.  A swapped or constant stride would read the
// wrong elements for a non-square shape.
static int grid_sum(int rows, int cols, int g[rows][cols])
{
    int r, c, t = 0;
    for (r = 0; r < rows; r++)
        for (c = 0; c < cols; c++)
            t += g[r][c];
    return t;
}

// 2-D VLA parameter written through (the store address path also depends on
// the run-time row stride).
static void grid_fill(int rows, int cols, int g[rows][cols])
{
    int r, c, v = 1;
    for (r = 0; r < rows; r++)
        for (c = 0; c < cols; c++)
            g[r][c] = v++;
}

// char element type exercises a 1-byte element stride (row stride = cols).
static int cgrid_sum(int rows, int cols, char g[rows][cols])
{
    int r, c, t = 0;
    for (r = 0; r < rows; r++)
        for (c = 0; c < cols; c++)
            t += g[r][c];
    return t;
}

int main(void)
{
    int n = 12;
    int16_t samples[12] = { 10, 40, 12, 38, 15, 35, 18, 33, 20, 30, 22, 28 };
    int16_t out[12]; // fixed here, but smooth() treats them as VLAs of size n

    int32_t changed = smooth(n, 3, samples, out);

    int32_t total = 0;
    for (int i = 0; i < n; i++) total += out[i];

    printf("c9904 changed=%ld first=%d last=%d avg=%ld\n",
           (long)changed, (int)out[0], (int)out[n - 1], (long)(total / n));

    // 2-D VLA parameters: non-square 4x3 int matrix, filled row-major 1..12.
    int m[4][3];
    grid_fill(4, 3, m);
    printf("grid sum=%d e32=%d e20=%d\n",
           grid_sum(4, 3, m), m[3][2], m[2][0]);

    // 2-D VLA parameter with a 1-byte (char) element stride.
    char cm[2][5] = { { 1, 2, 3, 4, 5 }, { 6, 7, 8, 9, 10 } };
    printf("cgrid sum=%d\n", cgrid_sum(2, 5, cm));
    return 0;
}
