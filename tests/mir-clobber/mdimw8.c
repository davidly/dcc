#include <stdio.h>

struct Mat {
    unsigned char cell[3][4];
    int r;
    int c;
};

struct IMat {
    int v[3][4];
    int r;
    int c;
};

struct Cube {
    int data[2][3][4];
    int a;
    int b;
    int d;
};

struct Cell {
    unsigned char g[2][2];
    int tag;
};

struct Grid {
    struct Cell cells[3];
};

static unsigned char guard0[8];
static struct Mat mx;
static unsigned char guard1[8];
static struct IMat im;
static unsigned char guard2[8];
static struct Cube cu;
static unsigned char guard3[8];
static struct Grid gr;
static unsigned char guard4[8];

static int failures;

static void check(const char *name, int got, int expected)
{
    if (got != expected) {
        printf("FAIL %s got %d expected %d\n", name, got, expected);
        failures++;
    }
}

static int row_of(void) { return mx.r; }
static int col_of(void) { return mx.c; }

int multidim_wave8(void)
{
    int i, j, k;

    mx.cell[0][0] = 1;
    mx.cell[1][0] = 15;
    mx.cell[2][3] = 42;
    mx.cell[1][2] = 99;

    check("char c[0][0]", (int)mx.cell[0][0], 1);
    check("char c[1][0]", (int)mx.cell[1][0], 15);
    check("char c[2][3]", (int)mx.cell[2][3], 42);
    check("char c[1][2]", (int)mx.cell[1][2], 99);

    for (i = 0; i < 3; i++)
        for (j = 0; j < 4; j++)
            mx.cell[i][j] = (unsigned char)(i * 4 + j);
    check("char loopfill[2][3]", (int)mx.cell[2][3], 11);
    check("char loopfill[1][1]", (int)mx.cell[1][1], 5);

    mx.r = 2; mx.c = 1;
    mx.cell[mx.r][mx.c] = 77;
    check("char member-idx const-read", (int)mx.cell[2][1], 77);
    check("char member-idx var-read", (int)mx.cell[mx.r][mx.c], 77);

    mx.r = 1; mx.c = 3;
    mx.cell[row_of()][col_of()] = 55;
    check("char fncall-idx", (int)mx.cell[1][3], 55);

    im.v[0][0] = 1000;
    im.v[1][0] = 2000;
    im.v[2][3] = 3003;
    im.v[1][2] = 1202;

    check("int v[0][0]", im.v[0][0], 1000);
    check("int v[1][0]", im.v[1][0], 2000);
    check("int v[2][3]", im.v[2][3], 3003);
    check("int v[1][2]", im.v[1][2], 1202);

    im.r = 2; im.c = 2;
    im.v[im.r][im.c] = 4242;
    check("int member-idx const-read", im.v[2][2], 4242);
    check("int member-idx var-read", im.v[im.r][im.c], 4242);

    for (i = 0; i < 2; i++)
        for (j = 0; j < 3; j++)
            for (k = 0; k < 4; k++)
                cu.data[i][j][k] = i * 100 + j * 10 + k;

    check("3d d[0][0][0]", cu.data[0][0][0], 0);
    check("3d d[1][2][3]", cu.data[1][2][3], 123);
    check("3d d[1][0][0]", cu.data[1][0][0], 100);
    check("3d d[0][2][1]", cu.data[0][2][1], 21);

    cu.a = 1; cu.b = 2; cu.d = 3;
    check("3d member-idx", cu.data[cu.a][cu.b][cu.d], 123);

    gr.cells[0].g[0][0] = 10;
    gr.cells[1].g[1][0] = 21;
    gr.cells[2].g[0][1] = 32;
    gr.cells[1].g[1][1] = 23;

    check("nest c0 g[0][0]", (int)gr.cells[0].g[0][0], 10);
    check("nest c1 g[1][0]", (int)gr.cells[1].g[1][0], 21);
    check("nest c2 g[0][1]", (int)gr.cells[2].g[0][1], 32);
    check("nest c1 g[1][1]", (int)gr.cells[1].g[1][1], 23);

    if (failures) {
        printf("FAIL multidim_array (%d failures)\n", failures);
        return 1;
    }
    printf("PASS multidim_array\n");
    return 0;
}

