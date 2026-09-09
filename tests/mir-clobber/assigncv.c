#include <stdio.h>

struct Box {
    long wide;
    float real;
    int word;
    char byte;
    long wides[2];
    float reals[2];
    int words[2];
    char bytes[2];
    int *pointers[2];
};

static long wide_values[10];
static float real_values[6];
static int matrix[2][3];
static int pointer_matrix_storage[2][3];
static long wide_rows[1][2];
static float real_rows[1][2];
static int words[4];
static int *pointers[2];
static int *pointer_rows[1][2];
static struct Box box;

static void check(int condition, const char *name, int *failures)
{
    if (!condition) {
        printf("FAIL %s\n", name);
        ++*failures;
    }
}

int main(void)
{
    int failures = 0;
    int *word_pointer = words;
    int (*matrix_pointer)[3] = pointer_matrix_storage;
    long (*wide_pointer)[2] = wide_rows;
    float (*real_pointer)[2] = real_rows;
    struct Box *box_pointer = &box;

    wide_values[0] = 7;
    wide_values[1] = 3.75f;
    wide_values[2] = 2;
    wide_values[2] <<= 3L;
    wide_values[3] = 20L;
    wide_values[3] += 4;
    wide_values[3] -= 2L;
    wide_values[3] *= 3;
    wide_values[3] /= 2L;
    wide_values[3] %= 17;
    wide_values[3] &= 15L;
    wide_values[3] |= 32;
    wide_values[3] ^= 3L;
    wide_values[3] >>= 1;

    real_values[0] = 2.5f;
    real_values[1] = 3;
    real_values[2] = 4L;
    real_values[3] = 10.0f;
    real_values[3] += 2;
    real_values[3] -= 1L;
    real_values[3] *= 2.0f;
    real_values[3] /= 3;

    matrix[1][2] = 7;
    matrix[1][2] += 3;
    (*matrix_pointer)[1] = 12;
    (*wide_pointer)[0] = 19;
    (*real_pointer)[1] = 20L;
    word_pointer[1] = 11;
    pointers[0] = &words[1];
    pointer_rows[0][1] = &words[1];

    box.wide = 9;
    box.wide += 5L;
    box.wide <<= 1;
    box.real = 1L;
    box.real += 2.5f;
    box.word = 13;
    box.byte = 14;
    box.wides[1] = 15;
    box.reals[1] = 16L;
    box.words[1] += 17;
    box.bytes[1] = 18;
    box.pointers[1] = &matrix[1][2];
    box_pointer->wide -= 4;
    box_pointer->real *= 2;

    check(wide_values[0] == 7, "wide assign", &failures);
    check(wide_values[1] == 3, "wide float assign", &failures);
    check(wide_values[2] == 16, "wide shift", &failures);
    check(wide_values[3] == 17, "wide compounds", &failures);
    check((long)real_values[0] == 2, "float assign", &failures);
    check((long)real_values[1] == 3, "float int assign", &failures);
    check((long)real_values[2] == 4, "float long assign", &failures);
    check((long)real_values[3] == 7, "float compounds", &failures);
    check(matrix[1][2] == 10, "matrix compound", &failures);
    check(pointer_matrix_storage[0][1] == 12 &&
          wide_rows[0][0] == 19 && (long)real_rows[0][1] == 20,
          "pointer to array assignments", &failures);
    check(words[1] == 11 && pointers[0] == &words[1],
          "pointer assignments", &failures);
    check(pointer_rows[0][1] == &words[1],
          "multidimensional pointer assignment", &failures);
    check(box.wide == 24 && (long)box.real == 7,
          "member compounds", &failures);
    check(box.word == 13 && box.byte == 14,
          "member assignments", &failures);
    check(box.wides[1] == 15 && (long)box.reals[1] == 16,
          "member arrays", &failures);
    check(box.words[1] == 17 && box.bytes[1] == 18,
          "member array compounds", &failures);
    check(box.pointers[1] == &matrix[1][2],
          "member pointer array", &failures);

    printf("assignment coverage failures=%d\n", failures);
    return failures != 0;
}
