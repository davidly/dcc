#include <stdio.h>
#include "tfileapp.h"

int tf_write_file(const char *name, const unsigned char *buffer, int length)
{
    FILE *file = fopen(name, "wb");
    int first;
    int second;

    if (file == NULL)
        return 0;
    first = fwrite(buffer, 1, 127, file);
    second = fwrite(buffer + 127, 1, length - 127, file);
    if (first != 127 || second != length - 127 || fclose(file) != 0)
        return 0;
    return 1;
}

int tf_read_file(const char *name, unsigned char *buffer, int length)
{
    FILE *file = fopen(name, "rb");
    int first;
    int middle;
    int last;

    if (file == NULL)
        return 0;
    first = fread(buffer, 1, 127, file);
    middle = fread(buffer + 127, 1, 2, file);
    last = fread(buffer + 129, 1, length - 129, file);
    fclose(file);
    return first == 127 && middle == 2 && last == length - 129;
}

int tfseek_check_window(const char *name,
                        const unsigned char *expected, int offset, int length)
{
    FILE *file = fopen(name, "rb");
    unsigned char window[12];
    int index;

    if (file == NULL || length > (int)sizeof(window))
        return 0;
    if (fseek(file, offset, SEEK_SET) != 0 || ftell(file) != offset ||
        fread(window, 1, length, file) != length) {
        fclose(file);
        return 0;
    }
    fclose(file);
    for (index = 0; index < length; ++index)
        if (window[index] != expected[offset + index])
            return 0;
    return 1;
}

int tferr_check_paths(void)
{
    FILE *file;

    remove("NOFILE.BIN");
    file = fopen("NOFILE.BIN", "rb");
    if (file != NULL) {
        fclose(file);
        return 0;
    }
    return remove("NOFILE.BIN") != 0;
}
