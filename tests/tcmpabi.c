/* Array decay must retain prototype conversions at indirect calls. */
#include <stdio.h>
typedef int (*Comparator)(const void *, const void *);
static int cmp_int_asc(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }
static void insertion_sort(void *base, int n, int size, Comparator cmp) {
    char *arr = (char *)base;
    for (int i = 1; i < n; i++) {
        char tmp[8]; int k;
        for (k = 0; k < size; k++) tmp[k] = arr[i*size+k];
        int j = i - 1;
        while (j >= 0 && cmp(arr + j*size, tmp) > 0) {
            for (k = 0; k < size; k++) arr[(j+1)*size+k] = arr[j*size+k];
            j--;
        }
        for (k = 0; k < size; k++) arr[(j+1)*size+k] = tmp[k];
    }
}
int main(void) {
    int a[6] = {5, 2, 8, 1, 9, 3};
    insertion_sort(a, 6, sizeof(int), cmp_int_asc);
    printf("tcmpabi isort=%d,%d,%d\n", a[0], a[2], a[5]);
    return !(a[0]==1 && a[2]==3 && a[5]==9);
}
