/* Row-pair IY reuse must only pair arrays that really are globals of this
 * module. Each local or static array below shares its name with a global
 * array, in the loop shape that pairs rows: C[i][j] += A[i][j] * B[i][j]
 * pairs C's row with A's. A local named like a global must not be addressed
 * through the global's label. */
#include <stdio.h>

int A[2][20], B[2][20], C[2][20];

static void clear(void)
{
    int i, j;
    for (i = 0; i < 2; i++) for (j = 0; j < 20; j++) C[i][j] = 0;
}

static int total(void)
{
    int i, j, t = 0;
    for (i = 0; i < 2; i++) for (j = 0; j < 20; j++) t += C[i][j];
    return t;
}

/* a local shadows the second array of the pair */
int second_local(void)
{
    int A[2][20];
    int i, j;
    for (i = 0; i < 2; i++) for (j = 0; j < 20; j++) A[i][j] = 3;
    for (i = 0; i < 2; i++) for (j = 0; j < 20; j++) C[i][j] += A[i][j] * B[i][j];
    return total();
}

/* a local shadows the first array of the pair */
int first_local(void)
{
    int C[2][20];
    int i, j, t = 0;
    for (i = 0; i < 2; i++) for (j = 0; j < 20; j++) C[i][j] = 1;
    for (i = 0; i < 2; i++) for (j = 0; j < 20; j++) C[i][j] += A[i][j] * B[i][j];
    for (i = 0; i < 2; i++) for (j = 0; j < 20; j++) t += C[i][j];
    return t;
}

/* both arrays of the pair are locals named like globals */
int both_local(void)
{
    int C[2][20], A[2][20];
    int i, j, t = 0;
    for (i = 0; i < 2; i++) for (j = 0; j < 20; j++) { C[i][j] = 2; A[i][j] = 4; }
    for (i = 0; i < 2; i++) for (j = 0; j < 20; j++) C[i][j] += A[i][j] * B[i][j];
    for (i = 0; i < 2; i++) for (j = 0; j < 20; j++) t += C[i][j];
    return t;
}

/* a static local shadows the second array of the pair */
int second_static(void)
{
    static int A[2][20];
    int i, j;
    for (i = 0; i < 2; i++) for (j = 0; j < 20; j++) A[i][j] = 5;
    for (i = 0; i < 2; i++) for (j = 0; j < 20; j++) C[i][j] += A[i][j] * B[i][j];
    return total();
}

/* the globals themselves, which may still pair */
int globals(void)
{
    int i, j;
    for (i = 0; i < 2; i++) for (j = 0; j < 20; j++) C[i][j] += A[i][j] * B[i][j];
    return total();
}

int main(void)
{
    int i, j;
    for (i = 0; i < 2; i++) for (j = 0; j < 20; j++) { A[i][j] = 1; B[i][j] = 2; }
    clear();
    printf("second_local %d\n", second_local());
    clear();
    printf("first_local %d\n", first_local());
    printf("both_local %d\n", both_local());
    clear();
    printf("second_static %d\n", second_static());
    clear();
    printf("globals %d\n", globals());
    printf("global C %d\n", total());
    return 0;
}
