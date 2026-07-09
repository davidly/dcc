// tvlaparm (was c9904): regression - C99 VLA function parameters `T p[n]`
// (n a preceding parameter) must be accepted and decay to `T *p`; dcc
// previously rejected the `[n]` declarator with DCC-E0401/E1106.
// Scenario: variable-length arrays for a runtime-sized sliding-window filter.
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
    return 0;
}
