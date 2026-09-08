#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#define _countof(a) (sizeof(a) / sizeof((a)[0]))
static uint8_t memory[256];
extern uint8_t *get_mem();
#define set_byte(addr, value) *(uint8_t *)get_mem(addr) = value

static void usage(err) char *err;
{
    printf("FAIL %s\n", err);
    exit(2);
}

uint8_t *get_mem(address) uint16_t address;
{
    return &memory[address & 255];
}

uint16_t hextoui(p) char *p;
{
    char c;
    uint16_t i, result;
    result = 0;
    while (' ' == *p)
        p++;
    while (c = *p) {
        if (c >= '0' && c <= '9')
            i = c - '0';
        else if (c >= 'a' && c <= 'f')
            i = 10 + c - 'a';
        else if (c >= 'A' && c <= 'F')
            i = 10 + c - 'A';
        else
            break;
        result = result * 16 + i;
        p++;
    }
    return result;
}

static uint16_t read_hex(p, len) char *p; uint8_t len;
{
    uint16_t result;
    char save;
    save = p[len];
    p[len] = 0;
    result = hextoui(p);
    p[len] = save;
    return result;
}

static bool load_intel(fp) FILE *fp;
{
    char *buf;
    uint8_t reclen, rectyp, x, val;
    uint16_t offset;
    char acLine[120];
    for (;;) {
        buf = fgets(acLine, _countof(acLine), fp);
        if (buf && strlen(buf) >= 11) {
            if (':' != buf[0])
                usage("error: input Intel HEX file is malformed");
            reclen = (uint8_t)read_hex(buf + 1, 2);
            offset = read_hex(buf + 3, 4);
            rectyp = (uint8_t)read_hex(buf + 7, 2);
            if (1 == rectyp)
                break;
            if (0 != rectyp)
                usage("file format not recognized");
            for (x = 0; x < reclen; x++) {
                if (feof(fp))
                    usage("malformed input file");
                val = (uint8_t)read_hex(buf + (2 * x) + 9, 2);
                set_byte(offset + x, val);
            }
        } else
            break;
    }
#ifdef MIR_CLOBBER_INTEL_EXTRA
    printf("intel extra control\n");
#endif
    fclose(fp);
    return true;
}

int main(void)
{
    FILE *fp = tmpfile();
    fputs(":0400000001020304F2\n:00000001FF\n", fp);
    rewind(fp);
    if (!load_intel(fp))
        return 1;
    printf("intel=%u,%u,%u,%u\n",
           memory[0], memory[1], memory[2], memory[3]);
    return 0;
}
