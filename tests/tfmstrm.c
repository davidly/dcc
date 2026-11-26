/* Single-use indirect float operands should stream to the multiply-add
 * helper without frame spills. Exercise local/global arrays, repeated
 * calls, aliasing, and calls/control flow that must keep the fallback. */
#include <stdio.h>

float aa[3] = { 2.0f, -3.0f, 1.5f };
float bb[3] = { 4.0f, 2.0f, 2.0f };
float cc[3] = { 1.0f, 10.0f, -2.0f };
int calls;

float factor(int i)
{
    calls++;
    return bb[i];
}

void accumulate(float *a, float *b, float *c, int i)
{
    c[i] += a[i] * b[i];
}

float conditional(int i, int flag)
{
    return cc[i] + aa[i] * (flag ? bb[i] : bb[0]);
}

int main(void)
{
    int i;
    float local[3];
    for (i = 0; i < 3; ++i) {
        accumulate(aa, bb, cc, i);
        local[i] = cc[i];
    }
    for (i = 0; i < 3; ++i)
        local[i] += aa[i] * bb[i];
    printf("global %.2f %.2f %.2f\n", cc[0], cc[1], cc[2]);
    printf("local %.2f %.2f %.2f\n", local[0], local[1], local[2]);
    /* Destination aliases both a factor and the addend. */
    accumulate(aa, bb, aa, 1);
    printf("alias %.2f\n", aa[1]);
    cc[2] += aa[2] * factor(2);
    printf("call %.2f %d\n", cc[2], calls);
    printf("branch %.2f %.2f\n", conditional(0, 0), conditional(0, 1));
    return 0;
}
