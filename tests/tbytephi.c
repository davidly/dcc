#include <stdio.h>

static unsigned char values[4];

struct Entry {
    char name[16];
    char disk;
    int size;
};

static struct Entry entries[2];

static int byte_phi_loop(void)
{
    unsigned char index;
    int sum = 0;

    for (index = 0; index < 4; ++index) {
        values[index] = (unsigned char)(index + 10);
        sum += values[index];
    }
    return sum + index;
}

static void set_disk(int index)
{
    entries[index].disk = 'C';
    entries[index].size = -1;
}

int main(void)
{
    int result = byte_phi_loop();

    set_disk(0);
    if (result != 50 || values[0] != 10 || values[3] != 13) {
        printf("tbytephi FAIL result=%d first=%u last=%u\n",
               result, values[0], values[3]);
        return 1;
    }
    if (entries[0].disk != 'C') {
        printf("tbytephi FAIL disk=%d\n", entries[0].disk);
        return 1;
    }
    if (entries[0].size != -1) {
        printf("tbytephi FAIL size=%d\n", entries[0].size);
        return 1;
    }
    puts("tbytephi PASS");
    return 0;
}
