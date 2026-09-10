#include <stdint.h>
#include <stdio.h>

#ifdef VLA18_VOLATILE
#define VLA18_SOURCE volatile
#else
#define VLA18_SOURCE
#endif

static int32_t vla18_smooth(
    int n, int w, const VLA18_SOURCE int16_t src[n], int16_t dst[n])
{
    int32_t changed = 0;
    int half = w / 2;

    for (int i = 0; i < n; i++) {
        int32_t sum = 0;
        int count = 0;
#ifdef VLA18_NEAR
        for (int j = i - half; j < i + half + 1; j++) {
#else
        for (int j = i - half; j <= i + half; j++) {
#endif
            if (j >= 0 && j < n) {
                sum += src[j];
                count++;
            }
        }
        dst[i] = (int16_t)(sum / count);
        if (dst[i] != src[i])
            changed++;
    }
    return changed;
}

static int32_t vla18_reference(
    int n, int w, const VLA18_SOURCE int16_t *src, int16_t *dst)
{
    int32_t changed = 0;
    int half = w / 2;
    int i = 0;

    while (i < n) {
        int32_t sum = 0;
        int count = 0;
        int j = i - half;

        while (j <= i + half) {
            if (j >= 0) {
                if (j < n) {
                    sum = sum + src[j];
                    count = count + 1;
                }
            }
            j = j + 1;
        }
        dst[i] = (int16_t)(sum / count);
        if (dst[i] != src[i])
            changed = changed + 1;
        i = i + 1;
    }
    return changed;
}

static int run_case(
    int n, int w, int alias_mode, unsigned long *checksum)
{
    int16_t actual[32];
    int16_t expected[32];
    int src_offset;
    int dst_offset;
    int32_t actual_changed;
    int32_t expected_changed;
    int item;

    for (item = 0; item < 32; ++item) {
        int value = ((item * 97 + n * 31 + w * 13) % 4001) - 2000;

        actual[item] = (int16_t)value;
        expected[item] = (int16_t)value;
    }
    if (alias_mode == 0) {
        src_offset = 0;
        dst_offset = 16;
    } else if (alias_mode == 1) {
        src_offset = 4;
        dst_offset = 4;
    } else if (alias_mode == 2) {
        src_offset = 4;
        dst_offset = 5;
    } else {
        src_offset = 5;
        dst_offset = 4;
    }
    actual_changed = vla18_smooth(
        n, w, actual + src_offset, actual + dst_offset);
    expected_changed = vla18_reference(
        n, w, expected + src_offset, expected + dst_offset);
    if (actual_changed != expected_changed)
        return 0;
    for (item = 0; item < 32; ++item)
        if (actual[item] != expected[item])
            return 0;
    *checksum = *checksum * 33L + actual_changed;
    for (item = 0; item < 32; ++item)
        *checksum = *checksum * 33L + actual[item];
    return 1;
}

static int vla18_stride_sum(
    int rows, int columns, int16_t values[rows][columns])
{
    int row;
    int column;
    int total = 0;

    for (row = 0; row < rows; ++row)
        for (column = 0; column < columns; ++column)
            total += values[row][column];
    return total;
}

static int vla18_stride(void)
{
    int16_t matrix[3][5];
    int row;
    int column;

    for (row = 0; row < 3; ++row)
        for (column = 0; column < 5; ++column)
            matrix[row][column] = (int16_t)(row * 100 + column * 7);
    return vla18_stride_sum(3, 5, matrix);
}

static int vla18_restore(int rounds)
{
    int checksum = 0;
    int round;

    for (round = 1; round <= rounds; ++round) {
        int count = round + 2;
        int16_t values[count];
        int item;

        for (item = 0; item < count; ++item)
            values[item] = (int16_t)(round * 10 + item);
        checksum += values[0] + values[count - 1];
    }
    return checksum;
}

int main(void)
{
    static const int dimensions[] = {1, 2, 3, 5, 9, 12};
    static const int windows[] = {-1, 1, 2, 3, 4, 5};
    unsigned long checksum = 0;
    int failures = 0;
    int dimension;
    int window;
    int alias_mode;

    for (dimension = 0;
         dimension < (int)(sizeof(dimensions) / sizeof(dimensions[0]));
         ++dimension)
        for (window = 0;
             window < (int)(sizeof(windows) / sizeof(windows[0]));
             ++window)
            for (alias_mode = 0; alias_mode < 4; ++alias_mode)
                if (!run_case(
                        dimensions[dimension], windows[window],
                        alias_mode, &checksum))
                    ++failures;
    if (vla18_stride() != 1710)
        ++failures;
    if (vla18_restore(6) != 447)
        ++failures;
    printf("vla18 failures=%d checksum=%lu stride=%d restore=%d\n",
           failures, checksum, vla18_stride(), vla18_restore(6));
    return failures != 0;
}
