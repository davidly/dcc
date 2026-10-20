#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint8_t name[11];
    uint16_t blocks;
} FileEntry;

static FileEntry files[64];
static FileEntry sort_temp;
static FileEntry *sort_left;
static uint8_t sort_pass;
static uint8_t sort_remaining;
static const uint16_t sizes[4] = { 0, 65535, 32768, 100 };

static int fail(const char *message)
{
    puts(message);
    return 1;
}

static void szsort(FileEntry *base, uint8_t count)
{
    if (count < 2)
        return;
    sort_pass = count - 1;
    while (sort_pass != 0) {
        sort_left = base;
        sort_remaining = sort_pass;
        do {
            if (sort_left->blocks < sort_left[1].blocks) {
                sort_temp = *sort_left;
                *sort_left = sort_left[1];
                sort_left[1] = sort_temp;
            }
            ++sort_left;
        } while (--sort_remaining != 0);
        --sort_pass;
    }
}

int main(void)
{
    uint8_t count;
    uint8_t index;
    uint8_t byte;
    uint8_t original;

    for (count = 0; count <= 64; ++count) {
        for (index = 0; index < 64; ++index) {
            for (byte = 0; byte < 11; ++byte)
                files[index].name[byte] = index + byte;
            files[index].blocks = sizes[index & 3];
        }
        szsort(files, count);
        for (index = 0; index < 64; ++index) {
            original = files[index].name[0];
            if ((index < count && original >= count) ||
                (index >= count && original != index) ||
                files[index].blocks != sizes[original & 3])
                return fail("FAIL record");
            for (byte = 0; byte < 11; ++byte)
                if (files[index].name[byte] != original + byte)
                    return fail("FAIL bytes");
            if (index != 0 && index < count &&
                (files[index - 1].blocks < files[index].blocks ||
                 (files[index - 1].blocks == files[index].blocks &&
                  files[index - 1].name[0] >= original)))
                return fail("FAIL order");
        }
    }
    puts("tszswap completed with great success");
    return 0;
}
