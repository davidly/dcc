#include <stdio.h>
#include <math.h>

int main(void)
{
    float pinf = INFINITY;
    float ninf = -INFINITY;
    float nan = NAN;
    float nnan = -NAN;

    printf("[%f]\n", pinf);
    printf("[%f]\n", ninf);
    printf("[%f]\n", nan);
    printf("[%f]\n", nnan);
    printf("[%10f]\n", pinf);
    printf("[%-10f]\n", pinf);
    printf("[%010f]\n", ninf);
    printf("[%.2f]\n", pinf);
    printf("[%.0f]\n", nan);
    return 0;
}
