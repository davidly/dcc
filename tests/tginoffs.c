/*
 * tginoffs.c - constant-expression pointer offsets in global aggregates.
 *
 * Relocatable global addresses may use an integer constant expression, not
 * just one literal token, and the offset is scaled by the pointed-to element
 * size.  dcc previously stopped after the first number.  Found via z88dk's
 * Issue_1409_offset_pointer_initialisation.c regression test.
 */
#include <stdio.h>

static unsigned char bytes[16];
static unsigned char *byte_frames[] = {
    bytes + 4,
    bytes - 4 * 2,
    bytes + (4 * 0),
    bytes + (4 * 3)
};

static unsigned int words[8];
static unsigned int *word_frames[] = {
    words + (2 * 0),
    words + (2 * 1),
    words + (2 * 3)
};

static int fails;

static void check(const char *name, int condition)
{
    if (!condition) {
        printf("FAIL %s\n", name);
        ++fails;
    }
}

int main(void)
{
    fails = 0;
    check("byte literal", byte_frames[0] == bytes + 4);
    check("byte grouped zero", byte_frames[2] == bytes);
    check("byte grouped product", byte_frames[3] == bytes + 12);
    check("word grouped zero", word_frames[0] == words);
    check("word scaling one", word_frames[1] == words + 2);
    check("word scaling product", word_frames[2] == words + 6);

    if (fails) {
        printf("tginoffs failed: %d\n", fails);
        return 1;
    }
    printf("tginoffs completed with great success\n");
    return 0;
}