static void fill_guard(unsigned char *guard, int value)
{
    int i;

    for (i = 0; i < 8; ++i)
        guard[i] = (unsigned char)value;
}

static void check_guard(const unsigned char *guard, int value)
{
    int i;

    for (i = 0; i < 8; ++i)
        if (guard[i] != (unsigned char)value)
            ++failures;
}

static unsigned long hash_bytes(
    unsigned long hash, const unsigned char *bytes, unsigned int count)
{
    unsigned int i;

    for (i = 0; i < count; ++i)
        hash = (hash ^ bytes[i]) * 16777619UL;
    return hash;
}

static void verify_data(void)
{
    int i;
    int j;
    int k;

    for (i = 0; i < 3; ++i)
        for (j = 0; j < 4; ++j) {
            int expected = i * 4 + j;

            if (i == 2 && j == 1)
                expected = 77;
            if (i == 1 && j == 3)
                expected = 55;
            if (mx.cell[i][j] != (unsigned char)expected)
                ++failures;
        }
    if (mx.r != 1 || mx.c != 3)
        ++failures;

    for (i = 0; i < 3; ++i)
        for (j = 0; j < 4; ++j) {
            int expected = 0;

            if (i == 0 && j == 0)
                expected = 1000;
            else if (i == 1 && j == 0)
                expected = 2000;
            else if (i == 1 && j == 2)
                expected = 1202;
            else if (i == 2 && j == 2)
                expected = 4242;
            else if (i == 2 && j == 3)
                expected = 3003;
            if (im.v[i][j] != expected)
                ++failures;
        }
    if (im.r != 2 || im.c != 2)
        ++failures;

    for (i = 0; i < 2; ++i)
        for (j = 0; j < 3; ++j)
            for (k = 0; k < 4; ++k)
                if (cu.data[i][j][k] != i * 100 + j * 10 + k)
                    ++failures;
    if (cu.a != 1 || cu.b != 2 || cu.d != 3)
        ++failures;

    for (i = 0; i < 3; ++i) {
        for (j = 0; j < 2; ++j)
            for (k = 0; k < 2; ++k) {
                int expected = 0;

                if (i == 0 && j == 0 && k == 0)
                    expected = 10;
                else if (i == 1 && j == 1 && k == 0)
                    expected = 21;
                else if (i == 1 && j == 1 && k == 1)
                    expected = 23;
                else if (i == 2 && j == 0 && k == 1)
                    expected = 32;
                if (gr.cells[i].g[j][k] != (unsigned char)expected)
                    ++failures;
            }
        if (gr.cells[i].tag != 0)
            ++failures;
    }
}

int main(void)
{
    unsigned long hash = 2166136261UL;
    int result;

    fill_guard(guard0, 0x11);
    fill_guard(guard1, 0x33);
    fill_guard(guard2, 0x55);
    fill_guard(guard3, 0x77);
    fill_guard(guard4, 0x99);
    result = multidim_wave8();
    if (result != 0)
        ++failures;
    check_guard(guard0, 0x11);
    check_guard(guard1, 0x33);
    check_guard(guard2, 0x55);
    check_guard(guard3, 0x77);
    check_guard(guard4, 0x99);
    verify_data();
    hash = hash_bytes(hash, (const unsigned char *)&mx, sizeof(mx));
    hash = hash_bytes(hash, (const unsigned char *)&im, sizeof(im));
    hash = hash_bytes(hash, (const unsigned char *)&cu, sizeof(cu));
    hash = hash_bytes(hash, (const unsigned char *)&gr, sizeof(gr));
    printf("MDIMW8 oracle failures=%d hash=%lu guards=%u,%u,%u,%u,%u\n",
           failures, hash, guard0[0], guard1[7], guard2[0],
           guard3[7], guard4[0]);
    return failures != 0;
}
