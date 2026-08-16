/*
 * tforhex.c - for-init declaration with a compound condition.
 * Generated archive case: batch11/c9926.
 */
#include <stdint.h>
#include <stdio.h>

static void hex_dump(const uint8_t *data, int length, char *out, int capacity)
{
    static const char hex[] = "0123456789ABCDEF";
    int position = 0;

    for (int index = 0;
         index < length && position + 2 < capacity;
         ++index) {
        out[position++] = hex[data[index] >> 4];
        out[position++] = hex[data[index] & 0x0fU];
    }
    out[position] = '\0';
}

int main(void)
{
    uint8_t data[4] = { 0xde, 0xad, 0xbe, 0xef };
    char out[16];
    int ok;

    hex_dump(data, 4, out, sizeof(out));
    printf("tforhex hexdump=%s\n", out);
    ok = out[0] == 'D' && out[1] == 'E' &&
         out[6] == 'E' && out[7] == 'F';
    if (ok)
        printf("tforhex passed with great success\n");
    return !ok;
}
