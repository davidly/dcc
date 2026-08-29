/* Multi-file CP/M binary I/O around both sides of a 128-byte record boundary,
 * plus deterministic missing-file and removal failure paths. */
#include <stdio.h>
#include <string.h>
#include "tfileapp.h"

int main(void)
{
    unsigned char expected[TF_BYTES];
    unsigned char actual[TF_BYTES];
    unsigned long expected_sum;
    unsigned long actual_sum;

    remove("TFDATA.BIN");
    tf_fill_pattern(expected, TF_BYTES);
    memset(actual, 0, sizeof(actual));

    if (!tf_write_file("TFDATA.BIN", expected, TF_BYTES) ||
        !tf_read_file("TFDATA.BIN", actual, TF_BYTES) ||
        memcmp(expected, actual, TF_BYTES) != 0 ||
        !tfseek_check_window("TFDATA.BIN", expected, 126, 7) ||
        !tfseek_check_window("TFDATA.BIN", expected, 255, 9) ||
        !tferr_check_paths()) {
        printf("tfileapp failed\n");
        remove("TFDATA.BIN");
        return 1;
    }

    expected_sum = tfsum_checksum(expected, TF_BYTES);
    actual_sum = tfsum_checksum(actual, TF_BYTES);
    remove("TFDATA.BIN");
    if (expected_sum != actual_sum) {
        printf("tfileapp checksum failed\n");
        return 1;
    }
    printf("tfileapp completed with great success\n");
    return 0;
}
